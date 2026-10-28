"""Generate Dova Stash home templates from hand-picked bindings + facts read with tools/esm_dump.py.

Ignored entries are derived automatically from the plugin data:
  * base CONT has the respawn flag                      -> ignored ("respawns")
  * PlayerBookShelfContainer (scripted display shelf)   -> ignored (never storage; a template may also list
    it under "displays" as a bookshelf, kind "bookshelf", to have Deposit put books on it)
  * enabled only until a house upgrade (enable parent with "opposite" flag) and no Hearthfire
    divert script                                        -> ignored ("removed by an upgrade")
  * explicit extra ignores (quest/test containers)
Ignored entries are labelled with the game's name for the container in sentence case and where it stands (the
nearest bound container: "Bookshelf (by the library chest)", numbered when the same label comes again); the reason
goes in "note".

Labels, one form everywhere (hotkey lists, panel): "<Kind> (by the <landmark>)", "<Kind> (by the <landmark>, 2)" for
repeats; "(second)", "(third)" only where there is no landmark; a short place where no landmark fits ("(east)", "(back
room)", "(upgrade)"). What a container holds goes before its kind ("Food barrel"), never in the parentheses
(esm_dump.label_problem; `check` warns too). A landmark is never the kind itself ("Bookshelf (by the bookshelves)") and
names at most 3 of one kind while another landmark is about as near.

Displays (DESIGN 8.4) come from the plugin data too: every bookshelf, named weapon rack (weapon, shield and dagger
racks, plaques, the parts of a coat-of-arms rack) and mannequin of the home's own plugins, without categories (each
takes what fits it: books but notes, the weapons or shield its rack holds, armor) and without a set (mannequins pick
one). Item plaques and display cases are left to the player: the plugin files cannot tell which ones the game fills.
Displays that go away with a house upgrade are left out.

`misc.curios` goes with `misc.gems` on every template (the gem container takes both).

Mod homes (the "Mods/" section) need their plugin: it is looked up in the `mods` folder of the MO2
instance in the environment variable DOVASTASH_MO2 (else the developer's) and the template is left as it is when it is not there.
"""
import argparse
import os
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))  # tools/ -> esm_dump
sys.path.insert(0, str(HERE))
import esm_dump as E  # noqa: E402
import jsonfmt  # noqa: E402

OUT = HERE.parents[1] / "dist" / "Data" / "SKSE" / "Plugins" / "DovaStash" / "Templates"
# Default load order (base game + DLC + every installed Creation Club plugin); set DOVASTASH_SKYRIM_DATA
# to point at another install.
LO = E.build_load_order(argparse.Namespace(data=None, plugins=None, vanilla_only=False, add=None, lang="english"))

# Mods that rebuild a home (DESIGN 6.2, "conflicts"): the template is not offered while one is loaded.
# WynterBreezehomeRedux.esp: Wynter's Breezehome Redux (checked in the MO2 mods folder). PlayerHouse.esp: Player
# houses overhaul, rebuilds the five city homes (name from its Nexus page; also in SelfSorting/00_Default.json).
CITY = ["PlayerHouse.esp"]
CONFLICTS = {
    "Vanilla/Breezehome.json": ["WynterBreezehomeRedux.esp"] + CITY,
    "Vanilla/Honeyside.json": CITY,
    "Vanilla/VlindrelHall.json": CITY,
    "Vanilla/Hjerim.json": CITY,
    "Vanilla/ProudspireManor.json": CITY,
}

# Each template's version: raise it with every change to the file (players' layouts name the version they came from).
# 1.0.1: ignored containers labelled by where they stand; 1.1.0: the Hearthfire wings' other choices bound.
# One patch more (1.0.2, 1.1.1, 1.0.1): displays listed, misc.curios with the gems, labels in one form; the city homes'
# child's bedroom furniture and more of the Arch-Mage's Quarters bound. Mod homes start at 1.0.0.
# One patch more where a label changed (1.0.3, 1.1.2, 1.0.1): never named after itself, at most 3 per landmark.
# Then: Wuth Rein's scripted shelves out of its displays (1.0.2); the thin mod homes bind more of their furniture, or say
# why they cannot (1.0.1); clearer labels (Winter Cove, the Tundra Homestead cellar's bookcases; "armor", not "armour").
# Then: notes say why categories go to the master (Goldenhills, Honeyside, Myrwatch and its gallery, Winter Cove); Winter
# Cove binds every category and the main hall's soul gem container, wardrobe and end table; the Tundra Homestead cellar's
# bookcases labelled by a landmark, no clutter with the jewelry; Niflholm's ignored containers by landmarks, not cells;
# Sicarius' Refuge's disabled mannequin out of its displays (1.0.2).
# Then: one template per multi-cell home (DESIGN 6.7): Lakeview Manor, Windstad Manor and Heljarchen Hall with their cellar
# and grounds (1.2.0), Tundra Homestead with its cellar and Myrwatch with its gallery (1.1.0); the cellar and gallery
# templates are retired (RETIRED; each home's `replaces` names them).
# Then: an own master for each area of Tel Jerdein and Niflholm, and Winter Cove's master in the main hall (its storage
# room's chest that area's master) (1.1.0); what a container holds out of the parentheses of its label (Lost Hall, Severin
# Manor, Tundra Homestead), the gallery's containers called so in Myrwatch, and a generic note for a container that goes
# away later (Nchuanthumz, Proudspire Manor): one patch more.
# Then: no repeat label without its first (the Hearthfire storage room chests, Hendraheim's wardrobe), "Grounds chest":
# one patch more (Severin Manor's armory sack: in its 1.0.4, not shipped yet).
# Then: "(back room, 2)", not "(back room, second)" (the Hearthfire homes): one patch more.
# Then: the Hearthfire notes say where cellar leftovers go and which of the containers sharing a category takes it: one
# patch more.
VERSIONS = {
    "Vanilla/LakeviewManor.json": "1.2.3", "Vanilla/WindstadManor.json": "1.2.3", "Vanilla/HeljarchenHall.json": "1.2.3",
    "CreationClub/Hendraheim.json": "1.0.4",
    "CreationClub/TundraHomestead.json": "1.1.1", "CreationClub/Myrwatch.json": "1.1.1",
    "Vanilla/SeverinManor.json": "1.0.4", "CreationClub/Nchuanthumz.json": "1.0.4", "Vanilla/ProudspireManor.json": "1.0.4",
    "CreationClub/DeadMansDread.json": "1.0.1", "Vanilla/ArchMagesQuarters.json": "1.0.1",
    "Vanilla/Breezehome.json": "1.0.2", "CreationClub/GallowsHall.json": "1.0.2",
    "CreationClub/GoldenhillsPlantation.json": "1.0.3", "Vanilla/Honeyside.json": "1.0.3",
    "Mods/Niflholm.json": "1.1.0",
    "Mods/PinegroveLodge.json": "1.0.2", "Mods/BalRuhn.json": "1.0.1", "Mods/WuthRein.json": "1.0.2",
    "Mods/Ebongrove.json": "1.0.2", "Mods/BlackFurRefuge.json": "1.0.1",
    "Mods/SicariusRefuge.json": "1.0.2", "Mods/TelJerdein.json": "1.1.0", "Mods/Antennaria.json": "1.0.1",
    "Mods/WinterCove.json": "1.1.0", "Mods/LostHall.json": "1.0.2", "Mods/SecretCellar.json": "1.0.1",
}
DEFAULT_VERSION = "1.0.3"
# Templates no longer shipped: merged into their home's one template (whole_home). Deleted when found.
RETIRED = ["Vanilla/LakeviewManorCellar.json", "Vanilla/WindstadManorCellar.json", "Vanilla/HeljarchenHallCellar.json",
           "Vanilla/LakeviewManorFull.json", "Vanilla/WindstadManorFull.json", "Vanilla/HeljarchenHallFull.json",
           "CreationClub/TundraHomesteadCellar.json", "CreationClub/TundraHomesteadFull.json",
           "CreationClub/MyrwatchGallery.json", "CreationClub/MyrwatchFull.json"]

ALC = ["alchemy.ingredients", "alchemy.potions", "alchemy.poisons"]
ARM = ["armor.heavy", "armor.light", "armor.shields"]


def cell_containers(cell_ref, lo=None):
    lo = lo or LO
    key = E.parse_ref(cell_ref)
    refs = lo.cell_refs(key)
    out = {}
    for rec, _g in refs.values():
        d = E.decode_ref(lo, rec, ["CONT"], refs)
        if d and d["baseType"] == "CONT" and not d["deleted"]:
            out[d["ref"].lower()] = {**d, "cellRef": cell_ref}
    return out


def cell_displays(cell_ref, include_plugins, lo):
    """The displays a template lists (see the module docstring), with their position and game name."""
    key = E.parse_ref(cell_ref)
    refs = lo.cell_refs(key, ("REFR", "ACHR"))
    placed = {}
    for rec, _g in refs.values():
        d = E.decode_ref(lo, rec, ["CONT", "ACTI", "NPC_"], refs)
        if d and not d["deleted"]:
            placed[d["ref"].lower()] = d
    out = []
    for x in E.find_displays(lo, key):
        d = placed.get(x["ref"].lower())
        if d is None or x["ref"].split("|")[0].lower() not in include_plugins:
            continue
        if x["kind"] not in ("bookshelf", "rack", "mannequin"):
            continue  # item plaques and cases: the player turns on the ones the game fills
        if x["kind"] == "rack" and not x.get("baseName"):
            continue  # unnamed parts of a rack (the player never sees them)
        if (d.get("enableParent") or {}).get("oppositeOfParent"):
            continue  # goes away with a house upgrade
        out.append({**x, "position": d["position"], "cellRef": cell_ref})
    return sorted(out, key=lambda x: x["ref"])


PROPER = {"Severin", "Dwemer", "Dwarven", "Nordic", "Imperial", "Falmer", "Orcish", "Daedric", "Ayleid", "Welkynd"}


def sentence(name):
    """The game's name in sentence case: "Large Sack" -> "Large sack", "Book Shelf" -> "Bookshelf"."""
    name = re.sub(r"\s*\([^()]*\)$", "", name or "") or "Container"  # "Trash Bin (Respawns)": the label adds its own ()
    words = name.replace("Book Shelf", "Bookshelf").split()
    return " ".join([words[0]] + [w if w in PROPER else w.lower() for w in words[1:]])


LABEL_RX = re.compile(r"^(?P<kind>.+?)(?: \((?P<inner>[^()]*)\))?$")


def check_label(label, areas=()):
    """One form for every label (module docstring): no "by the" outside the parentheses, the number after the
    landmark, ordinals only without one, what it holds never in the parentheses (esm_dump.label_problem; `areas`: the
    area names a label may give instead of a landmark)."""
    m = LABEL_RX.match(label)
    inner = m.group("inner") or ""
    assert " by the " not in m.group("kind") and " next to the " not in m.group("kind"), f"locator outside (): {label}"
    parts = [p.strip() for p in inner.split(",")] if inner else []
    for p in parts[1:]:
        assert not p.startswith("by the "), f"landmark not first: {label}"
    if any(p.startswith("by the ") for p in parts):
        assert not any(p in ("second", "third", "fourth") for p in parts), f"ordinal next to a landmark: {label}"
        assert not names_kind(parts[0], m.group("kind")), f"named after itself: {label}"
    problem = E.label_problem(label, areas)
    assert not problem, f"{label}: {problem}"


