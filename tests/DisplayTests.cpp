#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"

using namespace dova::core;
using namespace dova::test;

namespace
{
	ItemInfo Armor(FormRef a_form, std::uint32_t a_slots, std::int32_t a_value, std::initializer_list<const char*> a_keywords = {})
	{
		auto item = MakeItem(std::move(a_form), ItemType::Armor);
		item.armorSlots = a_slots;
		item.value = a_value;
		for (const auto* k : a_keywords) {
			item.keywords.emplace_back(k);
		}
		return item;
	}

	ItemInfo Valued(ItemInfo a_item, std::int32_t a_value)
	{
		a_item.value = a_value;
		return a_item;
	}

	DisplayStock OnPlayer(ItemInfo a_item, std::int32_t a_available = -1)
	{
		const auto n = a_available < 0 ? a_item.count : a_available;
		return DisplayStock{ ContainerRef::Player(), std::move(a_item), n };
	}

	DisplayStock InChest(ItemInfo a_item)
	{
		const auto n = a_item.count;
		return DisplayStock{ ContainerRef(Sk(0xC0)), std::move(a_item), n };
	}

	DisplaySpot Rack(std::uint32_t a_id, RackType a_type = RackType::Any)
	{
		return DisplaySpot{ Sk(a_id), DisplayKind::Rack, a_type, {}, {}, false, {} };
	}

	DisplaySpot Spot(std::uint32_t a_id, DisplayKind a_kind, std::vector<FormRef> a_items = {})
	{
		return DisplaySpot{ Sk(a_id), a_kind, RackType::Any, std::move(a_items), {}, false, {} };
	}

	DisplayBinding Binding(FormRef a_ref, std::optional<DisplayKind> a_kind, std::string a_label, std::vector<FormRef> a_items, bool a_ignored)
	{
		DisplayBinding d;
		d.ref = std::move(a_ref);
		d.kind = a_kind;
		d.label = std::move(a_label);
		d.items = std::move(a_items);
		d.ignored = a_ignored;
		return d;
	}

	constexpr std::uint32_t kHead = 1u << 0;
	constexpr std::uint32_t kBody = 1u << 2;
	constexpr std::uint32_t kHands = 1u << 3;
	constexpr std::uint32_t kFeet = 1u << 7;
	constexpr std::uint32_t kShieldSlot = 1u << 9;
}

