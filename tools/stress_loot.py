"""Generate Skyrim console batch files that fill the player's inventory with a wide spread of items
from the real MO2 load order, for stress-testing Dova Stash's sorting.

    python -I tools/stress_loot.py [--mo2 "E:\\modding\\Kexodus Skyrim"] [--profile Default] [--out DIR]

Writes into the MO2 mod `<mods>/dovastash inventory testing/root/`, which Root Builder deploys to the game
folder (where SkyrimSE.exe is, which is where the `bat` console command looks):

    ds_bulk.txt     big stacks: gold, lockpicks, ammo, potions, soul gems (keep rules, stack counts)
    ds_vanilla.txt  Skyrim + DLC: a few items of every type/subtype the category engine knows, uniques
                    and Daedric artifacts, scripted items
    ds_cc.txt       Creation Club: a few items per plugin
    ds_modsN.txt    every other plugin that adds items: up to --per-plugin each, in load order (chunked)
    ds_weird.txt    oddballs from mods: no keywords, unnamed, zero weight, vanilla-name clones, scripted...
    ds_manifest.csv what every line adds (runtime id, form ref, type, EditorID, name, why it was picked)

In game: `bat ds_vanilla` (and so on). Console form ids depend on the load order, so run this again after
changing plugins. Uses only the standard library and esm_dump.py; never writes outside --out.
"""
from __future__ import annotations

import argparse
import csv
import os
import re
import struct
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import esm_dump as E  # noqa: E402

REC_FLAG_IGNORED = 0x1000
REC_FLAG_NONPLAYABLE = 0x04  # MISC, ARMO, KEYM...: the record header's "Non-Playable" flag
BODT_FLAG_NONPLAYABLE = 0x10

WEAP_ANIM = {1: "OneHandSword", 2: "OneHandDagger", 3: "OneHandAxe", 4: "OneHandMace", 5: "TwoHandSword",
             6: "TwoHandAxe", 7: "Bow", 8: "Staff", 9: "Crossbow"}
ARMOR_TYPE = {0: "Light", 1: "Heavy", 2: "Clothing"}
SLOT_AMULET, SLOT_RING, SLOT_SHIELD, SLOT_CIRCLET = 0x20, 0x40, 0x200, 0x1000

COUNTS = {"WEAP": 1, "ARMO": 1, "AMMO": 37, "ALCH": 3, "INGR": 5, "BOOK": 1, "SCRL": 2, "SLGM": 2,
          "MISC": 2, "KEYM": 1, "LIGH": 2}

# MISC keywords the default category set keys on (00_Default.json); one bucket each.
MISC_KEYWORDS = ["VendorItemGem", "VendorItemOreIngot", "VendorItemAnimalHide", "VendorItemAnimalPart",
                 "VendorItemTool", "VendorItemFireword", "VendorItemClutter", "VendorItemKey",
                 "BYOHHouseCraftingCategorySmithing"]

# Vanilla items worth having by name: artifacts, quest-ish uniques, scripted or odd records.
UNIQUE_NAMES = [
    "Azura's Star", "The Black Star", "Wabbajack", "Mehrunes' Razor", "Ebony Blade", "Mace of Molag Bal",
    "Masque of Clavicus Vile", "Sanguine Rose", "Skull of Corruption", "Volendrung", "Dawnbreaker",
    "Ring of Hircine", "Savior's Hide", "Spellbreaker", "Oghma Infinium", "Skeleton Key", "Ring of Namira",
    "Ebony Mail", "Auriel's Bow", "Dragonbane", "Chillrend", "Wuuthrad", "Bloodskal Blade",
    "Miraak's Sword", "Miraak's Staff", "Elder Scroll", "Dragonbone Arrow", "Sunhallowed Elven Arrow",
    "Bloodcursed Elven Arrow", "Staff of Magnus", "Aetherial Crown", "Aetherial Shield", "Keening",
    "Konahrik", "Miraak", "Bolar's Oathblade", "The Rueful Axe", "Gauldur Blackblade",
    "Ghostblade", "Amulet of Talos", "Amulet of Mara", "Kyne's Token", "Shrouded Cowl",
    "Nightingale Bow", "Thieves Guild Hood", "Golden Claw", "Ebony Claw", "Diamond Claw", "The Gauldur Amulet",
    "Gauldur Amulet Fragment",
    "Unusual Gem", "Crown of Barenziah", "Stalhrim", "Heart Stone", "Dwarven Black Bow of Fate",
    "Pickaxe", "Woodcutter's Axe", "Notched Pickaxe", "Glass Bow of the Stag Prince",
    "Clay", "Quarried Stone", "Lockpick", "Torch", "Gold", "Iron Fittings", "Hinge", "Nails", "Lock",
    "Sweet Roll", "Skooma", "Daedra Heart", "Human Flesh", "Wedding Ring", "Mammoth Tusk", "Bone Meal",
]
# Vanilla records that change game state when read or carried; leave them out of the test.
VANILLA_SKIP_EDID = re.compile(r"(?i:BlackBook|^zz|DELETED|Dummy|^Test)|TEST")
JUNK = {"unnamed", "missingString"}  # records players never see; only ds_weird gets them


