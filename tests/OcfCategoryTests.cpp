// The optional Object Categorization Framework category set (dist/Optional/OCF/.../50_OCF.json, DESIGN §7.2).
//
// - loads next to 00_Default.json with no errors and no warnings (in particular: no default id overridden);
// - every keyword it uses is one OCF 6.1 really distributes, to the record types the category matches
//   (tests/data/ocf_keywords.txt, generated from OCF's KID files by tools/datagen/gen_ocf_categories.py);
// - without OCF keywords it changes nothing: every item classifies exactly as with the defaults alone;
// - with OCF keywords items land in the subcategories, and a layout that binds the default parent id
//   exactly still receives them.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "dova/core/Enums.h"

using namespace dova::core;
using namespace dova::test;

#if !defined(DOVA_DIST_DIR) || !defined(DOVA_OPTIONAL_DIR) || !defined(DOVA_TEST_DATA_DIR)
#	error "DOVA_DIST_DIR, DOVA_OPTIONAL_DIR and DOVA_TEST_DATA_DIR must be defined by tests/CMakeLists.txt"
#endif

namespace
{
	const std::filesystem::path kDefaultFile = std::filesystem::path(DOVA_DIST_DIR) / "Categories" / "00_Default.json";
	const std::filesystem::path kOcfFile = std::filesystem::path(DOVA_OPTIONAL_DIR) / "OCF" / "Data" / "SKSE" / "Plugins" / "DovaStash" / "Categories" / "50_OCF.json";
	const std::filesystem::path kOcfKeywords = std::filesystem::path(DOVA_TEST_DATA_DIR) / "ocf_keywords.txt";

	std::string ReadFile(const std::filesystem::path& a_path)
	{
		std::ifstream in(a_path, std::ios::binary);
		REQUIRE(in.good());
		return std::string(std::istreambuf_iterator<char>(in), {});
	}

	void ReportAll(const Diagnostics& a_diags)
	{
		for (const auto& d : a_diags.Items()) {
			UNSCOPED_INFO(d.ToString());
		}
	}

	// keyword (lower-cased) -> record signatures OCF distributes it to
	std::map<std::string, std::set<std::string>> LoadOcfKeywords()
	{
		std::map<std::string, std::set<std::string>> out;
		std::istringstream                            in(ReadFile(kOcfKeywords));
		for (std::string line; std::getline(in, line);) {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			if (line.empty() || line.front() == '#') {
				continue;
			}
			const auto tab = line.find('\t');
			REQUIRE(tab != std::string::npos);
			auto& types = out[ToLower(line.substr(0, tab))];
			std::istringstream sigs(line.substr(tab + 1));
			for (std::string sig; std::getline(sigs, sig, ',');) {
				types.insert(sig);
			}
		}
		return out;
	}

	// What the plugin loads when the optional file is installed: both files in one Categories folder.
	CategorySet LoadInstalled(bool a_withOcf, Diagnostics& a_diags)
	{
		TempDir dir;
		dir.Write("00_Default.json", ReadFile(kDefaultFile));
		if (a_withOcf) {
			dir.Write("50_OCF.json", ReadFile(kOcfFile));
		}
		return CategorySet::Compile(LoadCategoryDirectory(dir.Path(), a_diags), {}, a_diags);
	}

	CategorySet LoadInstalled(bool a_withOcf)
	{
		Diagnostics diags;
		auto        set = LoadInstalled(a_withOcf, diags);
		ReportAll(diags);
		REQUIRE(diags.Empty());
		return set;
	}

	ItemInfo With(ItemInfo a_item, std::initializer_list<const char*> a_keywords)
	{
		for (const auto* k : a_keywords) {
			a_item.keywords.emplace_back(k);
		}
		return a_item;
	}

	ItemInfo Armor(ArmorType a_type, std::initializer_list<const char*> a_keywords = {})
	{
		auto item = MakeItem(Sk(0x1000), ItemType::Armor);
		item.armorType = a_type;
		return With(std::move(item), a_keywords);
	}

