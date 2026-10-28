#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <format>
#include <optional>
#include <set>

#include "TestHelpers.h"
#include "dova/core/DraftSuggest.h"
#include "dova/core/Preview.h"

using namespace dova::core;
using namespace dova::test;

namespace
{
	DraftObject Container(std::string a_id, std::string a_edid, std::string a_name, std::array<float, 3> a_at, std::array<float, 3> a_size = { 60, 40, 40 })
	{
		DraftObject o;
		o.kind = DraftObject::Kind::Container;
		o.id = std::move(a_id);
		o.cell = "Skyrim.esm|0x00000100";
		o.editorId = std::move(a_edid);
		o.name = std::move(a_name);
		o.center = a_at;
		o.size = a_size;
		return o;
	}

	DraftObject Thing(DraftObject::Kind a_kind, std::string a_edid, std::string a_name, std::array<float, 3> a_at, std::vector<std::string> a_keywords = {})
	{
		DraftObject o;
		o.kind = a_kind;
		o.id = "Skyrim.esm|0x00000FFF";
		o.cell = "Skyrim.esm|0x00000100";
		o.editorId = std::move(a_edid);
		o.name = std::move(a_name);
		o.center = a_at;
		o.size = { 50, 50, 50 };
		o.keywords = std::move(a_keywords);
		return o;
	}

	const DraftSuggestion* Find(const DraftPlan& a_plan, std::size_t a_object, std::string_view a_pattern)
	{
		const auto* c = a_plan.Find(a_object);
		if (!c) {
			return nullptr;
		}
		for (const auto& s : c->suggestions) {
			if (s.pattern == a_pattern) {
				return &s;
			}
		}
		return nullptr;
	}
}

TEST_CASE("Draft tokens split EditorIDs and names into words", "[draft]")
{
	CHECK(DraftTokens("PlayerHouseChestAlchemy01") == std::vector<std::string>{ "player", "house", "chest", "alchemy" });
	CHECK(DraftTokens("DLCChestSmall") == std::vector<std::string>{ "dlc", "chest", "small" });
	CHECK(DraftTokens("Apothecary's Satchel") == std::vector<std::string>{ "apothecary", "satchel" });
	CHECK(DraftTokens("food_barrel-2") == std::vector<std::string>{ "food", "barrel" });

	const auto t = DraftTokens("WeaponsChest Soulgems");
	CHECK(DraftWordsMatch(t, std::vector<std::string>{ "weapon" }));  // plural
	CHECK(DraftWordsMatch(t, std::vector<std::string>{ "soul", "gem" }));  // glued in one token
	CHECK(DraftWordsMatch(t, std::vector<std::string>{ "soulgem*" }));
	CHECK_FALSE(DraftWordsMatch(t, std::vector<std::string>{ "weap" }));
	CHECK_FALSE(DraftWordsMatch(DraftTokens("Armory"), std::vector<std::string>{ "armor" }));  // whole words only
}

TEST_CASE("Default draft rules parse without diagnostics", "[draft]")
{
	Diagnostics d;
	const auto  r = ParseDraftRules(DefaultDraftRulesText(), "default", d);
	INFO((d.Empty() ? std::string() : d.Items().front().ToString()));
	REQUIRE(r);
	CHECK(d.Empty());
	CHECK(r->names.size() > 20);
	CHECK(r->stations.size() >= 8);
	CHECK(r->ignore.size() >= 4);
}

TEST_CASE("Draft rules report malformed entries and keep the rest", "[draft]")
{
	Diagnostics d;
	const auto  r = ParseDraftRules(R"({
		"$schema": "dovastash-draftrules-1",
		"names": [
			{ "words": ["food"], "categories": ["alchemy.food"], "confidence": 0.8 },
			{ "words": ["x"], "categories": ["Not A*Pattern"] },
			{ "categories": ["keys"] }
		],
		"stations": [ { "label": "forge", "categories": ["misc.ores_ingots"] } ],
		"bogus": 1
	})",
		"test", d);
	REQUIRE(r);
	CHECK(r->names.size() == 1);
	CHECK(r->stations.empty());
	CHECK(d.HasErrors());
	CHECK_FALSE(ParseDraftRules(R"({"$schema": "something-else"})", "test", d));
}

TEST_CASE("Draft ignores respawning, upgrade, owned and scripted-bookshelf containers with a note", "[draft]")
{
	std::vector<DraftObject> objects{
		Container("Skyrim.esm|0x00000A01", "BarrelFood01", "Barrel", { 0, 0, 0 }),
		Container("Skyrim.esm|0x00000A02", "PlayerBookShelfContainer", "Book Shelf", { 100, 0, 0 }),
		Container("Skyrim.esm|0x00000A03", "BYOHChest", "Chest", { 200, 0, 0 }),
		Container("Skyrim.esm|0x00000A04", "BYOHChest", "Chest", { 300, 0, 0 }),
		Container("Skyrim.esm|0x00000A05", "TavernChest", "Chest", { 400, 0, 0 }),
		Container("Skyrim.esm|0x00000A06", "PlayerHouseChest", "Chest", { 500, 0, 0 }),
	};
	objects[0].respawns = true;
	objects[2].removedByUpgrade = true;
	objects[3].removedByUpgrade = true;
	objects[3].scripts = { "BYOHDivertItemScript" };  // hands its items on: safe
	objects[4].ownedByOther = true;
	const auto plan = SuggestDraft(objects, DefaultDraftRules());
	REQUIRE(plan.containers.size() == 6);
	CHECK(plan.containers[0].ignored);
	CHECK_THAT(plan.containers[0].note, Catch::Matchers::ContainsSubstring("Respawns"));
	CHECK(plan.containers[1].ignored);
	CHECK_THAT(plan.containers[1].note, Catch::Matchers::ContainsSubstring("bookshelf"));
	CHECK(plan.containers[2].ignored);
	CHECK_FALSE(plan.containers[3].ignored);
	CHECK(plan.containers[4].ignored);
	CHECK_FALSE(plan.containers[5].ignored);
	// Ignored ones and the scripted one are never the master.
	REQUIRE(plan.master);
	CHECK(*plan.master == 5);
}

