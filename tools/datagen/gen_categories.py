"""Generate dist/.../Categories/00_Default.json (dovastash-categories-1). Higher priority wins."""
import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))  # tools/ -> esm_dump
sys.path.insert(0, str(HERE))
import esm_dump as E  # noqa: E402
import jsonfmt  # noqa: E402

OUT = HERE.parents[1] / "dist" / "Data" / "SKSE" / "Plugins" / "DovaStash" / "Categories" / "00_Default.json"
LO = E.build_load_order(argparse.Namespace(data=None, plugins=None, vanilla_only=True, add=None, lang="english"))


def misc_forms(edids):
    """Resolve MISC EditorIDs to form refs (vanilla masters) - fails loudly on typos."""
    by = {}
    for rec in LO.index("MISC").values():
        e = rec.edid()
        if e:
            by[e.lower()] = E.fmt_ref(rec.key)
    out = []
    for e in edids:
        assert e.lower() in by, f"MISC {e} not found"
        out.append(by[e.lower()])
    return out


DWEMER = misc_forms([
    # smeltable Dwemer scrap (vanilla tags these VendorItemOreIngot)
    "DwarvenScrapMetal", "DwarvenScrapBent", "DwarvenScrapLever", "DwarvenScrapLever02", "DwarvenGear",
    "DwarvenGyro", "DwarvenCog", "DwarvenPlateMetalSmall", "DwarvenPlateMetalLarge", "DwarvenLargeScrap",
    "DwarvenLargeScrap2", "DwarvenLargeScrap3", "DwarvenCenturionDynamo",
    # Dwemer tableware / curios (no vendor keyword in vanilla)
    "DwarvenBowl01", "DwarvenBowl02", "DwarvenBowl03", "DwarvenHighBowl01", "DwarvenHighBowl02",
    "DwarvenHighCup01", "DwarvenHighCup02", "DwarvenHighCup03", "DwarvenHighPlate01", "DwarvenHighPot01",
    "DwarvenFork", "DwarvenKnife", "DwarvenSpoon",
])
BUILDING = misc_forms([
    "BYOHMaterialLog", "BYOHMaterialStoneBlock", "BYOHMaterialClay", "BYOHMaterialGlass", "BYOHMaterialStraw",
    "BYOHMaterialHornGoat", "BYOHMaterialFittingsIron", "BYOHMaterialLock", "BYOHMaterialHinge", "BYOHMaterialNails",
    "Firewood01", "DLC2InvFirewood01",
])


def cat(id_, name, priority, **match):
    return {"id": id_, "name": name, "priority": priority, "match": match}


