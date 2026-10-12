#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"

#include "dova/core/Crafting.h"

using namespace dova::core;
using namespace dova::core::crafting;
using namespace dova::test;

namespace
{
	const FormRef kIronOre = Sk(0x71CF3);
	const FormRef kIronIngot = Sk(0x5ACE4);
	const FormRef kSteelIngot = Sk(0x5ACE5);
	const FormRef kCorundum = Sk(0x5ACDB);
	const FormRef kLeather = Sk(0xDB5D2);
	const FormRef kStrips = Sk(0x800E4);
	const FormRef kCrossbow = Sk(0x0F5C9);  // used by the Enhanced Crossbow recipe
	const FormRef kWheat = Sk(0x4B0BA);
	const FormRef kBlueMountain = Sk(0x77E1C);
	const FormRef kGrandFilled = Sk(0x2E4FF);
	const FormRef kModOre = FormRef{ "OreMod.esp", 0x800 };

	const FormRef kChest = Sk(0xC1);
	const FormRef kSack = Sk(0xC2);

	Stack Plain(ItemInfo a_item) { return Stack{ std::move(a_item), std::string{} }; }
	Stack Keyed(ItemInfo a_item, std::string a_key) { return Stack{ std::move(a_item), std::move(a_key) }; }
	Stack Unknown(ItemInfo a_item) { return Stack{ std::move(a_item), std::nullopt }; }

	ItemInfo Item(FormRef a_form, ItemType a_type, std::int32_t a_count, std::uint32_t a_stackId = 0)
	{
		return MakeItem(std::move(a_form), a_type, a_count, a_stackId);
	}

	RecipeIndex VanillaIndex()
	{
		RecipeIndex index;
		index.Add({ "CraftingSmelter", { kIronOre }, kIronIngot });
		index.Add({ "CraftingSmelter", { kIronIngot, kCorundum }, kSteelIngot });
		index.Add({ "CraftingTanningRack", { kLeather }, kStrips });
		index.Add({ "CraftingSmithingForge", { kSteelIngot, kStrips }, Sk(0x13989) });
		index.Add({ "CraftingSmithingForge", { kCrossbow, kSteelIngot }, Sk(0x800F19) });
		index.Add({ "CraftingCookpot", { kWheat }, Sk(0x65C9F) });
		return index;
	}

	SupplyRequest At(const RecipeIndex& a_index, std::vector<std::string> a_keywords, Station a_station = Station::Recipes, bool a_gear = false)
	{
		return SupplyRequest{ a_station, a_index.MaterialsFor(a_keywords), a_gear };
	}
}

TEST_CASE("Recipe index groups components by bench keyword", "[crafting]")
{
	auto index = VanillaIndex();
	index.Add({ "", { kIronOre }, std::nullopt });              // no bench: ignored
	index.Add({ "craftingsmelter", { kModOre }, kIronIngot });  // modded material, keyword in another case
	index.Add({ "CraftingSmelter", { FormRef{} }, std::nullopt });

	CHECK(index.Recipes() == 8);
	CHECK(index.WithoutBench() == 1);
	CHECK(index.Benches() == 4);
	CHECK(index.RecipesAt("CRAFTINGSMELTER") == 4);
	CHECK(index.RecipesAt("CraftingAlchemyWorkbench") == 0);

	const auto smelter = index.MaterialsFor(std::vector<std::string>{ "CraftingSmelter" });
	CHECK(smelter == std::unordered_set<FormRef>{ kIronOre, kIronIngot, kCorundum, kModOre });

	// A bench with several keywords gets the recipes of each.
	const auto both = index.MaterialsFor(std::vector<std::string>{ "CraftingTanningRack", "CraftingCookpot", "Unrelated" });
	CHECK(both == std::unordered_set<FormRef>{ kLeather, kWheat });
	CHECK(index.MaterialsFor(std::vector<std::string>{}).empty());
	CHECK(index.Materials() == 9);
}