TEST_CASE("Draft master: the largest usable chest near the entrance, with the reason", "[draft]")
{
	std::vector<DraftObject> objects{
		Container("Skyrim.esm|0x00000B01", "Sack01", "Sack", { 0, 0, 0 }, { 120, 80, 60 }),            // big but a sack
		Container("Skyrim.esm|0x00000B02", "PlayerHouseChest", "Chest", { 900, 0, 0 }, { 80, 50, 50 }),
		Container("Skyrim.esm|0x00000B03", "PlayerHouseChest", "Chest", { 100, 50, 0 }, { 80, 50, 50 }),  // same size, by the door
		Container("Skyrim.esm|0x00000B04", "EndTable01", "End Table", { 50, 0, 0 }, { 40, 40, 40 }),
		Container("Skyrim.esm|0x00000B05", "LargeChest", "Large Chest", { 300, 0, 0 }, { 140, 80, 70 }),
	};
	objects[4].scripts = { "SomeQuestScript" };  // scripted: never the master
	DraftObject door = Thing(DraftObject::Kind::Door, "WhiterunHouseDoor", "Door", { 0, 0, 0 });
	door.exteriorDoor = true;
	door.leadsTo = "Whiterun";
	objects.push_back(door);
	const auto plan = SuggestDraft(objects, DefaultDraftRules());
	REQUIRE(plan.master);
	CHECK(*plan.master == 2);
	CHECK_THAT(plan.masterReason, Catch::Matchers::ContainsSubstring("named like a chest"));
	CHECK_THAT(plan.masterReason, Catch::Matchers::ContainsSubstring("1.1 x 0.7 x 0.7 m, "));  // one unit: metres, as the door distance
	CHECK_THAT(plan.masterReason, Catch::Matchers::ContainsSubstring(" m from the door to Whiterun"));
	CHECK_FALSE(plan.masterReason.contains("units"));
	CHECK_THAT(plan.masterReason, Catch::Matchers::ContainsSubstring("; one of 4 that can be the master: "));
	CHECK_FALSE(plan.masterRunnersUp.empty());

	// One candidate: no count.
	const std::vector<DraftObject> single{ objects[2], door };
	const auto                     alone = SuggestDraft(single, DefaultDraftRules());
	REQUIRE(alone.master);
	CHECK_THAT(alone.masterReason, Catch::Matchers::ContainsSubstring("; the only container there from the start that can be the master: "));
	CHECK_FALSE(alone.masterReason.contains("one of"));
	// The master gets no suggestions of its own.
	CHECK(plan.Find(2)->suggestions.empty());
}

TEST_CASE("Draft master prefers the main cell", "[draft]")
{
	std::vector<DraftObject> objects{
		Container("Skyrim.esm|0x00000C01", "PlayerHouseChest", "Chest", { 0, 0, 0 }, { 200, 100, 100 }),
		Container("Skyrim.esm|0x00000C02", "PlayerHouseChest", "Chest", { 0, 0, 0 }, { 60, 40, 40 }),
	};
	objects[0].cell = "Skyrim.esm|0x00000200";  // the cellar
	DraftOptions options;
	options.mainCell = "Skyrim.esm|0x00000100";
	const auto plan = SuggestDraft(objects, DefaultDraftRules(), options);
	REQUIRE(plan.master);
	CHECK(*plan.master == 1);
}

