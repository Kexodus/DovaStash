#!/usr/bin/env python3
"""esm_dump.py - stdlib-only reader for Skyrim SE plugin files (.esm/.esp/.esl).

Built for Dova Stash template authors: find a home's cell and the form IDs of its containers,
in the exact "Plugin.esm|0x00ABCDEF" notation Dova Stash uses (plugin file + LOCAL form id).

Subcommands (all print JSON to stdout):
  keywords        list keyword EditorIDs -> form ref       (--filter SUBSTR, --check EDID ...)
  cells           find cells whose EditorID/name contains a substring
  cell            dump one cell: header info + every container reference in it
  ref             describe one placed reference (base, cell, flags, position)
  keyword-usage   count items (per record type) carrying the given keywords
  check           validate Dova Stash template/layout + category JSON files against the plugins
  plugins         show the resolved load order (masters, light/localized flags)

Run `python -I esm_dump.py <subcommand> -h` for options. See tools/README.md.
"""

from __future__ import annotations

import argparse
import json
import math
import mmap
import os
import struct
import sys
import zlib
from pathlib import Path

DEFAULT_DATA_DIRS = [
    os.environ.get("DOVASTASH_SKYRIM_DATA", ""),
    r"C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition\Data",
    r"E:\SteamLibrary\steamapps\common\Skyrim Special Edition\Data",
    r"D:\SteamLibrary\steamapps\common\Skyrim Special Edition\Data",
]
BASE_MASTERS = ["Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm"]

ITEM_TYPES = ["WEAP", "ARMO", "AMMO", "ALCH", "INGR", "BOOK", "SCRL", "SLGM", "MISC", "KEYM", "LIGH"]

REC_FLAG_DELETED = 0x20
REC_FLAG_PERSISTENT = 0x400
REC_FLAG_INITIALLY_DISABLED = 0x800
REC_FLAG_COMPRESSED = 0x40000
TES4_FLAG_MASTER = 0x1
TES4_FLAG_LOCALIZED = 0x80
TES4_FLAG_LIGHT = 0x200

CELL_FLAGS = {
    0x0001: "Interior",
    0x0002: "HasWater",
    0x0004: "CantTravelFromHere",
    0x0008: "NoLODWater",
    0x0020: "PublicArea",
    0x0040: "HandChanged",
    0x0080: "ShowSky",
    0x0100: "UseSkyLighting",
}
ECZN_FLAGS = {0x01: "NeverResets", 0x02: "MatchPCBelowMinLevel", 0x04: "DisableCombatBoundary"}
CONT_FLAG_RESPAWNS = 0x02

HDR = struct.Struct("<4sIIIIHH")  # type, size, flags, formid, vc, version, unknown  (24 bytes)


# --------------------------------------------------------------------------------------------
# LZ4 frame decoder (SE BSAs compress with LZ4 frames). Pure Python, used only for small files.
# --------------------------------------------------------------------------------------------
def _lz4_block(src: bytes, out: bytearray) -> None:
    i, n = 0, len(src)
    while i < n:
        token = src[i]
        i += 1
        lit = token >> 4
        if lit == 15:
            while True:
                b = src[i]
                i += 1
                lit += b
                if b != 255:
                    break
        out += src[i:i + lit]
        i += lit
        if i >= n:
            break
        off = src[i] | (src[i + 1] << 8)
        i += 2
        mlen = token & 15
        if mlen == 15:
            while True:
                b = src[i]
                i += 1
                mlen += b
                if b != 255:
                    break
        mlen += 4
        start = len(out) - off
        if off >= mlen:
            out += out[start:start + mlen]
        else:
            while mlen > 0:
                chunk = out[start:start + min(off, mlen)]
                out += chunk
                mlen -= len(chunk)


def lz4_frame_decompress(data: bytes) -> bytes:
    if struct.unpack_from("<I", data, 0)[0] != 0x184D2204:
        raise ValueError("not an LZ4 frame")
    flg = data[4]
    pos = 6
    if flg & 0x08:
        pos += 8  # content size
    if flg & 0x01:
        pos += 4  # dict id
    pos += 1  # header checksum
    out = bytearray()
    block_checksum = bool(flg & 0x10)
    while True:
        bsize = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        if bsize == 0:
            break
        raw = bsize & 0x80000000
        bsize &= 0x7FFFFFFF
        blk = data[pos:pos + bsize]
        pos += bsize
        if raw:
            out += blk
        else:
            _lz4_block(blk, out)
        if block_checksum:
            pos += 4
    return bytes(out)


# --------------------------------------------------------------------------------------------
# BSA (v104/v105) reader - just enough to pull Strings files out of archives.
# --------------------------------------------------------------------------------------------
class BSA:
    def __init__(self, path: Path):
        self.path = path
        self.files: dict[str, tuple[int, int, bool]] = {}  # lowercase "folder\\file" -> (offset, size, compressed)
        with open(path, "rb") as f:
            hdr = f.read(36)
            magic, ver, foff, aflags, nfolders, nfiles, tfolderlen, tfilelen = struct.unpack_from("<4sIIIIIII", hdr)
            if magic != b"BSA\0" or ver not in (103, 104, 105):
                raise ValueError(f"unsupported BSA {path}")
            self.version = ver
            self.compressed_default = bool(aflags & 0x4)
            self.embed_names = bool(aflags & 0x100) and ver != 103
            frec = 24 if ver == 105 else 16
            f.seek(foff)
            folders = []
            for _ in range(nfolders):
                rec = f.read(frec)
                count = struct.unpack_from("<I", rec, 8)[0]
                folders.append(count)
            entries = []  # (folder_name, size, offset)
            for count in folders:
                ln = f.read(1)[0]
                fname = f.read(ln).rstrip(b"\0").decode("cp1252", "replace")
                for _ in range(count):
                    rec = f.read(16)
                    _h, size, off = struct.unpack_from("<QII", rec)
                    entries.append((fname, size, off))
            names = f.read(tfilelen).split(b"\0")
            for (folder, size, off), name in zip(entries, names):
                comp = self.compressed_default
                if size & 0x40000000:
                    comp = not comp
                key = (folder + "\\" + name.decode("cp1252", "replace")).lower()
                self.files[key] = (off, size & 0x3FFFFFFF, comp)

    def read(self, key: str) -> bytes | None:
        ent = self.files.get(key.lower().replace("/", "\\"))
        if not ent:
            return None
        off, size, comp = ent
        with open(self.path, "rb") as f:
            f.seek(off)
            blob = f.read(size)
        p = 0
        if self.embed_names:
            p = 1 + blob[0]
        if comp:
            orig = struct.unpack_from("<I", blob, p)[0]
            payload = blob[p + 4:]
            if self.version == 105:
                data = lz4_frame_decompress(payload)
            else:
                data = zlib.decompress(payload)
            return data[:orig]
        return blob[p:]


def parse_strings_file(raw: bytes, length_prefixed: bool) -> dict[int, str]:
    count, _size = struct.unpack_from("<II", raw, 0)
    base = 8 + count * 8
    out = {}
    for k in range(count):
        sid, off = struct.unpack_from("<II", raw, 8 + k * 8)
        p = base + off
        if length_prefixed:
            ln = struct.unpack_from("<I", raw, p)[0]
            s = raw[p + 4:p + 4 + ln]
        else:
            e = raw.find(b"\0", p)
            s = raw[p:e]
        s = s.rstrip(b"\0")
        try:
            out[sid] = s.decode("utf-8")
        except UnicodeDecodeError:
            out[sid] = s.decode("cp1252", "replace")
    return out


# --------------------------------------------------------------------------------------------
# Plugin file
# --------------------------------------------------------------------------------------------
class Record:
    __slots__ = ("plugin", "type", "flags", "fid", "offset", "_subs")

    def __init__(self, plugin, rtype, flags, fid, offset):
        self.plugin = plugin
        self.type = rtype
        self.flags = flags
        self.fid = fid
        self.offset = offset
        self._subs = None

    @property
    def key(self):
        return self.plugin.resolve(self.fid)

    def subrecords(self) -> list[tuple[str, bytes]]:
        if self._subs is None:
            self._subs = self.plugin.read_subrecords(self.offset)
        return self._subs

    def get(self, sig: str) -> bytes | None:
        for t, d in self.subrecords():
            if t == sig:
                return d
        return None

    def get_all(self, sig: str) -> list[bytes]:
        return [d for t, d in self.subrecords() if t == sig]

    def edid(self) -> str | None:
        d = self.get("EDID")
        return cstr(d) if d is not None else None