	ItemInfo Alch(AlchemyKind a_kind, std::initializer_list<const char*> a_keywords = {})
	{
		auto item = MakeItem(Sk(0x2000), ItemType::Alchemy);
		item.alchemyKind = a_kind;
		return With(std::move(item), a_keywords);
	}

	ItemInfo Book(BookTeaches a_teaches, std::initializer_list<const char*> a_keywords = {})
	{
		auto item = MakeItem(Sk(0x3000), ItemType::Book);
		item.bookTeaches = a_teaches;
		return With(std::move(item), a_keywords);
	}

	ItemInfo Of(ItemType a_type, std::initializer_list<const char*> a_keywords = {})
	{
		return With(MakeItem(Sk(0x4000), a_type), a_keywords);
	}

	// Representative vanilla items, carrying only vanilla keywords.
	std::vector<ItemInfo> VanillaItems()
	{
		std::vector<ItemInfo> items;
		for (const auto wt : { WeaponType::OneHandSword, WeaponType::OneHandDagger, WeaponType::OneHandAxe, WeaponType::OneHandMace,
				 WeaponType::TwoHandSword, WeaponType::TwoHandAxe, WeaponType::Bow, WeaponType::Staff, WeaponType::Crossbow }) {
			items.push_back(Weapon(Sk(0x5000), wt));
		}
		items.push_back(With(Weapon(Sk(0x5001), WeaponType::TwoHandAxe), { "WeapTypeWarhammer" }));
		items.push_back(With(Weapon(Sk(0x5002), WeaponType::OneHandAxe), { "WeapTypeWarAxe", "VendorItemTool" }));  // pickaxe
		for (const auto at : { ArmorType::Heavy, ArmorType::Light, ArmorType::Clothing }) {
			items.push_back(Armor(at, { "ArmorHelmet" }));
			items.push_back(Armor(at, { "ArmorCuirass" }));
			items.push_back(Armor(at, { "ArmorShield" }));
		}
		items.push_back(Armor(ArmorType::Clothing, { "ArmorJewelry", "ClothingRing" }));
		items.push_back(Armor(ArmorType::Clothing, { "VendorItemJewelry", "ClothingNecklace" }));
		items.push_back(Armor(ArmorType::Clothing, { "ClothingCirclet", "VendorItemJewelry" }));
		for (const auto ak : { AlchemyKind::Potion, AlchemyKind::Poison, AlchemyKind::Food }) {
			items.push_back(Alch(ak));
		}
		items.push_back(Alch(AlchemyKind::Food, { "VendorItemFood", "VendorItemFoodRaw" }));
		items.push_back(Of(ItemType::Ingredient, { "VendorItemIngredient" }));
		for (const auto bt : { BookTeaches::Spell, BookTeaches::Skill, BookTeaches::None }) {
			items.push_back(Book(bt));
			items.push_back(Book(bt, { "VendorItemBook" }));
		}
		items.push_back(Book(BookTeaches::None, { "VendorItemRecipe" }));
		items.push_back(Of(ItemType::Scroll));
		for (const auto sg : { SoulGemState::Empty, SoulGemState::Filled }) {
			auto gem = Of(ItemType::SoulGem);
			gem.soulGemState = sg;
			items.push_back(gem);
		}
		for (const char* kw : { "VendorItemTool", "VendorItemGem", "VendorItemOreIngot", "VendorItemAnimalHide", "VendorItemAnimalPart",
				 "VendorItemClutter" }) {
			items.push_back(Of(ItemType::Misc, { kw }));
		}
		items.push_back(Of(ItemType::Misc));
		items.push_back(MakeItem(Sk(0x000C886A), ItemType::Misc));  // a Dwemer part from the explicit list
		items.push_back(MakeItem(F("HearthFires.esm|0x0000300E"), ItemType::Misc));  // a building material
		items.push_back(Of(ItemType::Key));
		{
			auto arrow = Of(ItemType::Ammo);
			arrow.ammoKind = AmmoKind::Arrow;
			items.push_back(arrow);
		}
		return items;
	}