TEST_CASE("Draft suggests categories from names, stations and nearby items", "[draft]")
{
	std::vector<DraftObject> objects{
		Container("Skyrim.esm|0x00000D01", "PlayerHouseChest", "Chest", { 0, 0, 0 }, { 150, 80, 80 }),  // master
		Container("Skyrim.esm|0x00000D02", "AlchemySatchel", "Apothecary's Satchel", { 2000, 0, 0 }),
		Container("Skyrim.esm|0x00000D03", "EndTable01", "End Table", { 500, 100, 0 }),                // next to the enchanter
		Container("Skyrim.esm|0x00000D04", "Cupboard01", "Cupboard", { 1000, 1000, 0 }),               // food on it
		Container("Skyrim.esm|0x00000D05", "Dresser01", "Dresser", { 3000, 3000, 0 }),
	};
	objects.push_back(Thing(DraftObject::Kind::Other, "EnchantingWorkbench", "Arcane Enchanter", { 560, 100, 0 }, { "WICraftingEnchanting" }));
	for (int i = 0; i < 3; ++i) {
		auto bread = Thing(DraftObject::Kind::Item, "FoodBread", "Bread", { 1000.0f + static_cast<float>(i) * 10.0f, 1000, 40 });
		bread.categories = { "alchemy.food" };
		objects.push_back(bread);
	}
	auto plate = Thing(DraftObject::Kind::Item, "BasicPlate01", "Plate", { 1000, 1000, 40 });
	plate.categories = { "misc.clutter" };  // says nothing: skipped
	objects.push_back(plate);

	const auto plan = SuggestDraft(objects, DefaultDraftRules());
	REQUIRE(plan.master);
	CHECK(*plan.master == 0);

	const auto* alch = Find(plan, 1, "alchemy.ingredients");
	REQUIRE(alch);
	CHECK(alch->accepted);
	CHECK(alch->source == DraftSource::Name);
	CHECK_THAT(alch->reason, Catch::Matchers::ContainsSubstring("Apothecary"));

	const auto* gems = Find(plan, 2, "soulgems.*");
	REQUIRE(gems);
	CHECK(gems->accepted);
	CHECK(gems->source == DraftSource::Station);
	CHECK_THAT(gems->reason, Catch::Matchers::ContainsSubstring("enchanter"));

	const auto* food = Find(plan, 3, "alchemy.food");
	REQUIRE(food);
	CHECK(food->accepted);  // the name hint and three loaves on it add up
	CHECK_THAT(food->reason, Catch::Matchers::ContainsSubstring("3 items"));
	CHECK_FALSE(Find(plan, 3, "misc.clutter"));

	// A dresser alone is a hint below the bar; nobody else has clothes, so the fill pass gives them to it.
	const auto* clothes = Find(plan, 4, "armor.clothing");
	REQUIRE(clothes);
	CHECK(clothes->accepted);
	CHECK(clothes->source == DraftSource::Fill);
	CHECK_THAT(clothes->reason, Catch::Matchers::ContainsSubstring("best free place for clothes"));

	// Without fill groups the hint stays a rejected alternative.
	auto rules = DefaultDraftRules();
	rules.fill.clear();
	const auto noFill = SuggestDraft(objects, rules);
	const auto* hint = Find(noFill, 4, "armor.clothing");
	REQUIRE(hint);
	CHECK_FALSE(hint->accepted);
}

TEST_CASE("Draft fill only uses free containers and skips groups already given", "[draft]")
{
	std::vector<DraftObject> objects{
		Container("Skyrim.esm|0x00001101", "PlayerHouseChest", "Chest", { 0, 0, 0 }, { 150, 80, 80 }),
		Container("Skyrim.esm|0x00001102", "WardrobeClothes", "Clothes Wardrobe", { 500, 0, 0 }),  // clothes by name
		Container("Skyrim.esm|0x00001103", "Dresser01", "Dresser", { 900, 0, 0 }),                 // a weaker clothes hint
		Container("Skyrim.esm|0x00001104", "EndTable01", "End Table", { 1300, 0, 0 }),             // a books hint
	};
	const auto plan = SuggestDraft(objects, DefaultDraftRules());
	CHECK(Find(plan, 1, "armor.clothing")->accepted);
	CHECK_FALSE(Find(plan, 2, "armor.clothing")->accepted);  // clothes are taken
	const auto* books = Find(plan, 3, "books.*");
	REQUIRE(books);
	CHECK(books->accepted);
	CHECK(books->source == DraftSource::Fill);
}

TEST_CASE("Draft: a container named after a category or an item gets it, and is no master", "[draft]")
{
	std::vector<DraftObject> objects{
		Container("Skyrim.esm|0x00001201", "StorageChest", "Soul Gems", { 0, 0, 0 }, { 200, 100, 100 }),  // biggest, but named
		Container("Skyrim.esm|0x00001202", "StorageChest", "Chest", { 300, 0, 0 }, { 150, 80, 80 }),
		Container("Skyrim.esm|0x00001203", "StorageSack", "Ores & Ingots", { 600, 0, 0 }),
		Container("Skyrim.esm|0x00001204", "StorageSack", "Ash Hopper Jelly", { 900, 0, 0 }),
		Container("Skyrim.esm|0x00001205", "StorageSack", "Chest of Drawers", { 1200, 0, 0 }),
	};
	DraftOptions options;
	options.categoryNames = { { "soulgems.*", "Soul gems" }, { "misc.ores_ingots", "Ores & ingots" }, { "misc.dwemer", "Dwemer parts & curios" } };
	options.itemCategoryByName = [](std::string_view a_name) -> std::optional<std::string> {
		return IEquals(a_name, "Ash Hopper Jelly") ? std::optional<std::string>("alchemy.ingredients") : std::nullopt;
	};
	const auto plan = SuggestDraft(objects, DefaultDraftRules(), options);
	REQUIRE(plan.master);
	CHECK(*plan.master == 1);
	CHECK(Find(plan, 0, "soulgems.*")->accepted);
	CHECK(Find(plan, 2, "misc.ores_ingots")->accepted);
	const auto* jelly = Find(plan, 3, "alchemy.ingredients");
	REQUIRE(jelly);
	CHECK(jelly->accepted);
	CHECK_THAT(jelly->reason, Catch::Matchers::ContainsSubstring("named after the item"));
	CHECK_FALSE(Find(plan, 4, "misc.dwemer"));
}