TEST_CASE("Judge brings only movable, uncustomized materials of this station", "[crafting]")
{
	const auto index = VanillaIndex();
	const auto forge = At(index, { "CraftingSmithingForge" });

	CHECK(Judge(forge, Plain(Item(kSteelIngot, ItemType::Misc, 5))) == Verdict::Bring);
	CHECK(Judge(forge, Keyed(Item(kSteelIngot, ItemType::Misc, 5), "owner=00013BBF")) == Verdict::Bring);
	CHECK(Judge(forge, Plain(Item(kIronOre, ItemType::Misc, 5))) == Verdict::NotMaterial);

	auto quest = Item(kSteelIngot, ItemType::Misc, 1);
	quest.questItem = true;
	CHECK(Judge(forge, Plain(quest)) == Verdict::Unmovable);
	auto nameless = Item(kSteelIngot, ItemType::Misc, 1);
	nameless.name.clear();
	CHECK(Judge(forge, Plain(nameless)) == Verdict::Unmovable);
	auto returns = Item(kSteelIngot, ItemType::Misc, 1);
	returns.returns = true;
	CHECK(Judge(forge, Plain(returns)) == Verdict::Unmovable);
	auto fav = Item(kSteelIngot, ItemType::Misc, 1);
	fav.favorited = true;
	CHECK(Judge(forge, Plain(fav)) == Verdict::Protected);
	auto worn = Item(kSteelIngot, ItemType::Misc, 1);
	worn.equipped = true;
	CHECK(Judge(forge, Plain(worn)) == Verdict::Protected);
	auto tempered = Item(kSteelIngot, ItemType::Misc, 1);
	tempered.customized = true;
	CHECK(Judge(forge, Plain(tempered)) == Verdict::Customized);
	CHECK(Judge(forge, Unknown(Item(kSteelIngot, ItemType::Misc, 1))) == Verdict::Customized);
	CHECK(Judge(forge, Plain(Item(FormRef{}, ItemType::Misc, 1))) == Verdict::Dynamic);

	SECTION("weapons, armor and ammo used as materials only with gear on")
	{
		CHECK(Judge(forge, Plain(Item(kCrossbow, ItemType::Weapon, 1))) == Verdict::Gear);
		const auto withGear = At(index, { "CraftingSmithingForge" }, Station::Recipes, true);
		CHECK(Judge(withGear, Plain(Item(kCrossbow, ItemType::Weapon, 1))) == Verdict::Bring);
		auto enchanted = Item(kCrossbow, ItemType::Weapon, 1);
		enchanted.customized = true;
		CHECK(Judge(withGear, Plain(enchanted)) == Verdict::Customized);
	}

	SECTION("alchemy lab: every ingredient")
	{
		const auto lab = At(index, { "CraftingAlchemyWorkbench" }, Station::Alchemy);
		CHECK_FALSE(lab.Empty());
		CHECK(Judge(lab, Plain(Item(kBlueMountain, ItemType::Ingredient, 3))) == Verdict::Bring);
		CHECK(Judge(lab, Plain(Potion(Sk(0x3EADE), 2, 30))) == Verdict::NotMaterial);
		CHECK(Judge(lab, Plain(Item(FormRef{}, ItemType::Ingredient, 1))) == Verdict::Dynamic);
	}

	SECTION("enchanter: every filled soul gem")
	{
		const auto enchanter = At(index, { "DLC2StaffEnchanter-not-here" }, Station::Enchanting);
		auto       filled = Item(kGrandFilled, ItemType::SoulGem, 2);
		filled.soulGemState = SoulGemState::Filled;
		auto empty = Item(Sk(0x2E4FC), ItemType::SoulGem, 2);
		empty.soulGemState = SoulGemState::Empty;
		CHECK(Judge(enchanter, Keyed(filled, "soul=5")) == Verdict::Bring);
		CHECK(Judge(enchanter, Plain(empty)) == Verdict::NotMaterial);
	}
}