def names_kind(landmark, kind):
    """True when a landmark is only the label's own kind again: "by the bookshelves" for a bookshelf."""
    lm = landmark.lower().removeprefix("by the ").strip()
    k = kind.lower()
    return lm in {k, k + "s", k + "es", k[:-1] + "ves" if k.endswith("f") else k}


# A bound container named only by what it is ("Chest (by the forge)") is a landmark by its own landmark; one with a
# name of its own ("Loft cupboard (by the weapon plaque)") by that name.
GENERIC = {"chest", "barrel", "cupboard", "satchel", "strong box", "strongbox", "safe", "sack", "large sack", "urn",
           "dresser", "wardrobe", "drawer", "end table", "bookcase", "chest of drawers"}
# Bound containers that only exist after a later build (a city home's child's bedroom): never a landmark.
NO_ANCHOR = {"hearthfires.esm|0x00004265", "hearthfires.esm|0x00004200"}


def place(label):
    """Where a bound container stands, from its label: "Kitchen sack (large)" -> "by the kitchen sack",
    "Chest (by the arcane enchanter, 2)" -> "by the arcane enchanter"."""
    m = LABEL_RX.match(label)
    inner = (m.group("inner") or "").split(",")[0].strip()
    base = m.group("kind")
    if inner.startswith("by the ") and base.lower() in GENERIC:
        return inner
    first, _, rest = base.partition(" ")
    return "by the " + (first if first in PROPER else first.lower()) + (" " + rest if rest else "")


def nearest(anchors, pos):
    return min(anchors, key=lambda a: sum((x - y) ** 2 for x, y in zip(a[0], pos)))[1]


# A landmark names at most this many of one kind ("Bookshelf (by the library chest, 3)"); the next goes by another
# landmark when one is not much farther (SLACK game units, about 4 m), else it is numbered on.
PER_LANDMARK, SLACK = 3, 300.0


def pick_landmark(kind, anchors, pos, seen, cell=None, areas=None):
    """The landmark for one label of `kind`: never the kind itself ("Mannequin (by the mannequins)"), the nearest that
    names fewer than PER_LANDMARK of the kind yet, if it is within SLACK of the nearest. Only landmarks in the same
    cell (`cell`, for a home over several cells); with none there, the area's name ("Bookshelf (Mage hall)")."""
    if cell is not None:
        anchors = [a for a in anchors if a[2] == cell]
        if not anchors:
            return (areas or {}).get(cell) or "another area"
    own = [a for a in anchors if not names_kind(a[1], kind)] or anchors
    ranked = sorted(own, key=lambda a: sum((x - y) ** 2 for x, y in zip(a[0], pos)))
    near = sum((x - y) ** 2 for x, y in zip(ranked[0][0], pos)) ** 0.5
    for a in ranked:
        if sum((x - y) ** 2 for x, y in zip(a[0], pos)) ** 0.5 > near + SLACK:
            break
        if seen.get(f"{kind} ({a[1]}", 0) < PER_LANDMARK:
            return a[1]
    return ranked[0][1]


def label_ignored(entries, conts, master_ref, areas=None, kinds=None, extra=None):
    """Labels the ignored entries after the nearest bound container (or the master) in their cell, numbering repeats.
    `kinds`: ref -> the kind to call one by, where the game's name would invite using it ("Ingredients" on a sack that
    respawns)."""
    anchors = [(conts[e["ref"].lower()]["position"], place(e["label"]), conts[e["ref"].lower()]["cellRef"]) for e in entries
               if not e.get("ignored") and e["ref"].lower() not in NO_ANCHOR]
    master = conts[master_ref.lower()]
    anchors.append((master["position"], "by the master " + (master.get("baseName") or "chest").lower(), master["cellRef"]))
    anchors += extra or []
    seen = {}
    for e in entries:  # a bound label is taken: an ignored one by the same landmark is numbered after it
        m = re.match(r"^(.* \(by the [^,()]*)(?:, (\d+))?\)$", e["label"]) if not e.get("ignored") else None
        if m:
            seen[m.group(1)] = max(seen.get(m.group(1), 0), int(m.group(2) or 1))
    for e in [e for e in entries if e.get("ignored")]:
        pos, cell = conts[e["ref"].lower()]["position"], conts[e["ref"].lower()]["cellRef"]
        kind = (kinds or {}).get(e["ref"].lower()) or sentence(e["label"])
        text = f"{kind} ({pick_landmark(kind, anchors, pos, seen, cell if areas else None, areas)}"
        seen[text] = seen.get(text, 0) + 1
        e["label"] = text + (f", {seen[text]})" if seen[text] > 1 else ")")
        check_label(e["label"], set((areas or {}).values()) | {"another area"})
    return anchors


def display_entries(displays, entries, anchors, areas=None):
    """Display entries: a bookshelf keeps the label of its (ignored) container entry; the others are named like
    ignored containers ("Weapon rack (by the armory chest)", "Mannequin (by the bedroom wardrobe, 2)")."""
    by_ref = {e["ref"].lower(): e for e in entries}
    seen = {}
    labels = {}
    for x in displays:
        cont = by_ref.get(x["ref"].lower())
        if x["kind"] == "bookshelf" and cont is not None:
            labels[x["ref"]] = cont["label"]
        else:
            name = sentence(x.get("baseName") or x["kind"].capitalize())
            text = f"{name} ({pick_landmark(name, anchors, x['position'], seen, x['cellRef'] if areas else None, areas)}"
            seen[text] = seen.get(text, 0) + 1
            labels[x["ref"]] = text + (f", {seen[text]})" if seen[text] > 1 else ")")
            check_label(labels[x["ref"]], set((areas or {}).values()) | {"another area"})
    return [{"ref": x["ref"], "label": labels[x["ref"]]} for x in displays]


def auto_ignores(conts, bound, extra_ignores, include_plugins):
    res = []
    for k, d in sorted(conts.items(), key=lambda kv: kv[1]["ref"]):
        if k in bound:
            continue
        plugin = d["ref"].split("|")[0]
        if plugin.lower() not in include_plugins:
            continue  # e.g. Creation Club additions inside vanilla homes: not our template's business
        name = d.get("baseName") or d.get("baseEditorId")
        reason = None
        ep = d.get("enableParent") or {}
        divert = any(s.lower() == "byohdivertitemscript" for s in d.get("refScripts", []))
        if k in extra_ignores:
            reason = extra_ignores[k]
        elif d["respawns"]:
            reason = "Respawns: what you store here can disappear."
        elif d["baseEditorId"] == "PlayerBookShelfContainer":
            reason = "Scripted bookshelf: it only holds books and hands everything else back."
        elif ep.get("oppositeOfParent") and not divert:
            reason = "Goes away later (an upgrade or a quest): anything inside would be lost."
        if reason:
            res.append({"ref": d["ref"], "label": name, "ignored": True, "note": reason})
    return res


def base_note(master_name):
    return (f"Each container takes the item categories listed (see Categories/00_Default.json); ignored ones are never "
            f"used. Anything without a container of its own goes to the master, the {master_name}.")


def build(name, file, cell, requires, master, bindings, extra_ignores=None, auto=True, shared=False, lo=None, cells=None,
          priority=0, note=None, skip_displays=None, landmarks=None, master_name=None, kinds=None, write=True):
    """`shared`: a category may be bound to several containers, a fallback chain in list order (DESIGN 7.3): for
    containers that are never there together, such as the furniture of a Hearthfire wing's three choices, or the
    child's bedroom of a city home taking over from the furniture it replaces. `cells`: the home's other cells
    (DESIGN 6.7), [(cell, area label)]; bindings and displays may be in any of them. `skip_displays`: displays left
    out ({ref: why}). `landmarks`: [(ref, "by the ...")] of unbound containers or displays that name the displays
    near them, where no bound container is near. `master_name`: what the note calls the master when the game gives
    it no name. `kinds`: see label_ignored. `write`: False for a part of a whole home (whole_home writes the file).
    A `cells` entry may name the area's own master as a third item (`cells[].master`, DESIGN 6.7): a container of that
    cell, used there while the home's master is out of reach (not persistent); it is neither bound nor ignored."""
    lo = lo or LO
    cells = [(c[0], c[1], c[2] if len(c) > 2 else None) for c in (cells or [])]
    conts = cell_containers(cell, lo)
    for extra_cell, _label, _m in cells:
        conts.update(cell_containers(extra_cell, lo))
    areas = {extra_cell: label for extra_cell, label, _m in cells} | ({cell: name} if cells else {})
    extra_ignores = {k.lower(): v for k, v in (extra_ignores or {}).items()}
    bound = {}
    entries = []
    for ref, label, cats in bindings:
        k = ref.lower()
        assert k in conts, f"{name}: {ref} is not a container in {cell}"
        d = conts[k]
        assert not d["respawns"], f"{name}: {ref} respawns"
        assert k not in bound, f"{name}: duplicate {ref}"
        check_label(label)
        bound[k] = True
        if "misc.gems" in cats and "misc.curios" not in cats:
            cats = cats[:cats.index("misc.gems") + 1] + ["misc.curios"] + cats[cats.index("misc.gems") + 1:]
        entries.append({"ref": d["ref"], "label": label, "categories": cats})
    mk = master.lower()
    assert mk in conts and not conts[mk]["respawns"], f"{name}: bad master {master}"
    assert mk not in bound
    bound[mk] = True
    for extra_cell, _label, area_master in cells:
        if area_master:
            am = area_master.lower()
            assert am in conts and conts[am]["cellRef"] == extra_cell, f"{name}: area master {area_master} is not in {extra_cell}"
            assert not conts[am]["respawns"] and not conts[am]["startsDisabled"], f"{name}: bad area master {area_master}"
            assert am not in bound and am not in extra_ignores, f"{name}: area master {area_master} is bound or ignored"
            bound[am] = True
    include = {p.lower() for p in requires}
    if auto:
        entries += auto_ignores(conts, bound, extra_ignores, include)
    else:
        for k, reason in extra_ignores.items():
            d = conts[k]
            entries.append({"ref": d["ref"], "label": d.get("baseName") or d.get("baseEditorId"), "ignored": True, "note": reason})
    seen_cats = {}
    for e in entries:
        for c in e.get("categories", []):
            assert shared or c not in seen_cats, f"{name}: category {c} bound twice ({seen_cats[c]} / {e['ref']})"
            seen_cats[c] = e["ref"]
    skip = {k.lower() for k in (skip_displays or {})}
    found = [x for c in [cell] + [c for c, _l, _m in cells] for x in cell_displays(c, include, lo)]
    assert skip <= {x["ref"].lower() for x in found}, f"{name}: skip_displays names a ref that is not a display"
    found = [x for x in found if x["ref"].lower() not in skip]
    where = {k: (d["position"], d["cellRef"]) for k, d in conts.items()} | {x["ref"].lower(): (x["position"], x["cellRef"])
                                                                          for x in found}
    extra = [(where[r.lower()][0], text, where[r.lower()][1]) for r, text in (landmarks or [])]
    anchors = label_ignored(entries, conts, master, areas, {k.lower(): v for k, v in (kinds or {}).items()}, extra)
    displays = display_entries(found, entries, anchors, areas)
    master_name = (master_name or conts[mk].get("baseName") or "master container").lower()
    doc = {
        "$schema": "dovastash-layout-1",
        "name": name,
        "note": base_note(master_name) + (f" {note}" if note else ""),
        "author": "Dova Stash",
        "version": VERSIONS.get(file, DEFAULT_VERSION),
        "priority": priority,
        "requires": requires,
        **({"conflicts": CONFLICTS[file]} if file in CONFLICTS else {}),
        "cell": cell,
        **({"cells": [{"cell": c, "label": l, **({"master": conts[m.lower()]["ref"]} if m else {})} for c, l, m in cells]}
           if cells else {}),
        "master": conts[mk]["ref"],
        "containers": entries,
        **({"displays": displays} if displays else {}),
    }
    if write:
        path = OUT / file
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(jsonfmt.dumps(doc, width=200, align=True) + "\n", encoding="utf-8")
    nb = sum(1 for e in entries if not e.get("ignored"))
    ni = sum(1 for e in entries if e.get("ignored"))
    print(f"{file}: {nb} bound, {ni} ignored, {len(displays)} displays, master {conts[mk]['ref']} ({conts[mk]['baseName']}), "
          f"{len(conts)} containers in {1 + len(cells)} cell(s), {sum(1 for d in conts.values() if d['respawns'])} respawning")
    return doc