TEST_CASE("Draft master: a name that says what it holds is no master, whatever its EditorID or size", "[draft]")
{
	std::vector<DraftObject> objects{
		Container("Skyrim.esm|0x00001401", "DruidStorageChest01", "Dragonpriest Masks", { 100, 0, 0 }, { 80, 40, 60 }),  // by the door, "chest" in the EditorID
		Container("Skyrim.esm|0x00001402", "ShelfFoodChest", "Food & Drinks", { 400, 0, 0 }, { 556, 95, 95 }),            // huge
		Container("Skyrim.esm|0x00001403", "Urn01", "Urn", { 150, 0, 0 }, { 67, 67, 85 }),
		Container("Skyrim.esm|0x00001404", "PlayerHouseChest", "Chest", { 900, 0, 0 }, { 64, 33, 33 }),                  // small, far
	};
	DraftObject door = Thing(DraftObject::Kind::Door, "HouseDoor", "Door", { 0, 0, 0 });
	door.exteriorDoor = true;
	objects.push_back(door);
	DraftOptions options;
	options.categoryNames = { { "alchemy.food", "Food & drink" } };
	const auto plan = SuggestDraft(objects, DefaultDraftRules(), options);
	REQUIRE(plan.master);
	CHECK(*plan.master == 3);
	CHECK_THAT(plan.masterReason, Catch::Matchers::ContainsSubstring("named like a chest"));
	// The masks have a name the rules cannot read: nothing is guessed for them, not even from what stands nearby.
	CHECK(plan.Find(0)->suggestions.empty());
	CHECK(Find(plan, 1, "alchemy.food")->accepted);

	// Only named containers usable: the master is one of them, and the reason says so.
	std::vector<DraftObject> named{ objects[0], objects[1], door };
	const auto onlyNamed = SuggestDraft(named, DefaultDraftRules(), options);
	REQUIRE(onlyNamed.master);
	CHECK_THAT(onlyNamed.masterReason, Catch::Matchers::ContainsSubstring("every usable container's does"));
}

TEST_CASE("Draft: decoration and a mannequin across the room give no container armor", "[draft]")
{
	std::vector<DraftObject> objects{
		Container("Skyrim.esm|0x00001501", "PlayerHouseChest", "Chest", { 0, 0, 0 }, { 150, 80, 80 }),
		Container("Skyrim.esm|0x00001502", "Cabinet01", "Cabinet", { 1000, 0, 0 }),   // shields hang next to it
		Container("Skyrim.esm|0x00001503", "Cupboard01", "Cupboard", { 2000, 0, 0 }),  // a mannequin 3 m away
		Container("Skyrim.esm|0x00001504", "Wardrobe01", "Wardrobe", { 3000, 0, 0 }),  // weapons lying on it
	};
	for (int i = 0; i < 3; ++i) {
		auto shield = Thing(DraftObject::Kind::Item, "ArmorIronShield", "Iron Shield", { 1000.0f + static_cast<float>(i) * 20.0f, 30, 60 });
		shield.categories = { "armor.shields" };
		objects.push_back(shield);
		auto sword = Thing(DraftObject::Kind::Item, "IronSword", "Iron Sword", { 3000.0f + static_cast<float>(i) * 20.0f, 0, 40 });
		sword.categories = { "weapons.swords" };
		objects.push_back(sword);
	}
	objects.push_back(Thing(DraftObject::Kind::Display, "PlayerHouseMannequin", "Mannequin", { 2210, 0, 0 }, { "display:mannequin" }));
	const auto plan = SuggestDraft(objects, DefaultDraftRules());
	for (const std::size_t c : { 1u, 2u, 3u }) {
		for (const auto& s : plan.Find(c)->suggestions) {
			INFO(s.pattern << ": " << s.reason);
			CHECK_FALSE((s.accepted && (s.pattern.starts_with("armor.") ? s.pattern != "armor.clothing" : s.pattern.starts_with("weapons."))));
		}
	}
	CHECK(Find(plan, 3, "armor.clothing")->accepted);  // the wardrobe's own job
}

TEST_CASE("Draft fill gives a group's categories on their own evidence only", "[draft]")
{
	std::vector<DraftObject> objects{
		Container("Skyrim.esm|0x00001601", "PlayerHouseChest", "Chest", { 0, 0, 0 }, { 150, 80, 80 }),
		Container("Skyrim.esm|0x00001602", "CrateIngots", "Ingots", { 1000, 0, 0 }),
		Container("Skyrim.esm|0x00001603", "Barrel01", "Smithing Gear", { 1100, 0, 0 }),  // ores by name: taken by "Ingots"
	};
	objects.push_back(Thing(DraftObject::Kind::Other, "Forge", "Blacksmith Forge", { 1050, 60, 0 }, { "CraftingSmithingForge" }));
	DraftOptions options;
	options.categoryNames = { { "misc.ores_ingots", "Ores & ingots" }, { "misc.animal_parts", "Animal parts" } };
	const auto plan = SuggestDraft(objects, DefaultDraftRules(), options);
	CHECK(Find(plan, 1, "misc.ores_ingots")->accepted);
	CHECK_FALSE(Find(plan, 2, "misc.ores_ingots")->accepted);
	CHECK_FALSE(Find(plan, 2, "misc.animal_parts"));  // "crafting materials" is not given on the ores evidence
}