TEST_CASE("Rack pivots follow the vanilla rack script", "[display]")
{
	CHECK(RackPivot(Weapon(Sk(1), WeaponType::OneHandSword)) == "SwordPivot01");
	CHECK(RackPivot(Weapon(Sk(1), WeaponType::OneHandDagger)) == "DaggerPivot01");
	CHECK(RackPivot(Weapon(Sk(1), WeaponType::TwoHandAxe)) == "BattleAxePivot01");
	auto hammer = Weapon(Sk(1), WeaponType::TwoHandAxe);
	hammer.keywords = { "WeapTypeWarhammer" };
	CHECK(RackPivot(hammer) == "WarhammerPivot01");
	CHECK(RackPivot(Weapon(Sk(1), WeaponType::Staff)) == "StaffPivot01");
	CHECK(RackPivot(Weapon(Sk(1), WeaponType::Crossbow)) == "BowPivot01");
	CHECK(RackPivot(Weapon(Sk(1), WeaponType::HandToHand)).empty());
	CHECK(RackPivot(Armor(Sk(1), kShieldSlot, 10, { "ArmorShield" })) == "ShieldPivot01");
	CHECK(RackPivot(Armor(Sk(1), kBody, 10)).empty());

	// The script goes by keyword (in its order), not by the engine's weapon type.
	auto oddSword = Weapon(Sk(1), WeaponType::OneHandSword);
	oddSword.keywords = { "WeapTypeDagger" };
	CHECK(RackPivot(oddSword) == "DaggerPivot01");
	CHECK_FALSE(RackTakes(RackType::NoDaggerOrBow, oddSword));
	CHECK(RackTakes(RackType::Dagger, oddSword));
	auto both = Weapon(Sk(1), WeaponType::OneHandMace);
	both.keywords = { "weaptypedagger", "WeapTypeMace" };
	CHECK(RackPivot(both) == "MacePivot01");

	const auto dagger = Weapon(Sk(1), WeaponType::OneHandDagger);
	const auto bow = Weapon(Sk(2), WeaponType::Bow);
	const auto sword = Weapon(Sk(3), WeaponType::OneHandSword);
	const auto shield = Armor(Sk(4), kShieldSlot, 10, { "ArmorShield" });
	CHECK(RackTakes(RackType::Any, dagger));
	CHECK(RackTakes(RackType::Any, bow));
	CHECK_FALSE(RackTakes(RackType::Any, shield));
	CHECK(RackTakes(RackType::Shield, shield));
	CHECK_FALSE(RackTakes(RackType::Shield, sword));
	CHECK_FALSE(RackTakes(RackType::NoDaggerOrBow, dagger));
	CHECK_FALSE(RackTakes(RackType::NoDaggerOrBow, bow));
	CHECK(RackTakes(RackType::NoDaggerOrBow, sword));
	CHECK(RackTakes(RackType::Dagger, dagger));
	CHECK_FALSE(RackTakes(RackType::Dagger, sword));
	CHECK(RackTakes(RackType::LargeCase, sword));
	CHECK(RackTakes(RackType::LargeCase, dagger));
	CHECK(RackTakes(RackType::LargeCase, bow));
	CHECK(RackTakes(RackType::LargeCase, Weapon(Sk(5), WeaponType::Staff)));  // long ones: refused by the plugin
	CHECK_FALSE(RackTakes(RackType::LargeCase, shield));

	// Crossbows only where the trigger has a crossbow node (the Unofficial Patch's racks).
	auto crossbow = Weapon(Sk(6), WeaponType::Crossbow);
	crossbow.keywords = { "WeapTypeBow" };
	CHECK_FALSE(RackTakes(RackType::Any, crossbow));
	CHECK(RackTakes(RackType::Any, crossbow, true));
	CHECK(RackTakes(RackType::LargeCase, crossbow, true));
	CHECK_FALSE(RackTakes(RackType::NoDaggerOrBow, crossbow, true));
	CHECK_FALSE(RackTakes(RackType::Dagger, crossbow, true));
	CHECK_FALSE(RackTakes(RackType::Shield, crossbow, true));
}

TEST_CASE("Racks skip crossbows without a crossbow node and forms their script refuses", "[display]")
{
	auto crossbow = Valued(Weapon(Sk(0x10), WeaponType::Crossbow), 900);
	crossbow.keywords = { "WeapTypeBow" };
	const std::vector<DisplayStock> stocks{
		OnPlayer(crossbow),                                              // 0
		OnPlayer(Valued(Weapon(Sk(0x11), WeaponType::Staff), 500)),       // 1 a staff that does not fit the case
		OnPlayer(Valued(Weapon(Sk(0x12), WeaponType::OneHandSword), 10)),  // 2
	};

	auto plainRack = Rack(0xA1);
	auto caseRack = Rack(0xA2, RackType::LargeCase);
	caseRack.refuses = { Sk(0x11) };
	{
		const std::vector<DisplaySpot> spots{ plainRack, caseRack };
		const auto                     plan = PlanDisplays(spots, stocks, {});
		// The case comes first and skips the staff; the plain rack can't take the crossbow.
		REQUIRE(plan.size() == 2);
		CHECK(plan[0] == DisplayAssignment{ 1, 2, 1 });
		CHECK(plan[1] == DisplayAssignment{ 0, 1, 1 });
	}
	{
		plainRack.crossbows = true;
		const std::vector<DisplaySpot> spots{ plainRack, caseRack };
		const auto                     plan = PlanDisplays(spots, stocks, {});
		REQUIRE(plan.size() == 2);
		CHECK(plan[0] == DisplayAssignment{ 1, 2, 1 });
		CHECK(plan[1] == DisplayAssignment{ 0, 0, 1 });
	}
}

