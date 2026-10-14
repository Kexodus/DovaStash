#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"

#include "dova/core/CraftingStorage.h"

using namespace dova::core;
using namespace dova::core::crafting;
using namespace dova::test;

namespace
{
	const FormRef kIronIngot = Sk(0x5ACE4);
	const FormRef kLeather = Sk(0xDB5D2);
	const FormRef kStrips = Sk(0x800E4);
	const FormRef kBlueMountain = Sk(0x77E1C);
	const FormRef kIronSword = Sk(0x12EB7);

	const FormRef kMaster = Sk(0xC0);
	const FormRef kChest = Sk(0xC1);
	const FormRef kSack = Sk(0xC2);
	const FormRef kBarrel = Sk(0xC3);

	Stack Plain(FormRef a_form, ItemType a_type, std::int32_t a_count, std::uint32_t a_stackId)
	{
		return Stack{ MakeItem(std::move(a_form), a_type, a_count, a_stackId), std::string{} };
	}

	Stack Keyed(FormRef a_form, ItemType a_type, std::int32_t a_count, std::uint32_t a_stackId, std::string a_key)
	{
		return Stack{ MakeItem(std::move(a_form), a_type, a_count, a_stackId), std::move(a_key) };
	}

	SupplyRequest Forge()
	{
		return SupplyRequest{ Station::Recipes, { kIronIngot, kLeather, kStrips, kIronSword }, false };
	}

	std::int32_t Total(const std::vector<Pull>& a_pulls)
	{
		std::int32_t n = 0;
		for (const auto& p : a_pulls) {
			n += p.move.count;
		}
		return n;
	}
}

TEST_CASE("Mode: Auto is Storage when the hooks are in, Move when not, Off next to another crafting mod", "[crafting][storage]")
{
	const std::vector<std::string> none;
	const std::vector<std::string> cprfc{ "Crafting Pulls Resources From Containers" };

	auto r = ResolveMode(SupplySetting::Auto, true, none);
	CHECK(r.mode == SupplyMode::Storage);
	CHECK_FALSE(r.chosen);
	CHECK(r.why.empty());

	r = ResolveMode(SupplySetting::Auto, false, none);
	CHECK(r.mode == SupplyMode::Move);
	CHECK_FALSE(r.why.empty());

	r = ResolveMode(SupplySetting::Auto, true, cprfc);
	CHECK(r.mode == SupplyMode::Off);
	CHECK(r.why == "Crafting Pulls Resources From Containers is loaded and already does this");

	const std::vector<std::string> two{ "Smart Crafting Storage", "Linked Crafting Storage" };
	CHECK(ResolveMode(SupplySetting::Auto, true, two).why == "Smart Crafting Storage, Linked Crafting Storage are loaded and already do this");
}

TEST_CASE("Mode: the player's own choice wins over the detection", "[crafting][storage]")
{
	const std::vector<std::string> cprfc{ "Crafting Pulls Resources From Containers" };

	auto r = ResolveMode(SupplySetting::Storage, true, cprfc);
	CHECK(r.mode == SupplyMode::Storage);
	CHECK(r.chosen);

	r = ResolveMode(SupplySetting::Move, true, cprfc);
	CHECK(r.mode == SupplyMode::Move);
	CHECK(r.chosen);

	r = ResolveMode(SupplySetting::Off, true, {});
	CHECK(r.mode == SupplyMode::Off);
	CHECK(r.chosen);

	// Chosen, but this runtime has no hooks (VR): the materials are brought instead.
	r = ResolveMode(SupplySetting::Storage, false, {});
	CHECK(r.mode == SupplyMode::Move);
	CHECK(r.chosen);
	CHECK_FALSE(r.why.empty());
}

TEST_CASE("Mode: setting values and stations", "[crafting][storage]")
{
	CHECK(ToSupplySetting(0) == SupplySetting::Auto);
	CHECK(ToSupplySetting(1) == SupplySetting::Off);
	CHECK(ToSupplySetting(2) == SupplySetting::Move);
	CHECK(ToSupplySetting(3) == SupplySetting::Storage);
	CHECK(ToSupplySetting(4) == SupplySetting::Auto);
	CHECK(ToSupplySetting(-1) == SupplySetting::Auto);

	CHECK(ModeFor(SupplyMode::Storage, Station::Recipes) == SupplyMode::Storage);
	CHECK(ModeFor(SupplyMode::Storage, Station::Alchemy) == SupplyMode::Storage);
	CHECK(ModeFor(SupplyMode::Storage, Station::Enchanting) == SupplyMode::Move);
	CHECK(ModeFor(SupplyMode::Move, Station::Alchemy) == SupplyMode::Move);
	CHECK(ModeFor(SupplyMode::Off, Station::Enchanting) == SupplyMode::Off);
	CHECK(SupplyModeName(SupplyMode::Storage) == "craft from storage");
}