def cstr(b: bytes) -> str:
    b = b.split(b"\0", 1)[0]
    try:
        return b.decode("utf-8")
    except UnicodeDecodeError:
        return b.decode("cp1252", "replace")


class Plugin:
    def __init__(self, path: Path, lang: str, data_dir: Path):
        self.path = path
        self.name = path.name
        self.lang = lang
        self.data_dir = data_dir
        self._fh = open(path, "rb")
        self.mm = mmap.mmap(self._fh.fileno(), 0, access=mmap.ACCESS_READ)
        rtype, size, flags, fid, *_ = HDR.unpack_from(self.mm, 0)
        if rtype != b"TES4":
            raise ValueError(f"{path} is not a TES4 plugin")
        self.header_flags = flags
        self.masters: list[str] = []
        for t, d in self._parse_subs(bytes(self.mm[24:24 + size])):
            if t == "MAST":
                self.masters.append(cstr(d))
        self.localized = bool(flags & TES4_FLAG_LOCALIZED)
        self.light = bool(flags & TES4_FLAG_LIGHT) or path.suffix.lower() == ".esl"
        self.top: dict[str, tuple[int, int]] = {}  # record type -> (offset, size) of top-level GRUP
        pos = 24 + size
        end = len(self.mm)
        while pos < end:
            t, gsize, label = struct.unpack_from("<4sI4s", self.mm, pos)
            if t != b"GRUP":
                raise ValueError(f"{self.name}: expected top-level GRUP at {pos:#x}")
            self.top[label.decode("ascii", "replace")] = (pos, gsize)
            pos += gsize
        self._strings: dict[str, dict[int, str] | None] = {}

    # ---- form ids -------------------------------------------------------------------------
    def resolve(self, fid: int) -> tuple[str, int]:
        """File-local form id -> (owning plugin name, local id). The key is load-order independent."""
        idx = fid >> 24
        if idx < len(self.masters):
            return (self.masters[idx], fid & 0xFFFFFF)
        return (self.name, (fid & 0xFFF) if self.light else (fid & 0xFFFFFF))

    # ---- raw record access ----------------------------------------------------------------
    @staticmethod
    def _parse_subs(data: bytes) -> list[tuple[str, bytes]]:
        out = []
        p, n = 0, len(data)
        big = None
        while p + 6 <= n:
            t = data[p:p + 4].decode("ascii", "replace")
            sz = data[p + 4] | (data[p + 5] << 8)
            p += 6
            if t == "XXXX":
                big = struct.unpack_from("<I", data, p)[0]
                p += sz
                continue
            if big is not None:
                sz, big = big, None
            out.append((t, data[p:p + sz]))
            p += sz
        return out

    def read_subrecords(self, offset: int) -> list[tuple[str, bytes]]:
        _t, size, flags = struct.unpack_from("<4sII", self.mm, offset)
        data = self.mm[offset + 24:offset + 24 + size]
        if flags & REC_FLAG_COMPRESSED:
            data = zlib.decompress(data[4:])
        return self._parse_subs(data)

    def walk(self, start: int, end: int, descend=None):
        """Yield ('REC', Record) and ('GRUP', offset, size, label_bytes, group_type) for items in
        [start, end). Groups are entered when descend(label, gtype) is truthy, otherwise skipped."""
        mm = self.mm
        pos = start
        while pos < end:
            t, size, flags, fid = struct.unpack_from("<4sIII", mm, pos)
            if t == b"GRUP":
                label = mm[pos + 8:pos + 12]
                gtype = struct.unpack_from("<i", mm, pos + 12)[0]
                yield ("GRUP", pos, size, label, gtype)
                if descend is not None and descend(label, gtype):
                    yield from self.walk(pos + 24, pos + size, descend)
                pos += size
            else:
                yield ("REC", Record(self, t.decode("ascii", "replace"), flags, fid, pos))
                pos += 24 + size

    def top_records(self, rtype: str):
        g = self.top.get(rtype)
        if not g:
            return
        off, size = g
        for item in self.walk(off + 24, off + size, descend=lambda l, gt: False):
            if item[0] == "REC" and item[1].type == rtype:
                yield item[1]

    # ---- strings --------------------------------------------------------------------------
    def strings(self, kind: str = "strings") -> dict[int, str] | None:
        if kind in self._strings:
            return self._strings[kind]
        stem = self.path.stem
        fname = f"{stem}_{self.lang}.{kind}"
        table = None
        loose = self.data_dir / "Strings" / fname
        if loose.exists():
            table = parse_strings_file(loose.read_bytes(), kind != "strings")
        else:
            for cand in _bsa_candidates(self.data_dir, stem):
                bsa = _open_bsa(cand)
                if bsa is None:
                    continue
                raw = bsa.read("strings\\" + fname.lower())
                if raw:
                    table = parse_strings_file(raw, kind != "strings")
                    break
        self._strings[kind] = table
        return table

    def lstring(self, data: bytes | None) -> str | None:
        if data is None:
            return None
        if not self.localized:
            return cstr(data)
        if len(data) < 4:
            return None
        sid = struct.unpack_from("<I", data, 0)[0]
        if sid == 0:
            return ""
        table = self.strings("strings")
        if table is None or sid not in table:
            return f"<lstring {sid:#010x} in {self.path.stem}_{self.lang}.STRINGS (not found)>"
        return table[sid]


_BSA_CACHE: dict[Path, BSA | None] = {}


def _open_bsa(path: Path) -> BSA | None:
    if path not in _BSA_CACHE:
        try:
            _BSA_CACHE[path] = BSA(path) if path.exists() else None
        except (OSError, ValueError, struct.error):
            _BSA_CACHE[path] = None
    return _BSA_CACHE[path]


def _bsa_candidates(data_dir: Path, stem: str) -> list[Path]:
    c = [data_dir / f"{stem}.bsa", data_dir / f"{stem} - Main.bsa", data_dir / f"{stem} - Interface.bsa",
         data_dir / f"{stem} - Strings.bsa", data_dir / "Skyrim - Interface.bsa"]
    seen, out = set(), []
    for p in c:
        if p not in seen:
            seen.add(p)
            out.append(p)
    return out


# --------------------------------------------------------------------------------------------
# Load order (later plugins override earlier ones)
# --------------------------------------------------------------------------------------------
def fmt_ref(key: tuple[str, int]) -> str:
    return f"{key[0]}|0x{key[1]:08X}"


def parse_ref(s: str) -> tuple[str, int]:
    plugin, _, fid = s.partition("|")
    if not fid:
        raise ValueError(f"bad form ref {s!r} (expected 'Plugin.esm|0x00ABCDEF')")
    return (plugin, int(fid, 16))


def keyeq(a: tuple[str, int], b: tuple[str, int]) -> bool:
    return a[1] == b[1] and a[0].lower() == b[0].lower()


def kl(key: tuple[str, int]) -> tuple[str, int]:
    return (key[0].lower(), key[1])