S = "Skyrim.esm|0x"
H = "HearthFires.esm|0x"
D = "Dragonborn.esm|0x"
VAN = ["Skyrim.esm", "Update.esm"]
VAN_HF = ["Skyrim.esm", "Update.esm", "HearthFires.esm"]

# ------------------------------------------------------------------ Breezehome
build("Breezehome", "Vanilla/Breezehome.json", S + "000165A8", VAN_HF, S + "000F3922", [
    (S + "0010EB84", "Alchemy corner chest (by the alchemy lab)", ALC),
    # The child's bedroom replaces the alchemy laboratory (and its chest): its chest takes the alchemy items then.
    (H + "00004265", "Child's bedroom chest", ALC),  # (NO_ANCHOR)
    (S + "0003EE69", "Dining room cupboard", ["alchemy.food"]),
    (S + "000CB437", "Loft cupboard (by the weapon plaque)", ["weapons.*"]),
    (S + "000CB433", "Housecarl's room dresser", ARM),
    (S + "000CB435", "Housecarl's room end table", ["ammo.*"]),
    (S + "000CB432", "Bedroom dresser", ["armor.clothing"]),
    (S + "000CB43A", "Bedroom night table", ["armor.jewelry", "misc.gems"]),
    (S + "000CB42D", "Bedroom end table (left)", ["soulgems.*", "scrolls"]),
    (S + "000CB42E", "Bedroom end table (right)", ["books.*"]),
    (S + "000CB442", "Living room cupboard", ["misc.ores_ingots", "misc.hides_pelts", "misc.animal_parts", "misc.dwemer"]),
    (S + "000CB45D", "Large kitchen sack", ["misc.building_materials", "misc.tools"]),
    (S + "000CB45E", "Large kitchen sack (second)", ["misc.clutter"]),
    (S + "000CB46C", "Small living room sack", ["keys"]),
], shared=True)

# ------------------------------------------------------------------ Honeyside
build("Honeyside", "Vanilla/Honeyside.json", S + "00016BDD", VAN_HF, S + "000D721E", [
    (S + "000D7227", "Kitchen cupboard", ["alchemy.food"]),
    (S + "000D71FA", "Lower floor wardrobe (by the alchemy lab)", ALC),
    (S + "000D9251", "Guest bedroom dresser", ["weapons.*"] + ARM),
    (S + "000D71FD", "Guest bedroom end table", ["ammo.*"]),
    (S + "000D9278", "Bedroom wardrobe", ["armor.clothing"]),
    (S + "000D9243", "Bedroom night table (by the bed)", ["armor.jewelry", "misc.gems"]),
    (S + "000D9245", "Bedroom end table", ["books.*", "scrolls"]),
    (S + "000D9246", "Bedroom night table (far side)", ["soulgems.*", "keys"]),
], note="No crafting storage here: ores, hides and other materials go to the master.")

# ------------------------------------------------------------------ Vlindrel Hall
build("Vlindrel Hall", "Vanilla/VlindrelHall.json", S + "00016DFA", VAN_HF, S + "00077CAB", [
    (S + "000E15AD", "Alchemy corner large sack", ALC),
    # Building the child's room takes the alchemy corner's sack away: the room's wardrobe takes the alchemy items then.
    (H + "00004200", "Child's bedroom wardrobe", ALC),
    (S + "000E157F", "Living room cupboard", ["alchemy.food"]),
    (S + "000DC683", "Living room dresser (by the dagger rack)", ["misc.dwemer"]),
    (S + "000E15C4", "Living room end table", ["keys"]),
    (S + "000E2E5A", "Bedroom wardrobe", ["armor.clothing"]),
    (S + "000E2E5D", "Bedroom chest of drawers", ["armor.jewelry", "misc.gems"]),
    (S + "000E2E5B", "Bedroom cupboard (by the dagger rack)", ["weapons.*"]),
    (S + "000E2E62", "Bedroom cupboard (by the dagger rack, 2)", ARM),
    (S + "000E2E5C", "Bedroom cupboard (back wall)", ["ammo.*"]),
    (S + "000E2E65", "Bedroom cupboard (side wall)", ["soulgems.*", "scrolls"]),
    (S + "000DC602", "Bedroom dresser", ["books.*"]),
    (S + "00077CC6", "Guest room cupboard", ["misc.ores_ingots", "misc.hides_pelts"]),
    (S + "000E2ECE", "Guest room dresser", ["misc.animal_parts", "misc.building_materials"]),
    (S + "000DC678", "Guest room end table", ["misc.tools"]),
    (S + "000DC6B0", "Upper hall sack", ["misc.clutter"]),
], extra_ignores={
    S + f"000C645{i}": "Your spouse's shop stock: it respawns." for i in range(5)
}, shared=True)

# ------------------------------------------------------------------ Hjerim
build("Hjerim", "Vanilla/Hjerim.json", S + "00016778", VAN_HF, S + "000DF5AC", [
    (S + "000DF5AA", "Bedroom wardrobe", ["armor.clothing"]),
    (S + "000DF5B5", "Bedroom night table (left)", ["armor.jewelry", "misc.gems"]),
    (S + "000DF5B6", "Bedroom night table (right)", ["soulgems.*", "scrolls"]),
    (S + "000DF49B", "Upstairs chest of drawers", ["books.*"]),
    (S + "000DF61E", "Guest room dresser", ["weapons.*"]),
    (S + "000DF61B", "Guest room wardrobe", ARM),
    (S + "000DF6E8", "Housecarl's end table (by the mannequins)", ["ammo.*"]),
    (S + "000DF49D", "Living room chest of drawers (back)", ["misc.ores_ingots", "misc.hides_pelts", "misc.animal_parts"]),
    (S + "000DF49E", "Living room chest of drawers (front)", ["misc.dwemer", "misc.building_materials", "misc.tools"]),
    (S + "000DF4C1", "Living room large sack", ALC),
    (S + "000DF4C3", "Living room large sack (second)", ["misc.clutter"]),
    (S + "000DF66C", "Kitchen large sack", ["alchemy.food"]),
    (S + "000DF670", "Kitchen small sack", ["keys"]),
], extra_ignores={
    S + "000E0E51": "Blood on the Ice quest chest.",
})

# ------------------------------------------------------------------ Proudspire Manor
build("Proudspire Manor", "Vanilla/ProudspireManor.json", S + "00016A06", VAN_HF, S + "0009DA07", [
    (S + "000D9B49", "Bedroom safe", ["armor.jewelry", "misc.gems"]),
    (S + "000D89BF", "Bedroom chest of drawers", ["armor.clothing"]),
    (S + "000D8A55", "Upper hall chest of drawers (by the weapon rack)", ARM),
    (S + "000E2627", "Bedroom night table", ["soulgems.*", "scrolls"]),
    (S + "000D89D5", "Upper living room cupboard", ["books.*"]),
    (S + "000D8A1C", "Living room cupboard (ground floor)", ALC),
    (S + "000D8AAA", "Kitchen cupboard", ["alchemy.food"]),
    (S + "000D8ABA", "Guest bedroom dresser (lower floor)", ["weapons.*"]),
    (S + "000D8ABB", "Guest bedroom end table (lower floor)", ["ammo.*"]),
    (S + "000D8AC4", "Guest bedroom large sack", ["misc.ores_ingots", "misc.hides_pelts", "misc.animal_parts"]),
    (S + "000D8AC8", "Guest bedroom large sack (second)", ["misc.building_materials", "misc.tools", "misc.dwemer"]),
    (S + "000D8A0B", "Ground floor large sack", ["misc.clutter", "keys"]),
])

# ------------------------------------------------------------------ Severin Manor
build("Severin Manor", "Vanilla/SeverinManor.json", D + "00017EBE", ["Skyrim.esm", "Update.esm", "Dragonborn.esm"],
      D + "00036EF2", [
    (D + "0001F31D", "Severin family safe", ["armor.jewelry", "misc.gems"]),
    (D + "00036EF3", "Master bedroom chest (by the bookshelves)", ["books.*"]),
    (D + "00036EFF", "Master bedroom chest (by the second bookshelf)", ["soulgems.*", "scrolls"]),
    (D + "00036EF4", "Master bedroom chest (corner)", ["armor.clothing"]),
    (D + "00036E03", "Armory large sack (by the mannequins)", ["weapons.*"]),
    (D + "00036EF1", "Armory large sack (by the weapon racks)", ARM),
    (D + "00036E04", "Armory large sack", ["ammo.*"]),
    (D + "000359BF", "Alchemy lab ingredients sack", ["alchemy.ingredients"]),
    (D + "000359C0", "Alchemy lab potions sack", ["alchemy.potions"]),
    (D + "000359C1", "Alchemy lab poisons sack", ["alchemy.poisons"]),
    (D + "00036EC6", "Kitchen barrel (upper floor)", ["alchemy.food"]),
    (D + "00036E95", "Smithy barrel", ["misc.ores_ingots", "misc.dwemer"]),
    (D + "00036F62", "Lower hall large sack", ["misc.hides_pelts", "misc.animal_parts"]),
    (D + "00036F63", "Lower hall large sack (second)", ["misc.building_materials", "misc.tools"]),
    (D + "00036EB9", "Guest room chest", ["keys", "misc.clutter"]),
])

