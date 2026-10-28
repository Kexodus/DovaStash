#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

#include "TestHelpers.h"

#include "dova/core/Sell.h"

using namespace dova::core;
using namespace dova::test;
namespace sell = dova::core::sell;

namespace
{
	const FormRef kCell = Sk(0x165A7);
	const FormRef kMaster = Sk(0xA00);
	const FormRef kChest = Sk(0xA01);
	const FormRef kSellChest = Sk(0xA02);

	sell::Barter Speech(float a_speech)
	{
		sell::Barter b;
		b.speech = a_speech;
		return b;
	}

	KeepRules Rules(const KeepConfig& a_global = DefaultKeepConfig(), const KeepConfig* a_layout = nullptr)
	{
		Diagnostics diags;
		return KeepRules::Compile(a_global, a_layout, diags);
	}

	sell::Stack Plain(ItemInfo a_item, std::int32_t a_value)
	{
		a_item.value = a_value;
		return sell::Stack{ std::move(a_item), sell::Copy::Plain };
	}

	ItemInfo Named(ItemInfo a_item, std::string a_name, std::uint32_t a_stackId)
	{
		a_item.name = std::move(a_name);
		a_item.stackId = a_stackId;
		return a_item;
	}

	Layout SellLayout()
	{
		Layout layout;
		layout.cell = kCell;
		layout.master = kMaster;
		ContainerBinding chest;
		chest.ref = kChest;
		chest.categories = { "weapons.*" };
		ContainerBinding junk;
		junk.ref = kSellChest;
		junk.label = "Sell chest";
		junk.categories = { "misc.clutter" };
		junk.sell = true;
		layout.containers = { chest, junk };
		return layout;
	}
}

TEST_CASE("Sell price: the vanilla barter formula", "[sell]")
{
	// fBarterMax - (fBarterMax - fBarterMin) * Speech / 100: 3.3 at Speech 0, 3.105 at 15, 2.0 at 100.
	CHECK(std::abs(sell::PriceFactor(Speech(0)) - 3.3) < 1e-6);
	CHECK(std::abs(sell::PriceFactor(Speech(15)) - 3.105) < 1e-6);
	CHECK(std::abs(sell::PriceFactor(Speech(100)) - 2.0) < 1e-6);
	CHECK(std::abs(sell::PriceFactor(Speech(250)) - 2.0) < 1e-6);  // Fortify Speech past 100 changes nothing here

	CHECK(sell::UnitPrice(100, Speech(0)) == 30);   // 100 / 3.3 = 30.3
	CHECK(sell::UnitPrice(100, Speech(15)) == 32);  // 100 / 3.105 = 32.2
	CHECK(sell::UnitPrice(100, Speech(100)) == 50);
	CHECK(sell::UnitPrice(1000, Speech(50)) == 377);  // 1000 / 2.65 = 377.4

	SECTION("perks and Fortify Barter")
	{
		auto b = Speech(100);
		b.perks = 1.3f;  // Haggling 5/5
		CHECK(sell::UnitPrice(100, b) == 65);
		b.fortify = 20.0f;  // +20%
		CHECK(sell::UnitPrice(100, b) == 78);
	}
	SECTION("fBarterSellMax caps what the game side pays")
	{
		auto b = Speech(100);
		b.perks = 2.0f;
		CHECK(sell::UnitPrice(100, b) == 100);  // never more than the value
		b.sellMax = 0.6f;
		CHECK(sell::UnitPrice(100, b) == 60);
	}
	SECTION("the player's percentage comes on top")
	{
		auto b = Speech(100);
		b.percent = 50;
		CHECK(sell::UnitPrice(100, b) == 25);
		b.percent = 0;
		CHECK(sell::UnitPrice(100, b) == 0);
	}
	SECTION("cheap and worthless items")
	{
		CHECK(sell::UnitPrice(1, Speech(15)) == 1);  // at least 1 for something worth something
		CHECK(sell::UnitPrice(0, Speech(15)) == 0);
		CHECK(sell::UnitPrice(-5, Speech(15)) == 0);
	}
	SECTION("broken game settings fall back to vanilla")
	{
		auto b = Speech(15);
		b.barterMin = std::numeric_limits<float>::quiet_NaN();
		b.barterMax = 0.0f;
		b.sellMax = std::numeric_limits<float>::infinity();
		b.perks = std::numeric_limits<float>::quiet_NaN();
		CHECK(sell::UnitPrice(100, b) == 32);
	}
}

