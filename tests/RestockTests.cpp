// Restock (DESIGN §8.7): quota rules with "restock" top the player up from the home's containers.

#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"

using namespace dova::core;
using namespace dova::test;

namespace
{
	const FormRef kMaster = Sk(0xA000);
	const FormRef kSatchel = Sk(0xC000);   // alchemy.*
	const FormRef kQuiver = Sk(0xC001);    // ammo.*
	const FormRef kLoose = Sk(0xF000);     // no categories
	const FormRef kIgnored = Sk(0xD000);   // ignored
	const FormRef kUnmanaged = Sk(0x9999); // not in the layout

	const FormRef kHealth = Sk(0x3EADE);      // minor healing, value 17
	const FormRef kHealthBig = Sk(0x3EADF);   // healing, value 36
	const FormRef kStamina = Sk(0x39BE7);
	const FormRef kSteelArrow = Sk(0x1397F);

	Layout MakeLayout()
	{
		Layout l;
		l.cell = Sk(0x165A7);
		l.master = kMaster;
		l.containers.push_back({ .ref = kSatchel, .label = "Satchel", .categories = { "alchemy.*" } });
		l.containers.push_back({ .ref = kQuiver, .label = "Quiver", .categories = { "ammo.*" } });
		l.containers.push_back({ .ref = kLoose, .label = "Loose" });
		l.containers.push_back({ .ref = kIgnored, .label = "Barrel", .categories = { "*" }, .ignored = true });
		return l;
	}

	ItemInfo HealthPotion(FormRef a_form, std::int32_t a_count, std::int32_t a_value, std::uint32_t a_stackId)
	{
		auto item = Potion(std::move(a_form), a_count, a_value, a_stackId);
		item.keywords = { "MagicAlchRestoreHealth" };
		return item;
	}

	ItemInfo Arrow(FormRef a_form, std::int32_t a_count, std::int32_t a_value, std::uint32_t a_stackId)
	{
		auto item = MakeItem(std::move(a_form), ItemType::Ammo, a_count, a_stackId);
		item.ammoKind = AmmoKind::Arrow;
		item.value = a_value;
		return item;
	}

	QuotaRule Restocking(std::string a_category, std::int32_t a_quota)
	{
		QuotaRule rule;
		rule.category = std::move(a_category);
		rule.quota = a_quota;
		rule.restock = true;
		return rule;
	}

	KeepConfig Only(std::vector<QuotaRule> a_quotas)
	{
		KeepConfig cfg;
		cfg.inherit = false;
		cfg.quotas = std::move(a_quotas);
		return cfg;
	}

	KeepRules Compile(const KeepConfig& a_global, const KeepConfig* a_layout = nullptr)
	{
		Diagnostics diags;
		auto        rules = KeepRules::Compile(a_global, a_layout, diags);
		CHECK_FALSE(diags.HasErrors());
		return rules;
	}

	struct Fixture
	{
		CategorySet categories = CompileCategories(kStandardCategories);
		Layout      layout = MakeLayout();

		std::vector<Move> Restock(std::span<const ItemInfo> a_inventory, std::span<const ContainerContents> a_stored, const KeepRules& a_keep,
			std::vector<RestockLine>* a_explain = nullptr, ContainerAvailability a_available = {}) const
		{
			return SortPlanner(layout, categories, std::move(a_available)).Restock(a_inventory, a_stored, a_keep, a_explain);
		}
	};

	std::int32_t Total(const std::vector<Move>& a_moves)
	{
		std::int32_t n = 0;
		for (const auto& m : a_moves) {
			n += m.count;
		}
		return n;
	}
}

