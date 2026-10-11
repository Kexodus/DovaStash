"""Generate the optional Object Categorization Framework (OCF) category set.

Outputs:
  dist/Optional/OCF/Data/SKSE/Plugins/DovaStash/Categories/50_OCF.json   the category file
  tests/data/ocf_keywords.txt                                         keyword list the C++ test checks against

Usage:
  python -I tools/datagen/gen_ocf_categories.py --ocf "E:\\modding\\...\\mods\\Object Categorization Framework"

The keyword list is read from OCF's KID files (*_KID.ini, active "Keyword = ..." lines only), so every
keyword used below is checked against what OCF really distributes, and to which record type. A typo or a
keyword OCF never puts on that record type fails the run.

Design rules (see docs/DESIGN.md §7.2):
- Every category is a NEW id below an existing default id or group (weapons.tools, armor.jewelry.rings...),
  never a redefinition of a default id. Without OCF the file is inert: every category needs an OCF_ keyword
  in keywordsAny, so nothing matches.
- A subcategory uses its parent's priority (the deeper id wins the tie) or slightly more. Exact bindings
  on the parent keep catching refined items, because the planner falls through to the next matching category.
"""
import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import jsonfmt  # noqa: E402

ROOT = HERE.parents[1]
DEFAULT = ROOT / "dist" / "Data" / "SKSE" / "Plugins" / "DovaStash" / "Categories" / "00_Default.json"
OUT = ROOT / "dist" / "Optional" / "OCF" / "Data" / "SKSE" / "Plugins" / "DovaStash" / "Categories" / "50_OCF.json"
KW_OUT = ROOT / "tests" / "data" / "ocf_keywords.txt"

# KID record type names -> record signatures (item types only).
KID_TYPES = {
    "Weapon": "WEAP", "Armor": "ARMO", "Ammo": "AMMO", "Potion": "ALCH", "Ingredient": "INGR", "Book": "BOOK",
    "Scroll": "SCRL", "Soul Gem": "SLGM", "Misc Item": "MISC", "Key": "KEYM", "Light": "LIGH",
}


def read_ocf_keywords(ocf_dir: Path):
    """{keyword: set(record signatures)} from every active `Keyword = Name|Type|...` line."""
    files = sorted(ocf_dir.glob("*_KID.ini"))
    if not files:
        sys.exit(f"no *_KID.ini files in {ocf_dir}")
    out = defaultdict(set)
    for f in files:
        for line in f.read_text(encoding="utf-8", errors="replace").splitlines():
            m = re.match(r"^\s*Keyword\s*=\s*([^|]+)\|([^|]*)", line)
            if m and m.group(2).strip() in KID_TYPES:
                out[m.group(1).strip()].add(KID_TYPES[m.group(2).strip()])
    return out


def cat(id_, name, priority, form_types, keywords, **extra):
    match = {"formTypes": form_types, "keywordsAny": keywords}
    match.update(extra)
    return {"id": id_, "name": name, "priority": priority, "match": match}


W, A, AL, IN, BK, MI = ["WEAP"], ["ARMO"], ["ALCH"], ["INGR"], ["BOOK"], ["MISC"]
EDIBLE = ["Food", "Potion"]  # never poisons


def armor_slots(parent, armor_type, names):
    slots = ["Head", "Body", "Hands", "Feet"]
    return [cat(f"{parent}.{sub}", label, 100, A, [f"OCF_ArmorType{slot}_Main"], armorTypes=[armor_type])
            for slot, (sub, label) in zip(slots, names)]


