"""Golden test: the editor's plugin reader against tools/esm_dump.py, its reference (docs/EDITOR.md §5.2).

For the cell of every shipped template (tests/data/shipped_templates.txt), it runs
`DovaStashEditor.com --data <Data> --cell <cell>` and `python -I tools/esm_dump.py cell <cell>` and compares
the container references (with their respawn and initially-disabled flags), the displays and the doors to other
cells. Item plaques and cases esm_dump finds but the editor's stricter script check (the plugin's) rejects are
allowed; the reverse is a difference. A cell whose plugin is not in --data (a mod's, installed under MO2) is
skipped and not counted. Then the home finder (with --mo2, no --only): every one of those cells must be a home's
cell in `DovaStashEditor.com --mo2 <instance> [--profile <name>] --draft-all --base-game`, which reads the
profile's templates (a cell only a template makes a home, such as the Arch-Mage's Quarters, needs them). Exit 0
when every check agrees, 1 on any difference, 2 when it cannot run.

    python -I editor/tests/editor_golden.py --data "E:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data" \
        --mo2 "E:/modding/Kexodus Skyrim" --profile "Dova Stash Testing"

--data defaults to $DOVASTASH_SKYRIM_DATA, --mo2 and --profile to $DOVASTASH_MO2 and $DOVASTASH_MO2_PROFILE. Both
readers load the base game's masters plus the cell's own plugin (a Creation Club file), nothing else. Stdlib only.
"""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
BASE = {"skyrim.esm", "update.esm", "dawnguard.esm", "hearthfires.esm", "dragonborn.esm"}
REF = re.compile(r"^\s+(\S+\|0x[0-9A-Fa-f]{8})\s")


def norm(ref):
    plugin, _, local = ref.partition("|")
    return f"{plugin.lower()}|0x{int(local, 16):08X}"


def shipped_cells():
    cells = []
    listing = REPO / "tests" / "data" / "shipped_templates.txt"
    for line in listing.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        path = REPO / "dist" / "Data" / "SKSE" / "Plugins" / "DovaStash" / "Templates" / line.split()[0]
        if not path.is_file():
            continue  # a file merged into another (its list line is the data lane's to drop)
        cell = json.loads(path.read_text(encoding="utf-8")).get("cell")
        if cell and cell not in cells:
            cells.append(cell)
    return cells


def editor_view(exe, data, cell):
    plugin = cell.split("|")[0]
    args = [str(exe), "--data", str(data), "--cell", cell]
    if plugin.lower() not in BASE:
        args += ["--plugin", plugin]
    run = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if run.returncode != 0:
        raise RuntimeError(f"editor exit {run.returncode}: {run.stderr.strip()[:300]}")
    containers, displays, doors, section = {}, set(), set(), None
    for line in run.stdout.splitlines():
        if line and not line.startswith(" "):
            section = line.split(":")[0].split(" (")[0]
            continue
        m = REF.match(line)
        if not m:
            continue
        ref = norm(m.group(1))
        if section == "Containers":
            containers[ref] = ("RESPAWNS" in line or "respawn-flag" in line, "initially-disabled" in line)
        elif section == "Displays":
            displays.add(ref)
        elif section == "Doors to other cells":
            doors.add(ref)
    return containers, displays, doors


def python_view(data, cell):
    plugin = cell.split("|")[0]
    args = [sys.executable, "-I", str(REPO / "tools" / "esm_dump.py"), "--data", str(data), "--vanilla-only"]
    if plugin.lower() not in BASE:
        args += ["--add", plugin]
    args += ["--compact", "cell", cell]
    run = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if run.returncode != 0:
        raise RuntimeError(f"esm_dump exit {run.returncode}: {run.stderr.strip()[:300]}")
    d = json.loads(run.stdout)
    containers = {norm(c["ref"]): (bool(c.get("respawns")), bool(c.get("initiallyDisabled"))) for c in d.get("containers", [])}
    # Item plaques and cases: esm_dump takes any script naming an item; the editor, as the plugin, also wants the
    # script to keep a "placed" flag or a marker (plaques) or to take items in (cases), from the compiled scripts.
    # So those (Hearthfire's planters and crafting triggers among them) may be listed by esm_dump alone, never
    # by the editor alone: (the displays both must list, every display esm_dump lists).
    displays = {norm(x["ref"]) for x in d.get("displays", []) if x.get("kind") not in ("item", "case")}
    any_display = {norm(x["ref"]) for x in d.get("displays", [])}
    doors = {norm(x["ref"]) for x in d.get("doors", [])}
    return containers, (displays, any_display), doors