TEST_CASE("Other crafting-from-storage mods are found by file name, any case", "[crafting][storage]")
{
	const std::vector<std::string> loaded{ "Skyrim.esm", "linkedcraftingstorage.ESP", "CraftingPullFromContainer.dll", "Unrelated.esp" };
	const auto found = DetectOtherMods(loaded);
	REQUIRE(found.size() == 2);
	CHECK(found[0] == "Crafting Pulls Resources From Containers");  // in list order
	CHECK(found[1] == "Linked Crafting Storage");

	CHECK(DetectOtherMods(std::vector<std::string>{ "Skyrim.esm", "CraftingPullFromContainer.esp" }).empty());
	CHECK(DetectOtherMods(std::vector<std::string>{ "Workbench Containers.esp" }) == std::vector<std::string>{ "Workbench Containers" });

	for (const auto& m : KnownCraftingStorageMods()) {
		CHECK_FALSE(m.file.empty());
		CHECK_FALSE(m.name.empty());
	}
}

TEST_CASE("Supply counts what Move mode would bring, per material", "[crafting][storage]")
{
	auto tempered = Plain(kIronIngot, ItemType::Misc, 4, 3);
	tempered.item.customized = true;
	auto favorite = Plain(kLeather, ItemType::Misc, 2, 4);
	favorite.item.favorited = true;
	auto quest = Plain(kLeather, ItemType::Misc, 1, 5);
	quest.item.questItem = true;

	const std::vector<Stored> stored{
		{ kMaster, { Plain(kIronIngot, ItemType::Misc, 5, 1), Plain(kBlueMountain, ItemType::Ingredient, 9, 2), tempered } },
		{ kChest, { Plain(kLeather, ItemType::Misc, 3, 6), favorite, quest, Keyed(kIronIngot, ItemType::Misc, 2, 7, "owner=00013BBF;") } },
		{ kSack, { Plain(kIronSword, ItemType::Weapon, 1, 8), Stack{ MakeItem(kStrips, ItemType::Misc, 4, 9), std::nullopt } } },
	};
	const auto supply = MakeSupply(Forge(), stored);

	REQUIRE(supply.contains(kIronIngot));
	CHECK(supply.at(kIronIngot).count == 7);  // 5 plain + 2 stolen; the tempered 4 never
	CHECK(supply.at(kIronIngot).containers == std::vector<FormRef>{ kMaster, kChest });
	CHECK(supply.at(kLeather).count == 3);    // favorited and quest copies never
	CHECK_FALSE(supply.contains(kBlueMountain));  // not a forge material
	CHECK_FALSE(supply.contains(kIronSword));     // gear, and bCraftingSupplyGear is off
	CHECK_FALSE(supply.contains(kStrips));        // unknown extra data never travels

	auto withGear = Forge();
	withGear.gear = true;
	CHECK(MakeSupply(withGear, stored).at(kIronSword).count == 1);

	const SupplyRequest lab{ Station::Alchemy, {}, false };
	const auto          alchemy = MakeSupply(lab, stored);
	REQUIRE(alchemy.size() == 1);
	CHECK(alchemy.at(kBlueMountain).count == 9);
}

TEST_CASE("Supply takes from the container the material is bound to first", "[crafting][storage]")
{
	const std::vector<Stored> stored{
		{ kMaster, { Plain(kIronIngot, ItemType::Misc, 1, 1) } },
		{ kChest, { Plain(kIronIngot, ItemType::Misc, 2, 2), Plain(kLeather, ItemType::Misc, 2, 3) } },
		{ kSack, { Plain(kIronIngot, ItemType::Misc, 3, 4) } },
	};
	const auto supply = MakeSupply(Forge(), stored, [](const ItemInfo& a_item) -> std::optional<FormRef> {
		if (a_item.form == kIronIngot) {
			return kSack;
		}
		if (a_item.form == kLeather) {
			return kBarrel;  // bound elsewhere, and that container holds none: layout order
		}
		return std::nullopt;
	});
	CHECK(supply.at(kIronIngot).containers == std::vector<FormRef>{ kSack, kMaster, kChest });
	CHECK(supply.at(kIronIngot).count == 6);
	CHECK(supply.at(kLeather).containers == std::vector<FormRef>{ kChest });
}