CATS = [
    # ---------------------------------------------------------------- weapons (defaults: 100, warhammers 110)
    cat("weapons.tools", "Tools (pickaxes, wood axes...)", 115, W, ["OCF_CanMineOre", "OCF_CanChopWood", "OCF_Tool"]),
    cat("weapons.throwing", "Throwing weapons", 114, W, ["OCF_WeapThrowable"]),
    cat("weapons.polearms", "Spears & polearms", 112, W, ["OCF_WeapPole1H", "OCF_WeapPole2H"]),
    # ---------------------------------------------------------------- armor (jewelry 120, others 100)
    # Clothing only: armored circlets and bracer-like gauntlets stay with heavy/light armor.
    cat("armor.jewelry.rings", "Rings", 120, A, ["OCF_HandTypeRing"], armorTypes=["Clothing"]),
    cat("armor.jewelry.necklaces", "Amulets & necklaces", 120, A,
        ["OCF_BodyTypeAmulet", "OCF_BodyTypeTorc", "OCF_BodyTypeHighNecklace"], armorTypes=["Clothing"]),
    cat("armor.jewelry.circlets", "Circlets & crowns", 120, A, ["OCF_HeadTypeCirclet", "OCF_HeadTypeCrown"], armorTypes=["Clothing"]),
    cat("armor.jewelry.accessories", "Earrings, bracelets & brooches", 120, A,
        ["OCF_HeadTypeEarrings", "OCF_HeadTypeEarCuffs", "OCF_HandTypeBracelet", "OCF_AccessoryJewelry", "OCF_AccessoryPiercing"],
        armorTypes=["Clothing"]),
    *armor_slots("armor.heavy", "Heavy", [("helmets", "Heavy helmets"), ("cuirasses", "Heavy cuirasses"),
                                          ("gauntlets", "Heavy gauntlets"), ("boots", "Heavy boots")]),
    *armor_slots("armor.light", "Light", [("helmets", "Light helmets"), ("cuirasses", "Light cuirasses"),
                                          ("gauntlets", "Light gauntlets"), ("boots", "Light boots")]),
    *armor_slots("armor.clothing", "Clothing", [("headwear", "Hats & hoods"), ("outfits", "Outfits & robes"),
                                                ("gloves", "Gloves"), ("footwear", "Shoes")]),
    # ---------------------------------------------------------------- alchemy (defaults: 100)
    cat("alchemy.potions.drugs", "Skooma & drugs", 105, AL, ["OCF_AlchDrug"], alchemy=EDIBLE),
    cat("alchemy.food.raw", "Raw meat, fish & eggs", 104, AL,
        ["OCF_AlchFood_MeatRaw", "OCF_AlchFood_FishRaw", "OCF_AlchFood_SeafoodRaw", "OCF_AlchFood_EggRaw"], alchemy=EDIBLE),
    cat("alchemy.food.alcohol", "Alcohol", 103, AL, ["OCF_AlchDrinkAlcohol"], alchemy=EDIBLE),
    cat("alchemy.food.drinks", "Drinks", 102, AL, ["OCF_AlchDrink", "OCF_AlchDrinkSoft"], alchemy=EDIBLE),
    cat("alchemy.food.cooking", "Cooking ingredients", 102, AL, ["OCF_AlchFood_Ingredient"], alchemy=EDIBLE),
    cat("alchemy.food.produce", "Produce & cheese", 101, AL,
        ["OCF_AlchFood_Fruit", "OCF_AlchFood_Vegetable", "OCF_AlchFood_Cheese"], alchemy=EDIBLE),
    cat("alchemy.food.baked", "Bread, pastries & sweets", 101, AL,
        ["OCF_AlchFood_Baked", "OCF_AlchFood_Bread", "OCF_AlchFood_Treat"], alchemy=EDIBLE),
    cat("alchemy.food.meals", "Meals & cooked food", 101, AL,
        ["OCF_AlchFood_Meal", "OCF_AlchFood_Stew", "OCF_AlchFood_Sandwich", "OCF_AlchFood_Meat", "OCF_AlchFood_Fish",
         "OCF_AlchFood_Seafood", "OCF_AlchFood_Egg"], alchemy=EDIBLE),
    cat("alchemy.ingredients.mushrooms", "Mushrooms & fungi", 103, IN, ["OCF_IngrPlant_Mushroom"]),
    cat("alchemy.ingredients.flowers", "Flowers", 103, IN, ["OCF_IngrPlant_Flower", "OCF_IngrPlant_FlowerDeathbell"]),
    cat("alchemy.ingredients.insects", "Insects", 103, IN,
        ["OCF_IngrRemains_Bug", "OCF_IngrRemains_BugBee", "OCF_IngrRemains_BugButterfly", "OCF_IngrRemains_BugDragonfly",
         "OCF_IngrRemains_BugFirefly"]),
    cat("alchemy.ingredients.plants", "Plants & herbs", 101, IN,
        ["OCF_IngrPlant", "OCF_IngrPlant_Berry", "OCF_IngrPlant_Leaf", "OCF_IngrPlant_Root", "OCF_IngrPlant_Wood",
         "OCF_IngrPlant_MoonSugar"]),
    cat("alchemy.ingredients.creature_parts", "Creature parts", 101, IN, ["OCF_IngrRemains", "OCF_AlchBlood"]),
    cat("alchemy.ingredients.minerals", "Minerals, salts & oils", 101, IN, ["OCF_IngrMisc", "OCF_IngrMisc_Mineral", "OCF_IngrMisc_Oil"]),
    # ---------------------------------------------------------------- books (spell tomes / skill 120, notes 110, other 100)
    *[cat(f"books.spelltomes.{school.lower()}", f"{school} spell tomes", 120, BK, [f"OCF_BookSpellTome_{school}"])
      for school in ["Alteration", "Conjuration", "Destruction", "Illusion", "Restoration"]],
    cat("books.spelltomes.other", "Other spell tomes", 120, BK, ["OCF_BookSpellTome_Other", "OCF_BookSpellTome_Shehai"]),
    cat("books.notes.recipes", "Recipes & formulas", 110, BK,
        ["OCF_BookTextRecipe_Alchemy", "OCF_BookTextRecipe_Cooking", "OCF_BookTextRecipe_Crafting",
         "OCF_BookTextRecipe_Daedric", "OCF_BookTextRecipe_Spider"]),
    cat("books.notes.journals", "Journals & diaries", 110, BK, ["OCF_BookTextJournal"]),
    cat("books.notes.letters", "Letters & documents", 110, BK, ["OCF_BookTextMissive", "OCF_BookTextRansom", "OCF_BookTextDocument"]),
    cat("books.other.manuals", "Guides & manuals", 100, BK, ["OCF_BookTextManual"]),
    # ---------------------------------------------------------------- soul gems / keys (defaults: 100)
    cat("soulgems.black", "Black soul gems", 105, ["SLGM"], ["OCF_SlgmColorBlack"]),
    cat("keys.homes", "Player home keys", 100, ["KEYM"], ["OCF_KeyPlayerHome"]),
    # ---------------------------------------------------------------- misc (building 210, dwemer 200, tools 150,
    #                                                                  gems 140, ores/hides/parts 130, clutter 10)
    cat("misc.dwemer.scrap", "Dwemer scrap", 200, MI, ["OCF_MiscMaterial_ScrapDwarven"]),
    cat("misc.dwemer.tableware", "Dwemer tableware", 200, MI, ["OCF_MiscTableware_Dwarven", "OCF_MiscPottery_Dwarven"]),
    cat("misc.tools.alchemy", "Alchemy apparatus", 151, MI, ["OCF_ToolAlchemy"]),
    cat("misc.tools.exploration", "Exploration gear", 151, MI,
        ["OCF_ToolCompass", "OCF_ToolSextant", "OCF_ToolSpyglass", "OCF_ToolParaglider", "OCF_ToolLantern"]),
    cat("misc.tools.camping", "Camping gear", 151, MI, ["OCF_MiscCampingGear"]),
    cat("misc.tools.general", "Hand tools", 150, MI, ["OCF_Tool"]),
    cat("misc.ores_ingots.ingots", "Ingots", 130, MI, ["OCF_MiscMaterial_Ingot"]),
    cat("misc.ores_ingots.ores", "Ores", 130, MI, ["OCF_MiscMaterial_Ore"]),
    cat("misc.hides_pelts.leather", "Leather", 130, MI, ["OCF_AnimalHideLeather", "OCF_AnimalHideNetch"]),
    cat("misc.hides_pelts.strips", "Leather strips & sinew", 130, MI, ["OCF_AnimalHideStrip"]),
    cat("misc.animal_parts.dragon", "Dragon bones & scales", 130, MI, ["OCF_AnimalPartDragon"]),
    # Clutter refinements sit just above the misc.clutter catch-all (10), below every other misc category.
    cat("misc.clutter.valuables", "Valuables", 12, MI,
        ["OCF_MiscTreasure", "OCF_MiscTreasure_Gold", "OCF_MiscTreasure_Silver", "OCF_MiscTreasure_Coinpurse",
         "OCF_MiscTableware_Gold", "OCF_MiscTableware_Silver", "OCF_MiscPottery_Silver"],
        keywordsNone=["OCF_MiscTreasure_Coin"]),  # loose septims stay plain clutter (and are kept by default)
    cat("misc.clutter.tableware", "Tableware & pottery", 11, MI,
        ["OCF_MiscTableware", "OCF_MiscTableware_Glazed", "OCF_MiscTableware_GlazedNordic", "OCF_MiscPottery"]),
    cat("misc.clutter.empty_vessels", "Empty bottles & jars", 11, MI,
        ["OCF_MiscEmptyVessel_Bottle", "OCF_MiscEmptyVessel_Flask", "OCF_MiscEmptyVessel_Jar", "OCF_MiscEmptyVessel_Jug"]),
    cat("misc.clutter.instruments", "Instruments", 11, MI, ["OCF_MiscInstrument"]),
    cat("misc.clutter.relics", "Relics & artifacts", 11, MI, ["OCF_Relic", "OCF_Artifact", "OCF_Replica"]),
    cat("misc.clutter.fossils", "Fossils", 11, MI, ["OCF_MiscFossil"]),
    cat("misc.clutter.crafting", "Crafting supplies", 11, MI, ["OCF_MiscMaterialGeneral", "OCF_MiscDye"]),
]