@dataclass
class Item:
    plugin: str
    local: int
    runtime: int
    type: str
    sub: str
    edid: str
    name: str
    value: int | None
    weight: float | None
    keywords: list[str]
    scripts: list[str]
    reasons: list[str] = field(default_factory=list)
    base_name: str = ""  # name in the owning plugin (overrides may rename; curated lists use this)

    @property
    def ref(self) -> str:
        return f"{self.plugin}|0x{self.local:08X}"

    @property
    def bucket(self) -> tuple[str, str]:
        return (self.type, self.sub)

    def score(self) -> int:
        return sum(2 if r == "scripted" else 1 for r in self.reasons)

    def junk(self) -> bool:
        return any(r in JUNK for r in self.reasons)


# ------------------------------------------------------------------------------------------------
# MO2
# ------------------------------------------------------------------------------------------------
def ini_value(text: str, key: str) -> str | None:
    m = re.search(rf"^{re.escape(key)}=(.*)$", text, re.M)
    if not m:
        return None
    v = m.group(1).strip()
    if v.startswith("@ByteArray(") and v.endswith(")"):
        v = v[len("@ByteArray("):-1]
    return v.replace("\\\\", "\\")


def mo2_setup(instance: Path, profile: str | None):
    ini = (instance / "ModOrganizer.ini").read_text(encoding="utf-8", errors="replace")
    game = Path(ini_value(ini, "gamePath") or "")
    if not (game / "Data" / "Skyrim.esm").exists():
        sys.exit(f"game not found at {game}")
    profile = profile or ini_value(ini, "selected_profile") or "Default"
    base = Path(ini_value(ini, "base_directory") or instance)
    mods_dir = Path((ini_value(ini, "mod_directory") or str(base / "mods")).replace("%BASE_DIR%", str(base)))
    overwrite = Path((ini_value(ini, "overwrite_directory") or str(base / "overwrite")).replace("%BASE_DIR%", str(base)))
    prof = base / "profiles" / profile
    return game, prof, mods_dir, overwrite


def plugin_files(game: Path, prof: Path, mods_dir: Path, overwrite: Path) -> dict[str, Path]:
    """lower-case plugin file name -> winning file, as MO2's virtual Data folder would show it."""
    def scan(folder: Path, out: dict[str, Path]):
        try:
            for e in os.scandir(folder):
                if e.is_file() and e.name.lower().endswith((".esp", ".esm", ".esl")):
                    out[e.name.lower()] = Path(e.path)
        except OSError:
            pass

    vfs: dict[str, Path] = {}
    scan(game / "Data", vfs)
    lines = (prof / "modlist.txt").read_text(encoding="utf-8", errors="replace").splitlines()
    for line in reversed(lines):  # last line = lowest priority
        if line.startswith("+"):
            scan(mods_dir / line[1:], vfs)
    scan(overwrite, vfs)
    return vfs


