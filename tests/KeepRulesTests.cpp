#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"

using namespace dova::core;
using namespace dova::test;

namespace
{
	std::vector<std::int32_t> Keep(const std::vector<ItemInfo>& a_inventory, const KeepConfig& a_global, const KeepConfig* a_layout = nullptr)
	{
		const auto  set = CompileCategories(kStandardCategories);
		Diagnostics diags;
		const auto  rules = KeepRules::Compile(a_global, a_layout, diags);
		CHECK_FALSE(diags.HasErrors());
		return rules.ComputeKeepCounts(a_inventory, set);
	}

	KeepConfig Quota(std::string a_category, std::int32_t a_quota)
	{
		KeepConfig cfg;
		cfg.quotas.push_back(QuotaRule{ .category = std::move(a_category), .quota = a_quota });
		return cfg;
	}
}

TEST_CASE("Default keeps: equipped, favorited, quest, gold, lockpicks, torches", "[keep]")
{
	auto equipped = Weapon(Sk(0x100), WeaponType::OneHandSword);
	equipped.equipped = true;
	auto favorited = Weapon(Sk(0x101), WeaponType::Bow);
	favorited.favorited = true;
	auto quest = Misc(Sk(0x102));
	quest.questItem = true;
	auto torch = MakeItem(kTorchForm, ItemType::Light, 3);

	const std::vector<ItemInfo> inv{
		equipped, favorited, quest,
		Misc(kGoldForm, 1500), Misc(kLockpickForm, 12), torch,
		Weapon(Sk(0x103), WeaponType::OneHandDagger, 2),
		Misc(Sk(0x104), 4),
	};
	CHECK(Keep(inv, DefaultKeepConfig()) == std::vector<std::int32_t>{ 1, 1, 1, 1500, 12, 3, 0, 0 });

	const auto defaults = DefaultKeepConfig();
	CHECK(defaults.always.size() == 6);
	CHECK(kGoldForm.ToString() == "Skyrim.esm|0x0000000F");
	CHECK(kLockpickForm.ToString() == "Skyrim.esm|0x0000000A");
	CHECK(kTorchForm.ToString() == "Skyrim.esm|0x0001D4EC");
}

TEST_CASE("Quest, non-playable, worn and favorited items are kept even with an empty config", "[keep]")
{
	auto quest = Misc(Sk(0x1));
	quest.questItem = true;
	auto unplayable = Misc(Sk(0x2), 5);
	unplayable.playable = false;
	auto equipped = Misc(Sk(0x3));
	equipped.equipped = true;
	auto favorited = Misc(Sk(0x4), 2);
	favorited.favorited = true;
	const auto other = Misc(Sk(0x5), 3);

	KeepConfig empty;
	empty.inherit = false;
	KeepConfig layout;
	layout.inherit = false;
	layout.never = { "*" };
	const std::vector<ItemInfo> inv{ quest, unplayable, equipped, favorited, other };
	CHECK(Keep(inv, empty) == std::vector<std::int32_t>{ 1, 5, 1, 2, 0 });
	CHECK(Keep(inv, DefaultKeepConfig(), &layout) == std::vector<std::int32_t>{ 1, 5, 1, 2, 0 });

	Diagnostics diags;
	const auto  rules = KeepRules::Compile(DefaultKeepConfig(), &layout, diags);
	const auto  categories = CompileCategories(kStandardCategories);
	std::string why;
	CHECK(rules.AlwaysKeeps(equipped, categories, &why));
	CHECK(why == "worn item");
	CHECK(rules.AlwaysKeeps(favorited, categories, &why));
	CHECK(why == "favorited item");
	CHECK_FALSE(rules.AlwaysKeeps(other, categories, &why));
}

TEST_CASE("Layout layer overrides the inherited global layer", "[keep]")
{
	const std::vector<ItemInfo> inv{ Misc(kGoldForm, 100), Misc(kLockpickForm, 5), Misc(Sk(0x50), 2) };

	SECTION("never in the layout beats always in the globals")
	{
		KeepConfig layout;
		layout.inherit = true;
		layout.never = { kGoldForm.ToString() };
		CHECK(Keep(inv, DefaultKeepConfig(), &layout) == std::vector<std::int32_t>{ 0, 5, 0 });
	}
	SECTION("always beats never inside one layer")
	{
		KeepConfig layout;
		layout.always = { "misc.clutter" };
		layout.never = { kGoldForm.ToString(), "misc.*" };
		CHECK(Keep(inv, DefaultKeepConfig(), &layout) == std::vector<std::int32_t>{ 100, 5, 2 });
	}
	SECTION("inherit: false drops the global defaults")
	{
		KeepConfig layout;
		layout.inherit = false;
		CHECK(Keep(inv, DefaultKeepConfig(), &layout) == std::vector<std::int32_t>{ 0, 0, 0 });
	}
	SECTION("inherit defaults to true when omitted")
	{
		KeepConfig layout;
		layout.always = { "Skyrim.esm|0x50" };
		CHECK(Keep(inv, DefaultKeepConfig(), &layout) == std::vector<std::int32_t>{ 100, 5, 2 });
	}
	SECTION("no layout keep block uses the globals")
	{
		CHECK(Keep(inv, DefaultKeepConfig(), nullptr) == std::vector<std::int32_t>{ 100, 5, 0 });
	}
}