class LoadOrder:
    def __init__(self, data_dir: Path, names: list[str], lang: str):
        self.data_dir = data_dir
        self.plugins: list[Plugin] = []
        for n in names:
            p = Path(n)
            if not p.is_absolute():
                p = data_dir / n
            if not p.exists():
                print(f"warning: plugin {n} not found, skipped", file=sys.stderr)
                continue
            pl = Plugin(p, lang, data_dir)
            pl.name = Path(n).name  # keep the caller's casing (Windows file names are case-insensitive)
            self.plugins.append(pl)
        self._index: dict[str, dict[tuple[str, int], Record]] = {}

    def index(self, rtype: str) -> dict[tuple[str, int], Record]:
        """Winning (last-loaded) top-level record of the given type, keyed by lowercase form key."""
        if rtype not in self._index:
            idx = {}
            for pl in self.plugins:
                for rec in pl.top_records(rtype):
                    idx[kl(rec.key)] = rec
            self._index[rtype] = idx
        return self._index[rtype]

    def lookup(self, key: tuple[str, int], types: list[str]) -> Record | None:
        for t in types:
            r = self.index(t).get(kl(key))
            if r is not None:
                return r
        return None

    def describe(self, key: tuple[str, int], types: list[str]) -> dict:
        r = self.lookup(key, types)
        d = {"form": fmt_ref(key)}
        if r is not None:
            d["type"] = r.type
            d["editorId"] = r.edid()
            full = r.plugin.lstring(r.get("FULL"))
            if full:
                d["name"] = full
        return d

    # ---- cells ----------------------------------------------------------------------------
    def iter_cells(self, pl: Plugin):
        """Yield (Record, worldspace_key_or_None) for every CELL record in the plugin; skips children."""
        if "CELL" in pl.top:
            off, size = pl.top["CELL"]
            for item in pl.walk(off + 24, off + size, descend=lambda l, gt: gt in (2, 3)):
                if item[0] == "REC" and item[1].type == "CELL":
                    yield item[1], None
        if "WRLD" in pl.top:
            off, size = pl.top["WRLD"]
            world = None
            for item in pl.walk(off + 24, off + size, descend=lambda l, gt: gt in (1, 4, 5)):
                if item[0] == "REC":
                    if item[1].type == "WRLD":
                        world = item[1].key
                    elif item[1].type == "CELL":
                        yield item[1], world

    def find_cell_children(self, pl: Plugin, cell_key) -> list[tuple[int, int, int]]:
        """Return [(offset, size, gtype)] of the persistent/temporary children groups of the cell."""
        out = []
        for top in ("CELL", "WRLD"):
            if top not in pl.top:
                continue
            off, size = pl.top[top]
            for item in pl.walk(off + 24, off + size, descend=lambda l, gt: gt in (1, 2, 3, 4, 5)):
                if item[0] == "GRUP" and item[4] == 6:
                    label_fid = struct.unpack("<I", item[3])[0]
                    if keyeq(pl.resolve(label_fid), cell_key):
                        g_off, g_size = item[1], item[2]
                        for sub in pl.walk(g_off + 24, g_off + g_size, descend=None):
                            if sub[0] == "GRUP" and sub[4] in (8, 9, 10):
                                out.append((sub[1], sub[2], sub[4]))
        return out

    def cell_record(self, cell_key) -> tuple[Record | None, object]:
        win, world = None, None
        for pl in self.plugins:
            for rec, w in self.iter_cells(pl):
                if keyeq(rec.key, cell_key):
                    win, world = rec, w
        return win, world

    def cell_refs(self, cell_key, types=("REFR",)) -> dict[tuple[str, int], tuple[Record, int]]:
        """Placed references of a cell (later plugins override earlier ones). For exterior cells this
        includes the worldspace's persistent references whose position falls inside the cell's grid square
        (the game stores those in the worldspace's persistent cell, not under the cell itself)."""
        refs: dict[tuple[str, int], tuple[Record, int]] = {}
        for pl in self.plugins:
            for g_off, g_size, gtype in self.find_cell_children(pl, cell_key):
                for item in pl.walk(g_off + 24, g_off + g_size, descend=None):
                    if item[0] == "REC" and item[1].type in types:
                        refs[kl(item[1].key)] = (item[1], gtype)
        crec, world = self.cell_record(cell_key)
        xclc = crec.get("XCLC") if crec is not None else None
        if world and xclc and len(xclc) >= 8:
            gx, gy = struct.unpack_from("<ii", xclc, 0)
            for rec in self.world_persistent_refs(world):
                pos = rec.get("DATA")
                if pos and len(pos) >= 8:
                    x, y = struct.unpack_from("<ff", pos, 0)
                    if math.floor(x / 4096.0) == gx and math.floor(y / 4096.0) == gy:
                        refs[kl(rec.key)] = (rec, 8)
        return refs

    def world_persistent_refs(self, world_key):
        """Winning REFR records stored in the worldspace's persistent cell, across the load order."""
        cache = self.__dict__.setdefault("_wpr", {})
        k = kl(world_key)
        if k in cache:
            return cache[k]
        win: dict[tuple[str, int], Record] = {}
        for pl in self.plugins:
            if "WRLD" not in pl.top:
                continue
            off, size = pl.top["WRLD"]
            for item in pl.walk(off + 24, off + size, descend=None):
                if item[0] != "GRUP" or item[4] != 1:
                    continue
                if not keyeq(pl.resolve(struct.unpack("<I", item[3])[0]), world_key):
                    continue
                for sub in pl.walk(item[1] + 24, item[1] + item[2], descend=None):
                    if sub[0] == "GRUP" and sub[4] == 6:  # children of the persistent cell (direct child)
                        for g in pl.walk(sub[1] + 24, sub[1] + sub[2], descend=None):
                            if g[0] == "GRUP" and g[4] == 8:
                                for r in pl.walk(g[1] + 24, g[1] + g[2], descend=None):
                                    if r[0] == "REC" and r[1].type == "REFR":
                                        win[kl(r[1].key)] = r[1]
        cache[k] = list(win.values())
        return cache[k]


# --------------------------------------------------------------------------------------------
# Decoders
# --------------------------------------------------------------------------------------------
def flags_list(value: int, table: dict[int, str]) -> list[str]:
    out = [name for bit, name in table.items() if value & bit]
    unknown = value & ~sum(table)
    if unknown:
        out.append(f"0x{unknown:X}")
    return out


def decode_cell(lo: LoadOrder, rec: Record, world) -> dict:
    pl = rec.plugin
    d = {
        "cell": fmt_ref(rec.key),
        "editorId": rec.edid(),
        "name": pl.lstring(rec.get("FULL")),
        "definedIn": pl.name,
        "worldspace": fmt_ref(world) if world else None,
    }
    data = rec.get("DATA")
    if data:
        fl = data[0] | ((data[1] << 8) if len(data) > 1 else 0)
        d["flags"] = flags_list(fl, CELL_FLAGS)
        d["interior"] = bool(fl & 1)
    xclc = rec.get("XCLC")
    if world and xclc and len(xclc) >= 8:
        d["grid"] = list(struct.unpack_from("<ii", xclc, 0))  # exterior cell: its grid square in the worldspace
    xown = rec.get("XOWN")
    d["owner"] = lo.describe(pl.resolve(struct.unpack_from("<I", xown)[0]), ["FACT", "NPC_"]) if xown else None
    xlcn = rec.get("XLCN")
    if xlcn:
        loc = lo.describe(pl.resolve(struct.unpack_from("<I", xlcn)[0]), ["LCTN"])
        lrec = lo.lookup(parse_ref(loc["form"]), ["LCTN"])
        if lrec is not None:
            loc["keywords"] = keyword_edids(lo, lrec)
        d["location"] = loc
    else:
        d["location"] = None
    xezn = rec.get("XEZN")
    if xezn:
        ez = lo.describe(pl.resolve(struct.unpack_from("<I", xezn)[0]), ["ECZN"])
        erec = lo.lookup(parse_ref(ez["form"]), ["ECZN"])
        if erec is not None:
            ed = erec.get("DATA")
            if ed and len(ed) >= 11:
                ez["flags"] = flags_list(ed[10], ECZN_FLAGS)
                ez["minLevel"] = ed[9]
        d["encounterZone"] = ez
    else:
        d["encounterZone"] = None
    return d