def active_order(game: Path, prof: Path, vfs: dict[str, Path]) -> list[str]:
    def read(p: Path) -> list[str]:
        return [l.strip() for l in p.read_text(encoding="utf-8", errors="replace").splitlines()
                if l.strip() and not l.startswith("#")]

    active = {n.lower() for n in E.BASE_MASTERS}
    ccc = game / "Skyrim.ccc"
    if ccc.exists():
        active |= {n.lower() for n in read(ccc)}
    active |= {l[1:].lower() for l in read(prof / "plugins.txt") if l.startswith("*")}
    active = {n for n in active if n in vfs}
    order = [n for n in read(prof / "loadorder.txt") if n.lower() in active]
    missing = active - {n.lower() for n in order}
    if missing:
        print(f"warning: {len(missing)} active plugins missing from loadorder.txt, appended", file=sys.stderr)
        order += sorted(missing)
    return order


# ------------------------------------------------------------------------------------------------
# Record decoding
# ------------------------------------------------------------------------------------------------
def u(fmt: str, b: bytes | None, off: int):
    if b is None or len(b) < off + struct.calcsize(fmt):
        return None
    return struct.unpack_from("<" + fmt, b, off)[0]


def value_weight(rec: E.Record) -> tuple[int | None, float | None]:
    d = rec.get("DATA")
    t = rec.type
    if t in ("WEAP", "ARMO", "INGR", "SCRL", "MISC", "KEYM", "SLGM"):
        return u("i", d, 0), u("f", d, 4)
    if t == "AMMO":
        return u("I", d, 12), u("f", d, 16)
    if t == "BOOK":
        return u("I", d, 8), u("f", d, 12)
    if t == "ALCH":
        return u("i", rec.get("ENIT"), 0), u("f", d, 0)
    if t == "LIGH":
        return u("I", d, 40), u("f", d, 44)
    return None, None


def classify(rec: E.Record) -> tuple[str | None, list[str]]:
    """(subtype, extra reasons), or (None, _) when the record is not a playable inventory item."""
    t, extra = rec.type, []
    if rec.flags & REC_FLAG_NONPLAYABLE:
        return None, extra  # hidden from the inventory menu; Dova Stash never moves these
    if t == "WEAP":
        dnam = rec.get("DNAM")
        anim, flags, flags2 = u("B", dnam, 0), u("H", dnam, 12), u("I", dnam, 40)
        if anim not in WEAP_ANIM or (flags or 0) & 0x80 or (flags2 or 0) & 0x2000:
            return None, extra
        sub = WEAP_ANIM[anim]
        if rec.get("EITM"):
            sub += "+ench"
        if rec.get("CNAM"):
            extra.append("template")
        return sub, extra
    if t == "ARMO":
        if not rec.get("BOD2") and (u("B", rec.get("BODT"), 4) or 0) & BODT_FLAG_NONPLAYABLE:
            return None, extra
        bod = rec.get("BOD2")
        slots, atype = (u("I", bod, 0), u("I", bod, 4)) if bod else (u("I", rec.get("BODT"), 0), u("I", rec.get("BODT"), 8))
        slots, atype = slots or 0, ARMOR_TYPE.get(atype or 0, "Light")
        if not slots:
            extra.append("noSlots")
        if slots & SLOT_SHIELD:
            sub = "Shield"
        elif atype == "Clothing" and slots & (SLOT_AMULET | SLOT_RING | SLOT_CIRCLET):
            sub = "Jewelry"
        else:
            sub = atype
        if rec.get("EITM"):
            sub += "+ench"
        if rec.get("TNAM"):
            extra.append("template")
        return sub, extra
    if t == "AMMO":
        flags = u("I", rec.get("DATA"), 4) or 0
        if flags & 0x2:
            return None, extra
        return ("Arrow" if flags & 0x4 else "Bolt"), extra
    if t == "ALCH":
        flags = u("I", rec.get("ENIT"), 4) or 0
        return ("Poison" if flags & 0x20000 else "Food" if flags & 0x2 else "Potion"), extra
    if t == "BOOK":
        flags = u("B", rec.get("DATA"), 0) or 0
        if flags & 0x2:
            extra.append("cantTake")
        return ("Spell" if flags & 0x4 else "Skill" if flags & 0x1 else "None"), extra
    if t == "SLGM":
        return ("Filled" if (u("B", rec.get("SOUL"), 0) or 0) else "Empty"), extra
    if t == "LIGH":
        if not (u("I", rec.get("DATA"), 12) or 0) & 0x2:
            return None, extra  # not carriable
        return "Torch", extra
    return "-", extra  # INGR, SCRL, MISC, KEYM; MISC refined by keyword below


