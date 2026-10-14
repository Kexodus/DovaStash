"""Generate Dova Stash home templates from hand-picked bindings + facts read with tools/esm_dump.py.

Ignored entries are derived automatically from the plugin data:
  * base CONT has the respawn flag                      -> ignored ("respawns")
  * PlayerBookShelfContainer (scripted display shelf)   -> ignored (never storage; a template may also list
    it under "displays" as a bookshelf, kind "bookshelf", to have Deposit put books on it)
  * enabled only until a house upgrade (enable parent with "opposite" flag) and no Hearthfire
    divert script                                        -> ignored ("removed by an upgrade")
  * explicit extra ignores (quest/test containers)
Ignored entries are labelled with the game's name for the container; the reason goes in "note".
"""
import argparse
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

ALC = ["alchemy.ingredients", "alchemy.potions", "alchemy.poisons"]
ARM = ["armor.heavy", "armor.light", "armor.shields"]


def cell_containers(cell_ref):
    key = E.parse_ref(cell_ref)
    refs = LO.cell_refs(key)
    out = {}
    for rec, _g in refs.values():
        d = E.decode_ref(LO, rec, ["CONT"], refs)
        if d and d["baseType"] == "CONT" and not d["deleted"]:
            out[d["ref"].lower()] = d
    return out


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
            reason = "Goes away with a house upgrade: anything inside would be lost."
        if reason:
            res.append({"ref": d["ref"], "label": name, "ignored": True, "note": reason})
    return res


def build(name, file, cell, requires, master, bindings, extra_ignores=None, auto=True):
    conts = cell_containers(cell)
    extra_ignores = {k.lower(): v for k, v in (extra_ignores or {}).items()}
    bound = {}
    entries = []
    for ref, label, cats in bindings:
        k = ref.lower()
        assert k in conts, f"{name}: {ref} is not a container in {cell}"
        d = conts[k]
        assert not d["respawns"], f"{name}: {ref} respawns"
        assert k not in bound, f"{name}: duplicate {ref}"
        bound[k] = True
        entries.append({"ref": d["ref"], "label": label, "categories": cats})
    mk = master.lower()
    assert mk in conts and not conts[mk]["respawns"], f"{name}: bad master {master}"
    assert mk not in bound
    bound[mk] = True
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
            assert c not in seen_cats, f"{name}: category {c} bound twice ({seen_cats[c]} / {e['ref']})"
            seen_cats[c] = e["ref"]
    master_name = (conts[mk].get("baseName") or "master container").lower()
    doc = {
        "$schema": "dovastash-layout-1",
        "name": name,
        "note": f"Each container takes the item categories listed (see Categories/00_Default.json); ignored ones "
                f"are never used. Anything without a container of its own goes to the master, the {master_name}.",
        "author": "Dova Stash",
        "version": "1.0.0",
        "priority": 0,
        "requires": requires,
        **({"conflicts": CONFLICTS[file]} if file in CONFLICTS else {}),
        "cell": cell,
        "master": conts[mk]["ref"],
        "containers": entries,
    }
    path = OUT / file
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(jsonfmt.dumps(doc, width=200, align=True) + "\n", encoding="utf-8")
    nb = sum(1 for e in entries if not e.get("ignored"))
    ni = sum(1 for e in entries if e.get("ignored"))
    print(f"{file}: {nb} bound, {ni} ignored, master {conts[mk]['ref']} ({conts[mk]['baseName']}), "
          f"{len(conts)} containers in cell, {sum(1 for d in conts.values() if d['respawns'])} respawning")


S = "Skyrim.esm|0x"
H = "HearthFires.esm|0x"
D = "Dragonborn.esm|0x"
VAN = ["Skyrim.esm", "Update.esm"]
VAN_HF = ["Skyrim.esm", "Update.esm", "HearthFires.esm"]