def keyword_edids(lo: LoadOrder, rec: Record) -> list[str]:
    kw = rec.get("KWDA")
    if not kw:
        return []
    out = []
    kidx = lo.index("KYWD")
    for i in range(len(kw) // 4):
        key = rec.plugin.resolve(struct.unpack_from("<I", kw, i * 4)[0])
        k = kidx.get(kl(key))
        out.append((k.edid() if k else None) or fmt_ref(key))
    return out


def decode_cont(rec: Record) -> dict:
    d = {"base": fmt_ref(rec.key), "baseEditorId": rec.edid(), "baseName": rec.plugin.lstring(rec.get("FULL"))}
    data = rec.get("DATA")
    fl = data[0] if data else 0
    d["respawns"] = bool(fl & CONT_FLAG_RESPAWNS)
    d["baseFlags"] = fl
    d["initialItems"] = len(rec.get_all("CNTO"))
    scripts = vmad_scripts(rec.get("VMAD"))
    if scripts:
        d["baseScripts"] = scripts
    return d


def vmad_scripts(vmad: bytes | None) -> list[str]:
    """Names of the Papyrus scripts attached via VMAD (fragments ignored)."""
    return [name for name, _props in vmad_decode(vmad)]


def vmad_decode(vmad: bytes | None) -> list[tuple[str, dict]]:
    """Scripts attached via VMAD with the properties set on this record: name -> value. Object values are
    raw form ids of this plugin (resolve them with Plugin.resolve); arrays are lists. Fragments ignored."""
    out: list[tuple[str, dict]] = []
    if not vmad or len(vmad) < 6:
        return out
    try:
        version, obj_format, count = struct.unpack_from("<hhH", vmad, 0)
        p = 6

        def wstr(p):
            ln = struct.unpack_from("<H", vmad, p)[0]
            return vmad[p + 2:p + 2 + ln].decode("cp1252", "replace"), p + 2 + ln

        def obj(p):
            fid = struct.unpack_from("<I", vmad, p + (0 if obj_format == 1 else 4))[0]
            return ("form", fid), p + 8

        for _ in range(count):
            name, p = wstr(p)
            if version >= 4:
                p += 1
            props = {}
            out.append((name, props))
            nprops = struct.unpack_from("<H", vmad, p)[0]
            p += 2
            for _ in range(nprops):
                pname, p = wstr(p)
                ptype = vmad[p]
                p += 2 if version >= 4 else 1
                if ptype == 1:
                    props[pname], p = obj(p)
                elif ptype == 2:
                    props[pname], p = wstr(p)
                elif ptype == 3:
                    props[pname] = struct.unpack_from("<i", vmad, p)[0]
                    p += 4
                elif ptype == 4:
                    props[pname] = struct.unpack_from("<f", vmad, p)[0]
                    p += 4
                elif ptype == 5:
                    props[pname] = bool(vmad[p])
                    p += 1
                elif ptype in (11, 12, 13, 14, 15):
                    n = struct.unpack_from("<I", vmad, p)[0]
                    p += 4
                    vals = []
                    for _ in range(n):
                        if ptype == 11:
                            v, p = obj(p)
                        elif ptype == 12:
                            v, p = wstr(p)
                        elif ptype == 13:
                            v, p = struct.unpack_from("<i", vmad, p)[0], p + 4
                        elif ptype == 14:
                            v, p = struct.unpack_from("<f", vmad, p)[0], p + 4
                        else:
                            v, p = bool(vmad[p]), p + 1
                        vals.append(v)
                    props[pname] = vals
                else:
                    return out  # unknown property type: stop, keep what we have
        return out
    except (struct.error, IndexError):
        return out


def _starts_disabled(rec: Record, cell_refs: dict | None, depth: int = 0) -> bool | None:
    """Whether the ref is disabled when the game starts: own 'initially disabled' flag, else derived
    from its enable-parent chain (only resolvable when the parent is in the same cell)."""
    if rec.flags & REC_FLAG_INITIALLY_DISABLED:
        return True
    xesp = rec.get("XESP")
    if not xesp:
        return False
    if depth > 16 or cell_refs is None:
        return None
    parent = cell_refs.get(kl(rec.plugin.resolve(struct.unpack_from("<I", xesp, 0)[0])))
    if parent is None:
        return None
    pd = _starts_disabled(parent[0], cell_refs, depth + 1)
    if pd is None:
        return None
    opposite = len(xesp) >= 8 and bool(struct.unpack_from("<I", xesp, 4)[0] & 1)
    return (not pd) if opposite else pd


def decode_ref(lo: LoadOrder, rec: Record, base_types: list[str], cell_refs: dict | None = None) -> dict | None:
    pl = rec.plugin
    name = rec.get("NAME")
    if not name:
        return None
    base_key = pl.resolve(struct.unpack_from("<I", name)[0])
    base = lo.lookup(base_key, base_types)
    if base is None:
        return None
    d = {"ref": fmt_ref(rec.key), "refEditorId": rec.edid(), "definedIn": pl.name, "baseType": base.type}
    if base.type == "CONT":
        d.update(decode_cont(base))
    else:
        d.update({"base": fmt_ref(base.key), "baseEditorId": base.edid(),
                  "baseName": base.plugin.lstring(base.get("FULL"))})
    d["initiallyDisabled"] = bool(rec.flags & REC_FLAG_INITIALLY_DISABLED)
    d["persistent"] = bool(rec.flags & REC_FLAG_PERSISTENT)
    d["deleted"] = bool(rec.flags & REC_FLAG_DELETED)
    xesp = rec.get("XESP")
    if xesp:
        parent_key = pl.resolve(struct.unpack_from("<I", xesp, 0)[0])
        eflags = struct.unpack_from("<I", xesp, 4)[0] if len(xesp) >= 8 else 0
        ep = {"ref": fmt_ref(parent_key), "oppositeOfParent": bool(eflags & 1), "popIn": bool(eflags & 2)}
        parent = cell_refs.get(kl(parent_key)) if cell_refs is not None else None
        if parent is not None:
            prec = parent[0]
            ep["editorId"] = prec.edid()
            pname = prec.get("NAME")
            if pname:
                ep["base"] = fmt_ref(prec.plugin.resolve(struct.unpack_from("<I", pname)[0]))
            ep["initiallyDisabled"] = bool(prec.flags & REC_FLAG_INITIALLY_DISABLED)
        else:
            ep["inThisCell"] = False
        d["enableParent"] = ep
    else:
        d["enableParent"] = None
    d["startsDisabled"] = _starts_disabled(rec, cell_refs)
    xown = rec.get("XOWN")
    if xown:
        d["owner"] = lo.describe(pl.resolve(struct.unpack_from("<I", xown)[0]), ["FACT", "NPC_"])
    ref_scripts = vmad_scripts(rec.get("VMAD"))
    if ref_scripts:
        d["refScripts"] = ref_scripts
    xlck = rec.get("XLCK")
    if xlck:
        d["lockLevel"] = xlck[0]
    pos = rec.get("DATA")
    if pos and len(pos) >= 24:
        x, y, z, rx, ry, rz = struct.unpack_from("<6f", pos)
        d["position"] = [round(x, 1), round(y, 1), round(z, 1)]
        d["rotationZDeg"] = round(math.degrees(rz), 1)
    return d


def default_load_order(data_dir: Path) -> list[str]:
    names = [n for n in BASE_MASTERS if (data_dir / n).exists()]
    ccc = data_dir.parent / "Skyrim.ccc"
    cc: list[str] = []
    if ccc.exists():
        for line in ccc.read_text(encoding="utf-8", errors="replace").splitlines():
            line = line.strip()
            if line and (data_dir / line).exists():
                cc.append(line)
    present = sorted(p.name for p in data_dir.iterdir()
                     if p.name.lower().startswith("cc") and p.suffix.lower() in (".esm", ".esl"))
    lower = {c.lower() for c in cc}
    cc += [p for p in present if p.lower() not in lower]
    # Keep the canonical casing from Skyrim.ccc (e.g. "ccBGSSSE001-Fish.esm"): that is how the game
    # and other plugins' master lists name the file, even when Steam stored it in lower case.
    return names + cc


def build_load_order(args) -> LoadOrder:
    data_dir = None
    if args.data:
        data_dir = Path(args.data)
    else:
        for c in DEFAULT_DATA_DIRS:
            if c and (Path(c) / "Skyrim.esm").exists():
                data_dir = Path(c)
                break
    if data_dir is None or not data_dir.exists():
        sys.exit("Skyrim Data directory not found; pass --data or set DOVASTASH_SKYRIM_DATA")
    if args.plugins:
        names = [n.strip() for n in args.plugins.split(",") if n.strip()]
    elif args.vanilla_only:
        names = [n for n in BASE_MASTERS if (data_dir / n).exists()]
    else:
        names = default_load_order(data_dir)
    names += args.add or []
    return LoadOrder(data_dir, names, args.lang)


# --------------------------------------------------------------------------------------------
# Commands
# --------------------------------------------------------------------------------------------
def cmd_plugins(lo: LoadOrder, args):
    return [{"plugin": p.name, "masters": p.masters, "light": p.light, "localized": p.localized,
             "master": bool(p.header_flags & TES4_FLAG_MASTER)} for p in lo.plugins]


def cmd_keywords(lo: LoadOrder, args):
    idx = lo.index("KYWD")
    by_edid = {}
    for rec in idx.values():
        e = rec.edid()
        if e:
            by_edid[e.lower()] = (e, fmt_ref(rec.key), rec.plugin.name)
    if args.check:
        res = {"found": {}, "missing": []}
        for name in args.check:
            for n in name.split(","):
                n = n.strip()
                if not n:
                    continue
                hit = by_edid.get(n.lower())
                if hit:
                    res["found"][hit[0]] = hit[1]
                else:
                    res["missing"].append(n)
        return res
    f = (args.filter or "").lower()
    return {e: ref for e, ref, _ in sorted(by_edid.values()) if f in e.lower()}


def cmd_cells(lo: LoadOrder, args):
    needle = args.query.lower()
    hits = {}
    for pl in lo.plugins:
        for rec, world in lo.iter_cells(pl):
            e = rec.edid() or ""
            match = needle in e.lower()
            name = None
            if not match and args.names and rec.get("FULL") is not None:
                name = pl.lstring(rec.get("FULL")) or ""
                match = needle in name.lower()
            if match:
                k = kl(rec.key)
                hits[k] = {"cell": fmt_ref(rec.key), "editorId": e or None,
                           "name": name if name is not None else pl.lstring(rec.get("FULL")),
                           "worldspace": fmt_ref(world) if world else None,
                           "lastOverride": pl.name}
    return list(hits.values())


def resolve_cell_arg(lo: LoadOrder, s: str):
    if "|" in s:
        return parse_ref(s)
    for pl in lo.plugins:
        for rec, _w in lo.iter_cells(pl):
            if (rec.edid() or "").lower() == s.lower():
                return rec.key
    sys.exit(f"cell {s!r} not found (use the 'cells' subcommand to search)")


def cmd_cell(lo: LoadOrder, args):
    key = resolve_cell_arg(lo, args.cell)
    rec, world = lo.cell_record(key)
    if rec is None:
        sys.exit(f"cell {fmt_ref(key)} not found in the load order")
    out = decode_cell(lo, rec, world)
    base_types = ["CONT"] + [t.strip().upper() for t in (args.with_types or "").split(",") if t.strip()]
    refs = lo.cell_refs(key)
    rows = []
    for rrec, gtype in refs.values():
        d = decode_ref(lo, rrec, base_types, refs)
        if d is None:
            continue
        if d["deleted"] and not args.include_deleted:
            continue
        rows.append(d)
    rows.sort(key=lambda r: (r["baseType"] != "CONT", r.get("baseName") or "", r["ref"]))
    out["containers"] = [r for r in rows if r["baseType"] == "CONT"]
    # Multi-cell homes (DESIGN 6.7): from the home's other cells, only persistent references can be reached.
    for r in out["containers"]:
        r["reachableFromOtherCells"] = r["persistent"]
    out["persistentCount"] = sum(1 for r in out["containers"] if r["persistent"])
    # Fronts: activators whose default linked ref (XLKR, no keyword) is a container in this cell open that
    # container (shelves and racks in front of a hidden chest). Dova Stash binds the container behind them.
    by_ref = {r["ref"]: r for r in out["containers"]}
    for rrec, _gtype in refs.values():
        name = rrec.get("NAME")
        if not name or rrec.flags & REC_FLAG_DELETED:
            continue
        base = lo.lookup(rrec.plugin.resolve(struct.unpack_from("<I", name)[0]), ["ACTI"])
        if base is None:
            continue
        for xlkr in rrec.get_all("XLKR"):
            if len(xlkr) < 8 or struct.unpack_from("<I", xlkr, 0)[0] != 0:
                continue
            target = by_ref.get(fmt_ref(rrec.plugin.resolve(struct.unpack_from("<I", xlkr, 4)[0])))
            if target is not None:
                target.setdefault("openedBy", []).append({
                    "ref": fmt_ref(rrec.key), "baseEditorId": base.edid(),
                    "baseName": base.plugin.lstring(base.get("FULL"))})
    out["containerCount"] = len(out["containers"])
    out["respawningCount"] = sum(1 for r in out["containers"] if r.get("respawns"))
    others = [r for r in rows if r["baseType"] != "CONT"]
    if others:
        out["otherRefs"] = others
    displays = find_displays(lo, key, args.include_deleted)
    if displays:
        out["displays"] = displays
        counts = {}
        for d in displays:
            counts[d["kind"]] = counts.get(d["kind"], 0) + 1
        out["displayCounts"] = counts
    return out


DISPLAY_ITEM_TYPES = ["ARMO", "WEAP", "BOOK", "MISC", "ALCH", "INGR", "SLGM", "AMMO", "SCRL"]
RACK_TYPES = {1: "Any", 2: "Shield", 3: "NoDaggerOrBow", 4: "Dagger", 5: "LargeCase"}


def _scripts_with_props(lo: LoadOrder, rec: Record, base: Record) -> dict[str, dict]:
    """Script name (lower) -> properties: the base record's, overridden by the reference's. Object values are
    resolved to load-order keys."""
    merged: dict[str, dict] = {}
    for r in (base, rec):
        for name, props in vmad_decode(r.get("VMAD")):
            entry = merged.setdefault(name.lower(), {"name": name, "props": {}})
            for k, v in props.items():
                if isinstance(v, tuple):
                    v = ("form", r.plugin.resolve(v[1]))
                elif isinstance(v, list):
                    v = [("form", r.plugin.resolve(x[1])) if isinstance(x, tuple) else x for x in v]
                entry["props"][k] = v
    return merged


def _display_items(lo: LoadOrder, props: dict) -> list[dict]:
    """Playable item forms a script's properties point at (form lists expanded)."""
    items, seen = [], set()

    def add(key, depth=0):
        if kl(key) in seen:
            return
        rec = lo.lookup(key, DISPLAY_ITEM_TYPES)
        if rec is not None:
            seen.add(kl(key))
            items.append({"ref": fmt_ref(rec.key), "editorId": rec.edid(), "name": rec.plugin.lstring(rec.get("FULL"))})
            return
        flst = lo.lookup(key, ["FLST"]) if depth == 0 else None
        if flst is not None:
            for lnam in flst.get_all("LNAM"):
                add(flst.plugin.resolve(struct.unpack_from("<I", lnam)[0]), depth + 1)

    for v in props.values():
        for x in (v if isinstance(v, list) else [v]):
            if isinstance(x, tuple) and x[0] == "form":
                add(x[1])
    return items


def _is_bookshelf_script(name: str, props: dict) -> bool:
    """PlayerBookShelfContainerScript, or a home mod's copy of it (its book marker and trigger keywords)."""
    return name.lower() == "playerbookshelfcontainerscript" or ("BookShelfBook01" in props and "BookShelfTrigger01" in props)


def bookshelf_capacity(rec: Record, props: dict) -> int | None:
    """How many books the shelf shows, as its script counts them on its first cell load (CountMaxBooks): the
    markers linked with BookShelfBook01, BookShelfBook02... up to the first one missing. None if the script has
    no BookShelfBook01 keyword."""
    links = {}
    for x in rec.get_all("XLKR"):
        if len(x) >= 8:
            kw, target = struct.unpack_from("<II", x)
            if kw:
                links[kl(rec.plugin.resolve(kw))] = target
    n = 0
    for i in range(1, 19):
        v = props.get(f"BookShelfBook{i:02}")
        if not (isinstance(v, tuple) and v[0] == "form"):
            return None if i == 1 else n
        if kl(v[1]) not in links:
            return n
        n += 1
    return n


def find_displays(lo: LoadOrder, cell_key, include_deleted=False) -> list[dict]:
    """Displays Dova Stash fills (DESIGN 8.4), as far as the plugin files show them: vanilla weapon racks
    (WeaponRackActivateSCRIPT), mannequins (MannequinActivatorSCRIPT), bookshelves
    (PlayerBookShelfContainerScript and copies of it, with the number of books they show), plaques whose script
    points at items ("item"; in game it also needs a 'placed' flag, which is rarely set in the plugin) and
    containers whose script points at items ("case"; in game it also needs an OnItemAdded event)."""
    out = []
    refs = lo.cell_refs(cell_key, ("REFR", "ACHR"))
    for rec, _gtype in refs.values():
        name = rec.get("NAME")
        if not name or (rec.flags & REC_FLAG_DELETED and not include_deleted):
            continue
        base = lo.lookup(rec.plugin.resolve(struct.unpack_from("<I", name)[0]), ["ACTI", "CONT", "NPC_", "MSTT", "STAT"])
        if base is None:
            continue
        scripts = _scripts_with_props(lo, rec, base)
        if not scripts:
            continue
        d = {"ref": fmt_ref(rec.key), "baseType": base.type, "baseEditorId": base.edid(), "baseName": base.plugin.lstring(base.get("FULL"))}
        rack = scripts.get("weaponrackactivatescript")
        if rec.type == "ACHR" or base.type == "NPC_":
            if "mannequinactivatorscript" in scripts:
                out.append({**d, "kind": "mannequin", "script": scripts["mannequinactivatorscript"]["name"]})
            continue
        if rack is not None:
            props = rack["props"]
            rtype = 3 if props.get("Patch14COARacks") else props.get("RackType", 1)
            out.append({**d, "kind": "rack", "rackType": RACK_TYPES.get(rtype, f"unknown ({rtype})"), "script": rack["name"]})
            continue
        shelf = next((e for e in scripts.values() if base.type == "CONT" and _is_bookshelf_script(e["name"], e["props"])), None)
        if shelf is not None:
            out.append({**d, "kind": "bookshelf", "capacity": bookshelf_capacity(rec, shelf["props"]), "script": shelf["name"]})
            continue
        for entry in scripts.values():
            items = _display_items(lo, entry["props"])
            if items:
                out.append({**d, "kind": "case" if base.type == "CONT" else "item", "script": entry["name"], "items": items})
                break
    out.sort(key=lambda r: (r["kind"], r.get("baseEditorId") or "", r["ref"]))
    return out


def cmd_ref(lo: LoadOrder, args):
    key = parse_ref(args.ref)
    want = kl(key)
    found = None
    for pl in lo.plugins:
        for top in ("CELL", "WRLD"):
            if top not in pl.top:
                continue
            off, size = pl.top[top]
            cell = None
            for item in pl.walk(off + 24, off + size, descend=lambda l, gt: True):
                if item[0] == "GRUP" and item[4] == 6:
                    cell = pl.resolve(struct.unpack("<I", item[3])[0])
                elif item[0] == "REC" and item[1].type in ("REFR", "ACHR") and kl(item[1].key) == want:
                    found = (item[1], cell)
    if not found:
        sys.exit(f"reference {args.ref} not found")
    rec, cell = found
    d = decode_ref(lo, rec, ["CONT", "FURN", "ACTI", "STAT", "MSTT", "DOOR", "LIGH", "MISC", "NPC_", "FLOR",
                              "TACT", "IDLM", "MOVS"] + ITEM_TYPES) or {"ref": fmt_ref(rec.key), "baseType": None}
    d["cell"] = fmt_ref(cell) if cell else None
    if cell:
        crec, _ = lo.cell_record(cell)
        d["cellEditorId"] = crec.edid() if crec else None
    return d


def cmd_keyword_usage(lo: LoadOrder, args):
    kidx = lo.index("KYWD")
    wanted = {}
    for n in args.keywords:
        for k in n.split(","):
            k = k.strip()
            if k:
                wanted[k.lower()] = k
    key_to_name = {}
    for rec in kidx.values():
        e = rec.edid()
        if e and e.lower() in wanted:
            key_to_name[kl(rec.key)] = e
    res = {wanted[w]: ({} if w in {v.lower() for v in key_to_name.values()} else "MISSING") for w in wanted}
    samples: dict[str, list[str]] = {}
    types = [t.strip().upper() for t in args.types.split(",")] if args.types else ITEM_TYPES
    for t in types:
        for rec in lo.index(t).values():
            kw = rec.get("KWDA")
            if not kw:
                continue
            for i in range(len(kw) // 4):
                k = kl(rec.plugin.resolve(struct.unpack_from("<I", kw, i * 4)[0]))
                name = key_to_name.get(k)
                if name:
                    res[name][t] = res[name].get(t, 0) + 1
                    s = samples.setdefault(name, [])
                    if len(s) < args.samples:
                        s.append(rec.edid() or fmt_ref(rec.key))
    return {"usage": res, "samples": samples}


# --------------------------------------------------------------------------------------------
# check: validate Dova Stash layout/template and category files against the load order
# --------------------------------------------------------------------------------------------
ENUMS = {
    "weaponTypes": ["HandToHand", "OneHandSword", "OneHandDagger", "OneHandAxe", "OneHandMace", "TwoHandSword",
                    "TwoHandAxe", "Bow", "Staff", "Crossbow"],
    "armorTypes": ["Light", "Heavy", "Clothing"],
    "bookTeaches": ["None", "Skill", "Spell"],
    "alchemy": ["Potion", "Poison", "Food"],
    "soulGem": ["Empty", "Filled"],
    "ammo": ["Arrow", "Bolt"],
}
MATCH_KEYS = {"formTypes", "keywordsAny", "keywordsAll", "keywordsNone", "forms", "formLists", "plugins",
              "weaponTypes", "armorTypes", "bookTeaches", "alchemy", "soulGem", "enchanted", "ammo"}
CELLS_KEYS = {"cell", "label", "note"}
LAYOUT_KEYS = {"$schema", "name", "note", "author", "version", "priority", "requires", "cell", "cells", "master", "containers", "displays",
               "keep", "learned", "declined", "basedOn"}
CONTAINER_KEYS = {"ref", "label", "note", "categories", "ignored", "forms", "sell"}
DISPLAY_KEYS = {"ref", "kind", "label", "note", "categories", "items", "only", "set", "ignored"}
DISPLAY_KINDS = {"rack", "mannequin", "item", "case", "bookshelf"}


def _as_list(v):
    return v if isinstance(v, list) else [v]


def _pattern_known(cat, category_ids):
    # Same rules as the plugin: "*" is everything, "x.*" is x and everything below it, ids ignore case.
    if cat == "*":
        return True
    ids = {x.lower() for x in category_ids}
    c = cat.lower()
    if c.endswith(".*"):
        base = c[:-2]
        return any(x == base or x.startswith(base + ".") for x in ids)
    return c in ids


def _check_ref_syntax(s, where, errors):
    try:
        plugin, fid = parse_ref(s)
    except (ValueError, AttributeError):
        errors.append(f"{where}: malformed form ref {s!r} (expected 'Plugin.esm|0x00ABCDEF')")
        return None
    if not s.split("|", 1)[1].lower().startswith("0x"):
        errors.append(f"{where}: form id in {s!r} must be hex with a 0x prefix")
    return plugin, fid


def check_categories(lo, doc, errors, warnings):
    ids = []
    kw = {(r.edid() or "").lower() for r in lo.index("KYWD").values()}
    loaded = {p.name.lower() for p in lo.plugins}
    for i, c in enumerate(doc.get("categories", [])):
        cid = c.get("id")
        if not cid:
            errors.append(f"categories[{i}]: missing id")
            continue
        where = f"category {cid}"
        if cid in ids:
            errors.append(f"{where}: duplicate id")
        ids.append(cid)
        if not c.get("name"):
            warnings.append(f"{where}: no name")
        if "priority" in c and not isinstance(c["priority"], int):
            errors.append(f"{where}: priority must be an integer")
        m = c.get("match", {})
        for k in m:
            if k not in MATCH_KEYS:
                errors.append(f"{where}: unknown predicate {k!r}")
        for t in _as_list(m.get("formTypes", [])):
            if t not in ITEM_TYPES:
                errors.append(f"{where}: unknown formType {t!r}")
        for k in ("keywordsAny", "keywordsAll", "keywordsNone"):
            for e in _as_list(m.get(k, [])):
                if e.lower() not in kw:
                    errors.append(f"{where}: keyword {e!r} does not exist in the loaded plugins")
        for k, allowed in ENUMS.items():
            for v in _as_list(m.get(k, [])):
                if str(v).lower() not in {a.lower() for a in allowed}:
                    errors.append(f"{where}: {k} value {v!r} not one of {allowed}")
        types = [t for t in _as_list(m.get("formTypes", [])) if t in ITEM_TYPES] or ITEM_TYPES
        for f in _as_list(m.get("forms", [])):
            key = _check_ref_syntax(f, where, errors)
            if key and not any(lo.index(t).get(kl(key)) for t in types):
                (errors if key[0].lower() in loaded else warnings).append(
                    f"{where}: form {f} not found as {'/'.join(types)}")
    return ids


def check_layout(lo, doc, errors, warnings, category_ids):
    for k in doc:
        if k not in LAYOUT_KEYS:
            warnings.append(f"unknown top-level key {k!r}")
    for k in ("name", "cell", "master", "containers"):
        if k not in doc:
            errors.append(f"missing required key {k!r}")
    for k in ("learned", "declined", "basedOn"):
        if k in doc:
            warnings.append(f"{k!r} belongs in player Layouts only, not in shipped templates")
    requires = [r.lower() for r in doc.get("requires", [])]
    loaded = {p.name.lower() for p in lo.plugins}
    missing_plugins = [r for r in doc.get("requires", []) if r.lower() not in loaded]
    if missing_plugins:
        warnings.append(f"required plugins not in the load order, refs not verified: {missing_plugins}")
    refs = []
    cell_key = _check_ref_syntax(doc.get("cell", ""), "cell", errors)
    # More cells of a multi-cell home (DESIGN 6.7): strings or {"cell", "label", "note"}.
    extra_cells = []
    cells_val = doc.get("cells", [])
    for i, entry in enumerate(cells_val if isinstance(cells_val, list) else [cells_val]):
        where = f"cells[{i}]"
        if isinstance(entry, dict):
            for k in entry:
                if k not in CELLS_KEYS:
                    warnings.append(f"{where}: unknown key {k!r}")
            text = entry.get("cell")
        else:
            text = entry
        if not isinstance(text, str):
            errors.append(f"{where}: expected a cell form ref or an object with 'cell'")
            continue
        ck = _check_ref_syntax(text, where, errors)
        if not ck:
            continue
        if cell_key and kl(ck) == kl(cell_key):
            warnings.append(f"{where}: {fmt_ref(ck)} is the home's main cell (ignored)")
        elif any(kl(ck) == kl(e) for e in extra_cells):
            warnings.append(f"{where}: duplicate cell {fmt_ref(ck)}")
        else:
            extra_cells.append(ck)
    master = doc.get("master")
    mk = _check_ref_syntax(master, "master", errors) if master else None
    if mk:
        refs.append(("master", mk, {}))
    seen, cats_seen = {}, {}
    for i, c in enumerate(doc.get("containers", [])):
        where = f"containers[{i}]"
        for k in c:
            if k not in CONTAINER_KEYS:
                warnings.append(f"{where}: unknown key {k!r}")
        key = _check_ref_syntax(c.get("ref", ""), where, errors)
        if not key:
            continue
        if kl(key) in seen:
            errors.append(f"{where}: duplicate ref {c['ref']} (also {seen[kl(key)]})")
        seen[kl(key)] = where
        if mk and kl(key) == kl(mk):
            warnings.append(f"{where}: the master is also listed as a container")
        if not c.get("label"):
            warnings.append(f"{where}: no label")
        if c.get("ignored") and c.get("categories"):
            warnings.append(f"{where}: ignored container also has categories")
        # The sell chest (DESIGN 8.7): a boolean; inert on an ignored container or the master.
        if "sell" in c and not isinstance(c["sell"], bool):
            errors.append(f"{where}: sell must be true or false")
        elif c.get("sell") and (c.get("ignored") or (mk and kl(key) == kl(mk))):
            warnings.append(f"{where}: sell has no effect on an ignored container or the master")
        for cat in c.get("categories", []):
            if cat in cats_seen:
                warnings.append(f"{where}: category {cat!r} also bound at {cats_seen[cat]}")
            cats_seen[cat] = where
            if category_ids is not None:
                if not _pattern_known(cat, category_ids):
                    errors.append(f"{where}: unknown category {cat!r}")
        refs.append((where, key, c))
    # Displays (DESIGN 8.4 "Displays in templates"): the display refs need their plugins in 'requires'; the items
    # and sets of racks and mannequins are soft references (skipped in games without their plugin), never required.
    display_refs, soft_refs, display_entries = [], [], []
    storage = {kl(mk)} if mk else set()
    storage |= {kl(r[1]) for r in refs if r[0] != "master" and not r[2].get("ignored")}
    for i, d in enumerate(doc.get("displays", [])):
        where = f"displays[{i}]"
        if not isinstance(d, dict):
            errors.append(f"{where}: expected an object")
            continue
        for k in d:
            if k not in DISPLAY_KEYS:
                warnings.append(f"{where}: unknown key {k!r}")
        if "kind" in d and str(d["kind"]).lower() not in DISPLAY_KINDS:
            errors.append(f"{where}: kind {d['kind']!r} is not one of {sorted(DISPLAY_KINDS)}")
        if "only" in d and not isinstance(d["only"], bool):
            errors.append(f"{where}: 'only' must be true or false")
        for cat in _as_list(d.get("categories", [])):
            if category_ids is not None and not _pattern_known(cat, category_ids):
                errors.append(f"{where}: unknown category {cat!r}")
        key = _check_ref_syntax(d.get("ref", ""), where, errors)
        if key:
            display_refs.append((where, key))
            display_entries.append((where, key, d))
            if kl(key) in storage:
                warnings.append(f"{where}: {fmt_ref(key)} is also the master or a container in use: it is storage, never filled as a display")
        for j, item in enumerate(_as_list(d.get("items", []))):
            ik = _check_ref_syntax(item, f"{where}.items[{j}]", errors)
            if ik:
                soft_refs.append((f"{where}.items[{j}]", ik))
        if d.get("only") is True and not d.get("items"):
            warnings.append(f"{where}: 'only' without 'items' does nothing")
        elif d.get("only") is True and "set" in d:
            warnings.append(f"{where}: 'only' is ignored next to 'set' (the set decides what the mannequin wears)")
        if "set" in d:
            sk = _check_ref_syntax(d["set"], f"{where}.set", errors)
            if sk:
                soft_refs.append((f"{where}.set", sk))
    for k in [cell_key] + extra_cells + [r[1] for r in refs] + [r[1] for r in display_refs]:
        if k and k[0].lower() not in requires:
            errors.append(f"plugin {k[0]} is referenced but not listed in 'requires'")
    optional = sorted({k[0] for _w, k in soft_refs if k[0].lower() not in requires}, key=str.lower)
    if optional:
        info_optional = {"optionalPlugins": optional}  # display items/sets: fine without them (skipped, one info log line)
    else:
        info_optional = {}
    player_layout = "learned" in doc or "declined" in doc or "basedOn" in doc

    def kind_warnings(script_kinds):
        # The kind the scripts give a display wins over the entry's 'kind' (as in the DLL).
        for where, key, d in display_entries:
            kind = script_kinds.get(kl(key)) or (str(d["kind"]).lower() if "kind" in d else None)
            if kind is None:
                continue
            if "set" in d and kind != "mannequin":
                warnings.append(f"{where}: 'set' is only used by mannequins (this is a {kind})")
            if kind in ("item", "case"):
                if d.get("categories"):
                    warnings.append(f"{where}: 'categories' are only used by racks, mannequins and bookshelves (this is a {kind})")
                if d.get("only") is True:
                    warnings.append(f"{where}: 'only' is for racks, mannequins and bookshelves; an item plaque or case always takes just what it is made for")
                if d.get("items") and kl(key) in script_kinds and not player_layout:
                    warnings.append(f"{where}: an {kind} display takes what its script is made for; a template's 'items' are not used")
            if kind == "bookshelf":
                for j, item in enumerate(_as_list(d.get("items", []))):
                    try:
                        ik = parse_ref(item)
                    except (ValueError, AttributeError):
                        continue
                    if ik[0].lower() in loaded and lo.lookup(ik, DISPLAY_ITEM_TYPES) is not None and lo.lookup(ik, ["BOOK"]) is None:
                        warnings.append(f"{where}.items[{j}]: {fmt_ref(ik)} is not a book; a bookshelf only takes books")

    info = dict(info_optional)
    if not cell_key or missing_plugins:
        kind_warnings({})
        return info
    crec, _world = lo.cell_record(cell_key)
    if crec is None:
        errors.append(f"cell {doc['cell']} not found")
        kind_warnings({})
        return info
    info["cellEditorId"] = crec.edid()
    info["cellName"] = crec.plugin.lstring(crec.get("FULL"))
    cell_refs = lo.cell_refs(cell_key)
    # A multi-cell home: its references may be in any of its cells. Which cell each is in decides whether the
    # other cells reach it (only persistent references, DESIGN 6.7).
    ref_cell = {k: cell_key for k in cell_refs}
    home_cells = [cell_key]
    for ck in extra_cells:
        if ck[0].lower() not in loaded:
            continue
        xrec, _xworld = lo.cell_record(ck)
        if xrec is None:
            errors.append(f"cells: cell {fmt_ref(ck)} not found")
            continue
        home_cells.append(ck)
        for k, v in lo.cell_refs(ck).items():
            cell_refs.setdefault(k, v)
            ref_cell.setdefault(k, ck)
    if len(home_cells) > 1:
        info["cellsChecked"] = len(home_cells)
        info["onlyReachableInTheirCell"] = []
    where_text = f"cell {doc['cell']}" if len(home_cells) == 1 else "any of the home's cells"
    for where, key, entry in refs:
        hit = cell_refs.get(kl(key))
        if hit is None:
            errors.append(f"{where}: {fmt_ref(key)} is not a reference in {where_text}")
            continue
        if len(home_cells) > 1 and not (hit[0].flags & REC_FLAG_PERSISTENT) and (where == "master" or not entry.get("ignored")):
            crec2, _w = lo.cell_record(ref_cell[kl(key)])
            info["onlyReachableInTheirCell"].append(f"{where} {fmt_ref(key)} ({crec2.edid() if crec2 else fmt_ref(ref_cell[kl(key)])})")
        d = decode_ref(lo, hit[0], ["CONT"], cell_refs)
        if d is None:
            errors.append(f"{where}: {fmt_ref(key)} is not a container (base is not a CONT)")
            continue
        bound = where == "master" or not entry.get("ignored")
        if d["deleted"]:
            errors.append(f"{where}: {fmt_ref(key)} is deleted")
        if d["respawns"] and bound:
            errors.append(f"{where}: {fmt_ref(key)} ({d['baseEditorId']}) RESPAWNS - never bind it")
        if bound and "PlayerBookShelfContainerScript" in d.get("baseScripts", []):
            warnings.append(f"{where}: {fmt_ref(key)} is a scripted bookshelf (it holds a few books and hands the rest back): "
                            "mark it ignored and list it under 'displays' instead")
        ep = d.get("enableParent") or {}
        divert = any(s.lower() == "byohdivertitemscript" for s in d.get("refScripts", []))
        if bound and ep.get("oppositeOfParent") and not divert:
            warnings.append(f"{where}: {fmt_ref(key)} disappears when its enable parent is enabled (house upgrade?)")
        if where == "master" and d.get("startsDisabled"):
            warnings.append(f"master {fmt_ref(key)} starts disabled (the game enables it later, e.g. when built)")
    info["containersChecked"] = len(refs)
    if doc.get("displays"):
        actor_refs = {}
        for hc in home_cells:
            actor_refs.update(lo.cell_refs(hc, ("REFR", "ACHR")))
        for where, key in display_refs:
            if kl(key) not in actor_refs:
                errors.append(f"{where}: {fmt_ref(key)} is not a reference in {where_text}")
        for where, key in soft_refs:
            if key[0].lower() not in loaded:
                continue  # optional: skipped in games without that plugin (optionalPlugins)
            if where.endswith(".set"):
                if lo.lookup(key, ["ARMO"]) is None:
                    errors.append(f"{where}: {fmt_ref(key)} is not an armor piece (ARMO)")
            elif lo.lookup(key, DISPLAY_ITEM_TYPES) is None:
                errors.append(f"{where}: {fmt_ref(key)} is not an item ({', '.join(DISPLAY_ITEM_TYPES)})")
        script_kinds = {kl(parse_ref(d["ref"])): d["kind"] for hc in home_cells for d in find_displays(lo, hc)}
        kind_warnings(script_kinds)
        info["displaysChecked"] = len(doc["displays"])
    return info


def strip_json_comments(text: str) -> str:
    """Removes // and /* */ comments outside strings, as the DLL does when it reads JSON."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            while i < n and text[i] not in "\r\n":
                i += 1
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            out.append(" ")
        else:
            out.append(c)
            i += 1
    return "".join(out)


def cmd_check(lo, args):
    files = []
    for f in args.files:
        p = Path(f)
        files += sorted(p.rglob("*.json")) if p.is_dir() else [p]
    files += [Path(c) for c in (args.categories or [])]
    docs = []
    for f in files:
        try:
            docs.append((f, json.loads(strip_json_comments(f.read_text(encoding="utf-8-sig"))), None))
        except (OSError, json.JSONDecodeError) as e:
            docs.append((f, None, f"invalid JSON: {e}"))
    # category files first, so layouts can be checked against the known category ids
    docs.sort(key=lambda t: 0 if t[1] and t[1].get("$schema") == "dovastash-categories-1" else 1)
    cats, results, ok = None, [], True
    for f, doc, err in docs:
        errors, warnings, info = ([err] if err else []), [], {}
        if doc is not None:
            schema = doc.get("$schema")
            if schema == "dovastash-categories-1":
                ids = check_categories(lo, doc, errors, warnings)
                cats = (cats or []) + ids
                info["categories"] = len(ids)
            elif schema == "dovastash-layout-1":
                info = check_layout(lo, doc, errors, warnings, cats)
            else:
                errors.append(f"unknown $schema {schema!r}")
        ok = ok and not errors
        results.append({"file": str(f), "errors": errors, "warnings": warnings, **info})
    out = {"ok": ok, "results": results}
    if not ok:
        sys.stdout.reconfigure(encoding="utf-8")
        print(json.dumps(out, indent=2, ensure_ascii=False))
        sys.exit(1)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", help="Skyrim 'Data' directory (default: $DOVASTASH_SKYRIM_DATA or common Steam paths)")
    ap.add_argument("--plugins", help="explicit comma-separated load order (overrides the default)")
    ap.add_argument("--vanilla-only", action="store_true", help="only Skyrim/Update/Dawnguard/HearthFires/Dragonborn")
    ap.add_argument("--add", action="append", metavar="PLUGIN",
                    help="append a plugin (file name in Data, or a full path, e.g. from an MO2 mod folder); repeatable")
    ap.add_argument("--lang", default="english", help="Strings language (default: english)")
    ap.add_argument("--compact", action="store_true", help="single-line JSON")
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("plugins", help="show the resolved load order")

    p = sub.add_parser("keywords", help="list keyword EditorIDs -> form ref")
    p.add_argument("--filter", help="case-insensitive substring filter")
    p.add_argument("--check", nargs="+", metavar="EDID", help="verify that these keyword EditorIDs exist")

    p = sub.add_parser("cells", help="find cells by EditorID substring")
    p.add_argument("query")
    p.add_argument("--names", action="store_true", help="also match the in-game cell name (slower)")

    p = sub.add_parser("cell", help="dump a cell and its container references")
    p.add_argument("cell", help="cell EditorID (exact) or form ref 'Plugin.esm|0x0001234'")
    p.add_argument("--with", dest="with_types", metavar="TYPES",
                   help="also list refs of these base types, e.g. FURN,ACTI (crafting stations, racks)")
    p.add_argument("--include-deleted", action="store_true")

    p = sub.add_parser("ref", help="describe one placed reference and the cell it lives in")
    p.add_argument("ref", help="form ref 'Plugin.esm|0x0001234'")

    p = sub.add_parser("keyword-usage", help="count items that carry the given keywords")
    p.add_argument("keywords", nargs="+")
    p.add_argument("--types", help="record types to scan (default: all item types)")
    p.add_argument("--samples", type=int, default=5)

    p = sub.add_parser("check", help="validate Dova Stash template/layout and category JSON files")
    p.add_argument("files", nargs="+", help="JSON files or directories (searched recursively)")
    p.add_argument("--categories", nargs="*", help="extra category files used to resolve category ids")

    args = ap.parse_args(argv)
    lo = build_load_order(args)
    fn = {"plugins": cmd_plugins, "keywords": cmd_keywords, "cells": cmd_cells, "cell": cmd_cell,
          "ref": cmd_ref, "keyword-usage": cmd_keyword_usage, "check": cmd_check}[args.cmd]
    result = fn(lo, args)
    out = json.dumps(result, ensure_ascii=False, indent=None if args.compact else 2)
    sys.stdout.reconfigure(encoding="utf-8")
    print(out)


if __name__ == "__main__":
    main()