TEST_CASE("PlanPull takes whole stacks from every stored container", "[crafting]")
{
	const auto index = VanillaIndex();
	const auto smelter = At(index, { "CraftingSmelter" });

	std::vector<Stored> stored{
		{ kChest, { Plain(Item(kIronOre, ItemType::Misc, 20, 1)), Plain(Item(kLeather, ItemType::Misc, 4, 2)), Keyed(Item(kIronOre, ItemType::Misc, 3, 3), "owner=1") } },
		{ kSack, { Plain(Item(kCorundum, ItemType::Misc, 7, 4)), Unknown(Item(kCorundum, ItemType::Misc, 1, 5)) } },
	};
	const auto pulls = PlanPull(smelter, stored);
	REQUIRE(pulls.size() == 3);
	CHECK(pulls[0].move == Move{ 1, kIronOre, 20, ContainerRef(kChest), ContainerRef::Player() });
	CHECK(pulls[0].key.empty());
	CHECK(pulls[1].move == Move{ 3, kIronOre, 3, ContainerRef(kChest), ContainerRef::Player() });
	CHECK(pulls[1].key == "owner=1");
	CHECK(pulls[2].move == Move{ 4, kCorundum, 7, ContainerRef(kSack), ContainerRef::Player() });

	SECTION("a station without recipes brings nothing")
	{
		const auto chair = At(index, { "FurnitureSitting" });
		CHECK(chair.Empty());
		CHECK(PlanPull(chair, stored).empty());
	}
}

TEST_CASE("MakeLedger records what really left each container", "[crafting]")
{
	const auto index = VanillaIndex();
	const auto smelter = At(index, { "CraftingSmelter" });

	const std::vector<Stored> before{
		{ kChest, { Plain(Item(kIronOre, ItemType::Misc, 20, 1)), Plain(Item(kCorundum, ItemType::Misc, 2, 2)) } },
		{ kSack, { Plain(Item(kIronOre, ItemType::Misc, 5, 3)) } },
	};
	const auto pulls = PlanPull(smelter, before);
	REQUIRE(pulls.size() == 3);

	// The chest's ore moved, its corundum did not (the mover skipped it), the sack's ore moved.
	const std::vector<Stored> after{
		{ kChest, { Plain(Item(kCorundum, ItemType::Misc, 2, 2)) } },
		{ kSack, {} },
	};
	const Counts playerBefore{ { StackKey{ kIronOre, "" }, 4 }, { StackKey{ kLeather, "" }, 9 } };
	const auto   ledger = MakeLedger(pulls, before, after, playerBefore);

	REQUIRE(ledger.pulled.size() == 2);
	CHECK(ledger.pulled[0].form == kIronOre);
	CHECK(ledger.pulled[0].from == kChest);
	CHECK(ledger.pulled[0].count == 20);
	CHECK(ledger.pulled[1].from == kSack);
	CHECK(ledger.pulled[1].count == 5);
	CHECK(ledger.Total() == 25);
	CHECK(ledger.before == Counts{ { StackKey{ kIronOre, "" }, 4 } });
	CHECK(ledger.Keys() == std::vector<StackKey>{ StackKey{ kIronOre, "" } });

	SECTION("never more than planned, even if more left the container")
	{
		const std::vector<Stored> drained{ { kChest, {} }, { kSack, {} } };
		const std::vector<Stored> bigger{
			{ kChest, { Plain(Item(kIronOre, ItemType::Misc, 50, 1)), Plain(Item(kCorundum, ItemType::Misc, 2, 2)) } },
			{ kSack, { Plain(Item(kIronOre, ItemType::Misc, 5, 3)) } },
		};
		const auto l = MakeLedger(pulls, bigger, drained, {});
		CHECK(l.Total() == 20 + 2 + 5);
	}
}