# ------------------------------------------------------------------ Breezehome
build("Breezehome", "Vanilla/Breezehome.json", S + "000165A8", VAN_HF, S + "000F3922", [
    (S + "0010EB84", "Alchemy corner chest (next to the alchemy lab)", ALC),
    (S + "0003EE69", "Dining room cupboard", ["alchemy.food"]),
    (S + "000CB437", "Loft cupboard (by the weapon plaque)", ["weapons.*"]),
    (S + "000CB433", "Housecarl's room dresser", ARM),
    (S + "000CB435", "Housecarl's room end table", ["ammo.*"]),
    (S + "000CB432", "Bedroom dresser", ["armor.clothing"]),
    (S + "000CB43A", "Bedroom night table", ["armor.jewelry", "misc.gems"]),
    (S + "000CB42D", "Bedroom end table (left)", ["soulgems.*", "scrolls"]),
    (S + "000CB42E", "Bedroom end table (right)", ["books.*"]),
    (S + "000CB442", "Living room cupboard", ["misc.ores_ingots", "misc.hides_pelts", "misc.animal_parts", "misc.dwemer"]),
    (S + "000CB45D", "Kitchen sack (large)", ["misc.building_materials", "misc.tools"]),
    (S + "000CB45E", "Kitchen sack (large, second)", ["misc.clutter"]),
    (S + "000CB46C", "Living room sack (small)", ["keys"]),
])

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
])

# ------------------------------------------------------------------ Vlindrel Hall
build("Vlindrel Hall", "Vanilla/VlindrelHall.json", S + "00016DFA", VAN_HF, S + "00077CAB", [
    (S + "000E15AD", "Alchemy corner large sack", ALC),
    (S + "000E157F", "Living room cupboard", ["alchemy.food"]),
    (S + "000DC683", "Living room dresser (by the dagger rack)", ["misc.dwemer"]),
    (S + "000E15C4", "Living room end table", ["keys"]),
    (S + "000E2E5A", "Bedroom wardrobe", ["armor.clothing"]),
    (S + "000E2E5D", "Bedroom chest of drawers", ["armor.jewelry", "misc.gems"]),
    (S + "000E2E5B", "Bedroom cupboard (by the dagger rack)", ["weapons.*"]),
    (S + "000E2E62", "Bedroom cupboard (second, by the dagger rack)", ARM),
    (S + "000E2E5C", "Bedroom cupboard (back wall)", ["ammo.*"]),
    (S + "000E2E65", "Bedroom cupboard (side wall)", ["soulgems.*", "scrolls"]),
    (S + "000DC602", "Bedroom dresser", ["books.*"]),
    (S + "00077CC6", "Guest room cupboard", ["misc.ores_ingots", "misc.hides_pelts"]),
    (S + "000E2ECE", "Guest room dresser", ["misc.animal_parts", "misc.building_materials"]),
    (S + "000DC678", "Guest room end table", ["misc.tools"]),
    (S + "000DC6B0", "Upper hall sack", ["misc.clutter"]),
], extra_ignores={
    S + f"000C645{i}": "Your spouse's shop stock: it respawns." for i in range(5)
})

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
    (D + "00036E04", "Armory large sack (second)", ["ammo.*"]),
    (D + "000359BF", "Alchemy lab sack (ingredients)", ["alchemy.ingredients"]),
    (D + "000359C0", "Alchemy lab sack (potions)", ["alchemy.potions"]),
    (D + "000359C1", "Alchemy lab sack (poisons)", ["alchemy.poisons"]),
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
    ("02", "019"): ("Main hall display wardrobe (back room, second)", ["misc.hides_pelts", "misc.animal_parts"]),
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


def hearthfire(name, file, cell, rules, master_rx):
    conts = cell_containers(cell)
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
            bindings.append((d["ref"], rule[0], rule[1]))
    assert master, name
    assert len(bindings) == len(rules), (name, len(bindings), len(rules))
    build(name, file, cell, VAN_HF, master, bindings)


for house, cell, cellar, n in [("Lakeview Manor", H + "00002FEC", H + "00004849", 1),
                               ("Windstad Manor", H + "00011CB3", H + "00015154", 2),
                               ("Heljarchen Hall", H + "00016A56", H + "00016A55", 3)]:
    fname = house.replace(" ", "")
    hearthfire(house, f"Vanilla/{fname}.json", cell, HF_MAIN, rf"^BYOHHouse{n}Room01Chest")
    hearthfire(f"{house} Cellar", f"Vanilla/{fname}Cellar.json", cellar, HF_CELLAR,
               rf"^BYOHHouse{n}InteriorRoom12Part006")