# ------------------------------------------------------------------ Hearthfire homes
HF_MAIN = {
    # (room, part) -> (label, categories)
    ("02", "006"): ("Main hall chest (west)", ["misc.building_materials"]),
    ("02", "007"): ("Main hall chest (east)", ["misc.ores_ingots", "misc.tools"]),
    ("02", "016"): ("Main hall safe (back room)", ["misc.gems", "armor.jewelry"]),
    ("02", "018"): ("Main hall display wardrobe (back room)", ["misc.dwemer"]),
    ("02", "019"): ("Main hall display wardrobe (back room, 2)", ["misc.hides_pelts", "misc.animal_parts"]),
    ("02", "020"): ("Main hall cupboard (back room)", ["alchemy.food"]),
    ("02", "025"): ("Main hall upstairs low wardrobe", ["keys"]),
    ("02", "032"): ("Main hall upstairs wardrobe", ["armor.clothing"]),
    ("02", "040"): ("Main hall upstairs cupboard", ["misc.clutter"]),
    ("05", "006"): ("Enchanter's tower chest", ["soulgems.*"]),
    ("05", "020"): ("Enchanter's tower cupboard", ["scrolls"]),
    ("05", "123"): ("Enchanter's tower chest (by the cupboard)", ["books.spelltomes"]),
    ("05", "008"): ("Enchanter's tower display table", ["weapons.staves"]),
    ("08", "006"): ("Alchemy laboratory chest", ["alchemy.ingredients"]),
    ("08", "020"): ("Alchemy laboratory cupboard", ["alchemy.potions"]),
    ("08", "051"): ("Alchemy laboratory display wardrobe", ["alchemy.poisons"]),
    ("09", "006"): ("Armory chest", ["weapons.swords", "weapons.waraxes", "weapons.maces", "weapons.daggers"]),
    ("09", "123"): ("Armory chest (by the cupboard)", ["weapons.greatswords", "weapons.axes", "weapons.warhammers"]),
    ("09", "020"): ("Armory cupboard", ["weapons.bows", "weapons.crossbows"]),
    ("09", "141"): ("Armory display cupboard", ["ammo.*"]),
    ("09", "042"): ("Armory dresser", ["armor.heavy"]),
    ("09", "153"): ("Armory dresser (second)", ["armor.light"]),
    ("09", "008"): ("Armory display table", ["armor.shields"]),
    ("11", "006"): ("Library chest", ["books.skill", "books.notes", "books.other"]),
}
# The other choices of each wing (Hearthfire builds one of three per wing; the furniture of the two not built stays
# disabled, and disabled containers are skipped: DESIGN 7.3, "Same category on several containers"). A category bound
# in several places is a fallback chain in list order, so each entry says where it goes in the list:
#   FIRST: before the main hall and the bound wing rooms: this room takes the category over when built;
#   LAST:  after them: it holds what the wing choice it replaces would have held (not built: the master's otherwise).
# West wing: Bedrooms (03), Greenhouse (04) | Enchanter's Tower (05). North: Storage Room (06), Trophy Room (07) |
# Alchemy Laboratory (08). East: Kitchen (10) | Armory (09), Library (11).
FIRST, LAST = 0, 2
HF_WINGS = {
    ("03", "032"): ("Bedrooms tall wardrobe", ["armor.clothing"], FIRST),
    ("03", "016"): ("Bedrooms safe", ["armor.jewelry", "misc.gems"], FIRST),
    ("03", "006"): ("Bedrooms chest", ["soulgems.*"], LAST),
    ("03", "046"): ("Bedrooms long chest", ["scrolls"], LAST),
    ("03", "090"): ("Bedrooms chest (by the first bed)", ["books.spelltomes"], LAST),
    ("03", "091"): ("Bedrooms chest (by the second bed)", ["weapons.staves"], LAST),
    ("03", "018"): ("Bedrooms small wardrobe", ["armor.light"], LAST),
    ("03", "019"): ("Bedrooms display wardrobe", ["armor.heavy"], LAST),
    ("03", "025"): ("Bedrooms small wardrobe (second)", ["armor.shields"], LAST),
    ("04", "020"): ("Greenhouse cupboard", ["alchemy.ingredients"], LAST),
    ("04", "040"): ("Greenhouse cupboard (second)", ["soulgems.*", "scrolls", "books.spelltomes", "weapons.staves"], LAST),
    ("06", "006"): ("Storage room chest", ["misc.ores_ingots"], FIRST),
    ("06", "007"): ("Storage room chest (second)", ["misc.hides_pelts"], FIRST),
    ("06", "128"): ("Storage room chest (by the display table)", ["misc.animal_parts"], FIRST),
    ("06", "131"): ("Storage room chest (by the table)", ["misc.building_materials"], FIRST),
    ("06", "132"): ("Storage room chest (by the table, 2)", ["misc.tools"], FIRST),
    ("06", "133"): ("Storage room chest (by the display shelf)", ["misc.dwemer"], FIRST),
    ("06", "020"): ("Storage room cupboard", ["alchemy.ingredients"], LAST),
    ("06", "040"): ("Storage room cupboard (second)", ["alchemy.potions"], LAST),
    ("06", "019"): ("Storage room display dresser", ["alchemy.poisons"], LAST),
    ("07", "006"): ("Trophy room chest", ["misc.animal_parts", "misc.hides_pelts"], FIRST),
    ("07", "020"): ("Trophy room cupboard", ["alchemy.ingredients"], LAST),
    ("07", "040"): ("Trophy room cupboard (second)", ["alchemy.potions"], LAST),
    ("07", "041"): ("Trophy room cupboard (third)", ["alchemy.poisons"], LAST),
    ("10", "020"): ("Kitchen cupboard", ["alchemy.food"], FIRST),
    ("10", "040"): ("Kitchen cupboard (second)", ["alchemy.ingredients"], LAST),
}
HF_CELLAR = {
    ("12", "007"): ("Cellar chest (by the tanning rack)", ["misc.hides_pelts", "misc.animal_parts"]),
    ("12", "074"): ("Cellar chest (by the forge)", ["weapons.*"]),
    ("12", "044"): ("Cellar end table (by the forge)", ["misc.ores_ingots"]),
    ("12", "008"): ("Cellar display table (by the workbench)", ARM),
    ("12", "137"): ("Cellar end table (by the carpenter's workbench)", ["misc.building_materials", "misc.tools"]),
    ("12", "181"): ("Cellar safe bank (by the smelter)", ["misc.gems", "armor.jewelry"]),
    ("12", "182"): ("Cellar safe bank (by the tanning rack)", ["misc.dwemer"]),
}
RX = re.compile(r"^BYOHHouse\dInteriorRoom(\d\d)Part(\d{3})(?!.*_CHILD)")


def hearthfire(name, file, cell, rules, master_rx, wings=None, write=True):
    conts = cell_containers(cell)
    rules = {k: (label, cats, 1) for k, (label, cats) in rules.items()} | (wings or {})
    bindings, master = [], None
    for d in sorted(conts.values(), key=lambda d: d["refEditorId"] or ""):
        e = d["refEditorId"] or ""
        if re.match(master_rx, e):
            assert master is None
            master = d["ref"]
            continue
        if d.get("enableParent"):
            continue  # decorative clutter enabled together with a shelf/table, not a crafted container
        m = RX.match(e)
        if not m:
            continue
        rule = rules.get((m.group(1), m.group(2)))
        if rule:
            bindings.append((rule[2], e, (d["ref"], rule[0], rule[1])))
    assert master, name
    assert len(bindings) == len(rules), (name, len(bindings), len(rules))
    bindings = [b[2] for b in sorted(bindings, key=lambda b: (b[0], b[1]))]  # FIRST, the house's own, LAST
    return build(name, file, cell, VAN_HF, master, bindings, shared=bool(wings), write=write)


def area_labels(part, area):
    """`part`'s labels with the area's word in front of each bound container's ("Armory cupboard" -> "Gallery armory
    cupboard"), and the landmarks named after them changed to match; a container named only by its kind keeps its own
    landmark ("Large sack (by the tanning rack)" -> "Gallery large sack (by the tanning rack)")."""
    swap = {}
    containers = []
    for e in part["containers"]:
        if e.get("ignored"):
            containers.append(e)
            continue
        m = LABEL_RX.match(e["label"])
        kind = m.group("kind")
        new = f"{area} {kind[0].lower()}{kind[1:]}" + (f" ({m.group('inner')})" if m.group("inner") else "")
        if kind.lower() not in GENERIC:
            swap[place(e["label"])] = place(new)
        check_label(new)
        containers.append({**e, "label": new})
    def landmarks(x):
        label = x["label"]
        for old, new in swap.items():
            label = re.sub(r"\(" + re.escape(old) + r"(?=[,)])", "(" + new, label)
        return {**x, "label": label}
    return {**part, "containers": [landmarks(e) for e in containers], "displays": [landmarks(x) for x in part.get("displays", [])]}


ORDINALS = ["", "", "second", "third", "fourth", "fifth"]


def label_ordinal(label):
    """(kind, landmark or None, n): "Chest" (1), "Chest (second)" (2), "Chest (by the table)" (1), "Chest (by the
    table, 2)" (2)."""
    if m := re.fullmatch(r"(.*) \((.*), (\d+)\)", label):
        return m.group(1), m.group(2), int(m.group(3))
    if m := re.fullmatch(r"(.*) \((.*)\)", label):
        if m.group(2) in ORDINALS[2:]:
            return m.group(1), None, ORDINALS.index(m.group(2))
        return m.group(1), m.group(2), 1
    return label, None, 1


def format_ordinal(kind, inner, n):
    if inner:
        return f"{kind} ({inner})" if n == 1 else f"{kind} ({inner}, {n})"
    return kind if n == 1 else f"{kind} ({ORDINALS[n]})"


def renumber(items, dropped):
    """Shifts the repeats of each dropped label down ('(second)' becomes the first, ', 3' becomes ', 2'), so no label
    numbers a container whose first is not listed (tools/README.md: ', 2' and '(second)' mark repeats)."""
    for gone in dropped:
        kind, inner, n = label_ordinal(gone)
        taken = {x["label"] for x in items}
        for x in sorted(items, key=lambda x: label_ordinal(x["label"])[2]):
            k, i, m = label_ordinal(x["label"])
            if (k, i) == (kind, inner) and m > n:
                new = format_ordinal(k, i, m - 1)
                if new not in taken:
                    taken.discard(x["label"])
                    x["label"] = new
                    taken.add(new)
    return items


def whole_home(name, file, main, other, cells, replaces, first=(), moved=(), lo=None, note=None, relabel_main=(), area_note=None):
    """A home over several cells as its one template (DESIGN 6.7), from its single-cell parts as built (not written):
    `first` (bindings in the other cells listed before the main's: [(ref, label, categories)]), the main part's containers
    without the categories in `moved` (another cell's container takes them; a container left with none is dropped), then
    `other`'s containers and both parts' displays; `cells` [(cell, area label)]. The main part's repeats of a dropped
    container's label move down (renumber). The other part's master is its area's
    master (`cells[].master`: used there while the home's master is out of reach), so a setup made from the retired
    template (`replaces`, paths under Templates/) is taken in untouched while the other part's labels stay as they were
    (Myrwatch's gallery calls its containers so, `area_labels`: its retired template never shipped). A label that
    would come twice is changed in the main part (`relabel_main`: (old, new) text). `note`: the main part's note without
    what only it needs; `area_note`: a sentence on the other area's master."""
    lo = lo or LO
    conts = {}
    for c, _label in cells:
        conts.update(cell_containers(c, lo))
    entries = []
    for ref, label, cats in first:
        d = conts.get(ref.lower())
        assert d and not d["respawns"], f"{name}: {ref} is not a usable container in {cells}"
        check_label(label)
        entries.append({"ref": d["ref"], "label": label, "categories": cats})
    def relabel(x):
        label = x["label"]
        for old, new in relabel_main:
            label = label.replace(old, new)
        return {**x, "label": label}
    kept, dropped = [], []
    for e in main["containers"]:
        e = relabel(e)
        if e.get("categories"):
            cats = [c for c in e["categories"] if c not in moved]
            if not cats:
                dropped.append(e["label"])
                continue
            e = {**e, "categories": cats}
        kept.append(e)
    main_displays = [relabel(x) for x in main.get("displays", [])]
    renamed = {id(e): e["label"] for e in kept}
    renumber(kept, dropped)
    renamed = {renamed[id(e)]: e["label"] for e in kept if renamed[id(e)] != e["label"]}
    for d in main_displays:
        # A display named after a renumbered container ("Weapon rack (by the storage room chest (second))").
        for old, new in renamed.items():
            d["label"] = d["label"].replace(old.lower(), new.lower()).replace(old, new)
    entries += kept
    entries += other["containers"]
    displays = main_displays + other.get("displays", [])
    labels = [e["label"] for e in entries if not e.get("ignored")]
    assert len(labels) == len(set(labels)), f"{name}: a label comes twice"
    names = ["the " + label.lower() for _c, label in cells]
    areas = "the house, " + ", ".join(names[:-1]) + " and " + names[-1] if len(names) > 1 else "the house and " + names[0]
    doc = {
        "$schema": "dovastash-layout-1",
        "name": name,
        "note": (note or main["note"]) + f" One setup for the whole home: {areas}. From another area, only containers that always "
                "stay loaded are used." + (f" {area_note}" if area_note else ""),
        "author": "Dova Stash",
        "version": VERSIONS[file],
        "priority": 0,
        "requires": main["requires"],
        "replaces": ["Templates/" + r for r in replaces],
        "cell": main["cell"],
        "cells": [{"cell": c, "label": label, **({"master": other["master"]} if c == other["cell"] else {})} for c, label in cells],
        "master": main["master"],
        "containers": entries,
        **({"displays": displays} if displays else {}),
    }
    path = OUT / file
    path.write_text(jsonfmt.dumps(doc, width=200, align=True) + "\n", encoding="utf-8")
    print(f"{file}: {len(entries)} containers, {len(displays)} displays over {1 + len(cells)} cells, replaces {', '.join(replaces)}")