TEST_CASE("Racks take the best weapons, one copy of each, specialised racks first", "[display]")
{
	const std::vector<DisplayStock> stocks{
		OnPlayer(Valued(Weapon(Sk(0x10), WeaponType::OneHandSword, 3), 25)),  // 0 iron sword x3
		OnPlayer(Valued(Weapon(Sk(0x11), WeaponType::OneHandDagger), 10)),    // 1 dagger
		InChest(Valued(Weapon(Sk(0x12), WeaponType::TwoHandSword), 900)),     // 2 stored greatsword
		OnPlayer(Valued(Weapon(Sk(0x13), WeaponType::Bow), 300)),             // 3 bow
		OnPlayer(Armor(Sk(0x14), kShieldSlot, 50, { "ArmorShield" })),        // 4 shield
	};
	const std::vector<DisplaySpot> spots{ Rack(0xA1), Rack(0xA2), Rack(0xA3), Rack(0xA4), Rack(0xA5, RackType::Dagger),
		Rack(0xA6, RackType::Shield) };
	const auto plan = PlanDisplays(spots, stocks, {});

	// Shield plaque, dagger rack, then the "any" racks by value: greatsword, bow, iron sword (once).
	REQUIRE(plan.size() == 5);
	CHECK(plan[0] == DisplayAssignment{ 5, 4, 1 });
	CHECK(plan[1] == DisplayAssignment{ 4, 1, 1 });
	CHECK(plan[2] == DisplayAssignment{ 0, 2, 1 });
	CHECK(plan[3] == DisplayAssignment{ 1, 3, 1 });
	CHECK(plan[4] == DisplayAssignment{ 2, 0, 1 });
}

TEST_CASE("Displays skip full spots, shown forms and items that cannot move", "[display]")
{
	auto quest = Valued(Weapon(Sk(0x20), WeaponType::OneHandSword), 5000);
	quest.questItem = true;
	const std::vector<DisplayStock> stocks{
		OnPlayer(quest),
		OnPlayer(Valued(Weapon(Sk(0x21), WeaponType::OneHandSword), 100)),    // already on display
		OnPlayer(Valued(Weapon(Sk(0x22), WeaponType::OneHandSword), 50), 0),  // kept by keep rules
		OnPlayer(Valued(Weapon(Sk(0x23), WeaponType::OneHandMace), 20)),
	};
	auto full = Rack(0xA1);
	full.full = true;
	const std::vector<DisplaySpot> spots{ full, Rack(0xA2), Rack(0xA3) };
	const auto plan = PlanDisplays(spots, stocks, { Sk(0x21) });
	REQUIRE(plan.size() == 1);
	CHECK(plan[0] == DisplayAssignment{ 1, 3, 1 });
}

TEST_CASE("Item plaques get their own item, plain copies only, and keep it from racks", "[display]")
{
	auto tempered = Valued(Weapon(Sk(0x30), WeaponType::Bow), 2000);  // a tempered copy of the plaque's bow
	tempered.customized = true;
	const std::vector<DisplayStock> stocks{
		OnPlayer(tempered),
		InChest(Valued(Weapon(Sk(0x30), WeaponType::Bow), 2000)),  // a plain copy in storage
		OnPlayer(Armor(Sk(0x31), kHead, 3000)),                    // a mask whose plaque is full
		OnPlayer(Valued(Weapon(Sk(0x32), WeaponType::OneHandSword), 10)),
	};
	auto maskPlaque = Spot(0xB2, DisplayKind::Item, { Sk(0x31) });
	maskPlaque.full = true;
	const std::vector<DisplaySpot> spots{ Rack(0xA1), Spot(0xB1, DisplayKind::Item, { Sk(0x30) }), maskPlaque, Spot(0xB3, DisplayKind::Mannequin) };
	const auto plan = PlanDisplays(spots, stocks, {});

	// The bow plaque takes the stored plain copy. The rack never takes the tempered bow (reserved for its
	// plaque) and gets the sword. The mask is reserved for its plaque, so the mannequin stays empty.
	REQUIRE(plan.size() == 2);
	CHECK(plan[0] == DisplayAssignment{ 1, 1, 1 });
	CHECK(plan[1] == DisplayAssignment{ 0, 3, 1 });
}