W = ["WEAP"]
CATS = [
    # ---------------------------------------------------------------- weapons
    cat("weapons.swords", "Swords", 100, formTypes=W, weaponTypes=["OneHandSword"]),
    cat("weapons.greatswords", "Greatswords", 100, formTypes=W, weaponTypes=["TwoHandSword"]),
    cat("weapons.daggers", "Daggers", 100, formTypes=W, weaponTypes=["OneHandDagger"]),
    # Battleaxes and warhammers share the TwoHandAxe engine type; the WeapTypeWarhammer keyword splits them.
    cat("weapons.axes", "Battleaxes", 100, formTypes=W, weaponTypes=["TwoHandAxe"], keywordsNone=["WeapTypeWarhammer"]),
    cat("weapons.waraxes", "War axes", 100, formTypes=W, weaponTypes=["OneHandAxe"]),
    cat("weapons.maces", "Maces", 100, formTypes=W, weaponTypes=["OneHandMace"]),
    cat("weapons.warhammers", "Warhammers", 110, formTypes=W, weaponTypes=["TwoHandAxe"], keywordsAny=["WeapTypeWarhammer"]),
    cat("weapons.bows", "Bows", 100, formTypes=W, weaponTypes=["Bow"]),
    cat("weapons.crossbows", "Crossbows", 100, formTypes=W, weaponTypes=["Crossbow"]),
    cat("weapons.staves", "Staves", 100, formTypes=W, weaponTypes=["Staff"]),
    # ---------------------------------------------------------------- armor
    cat("armor.shields", "Shields", 120, formTypes=["ARMO"], keywordsAny=["ArmorShield"]),
    cat("armor.jewelry", "Jewelry", 120, formTypes=["ARMO"], keywordsAny=["ArmorJewelry", "VendorItemJewelry"]),
    cat("armor.heavy", "Heavy armor", 100, formTypes=["ARMO"], armorTypes=["Heavy"]),
    cat("armor.light", "Light armor", 100, formTypes=["ARMO"], armorTypes=["Light"]),
    cat("armor.clothing", "Clothing", 100, formTypes=["ARMO"], armorTypes=["Clothing"]),
    # ---------------------------------------------------------------- ammo
    cat("ammo.arrows", "Arrows", 100, formTypes=["AMMO"], ammo=["Arrow"]),
    cat("ammo.bolts", "Bolts", 100, formTypes=["AMMO"], ammo=["Bolt"]),
    # ---------------------------------------------------------------- alchemy
    cat("alchemy.potions", "Potions", 100, formTypes=["ALCH"], alchemy=["Potion"]),
    cat("alchemy.poisons", "Poisons", 100, formTypes=["ALCH"], alchemy=["Poison"]),
    cat("alchemy.food", "Food & drink", 100, formTypes=["ALCH"], alchemy=["Food"]),
    cat("alchemy.ingredients", "Ingredients", 100, formTypes=["INGR"]),
    # ---------------------------------------------------------------- books
    cat("books.spelltomes", "Spell tomes", 120, formTypes=["BOOK"], bookTeaches=["Spell"]),
    cat("books.skill", "Skill books", 120, formTypes=["BOOK"], bookTeaches=["Skill"]),
    # Vanilla letters carry VendorItemBook like real books, so this only catches recipes and untagged notes.
    cat("books.notes", "Notes & recipes", 110, formTypes=["BOOK"], bookTeaches=["None"], keywordsNone=["VendorItemBook"]),
    cat("books.other", "Books", 100, formTypes=["BOOK"]),
    # ---------------------------------------------------------------- magic
    cat("scrolls", "Scrolls", 100, formTypes=["SCRL"]),
    cat("soulgems.empty", "Empty soul gems", 100, formTypes=["SLGM"], soulGem=["Empty"]),
    cat("soulgems.filled", "Filled soul gems", 100, formTypes=["SLGM"], soulGem=["Filled"]),
    # ---------------------------------------------------------------- misc
    # Explicit lists win over keyword categories: Quarried Stone is tagged VendorItemOreIngot, Iron Fittings
    # VendorItemTool, Dwemer scrap VendorItemOreIngot, Glass/Straw/Clay/Firewood VendorItemClutter.
    cat("misc.building_materials", "Building materials", 210, formTypes=["MISC"], forms=BUILDING),
    cat("misc.dwemer", "Dwemer parts & curios", 200, formTypes=["MISC"], forms=DWEMER),
    cat("misc.tools", "Tools", 150, formTypes=["MISC"], keywordsAny=["VendorItemTool"]),
    cat("misc.gems", "Gems", 140, formTypes=["MISC"], keywordsAny=["VendorItemGem"]),
    cat("misc.ores_ingots", "Ores & ingots", 130, formTypes=["MISC"], keywordsAny=["VendorItemOreIngot"]),
    cat("misc.hides_pelts", "Hides & pelts", 130, formTypes=["MISC"], keywordsAny=["VendorItemAnimalHide"]),
    cat("misc.animal_parts", "Animal parts", 130, formTypes=["MISC"], keywordsAny=["VendorItemAnimalPart"]),
    # Catch-all for MISC: most vanilla tableware/clutter has no vendor keyword at all. Gold and lockpicks are
    # never moved (default keep rules), quest items are always skipped by the mover.
    cat("misc.clutter", "Clutter & other misc items", 10, formTypes=["MISC"]),
    # ---------------------------------------------------------------- keys
    cat("keys", "Keys", 100, formTypes=["KEYM"]),
]

doc = {"$schema": "dovastash-categories-1", "categories": CATS}
OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_text(jsonfmt.dumps(doc) + "\n", encoding="utf-8")
print(f"wrote {len(CATS)} categories to {OUT}")