TEST_CASE("Draft: a container named for its contents hears nothing about other categories", "[draft]")
{
	std::vector<DraftObject> objects{
		Container("Skyrim.esm|0x00001701", "PlayerHouseChest", "Chest", { 0, 0, 0 }, { 150, 80, 80 }),
		Container("Skyrim.esm|0x00001702", "Crate01", "Arrows & Bolts", { 1000, 0, 0 }),
	};
	objects.push_back(Thing(DraftObject::Kind::Other, "CraftingCookpot", "Cooking Pot", { 1060, 0, 0 }, { "CraftingCookpot" }));
	objects.push_back(Thing(DraftObject::Kind::Other, "AlchemyLab", "Alchemy Lab", { 1000, 80, 0 }, { "WICraftingAlchemy" }));
	const auto plan = SuggestDraft(objects, DefaultDraftRules());
	const auto& s = plan.Find(1)->suggestions;
	REQUIRE(s.size() == 1);
	CHECK(s.front().pattern == "ammo.*");
	CHECK(s.front().accepted);
}

TEST_CASE("Draft: the display name outranks a hint, a group outranks an EditorID", "[draft]")
{
	std::vector<DraftObject> objects{
		Container("Skyrim.esm|0x00001801", "PlayerHouseChest", "Chest", { 0, 0, 0 }, { 150, 80, 80 }),
		Container("Skyrim.esm|0x00001802", "Satchel01", "Satchel", { 1000, 0, 0 }),                         // a hint, by the lab
		Container("Skyrim.esm|0x00001803", "Basket01", "Ingredient Basket", { 3000, 0, 0 }),                // named for them, far away
		Container("Skyrim.esm|0x00001804", "BarrelIngredientCommon01", "Barrel", { 1050, 50, 0 }),         // the EditorID says so, by the lab
		Container("Skyrim.esm|0x00001805", "Basket01", "Cooking Ingredients", { 5000, 0, 0 }),             // food, not alchemy
	};
	objects.push_back(Thing(DraftObject::Kind::Other, "AlchemyLab", "Alchemy Lab", { 1020, 20, 0 }, { "WICraftingAlchemy" }));
	const auto plan = SuggestDraft(objects, DefaultDraftRules());
	CHECK(Find(plan, 2, "alchemy.ingredients")->accepted);
	CHECK_FALSE(Find(plan, 1, "alchemy.ingredients")->accepted);
	const auto* barrel = Find(plan, 3, "alchemy.ingredients");
	REQUIRE(barrel);
	CHECK_FALSE(barrel->accepted);
	CHECK_THAT(barrel->reason, Catch::Matchers::ContainsSubstring("EditorID BarrelIngredientCommon01"));
	CHECK_FALSE(Find(plan, 4, "alchemy.ingredients"));
	CHECK(Find(plan, 4, "alchemy.food")->accepted);
}

TEST_CASE("Draft: a generic name is no item's name", "[draft]")
{
	std::vector<DraftObject> objects{
		Container("Skyrim.esm|0x00001901", "PlayerHouseChest", "Chest", { 0, 0, 0 }, { 150, 80, 80 }),
		Container("Skyrim.esm|0x00001902", "Crate01", "Storage", { 500, 0, 0 }),
		Container("Skyrim.esm|0x00001903", "Basket01", "Garlic Basket", { 900, 0, 0 }),
	};
	DraftOptions options;
	options.itemCategoryByName = [](std::string_view a_name) -> std::optional<std::string> {
		if (IEquals(a_name, "Storage")) {
			return "books.other";  // some mod's book
		}
		return IEquals(a_name, "garlic") ? std::optional<std::string>("alchemy.ingredients") : std::nullopt;
	};
	const auto plan = SuggestDraft(objects, DefaultDraftRules(), options);
	CHECK_FALSE(Find(plan, 1, "books.other"));
	const auto* garlic = Find(plan, 2, "alchemy.ingredients");
	REQUIRE(garlic);
	CHECK(garlic->accepted);
	CHECK_THAT(garlic->reason, Catch::Matchers::ContainsSubstring("named after the item \"garlic\""));
}

TEST_CASE("DraftCategoryNames lists leaves by id and groups as wildcards", "[draft]")
{
	const auto categories = CompileCategories(kStandardCategories);
	const auto names = DraftCategoryNames(categories);
	CHECK(std::ranges::find(names, std::pair<std::string, std::string>{ "weapons.*", "Weapons" }) != names.end());
}

TEST_CASE("Draft gives each category to one container, the others keep it rejected", "[draft]")
{
	std::vector<DraftObject> objects{
		Container("Skyrim.esm|0x00000E01", "PlayerHouseChest", "Chest", { 0, 0, 0 }, { 150, 80, 80 }),
		Container("Skyrim.esm|0x00000E02", "Satchel01", "Satchel", { 2000, 0, 0 }),
		Container("Skyrim.esm|0x00000E03", "Satchel01", "Satchel", { 3000, 0, 0 }),
	};
	objects.push_back(Thing(DraftObject::Kind::Other, "AlchemyLab", "Alchemy Lab", { 2950, 0, 0 }, { "WICraftingAlchemy" }));
	const auto plan = SuggestDraft(objects, DefaultDraftRules());
	const auto* far = Find(plan, 1, "alchemy.ingredients");
	const auto* near = Find(plan, 2, "alchemy.ingredients");
	REQUIRE(far);
	REQUIRE(near);
	CHECK(near->accepted);
	CHECK_FALSE(far->accepted);
	CHECK(far->lostTo == "Skyrim.esm|0x00000E03");
}