# The Hearthfire houses with the cellar and the grounds (tests/ingame plans 21-23): ingots to the cellar's forge, building
# materials to the grounds chest (the carpenter's workbench is outside), the rest as in the house's own template.
HF_FULL = {1: (S + "00009BF7", [], H + "00003068"), 2: (S + "000092B2", [S + "00009291"], H + "0000B9F5"),
           3: (S + "000090C4", [S + "000090A4"], H + "00010FCE")}
for house, cell, cellar, n in [("Lakeview Manor", H + "00002FEC", H + "00004849", 1),
                               ("Windstad Manor", H + "00011CB3", H + "00015154", 2),
                               ("Heljarchen Hall", H + "00016A56", H + "00016A55", 3)]:
    fname = house.replace(" ", "")
    main_doc = hearthfire(house, f"Vanilla/{fname}.json", cell, HF_MAIN, rf"^BYOHHouse{n}Room01Chest", HF_WINGS, write=False)
    cellar_doc = hearthfire(f"{house} Cellar", f"Vanilla/{fname}Cellar.json", cellar, HF_CELLAR,
                            rf"^BYOHHouse{n}InteriorRoom12Part006", write=False)
    grounds, yards, grounds_chest = HF_FULL[n]
    whole_home(house, f"Vanilla/{fname}.json", main_doc, cellar_doc,
               [(cellar, "Cellar"), (grounds, "Grounds")] + [(y, "Smelter yard") for y in yards], [f"Vanilla/{fname}Cellar.json"],
               first=[(grounds_chest, "Grounds chest", ["misc.building_materials"])],
               moved={"misc.building_materials", "misc.ores_ingots"},
               area_note="In the cellar the master is out of reach: what has no container there goes to the cellar's chest. "
                         "Where containers share a category, the first one built, in list order, takes it.")

# ------------------------------------------------------------------ Creation Club
U = "Update.esm|0x"

T = "ccEEJSSE001-Hstead.esm|0x"
TH_DOC = build("Tundra Homestead", "CreationClub/TundraHomestead.json", T + "0000087F",
      ["Skyrim.esm", "Update.esm", "ccEEJSSE001-Hstead.esm"], T + "00084A88", [
    (T + "00000A65", "Alchemy corner ingredients cupboard", ["alchemy.ingredients"]),
    (T + "0000098D", "Apothecary's satchel (by the alchemy lab)", ["alchemy.potions", "alchemy.poisons"]),
    (T + "00000908", "Alchemy corner cupboard", ["misc.animal_parts"]),
    (T + "00000A3C", "Shrine cupboard (by the enchanter)", ["soulgems.*"]),
    (T + "00000AC0", "Chest (by the arcane enchanter)", ["books.spelltomes", "scrolls"]),
    (T + "00000AAC", "Enchanter cupboard", ["keys"]),
    (T + "00000B04", "Safe", ["armor.jewelry", "misc.gems"]),
    (T + "00000B10", "Sitting room cupboard", ["books.skill", "books.notes", "books.other"]),
    (T + "00000FEF", "Kitchen low cupboard", ["alchemy.food"]),
    (T + "00000FAF", "Bedroom dresser", ["armor.clothing"]),
    (T + "00000FBA", "Bedroom cupboard", ARM + ["weapons.*", "ammo.*"]),
    (T + "0000098F", "Sitting room small chest", ["misc.ores_ingots", "misc.hides_pelts"]),
    (T + "00000A1D", "Sitting room strong box", ["misc.dwemer", "misc.tools", "misc.building_materials"]),
], write=False)
TH_CELLAR_DOC = build("Tundra Homestead Cellar", "CreationClub/TundraHomesteadCellar.json", T + "00000B06",
      ["Skyrim.esm", "Update.esm", "ccEEJSSE001-Hstead.esm"], T + "00000F85", [
    (T + "00000F87", "Cellar safe (by the mannequins)", ["weapons.*", "ammo.*"]),
    (T + "00000D1B", "Cellar bookcase (by the mannequin)", ["books.*"]),
    (T + "00000D5C", "Cellar bookcase (by the mannequin, 2)", ARM),
    (T + "00000DBB", "Cellar bookcase (by the mannequin, 3)", ["misc.gems", "armor.jewelry"]),
], write=False)

# With its cellar (tests/ingame plan 38): the cellar's weapons and armor stay there, the rest as in the hall's template.
whole_home("Tundra Homestead", "CreationClub/TundraHomestead.json", TH_DOC, TH_CELLAR_DOC,
           [(T + "00000B06", "Cellar")], ["CreationClub/TundraHomesteadCellar.json"],
           area_note="In the cellar the master is out of reach: what has no container there goes to the cellar's chest.")

M = "ccEEJSSE002-Tower.esl|0x"
MY = ["Skyrim.esm", "Update.esm", "ccEEJSSE002-Tower.esl"]
MYRWATCH_DOC = build("Myrwatch", "CreationClub/Myrwatch.json", M + "0000083B", MY, M + "00000B3A", [
    (M + "00000B15", "Armory chest (by the mannequin)", ["weapons.*"]),
    (M + "00000B2D", "Armory cupboard", ARM),
    (M + "00000B45", "Armory cupboard (by the weapon racks)", ["ammo.*"]),
    (M + "00000B4B", "Armory safe", ["armor.jewelry", "misc.gems"]),
    (M + "000009E8", "Shrine cupboard", ["scrolls"]),
    (M + "00000A60", "Shrine cupboard (second)", ["soulgems.*"]),
    (M + "000008A7", "Bedroom wardrobe", ["armor.clothing"]),
    (M + "00000BC6", "Library wardrobe", ["books.*"]),
    (M + "00000A37", "Kitchen large sack", ["alchemy.food"]),
], write=False)
GALLERY_DOC = build("Myrwatch Gallery", "CreationClub/MyrwatchGallery.json", M + "0000092C", MY, M + "00000E5D", [
    (M + "00000EF8", "Alchemy cupboard", ["alchemy.ingredients"]),
    (M + "00000F8C", "Alchemy cupboard (second)", ["alchemy.potions", "alchemy.poisons"]),
    (M + "00000EF7", "Soul gem barrel", ["soulgems.*"]),
    (M + "00000F0A", "Enchanting cupboard", ["scrolls"]),
    (M + "00000F7E", "Enchanting cupboard (second)", ["books.spelltomes"]),
    (M + "00000F43", "Staff enchanter cupboard", ["weapons.staves"]),
    (M + "00000E56", "Shield display cupboard", ["armor.shields"]),
    (M + "00000E99", "Armory cupboard", ["armor.heavy", "armor.light"]),
    # weapons.* next to the staff cupboard's weapons.staves: an exact id beats a wildcard, so staves go there.
    (M + "00000F73", "Armory cupboard (by the mannequin)", ["weapons.*"]),
    (M + "00000F01", "Large sack (by the tanning rack)", ["misc.hides_pelts", "misc.animal_parts"]),
    (M + "00000D31", "Curio cabinet", ["misc.gems", "armor.jewelry"]),
    (M + "00000D32", "Curio cabinet (second)", ["misc.dwemer"]),
], write=False)

# The tower with its gallery (tests/ingame plan 31): the tower's containers first, then the gallery's; where both bind a
# category, the one in the player's cell is used (the other is not loaded).
# The gallery's containers are called so ("Gallery armory cupboard"), as the Tundra Homestead cellar's and the Hearthfire
# cellars' are.
whole_home("Myrwatch", "CreationClub/Myrwatch.json", MYRWATCH_DOC, area_labels(GALLERY_DOC, "Gallery"),
           [(M + "0000092C", "Gallery")], ["CreationClub/MyrwatchGallery.json"], note=base_note("chest"),
           area_note="In the gallery the master is out of reach: what has no container there goes to the gallery's wardrobe.")

SF = "ccEEJSSE003-Hollow.esl|0x"
build("Shadowfoot Sanctum", "CreationClub/ShadowfootSanctum.json", U + "00002F98",
      ["Skyrim.esm", "Update.esm", "ccEEJSSE003-Hollow.esl"], SF + "00000EB7", [
    (SF + "00000995", "Smithing chest", ["misc.ores_ingots"]),
    (SF + "00000A41", "Barrel (by the tanning rack)", ["misc.hides_pelts", "misc.animal_parts"]),
    (SF + "00000B01", "Barrel (by the workbench)", ["misc.tools", "misc.building_materials", "misc.dwemer"]),
    (SF + "00000AA7", "Alchemy cupboard", ["alchemy.ingredients"]),
    (SF + "00000D1F", "Alchemy cupboard (second)", ["alchemy.potions", "alchemy.poisons"]),
    (SF + "00000E7A", "Enchanting cupboard", ["soulgems.*"]),
    (SF + "00000E7B", "Enchanting cupboard (second)", ["scrolls", "books.spelltomes"]),
    (SF + "00000E81", "Weapon rack cupboard", ["weapons.*"]),
    (SF + "00000E82", "Armor cupboard (by the weapon racks)", ARM),
    (SF + "00000A12", "Bedroom cupboard", ["ammo.*"]),
    (SF + "000009E0", "Kitchen cupboard", ["alchemy.food"]),
    (SF + "00000AF8", "Bedroom safe", ["armor.jewelry", "misc.gems"]),
    (SF + "00000B07", "Bedroom wardrobe", ["armor.clothing"]),
    (SF + "00000E20", "Bedroom chest", ["keys"]),
    (SF + "00000A5A", "Bookcase (by the shrine)", ["books.skill", "books.notes", "books.other"]),
], extra_ignores={SF + "000009C0": "Developer test container."})

HH = "ccEEJSSE004-Hall.esl|0x"
build("Hendraheim", "CreationClub/Hendraheim.json", U + "00002FD9",
      ["Skyrim.esm", "Update.esm", "ccEEJSSE004-Hall.esl"], HH + "00000A81", [
    (HH + "000008DE", "Safe", ["armor.jewelry", "misc.gems"]),
    (HH + "00000F82", "Bedroom wardrobe", ["armor.clothing"]),
    (HH + "00000FF2", "Wardrobe", ARM),
    (HH + "00000881", "Narrow dresser (by the bench)", ["weapons.*"]),
    (HH + "00000955", "Drawers (by the beds)", ["ammo.*"]),
    (HH + "000008A5", "Narrow dresser (by the oven)", ["alchemy.food"]),
    (HH + "00000A66", "Satchel (by the alchemy lab)", ALC),
    (HH + "00000FB6", "Narrow dresser (by the arcane enchanter)", ["soulgems.*", "scrolls"]),
    (HH + "0000086C", "Bookcase", ["books.*"]),
    (HH + "000009D0", "Strong box", ["misc.ores_ingots", "misc.hides_pelts", "misc.animal_parts"]),
    (HH + "00000A42", "Narrow dresser (by the bed)", ["keys"]),
])

