// Which categories a display can take (DESIGN §8.4 "Categories on displays", core/DisplayCategories.h): decided
// from what each category can match, for every kind and rack type, with the shipped default set, the optional
// OCF set and user categories; the lint; and the fill side never putting up an item a display does not hold.

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"

using namespace dova::core;
using namespace dova::test;

namespace
{
	std::string ReadFile(const std::filesystem::path& a_path)
	{
		std::ifstream in(a_path, std::ios::binary);
		REQUIRE(in.good());
		return std::string(std::istreambuf_iterator<char>(in), {});
	}

	// The shipped default set, with the optional OCF set next to it when asked.
	CategorySet Installed(bool a_withOcf = false)
	{
		TempDir dir;
		dir.Write("00_Default.json", ReadFile(std::filesystem::path(DOVA_DIST_DIR) / "Categories" / "00_Default.json"));
		if (a_withOcf) {
			dir.Write("50_OCF.json", ReadFile(std::filesystem::path(DOVA_OPTIONAL_DIR) / "OCF" / "Data" / "SKSE" / "Plugins" / "DovaStash" / "Categories" /
			                                  "50_OCF.json"));
		}
		Diagnostics diags;
		auto        set = CategorySet::Compile(LoadCategoryDirectory(dir.Path(), diags), {}, diags);
		REQUIRE(diags.Empty());
		return set;
	}

	DisplayFit Rack(RackType a_type, bool a_crossbows = false)
	{
		return DisplayFit{ DisplayKind::Rack, a_type, a_crossbows };
	}

	DisplayFit Kind(DisplayKind a_kind)
	{
		return DisplayFit{ a_kind, std::nullopt, true };
	}

	using Patterns = std::vector<std::string>;

	ItemInfo Armor(FormRef a_form, std::uint32_t a_slots, std::initializer_list<const char*> a_keywords = {})
	{
		auto item = MakeItem(std::move(a_form), ItemType::Armor);
		item.armorSlots = a_slots;
		for (const auto* k : a_keywords) {
			item.keywords.emplace_back(k);
		}
		return item;
	}

	DisplayStock Carried(ItemInfo a_item, std::vector<std::string> a_categories)
	{
		const auto n = a_item.count;
		DisplayStock s{ ContainerRef::Player(), std::move(a_item), n };
		s.categories = std::move(a_categories);
		return s;
	}

	DisplaySpot Spot(std::uint32_t a_id, DisplayKind a_kind, std::vector<std::string> a_categories, RackType a_rack = RackType::Any)
	{
		DisplaySpot s;
		s.ref = Sk(a_id);
		s.kind = a_kind;
		s.rack = a_rack;
		s.categories = std::move(a_categories);
		s.room = a_kind == DisplayKind::Bookshelf ? 10 : 0;
		return s;
	}
}

TEST_CASE("Display pickers offer only what the display holds", "[display][categories]")
{
	const auto set = Installed();
	const Patterns allWeapons{ "weapons.*", "weapons.swords", "weapons.greatswords", "weapons.daggers", "weapons.axes", "weapons.waraxes",
		"weapons.maces", "weapons.warhammers", "weapons.bows", "weapons.staves" };

	// A rack for any weapon (the weapon plaque): weapons, no shields; crossbows only with a crossbow node.
	CHECK(DisplayCategoryChoices(set, Rack(RackType::Any)) == allWeapons);
	auto withCrossbows = allWeapons;
	withCrossbows.insert(withCrossbows.begin() + 9, "weapons.crossbows");
	CHECK(DisplayCategoryChoices(set, Rack(RackType::Any, true)) == withCrossbows);
	CHECK(DisplayCategoryChoices(set, Rack(RackType::LargeCase, true)) == withCrossbows);
	// Shield racks: shields, and the armor types shields have (heavy or light), never jewelry or clothing.
	CHECK(DisplayCategoryChoices(set, Rack(RackType::Shield, true)) == Patterns{ "armor.*", "armor.shields", "armor.heavy", "armor.light" });
	CHECK(DisplayCategoryChoices(set, Rack(RackType::Dagger, true)) == Patterns{ "weapons.*", "weapons.daggers" });
	CHECK(DisplayCategoryChoices(set, Rack(RackType::NoDaggerOrBow, true)) ==
		  Patterns{ "weapons.*", "weapons.swords", "weapons.greatswords", "weapons.axes", "weapons.waraxes", "weapons.maces", "weapons.warhammers",
			  "weapons.staves" });
	// A rack whose type is unknown: anything some rack takes.
	const auto unknown = DisplayCategoryChoices(set, Kind(DisplayKind::Rack));
	CHECK(std::ranges::find(unknown, "weapons.crossbows") != unknown.end());
	CHECK(std::ranges::find(unknown, "armor.shields") != unknown.end());
	CHECK(std::ranges::find(unknown, "armor.jewelry") == unknown.end());

	// Mannequins: what they wear, circlets included (jewelry), never shields.
	CHECK(DisplayCategoryChoices(set, Kind(DisplayKind::Mannequin)) == Patterns{ "armor.*", "armor.jewelry", "armor.heavy", "armor.light", "armor.clothing" });
	CHECK(DisplayCategoryChoices(set, Kind(DisplayKind::Bookshelf)) == Patterns{ "books.*", "books.spelltomes", "books.skill", "books.notes", "books.other" });
	// Plaques and cases are made for their own items.
	CHECK(DisplayCategoryChoices(set, Kind(DisplayKind::Item)).empty());
	CHECK(DisplayCategoryChoices(set, Kind(DisplayKind::Case)).empty());
}