	FormRef Bound(const CategorySet& a_categories, const ItemInfo& a_item, std::initializer_list<std::pair<FormRef, const char*>> a_bindings)
	{
		Layout layout;
		layout.cell = Sk(0x165A7);
		layout.master = Sk(0xA000);
		for (const auto& [ref, pattern] : a_bindings) {
			layout.containers.push_back({ .ref = ref, .categories = { pattern } });
		}
		const auto dest = SortPlanner(layout, a_categories).ResolveDestination(a_item);
		REQUIRE(dest.has_value());
		return dest->container;
	}
}

TEST_CASE("OCF category set loads next to the defaults with no errors or warnings", "[dist][ocf]")
{
	Diagnostics fileDiags;
	const auto  ocf = ParseCategoryText(ReadFile(kOcfFile), "50_OCF.json", fileDiags);
	ReportAll(fileDiags);
	CHECK(fileDiags.Empty());
	CHECK(ocf.size() >= 30);
	CHECK(ocf.size() <= 80);

	Diagnostics defaultDiags;
	const auto  defaults = ParseCategoryText(ReadFile(kDefaultFile), "00_Default.json", defaultDiags);
	REQUIRE_FALSE(defaultDiags.HasErrors());

	// Every OCF id is new (an override would change behaviour for non-OCF users) and hangs below a default
	// category or group, so "weapons.*"-style bindings keep covering it.
	std::set<std::string> defaultIds;
	std::set<std::string> groups;
	for (const auto& d : defaults) {
		defaultIds.insert(d.id);
		groups.insert(d.id.substr(0, d.id.find('.')));
	}
	for (const auto& c : ocf) {
		INFO(c.id);
		CHECK_FALSE(defaultIds.contains(c.id));
		const auto parent = c.id.substr(0, c.id.rfind('.'));
		CHECK(parent != c.id);
		CHECK((defaultIds.contains(parent) || groups.contains(parent)));
	}

	const auto installed = LoadInstalled(true);
	CHECK(installed.Size() == defaults.size() + ocf.size());
}

TEST_CASE("OCF categories only use keywords OCF 6.1 distributes to the matched record types", "[dist][ocf]")
{
	const auto known = LoadOcfKeywords();
	REQUIRE(known.size() > 500);

	Diagnostics diags;
	const auto  ocf = ParseCategoryText(ReadFile(kOcfFile), "50_OCF.json", diags);
	REQUIRE_FALSE(diags.HasErrors());
	REQUIRE_FALSE(ocf.empty());

	for (const auto& c : ocf) {
		INFO(c.id);
		const auto& m = c.match;
		// Gate: at least one OCF keyword is required, so nothing matches without OCF installed.
		REQUIRE(m.keywordsAny.has_value());
		REQUIRE_FALSE(m.keywordsAny->empty());
		REQUIRE(m.formTypes.has_value());

		std::set<std::string> sigs;
		for (const auto t : *m.formTypes) {
			sigs.insert(std::string(ToString(t)));
		}
		std::vector<std::string> used = *m.keywordsAny;
		for (const auto& list : { m.keywordsAll, m.keywordsNone }) {
			if (list) {
				used.insert(used.end(), list->begin(), list->end());
			}
		}
		for (const auto& kw : *m.keywordsAny) {
			INFO(kw);
			CHECK(kw.starts_with("OCF_"));
		}
		for (const auto& kw : used) {
			INFO(kw);
			const auto it = known.find(ToLower(kw));
			REQUIRE(it != known.end());
			CHECK(std::ranges::any_of(sigs, [&](const std::string& s) { return it->second.contains(s); }));
		}
	}
}