TEST_CASE("CountStacks counts only copies that could move", "[crafting]")
{
	auto fav = Item(kIronIngot, ItemType::Misc, 2);
	fav.favorited = true;
	auto tempered = Item(kIronIngot, ItemType::Misc, 1);
	tempered.customized = true;
	const std::vector<Stack> stacks{
		Plain(Item(kIronIngot, ItemType::Misc, 3)),
		Keyed(Item(kIronIngot, ItemType::Misc, 2), "owner=1"),
		Plain(fav),
		Plain(tempered),
		Unknown(Item(kIronIngot, ItemType::Misc, 4)),
		Plain(Item(kIronIngot, ItemType::Misc, 1)),
	};
	const auto counts = CountStacks(stacks);
	CHECK(counts == Counts{ { StackKey{ kIronIngot, "" }, 4 }, { StackKey{ kIronIngot, "owner=1" }, 2 } });
}

namespace
{
	Ledger OneLedger(FormRef a_form, std::int32_t a_pulled, std::int32_t a_before, std::string a_key = {})
	{
		Ledger l;
		l.pulled.push_back(Pulled{ a_form, a_key, kChest, a_pulled });
		l.before[StackKey{ a_form, a_key }] = a_before;
		return l;
	}

	std::int32_t BackOf(const ReturnPlan& a_plan, const FormRef& a_form)
	{
		std::int32_t n = 0;
		for (const auto& b : a_plan.back) {
			if (b.form == a_form) {
				n += b.count;
			}
		}
		return n;
	}
}

TEST_CASE("PlanReturn: what was used up stays used, the player keeps their own copies", "[crafting]")
{
	SECTION("nothing used: everything brought goes back, the player's own copies stay")
	{
		const auto plan = PlanReturn(OneLedger(kSteelIngot, 10, 2), Counts{ { StackKey{ kSteelIngot, "" }, 12 } }, {});
		CHECK(plan.returned == 10);
		CHECK(plan.pulled == 10);
		REQUIRE(plan.back.size() == 1);
		CHECK(plan.back[0].to == kChest);
		CHECK(plan.back[0].count == 10);
	}
	SECTION("some used: used copies count against the brought ones first")
	{
		// 2 own + 10 brought - 5 used = 7 -> 5 back, 2 stay.
		const auto plan = PlanReturn(OneLedger(kSteelIngot, 10, 2), Counts{ { StackKey{ kSteelIngot, "" }, 7 } }, {});
		CHECK(plan.returned == 5);
	}
	SECTION("more used than brought: nothing goes back, the rest stays")
	{
		const auto plan = PlanReturn(OneLedger(kSteelIngot, 10, 2), Counts{ { StackKey{ kSteelIngot, "" }, 1 } }, {});
		CHECK(plan.returned == 0);
		CHECK(plan.back.empty());
	}
	SECTION("all used")
	{
		const auto plan = PlanReturn(OneLedger(kSteelIngot, 10, 0), {}, {});
		CHECK(plan.returned == 0);
		REQUIRE(plan.lines.size() == 1);
		CHECK(plan.lines[0].now == 0);
	}
	SECTION("more than expected (a script added some): at most what was brought")
	{
		const auto plan = PlanReturn(OneLedger(kSteelIngot, 10, 0), Counts{ { StackKey{ kSteelIngot, "" }, 30 } }, {});
		CHECK(plan.returned == 10);
	}
}