TEST_CASE("Quota rules read and write \"restock\"", "[restock][layout]")
{
	Diagnostics diags;
	const auto  layout = ParseLayoutText(R"({
		"$schema": "dovastash-layout-1", "name": "Home", "cell": "Skyrim.esm|0x1", "containers": [],
		"keep": { "quotas": [
			{ "category": "alchemy.potions", "quota": 10, "restock": true },
			{ "category": "ammo.arrows", "quota": 50 },
			{ "category": "misc.gems", "quota": 2, "restock": false },
			{ "category": "keys", "quota": 1, "restock": "yes" }
		] }
	})", "test.json", diags);
	REQUIRE(layout);
	INFO((diags.Empty() ? std::string() : diags.Items().front().ToString()));
	CHECK_FALSE(diags.HasErrors());
	CHECK(diags.Items().size() == 1);  // the "yes": a warning, kept
	REQUIRE(layout->keep);
	const auto& q = layout->keep->quotas;
	REQUIRE(q.size() == 4);
	CHECK(q[0].restock);
	CHECK_FALSE(q[1].restock);  // absent: off, as before restock existed
	CHECK_FALSE(q[2].restock);
	CHECK_FALSE(q[3].restock);
	CHECK(q[3].extra.at("restock") == "yes");

	const auto text = SerializeLayout(*layout);
	CHECK(text.find(R"("restock": true)") != std::string::npos);
	CHECK(text.find(R"("restock": true)") > text.find(R"("quota": 10)"));  // after the quota
	CHECK(text.find(R"("restock": false)") == std::string::npos);  // off is not written
	CHECK(text.find(R"("restock": "yes")") != std::string::npos);  // kept verbatim

	Diagnostics again;
	const auto  round = ParseLayoutText(text, "round.json", again);
	REQUIRE(round);
	CHECK(SerializeLayout(*round) == text);
	CHECK(round->keep->quotas[0].restock);
}

TEST_CASE("RestockApplies follows the layers in force", "[restock][keep]")
{
	const auto restocking = Only({ Restocking("alchemy.potions", 5) });
	auto       plain = Only({ QuotaRule{ .category = "alchemy.potions", .quota = 5 } });

	CHECK_FALSE(RestockApplies(DefaultKeepConfig(), nullptr));
	CHECK(RestockApplies(restocking, nullptr));
	CHECK(RestockApplies(DefaultKeepConfig(), &restocking));

	plain.inherit = true;
	CHECK(RestockApplies(restocking, &plain));
	plain.inherit = false;
	CHECK_FALSE(RestockApplies(restocking, &plain));  // the layout replaces the globals

	CHECK(Compile(restocking).Restocks());
	CHECK_FALSE(Compile(DefaultKeepConfig()).Restocks());
}

TEST_CASE("Restock tops the player up to the quota, most valuable first, never over", "[restock][planner]")
{
	Fixture    f;
	const auto keep = Compile(Only({ Restocking("alchemy.potions", 10) }));

	const std::vector<ItemInfo> inventory{ HealthPotion(kHealth, 3, 17, 1) };
	const std::vector<ContainerContents> stored{
		{ kMaster, { HealthPotion(kHealthBig, 2, 36, 10) } },
		{ kSatchel, { HealthPotion(kHealth, 20, 17, 20), HealthPotion(kHealthBig, 4, 36, 21), Potion(kStamina, 6, 20, 22) } },
	};

	std::vector<RestockLine> lines;
	const auto               moves = f.Restock(inventory, stored, keep, &lines);
	// 7 missing: the 6 healing potions (value 36: the satchel's first, it is their home), then 1 of the next
	// most valuable potion (stamina, 20), part of its stack.
	REQUIRE(moves.size() == 3);
	CHECK(moves[0] == Move{ 21, kHealthBig, 4, ContainerRef(kSatchel), ContainerRef::Player() });
	CHECK(moves[1] == Move{ 10, kHealthBig, 2, ContainerRef(kMaster), ContainerRef::Player() });
	CHECK(moves[2] == Move{ 22, kStamina, 1, ContainerRef(kSatchel), ContainerRef::Player() });
	CHECK(Total(moves) == 7);
	REQUIRE(lines.size() == 1);
	CHECK(lines[0].quota == 10);
	CHECK(lines[0].have == 3);
	CHECK(lines[0].taken == 7);
	CHECK(lines[0].label == "quota alchemy.potions 10 (global)");

	SECTION("nothing when the player already carries the quota")
	{
		const std::vector<ItemInfo> full{ HealthPotion(kHealth, 12, 17, 1) };
		CHECK(f.Restock(full, stored, keep).empty());
	}
	SECTION("storage running short takes what there is")
	{
		const std::vector<ContainerContents> few{ { kSatchel, { HealthPotion(kHealth, 2, 17, 20) } } };
		std::vector<RestockLine>             short_;
		CHECK(Total(f.Restock(inventory, few, keep, &short_)) == 2);
		CHECK(short_[0].taken == 2);
	}
}