TEST_CASE("Without OCF keywords the OCF set changes no classification", "[dist][ocf]")
{
	const auto defaults = LoadInstalled(false);
	const auto withOcf = LoadInstalled(true);
	for (const auto& item : VanillaItems()) {
		INFO(std::string(ToString(item.type)) << " " << item.form.ToString() << " keywords=" << item.keywords.size());
		CHECK(withOcf.Classify(item) == defaults.Classify(item));
		CHECK(withOcf.MatchAll(item) == defaults.MatchAll(item));
	}
}

TEST_CASE("OCF keywords refine items into subcategories", "[dist][ocf]")
{
	const auto c = LoadInstalled(true);

	CHECK(c.Classify(Armor(ArmorType::Clothing, { "ArmorJewelry", "ClothingRing", "OCF_HandTypeRing" })) == "armor.jewelry.rings");
	CHECK(c.Classify(Armor(ArmorType::Clothing, { "VendorItemJewelry", "OCF_BodyTypeAmulet" })) == "armor.jewelry.necklaces");
	// An armored circlet is still armor.
	CHECK(c.Classify(Armor(ArmorType::Heavy, { "ArmorHelmet", "OCF_HeadTypeCirclet", "OCF_ArmorTypeHead_Main" })) == "armor.heavy.helmets");
	CHECK(c.Classify(Armor(ArmorType::Light, { "ArmorCuirass", "OCF_ArmorTypeBody_Main" })) == "armor.light.cuirasses");
	CHECK(c.Classify(Armor(ArmorType::Clothing, { "ClothingFeet", "OCF_ArmorTypeFeet_Main" })) == "armor.clothing.footwear");
	CHECK(c.Classify(Armor(ArmorType::Heavy, { "ArmorShield", "OCF_ArmorShield" })) == "armor.shields");

	CHECK(c.Classify(With(Weapon(Sk(0x5002), WeaponType::OneHandAxe), { "VendorItemTool", "OCF_WeapTypePickaxe1H", "OCF_CanMineOre" })) == "weapons.tools");
	CHECK(c.Classify(With(Weapon(Sk(0x5003), WeaponType::TwoHandAxe), { "OCF_WeapPole2H", "OCF_WeapTypeHalberd2H" })) == "weapons.polearms");

	CHECK(c.Classify(Alch(AlchemyKind::Food, { "VendorItemFoodRaw", "OCF_AlchFood_Meat", "OCF_AlchFood_MeatRaw" })) == "alchemy.food.raw");
	CHECK(c.Classify(Alch(AlchemyKind::Food, { "OCF_AlchFood_Meat" })) == "alchemy.food.meals");
	CHECK(c.Classify(Alch(AlchemyKind::Food, { "OCF_AlchDrink", "OCF_AlchDrinkAlcohol" })) == "alchemy.food.alcohol");
	CHECK(c.Classify(Alch(AlchemyKind::Potion, { "OCF_AlchDrug", "OCF_AlchDrugSkooma" })) == "alchemy.potions.drugs");
	CHECK(c.Classify(Alch(AlchemyKind::Poison, { "OCF_AlchDrink" })) == "alchemy.poisons");  // never a drink

	CHECK(c.Classify(Of(ItemType::Ingredient, { "OCF_IngrPlant", "OCF_IngrPlant_Mushroom" })) == "alchemy.ingredients.mushrooms");
	CHECK(c.Classify(Of(ItemType::Ingredient, { "OCF_IngrRemains", "OCF_IngrRemains_Bug" })) == "alchemy.ingredients.insects");
	CHECK(c.Classify(Of(ItemType::Ingredient, { "OCF_IngrRemains", "OCF_IngrRemains_Fat" })) == "alchemy.ingredients.creature_parts");

	CHECK(c.Classify(Book(BookTeaches::Spell, { "VendorItemSpellTome", "OCF_BookSpellTome_Destruction" })) == "books.spelltomes.destruction");
	CHECK(c.Classify(Book(BookTeaches::None, { "VendorItemRecipe", "OCF_BookTextRecipe_Alchemy" })) == "books.notes.recipes");
	CHECK(c.Classify(Book(BookTeaches::Skill, { "VendorItemBook", "OCF_BookTextSkill", "OCF_BookTextJournal" })) == "books.skill");

	CHECK(c.Classify(Of(ItemType::Misc, { "VendorItemOreIngot", "OCF_MiscMaterial_Ingot" })) == "misc.ores_ingots.ingots");
	CHECK(c.Classify(Of(ItemType::Misc, { "VendorItemOreIngot", "OCF_MiscMaterial_Ore" })) == "misc.ores_ingots.ores");
	CHECK(c.Classify(Of(ItemType::Misc, { "VendorItemAnimalHide", "OCF_AnimalHideLeather" })) == "misc.hides_pelts.leather");
	CHECK(c.Classify(Of(ItemType::Misc, { "VendorItemOreIngot", "OCF_MiscMaterial_ScrapDwarven" })) == "misc.dwemer.scrap");
	CHECK(c.Classify(Of(ItemType::Misc, { "VendorItemTool", "OCF_Tool", "OCF_ToolAlchemy" })) == "misc.tools.alchemy");
	CHECK(c.Classify(Of(ItemType::Misc, { "VendorItemTool", "OCF_Tool" })) == "misc.tools.general");
	CHECK(c.Classify(Of(ItemType::Misc, { "VendorItemGem", "OCF_MiscTreasure" })) == "misc.gems");  // gems keep their own box
	CHECK(c.Classify(Of(ItemType::Misc, { "OCF_MiscTreasure", "OCF_MiscTreasure_Silver" })) == "misc.clutter.valuables");
	CHECK(c.Classify(Of(ItemType::Misc, { "OCF_MiscTreasure", "OCF_MiscTreasure_Coin" })) == "misc.clutter");  // septims
	CHECK(c.Classify(Of(ItemType::Misc, { "OCF_MiscTableware" })) == "misc.clutter.tableware");

	CHECK(c.Classify(Of(ItemType::Key, { "OCF_KeyPlayerHome" })) == "keys.homes");
}

