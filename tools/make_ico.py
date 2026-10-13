"""Packs PNG images into a Windows .ico (PNG-compressed entries, Vista+). Stdlib only.

Usage: python -I tools/make_ico.py out.ico in1.png [in2.png ...]
Used for the Dova Stash Editor's icon: presskit/logo/icon/*.png + logo 256 -> editor/res/DovaStashEditor.ico.
"""
import struct
import sys
from pathlib import Path


def png_size(data: bytes) -> tuple[int, int]:
	if data[:8] != b"\x89PNG\r\n\x1a\n":
		raise ValueError("not a PNG")
	return struct.unpack(">II", data[16:24])


def main(argv: list[str]) -> int:
	if len(argv) < 3:
		print(__doc__)
		return 2
	images = []
	for p in argv[2:]:
		data = Path(p).read_bytes()
		w, h = png_size(data)
		images.append((w, h, data))
	images.sort(key=lambda i: i[0])
	header = struct.pack("<HHH", 0, 1, len(images))
	offset = 6 + 16 * len(images)
	entries, blobs = b"", b""
	for w, h, data in images:
		entries += struct.pack("<BBBBHHII", w % 256, h % 256, 0, 0, 1, 32, len(data), offset)
		blobs += data
		offset += len(data)
	Path(argv[1]).write_bytes(header + entries + blobs)
	print(f"wrote {argv[1]} with {len(images)} sizes: {[i[0] for i in images]}")
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv))