TEST_CASE("Draft: a container named for one category beats one that has it as part of a group", "[draft]")
{
	std::vector<DraftObject> objects{
		Container("Skyrim.esm|0x00001301", "PlayerHouseChest", "Chest", { 0, 0, 0 }, { 150, 80, 80 }),
		Container("Skyrim.esm|0x00001302", "BasketPotion", "Potion Basket", { 2000, 0, 0 }),  // potions + poisons, next to the lab
		Container("Skyrim.esm|0x00001303", "CratePoison", "Poison Crate", { 4000, 0, 0 }),    // poisons only
	};
	objects.push_back(Thing(DraftObject::Kind::Other, "AlchemyLab", "Alchemy Lab", { 2050, 0, 0 }, { "WICraftingAlchemy" }));
	const auto plan = SuggestDraft(objects, DefaultDraftRules());
	const auto* basket = Find(plan, 1, "alchemy.poisons");
	const auto* crate = Find(plan, 2, "alchemy.poisons");
	REQUIRE(basket);
	REQUIRE(crate);
	CHECK(basket->confidence > crate->confidence);  // more evidence...
	CHECK(crate->accepted);                         // ...but the crate is named for poisons
	CHECK_FALSE(basket->accepted);
	CHECK(Find(plan, 1, "alchemy.potions")->accepted);
}

TEST_CASE("Draft: on a tie the container whose name is only about the category wins", "[draft]")
{
	std::vector<DraftObject> objects{
		Container("Skyrim.esm|0x00001401", "PlayerHouseChest", "Chest", { 0, 0, 0 }, { 150, 80, 80 }),
		Container("Skyrim.esm|0x00001402", "PlayerHouseSatchel", "Alchemy Recipes", { 500, 0, 0 }),  // earlier, also alchemy
		Container("Skyrim.esm|0x00001403", "PlayerHouseSatchel", "Notes and Letters", { 1000, 0, 0 }),
	};
	const auto  plan = SuggestDraft(objects, DefaultDraftRules());
	const auto* recipes = Find(plan, 1, "books.notes");
	const auto* notes = Find(plan, 2, "books.notes");
	REQUIRE(recipes);
	REQUIRE(notes);
	CHECK(recipes->confidence == notes->confidence);
	CHECK(notes->accepted);
	CHECK_FALSE(recipes->accepted);
	CHECK(recipes->lostTo == "Skyrim.esm|0x00001403");
}

TEST_CASE("Draft drops patterns the loaded categories do not know", "[draft]")
{
	std::vector<DraftObject> objects{
		Container("Skyrim.esm|0x00000F01", "PlayerHouseChest", "Chest", { 0, 0, 0 }, { 150, 80, 80 }),
		Container("Skyrim.esm|0x00000F02", "WeaponChest", "Weapon Chest", { 500, 0, 0 }),
	};
	DraftOptions options;
	options.knownPattern = [](std::string_view p) { return p != "weapons.*"; };
	const auto plan = SuggestDraft(objects, DefaultDraftRules(), options);
	CHECK_FALSE(Find(plan, 1, "weapons.*"));
}

TEST_CASE("ApplyDraftPlan writes master, accepted categories and ignored notes", "[draft]")
{
	std::vector<DraftObject> objects{
		Container("Skyrim.esm|0x00001001", "PlayerHouseChest", "Chest", { 0, 0, 0 }, { 150, 80, 80 }),
		Container("Skyrim.esm|0x00001002", "WeaponChest", "Weapon Chest", { 500, 0, 0 }),
		Container("Skyrim.esm|0x00001003", "Barrel01", "Barrel", { 900, 0, 0 }),
	};
	objects[2].respawns = true;
	auto plan = SuggestDraft(objects, DefaultDraftRules());
	Layout layout;
	layout.containers.push_back({ .ref = Sk(0x1002), .label = "old" });
	ApplyDraftPlan(plan, objects, layout);
	REQUIRE(layout.master);
	CHECK(*layout.master == Sk(0x1001));
	REQUIRE(layout.containers.size() == 2);
	const auto* weapons = layout.FindContainer(Sk(0x1002));
	REQUIRE(weapons);
	CHECK(weapons->label == "Weapon Chest");
	CHECK(weapons->categories == std::vector<std::string>{ "weapons.*" });
	const auto* barrel = layout.FindContainer(Sk(0x1003));
	REQUIRE(barrel);
	CHECK(barrel->ignored);
	CHECK_FALSE(barrel->note.empty());

	// A rejected suggestion stays out of the file.
	plan.Find(1)->suggestions.front().accepted = false;
	ApplyDraftPlan(plan, objects, layout);
	CHECK(layout.FindContainer(Sk(0x1002))->categories.empty());
}