TEST_CASE("Sell chest: what is never sold", "[sell]")
{
	const auto set = CompileCategories(kStandardCategories);
	const auto keep = Rules();
	const auto b = Speech(15);
	auto       judge = [&](sell::Stack a_stack) { return sell::Judge(a_stack, keep, set, b); };

	CHECK(judge(Plain(Misc(Sk(0x100), 3), 10)) == sell::Verdict::Sell);

	auto empty = Plain(Misc(Sk(0x100), 0), 10);
	CHECK(judge(empty) == sell::Verdict::Empty);
	auto quest = Plain(Misc(Sk(0x100)), 10);
	quest.item.questItem = true;
	CHECK(judge(quest) == sell::Verdict::Quest);
	auto token = Plain(Misc(Sk(0x100)), 10);
	token.item.name.clear();
	CHECK(judge(token) == sell::Verdict::NonPlayable);
	auto returns = Plain(Misc(Sk(0x100)), 10);
	returns.item.returns = true;
	CHECK(judge(returns) == sell::Verdict::Returns);
	auto favorited = Plain(Weapon(Sk(0x101), WeaponType::OneHandSword), 10);
	favorited.item.favorited = true;
	CHECK(judge(favorited) == sell::Verdict::Protected);
	CHECK(judge(Plain(Misc(kGoldForm, 500), 1)) == sell::Verdict::Gold);
	CHECK(judge(Plain(Misc(FormRef{}, 1), 50)) == sell::Verdict::Dynamic);  // a potion the player made
	auto tempered = Plain(Weapon(Sk(0x101), WeaponType::OneHandSword), 10);
	tempered.item.customized = true;
	CHECK(judge(tempered) == sell::Verdict::Customized);
	CHECK(judge(sell::Stack{ Misc(Sk(0x102)), sell::Copy::Special }) == sell::Verdict::Customized);
	auto stolen = Plain(Misc(Sk(0x102)), 10);
	stolen.copy = sell::Copy::Stolen;
	CHECK(judge(stolen) == sell::Verdict::Stolen);
	CHECK(judge(Plain(Misc(kLockpickForm, 20), 2)) == sell::Verdict::Kept);  // the default keep rules
	CHECK(judge(Plain(Misc(Sk(0x103)), 0)) == sell::Verdict::Worthless);

	SECTION("keep rules: a layout's always protects, a layout's never overrides a global always")
	{
		KeepConfig layoutKeep;
		layoutKeep.always = { "misc.gems" };
		layoutKeep.never = { kLockpickForm.ToString() };
		const auto  rules = Rules(DefaultKeepConfig(), &layoutKeep);
		std::string why;
		CHECK(sell::Judge(Plain(Misc(Sk(0x104), 1, { "VendorItemGem" }), 100), rules, set, b, &why) == sell::Verdict::Kept);
		CHECK(why == "always misc.gems (layout)");
		CHECK(sell::Judge(Plain(Misc(kLockpickForm, 20), 2), rules, set, b) == sell::Verdict::Sell);
		CHECK(sell::Judge(Plain(Misc(kTorchForm, 2), 2), rules, set, b) == sell::Verdict::Kept);  // global
	}
	SECTION("quotas say how many to carry, not what may be sold")
	{
		KeepConfig global;
		global.quotas.push_back(QuotaRule{ .category = std::string("alchemy.potions"), .quota = 5 });
		CHECK(sell::Judge(Plain(Potion(Sk(0x105), 8, 25), 25), Rules(global), set, b) == sell::Verdict::Sell);
	}
}