TEST_CASE("PlanReturn never returns what was just crafted", "[crafting]")
{
	SECTION("smelting: iron ingots crafted from ore and also brought as a material")
	{
		// Brought 10 iron ingots (0 own). Smelted 3 more from ore, used 4 for steel: 9 now. 6 go back, the 3
		// crafted stay.
		Ledger l = OneLedger(kIronIngot, 10, 0);
		l.pulled.push_back(Pulled{ kIronOre, "", kChest, 6 });
		l.before[StackKey{ kIronOre, "" }] = 0;
		const auto plan = PlanReturn(l, Counts{ { StackKey{ kIronIngot, "" }, 9 }, { StackKey{ kIronOre, "" }, 0 } },
			{ { kIronIngot, 3 }, { kSteelIngot, 4 } });
		CHECK(BackOf(plan, kIronIngot) == 6);
		CHECK(BackOf(plan, kIronOre) == 0);
		CHECK(plan.returned == 6);
		const auto it = std::ranges::find_if(plan.lines, [](const ReturnLine& x) { return x.stack.form == kIronIngot; });
		REQUIRE(it != plan.lines.end());
		CHECK(it->crafted == 3);
		CHECK(it->before == 0);
		CHECK(it->pulled == 10);
		CHECK(it->now == 9);
		CHECK(it->back == 6);
	}
	SECTION("crafted and nothing used: the crafted copies stay with the player")
	{
		const auto plan = PlanReturn(OneLedger(kStrips, 4, 1), Counts{ { StackKey{ kStrips, "" }, 9 } }, { { kStrips, 4 } });
		CHECK(plan.returned == 4);  // 1 own + 4 crafted stay
	}
	SECTION("crafted copies are counted against the plain stack first, then keyed ones")
	{
		Ledger l;
		l.pulled.push_back(Pulled{ kIronIngot, "", kChest, 2 });
		l.pulled.push_back(Pulled{ kIronIngot, "owner=1", kChest, 3 });
		const auto plan = PlanReturn(l, Counts{ { StackKey{ kIronIngot, "" }, 3 }, { StackKey{ kIronIngot, "owner=1" }, 3 } }, { { kIronIngot, 4 } });
		// Plain: 3 now, 3 crafted -> 0 back. Keyed: 3 now, 1 crafted left -> 2 back.
		CHECK(plan.returned == 2);
		REQUIRE(plan.back.size() == 1);
		CHECK(plan.back[0].key == "owner=1");
		CHECK(plan.back[0].count == 2);
	}
}

TEST_CASE("PlanReturn sends copies back to the containers they came from", "[crafting]")
{
	Ledger l;
	l.pulled.push_back(Pulled{ kLeather, "", kChest, 4 });
	l.pulled.push_back(Pulled{ kLeather, "", kSack, 6 });
	l.before[StackKey{ kLeather, "" }] = 1;

	SECTION("all back")
	{
		const auto plan = PlanReturn(l, Counts{ { StackKey{ kLeather, "" }, 11 } }, {});
		REQUIRE(plan.back.size() == 2);
		CHECK(plan.back[0].to == kChest);
		CHECK(plan.back[0].count == 4);
		CHECK(plan.back[1].to == kSack);
		CHECK(plan.back[1].count == 6);
	}
	SECTION("partly used: the first container is filled first")
	{
		const auto plan = PlanReturn(l, Counts{ { StackKey{ kLeather, "" }, 6 } }, {});
		REQUIRE(plan.back.size() == 2);
		CHECK(plan.back[0].count == 4);
		CHECK(plan.back[1].count == 1);
		CHECK(plan.returned == 5);
	}
}

TEST_CASE("ReturnMoves moves matching player stacks only", "[crafting]")
{
	auto fav = Item(kLeather, ItemType::Misc, 5, 2);
	fav.favorited = true;
	const std::vector<Stack> player{
		Plain(fav),
		Plain(Item(kLeather, ItemType::Misc, 7, 3)),
		Keyed(Item(kLeather, ItemType::Misc, 2, 4), "owner=1"),
		Plain(Item(kIronIngot, ItemType::Misc, 9, 5)),
	};
	const std::vector<Back> back{
		{ kLeather, "", kChest, 4 },
		{ kLeather, "", kSack, 6 },     // only 3 plain copies left after the first: moves 3
		{ kLeather, "owner=1", kSack, 1 },
		{ kSteelIngot, "", kChest, 2 },  // none on the player
	};
	const auto moves = ReturnMoves(back, player);
	REQUIRE(moves.size() == 3);
	CHECK(moves[0] == Move{ 3, kLeather, 4, ContainerRef::Player(), ContainerRef(kChest) });
	CHECK(moves[1] == Move{ 3, kLeather, 3, ContainerRef::Player(), ContainerRef(kSack) });
	CHECK(moves[2] == Move{ 4, kLeather, 1, ContainerRef::Player(), ContainerRef(kSack) });
}