TEST_CASE("Preview: where items go follows the planner and the displays", "[preview]")
{
	const auto categories = CompileCategories(kStandardCategories);
	Layout     layout;
	layout.master = Sk(0xA000);
	layout.containers.push_back({ .ref = Sk(0xB000), .label = "Armory", .categories = { "weapons.*" } });
	layout.containers.push_back({ .ref = Sk(0xC000), .label = "Satchel", .categories = { "alchemy.*" }, .forms = { Sk(0x777) } });
	layout.containers.push_back({ .ref = Sk(0xD000), .label = "Empty one", .categories = { "keys" } });
	layout.containers.push_back({ .ref = Sk(0xE000), .label = "Barrel", .ignored = true });

	std::vector<ItemInfo> items{
		Weapon(Sk(0x100), WeaponType::OneHandSword, 2),
		Potion(Sk(0x200), 5, 10),
		Misc(Sk(0x300), 3),                             // misc.clutter: no container -> master
		Misc(Sk(0x777), 1),                             // explicit binding
		Weapon(Sk(0x101), WeaponType::Bow, 1),
	};
	items.push_back(MakeItem(Sk(0x400), ItemType::Misc, 1));
	items.back().questItem = true;

	SECTION("containers")
	{
		const auto r = PreviewWhereItemsGo(layout, categories, items);
		REQUIRE(r.byContainer.size() == 4);  // master + 3 non-ignored
		CHECK(r.byContainer[0].master);
		CHECK(r.byContainer[0].total == 3);
		CHECK(r.byContainer[0].perCategory.at("misc.clutter") == 3);
		const auto* armory = r.Find(Sk(0xB000));
		REQUIRE(armory);
		CHECK(armory->total == 3);
		CHECK(armory->items.size() == 2);
		const auto* satchel = r.Find(Sk(0xC000));
		REQUIRE(satchel);
		CHECK(satchel->total == 6);
		CHECK(satchel->items.back().reason == Destination::Reason::Explicit);
		const auto* empty = r.Find(Sk(0xD000));
		REQUIRE(empty);
		CHECK(empty->items.empty());
		CHECK_FALSE(r.Find(Sk(0xE000)));
		CHECK(r.neverMoved == std::vector<std::size_t>{ 5 });
		CHECK(r.toMaster == 3);
		CHECK(r.toContainers == 9);
	}
	SECTION("a rack takes one weapon first")
	{
		DisplaySpot rack;
		rack.ref = Sk(0xF000);
		rack.kind = DisplayKind::Rack;
		rack.rack = RackType::Any;
		const std::vector<DisplaySpot> spots{ rack };
		const auto                     r = PreviewWhereItemsGo(layout, categories, items, spots);
		REQUIRE(r.onDisplays.size() == 1);
		CHECK(r.onDisplays[0].ref == Sk(0xF000));
		CHECK(r.Find(Sk(0xB000))->total == 2);
	}
	SECTION("no master: homeless items have no destination")
	{
		layout.master.reset();
		const auto r = PreviewWhereItemsGo(layout, categories, items);
		CHECK(r.noDestination == std::vector<std::size_t>{ 2 });
	}
}

TEST_CASE("Draft wings: a big home takes a category once per wing; a wing's station only speaks for its own containers", "[draft]")
{
	const auto wing = [](DraftObject a_o, std::string a_key) {
		a_o.appearsLater = true;
		a_o.appearsWith = std::move(a_key);
		return a_o;
	};
	const auto build = [&](std::size_t a_fillers) {
		std::vector<DraftObject> objects{
			Container("Skyrim.esm|0x00000A00", "PlayerHouseChest", "Chest", { 0, 0, 0 }, { 80, 50, 50 }),
			wing(Container("Skyrim.esm|0x00000A01", "BYOHChest", "Chest", { 3000, 0, 0 }), "Room06"),
			wing(Thing(DraftObject::Kind::Other, "Smelter", "Smelter", { 3100, 0, 0 }, { "CraftingSmelter" }), "Room06"),
			wing(Container("Skyrim.esm|0x00000A02", "BYOHChest", "Chest", { 6000, 0, 0 }), "Room07"),
			wing(Thing(DraftObject::Kind::Other, "Smelter", "Smelter", { 6100, 0, 0 }, { "CraftingSmelter" }), "Room07"),
		};
		for (std::size_t i = 0; i < a_fillers; ++i) {
			objects.push_back(Container(std::format("Skyrim.esm|0x00000B{:02X}", i), "EndTable01", "End Table", { -2000.0f - 1000.0f * static_cast<float>(i), 0, 0 }));
		}
		DraftObject door = Thing(DraftObject::Kind::Door, "HouseDoor", "Door", { 0, 0, 0 });
		door.exteriorDoor = true;
		objects.push_back(door);
		return objects;
	};
	const auto ingots = [](const DraftPlan& a_plan, std::size_t a_object) {
		const auto* s = Find(a_plan, a_object, "misc.ores_ingots");
		return s && s->accepted;
	};

	const auto big = build(DefaultDraftRules().areaContainers);
	const auto bigPlan = SuggestDraft(big, DefaultDraftRules());
	CHECK(ingots(bigPlan, 1));
	CHECK(ingots(bigPlan, 3));

	const auto small = build(0);
	const auto smallPlan = SuggestDraft(small, DefaultDraftRules());
	CHECK(ingots(smallPlan, 1) != ingots(smallPlan, 3));  // once per home

	// A smelter built with another wing says nothing about this one's chest.
	auto other = small;
	other[4].appearsWith = "Room06";
	const auto otherPlan = SuggestDraft(other, DefaultDraftRules());
	CHECK_FALSE(Find(otherPlan, 3, "misc.ores_ingots"));
}