TEST_CASE("A shortfall is taken in order, partial stacks, plain copies before keyed ones", "[crafting][storage]")
{
	const std::vector<Stored> stored{
		{ kMaster, { Plain(kIronIngot, ItemType::Misc, 1, 1) } },
		{ kChest, { Keyed(kIronIngot, ItemType::Misc, 2, 2, "owner=00013BBF;"), Plain(kIronIngot, ItemType::Misc, 2, 3), Plain(kLeather, ItemType::Misc, 5, 4) } },
		{ kSack, { Plain(kIronIngot, ItemType::Misc, 3, 5) } },
	};
	const std::vector<FormRef> order{ kSack, kMaster, kChest };

	// The bound sack covers it alone: one partial stack.
	auto take = PlanTake(Forge(), stored, kIronIngot, order, 2);
	REQUIRE(take.size() == 1);
	CHECK(take[0].move.from == ContainerRef(kSack));
	CHECK(take[0].move.to.IsPlayer());
	CHECK(take[0].move.stackId == 5);
	CHECK(take[0].move.count == 2);
	CHECK(take[0].key.empty());

	// Two containers and more: sack (3), master (1), then the chest's plain copies before its stolen ones.
	take = PlanTake(Forge(), stored, kIronIngot, order, 7);
	REQUIRE(take.size() == 4);
	CHECK(take[0].move.stackId == 5);
	CHECK(take[1].move.stackId == 1);
	CHECK(take[2].move.stackId == 3);
	CHECK(take[3].move.stackId == 2);
	CHECK(take[3].move.count == 1);
	CHECK(take[3].key == "owner=00013BBF;");
	CHECK(Total(take) == 7);

	// More than there is: everything, no more.
	take = PlanTake(Forge(), stored, kIronIngot, order, 50);
	CHECK(Total(take) == 8);

	// A container missing from the order comes last.
	take = PlanTake(Forge(), stored, kIronIngot, std::vector<FormRef>{ kChest }, 3);
	REQUIRE(take.size() == 2);
	CHECK(take[0].move.stackId == 3);
	CHECK(take[1].move.stackId == 2);

	CHECK(PlanTake(Forge(), stored, kIronIngot, order, 0).empty());
	CHECK(PlanTake(Forge(), stored, kBlueMountain, order, 3).empty());
}

TEST_CASE("A shortfall never takes what Move mode would leave", "[crafting][storage]")
{
	auto favorite = Plain(kLeather, ItemType::Misc, 4, 1);
	favorite.item.favorited = true;
	const std::vector<Stored> stored{ { kChest, { favorite, Plain(kLeather, ItemType::Misc, 1, 2) } } };
	const auto                take = PlanTake(Forge(), stored, kLeather, std::vector<FormRef>{ kChest }, 3);
	REQUIRE(take.size() == 1);
	CHECK(take[0].move.stackId == 2);
	CHECK(take[0].move.count == 1);
}

TEST_CASE("Shortfall and unused copies after a craft", "[crafting][storage]")
{
	CHECK(Shortfall(3, 5) == 0);
	CHECK(Shortfall(3, 3) == 0);
	CHECK(Shortfall(5, 2) == 3);
	CHECK(Shortfall(2, 0) == 2);
	CHECK(Shortfall(2, -1) == 2);

	// Had 2, 3 brought, the craft took 5: nothing to put back.
	CHECK(Unused(2, 3, 5, 0) == 0);
	// The craft took nothing: the 3 brought go back.
	CHECK(Unused(2, 3, 5, 5) == 3);
	// It took 2 of the 5: three are left over, at most the 3 brought go back.
	CHECK(Unused(2, 3, 5, 3) == 3);
	// It took 4: one left over, it goes back.
	CHECK(Unused(2, 3, 5, 1) == 1);
	// Nothing was brought: nothing goes back, whatever happened.
	CHECK(Unused(6, 0, 5, 6) == 0);
}