BC = "ccEEJSSE005-Cave.esm|0x"
build("Bloodchill Manor", "CreationClub/BloodchillManor.json", U + "00002FD1",
      ["Skyrim.esm", "Update.esm", "ccEEJSSE005-Cave.esm"], BC + "00038832", [
    (BC + "00075884", "Ingot chest (smithy)", ["misc.ores_ingots"]),
    (BC + "00075887", "Ore barrel (smithy)", ["misc.dwemer"]),
    (BC + "000758B4", "Smithy chest (by the smelter)", ["misc.tools", "misc.building_materials"]),
    (BC + "0007589A", "Smithy strong box", ["misc.gems"]),
    (BC + "000758F3", "Armory chest (by the weapon racks)", ["weapons.*"]),
    (BC + "000758B9", "Armory cupboard", ["ammo.*"]),
    (BC + "000758BA", "Armory cupboard (second)", ["armor.shields"]),
    (BC + "00089E7E", "Mannequin room chest", ["armor.heavy", "armor.light"]),
    (BC + "00089F1B", "Enchanting cupboard", ["soulgems.*"]),
    (BC + "00089F28", "Enchanting cupboard (second)", ["scrolls"]),
    (BC + "00089F2E", "Enchanting end table", ["books.spelltomes"]),
    (BC + "00089F29", "Alchemy cupboard", ["alchemy.ingredients"]),
    (BC + "0008F03A", "Alchemy satchel", ["alchemy.potions", "alchemy.poisons"]),
    (BC + "00056FB4", "Kitchen cupboard (by the oven)", ["alchemy.food"]),
    (BC + "0005C1A8", "Library bookcase", ["books.skill", "books.notes", "books.other"]),
    (BC + "00042A70", "Bedroom wardrobe", ["armor.clothing"]),
    (BC + "00084D55", "Safe", ["armor.jewelry"]),
    (BC + "00038831", "Bedroom knapsack", ["keys"]),
    (BC + "00089EFD", "Chest (by the dagger rack)", ["misc.hides_pelts", "misc.animal_parts"]),
    (BC + "00047BC2", "Fish barrel (by the chopping block)", ["misc.clutter"]),
])

G = "ccRMSSSE001-NecroHouse.esl|0x"
build("Gallows Hall", "CreationClub/GallowsHall.json", U + "00002FD0",
      ["Skyrim.esm", "Update.esm", "ccRMSSSE001-NecroHouse.esl"], G + "00000968", [
    (G + "00000C0D", "Chest (by the arcane enchanter)", ["soulgems.*", "scrolls"]),
    (G + "00000A04", "Safe (by the arcane enchanter)", ["armor.jewelry", "misc.gems"]),
    (G + "00000A91", "Alchemy satchel", ALC),
    (G + "0000094C", "Cupboard (by the bench)", ["alchemy.food"]),
    (G + "000009F6", "Noble cupboard", ["books.*"]),
    (G + "000008EF", "Wardrobe (by the weapon plaque)", ["weapons.*"]),
    (G + "000008F5", "Bedroom wardrobe", ["armor.clothing"]),
    (G + "00000A12", "Chest of drawers (lower hall)", ARM),
    (G + "00000BC0", "Chest of drawers (by the sconces)", ["ammo.*"]),
    (G + "00000C00", "Bedroom night table", ["keys"]),
    (G + "00000831", "Strong box (by the coffins)", ["misc.animal_parts"]),
])

GH = "ccVSVSSE004-BeAFarmer.esl|0x"
build("Goldenhills Plantation", "CreationClub/GoldenhillsPlantation.json", U + "000030EC",
      ["Skyrim.esm", "Update.esm", "ccVSVSSE004-BeAFarmer.esl"], GH + "000009F8", [
    (GH + "00000955", "Laboratory chest (upgrade)", ["soulgems.*", "scrolls", "books.spelltomes"]),
    (GH + "0000096A", "Laboratory satchel (upgrade)", ALC),
    (GH + "00000A90", "Bedroom chest (upgrade)", ["weapons.*"] + ARM),
    (GH + "00000DA9", "Bedroom end table (upgrade)", ["armor.jewelry", "misc.gems"]),
    (GH + "00000A05", "Wardrobe", ["armor.clothing"]),
    (GH + "00000A8F", "Loft strong box (upgrade)", ["misc.ores_ingots", "misc.hides_pelts"]),
], extra_ignores={
    GH + "000009F4": "Farm pantry: the farm's own script fills and empties it.",
    GH + "00000DB9": "Goes away when the farmhouse is cleaned up: anything inside would be lost.",
    GH + "00000AF8": "Goes away with the children's bedroom upgrade.",
}, note="The bookshelves are scripted and the pantry is the farm's: books, food, ammo, keys and most crafting materials "
        "go to the master.")

N = "ccAFDSSE001-DweSanctuary.esm|0x"
build("Nchuanthumz", "CreationClub/Nchuanthumz.json", N + "00000A5E",
      ["Skyrim.esm", "Update.esm", "ccAFDSSE001-DweSanctuary.esm"], N + "00000DB9", [
    (N + "00000EA8", "Chest (by the smelter)", ["misc.ores_ingots", "misc.dwemer"]),
    (N + "000011C6", "Chest (by the mannequins)", ARM),
    (N + "00001143", "Strong box (by the forge)", ["weapons.*"]),
    (N + "0000116B", "Strong box (by the grindstone)", ["ammo.*"]),
    (N + "000FEA97", "Bedroom dresser", ["armor.clothing"]),
    (N + "000013C2", "Chest (by the bed rolls)", ["armor.jewelry", "misc.gems"]),
    (N + "00000F25", "Garden wing chest (by the beds)", ["books.*"]),
    (N + "00048630", "Garden wing chest (by the bookshelves)", ["soulgems.*", "scrolls"]),
    (N + "000011DC", "Alchemy satchel", ["alchemy.ingredients"]),
    (N + "00029FDB", "Alchemy satchel (second)", ["alchemy.potions", "alchemy.poisons"]),
    (N + "00000A60", "Garden wing cupboard", ["misc.hides_pelts", "misc.animal_parts"]),
    (N + "00000E8F", "Kitchen cupboard", ["alchemy.food"]),
])

DMD = "ccBGSSSE031-AdvCyrus.esm|0x"
build("Dead Man's Dread", "CreationClub/DeadMansDread.json", DMD + "000009D5",
      ["Skyrim.esm", "Update.esm", "ccBGSSSE031-AdvCyrus.esm"], DMD + "00001620", [
    (DMD + "00001444", "Captain's cabin safe", ["armor.jewelry", "misc.gems"]),
    (DMD + "000015FA", "Captain's wardrobe", ["armor.clothing"]),
    (DMD + "00000F43", "Captain's cabin dresser", ["books.*"]),
    (DMD + "000014B6", "Captain's cabin knapsack", ["keys"]),
    (DMD + "000015F9", "Boatswain's wardrobe", ARM),
    (DMD + "00000A59", "Crew quarters dresser", ["weapons.*"]),
    (DMD + "00000B4C", "Cupboard (by the grindstone)", ["ammo.*"]),
    (DMD + "000014F1", "Cupboard (by the anvil)", ["misc.tools", "misc.building_materials"]),
    (DMD + "00001512", "Barrel (by the tanning rack)", ["misc.ores_ingots", "misc.dwemer"]),
    (DMD + "00001513", "Barrel (by the tanning rack, 2)", ["misc.hides_pelts", "misc.animal_parts"]),
    (DMD + "000014AB", "Satchel (by the alchemy lab)", ["alchemy.ingredients"]),
    (DMD + "00001478", "Barrel (by the alchemy lab)", ["alchemy.potions", "alchemy.poisons"]),
    (DMD + "00001485", "Cupboard (by the arcane enchanter)", ["soulgems.*", "scrolls"]),
    (DMD + "00001269", "Galley cupboard (by the cooking pot)", ["alchemy.food"]),
], extra_ignores={DMD + "000009F2": "The ship's loot chest: the game may change what is in it."})

# Arch-Mage's Quarters (the College of Winterhold): a home once the player is Arch-Mage.
build("Arch-Mage's Quarters", "Vanilla/ArchMagesQuarters.json", S + "000CAB92", VAN, S + "00077D7E", [
    (S + "00077CEB", "Bedroom safe", ["armor.jewelry", "misc.gems", "soulgems.*"]),
    (S + "000776D0", "Bedroom wardrobe", ["armor.clothing"]),
    (S + "000776B8", "Bedroom chest of drawers", ARM),
    (S + "000776D1", "Bedroom chest of drawers (second)", ["weapons.*", "ammo.*"]),
    (S + "00077689", "Barrel (by the alchemy lab)", ["alchemy.ingredients"]),
    (S + "000776B6", "Barrel (by the alchemy lab, 2)", ["alchemy.potions"]),
    (S + "001096F9", "Barrel (by the pile of sacks)", ["alchemy.poisons"]),
    (S + "000776A8", "Food barrel", ["alchemy.food"]),
    (S + "0010CE90", "Large sack (by the bed)", ["books.*", "scrolls"]),
    (S + "0010CE6E", "Sack (by the bed)", ["keys"]),
    (S + "00077CEC", "Large sack (by the pile of sacks)", ["misc.ores_ingots", "misc.dwemer"]),
    (S + "00077CED", "Large sack (by the food barrel)", ["misc.hides_pelts", "misc.animal_parts"]),
    (S + "00077D7B", "Large sack (by the alchemy lab)", ["misc.building_materials", "misc.tools", "misc.clutter"]),
])

# ------------------------------------------------------------------ Mod homes
# Homes the Lux patch hub supports, each tested in game (tests/ingame plans 51-55, 57). Built only when the home's
# plugin is in the MO2 mods folder; otherwise the shipped file is left as it is.
MO2 = Path(os.environ.get("DOVASTASH_MO2") or r"E:\modding\Kexodus Skyrim")


# The note of a home whose areas each have a master of their own (`cells[].master`).
AREA_NOTE = "In another area the master is out of reach: what has no container there goes to that area's own master."


def mod_home(name, file, plugin, cell, master, bindings, extra_ignores=None, cells=None, shared=False, **kw):
    hits = sorted((MO2 / "mods").glob(f"*/{plugin}")) if (MO2 / "mods").is_dir() else []
    if not hits:
        print(f"{file}: skipped ({plugin} is not in {MO2 / 'mods'})")
        return
    lo = E.build_load_order(argparse.Namespace(data=None, plugins=None, vanilla_only=False, add=[str(hits[0])], lang="english"))
    VERSIONS.setdefault(file, "1.0.0")
    build(name, file, cell, ["Skyrim.esm", "Update.esm", plugin], master, bindings, extra_ignores, lo=lo, cells=cells,
          shared=shared, **kw)