TEST_CASE("Draft wings: a room's name shares its categories out over the room's containers", "[draft]")
{
	// A Lakeview-like house: an armory, a library and a kitchen built as wings, a main hall nobody named a rule for.
	const auto wing = [](DraftObject a_o, std::string a_key, std::string a_name) {
		a_o.appearsLater = true;
		a_o.appearsWith = std::move(a_key);
		a_o.wing = std::move(a_name);
		return a_o;
	};
	std::vector<DraftObject> objects{
		Container("Skyrim.esm|0x00000A00", "PlayerHouseChest", "Chest", { 0, 0, 0 }, { 80, 50, 50 }),
		wing(Container("Skyrim.esm|0x00000A01", "BYOHChest", "Chest", { 3000, 0, 0 }), "Room09", "Armory"),
		wing(Container("Skyrim.esm|0x00000A02", "BYOHCupboard", "Cupboard", { 3100, 0, 0 }), "Room09", "Armory"),
		wing(Container("Skyrim.esm|0x00000A03", "BYOHDresser", "Dresser", { 3200, 0, 0 }), "Room09", "Armory"),
		wing(Container("Skyrim.esm|0x00000A04", "BYOHChest", "Chest", { 3300, 0, 0 }), "Room09", "Armory"),
		wing(Container("Skyrim.esm|0x00000A05", "BYOHDresser", "Dresser", { 3400, 0, 0 }), "Room09", "Armory"),
		wing(Container("Skyrim.esm|0x00000A06", "BYOHChest", "Chest", { 6000, 0, 0 }), "Room11", "Library"),
		wing(Container("Skyrim.esm|0x00000A07", "BYOHCupboard", "Cupboard", { 9000, 0, 0 }), "Room10", "Kitchen"),
		wing(Container("Skyrim.esm|0x00000A08", "BYOHChest", "Chest", { 12000, 0, 0 }), "Room02", "Main hall"),
	};
	for (std::size_t i = 0; i < DefaultDraftRules().areaContainers; ++i) {
		objects.push_back(Container(std::format("Skyrim.esm|0x00000B{:02X}", i), "EndTable01", "End Table", { -2000.0f - 1000.0f * static_cast<float>(i), 0, 0 }));
	}
	DraftObject door = Thing(DraftObject::Kind::Door, "HouseDoor", "Door", { 0, 0, 0 });
	door.exteriorDoor = true;
	objects.push_back(door);

	const auto plan = SuggestDraft(objects, DefaultDraftRules());
	const auto holder = [&](std::string_view a_pattern) {
		std::optional<std::size_t> at;
		for (std::size_t o = 1; o <= 8; ++o) {
			if (const auto* s = Find(plan, o, a_pattern); s && s->accepted) {
				CHECK_FALSE(at);  // once per wing
				at = o;
			}
		}
		return at;
	};

	// The armory's five categories go to five different containers, each with the room as the reason.
	std::set<std::size_t> armory;
	for (const auto* p : { "weapons.*", "armor.heavy", "armor.light", "armor.shields", "ammo.*" }) {
		const auto at = holder(p);
		REQUIRE(at);
		CHECK(*at >= 1);
		CHECK(*at <= 5);
		armory.insert(*at);
		CHECK_THAT(Find(plan, *at, p)->reason, Catch::Matchers::ContainsSubstring("in the Armory"));
	}
	CHECK(armory.size() == 5);
	CHECK(holder("books.*") == std::optional<std::size_t>(6));
	CHECK(holder("alchemy.food") == std::optional<std::size_t>(7));
	// "Main hall" says nothing: its chest gets no category from the room.
	CHECK(std::ranges::none_of(plan.Find(8)->suggestions, [](const DraftSuggestion& s) { return s.reason.find("in the ") != std::string::npos; }));

	// Without the room's name, the armory's plain chests get none of it.
	auto unnamed = objects;
	for (auto& o : unnamed) {
		o.wing.clear();
	}
	const auto plain = SuggestDraft(unnamed, DefaultDraftRules());
	CHECK_FALSE(Find(plain, 1, "weapons.*"));
	CHECK_FALSE(Find(plain, 6, "books.*"));
}

TEST_CASE("Draft rules: wings need words and categories", "[draft]")
{
	Diagnostics d;
	const auto  r = ParseDraftRules(R"({ "$schema": "dovastash-draftrules-1", "wings": [
		{ "label": "armory", "words": ["armory"], "categories": ["weapons.*"], "confidence": 0.7 },
		{ "label": "nothing", "categories": ["books.*"] },
		{ "label": "empty", "words": ["library"] } ] })",
		"test", d);
	REQUIRE(r);
	REQUIRE(r->wings.size() == 1);
	CHECK(r->wings[0].confidence == 0.7f);
	CHECK(d.ErrorCount() == 2);
	CHECK(DefaultDraftRules().wings.size() >= 6);
}