# ------------------------------------------------------------------ Creation Club
U = "Update.esm|0x"

T = "ccEEJSSE001-Hstead.esm|0x"
build("Tundra Homestead", "CreationClub/TundraHomestead.json", T + "0000087F",
      ["Skyrim.esm", "Update.esm", "ccEEJSSE001-Hstead.esm"], T + "00084A88", [
    (T + "00000A65", "Ingredients cupboard (alchemy corner)", ["alchemy.ingredients"]),
    (T + "0000098D", "Apothecary's satchel (by the alchemy lab)", ["alchemy.potions", "alchemy.poisons"]),
    (T + "00000908", "Alchemy corner cupboard", ["misc.animal_parts"]),
    (T + "00000A3C", "Shrine cupboard (by the enchanter)", ["soulgems.*"]),
    (T + "00000AC0", "Chest by the arcane enchanter", ["books.spelltomes", "scrolls"]),
    (T + "00000AAC", "Enchanter cupboard", ["keys"]),
    (T + "00000B04", "Safe", ["armor.jewelry", "misc.gems"]),
    (T + "00000B10", "Sitting room cupboard", ["books.skill", "books.notes", "books.other"]),
    (T + "00000FEF", "Kitchen low cupboard", ["alchemy.food"]),
    (T + "00000FAF", "Bedroom dresser", ["armor.clothing"]),
    (T + "00000FBA", "Bedroom cupboard", ARM + ["weapons.*", "ammo.*"]),
    (T + "0000098F", "Small chest (sitting room)", ["misc.ores_ingots", "misc.hides_pelts"]),
    (T + "00000A1D", "Sitting room strong box", ["misc.dwemer", "misc.tools", "misc.building_materials"]),
])
build("Tundra Homestead Cellar", "CreationClub/TundraHomesteadCellar.json", T + "00000B06",
      ["Skyrim.esm", "Update.esm", "ccEEJSSE001-Hstead.esm"], T + "00000F85", [
    (T + "00000F87", "Cellar safe (by the mannequins)", ["weapons.*", "ammo.*"]),
    (T + "00000D1B", "Cellar bookcase (by the mannequin)", ["books.*"]),
    (T + "00000D5C", "Cellar bookcase (by the armchair)", ARM),
    (T + "00000DBB", "Cellar bookcase (by the dragon claws)", ["misc.gems", "armor.jewelry", "misc.clutter"]),
])

M = "ccEEJSSE002-Tower.esl|0x"
MY = ["Skyrim.esm", "Update.esm", "ccEEJSSE002-Tower.esl"]
build("Myrwatch", "CreationClub/Myrwatch.json", M + "0000083B", MY, M + "00000B3A", [
    (M + "00000B15", "Armory chest (by the mannequin)", ["weapons.*"]),
    (M + "00000B2D", "Armory cupboard", ARM),
    (M + "00000B45", "Armory cupboard (by the weapon racks)", ["ammo.*"]),
    (M + "00000B4B", "Armory safe", ["armor.jewelry", "misc.gems"]),
    (M + "000009E8", "Shrine cupboard", ["scrolls"]),
    (M + "00000A60", "Shrine cupboard (second)", ["soulgems.*"]),
    (M + "000008A7", "Bedroom wardrobe", ["armor.clothing"]),
    (M + "00000BC6", "Library wardrobe", ["books.*"]),
    (M + "00000A37", "Kitchen large sack", ["alchemy.food"]),
])
build("Myrwatch Gallery", "CreationClub/MyrwatchGallery.json", M + "0000092C", MY, M + "00000E5D", [
    (M + "00000EF8", "Alchemy cupboard", ["alchemy.ingredients"]),
    (M + "00000F8C", "Alchemy cupboard (second)", ["alchemy.potions", "alchemy.poisons"]),
    (M + "00000EF7", "Soul gem barrel", ["soulgems.*"]),
    (M + "00000F0A", "Enchanting cupboard", ["scrolls"]),
    (M + "00000F7E", "Enchanting cupboard (second)", ["books.spelltomes"]),
    (M + "00000F43", "Staff enchanter cupboard", ["weapons.staves"]),
    (M + "00000E56", "Shield display cupboard", ["armor.shields"]),
    (M + "00000E99", "Armory cupboard", ["armor.heavy", "armor.light"]),
    (M + "00000F73", "Armory cupboard (by the mannequin)", ["weapons.swords", "weapons.greatswords", "weapons.daggers",
                                                           "weapons.axes", "weapons.waraxes", "weapons.maces",
                                                           "weapons.warhammers", "weapons.bows", "weapons.crossbows"]),
    (M + "00000F01", "Large sack by the tanning rack", ["misc.hides_pelts", "misc.animal_parts"]),
    (M + "00000D31", "Curio cabinet", ["misc.gems", "armor.jewelry"]),
    (M + "00000D32", "Curio cabinet (second)", ["misc.dwemer"]),
])