TEST_CASE("Soul gems, potions and the like go on no display", "[display][categories]")
{
	const auto set = Installed(true);
	const std::vector<DisplayFit> fits{ Rack(RackType::Any, true), Rack(RackType::Shield), Rack(RackType::NoDaggerOrBow), Rack(RackType::Dagger),
		Rack(RackType::LargeCase, true), Kind(DisplayKind::Rack), Kind(DisplayKind::Mannequin), Kind(DisplayKind::Bookshelf),
		Kind(DisplayKind::Item), Kind(DisplayKind::Case) };
	for (const auto& fit : fits) {
		INFO(DisplayFitNoun(fit));
		for (const auto* p : { "soulgems.*", "soulgems.filled", "soulgems.black", "alchemy.*", "alchemy.potions", "alchemy.poisons", "alchemy.food.raw",
				 "alchemy.ingredients", "ammo.*", "ammo.arrows", "scrolls", "misc.*", "misc.gems", "keys", "keys.homes" }) {
			INFO(p);
			CHECK(PatternFitsDisplay(set, p, fit) == false);
		}
	}
}

TEST_CASE("Display category patterns: roots, wildcards and unknown ones", "[display][categories]")
{
	const auto set = Installed();
	CHECK(PatternFitsDisplay(set, "*", Rack(RackType::Any)) == true);
	CHECK(PatternFitsDisplay(set, "*", Kind(DisplayKind::Item)) == false);
	CHECK(PatternFitsDisplay(set, "weapons.*", Rack(RackType::Dagger)) == true);  // daggers are weapons
	CHECK(PatternFitsDisplay(set, "weapons.swords", Rack(RackType::Dagger)) == false);
	CHECK(PatternFitsDisplay(set, "armor.*", Rack(RackType::Any)) == false);
	CHECK(PatternFitsDisplay(set, "armor.*", Rack(RackType::Shield)) == true);
	CHECK(PatternFitsDisplay(set, "armor.shields", Kind(DisplayKind::Mannequin)) == false);
	CHECK(PatternFitsDisplay(set, "books.*", Kind(DisplayKind::Mannequin)) == false);
	CHECK(PatternFitsDisplay(set, "weapons", Rack(RackType::Any)) == std::nullopt);  // a grouping id matches no item
	CHECK(PatternFitsDisplay(set, "mymod.*", Rack(RackType::Any)) == std::nullopt);  // unknown: no verdict
	CHECK(UnfitDisplayCategories(set, Patterns{ "weapons.bows", "soulgems.*", "mymod.*", "armor.heavy" }, Rack(RackType::Any)) ==
		  Patterns{ "soulgems.*", "armor.heavy" });
	CHECK(UnfitCategoryMessage(set, "soulgems.*", Rack(RackType::Any)) == "Soul gems can't go on a weapon rack");
	CHECK(UnfitCategoryMessage(set, "weapons.swords", Rack(RackType::Dagger)) == "Swords can't go on a dagger rack");
	CHECK(UnfitCategoryMessage(set, "armor.shields", Kind(DisplayKind::Mannequin)) == "Shields can't go on a mannequin");
}

