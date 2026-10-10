#!/usr/bin/env python3
"""Fill the image and source-code placeholders in the Dova Stash Nexus page copy.

The page sources in this folder reference images as ``{IMG}/<filename>`` and the source
repository as ``{SOURCE_URL}``. Nexus BBCode needs absolute public URLs, so this script writes
ready-to-paste copies into ``nexus/out/`` with the placeholders replaced.

Usage (standard library only; run in isolated mode):

    python -I nexus/fill_urls.py --base-url https://example.com/dovastash/img \
        --source-url https://github.com/<you>/DovaStash

    # Hosts that rename uploads (Nexus, imgur): map each file name to its URL in a JSON file.
    python -I nexus/fill_urls.py --map nexus/image_urls.json --source-url ...

    # See which images the page needs, then stop.
    python -I nexus/fill_urls.py --list-images

``--map`` entries take priority over ``--base-url``, so you can host most images under one
prefix and override a few. The script exits with status 1 if a placeholder would be left
unresolved, unless you pass --allow-placeholders.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from urllib.parse import quote

HERE = Path(__file__).resolve().parent

# Page sources (relative to this folder) that get processed.
SOURCES = [
    "description.bbcode",
    "sticky_post.bbcode",
    "summary.txt",
    "file_description.txt",
    "changelog.txt",
    "nexus_fields.md",
]

IMG_RE = re.compile(r"\{IMG\}/([A-Za-z0-9_.\-]+)")
SOURCE_TOKEN = "{SOURCE_URL}"

SUMMARY_LIMIT = 350

# BBCode tags whose open/close counts must match in the .bbcode files.
PAIRED_TAGS = ["b", "i", "u", "s", "center", "size", "color", "font", "list", "spoiler", "url", "code", "img", "quote"]


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__.split("\n\n", 1)[0])
    p.add_argument("--base-url", help="public URL prefix the image files are reachable under (file name is appended)")
    p.add_argument("--map", type=Path, help='JSON file {"<filename>": "<full url>", ...}; overrides --base-url per file')
    p.add_argument("--source-url", help="public URL of the source code (GitHub repository or source download)")
    p.add_argument("--out", type=Path, default=HERE / "out", help="output folder (default: nexus/out)")
    p.add_argument("--art-dir", type=Path, default=HERE / "art" / "out", help="local image folder to check against (default: nexus/art/out)")
    p.add_argument("--allow-placeholders", action="store_true", help="write output even if placeholders stay unresolved")
    p.add_argument("--list-images", action="store_true", help="print the image files the page references and exit")
    return p.parse_args(argv)


def load_sources() -> dict[str, str]:
    texts: dict[str, str] = {}
    for name in SOURCES:
        path = HERE / name
        if not path.is_file():
            print(f"warning: {name} not found, skipped", file=sys.stderr)
            continue
        texts[name] = path.read_text(encoding="utf-8")
    return texts


def referenced_images(texts: dict[str, str]) -> list[str]:
    seen: dict[str, None] = {}
    for text in texts.values():
        for m in IMG_RE.finditer(text):
            seen.setdefault(m.group(1), None)
    return list(seen)


def load_map(path: Path | None) -> dict[str, str]:
    if path is None:
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        sys.exit(f"error: cannot read --map {path}: {e}")
    if not isinstance(data, dict) or not all(isinstance(k, str) and isinstance(v, str) for k, v in data.items()):
        sys.exit(f"error: --map {path} must be a JSON object of file name -> URL strings")
    return data


def check_bbcode(name: str, text: str) -> list[str]:
    problems = []
    for tag in PAIRED_TAGS:
        opened = len(re.findall(rf"\[{tag}(?:=[^\]]*)?\]", text, flags=re.IGNORECASE))
        closed = len(re.findall(rf"\[/{tag}\]", text, flags=re.IGNORECASE))
        if opened != closed:
            problems.append(f"{name}: [{tag}] opened {opened}x, closed {closed}x")
    return problems


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    texts = load_sources()
    images = referenced_images(texts)

    if args.list_images:
        for img in images:
            present = (args.art_dir / img).is_file()
            print(f"{img}{'' if present else '    (missing locally)'}")
        return 0

    url_map = load_map(args.map)
    base = args.base_url.rstrip("/") if args.base_url else None
    if base and not base.startswith("https://"):
        print(f"warning: --base-url is not https ({base}); Nexus may block mixed content", file=sys.stderr)

    errors: list[str] = []
    warnings: list[str] = []

    # Resolve every image once.
    resolved: dict[str, str] = {}
    for img in images:
        if img in url_map:
            resolved[img] = url_map[img]
        elif base:
            resolved[img] = f"{base}/{quote(img)}"
        else:
            errors.append(f"no URL for image {img} (give --base-url or add it to --map)")
        if not (args.art_dir / img).is_file():
            warnings.append(f"image {img} is not in {args.art_dir} (check that it was generated and uploaded)")
    for key in url_map:
        if key not in images:
            warnings.append(f"--map entry {key} is not referenced by any page file")

    if not args.source_url:
        errors.append(
            "no --source-url: {SOURCE_URL} would stay unfilled. GPL-3.0 requires the source to be available "
            "to everyone who downloads the DLL: make the repository public or upload a source archive and link it"
        )

    out_texts: dict[str, str] = {}
    for name, text in texts.items():
        text = IMG_RE.sub(lambda m: resolved.get(m.group(1), m.group(0)), text)
        if args.source_url:
            text = text.replace(SOURCE_TOKEN, args.source_url)
        out_texts[name] = text
        if name.endswith(".bbcode"):
            errors.extend(check_bbcode(name, text))

    if "summary.txt" in out_texts:
        length = len(out_texts["summary.txt"].strip())
        if length > SUMMARY_LIMIT:
            errors.append(f"summary.txt is {length} characters; Nexus allows {SUMMARY_LIMIT}")
        else:
            print(f"summary.txt: {length}/{SUMMARY_LIMIT} characters")

    for name, text in out_texts.items():
        left = sorted(set(IMG_RE.findall(text)))
        if left:
            errors.append(f"{name}: unresolved images: {', '.join(left)}")
        if SOURCE_TOKEN in text:
            errors.append(f"{name}: {SOURCE_TOKEN} left unfilled")

    for w in dict.fromkeys(warnings):
        print(f"warning: {w}", file=sys.stderr)
    unique_errors = list(dict.fromkeys(errors))
    for e in unique_errors:
        print(f"error: {e}", file=sys.stderr)
    if unique_errors and not args.allow_placeholders:
        print("nothing written (use --allow-placeholders to write anyway)", file=sys.stderr)
        return 1

    args.out.mkdir(parents=True, exist_ok=True)
    for name, text in out_texts.items():
        (args.out / name).write_text(text, encoding="utf-8", newline="\n")
        print(f"wrote {args.out / name}")
    print(f"{len(images)} images referenced, {len(resolved)} resolved")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