TEST_CASE("Sell chest: the plan", "[sell]")
{
	const auto set = CompileCategories(kStandardCategories);
	const auto keep = Rules();
	auto       stolen = Plain(Named(Misc(Sk(0x202), 2), "Silver Goblet", 3), 40);
	stolen.copy = sell::Copy::Stolen;
	const std::vector<sell::Stack> stacks{
		Plain(Named(Misc(Sk(0x200), 5), "Tankard", 1), 3),
		Plain(Named(Weapon(Sk(0x201), WeaponType::OneHandDagger, 2), "Iron Dagger", 2), 10),
		stolen,
		Plain(Named(Misc(kLockpickForm, 9), "Lockpick", 4), 2),
		Plain(Named(Misc(Sk(0x200), 1), "Tankard", 5), 3),
	};
	const auto plan = sell::PlanSale(stacks, keep, set, Speech(100));
	REQUIRE(plan.lines.size() == 3);
	CHECK(plan.lines[0].stackId == 1);
	CHECK(plan.lines[0].unitPrice == 1);  // 3 / 2 = 1.5
	CHECK(plan.lines[1].stackId == 2);
	CHECK(plan.lines[1].Total() == 10);
	CHECK(plan.lines[2].stackId == 5);
	CHECK(plan.items == 8);
	CHECK(plan.gold == 5 + 10 + 1);
	CHECK(plan.stays.at(sell::Verdict::Stolen) == 2);
	CHECK(plan.stays.at(sell::Verdict::Kept) == 9);
	CHECK(plan.verdicts == std::vector<sell::Verdict>{ sell::Verdict::Sell, sell::Verdict::Sell, sell::Verdict::Stolen, sell::Verdict::Kept, sell::Verdict::Sell });
	CHECK(plan.why[3] == "always Skyrim.esm|0x0000000A (global)");
	CHECK(plan.Sold() == sell::Counts{ { Sk(0x200), 6 }, { Sk(0x201), 2 } });
	CHECK(sell::Describe(plan, 1) == "Iron Dagger x2 +1 more");
	CHECK(sell::Describe(plan, 5) == "Iron Dagger x2, Tankard x6");

	SECTION("perk entries per item come on top of the global ones")
	{
		auto perked = stacks;
		perked[1].perks = 1.2f;  // a perk that names daggers
		const auto p = sell::PlanSale(perked, keep, set, Speech(100));
		REQUIRE(p.lines.size() == 3);
		CHECK(p.lines[1].unitPrice == 6);  // 10 * 1.2 / 2
		CHECK(p.lines[0].unitPrice == 1);
	}
	SECTION("a limit sells at most that many of each form, in stack order")
	{
		const sell::Counts limit{ { Sk(0x200), 2 } };
		const auto         limited = sell::PlanSale(stacks, keep, set, Speech(100), &limit);
		REQUIRE(limited.lines.size() == 1);
		CHECK(limited.lines[0].stackId == 1);
		CHECK(limited.lines[0].count == 2);
		CHECK(limited.items == 2);
		CHECK(limited.stays.at(sell::Verdict::Sell) == 3 + 2 + 1);  // tankards past the limit, the daggers, the last tankard
	}
	SECTION("nothing to sell")
	{
		const auto none = sell::PlanSale(std::span<const sell::Stack>{}, keep, set, Speech(100));
		CHECK(none.Empty());
		CHECK(none.gold == 0);
		CHECK(sell::Describe(none, 3).empty());
	}
}