TEST_CASE("Layouts binding the default parent id still receive OCF-refined items", "[dist][ocf][planner]")
{
	const auto    c = LoadInstalled(true);
	const FormRef jewelryBox = Sk(0xB000);
	const FormRef ringBox = Sk(0xB001);
	const FormRef wardrobe = Sk(0xB002);
	const FormRef pantry = Sk(0xB003);

	const auto ring = Armor(ArmorType::Clothing, { "ArmorJewelry", "ClothingRing", "OCF_HandTypeRing" });
	// Shipped templates bind exact default ids ("armor.jewelry"): the planner falls through to it.
	CHECK(Bound(c, ring, { { jewelryBox, "armor.jewelry" } }) == jewelryBox);
	CHECK(Bound(c, ring, { { jewelryBox, "armor.*" } }) == jewelryBox);
	CHECK(Bound(c, ring, { { jewelryBox, "armor.jewelry" }, { ringBox, "armor.jewelry.rings" } }) == ringBox);

	// A refined item that does not match the parent goes where it went without OCF.
	const auto bracelet = Armor(ArmorType::Clothing, { "OCF_HandTypeBracelet" });
	CHECK(c.Classify(bracelet) == "armor.jewelry.accessories");
	CHECK(Bound(c, bracelet, { { jewelryBox, "armor.jewelry" }, { wardrobe, "armor.clothing" } }) == wardrobe);

	const auto mead = Alch(AlchemyKind::Food, { "OCF_AlchDrink", "OCF_AlchDrinkAlcohol" });
	CHECK(Bound(c, mead, { { pantry, "alchemy.food" } }) == pantry);
}
