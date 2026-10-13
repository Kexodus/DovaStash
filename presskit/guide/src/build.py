"""Print the Dova Stash Editor user guide to presskit/guide/DovaStashEditor-UserGuide.pdf.

Stdlib only. Run from anywhere with:   python -I presskit/guide/src/build.py [--copy-to <folder>]

The pages are guide.html (+ guide.css, pages.css, guide.js, figs.js); the screenshots are in ../img.
Headless Microsoft Edge prints them (US Letter, backgrounds on, no browser header or footer), the
same browser presskit/src/render.py uses. The script then checks the page count against the number
of <section class="page"> in guide.html, so a page that overflows onto a second sheet fails the build.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SRC = Path(__file__).resolve().parent
GUIDE = SRC.parent
OUT = GUIDE / "DovaStashEditor-UserGuide.pdf"
EDGE = os.environ.get(
    "DS_EDGE", r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe")


def pdf_pages(path):
    """Page count from the page tree (Edge writes an uncompressed /Count in the root /Pages)."""
    data = path.read_bytes()
    counts = [int(m) for m in re.findall(rb"/Type\s*/Pages\b[^>]*?/Count\s+(\d+)", data)]
    counts += [int(m) for m in re.findall(rb"/Count\s+(\d+)[^>]*?/Type\s*/Pages\b", data)]
    if counts:
        return max(counts)
    return len(re.findall(rb"/Type\s*/Page\b", data))


def main(argv):
    copy_to = None
    if "--copy-to" in argv:
        copy_to = Path(argv[argv.index("--copy-to") + 1])
    expected = len(re.findall(r'<section class="page', (SRC / "guide.html").read_text(encoding="utf-8")))
    with tempfile.TemporaryDirectory(prefix="ds_guide_", ignore_cleanup_errors=True) as prof:
        tmp = Path(prof) / "guide.pdf"
        args = [EDGE, "--headless=new", "--disable-gpu", "--hide-scrollbars",
                "--allow-file-access-from-files", "--virtual-time-budget=20000",
                "--run-all-compositor-stages-before-draw", "--no-pdf-header-footer",
                "--generate-pdf-document-outline", "--no-first-run", "--disable-extensions",
                f"--user-data-dir={prof}", f"--print-to-pdf={tmp}", (SRC / "guide.html").as_uri()]
        subprocess.run(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=180)
        if not tmp.exists():
            print("FAIL: Edge did not write the PDF")
            return 1
        shutil.copyfile(tmp, OUT)
    pages = pdf_pages(OUT)
    size = OUT.stat().st_size
    ok = pages == expected
    print(f"{'ok  ' if ok else 'FAIL'} {OUT.relative_to(GUIDE.parent.parent)}: {pages} pages "
          f"(expected {expected}), {size / 1024 / 1024:.1f} MB")
    if ok and copy_to:
        copy_to.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(OUT, copy_to / OUT.name)
        print(f"copied to {copy_to / OUT.name}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