TEST_CASE("Timed selling: only what has been there the whole period", "[sell]")
{
	const auto a = Sk(0x300);
	const auto b = Sk(0x301);
	constexpr double kDay = 24.0;

	// First look: nothing is due, the clock starts.
	auto step = sell::StepAging(std::nullopt, { { a, 5 } }, 10.0, kDay);
	CHECK_FALSE(step.due);
	CHECK(step.next.since == 10.0);
	CHECK(step.next.counts == sell::Counts{ { a, 5 } });

	// Half a day later: 2 taken out, 3 of another arrived. Not due; the arrivals are not counted as old.
	step = sell::StepAging(step.next, { { a, 3 }, { b, 3 } }, 10.5, kDay);
	CHECK_FALSE(step.due);
	CHECK(step.next.since == 10.0);
	CHECK(step.next.counts == sell::Counts{ { a, 3 } });

	// A day after the first look: the 3 old ones are sold, the rest starts its own day.
	step = sell::StepAging(step.next, { { a, 4 }, { b, 3 } }, 11.0, kDay);
	CHECK(step.due);
	CHECK(step.sell == sell::Counts{ { a, 3 } });
	CHECK(step.next.since == 11.0);
	CHECK(step.next.counts == sell::Counts{ { a, 1 }, { b, 3 } });

	SECTION("everything old was taken out: nothing due, a fresh start")
	{
		const auto empty = sell::StepAging(sell::Aging{ 1.0, { { a, 2 } } }, { { b, 1 } }, 5.0, kDay);
		CHECK_FALSE(empty.due);
		CHECK(empty.sell.empty());
		CHECK(empty.next.since == 5.0);
		CHECK(empty.next.counts == sell::Counts{ { b, 1 } });
	}
	SECTION("the clock went back (another save): start again")
	{
		const auto back = sell::StepAging(sell::Aging{ 9.0, { { a, 2 } } }, { { a, 2 } }, 3.0, kDay);
		CHECK_FALSE(back.due);
		CHECK(back.next.since == 3.0);
	}
	SECTION("a shorter period")
	{
		const auto hour = sell::StepAging(sell::Aging{ 1.0, { { a, 2 } } }, { { a, 2 } }, 1.0 + 1.0 / 24.0, 1.0);
		CHECK(hour.due);
		CHECK(hour.sell == sell::Counts{ { a, 2 } });
	}
}

TEST_CASE("Sell chest in the layout file", "[sell][layout]")
{
	constexpr std::string_view kJson = R"({
		"$schema": "dovastash-layout-1",
		"name": "Test",
		"cell": "Skyrim.esm|0x000165A7",
		"master": "Skyrim.esm|0x00000A00",
		"containers": [
			{ "ref": "Skyrim.esm|0x00000A02", "label": "Junk", "categories": ["misc.clutter"], "sell": true },
			{ "ref": "Skyrim.esm|0x00000A03", "sell": true, "ignored": true },
			{ "ref": "Skyrim.esm|0x00000A04", "sell": "yes" }
		]
	})";
	Diagnostics diags;
	const auto  layout = ParseLayoutText(kJson, "test.json", diags);
	REQUIRE(layout);
	CHECK(layout->containers[0].sell);
	CHECK(layout->IsSellChest(Sk(0xA02)));
	CHECK_FALSE(layout->IsSellChest(Sk(0xA03)));  // ignored: inert
	CHECK_FALSE(layout->IsSellChest(Sk(0xA04)));  // wrong type: kept verbatim, not used
	CHECK(diags.HasErrors());
	CHECK(layout->HasSellChest());

	const auto text = SerializeLayout(*layout);
	CHECK(text.find("\"sell\": true") != std::string::npos);
	CHECK(text.find("\"sell\": \"yes\"") != std::string::npos);
	Diagnostics again;
	const auto  reloaded = ParseLayoutText(text, "test.json", again);
	REQUIRE(reloaded);
	CHECK(SerializeLayout(*reloaded) == text);

	SECTION("never the master")
	{
		constexpr std::string_view kMasterSell = R"({
			"$schema": "dovastash-layout-1",
			"cell": "Skyrim.esm|0x000165A7",
			"master": "Skyrim.esm|0x00000A00",
			"containers": [ { "ref": "Skyrim.esm|0x00000A00", "sell": true } ]
		})";
		Diagnostics d;
		const auto  l = ParseLayoutText(kMasterSell, "test.json", d);
		REQUIRE(l);
		CHECK_FALSE(l->IsSellChest(Sk(0xA00)));
		CHECK_FALSE(l->HasSellChest());
		CHECK(std::ranges::any_of(d.Items(), [](const Diagnostic& x) { return x.message.find("sell chest") != std::string::npos; }));
	}
	SECTION("never an area master")
	{
		constexpr std::string_view kAreaSell = R"({
			"$schema": "dovastash-layout-1",
			"cell": "Skyrim.esm|0x000165A7",
			"master": "Skyrim.esm|0x00000A00",
			"cells": [ { "cell": "Skyrim.esm|0x000165A8", "label": "Cellar", "master": "Skyrim.esm|0x00000A05" } ],
			"containers": [ { "ref": "Skyrim.esm|0x00000A05", "sell": true } ]
		})";
		Diagnostics d;
		const auto  l = ParseLayoutText(kAreaSell, "test.json", d);
		REQUIRE(l);
		CHECK_FALSE(l->IsSellChest(Sk(0xA05)));
		CHECK_FALSE(l->HasSellChest());
		CHECK(std::ranges::any_of(d.Items(), [](const Diagnostic& x) {
			return x.message.find("cells[0].master: an area master can't be the sell chest") != std::string::npos;
		}));
	}
	SECTION("a plain container writes no sell key")
	{
		Layout plain;
		plain.cell = kCell;
		ContainerBinding c;
		c.ref = kChest;
		plain.containers.push_back(c);
		CHECK(SerializeLayout(plain).find("sell") == std::string::npos);
	}
}

