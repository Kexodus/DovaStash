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


def cat(id_, name, priority, icon, description=None, **match):
    out = {"id": id_, "name": name}
    if description:
        out["description"] = description
    out.update({"priority": priority, "icon": icon, "match": match})
    return out


def group(id_, name, icon):
    """A parent node (no match): its name and icon for the panel; `weapons.*` covers its children."""
    return {"id": id_, "name": name, "icon": icon}


def skyui(label):
    return {"source": "skyui/icons_category_psychosteve.swf", "label": label}


W = ["WEAP"]
CATS = [
    # ---------------------------------------------------------------- weapons
    group("weapons", "Weapons", skyui("inv_weapons")),
    cat("weapons.swords", "Swords", 100, "weapon_sword", formTypes=W, weaponTypes=["OneHandSword"]),
    cat("weapons.greatswords", "Greatswords", 100, "weapon_greatsword", formTypes=W, weaponTypes=["TwoHandSword"]),
    cat("weapons.daggers", "Daggers", 100, "weapon_dagger", formTypes=W, weaponTypes=["OneHandDagger"]),
    # Battleaxes and warhammers share the TwoHandAxe engine type; the WeapTypeWarhammer keyword splits them.
    cat("weapons.axes", "Battleaxes", 100, "weapon_battleaxe", "Two-handed battleaxes; war axes are weapons.waraxes.",
        formTypes=W, weaponTypes=["TwoHandAxe"], keywordsNone=["WeapTypeWarhammer"]),
    cat("weapons.waraxes", "War axes", 100, "weapon_waraxe", formTypes=W, weaponTypes=["OneHandAxe"]),
    cat("weapons.maces", "Maces", 100, "weapon_mace", formTypes=W, weaponTypes=["OneHandMace"]),
    cat("weapons.warhammers", "Warhammers", 110, "weapon_hammer", formTypes=W, weaponTypes=["TwoHandAxe"], keywordsAny=["WeapTypeWarhammer"]),
    cat("weapons.bows", "Bows", 100, "weapon_bow", formTypes=W, weaponTypes=["Bow"]),
    cat("weapons.crossbows", "Crossbows", 100, "weapon_crossbow", formTypes=W, weaponTypes=["Crossbow"]),
    cat("weapons.staves", "Staves", 100, "weapon_staff", formTypes=W, weaponTypes=["Staff"]),
    # ---------------------------------------------------------------- armor
    group("armor", "Armor", skyui("inv_armor")),
    cat("armor.shields", "Shields", 120, "armor_shield", formTypes=["ARMO"], keywordsAny=["ArmorShield"]),
    cat("armor.jewelry", "Jewelry", 120, "armor_amulet", formTypes=["ARMO"], keywordsAny=["ArmorJewelry", "VendorItemJewelry"]),
    cat("armor.heavy", "Heavy armor", 100, "armor_body", formTypes=["ARMO"], armorTypes=["Heavy"]),
    cat("armor.light", "Light armor", 100, "lightarmor_body", formTypes=["ARMO"], armorTypes=["Light"]),
    cat("armor.clothing", "Clothing", 100, "clothing_body", formTypes=["ARMO"], armorTypes=["Clothing"]),
    # ---------------------------------------------------------------- ammo
    group("ammo", "Ammo", "weapon_arrow"),
    cat("ammo.arrows", "Arrows", 100, "weapon_arrow", formTypes=["AMMO"], ammo=["Arrow"]),
    cat("ammo.bolts", "Bolts", 100, "weapon_bolt", formTypes=["AMMO"], ammo=["Bolt"]),
    # ---------------------------------------------------------------- alchemy
    group("alchemy", "Alchemy", skyui("inv_potions")),
    cat("alchemy.potions", "Potions", 100, "potion_health", formTypes=["ALCH"], alchemy=["Potion"]),
    cat("alchemy.poisons", "Poisons", 100, "potion_poison", formTypes=["ALCH"], alchemy=["Poison"]),
    cat("alchemy.food", "Food & drink", 100, "default_food", formTypes=["ALCH"], alchemy=["Food"]),
    cat("alchemy.ingredients", "Ingredients", 100, "default_ingredient", formTypes=["INGR"]),
    # ---------------------------------------------------------------- books
    group("books", "Books", skyui("inv_books")),
    cat("books.spelltomes", "Spell tomes", 120, "book_tome", formTypes=["BOOK"], bookTeaches=["Spell"]),
    cat("books.skill", "Skill books", 120, "default_book", formTypes=["BOOK"], bookTeaches=["Skill"]),
    # Vanilla letters carry VendorItemBook like real books, so this only catches recipes and untagged notes.
    cat("books.notes", "Notes & recipes", 110, "book_note", formTypes=["BOOK"], bookTeaches=["None"], keywordsNone=["VendorItemBook"]),
    cat("books.other", "Other books", 100, "default_book", formTypes=["BOOK"]),
    # ---------------------------------------------------------------- magic
    cat("scrolls", "Scrolls", 100, "default_scroll", formTypes=["SCRL"]),
    group("soulgems", "Soul gems", "misc_soulgem"),
    cat("soulgems.empty", "Empty soul gems", 100, "soulgem_empty", formTypes=["SLGM"], soulGem=["Empty"]),
    cat("soulgems.filled", "Filled soul gems", 100, "soulgem_full", formTypes=["SLGM"], soulGem=["Filled"]),
    # ---------------------------------------------------------------- misc
    group("misc", "Miscellaneous", skyui("inv_misc")),
    # Explicit lists win over keyword categories: Quarried Stone is tagged VendorItemOreIngot, Iron Fittings
    # VendorItemTool, Dwemer scrap VendorItemOreIngot, Glass/Straw/Clay/Firewood VendorItemClutter.
    cat("misc.building_materials", "Building materials", 210, "misc_wood", formTypes=["MISC"], forms=BUILDING),
    cat("misc.dwemer", "Dwemer parts & curios", 200, "misc_clutter", formTypes=["MISC"], forms=DWEMER),
    # Creation Club curios the game makes potions (ALCH): kept out of alchemy.potions, so Restock never hands
    # them out: Rare Curios' Welkynd Stone (ccBGSSSE037-Curios.esl) and the drinkable one of ccBGSSSE067-DaedInv.esm
    # (form refs written out: this script reads the vanilla masters only).
    cat("misc.curios", "Curios", 220, "misc_gem", "Rare finds the game counts as potions (Welkynd Stone).",
        formTypes=["ALCH"], forms=["ccBGSSSE037-Curios.esl|0x00000800", "ccBGSSSE067-DaedInv.esm|0x0020E802"]),
    cat("misc.tools", "Tools", 150, "misc_clutter", formTypes=["MISC"], keywordsAny=["VendorItemTool"]),
    # Carried lights (LIGH): torches, and the lanterns some mods add.
    cat("misc.lights", "Torches", 100, "misc_torch", formTypes=["LIGH"]),
    cat("misc.gems", "Gems", 140, "misc_gem", formTypes=["MISC"], keywordsAny=["VendorItemGem"]),
    cat("misc.ores_ingots", "Ores & ingots", 130, "misc_ingot", formTypes=["MISC"], keywordsAny=["VendorItemOreIngot"]),
    cat("misc.hides_pelts", "Hides & pelts", 130, "misc_hide", formTypes=["MISC"], keywordsAny=["VendorItemAnimalHide"]),
    cat("misc.animal_parts", "Animal parts", 130, "misc_remains", formTypes=["MISC"], keywordsAny=["VendorItemAnimalPart"]),
    # Catch-all for MISC: most vanilla tableware/clutter has no vendor keyword at all. Gold and lockpicks are
    # never moved (default keep rules), quest items are always skipped by the mover.
    cat("misc.clutter", "Clutter & other misc items", 10, "misc_clutter", formTypes=["MISC"]),
    # ---------------------------------------------------------------- keys
    cat("keys", "Keys", 100, "default_key", formTypes=["KEYM"]),
]

doc = {"$schema": "dovastash-categories-1", "categories": CATS}
OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_text(jsonfmt.dumps(doc) + "\n", encoding="utf-8")
print(f"wrote {len(CATS)} categories to {OUT}")