TEST_CASE("Mannequins wear one matching set with a body piece", "[display]")
{
	const std::vector<DisplayStock> stocks{
		OnPlayer(Armor(Sk(0x40), kBody, 100, { "ArmorMaterialIron" })),                          // 0
		OnPlayer(Armor(Sk(0x41), kHead, 60, { "ArmorMaterialIron" })),                           // 1
		OnPlayer(Armor(Sk(0x42), kBody, 900, { "ArmorMaterialDaedric" })),                       // 2
		OnPlayer(Armor(Sk(0x43), kHands, 300, { "ArmorMaterialDaedric" })),                      // 3
		OnPlayer(Armor(Sk(0x44), kHands, 200, { "ArmorMaterialDaedric" })),                      // 4 second gauntlets
		OnPlayer(Armor(Sk(0x45), kFeet | kHands, 50, { "ArmorMaterialIron" })),                  // 5 boots and gloves in one
		OnPlayer(Armor(Sk(0x46), kHead, 999, { "ArmorMaterialGlass" })),                         // 6 no glass cuirass
		OnPlayer(Armor(Sk(0x47), kShieldSlot, 500, { "ArmorMaterialDaedric", "ArmorShield" })),  // 7 shield: never
	};
	const std::vector<DisplaySpot> spots{ Spot(0xC1, DisplayKind::Mannequin), Spot(0xC2, DisplayKind::Mannequin),
		Spot(0xC3, DisplayKind::Mannequin) };
	const auto plan = PlanDisplays(spots, stocks, {});

	// The more complete set first: iron (cuirass, helmet, the boots-and-gloves piece: four slot groups),
	// then daedric (cuirass and the better gauntlets), then nothing: the glass helmet has no cuirass.
	REQUIRE(plan.size() == 5);
	CHECK(plan[0] == DisplayAssignment{ 0, 0, 1 });
	CHECK(plan[1] == DisplayAssignment{ 0, 1, 1 });
	CHECK(plan[2] == DisplayAssignment{ 0, 5, 1 });
	CHECK(plan[3] == DisplayAssignment{ 1, 2, 1 });
	CHECK(plan[4] == DisplayAssignment{ 1, 3, 1 });
}

TEST_CASE("Cases get every item they are made for that they lack", "[display]")
{
	const std::vector<DisplayStock> stocks{
		OnPlayer(Weapon(Sk(0x50), WeaponType::OneHandMace)),
		InChest(Weapon(Sk(0x51), WeaponType::TwoHandAxe)),
		OnPlayer(Weapon(Sk(0x52), WeaponType::OneHandSword)),
	};
	auto c = Spot(0xD1, DisplayKind::Case, { Sk(0x50), Sk(0x51), Sk(0x52), Sk(0x53) });
	c.holds = { Sk(0x52) };
	const std::vector<DisplaySpot> spots{ Rack(0xA1), c };
	const auto plan = PlanDisplays(spots, stocks, {});
	// The rack gets nothing: what a case is made for is reserved for it.
	REQUIRE(plan.size() == 2);
	CHECK(plan[0] == DisplayAssignment{ 1, 0, 1 });
	CHECK(plan[1] == DisplayAssignment{ 1, 1, 1 });
}

TEST_CASE("Preferred items win a rack; artifacts beat value", "[display]")
{
	auto artifact = Valued(Weapon(Sk(0x60), WeaponType::OneHandMace), 10);
	artifact.keywords = { "DaedricArtifact" };
	const std::vector<DisplayStock> stocks{
		OnPlayer(Valued(Weapon(Sk(0x61), WeaponType::OneHandSword), 5000)),
		OnPlayer(artifact),
		OnPlayer(Valued(Weapon(Sk(0x62), WeaponType::OneHandSword), 1)),
	};
	auto preferring = Rack(0xA1);
	preferring.items = { Sk(0x62) };
	const std::vector<DisplaySpot> spots{ preferring, Rack(0xA2), Rack(0xA3) };
	const auto plan = PlanDisplays(spots, stocks, {});
	REQUIRE(plan.size() == 3);
	CHECK(plan[0] == DisplayAssignment{ 0, 2, 1 });
	CHECK(plan[1] == DisplayAssignment{ 1, 1, 1 });
	CHECK(plan[2] == DisplayAssignment{ 2, 0, 1 });
}