TEST_CASE("Sell chest and the sort planner", "[sell][planner]")
{
	const auto        set = CompileCategories(kStandardCategories);
	const auto        layout = SellLayout();
	const SortPlanner planner(layout, set);

	SECTION("Deposit routes items there by the category engine")
	{
		auto       clutter = Misc(Sk(0x400), 3);
		clutter.stackId = 7;
		const auto moves = planner.Deposit(std::vector<ItemInfo>{ clutter }, Rules(), nullptr);
		REQUIRE(moves.size() == 1);
		CHECK(moves[0].to == ContainerRef(kSellChest));
	}
	SECTION("Sort all never empties it: what was put there by hand stays")
	{
		auto sword = Weapon(Sk(0x401), WeaponType::OneHandSword);
		sword.stackId = 1;
		auto clutter = Misc(Sk(0x402));
		clutter.stackId = 2;
		const std::vector<ContainerContents> contents{ { kSellChest, { sword } }, { kMaster, { clutter } } };
		const auto                           moves = planner.SortAll(contents);
		REQUIRE(moves.size() == 1);
		CHECK(moves[0].stackId == 2);
		CHECK(moves[0].to == ContainerRef(kSellChest));
		CHECK_FALSE(planner.IsSortSource(kSellChest));
		CHECK(planner.IsManaged(kSellChest));
	}
	SECTION("Retrieve never takes from it")
	{
		auto clutter = Misc(Sk(0x403));
		clutter.stackId = 3;
		auto sword = Weapon(Sk(0x404), WeaponType::OneHandSword);
		sword.stackId = 4;
		const std::vector<ContainerContents> contents{ { kSellChest, { clutter } }, { kChest, { sword } } };
		const auto                           moves = planner.Retrieve(contents, "*");
		REQUIRE(moves.size() == 1);
		CHECK(moves[0].stackId == 4);
	}
	SECTION("learning into it works; a category is never suggested for it")
	{
		auto copy = layout;
		auto sword = Weapon(Sk(0x405), WeaponType::OneHandSword);
		CHECK(Learn(copy, sword, kSellChest, set) == LearnResult::Recorded);
		const SortPlanner learned(copy, set);
		const auto        dest = learned.ResolveDestination(sword);
		REQUIRE(dest);
		CHECK(dest->container == kSellChest);
		const LearnedCategories counts{ { "weapons.swords", 5 } };
		CHECK_FALSE(SuggestCategory(copy, kSellChest, counts, 3));
	}
	SECTION("a variant learned into it sends neither its root nor the root's other variants")
	{
		auto copy = layout;
		auto root = Weapon(Sk(0x406), WeaponType::OneHandSword);
		auto debilitation = Weapon(Sk(0x407), WeaponType::OneHandSword);
		debilitation.family = root.form;
		auto burning = Weapon(Sk(0x408), WeaponType::OneHandSword);
		burning.family = root.form;
		CHECK(Learn(copy, debilitation, kSellChest, set) == LearnResult::Recorded);
		CHECK(copy.learned.contains(debilitation.form));
		CHECK_FALSE(copy.learned.contains(root.form));
		const SortPlanner learned(copy, set);
		CHECK(learned.ResolveDestination(debilitation)->container == kSellChest);
		CHECK(learned.ResolveDestination(root)->container == kChest);
		CHECK(learned.ResolveDestination(burning)->container == kChest);
	}
	SECTION("a variant learned into it leaves the family's entry alone")
	{
		auto copy = layout;
		auto root = Weapon(Sk(0x409), WeaponType::OneHandSword);
		auto variant = Weapon(Sk(0x40A), WeaponType::OneHandSword);
		variant.family = root.form;
		copy.learned.emplace(root.form, kMaster);
		CHECK(Learn(copy, variant, kSellChest, set) == LearnResult::Recorded);
		CHECK(copy.learned.at(root.form) == kMaster);
		CHECK(copy.learned.at(variant.form) == kSellChest);
	}
	SECTION("a root learned or bound into it does not send its variants")
	{
		auto copy = layout;
		auto root = Weapon(Sk(0x40B), WeaponType::OneHandSword);
		auto variant = Weapon(Sk(0x40C), WeaponType::OneHandSword);
		variant.family = root.form;
		CHECK(Learn(copy, root, kSellChest, set) == LearnResult::Recorded);
		const SortPlanner learned(copy, set);
		CHECK(learned.ResolveDestination(root)->container == kSellChest);
		CHECK(learned.ResolveDestination(variant)->container == kChest);

		auto bound = layout;
		bound.containers[1].forms.push_back(root.form);
		const SortPlanner explicitly(bound, set);
		CHECK(explicitly.ResolveDestination(root)->container == kSellChest);
		CHECK(explicitly.ResolveDestination(variant)->container == kChest);
	}
}