TEST_CASE("Display categories are decided by what they match: OCF and user categories", "[display][categories]")
{
	const auto ocf = Installed(true);
	CHECK(PatternFitsDisplay(ocf, "weapons.polearms", Rack(RackType::Any)) == true);
	CHECK(PatternFitsDisplay(ocf, "armor.jewelry.circlets", Kind(DisplayKind::Mannequin)) == true);
	CHECK(PatternFitsDisplay(ocf, "armor.jewelry.rings", Kind(DisplayKind::Mannequin)) == false);
	CHECK(PatternFitsDisplay(ocf, "armor.jewelry.necklaces", Kind(DisplayKind::Mannequin)) == false);
	CHECK(PatternFitsDisplay(ocf, "armor.heavy.helmets", Kind(DisplayKind::Mannequin)) == true);
	CHECK(PatternFitsDisplay(ocf, "armor.heavy.helmets", Rack(RackType::Shield)) == false);
	CHECK(PatternFitsDisplay(ocf, "books.spelltomes.alteration", Kind(DisplayKind::Bookshelf)) == true);
	// The shield rack's picker with OCF: no helmets or jewelry.
	CHECK(DisplayCategoryChoices(ocf, Rack(RackType::Shield)) == Patterns{ "armor.*", "armor.shields", "armor.heavy", "armor.light" });

	const auto user = CompileCategories(R"({ "$schema": "dovastash-categories-1", "categories": [
		{ "id": "loot.artifacts", "name": "Artifacts", "match": { "keywordsAny": ["DaedricArtifact"] } },
		{ "id": "loot.blades", "name": "Blades", "match": { "formTypes": ["WEAP"], "keywordsAny": ["WeapTypeDagger"] } },
		{ "id": "loot.shields", "name": "Kite shields", "match": { "formTypes": ["ARMO"], "keywordsAny": ["OCF_EquipTypeShield"] } },
		{ "id": "loot.ebony", "name": "Ebony", "match": { "formTypes": ["WEAP", "ARMO"], "keywordsAny": ["ArmorMaterialEbony", "WeapMaterialEbony"] } },
		{ "id": "loot.fists", "name": "Fists", "match": { "weaponTypes": ["HandToHand"] } },
		{ "id": "loot.gems", "name": "Soul gems", "match": { "soulGem": ["Filled"] } }
	] })");
	// No type known: it could be anything, so it fits wherever something goes.
	CHECK(PatternFitsDisplay(user, "loot.artifacts", Rack(RackType::Any)) == true);
	CHECK(PatternFitsDisplay(user, "loot.artifacts", Kind(DisplayKind::Bookshelf)) == true);
	CHECK(PatternFitsDisplay(user, "loot.blades", Rack(RackType::Dagger)) == true);
	CHECK(PatternFitsDisplay(user, "loot.blades", Rack(RackType::NoDaggerOrBow)) == false);
	CHECK(PatternFitsDisplay(user, "loot.shields", Rack(RackType::Shield)) == true);
	CHECK(PatternFitsDisplay(user, "loot.shields", Kind(DisplayKind::Mannequin)) == false);
	CHECK(PatternFitsDisplay(user, "loot.ebony", Rack(RackType::Shield)) == true);  // ebony shields
	CHECK(PatternFitsDisplay(user, "loot.ebony", Kind(DisplayKind::Mannequin)) == true);
	CHECK(PatternFitsDisplay(user, "loot.fists", Rack(RackType::Any)) == false);  // no rack node holds them
	CHECK(PatternFitsDisplay(user, "loot.gems", Rack(RackType::Any)) == false);  // soulGem: soul gems only
	CHECK(DisplayCategoryChoices(user, Rack(RackType::Dagger)) == Patterns{ "loot.*", "loot.artifacts", "loot.blades", "loot.ebony" });
}