P = "Eli_Pinegrove Lodge.esp|0x"
mod_home("Pinegrove Lodge", "Mods/PinegroveLodge.json", "Eli_Pinegrove Lodge.esp", P + "0002933F", P + "001315AD", [
    (P + "001315D3", "Weapon barrel", ["weapons.*"]),
    (P + "00084CDF", "Armor stand", ["armor.heavy", "armor.light"]),
    (P + "00084CE4", "Armor stand (second)", ["armor.shields"]),
    (P + "001315AE", "Chest (by the master chest)", ["ammo.*"]),
    (P + "00136720", "Strong box (by the armor stand)", ["misc.hides_pelts", "misc.animal_parts"]),
    (P + "0007FBC0", "Bedroom dresser", ["armor.clothing"]),
    (P + "0007FBC2", "Jewelry tray", ["armor.jewelry", "misc.gems"]),
    (P + "0009407C", "Strong box (by the double bed)", ["keys"]),
    (P + "0013673C", "Upstairs chest", ["misc.ores_ingots", "misc.dwemer", "misc.building_materials", "misc.tools"]),
    (P + "000C6C36", "Food stores", ["alchemy.food"]),
    (P + "000E54AB", "Ingredients basket (by the alchemy lab)", ["alchemy.ingredients"]),
    (P + "000EA608", "Barrel (by the alchemy lab)", ["alchemy.potions", "alchemy.poisons"]),
    (P + "000EA609", "Large sack (by the alchemy lab)", ["misc.clutter"]),
    (P + "000E54AD", "Book basket (by the alchemy lab)", ["books.*"]),
    (P + "00131599", "Scroll basket (by the arcane enchanter)", ["scrolls"]),
    (P + "0013157D", "Soul gems (by the arcane enchanter)", ["soulgems.*"]),
])

B = "mtBalRuhn.esp|0x"
mod_home("Bal Ruhn", "Mods/BalRuhn.json", "mtBalRuhn.esp", B + "00000D64", B + "000018DC", [
    (B + "000044CB", "Armor and weapons", ARM + ["weapons.*", "ammo.*"]),
    (B + "0000B330", "Staves", ["weapons.staves"]),
    (B + "000018E9", "Upstairs wardrobe", ["armor.clothing"]),
    (B + "000044E5", "Wall safe", ["armor.jewelry", "misc.gems"]),
    (B + "0000B317", "Soul gems", ["soulgems.*"]),
    (B + "0000E231", "Scrolls", ["scrolls"]),
    (B + "00003F61", "Recipes", ["books.notes"]),
    (B + "000044F2", "Bookcase", ["books.*"]),
    (B + "0000E249", "Potions and poisons", ["alchemy.potions", "alchemy.poisons"]),
    (B + "0000B334", "Urn (by the alchemy lab)", ["alchemy.ingredients"]),
    (B + "000044F8", "Kitchen chest", ["alchemy.food"]),
    (B + "0000E215", "Smithing supplies", ["misc.ores_ingots", "misc.dwemer", "misc.building_materials", "misc.tools"]),
    (B + "000044CC", "Barrel (by the smithing supplies)", ["misc.hides_pelts", "misc.animal_parts"]),
    (B + "00011116", "Sacks (by the master chest)", ["misc.clutter"]),
    (B + "00002403", "Upstairs end table", ["keys"]),
], extra_ignores={B + "0000E20A": "The imbuing chamber's recipe chest: its script handles what is inside."})

W = "WuthRein.esp|0x"
mod_home("Wuth Rein", "Mods/WuthRein.json", "WuthRein.esp", W + "00001308", W + "00001488", [
    (W + "00001409", "Barrel (by the grindstone)", ["misc.ores_ingots", "misc.dwemer"]),
    (W + "000014AF", "Barrel (by the grindstone, 2)", ["misc.building_materials", "misc.tools"]),
    (W + "000013F7", "Barrel (by the tanning rack)", ["misc.hides_pelts", "misc.animal_parts"]),
    (W + "00001388", "Chest (by the arcane enchanter)", ["soulgems.*", "scrolls"]),
    (W + "0000137D", "Chest (by the arcane enchanter, 2)", ["weapons.*", "ammo.*"]),
    (W + "000013C7", "Barrel (by the arcane enchanter)", ["alchemy.food"]),
    (W + "0000138A", "Chest (by the alchemy lab)", ARM),
    (W + "000013D7", "Urn (by the alchemy lab)", ["alchemy.ingredients"]),
    (W + "0002730C", "Satchel (by the alchemy lab)", ["alchemy.potions", "alchemy.poisons"]),
    (W + "0000141B", "Chest (by the bed)", ["armor.clothing"]),
    (W + "00001337", "Strong box (by the bed)", ["armor.jewelry", "misc.gems"]),
    (W + "00001405", "Chest (by the bookshelves)", ["books.*"]),
    (W + "0002C4CB", "Strong box (by the single bed)", ["keys"]),
    (W + "0002C4A0", "Chest (by the single bed)", ["misc.clutter"]),
# Its bookshelves (scripted, ignored as storage) are not offered as displays: a book a Deposit put on 0x13C5 was gone a
# second later with no move logged (tests/ingame plan 53, 3.4). Books go to the chest by the shelves until the plugin
# knows where it went.
], skip_displays={W + f"0000{r}": "loses a book" for r in ("13C5", "3043", "3070", "307F")})

EB = "Undriel_Ebongrove.esp|0x"
mod_home("Ebongrove", "Mods/Ebongrove.json", "Undriel_Ebongrove.esp", EB + "00026902", EB + "0009BB37", [
    (EB + "0017ACF8", "Weapons", ["weapons.*", "ammo.*"]),
    (EB + "0017ACE8", "Armor", ARM),
    (EB + "002ECC3B", "Staffs", ["weapons.staves"]),
    (EB + "000C9604", "Soul gems", ["soulgems.*"]),
    (EB + "000AFF98", "Scrolls", ["scrolls"]),
    (EB + "000B50D0", "Ingredients", ["alchemy.ingredients"]),
    (EB + "000C44B5", "Potions", ["alchemy.potions", "alchemy.poisons"]),
    (EB + "000C95F9", "Jewelry", ["armor.jewelry", "misc.gems"]),
    (EB + "000E7D3E", "Wardrobe", ["armor.clothing"]),
    (EB + "0017AC77", "Meat", ["alchemy.food"]),
    (EB + "00259D57", "Pelts and leather", ["misc.hides_pelts", "misc.animal_parts"]),
    (EB + "00129BA4", "Crafting supplies (by the smelter)", ["misc.ores_ingots", "misc.dwemer"]),
    (EB + "00091906", "Crafting supplies", ["misc.building_materials", "misc.tools"]),
    (EB + "002ECC98", "Misc", ["misc.clutter"]),
    (EB + "000BF35F", "Bookcase", ["books.*"]),
    (EB + "00138ECB", "Strong box (by the bed)", ["keys"]),
])

HY = "Eli_WitchyHaus.esp|0x"
mod_home("Hydda", "Mods/Hydda.json", "Eli_WitchyHaus.esp", HY + "00005921", HY + "00051C1D", [
    (HY + "0001EF0A", "Dresser (by the bed)", ["armor.clothing"]),
    (HY + "0001EF13", "Dresser (by the cooking pot)", ["weapons.*", "ammo.*"] + ARM),
    (HY + "00056D8A", "Staves", ["weapons.staves"]),
    (HY + "0004CA0C", "Key box", ["keys"]),
    (HY + "0005BEAF", "Strongbox", ["armor.jewelry", "misc.gems"]),
    (HY + "00051C62", "Soul gems", ["soulgems.*"]),
    (HY + "00051B5E", "Ingredients basket (by the alchemy lab)", ["alchemy.ingredients"]),
    (HY + "00051BFA", "Potions", ["alchemy.potions"]),
    (HY + "00051BD9", "Poisons", ["alchemy.poisons"]),
    (HY + "0003D633", "Bottom shelf (by the cooking pot)", ["alchemy.food"]),
    (HY + "0004CA3A", "Bookcase", ["books.*"]),
    (HY + "0004CA3C", "Bookcase (second)", ["scrolls"]),
    (HY + "0001EF76", "Large sack (by the cooking pot)", ["misc.clutter"]),
    (HY + "00051C02", "Large sack (by the alchemy lab)",
     ["misc.ores_ingots", "misc.hides_pelts", "misc.animal_parts", "misc.building_materials", "misc.tools", "misc.dwemer"]),
], extra_ignores={HY + "00051B72": "Disenchanter: its script takes the enchantment off what you put in."})

BF = "solstheimretreat.esp|0x"
mod_home("Black Fur Refuge", "Mods/BlackFurRefuge.json", "solstheimretreat.esp", BF + "00005A00", BF + "0000ADFE", [
    (BF + "00005B3F", "Wardrobe", ARM),
    (BF + "00005B2F", "Dresser (by the bed)", ["armor.clothing"]),
    (BF + "00005B3D", "Upstairs safe", ["armor.jewelry", "misc.gems"]),
    (BF + "00005B39", "Upstairs chest", ["weapons.*", "ammo.*"]),
    (BF + "00005B3E", "Strong box (by the bed)", ["keys"]),
    (BF + "0000ADE3", "Safe (by the alchemy lab)", ["soulgems.*", "scrolls"]),
    (BF + "0000ADE2", "Strong box (by the alchemy lab)", ["books.*"]),
    (BF + "00005A99", "Large sack (by the alchemy lab)", ["alchemy.ingredients"]),
    (BF + "0000FF38", "Chest (by the master chest)", ["alchemy.food"]),
    (BF + "0000FF39", "Strong box (by the master chest)", ["alchemy.potions", "alchemy.poisons"]),
    (BF + "00005A0B", "Large sack (by the master chest)", ["misc.clutter"]),
    (BF + "0001A219", "Sack (by the cooking pot)",
     ["misc.ores_ingots", "misc.hides_pelts", "misc.animal_parts", "misc.building_materials", "misc.tools", "misc.dwemer"]),
])

# The Lux-listed homes the in-game plans 41-46 and 56 test: the bindings those runs use (the Dova Stash Editor's draft,
# checked in game) without the ones a container reachable from everywhere always beats, labelled like the others; the
# areas keep the game's cell names.
SR = "Eli_Sicarius' Refuge.esp|0x"
mod_home("Sicarius' Refuge", "Mods/SicariusRefuge.json", "Eli_Sicarius' Refuge.esp", SR + "0016D087", SR + "0016D16A", [
    (SR + "00011D3B", "Poisons", ["alchemy.poisons"]),
    (SR + "0016CCA5", "Arrows", ["ammo.*"]),
    (SR + "0016CCD3", "Wardrobe", ["armor.clothing"]),
    (SR + "0016CCD7", "Chest (by the wardrobe)", ["books.*"]),
    (SR + "0016CD28", "Potions", ["alchemy.potions"]),
    (SR + "0016CD49", "Ingredients (by the soul gems)", ["alchemy.ingredients"]),
    (SR + "0016CDE6", "Soul gems", ["soulgems.*"]),
    (SR + "0016CE8F", "Weapon barrel", ["weapons.*"]),
    (SR + "0016CE9A", "Tool barrel", ["misc.tools"]),
    (SR + "0016CEF8", "Misc supplies", ["misc.clutter"]),
    (SR + "0016CEFA", "Ingots", ["misc.ores_ingots"]),
    (SR + "0016CEFB", "Bones and remains", ["misc.animal_parts"]),
    (SR + "0016CEFC", "Leathers and linen", ["misc.hides_pelts"]),
    (SR + "0016CEFD", "Firewood", ["misc.building_materials"]),
    (SR + "0016D0EE", "Meats", ["alchemy.food"]),
    (SR + "0016D123", "Armor storage", ARM),
    (SR + "0016D1A6", "Strong box", ["armor.jewelry", "misc.gems"]),
], extra_ignores={SR + "0016CF76": "Trash bin: what you put in it is thrown away.",
                  SR + "0016CEDA": "Disenchanter: its script takes the enchantment off what you put in."},
   skip_displays={SR + "0016CC2D": "disabled, and nothing in the mod enables it: no Deposit could dress it",
                  SR + "0016CC2E": "the mannequin of that activator: never there to dress either"})