SF = "ccEEJSSE003-Hollow.esl|0x"
build("Shadowfoot Sanctum", "CreationClub/ShadowfootSanctum.json", U + "00002F98",
      ["Skyrim.esm", "Update.esm", "ccEEJSSE003-Hollow.esl"], SF + "00000EB7", [
    (SF + "00000995", "Smithing chest", ["misc.ores_ingots"]),
    (SF + "00000A41", "Barrel by the tanning rack", ["misc.hides_pelts", "misc.animal_parts"]),
    (SF + "00000B01", "Barrel by the workbench", ["misc.tools", "misc.building_materials", "misc.dwemer"]),
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
    (HH + "00000FF2", "Wardrobe (second)", ARM),
    (HH + "00000881", "Narrow dresser (by the bench)", ["weapons.*"]),
    (HH + "00000955", "Drawers (by the beds)", ["ammo.*"]),
    (HH + "000008A5", "Narrow dresser (by the oven)", ["alchemy.food"]),
    (HH + "00000A66", "Satchel by the alchemy lab", ALC),
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
    (BC + "00089EFD", "Chest by the dagger rack", ["misc.hides_pelts", "misc.animal_parts"]),
    (BC + "00047BC2", "Fish barrel (by the chopping block)", ["misc.clutter"]),
])

G = "ccRMSSSE001-NecroHouse.esl|0x"
build("Gallows Hall", "CreationClub/GallowsHall.json", U + "00002FD0",
      ["Skyrim.esm", "Update.esm", "ccRMSSSE001-NecroHouse.esl"], G + "00000968", [
    (G + "00000C0D", "Chest by the arcane enchanter", ["soulgems.*", "scrolls"]),
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
})

N = "ccAFDSSE001-DweSanctuary.esm|0x"
build("Nchuanthumz", "CreationClub/Nchuanthumz.json", N + "00000A5E",
      ["Skyrim.esm", "Update.esm", "ccAFDSSE001-DweSanctuary.esm"], N + "00000DB9", [
    (N + "00000EA8", "Chest by the smelter", ["misc.ores_ingots", "misc.dwemer"]),
    (N + "000011C6", "Chest by the mannequins", ARM),
    (N + "00001143", "Strong box by the forge", ["weapons.*"]),
    (N + "0000116B", "Strong box by the grindstone", ["ammo.*"]),
    (N + "000FEA97", "Bedroom dresser", ["armor.clothing"]),
    (N + "000013C2", "Chest by the bed rolls", ["armor.jewelry", "misc.gems"]),
    (N + "00000F25", "Garden wing chest (by the beds)", ["books.*"]),
    (N + "00048630", "Garden wing chest (by the bookshelves)", ["soulgems.*", "scrolls"]),
    (N + "000011DC", "Alchemy satchel", ["alchemy.ingredients"]),
    (N + "00029FDB", "Alchemy satchel (second)", ["alchemy.potions", "alchemy.poisons"]),
    (N + "00000A60", "Garden wing cupboard", ["misc.hides_pelts", "misc.animal_parts"]),
    (N + "00000E8F", "Kitchen cupboard", ["alchemy.food"]),
])