TEST_CASE("The lint names each category a display can't take, and keeps it", "[display][categories][template]")
{
	const auto set = Installed();
	Diagnostics diags;
	const auto  layout = ParseLayoutText(R"({ "$schema": "dovastash-layout-1", "cell": "Skyrim.esm|0x1", "displays": [
		{ "ref": "Skyrim.esm|0x00102D37", "label": "Weapon Plaque", "categories": ["soulgems.*"] },
		{ "ref": "Skyrim.esm|0x00000A02", "label": "Dagger rack",   "categories": ["weapons.daggers", "weapons.swords", "mymod.*"] },
		{ "ref": "Skyrim.esm|0x00000A03", "kind": "mannequin",      "categories": ["armor.shields", "armor.heavy"] },
		{ "ref": "Skyrim.esm|0x00000A04", "label": "Off",           "categories": ["soulgems.*"], "ignored": true },
		{ "ref": "Skyrim.esm|0x00000A05", "label": "Not found",     "categories": ["soulgems.*"] },
		{ "ref": "Skyrim.esm|0x00000A06", "label": "Mask wall",     "categories": ["weapons.*"] }
	] })",
		"t.json", diags);
	REQUIRE(layout);
	const auto fitOf = [](const DisplayBinding& a_d) -> std::optional<DisplayFit> {
		switch (a_d.ref.localId) {
		case 0x00102D37:
			return DisplayFit{ DisplayKind::Rack, RackType::Any, true };
		case 0xA02:
			return DisplayFit{ DisplayKind::Rack, RackType::Dagger, true };
		case 0xA06:
			return DisplayFit{ DisplayKind::Item, std::nullopt, true };
		default:
			return std::nullopt;
		}
	};
	Diagnostics lint;
	ValidateDisplayCategories(*layout, set, fitOf, "t.json", lint);
	std::vector<std::string> lines;
	for (const auto& d : lint.Items()) {
		lines.push_back(d.ToString());
	}
	CHECK(lines == std::vector<std::string>{
		"warning: t.json: display Skyrim.esm|0x00102D37 (Weapon Plaque): Soul gems can't go on a weapon rack; nothing goes on it",
		"warning: t.json: display Skyrim.esm|0x00000A02 (Dagger rack): Swords can't go on a dagger rack",
		"warning: t.json: display Skyrim.esm|0x00000A03: Shields can't go on a mannequin",  // its `kind`, no fit known
		"warning: t.json: display Skyrim.esm|0x00000A06 (Mask wall): \"categories\" are only used by racks, mannequins and bookshelves (ignored)",
	});
	// Nothing is removed from the data.
	CHECK(layout->displays[0].categories == Patterns{ "soulgems.*" });
	CHECK(SerializeLayout(*layout).find("\"categories\": [\"soulgems.*\"]") != std::string::npos);
}

TEST_CASE("A category a display can't take never puts anything on it", "[display][categories]")
{
	const auto sword = Carried(Weapon(Sk(0x10), WeaponType::OneHandSword), { "weapons.swords" });
	auto       gemItem = MakeItem(Sk(0x11), ItemType::SoulGem);
	gemItem.soulGemState = SoulGemState::Filled;
	const auto gem = Carried(gemItem, { "soulgems.filled" });
	auto       potionItem = Potion(Sk(0x12), 1, 50);
	const auto potion = Carried(potionItem, { "alchemy.potions" });
	const auto shield = Carried(Armor(Sk(0x13), 1u << 9, { "ArmorShield" }), { "armor.shields", "armor.heavy" });
	const auto cuirass = Carried(Armor(Sk(0x14), 1u << 2, { "ArmorCuirass" }), { "armor.heavy" });
	const auto ring = Carried(Armor(Sk(0x15), 1u << 6, { "ClothingRing" }), { "armor.jewelry" });
	auto       bookItem = MakeItem(Sk(0x16), ItemType::Book);
	const auto book = Carried(bookItem, { "books.other" });
	const std::vector<DisplayStock> stocks{ sword, gem, potion, shield, cuirass, ring, book };
	const auto placed = [&](const DisplaySpot& a_spot) {
		std::vector<FormRef> out;
		for (const auto& a : PlanDisplays(std::vector{ a_spot }, stocks, {})) {
			out.push_back(stocks[a.stock].item.form);
		}
		return out;
	};

	// The weapon plaque bound to soul gems: nothing goes on it (no soul gem, no sword).
	CHECK(placed(Spot(0xA1, DisplayKind::Rack, { "soulgems.*" })).empty());
	// Next to a category it can take, the other is ignored.
	CHECK(placed(Spot(0xA2, DisplayKind::Rack, { "soulgems.*", "weapons.swords" })) == std::vector{ Sk(0x10) });
	CHECK(placed(Spot(0xA3, DisplayKind::Rack, { "*" })) == std::vector{ Sk(0x10) });
	CHECK(placed(Spot(0xA4, DisplayKind::Rack, {}, RackType::Shield)) == std::vector{ Sk(0x13) });
	CHECK(placed(Spot(0xA5, DisplayKind::Rack, { "armor.heavy" })).empty());  // a weapon rack takes no armor
	CHECK(placed(Spot(0xA6, DisplayKind::Mannequin, { "armor.shields" })).empty());
	CHECK(placed(Spot(0xA7, DisplayKind::Mannequin, { "armor.jewelry" })).empty());  // a ring: no slot a mannequin shows
	CHECK(placed(Spot(0xA8, DisplayKind::Mannequin, { "alchemy.*", "*" })) == std::vector{ Sk(0x14) });
	CHECK(placed(Spot(0xA9, DisplayKind::Bookshelf, { "weapons.*" })).empty());
	CHECK(placed(Spot(0xAA, DisplayKind::Bookshelf, { "*" })) == std::vector{ Sk(0x16) });
}