NF = "Niflholm.esp|0x"
mod_home("Niflholm", "Mods/Niflholm.json", "Niflholm.esp", NF + "00005913", NF + "000EE489", [
    (NF + "00001B17", "Kitchen sack", ["misc.clutter"]),
    (NF + "000047A7", "Firewood", ["misc.building_materials"]),
    (NF + "00007AA0", "Scrolls", ["scrolls"]),
    (NF + "00007AA3", "Ingredients", ["alchemy.ingredients"]),
    (NF + "00007AA4", "Spell tomes", ["books.spelltomes"]),
    (NF + "00007AA6", "Soul gems", ["soulgems.*"]),
    (NF + "000177F7", "Food storage", ["alchemy.food"]),
    (NF + "00017812", "Books", ["books.*"]),
    (NF + "00019AE3", "Jewelry", ["armor.jewelry", "misc.gems"]),
    (NF + "0004B93E", "Alchemical recipes", ["books.notes"]),
    (NF + "0004B942", "Poisons", ["alchemy.poisons"]),
    (NF + "0004B943", "Potions", ["alchemy.potions"]),
    (NF + "00097CA8", "Keys", ["keys"]),
    (NF + "000B136B", "Leather and strips", ["misc.hides_pelts"]),
    (NF + "000B137C", "Ingots", ["misc.ores_ingots"]),
    (NF + "0017C6EF", "Armor", ["armor.*"]),
    (NF + "0021F6C1", "Staffs", ["weapons.staves"]),
    (NF + "0022EB39", "Weapons", ["weapons.*"]),
    (NF + "00233C56", "Bows and arrows", ["ammo.*"]),
    # In the guest house the kitchen sack is out of reach (it is not persistent): this one takes the clutter there.
    (NF + "001AF905", "Guest house knapsack", ["misc.clutter"]),
], extra_ignores={NF + "0017C6C2": "Runs a script on what you put in (the mod's sell cupboard).",
                  NF + "0017C6C1": "Runs a script on what you put in (the mod's drop cupboard)."},
   # The master is not persistent: each area has a chest of its own for what has no container there (`cells[].master`).
   cells=[(NF + "00001880", "Niflholm", NF + "000E41EC"), (NF + "00007A95", "Niflholm Mage Hall", NF + "000416F3"),
          (NF + "000077D9", "Niflholm Living Hall", NF + "000B6541"), (NF + "000199A1", "Niflholm Storage", NF + "00097CD8"),
          (NF + "001AA4CF", "Niflholm Guest House", NF + "001AA5A1"), (NF + "0024829C", "Niflholm Hidden Cellar", NF + "002483D9")],
   shared=True, note=AREA_NOTE,
   # Nothing is bound in the storage room or the living hall: unbound furniture names the ignored containers there.
   landmarks=[(NF + "00097CD8", "by the noble chest"), (NF + "00019A9C", "by the knapsack"), (NF + "00019ABD", "by the sacks"),
              (NF + "00200EBE", "by the drawers"), (NF + "00200ED5", "by the cupboards"),
              (NF + "00200EB7", "by the far cupboards")])

TJ = "rmp_telvannihearthfires.esp|0x"
mod_home("Tel Jerdein", "Mods/TelJerdein.json", "rmp_telvannihearthfires.esp", TJ + "00014D76", TJ + "00084894", [
    (TJ + "000899C1", "Ingredients", ["alchemy.ingredients"]),
    (TJ + "000AD226", "Potions", ["alchemy.potions", "alchemy.poisons"]),
    (TJ + "000AD229", "Soul gems", ["soulgems.*"]),
    (TJ + "0011CDEB", "Foyer sack", ["misc.clutter"]),
    (TJ + "00121F43", "Root cellar dresser", ["misc.ores_ingots", "misc.hides_pelts"]),
    (TJ + "001730CB", "Bedroom bookcase", ["books.*"]),
    (TJ + "001730CC", "Bedroom wardrobe", ["armor.clothing"]),
    (TJ + "001781D7", "Foyer cupboard", ["alchemy.food"]),
    (TJ + "001730C5", "Bedroom chest of drawers", ["weapons.*", "ammo.*"] + ARM),
    (TJ + "001730C9", "Bedroom end table", ["armor.jewelry", "misc.gems"]),
    (TJ + "001730C3", "Bedroom knapsack", ["keys"]),
    (TJ + "0011CDDB", "Foyer dresser", ["scrolls"]),
    (TJ + "0011CDE1", "Foyer large sack", ["misc.dwemer"]),
    (TJ + "000A3004", "Root cellar wardrobe", ["misc.building_materials", "misc.tools"]),
    (TJ + "00127090", "Root cellar chest", ["misc.animal_parts"]),
], cells=[(TJ + "0000AABB", "Tel Jerdein - Foyer", TJ + "00075488"), (TJ + "0008EADA", "Tel Jerdein - Root Cellar", TJ + "0012708F")],
   note=AREA_NOTE)

AN = "Antennaria.esp|0x"
mod_home("Antennaria", "Mods/Antennaria.json", "Antennaria.esp", AN + "00000D62", AN + "0000B018", [
    (AN + "00006F8A", "Ingot storage", ["misc.ores_ingots"]),
    (AN + "00007532", "Organs and eggs", ["alchemy.food"]),
    (AN + "0000752B", "Flora", ["alchemy.ingredients"]),
    (AN + "00007526", "Supplies", ["alchemy.potions", "alchemy.poisons"]),
    (AN + "0000B024", "Jewelry chest", ["armor.jewelry"]),
    (AN + "0000B025", "Chest (by the jewelry chest)", ["soulgems.*", "scrolls"]),
    (AN + "0000B017", "Book drawer", ["books.*"]),
    (AN + "0000E34D", "Gem chest", ["misc.gems"]),
    (AN + "0000BB06", "Weapon chest", ["weapons.*"]),
    (AN + "0000BB17", "Ammo chest", ["ammo.*"]),
    (AN + "0000BB0A", "Armor chest", ARM),
    (AN + "0000BB18", "Clothing chest", ["armor.clothing"]),
    (AN + "0000BB2D", "Chest (by the dwarven chests)", ["misc.hides_pelts"]),
    (AN + "0000BB2C", "Chest (by the dwarven chests, 2)", ["misc.animal_parts"]),
    (AN + "0000BB30", "Chest (by the dwarven chests, 3)", ["misc.clutter"]),
    (AN + "0000BB2F", "Tool chest", ["misc.tools", "misc.building_materials", "misc.dwemer"]),
], cells=[(AN + "0000A3A2", "Antennaria Library")], kinds={AN + "0000752A": "Large sack"})

WC = "Winter Cove.esp|0x"
# The storage room's chests are persistent: reached from the main hall too. The main hall's own containers are not:
# they are used while the player is there (soul gems before the Scrolls chest, the same pattern listed first; clothes and
# jewelry by their exact ids before the Armor chest's armor.*), and the storage room's chests otherwise. The master is a
# main hall chest; the storage room's own chest (not persistent) is that area's master.
mod_home("Winter Cove", "Mods/WinterCove.json", "Winter Cove.esp", WC + "00000D62", WC + "00023D65", [
    (WC + "00012A70", "Soul gems", ["soulgems.*"]),
    (WC + "0001CA30", "Wardrobe (by the soul gems)", ["armor.clothing"]),
    (WC + "0001CA33", "End table (by the wardrobe)", ["armor.jewelry"]),
    (WC + "0000E31B", "Food", ["alchemy.food"]),
    (WC + "0000E31C", "Firewood", ["misc.building_materials", "misc.clutter"]),
    (WC + "0000E31D", "Books", ["books.*"]),
    (WC + "0000E328", "Potions and poisons", ["alchemy.poisons", "alchemy.potions"]),
    (WC + "0000E329", "Ingredients", ["alchemy.ingredients"]),
    (WC + "00012A7C", "Scrolls", ["scrolls", "soulgems.*"]),
    (WC + "00012A7D", "Keys", ["keys", "misc.gems"]),
    (WC + "00012A7E", "Armor", ["armor.*"]),
    (WC + "00012A7F", "Shields", ["armor.shields"]),
    (WC + "000216D3", "Notes and journals", ["books.notes"]),
    (WC + "000216D4", "Weapons", ["weapons.*", "ammo.*"]),
    (WC + "000216D5", "Ingots", ["misc.ores_ingots", "misc.dwemer", "misc.tools"]),
    (WC + "000216D7", "Leather and strips", ["misc.hides_pelts", "misc.animal_parts"]),
], cells=[(WC + "0000E30B", "Winter Cove storage", WC + "000216D8")], shared=True,
   note="In the storage room the master is out of reach: what has no container there goes to the storage room's chest.",
   # The armory's own pieces name its racks.
   landmarks=[(WC + "000324A7", "by the shield racks"), (WC + "00032A21", "by the shield racks"),
              (WC + "00032A25", "by the mannequin"), (WC + "00032489", "by the weapon racks")])

# Unique Vampire Dens: two of its dens are homes (Lost Hall in Windhelm, the Secret Cellar in Solitude).
UV = "gonkishvampdens.esp|0x"
mod_home("Lost Hall", "Mods/LostHall.json", "gonkishvampdens.esp", UV + "000736AA", UV + "00082A50", [
    (UV + "000739DF", "End table (by the wardrobes)", ["books.*"]),
    (UV + "000739FE", "Wardrobe", ["armor.clothing"]),
    (UV + "00082B6A", "Food barrel", ["alchemy.food"]),
    (UV + "00082B6B", "Ingredients barrel", ["alchemy.ingredients"]),
    (UV + "000739F3", "Wardrobe (by the cupboard)", ARM),
    (UV + "000739F2", "Wardrobe (by the cupboard, 2)", ["weapons.*", "ammo.*"]),
    (UV + "000739F4", "Cupboard (by the wardrobes)", ["alchemy.potions", "alchemy.poisons"]),
    (UV + "000739E0", "End table (by the wardrobes, 2)", ["armor.jewelry", "misc.gems"]),
], note="There is little furniture here: soul gems, scrolls, keys and crafting materials go to the master too.")
mod_home("Secret Cellar", "Mods/SecretCellar.json", "gonkishvampdens.esp", UV + "00057CD7", UV + "0005807B", [
    (UV + "00058093", "Bookcase (by the noble chest)", ["books.*"]),
    (UV + "0005F8B8", "Noble chest", ALC),
    (UV + "00064897", "Food barrel", ["alchemy.food"]),
    (UV + "00064898", "Sack (by the food barrel)", ["misc.clutter"]),
    (UV + "000648A3", "Strong box", ["armor.jewelry", "misc.gems"]),
    (UV + "00058068", "End table (by the master chest)", ["soulgems.*", "scrolls"]),
    (UV + "00058069", "End table (by the noble chest)", ["keys"]),
    (UV + "0006489A", "Large sack (by the food barrel)",
     ["misc.ores_ingots", "misc.hides_pelts", "misc.animal_parts", "misc.building_materials", "misc.tools", "misc.dwemer"]),
], note="No wardrobe here: weapons, armor and clothes go to the master too.")

# Retired templates (merged into their home's one template): gone from the shipped data.
for r in RETIRED:
    if (OUT / r).exists():
        (OUT / r).unlink()
        print(f"{r}: retired, deleted")