TEST_CASE("Restock only takes from managed, available containers, and never unmovable items", "[restock][planner]")
{
	Fixture    f;
	const auto keep = Compile(Only({ Restocking("ammo.arrows", 50) }));

	auto quest = Arrow(Sk(0x500), 5, 99, 5);
	quest.questItem = true;
	auto unplayable = Arrow(Sk(0x501), 5, 99, 6);
	unplayable.playable = false;
	auto returns = Arrow(Sk(0x502), 5, 99, 7);
	returns.returns = true;

	const std::vector<ContainerContents> stored{
		{ kIgnored, { Arrow(kSteelArrow, 100, 3, 1) } },
		{ kUnmanaged, { Arrow(kSteelArrow, 100, 3, 2) } },
		{ kLoose, { Arrow(kSteelArrow, 100, 3, 3) } },  // unavailable below
		{ kQuiver, { quest, unplayable, returns, Arrow(kSteelArrow, 30, 3, 4) } },
	};
	const auto moves = f.Restock({}, stored, keep, nullptr, [](const FormRef& a_ref) { return a_ref != kLoose; });
	REQUIRE(moves.size() == 1);
	CHECK(moves[0] == Move{ 4, kSteelArrow, 30, ContainerRef(kQuiver), ContainerRef::Player() });
}

TEST_CASE("Plain copies come before customized ones", "[restock][planner]")
{
	Fixture    f;
	const auto keep = Compile(Only({ Restocking("ammo.arrows", 10) }));
	auto       renamed = Arrow(kSteelArrow, 10, 3, 1);
	renamed.customized = true;
	const std::vector<ContainerContents> stored{ { kQuiver, { renamed, Arrow(kSteelArrow, 10, 3, 2) } } };
	const auto                           moves = f.Restock({}, stored, keep);
	REQUIRE(moves.size() == 1);
	CHECK(moves[0].stackId == 2);
}

TEST_CASE("Which rule a stored stack belongs to", "[restock][keep]")
{
	Fixture f;
	const std::vector<ContainerContents> stored{
		{ kSatchel, { HealthPotion(kHealth, 20, 17, 1), Potion(kStamina, 20, 20, 2) } },
	};

	SECTION("the first matching rule owns it: one that does not restock keeps it in storage")
	{
		KeepConfig cfg = Only({ QuotaRule{ .category = "alchemy.potions", .keywordsAny = std::vector<std::string>{ "MagicAlchRestoreHealth" }, .quota = 5 },
			Restocking("alchemy.potions", 10) });
		const auto moves = f.Restock({}, stored, Compile(cfg));
		REQUIRE(moves.size() == 1);
		CHECK(moves[0].stackId == 2);  // stamina potions only
		CHECK(moves[0].count == 10);
	}
	SECTION("each restocking rule counts only what it owns")
	{
		auto health = Restocking("alchemy.potions", 5);
		health.keywordsAny = std::vector<std::string>{ "MagicAlchRestoreHealth" };
		const KeepConfig            cfg = Only({ health, Restocking("alchemy.potions", 10) });
		const std::vector<ItemInfo> inventory{ HealthPotion(kHealth, 8, 17, 50) };  // 8 health: no other potion yet
		const auto                  moves = f.Restock(inventory, stored, Compile(cfg));
		REQUIRE(moves.size() == 1);
		CHECK(moves[0].stackId == 2);
		CHECK(moves[0].count == 10);
	}
	SECTION("a never rule blocks it, unless an always rule of that layer keeps it")
	{
		KeepConfig cfg = Only({ Restocking("alchemy.potions", 10) });
		cfg.never = { "alchemy.potions" };
		CHECK(f.Restock({}, stored, Compile(cfg)).empty());
		cfg.always = { "alchemy.potions" };
		CHECK(Total(f.Restock({}, stored, Compile(cfg))) == 10);
	}
	SECTION("the layout's rule comes before the global one")
	{
		const auto global = Only({ Restocking("alchemy.potions", 10) });
		auto       layout = Only({ QuotaRule{ .category = "alchemy.potions", .quota = 2 } });
		layout.inherit = true;
		CHECK(f.Restock({}, stored, Compile(global, &layout)).empty());
	}
}