TEST_CASE("Racks and mannequins with categories take only items in them", "[display]")
{
	std::vector<DisplayStock> stocks{
		OnPlayer(Valued(Weapon(Sk(0x10), WeaponType::OneHandSword), 500)),
		OnPlayer(Valued(Weapon(Sk(0x11), WeaponType::Bow), 50)),
		OnPlayer(Armor(Sk(0x20), kBody, 900, { "ArmorMaterialDaedric" })),
		OnPlayer(Armor(Sk(0x21), kBody, 100, { "ArmorMaterialLeather" })),
	};
	stocks[0].categories = { "weapons.swords" };
	stocks[1].categories = { "weapons.bows" };
	stocks[2].categories = { "armor.heavy" };
	stocks[3].categories = { "armor.light" };

	auto bows = Rack(0xA1);
	bows.categories = { "weapons.bows" };
	auto mannequin = Spot(0xA2, DisplayKind::Mannequin);
	mannequin.categories = { "armor.light" };
	const std::vector<DisplaySpot> spots{ bows, mannequin, Rack(0xA3) };

	const auto plan = PlanDisplays(spots, stocks, {});
	REQUIRE(plan.size() == 3);
	const auto has = [&](std::size_t a_spot, std::size_t a_stock) {
		return std::ranges::find(plan, DisplayAssignment{ a_spot, a_stock, 1 }) != plan.end();
	};
	CHECK(has(0, 1));  // the bow rack gets the bow, not the more valuable sword
	CHECK(has(1, 3));  // the light-armor mannequin skips the daedric set
	CHECK(has(2, 0));  // a rack without categories takes anything that fits

	// Categories that match nothing here: the rack stays empty.
	auto staves = Rack(0xA4);
	staves.categories = { "weapons.staves" };
	CHECK(PlanDisplays(std::vector{ staves }, stocks, {}).empty());
	// Wildcards work as for containers.
	auto weapons = Rack(0xA5);
	weapons.categories = { "weapons.*" };
	const auto any = PlanDisplays(std::vector{ weapons }, stocks, {});
	REQUIRE(any.size() == 1);
	CHECK(any[0].stock == 0);
}

TEST_CASE("Display categories survive a round trip", "[display][layout]")
{
	Diagnostics diags;
	const auto  layout = ParseLayoutText(R"({ "$schema": "dovastash-layout-1", "cell": "Skyrim.esm|0x1",
		"displays": [ { "ref": "Skyrim.esm|0xA01", "label": "Bow rack", "categories": ["weapons.bows", "weapons.crossbows"] },
		              { "ref": "Skyrim.esm|0xA02", "categories": ["not a pattern*x"] } ] })",
		"cats.json", diags);
	REQUIRE(layout);
	CHECK_FALSE(diags.HasErrors());
	CHECK(diags.WarningCount() == 1);  // the invalid pattern
	REQUIRE(layout->displays.size() == 2);
	CHECK(layout->displays[0].categories == std::vector<std::string>{ "weapons.bows", "weapons.crossbows" });
	const auto text = SerializeLayout(*layout);
	CHECK(text.find("\"label\": \"Bow rack\", \"categories\": [\"weapons.bows\", \"weapons.crossbows\"]") != std::string::npos);
	Diagnostics again;
	CHECK(SerializeLayout(*ParseLayoutText(text, "cats.json", again)) == text);
}