def keyword_edids(rec: E.Record, kw: dict[tuple[str, int], str]) -> list[str]:
    out = []
    for d in rec.get_all("KWDA"):
        for i in range(0, len(d) - 3, 4):
            key = rec.plugin.resolve(struct.unpack_from("<I", d, i)[0])
            out.append(kw.get((key[0].lower(), key[1]), f"{key[0]}|0x{key[1]:08X}"))
    return out


# ------------------------------------------------------------------------------------------------
# Scan
# ------------------------------------------------------------------------------------------------
def scan(order: list[str], vfs: dict[str, Path], lang: str):
    kw: dict[tuple[str, int], str] = {}
    owners: dict[tuple[str, int], tuple[str, int, str, str]] = {}  # form key -> (plugin, runtime id, EDID, name)
    items: dict[tuple[str, int], Item] = {}  # form key -> item as its winning (last) record says
    full_idx = light_idx = 0
    for name in order:
        path = vfs[name.lower()]
        try:
            pl = E.Plugin(path, lang, path.parent)
        except (OSError, ValueError, struct.error) as e:
            print(f"warning: {name}: {e}", file=sys.stderr)
            continue
        pl.name = name
        if pl.light:
            base, mask, light_idx = 0xFE000000 | (light_idx << 12), 0xFFF, light_idx + 1
        else:
            base, mask, full_idx = full_idx << 24, 0xFFFFFF, full_idx + 1
        nmast = len(pl.masters)
        try:
            for rec in pl.top_records("KYWD"):
                if rec.fid >> 24 >= nmast and (e := rec.edid()):
                    kw[(name.lower(), rec.key[1])] = e
            for t in E.ITEM_TYPES:
                for rec in pl.top_records(t):
                    if rec.flags & REC_FLAG_IGNORED:
                        continue
                    key = (rec.key[0].lower(), rec.key[1])
                    if rec.fid >> 24 >= nmast:
                        full = rec.get("FULL")
                        owners[key] = (name, base | (rec.fid & mask), rec.edid() or "",
                                       (pl.lstring(full) or "") if full is not None else "")
                    if key not in owners:
                        continue  # override of a record whose plugin is not loaded
                    # Later plugins win, as in game: an override can rename the item, change its
                    # keywords, or flag it non-playable.
                    owner, runtime, edid, base_name = owners[key]
                    try:
                        it = None if rec.flags & E.REC_FLAG_DELETED else make_item(rec, owner, runtime, kw)
                    except (struct.error, zlib.error, IndexError):
                        continue
                    if it:
                        it.edid, it.base_name = edid, base_name  # identity stays the owning record's
                        items[key] = it
                    else:
                        items.pop(key, None)
        except (struct.error, ValueError) as e:
            print(f"warning: {name}: {e}", file=sys.stderr)
        finally:
            pl.mm.close()
            pl._fh.close()
    print(f"{len(order)} plugins ({full_idx} full, {light_idx} light), {len(items)} playable items", file=sys.stderr)
    return list(items.values())


def make_item(rec: E.Record, plugin: str, runtime: int, kw) -> Item | None:
    sub, reasons = classify(rec)
    if sub is None:
        return None
    full = rec.get("FULL")
    name = rec.plugin.lstring(full) if full is not None else ""
    if name and name.startswith("<lstring"):
        reasons.append("missingString")
    keywords = keyword_edids(rec, kw)
    if rec.type == "MISC":
        sub = next((k for k in MISC_KEYWORDS if k in keywords), "noVendorKeyword")
    value, weight = value_weight(rec)
    scripts = E.vmad_scripts(rec.get("VMAD"))
    if scripts:
        reasons.append("scripted")
    if not name:
        reasons.append("unnamed")
    if not keywords and rec.type not in ("INGR", "SCRL", "SLGM", "KEYM", "LIGH"):
        reasons.append("noKeywords")
    if weight == 0 and rec.type in ("WEAP", "ARMO", "AMMO", "MISC", "BOOK"):
        reasons.append("zeroWeight")
    if weight is not None and weight >= 100:
        reasons.append("veryHeavy")
    if value is not None and value < 0:
        reasons.append("negativeValue")
    if value is not None and value >= 20000:
        reasons.append("veryValuable")
    if name and len(name) > 45:
        reasons.append("longName")
    if name and not name.isascii():
        reasons.append("nonAsciiName")
    return Item(plugin, rec.key[1], runtime, rec.type, sub, rec.edid() or "", name or "", value, weight,
                keywords, scripts, reasons)