TEST_CASE("What the player keeps counts against the quota, as on Deposit", "[restock][keep]")
{
	Fixture f;
	// The default global keep (always lockpicks) plus a restocking lockpick quota in the same layer.
	auto global = DefaultKeepConfig();
	global.quotas.push_back(QuotaRule{ .forms = std::vector<FormRef>{ kLockpickForm }, .quota = 5, .restock = true });
	const auto keep = Compile(global);

	const std::vector<ContainerContents> stored{ { kLoose, { Misc(kLockpickForm, 40) } } };
	const std::vector<ItemInfo>          inventory{ Misc(kLockpickForm, 2) };
	const auto                           goals = keep.RestockGoals(inventory, f.categories);
	REQUIRE(goals.size() == 1);
	CHECK(goals[0].have == 2);
	CHECK(goals[0].Missing() == 3);
	CHECK(Total(f.Restock(inventory, stored, keep)) == 3);

	auto favorite = HealthPotion(kHealth, 4, 17, 9);
	favorite.favorited = true;
	auto withFavorites = DefaultKeepConfig();
	withFavorites.quotas.push_back(Restocking("alchemy.potions", 10));
	const auto goals2 = Compile(withFavorites).RestockGoals(std::vector<ItemInfo>{ favorite }, f.categories);
	REQUIRE(goals2.size() == 1);
	CHECK(goals2[0].have == 4);  // a favorited copy is kept, so it counts
}

TEST_CASE("Deposit right after Restock leaves what was restocked", "[restock][planner][deposit]")
{
	Fixture    f;
	const auto keep = Compile(Only({ Restocking("alchemy.potions", 10), Restocking("ammo.arrows", 20) }));

	std::vector<ItemInfo> inventory{ HealthPotion(kHealth, 1, 17, 1), Arrow(kSteelArrow, 5, 3, 2) };
	const std::vector<ContainerContents> stored{
		{ kSatchel, { HealthPotion(kHealthBig, 4, 36, 10), HealthPotion(kHealth, 20, 17, 11) } },
		{ kQuiver, { Arrow(kSteelArrow, 100, 3, 12), Arrow(Sk(0x139C0), 6, 12, 13) } },
	};
	const auto moves = f.Restock(inventory, stored, keep);
	CHECK(Total(moves) == 9 + 15);

	// The player's inventory after the moves (each restocked stack arrives as a stack of its own).
	std::uint32_t id = 100;
	for (const auto& m : moves) {
		for (const auto& c : stored) {
			for (const auto& item : c.items) {
				if (item.stackId == m.stackId) {
					auto arrived = item;
					arrived.count = m.count;
					arrived.stackId = id++;
					inventory.push_back(arrived);
				}
			}
		}
	}
	const auto deposit = SortPlanner(f.layout, f.categories).Deposit(inventory, keep);
	CHECK(deposit.empty());
	std::vector<RestockLine> lines;
	CHECK(f.Restock(inventory, stored, keep, &lines).empty());  // and a second Restock finds every quota met
	for (const auto& line : lines) {
		CHECK(line.have == line.quota);
		CHECK(line.taken == 0);
	}
}