TEST_CASE("Items going on display are taken out of the deposit", "[display]")
{
	std::vector<Move> moves{
		Move{ 1, Sk(0x10), 3, ContainerRef::Player(), ContainerRef(Sk(0xC0)) },
		Move{ 2, Sk(0x11), 1, ContainerRef::Player(), ContainerRef(Sk(0xC0)) },
	};
	auto sword = Weapon(Sk(0x10), WeaponType::OneHandSword, 3);
	sword.stackId = 1;
	auto dagger = Weapon(Sk(0x11), WeaponType::OneHandDagger, 1);
	dagger.stackId = 2;
	auto stored = Weapon(Sk(0x12), WeaponType::OneHandMace, 1);
	stored.stackId = 1;  // same id in another snapshot: not the player's
	const std::vector<DisplayStock>      stocks{ OnPlayer(sword), OnPlayer(dagger), InChest(stored) };
	const std::vector<DisplayAssignment> plan{ { 0, 0, 1 }, { 1, 1, 1 }, { 2, 2, 1 } };
	ReserveForDisplays(moves, stocks, plan);
	REQUIRE(moves.size() == 1);
	CHECK(moves[0].stackId == 1);
	CHECK(moves[0].count == 2);
}

TEST_CASE("Layouts keep display entries through a round trip", "[display][layout]")
{
	constexpr std::string_view kText = R"({
		"$schema": "dovastash-layout-1",
		"cell": "Skyrim.esm|0x000165A7",
		"containers": [],
		"displays": [
			{ "ref": "Skyrim.esm|0x00000A01", "kind": "item", "label": "Elder Scroll", "items": ["Dawnguard.esm|0x000118F9", "nonsense"] },
			{ "ref": "Skyrim.esm|0x00000A02", "ignored": true, "pose": 2 },
			{ "ref": "Skyrim.esm|0x00000A03", "kind": "plinth" },
			"broken"
		]
	})";
	Diagnostics diags;
	const auto  layout = ParseLayoutText(kText, "test.json", diags);
	REQUIRE(layout);
	REQUIRE(layout->displays.size() == 3);
	CHECK(layout->displays[0].kind == DisplayKind::Item);
	CHECK(layout->displays[0].items == std::vector<FormRef>{ F("Dawnguard.esm|0x000118F9") });
	CHECK(layout->displays[0].unparsedItems == std::vector<std::string>{ "nonsense" });
	CHECK(layout->displays[1].ignored);
	CHECK_FALSE(layout->displays[2].kind);
	CHECK(layout->unparsedDisplays.size() == 1);
	CHECK(diags.HasErrors());

	const auto  text = SerializeLayout(*layout);
	Diagnostics again;
	const auto  reparsed = ParseLayoutText(text, "again.json", again);
	REQUIRE(reparsed);
	CHECK(SerializeLayout(*reparsed) == text);
	CHECK(text.find("\"pose\": 2") != std::string::npos);
	CHECK(text.find("\"kind\": \"plinth\"") != std::string::npos);
	CHECK(text.find("\"broken\"") != std::string::npos);
}

TEST_CASE("Applying a template merges display entries", "[display][template]")
{
	Layout tmpl;
	tmpl.cell = Sk(0x165A7);
	tmpl.displays.push_back(Binding(Sk(0xA01), DisplayKind::Rack, "", {}, false));
	tmpl.displays.push_back(Binding(Sk(0xA02), std::nullopt, "Mask wall", { Sk(0x31) }, false));
	Layout player;
	player.cell = Sk(0x165A7);
	player.displays.push_back(Binding(Sk(0xA01), std::nullopt, "By the door", { Sk(0x10) }, true));
	player.displays.push_back(Binding(Sk(0xA09), DisplayKind::Mannequin, "", {}, false));

	const auto merged = ApplyTemplate(TemplateCandidate{ "Templates/T.json", tmpl }, &player);
	REQUIRE(merged.displays.size() == 3);
	CHECK(merged.displays[0].ignored);
	CHECK(merged.displays[0].kind == DisplayKind::Rack);
	CHECK(merged.displays[0].label == "By the door");
	CHECK(merged.displays[0].items == std::vector<FormRef>{ Sk(0x10) });
	CHECK(merged.displays[1].label == "Mask wall");
	CHECK(merged.displays[2].ref == Sk(0xA09));
}