def home_cells(exe, mo2, profile):
    """Every cell of every home `--draft-all --base-game` lists (main and joined), the profile's templates read."""
    args = [str(exe), "--mo2", str(mo2)] + (["--profile", profile] if profile else []) + ["--draft-all", "--base-game"]
    run = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if run.returncode != 0:
        raise RuntimeError(f"editor exit {run.returncode}: {run.stderr.strip()[:300]}")
    cells = set()
    for line in run.stdout.splitlines():
        if line.strip().startswith("cells:"):
            cells.update(norm(m) for m in re.findall(r"\S+\|0x[0-9A-Fa-f]{8}", line))
    return cells


def compare(cell, ed, py):
    out = []
    must, may = py[1]
    for what, a, b, extra in (("containers", set(ed[0]), set(py[0]), set(py[0])), ("displays", ed[1], must, may), ("doors", ed[2], py[2], py[2])):
        if a - extra:
            out.append(f"  {what} only the editor lists: {', '.join(sorted(a - extra))}")
        if b - a:
            out.append(f"  {what} only esm_dump lists: {', '.join(sorted(b - a))}")
    for ref in sorted(set(ed[0]) & set(py[0])):
        if ed[0][ref] != py[0][ref]:
            out.append(f"  {ref}: editor (respawns, initially disabled) {ed[0][ref]}, esm_dump {py[0][ref]}")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", default=os.environ.get("DOVASTASH_SKYRIM_DATA"), help="Skyrim Data folder ($DOVASTASH_SKYRIM_DATA)")
    ap.add_argument("--editor", default=str(REPO / "build" / "editor" / "editor" / "Release" / "DovaStashEditor.com"))
    ap.add_argument("--only", help="cells whose ref contains this text")
    ap.add_argument("--mo2", default=os.environ.get("DOVASTASH_MO2"), help="MO2 instance for the home list step ($DOVASTASH_MO2)")
    ap.add_argument("--profile", default=os.environ.get("DOVASTASH_MO2_PROFILE"), help="its profile ($DOVASTASH_MO2_PROFILE)")
    a = ap.parse_args()
    if not a.data or not Path(a.data).is_dir():
        print("error: --data <Skyrim Data folder> (or DOVASTASH_SKYRIM_DATA) is needed", file=sys.stderr)
        return 2
    if not Path(a.editor).is_file():
        print(f"error: no editor at {a.editor} (cmake --build --preset editor-release)", file=sys.stderr)
        return 2
    bad = 0
    cells = []
    for cell in shipped_cells():
        if a.only and a.only.lower() not in cell.lower():
            continue
        plugin = cell.split("|")[0]
        if not (Path(a.data) / plugin).is_file():
            print(f"skip {cell}: {plugin} not in --data")
            continue
        cells.append(cell)
    for cell in cells:
        try:
            ed, py = editor_view(a.editor, a.data, cell), python_view(a.data, cell)
        except (RuntimeError, ValueError) as e:
            print(f"FAIL {cell}: {e}")
            bad += 1
            continue
        diff = compare(cell, ed, py)
        if diff:
            bad += 1
            print(f"FAIL {cell}")
            print("\n".join(diff))
        else:
            print(f"ok   {cell}: {len(ed[0])} containers, {len(ed[1])} displays, {len(ed[2])} doors")
    print(f"{len(cells) - bad} of {len(cells)} cells agree")
    if not a.mo2:
        print("skip home list: --mo2 <instance> [--profile <name>] (its templates) is needed")
    elif not a.only:
        try:
            homes = home_cells(a.editor, a.mo2, a.profile)
        except RuntimeError as e:
            print(f"FAIL home list: {e}")
            return 1
        missed = [c for c in cells if norm(c) not in homes]
        for c in missed:
            print(f"FAIL home list: {c} is in no home of --draft-all --base-game")
        print(f"{len(cells) - len(missed)} of {len(cells)} template cells are homes")
        bad += len(missed)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