def validate(keywords, defaults):
    default_ids = {c["id"] for c in defaults}
    groups = {i.split(".")[0] for i in default_ids}
    seen = set()
    for c in CATS:
        cid, m = c["id"], c["match"]
        assert cid not in seen, f"{cid}: duplicate id"
        seen.add(cid)
        assert cid not in default_ids, f"{cid}: redefines a default category"
        parent = cid.rsplit(".", 1)[0]
        assert parent in default_ids or parent in groups, f"{cid}: parent {parent} is not a default category or group"
        assert m["keywordsAny"] and all(k.startswith("OCF_") for k in m["keywordsAny"]), f"{cid}: must be gated on OCF_ keywords"
        for k in m["keywordsAny"] + m.get("keywordsNone", []):
            assert k in keywords, f"{cid}: {k} is not distributed by OCF"
            assert keywords[k] & set(m["formTypes"]), f"{cid}: OCF never puts {k} on {m['formTypes']} (only {sorted(keywords[k])})"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ocf", required=True, type=Path, help="OCF mod folder containing the *_KID.ini files")
    ap.add_argument("--ocf-version", default="6.1", help="OCF version recorded in the outputs (default 6.1)")
    args = ap.parse_args()

    keywords = read_ocf_keywords(args.ocf)
    defaults = json.loads(DEFAULT.read_text(encoding="utf-8"))["categories"]
    validate(keywords, defaults)

    doc = {
        "$schema": "dovastash-categories-1",
        "description": f"Optional refinements for Object Categorization Framework {args.ocf_version} (Nexus SSE 81469). "
                       "Every category requires an OCF_ keyword, so without OCF nothing here matches. "
                       "Generated by tools/datagen/gen_ocf_categories.py.",
        "categories": CATS,
    }
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(jsonfmt.dumps(doc) + "\n", encoding="utf-8")

    KW_OUT.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        f"# Item keywords distributed by Object Categorization Framework {args.ocf_version} (active Keyword= lines of its",
        "# *_KID.ini files), with the record types each one is distributed to. Generated by",
        "# tools/datagen/gen_ocf_categories.py; tests/OcfCategoryTests.cpp checks 50_OCF.json against it.",
    ]
    lines += [f"{k}\t{','.join(sorted(v))}" for k, v in sorted(keywords.items(), key=lambda kv: kv[0].lower())]
    KW_OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")

    print(f"wrote {len(CATS)} categories to {OUT}")
    print(f"wrote {len(keywords)} keywords to {KW_OUT}")


if __name__ == "__main__":
    main()