TEST_CASE("Sell chest and templates", "[sell][template]")
{
	TemplateCandidate t;
	t.source = "Templates/Test.json";
	t.layout = SellLayout();
	t.layout.version = "1.0.0";

	SECTION("a template's sell chest is applied")
	{
		const auto applied = ApplyTemplate(t);
		CHECK(applied.IsSellChest(kSellChest));
	}
	SECTION("the player's own sell chest does not survive a switch: the role comes from the template")
	{
		Layout mine;
		mine.cell = kCell;
		mine.master = kMaster;
		ContainerBinding own;
		own.ref = Sk(0xA09);
		own.sell = true;
		own.categories = { "misc.gems" };
		ContainerBinding chest;
		chest.ref = kChest;
		chest.sell = true;
		mine.containers = { own, chest };
		const auto applied = ApplyTemplate(t, &mine);
		CHECK_FALSE(applied.IsSellChest(Sk(0xA09)));
		CHECK_FALSE(applied.IsSellChest(kChest));
		CHECK(applied.IsSellChest(kSellChest));
	}
	SECTION("exported as a template, the sell chest stays")
	{
		const auto exported = ExportAsTemplate(SellLayout());
		CHECK(exported.IsSellChest(kSellChest));
	}
}

TEST_CASE("Sell chest: wins the tie against a storage container bound to the same category", "[sell]")
{
	// Most templates bind misc.clutter somewhere; the sell chest made afterwards binds it too. List order would
	// send every piece of clutter to the earlier storage container and the sell chest would stay empty.
	Layout layout = SellLayout();
	ContainerBinding clutter;
	clutter.ref = Sk(0xA03);
	clutter.label = "Guest room chest";
	clutter.categories = { "misc.clutter" };
	layout.containers.insert(layout.containers.begin(), clutter);  // before the sell chest in the list
	const auto   categories = CompileCategories(kStandardCategories);
	SortPlanner  planner(layout, categories);
	const auto   tankard = MakeItem(Sk(0x319E3), ItemType::Misc);
	const auto dest = planner.ResolveDestination(tankard);
	REQUIRE(dest.has_value());
	CHECK(dest->container == kSellChest);
	CHECK(dest->reason == Destination::Reason::Category);

	// A more specific pattern on the storage container still wins over the sell chest's broader one.
	layout.containers.front().categories = { "misc.clutter" };
	layout.containers.back().categories = { "misc.*" };
	SortPlanner planner2(layout, categories);
	const auto  dest2 = planner2.ResolveDestination(tankard);
	REQUIRE(dest2.has_value());
	CHECK(dest2->container == Sk(0xA03));
}