# ------------------------------------------------------------------------------------------------
# Selection
# ------------------------------------------------------------------------------------------------
def spread(items: list[Item], per_bucket: int) -> list[Item]:
    """Up to per_bucket items per (type, subtype), alternating plugins, oddest first."""
    by: dict[tuple[str, str], list[Item]] = {}
    for it in items:
        if not it.junk():
            by.setdefault(it.bucket, []).append(it)
    out = []
    for bucket in sorted(by):
        cands = sorted(by[bucket], key=lambda i: (-i.score(), i.local))
        seen_plugins: set[str] = set()
        picked: list[Item] = []
        for c in cands:  # first pass: one per plugin
            if len(picked) < per_bucket and c.plugin not in seen_plugins:
                picked.append(c)
                seen_plugins.add(c.plugin)
        for c in cands:
            if len(picked) < per_bucket and c not in picked:
                picked.append(c)
        out += picked
    return out


def per_plugin(items: list[Item], cap: int) -> list[Item]:
    """Each plugin's items, one per bucket round-robin (oddest first), up to cap; big plugins get more."""
    out, by_plugin = [], {}
    for it in items:
        if not it.junk():
            by_plugin.setdefault(it.plugin, []).append(it)
    for its in by_plugin.values():
        n = cap + min(2 * cap, len(its) // 100)
        buckets: dict[tuple[str, str], list[Item]] = {}
        for it in sorted(its, key=lambda i: (-i.score(), i.local)):
            buckets.setdefault(it.bucket, []).append(it)
        queues = sorted(buckets.values(), key=lambda q: (-q[0].score(), q[0].bucket))
        picked: list[Item] = []
        while len(picked) < n and any(queues):
            for q in queues:
                if q and len(picked) < n:
                    picked.append(q.pop(0))
        out += picked
    return out


# ------------------------------------------------------------------------------------------------
# Output
# ------------------------------------------------------------------------------------------------
def write_bat(out: Path, fname: str, lines: list[tuple[Item, int, str]], manifest: list):
    with open(out / fname, "w", encoding="ascii", newline="\r\n") as f:
        for i, (it, count, note) in enumerate(lines, 1):
            f.write(f"player.additem {it.runtime:08X} {count}\n")
            manifest.append([fname, i, f"{it.runtime:08X}", count, it.ref, it.type, it.sub, it.edid, it.name,
                             " ".join(filter(None, [note, *it.reasons]))])
    print(f"  {fname}: {len(lines)} lines", file=sys.stderr)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mo2", default=r"E:\modding\Kexodus Skyrim", help="MO2 instance folder")
    ap.add_argument("--profile", help="MO2 profile (default: the selected one)")
    ap.add_argument("--out", help="output folder (default: <mods>/dovastash inventory testing/root)")
    ap.add_argument("--per-plugin", type=int, default=6, help="items per mod plugin (default 6)")
    ap.add_argument("--chunk", type=int, default=300, help="lines per ds_modsN.txt (default 300)")
    ap.add_argument("--lang", default="english")
    a = ap.parse_args(argv)

    game, prof, mods_dir, overwrite = mo2_setup(Path(a.mo2), a.profile)
    out = Path(a.out) if a.out else mods_dir / "dovastash inventory testing" / "root"
    out.mkdir(parents=True, exist_ok=True)
    vfs = plugin_files(game, prof, mods_dir, overwrite)
    order = active_order(game, prof, vfs)
    items = scan(order, vfs, a.lang)

    base = {n.lower() for n in E.BASE_MASTERS}
    ccc = game / "Skyrim.ccc"
    cc = {l.strip().lower() for l in ccc.read_text(errors="replace").splitlines()} if ccc.exists() else set()
    vanilla = [i for i in items if i.plugin.lower() in base and not VANILLA_SKIP_EDID.search(i.edid)]
    ccs = [i for i in items if i.plugin.lower() in cc]
    mods = [i for i in items if i.plugin.lower() not in base | cc]

    vanilla_names = {n.lower() for i in vanilla for n in (i.name, i.base_name) if n}
    for it in mods:
        if it.name and it.name.lower() in vanilla_names:
            it.reasons.append("vanillaNameClone")

    manifest: list[list] = []
    print(f"writing to {out}", file=sys.stderr)

    # bulk stacks: exercise counts and the default keep rules (gold, lockpicks, torches)
    by_edid = {i.edid.lower(): i for i in vanilla}
    bulk = [("Gold001", 100000), ("Lockpick", 500), ("Torch01", 25), ("IronArrow", 5000),
            ("DLC1BoltSteel", 2500), ("RestoreHealth01", 60), ("RestoreMagicka01", 60), ("SoulGemPetty", 40),
            ("SoulGemGrandFilled", 12), ("SoulGemBlack", 8), ("FoodSweetroll", 99), ("Leather01", 150),
            ("IngotIron", 250), ("DeathBell", 200), ("Book2CommonBeggarPrince", 3)]
    bulk_lines = []
    for edid, n in bulk:
        if (it := by_edid.get(edid.lower())):
            bulk_lines.append((it, n, "bulk"))
        else:
            print(f"  note: bulk {edid} not found, skipped", file=sys.stderr)
    write_bat(out, "ds_bulk.txt", bulk_lines, manifest)

    # vanilla: every bucket, named uniques, scripted records
    picked = [(i, "bucket") for i in spread(vanilla, 6)]
    have = {id(i) for i, _ in picked}
    wanted = {n.lower() for n in UNIQUE_NAMES}
    per_name: dict[str, int] = {}
    for it in sorted(vanilla, key=lambda i: i.local):
        n = it.base_name.lower() if it.base_name.lower() in wanted else it.name.lower()
        if n in wanted:
            per_name[n] = per_name.get(n, 0) + 1
            if per_name[n] <= 2 and id(it) not in have:
                have.add(id(it))
                picked.append((it, "unique"))
    missing = sorted(wanted - set(per_name))
    if missing:
        print(f"  note: no vanilla item named {', '.join(missing)}", file=sys.stderr)
    scripted = [i for i in vanilla if i.scripts and id(i) not in have and i.type in ("WEAP", "ARMO", "MISC", "SLGM")]
    picked += [(i, "scripted") for i in spread(scripted, 8)]
    write_bat(out, "ds_vanilla.txt", [(i, COUNTS[i.type], why) for i, why in picked], manifest)

    write_bat(out, "ds_cc.txt", [(i, COUNTS[i.type], "") for i in per_plugin(ccs, a.per_plugin)], manifest)

    mod_pick = per_plugin(mods, a.per_plugin)
    chosen = {id(i) for i in mod_pick}
    for n, start in enumerate(range(0, len(mod_pick), a.chunk), 1):
        chunk = mod_pick[start:start + a.chunk]
        write_bat(out, f"ds_mods{n}.txt", [(i, COUNTS[i.type], "") for i in chunk], manifest)

    weird: list[Item] = []
    for why in ("noKeywords", "unnamed", "zeroWeight", "vanillaNameClone", "scripted", "noSlots", "cantTake",
                "template", "missingString", "negativeValue", "veryValuable", "veryHeavy", "longName",
                "nonAsciiName", "noVendorKeyword"):
        pool = [i for i in mods if id(i) not in chosen and (why in i.reasons or i.sub == why)]
        seen: set[str] = set()
        for it in sorted(pool, key=lambda i: (-i.score(), i.plugin, i.local)):
            if it.plugin not in seen and len(seen) < 25:
                seen.add(it.plugin)
                chosen.add(id(it))
                weird.append(it)
    write_bat(out, "ds_weird.txt", [(i, COUNTS[i.type], "") for i in weird], manifest)

    with open(out / "ds_manifest.csv", "w", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f)
        w.writerow(["file", "line", "consoleId", "count", "formRef", "type", "subtype", "editorId", "name", "why"])
        w.writerows(manifest)
    print(f"  ds_manifest.csv: {len(manifest)} rows", file=sys.stderr)


if __name__ == "__main__":
    main()