TEST_CASE("Quotas keep the most valuable stacks first", "[keep][quota]")
{
	const std::vector<ItemInfo> inv{
		Potion(Sk(0x10), 3, 50, 1),
		Potion(Sk(0x11), 4, 20, 2),
		Potion(Sk(0x12), 2, 100, 3),
		Weapon(Sk(0x13), WeaponType::Bow),
	};
	CHECK(Keep(inv, Quota("alchemy.potions", 5)) == std::vector<std::int32_t>{ 3, 0, 2, 0 });
	CHECK(Keep(inv, Quota("alchemy.potions", 4)) == std::vector<std::int32_t>{ 2, 0, 2, 0 });  // partial stack
	CHECK(Keep(inv, Quota("alchemy.*", 100)) == std::vector<std::int32_t>{ 3, 4, 2, 0 });
	CHECK(Keep(inv, Quota("alchemy.potions", 0)) == std::vector<std::int32_t>{ 0, 0, 0, 0 });
	CHECK(Keep(inv, Quota("alchemy.poisons", 5)) == std::vector<std::int32_t>{ 0, 0, 0, 0 });
}

TEST_CASE("Quota tie-breaking is deterministic", "[keep][quota]")
{
	// Same value: bigger stack first, then form, then stackId.
	const std::vector<ItemInfo> inv{
		Potion(Sk(0x20), 1, 10, 7),
		Potion(Sk(0x21), 2, 10, 1),
		Potion(Sk(0x20), 1, 10, 3),
	};
	CHECK(Keep(inv, Quota("alchemy.potions", 3)) == std::vector<std::int32_t>{ 0, 2, 1 });
}

TEST_CASE("Always-kept stacks count against a quota", "[keep][quota]")
{
	auto favorite = Potion(Sk(0x30), 3, 1, 1);
	favorite.favorited = true;
	const std::vector<ItemInfo> inv{ favorite, Potion(Sk(0x31), 10, 5, 2) };

	auto cfg = Quota("alchemy.potions", 5);
	cfg.always = { "favorited" };
	CHECK(Keep(inv, cfg) == std::vector<std::int32_t>{ 3, 2 });

	cfg.quotas[0].quota = 2;  // already over quota with the favorite alone
	CHECK(Keep(inv, cfg) == std::vector<std::int32_t>{ 3, 0 });
}

TEST_CASE("Quota selectors combine with AND", "[keep][quota]")
{
	auto health = Potion(Sk(0x40), 10, 20, 1);
	health.keywords = { "MagicAlchRestoreHealth" };
	auto stamina = Potion(Sk(0x41), 10, 20, 2);
	stamina.keywords = { "MagicAlchRestoreStamina" };

	KeepConfig cfg;
	cfg.quotas.push_back(QuotaRule{ .category = "alchemy.potions", .keywordsAny = std::vector<std::string>{ "magicalchrestorehealth" }, .quota = 5 });
	CHECK(Keep({ health, stamina }, cfg) == std::vector<std::int32_t>{ 5, 0 });

	KeepConfig byForm;
	byForm.quotas.push_back(QuotaRule{ .forms = std::vector<FormRef>{ Sk(0x41) }, .quota = 1 });
	CHECK(Keep({ health, stamina }, byForm) == std::vector<std::int32_t>{ 0, 1 });
}

TEST_CASE("The first matching quota rule claims a stack; layout rules first", "[keep][quota]")
{
	const std::vector<ItemInfo> inv{ Potion(Sk(0x50), 10, 1), Potion(Sk(0x51), 10, 1) };

	KeepConfig global;
	global.quotas.push_back(QuotaRule{ .category = "alchemy.*", .quota = 20 });
	KeepConfig layout;
	layout.quotas.push_back(QuotaRule{ .forms = std::vector<FormRef>{ Sk(0x50) }, .quota = 3 });

	CHECK(Keep(inv, global, &layout) == std::vector<std::int32_t>{ 3, 10 });

	KeepConfig twoRules;
	twoRules.quotas.push_back(QuotaRule{ .category = "alchemy.potions", .quota = 4 });
	twoRules.quotas.push_back(QuotaRule{ .category = "alchemy.*", .quota = 100 });
	CHECK(Keep(inv, twoRules) == std::vector<std::int32_t>{ 4, 0 });
}

TEST_CASE("Homeless items only match the '*' keep pattern", "[keep]")
{
	auto unknown = MakeItem(Sk(0x60), ItemType::Other, 2);
	KeepConfig cfg;
	cfg.always = { "*" };
	CHECK(Keep({ unknown }, cfg) == std::vector<std::int32_t>{ 2 });
	cfg.always = { "misc.*" };
	CHECK(Keep({ unknown }, cfg) == std::vector<std::int32_t>{ 0 });
}

TEST_CASE("Invalid keep entries are ignored with a warning", "[keep]")
{
	KeepConfig cfg;
	cfg.always = { "not valid!", "Skyrim.esm|zz", "equipped" };
	Diagnostics diags;
	const auto  rules = KeepRules::Compile(cfg, nullptr, diags);
	CHECK(diags.WarningCount() == 2);
	REQUIRE(rules.Layers().size() == 1);
	CHECK(rules.Layers()[0].always.size() == 1);
}

TEST_CASE("A keep entry that is a broken form reference says what is wrong with it", "[keep][diagnostics]")
{
	KeepConfig layout;
	layout.always = { "Skyrim.esm|0x0100000F", "ammo.*" };
	Diagnostics diags;
	(void)KeepRules::Compile(KeepConfig{}, &layout, diags);
	const bool  said = std::ranges::any_of(diags.Items(), [](const Diagnostic& d) {
		return d.message.find("ignoring keep entry \"Skyrim.esm|0x0100000F\" (not a token, form reference or category pattern): "
							  "form id above 0x00FFFFFF, drop the load-order byte") != std::string::npos;
    });
	CHECK(said);
}
