#include <set>

#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"

using namespace dova::core;
using namespace dova::test;

namespace
{
	const FormRef kMaster = Sk(0xA000);
	const FormRef kArmory = Sk(0xB000);     // weapons.*
	const FormRef kBowRack = Sk(0xB001);    // weapons.bows
	const FormRef kSatchel = Sk(0xC000);    // alchemy.*, explicit binding for 0x777
	const FormRef kDisplay = Sk(0xD000);    // ignored, binds "*"
	const FormRef kValuables = Sk(0xE000);  // misc.valuables
	const FormRef kLooseChest = Sk(0xF000); // no categories
	const FormRef kUnmanaged = Sk(0x9999);  // not in the layout

	Layout MakeLayout()
	{
		Layout l;
		l.cell = Sk(0x165A7);
		l.master = kMaster;
		l.containers.push_back({ .ref = kArmory, .label = "Armory", .categories = { "weapons.*" } });
		l.containers.push_back({ .ref = kBowRack, .label = "Bows", .categories = { "weapons.bows" } });
		l.containers.push_back({ .ref = kSatchel, .label = "Satchel", .categories = { "alchemy.*" }, .forms = { Sk(0x777) } });
		l.containers.push_back({ .ref = kDisplay, .label = "Display", .categories = { "*" }, .forms = { Sk(0x778) }, .ignored = true });
		l.containers.push_back({ .ref = kValuables, .label = "Valuables", .categories = { "misc.valuables" } });
		l.containers.push_back({ .ref = kLooseChest, .label = "Loose" });
		return l;
	}

	struct Fixture
	{
		CategorySet categories = CompileCategories(kStandardCategories);
		Layout      layout = MakeLayout();

		[[nodiscard]] std::optional<Destination> Dest(const ItemInfo& a_item, ContainerAvailability a_available = {}) const
		{
			return SortPlanner(layout, categories, std::move(a_available)).ResolveDestination(a_item);
		}
	};

	KeepRules NoKeep()
	{
		KeepConfig cfg;
		cfg.inherit = false;
		Diagnostics diags;
		return KeepRules::Compile(cfg, nullptr, diags);
	}
}

TEST_CASE("Destination precedence: explicit > learned > category > master", "[planner]")
{
	Fixture f;
	const auto sword = Weapon(Sk(0x777), WeaponType::OneHandSword);

	// Explicit binding beats both learned and category.
	f.layout.learned.emplace(Sk(0x777), kLooseChest);
	auto d = f.Dest(sword);
	REQUIRE(d);
	CHECK(d->container == kSatchel);
	CHECK(d->reason == Destination::Reason::Explicit);

	// Without the explicit binding, learned wins.
	f.layout.containers[2].forms.clear();
	d = f.Dest(sword);
	REQUIRE(d);
	CHECK(d->container == kLooseChest);
	CHECK(d->reason == Destination::Reason::Learned);

	// Without learned, the category binding wins.
	f.layout.learned.clear();
	d = f.Dest(sword);
	REQUIRE(d);
	CHECK(d->container == kArmory);
	CHECK(d->reason == Destination::Reason::Category);
	CHECK(d->category == "weapons.swords");

	// Homeless -> master.
	d = f.Dest(MakeItem(Sk(0x1234), ItemType::Book));
	REQUIRE(d);
	CHECK(d->container == kMaster);
	CHECK(d->reason == Destination::Reason::Master);
}

TEST_CASE("Ignored containers are never destinations", "[planner]")
{
	Fixture f;
	// Explicit binding on an ignored container is skipped; the "*" binding there is skipped too.
	auto d = f.Dest(MakeItem(Sk(0x778), ItemType::Book));
	REQUIRE(d);
	CHECK(d->container == kMaster);

	// Learned to an ignored container or to the master is skipped.
	const auto key = MakeItem(Sk(0x55), ItemType::Key);
	f.layout.learned.emplace(Sk(0x55), kDisplay);
	CHECK(f.Dest(key)->reason == Destination::Reason::Master);
	f.layout.learned[Sk(0x55)] = kMaster;
	CHECK(f.Dest(key)->reason == Destination::Reason::Master);
	// Learned to a container no longer in the layout is skipped.
	f.layout.learned[Sk(0x55)] = kUnmanaged;
	CHECK(f.Dest(key)->reason == Destination::Reason::Master);
}

TEST_CASE("The most specific binding pattern wins", "[planner]")
{
	Fixture f;
	CHECK(f.Dest(Weapon(Sk(0x1), WeaponType::Bow))->container == kBowRack);
	CHECK(f.Dest(Weapon(Sk(0x2), WeaponType::OneHandDagger))->container == kArmory);

	// Ties go to the container listed first.
	f.layout.containers.push_back({ .ref = Sk(0xB002), .categories = { "weapons.bows" } });
	CHECK(f.Dest(Weapon(Sk(0x1), WeaponType::Bow))->container == kBowRack);

	// A non-ignored "*" catch-all only gets what nothing more specific binds.
	f.layout.containers.push_back({ .ref = Sk(0xB003), .categories = { "*" } });
	CHECK(f.Dest(Weapon(Sk(0x1), WeaponType::Bow))->container == kBowRack);
	CHECK(f.Dest(MakeItem(Sk(0x3), ItemType::Key))->container == Sk(0xB003));
	// Homeless items (no category at all) are not caught by "*": they go to the master.
	CHECK(f.Dest(MakeItem(Sk(0x4), ItemType::Book))->container == kMaster);
}

TEST_CASE("Falls back to the next matching category when the best one is unbound", "[planner]")
{
	Fixture f;
	// A gem is misc.gems (priority 100, unbound) and misc.valuables (priority 50, bound).
	const auto gem = Misc(Sk(0x63B42), 1, { "VendorItemGem" });
	REQUIRE(f.categories.Classify(gem) == "misc.gems");
	auto d = f.Dest(gem);
	REQUIRE(d);
	CHECK(d->container == kValuables);
	CHECK(d->category == "misc.valuables");
}

TEST_CASE("Unavailable containers are skipped", "[planner]")
{
	Fixture    f;
	const auto bow = Weapon(Sk(0x1), WeaponType::Bow);

	auto d = f.Dest(bow, [](const FormRef& a_ref) { return a_ref != kBowRack; });
	REQUIRE(d);
	CHECK(d->container == kArmory);

	d = f.Dest(bow, [](const FormRef& a_ref) { return a_ref == kMaster; });
	REQUIRE(d);
	CHECK(d->container == kMaster);

	// Nothing available: no destination, so the item will not be moved.
	CHECK_FALSE(f.Dest(bow, [](const FormRef&) { return false; }).has_value());

	// No master at all and homeless.
	f.layout.master.reset();
	CHECK_FALSE(f.Dest(MakeItem(Sk(0x2), ItemType::Book)).has_value());
}

TEST_CASE("Deposit respects keep counts and never moves quest or non-playable items", "[planner][deposit]")
{
	Fixture f;
	auto    quest = Weapon(Sk(0x10), WeaponType::Bow);
	quest.questItem = true;
	auto unplayable = Weapon(Sk(0x11), WeaponType::Bow);
	unplayable.playable = false;
	auto empty = Weapon(Sk(0x12), WeaponType::Bow, 0);

	std::vector<ItemInfo> inv{
		quest, unplayable, empty,
		Potion(Sk(0x20), 10, 5, 42),
		Weapon(Sk(0x21), WeaponType::Bow),
		MakeItem(Sk(0x22), ItemType::Book, 2),
		Misc(kGoldForm, 300),
	};

	SECTION("with an empty keep config")
	{
		const auto moves = SortPlanner(f.layout, f.categories).Deposit(inv, NoKeep());
		REQUIRE(moves.size() == 4);
		CHECK(moves[0] == Move{ 42, Sk(0x20), 10, ContainerRef::Player(), kSatchel });
		CHECK(moves[1] == Move{ 0, Sk(0x21), 1, ContainerRef::Player(), kBowRack });
		CHECK(moves[2] == Move{ 0, Sk(0x22), 2, ContainerRef::Player(), kMaster });
		CHECK(moves[3].form == kGoldForm);
		CHECK(moves[3].to == ContainerRef(kMaster));
		for (const auto& m : moves) {
			CHECK(m.from.IsPlayer());
			CHECK_FALSE(m.to.IsPlayer());
		}
	}
	SECTION("with defaults and a potion quota")
	{
		KeepConfig layoutKeep;
		layoutKeep.quotas.push_back(QuotaRule{ .category = "alchemy.potions", .quota = 3 });
		Diagnostics diags;
		const auto  keep = KeepRules::Compile(DefaultKeepConfig(), &layoutKeep, diags);
		const auto  moves = SortPlanner(f.layout, f.categories).Deposit(inv, keep);
		REQUIRE(moves.size() == 3);
		CHECK(moves[0] == Move{ 42, Sk(0x20), 7, ContainerRef::Player(), kSatchel });  // 3 kept
		CHECK(moves[1].form == Sk(0x21));
		CHECK(moves[2].form == Sk(0x22));
	}
	SECTION("homeless items stay when there is no master")
	{
		f.layout.master.reset();
		const auto moves = SortPlanner(f.layout, f.categories).Deposit(inv, NoKeep());
		REQUIRE(moves.size() == 2);
		CHECK(moves[0].to == ContainerRef(kSatchel));
		CHECK(moves[1].to == ContainerRef(kBowRack));
	}
}

TEST_CASE("Deposit explains every stack: moved where and why, or why it stays", "[planner][deposit][explain]")
{
	using enum StackDecision::Outcome;
	Fixture f;
	auto    quest = Weapon(Sk(0x10), WeaponType::Bow);
	quest.questItem = true;
	auto unplayable = Weapon(Sk(0x11), WeaponType::Bow);
	unplayable.playable = false;
	auto nameless = Misc(Sk(0x12));
	nameless.name.clear();
	nameless.playable = false;  // the plugin marks nameless items non-playable too
	auto equipped = Weapon(Sk(0x13), WeaponType::OneHandSword);
	equipped.equipped = true;

	const std::vector<ItemInfo> inv{
		quest, unplayable, nameless, Weapon(Sk(0x14), WeaponType::Bow, 0), equipped,
		Potion(Sk(0x20), 10, 5, 42),
		MakeItem(Sk(0x22), ItemType::Book, 2),
		Misc(kGoldForm, 300),
	};
	KeepConfig layoutKeep;
	layoutKeep.quotas.push_back(QuotaRule{ .category = "alchemy.potions", .quota = 3 });
	Diagnostics diags;
	const auto  keep = KeepRules::Compile(DefaultKeepConfig(), &layoutKeep, diags);

	std::vector<StackDecision> why;
	const auto                 moves = SortPlanner(f.layout, f.categories).Deposit(inv, keep, &why);
	CHECK(moves == SortPlanner(f.layout, f.categories).Deposit(inv, keep));  // explaining changes nothing
	REQUIRE(why.size() == inv.size());
	CHECK(why[0].outcome == Quest);
	CHECK(why[1].outcome == NonPlayable);
	CHECK(why[2].outcome == Nameless);
	CHECK(why[3].outcome == Empty);
	CHECK(why[4].outcome == Kept);
	CHECK(why[4].kept == 1);
	CHECK(why[4].keepRule == "always equipped (global)");
	CHECK(why[5].outcome == Move);
	CHECK(why[5].moved == 7);
	CHECK(why[5].kept == 3);
	CHECK(why[5].keepRule == "quota alchemy.potions 3 (layout)");
	REQUIRE(why[5].destination.has_value());
	CHECK(why[5].destination->container == kSatchel);
	CHECK(why[5].destination->reason == Destination::Reason::Category);
	CHECK(why[6].outcome == Move);
	CHECK(why[6].destination->reason == Destination::Reason::Master);
	CHECK(why[6].keepRule.empty());
	CHECK(why[7].outcome == Kept);
	CHECK(why[7].keepRule == "always Skyrim.esm|0x0000000F (global)");

	// No master and no binding: the book has nowhere to go.
	f.layout.master.reset();
	(void)SortPlanner(f.layout, f.categories).Deposit(inv, keep, &why);
	CHECK(why[6].outcome == NoDestination);
	CHECK(StackDecision::OutcomeName(NoDestination) == "no destination");
	CHECK(DestinationReasonName(Destination::Reason::Master) == "homeless");
}

TEST_CASE("Distribute moves master contents to destinations; homeless stay", "[planner][distribute]")
{
	Fixture f;
	auto    quest = Potion(Sk(0x30), 1, 1);
	quest.questItem = true;
	const std::vector<ItemInfo> master{
		Weapon(Sk(0x31), WeaponType::OneHandSword),
		MakeItem(Sk(0x32), ItemType::Book),
		quest,
		Potion(Sk(0x33), 4, 1, 9),
	};
	const auto moves = SortPlanner(f.layout, f.categories).Distribute(master);
	REQUIRE(moves.size() == 2);
	CHECK(moves[0] == Move{ 0, Sk(0x31), 1, kMaster, kArmory });
	CHECK(moves[1] == Move{ 9, Sk(0x33), 4, kMaster, kSatchel });

	std::vector<StackDecision> why;
	CHECK(SortPlanner(f.layout, f.categories).Distribute(master, &why) == moves);
	REQUIRE(why.size() == 4);
	CHECK(why[0].outcome == StackDecision::Outcome::Move);
	CHECK(why[1].outcome == StackDecision::Outcome::Homeless);
	CHECK(why[2].outcome == StackDecision::Outcome::Quest);
	CHECK(why[3].destination->category == "alchemy.potions");

	// Master unavailable or missing: nothing happens.
	CHECK(SortPlanner(f.layout, f.categories, [](const FormRef& a_ref) { return a_ref != kMaster; }).Distribute(master).empty());
	f.layout.master.reset();
	CHECK(SortPlanner(f.layout, f.categories).Distribute(master).empty());
}

TEST_CASE("SortAll fixes misplaced items and leaves ignored/unmanaged containers alone", "[planner][sortall]")
{
	Fixture f;
	f.layout.learned.emplace(Sk(0x44), kLooseChest);

	const std::vector<ContainerContents> contents{
		{ kMaster, { Weapon(Sk(0x40), WeaponType::Bow), MakeItem(Sk(0x41), ItemType::Book) } },
		{ kArmory, { Weapon(Sk(0x42), WeaponType::OneHandSword), Weapon(Sk(0x43), WeaponType::Bow), Potion(Sk(0x44), 2, 1) } },
		{ kLooseChest, { MakeItem(Sk(0x45), ItemType::Book, 3), Potion(Sk(0x44), 1, 1) } },
		{ kDisplay, { Weapon(Sk(0x46), WeaponType::Bow) } },
		{ kUnmanaged, { Weapon(Sk(0x47), WeaponType::Bow) } },
	};
	const auto moves = SortPlanner(f.layout, f.categories).SortAll(contents);
	REQUIRE(moves.size() == 3);
	CHECK(moves[0] == Move{ 0, Sk(0x40), 1, kMaster, kBowRack });
	CHECK(moves[1] == Move{ 0, Sk(0x43), 1, kArmory, kBowRack });
	CHECK(moves[2] == Move{ 0, Sk(0x44), 2, kArmory, kLooseChest });  // learned
	// The loose chest has no bindings: it is a target only, so its homeless book stays.
	for (const auto& m : moves) {
		CHECK(m.from != m.to);
		CHECK(m.from != ContainerRef(kDisplay));
		CHECK(m.to != ContainerRef(kDisplay));
		CHECK(m.from != ContainerRef(kUnmanaged));
	}

	// An unavailable source container is not touched.
	const auto skipArmory = SortPlanner(f.layout, f.categories, [](const FormRef& a_ref) { return a_ref != kArmory; }).SortAll(contents);
	for (const auto& m : skipArmory) {
		CHECK(m.from != ContainerRef(kArmory));
		CHECK(m.to != ContainerRef(kArmory));
	}
}

TEST_CASE("SortAll never empties containers without bindings", "[planner][sortall]")
{
	Fixture f;
	const FormRef kKeepsake = Sk(0xF001);  // added by Learn: no categories, no forms
	const FormRef kFormsOnly = Sk(0xF002); // explicit forms only
	f.layout.containers.push_back({ .ref = kKeepsake, .label = "Keepsakes" });
	f.layout.containers.push_back({ .ref = kFormsOnly, .label = "Trophies", .forms = { Sk(0x90) } });
	f.layout.learned.emplace(Sk(0x91), kKeepsake);

	const SortPlanner planner(f.layout, f.categories);
	CHECK(planner.IsSortSource(kMaster));
	CHECK(planner.IsSortSource(kArmory));
	CHECK(planner.IsSortSource(kFormsOnly));
	CHECK_FALSE(planner.IsSortSource(kKeepsake));
	CHECK_FALSE(planner.IsSortSource(kLooseChest));
	CHECK_FALSE(planner.IsSortSource(kDisplay));
	CHECK_FALSE(planner.IsSortSource(kUnmanaged));

	const std::vector<ContainerContents> contents{
		// Keepsake chest: a sword (would go to the armory) and a book (homeless) both stay.
		{ kKeepsake, { Weapon(Sk(0x92), WeaponType::OneHandSword), MakeItem(Sk(0x93), ItemType::Book) } },
		// Forms-only container is a source: the bow leaves, the explicitly bound trophy stays.
		{ kFormsOnly, { Weapon(Sk(0x94), WeaponType::Bow), MakeItem(Sk(0x90), ItemType::Misc) } },
		// Learned items still flow INTO the keepsake chest.
		{ kMaster, { MakeItem(Sk(0x91), ItemType::Misc, 2) } },
	};
	const auto moves = planner.SortAll(contents);
	REQUIRE(moves.size() == 2);
	CHECK(moves[0] == Move{ 0, Sk(0x94), 1, kFormsOnly, kBowRack });
	CHECK(moves[1] == Move{ 0, Sk(0x91), 2, kMaster, kKeepsake });
	for (const auto& m : moves) {
		CHECK(m.from != ContainerRef(kKeepsake));
	}
}

TEST_CASE("Nameless items (script tokens) are never moved or learned", "[planner]")
{
	Fixture f;
	auto token = Misc(Sk(0xA1), 1);
	token.name.clear();
	CHECK_FALSE(SortPlanner::CanMove(token));
	auto named = token;
	named.name = "Iron Ingot";
	CHECK(SortPlanner::CanMove(named));

	const std::vector<ItemInfo> inv{ token, MakeItem(Sk(0xA2), ItemType::Book) };
	const auto moves = SortPlanner(f.layout, f.categories).Deposit(inv, NoKeep());
	REQUIRE(moves.size() == 1);
	CHECK(moves[0].form == Sk(0xA2));

	const std::vector<ContainerContents> contents{ { kMaster, { token } }, { kArmory, { token } } };
	CHECK(SortPlanner(f.layout, f.categories).SortAll(contents).empty());
	CHECK(SortPlanner(f.layout, f.categories).Distribute(contents[0].items).empty());
	CHECK(SortPlanner(f.layout, f.categories).Retrieve(contents, "*").empty());

	CHECK(Learn(f.layout, token, kLooseChest, f.categories) == LearnResult::RejectedItem);
	CHECK_FALSE(f.layout.learned.contains(Sk(0xA1)));
}

TEST_CASE("Items a script sends back when stored are never moved again", "[planner]")
{
	Fixture f;
	auto boomerang = Misc(Sk(0xA3), 1);
	boomerang.returns = true;
	CHECK_FALSE(SortPlanner::CanMove(boomerang));

	const std::vector<ItemInfo> inv{ boomerang, MakeItem(Sk(0xA2), ItemType::Book) };
	std::vector<StackDecision>  why;
	const auto                  moves = SortPlanner(f.layout, f.categories).Deposit(inv, NoKeep(), &why);
	REQUIRE(moves.size() == 1);
	CHECK(moves[0].form == Sk(0xA2));
	REQUIRE(why.size() == 2);
	CHECK(why[0].outcome == StackDecision::Outcome::Returns);
	CHECK(SortPlanner(f.layout, f.categories).Distribute(std::vector<ItemInfo>{ boomerang }).empty());
	CHECK(Learn(f.layout, boomerang, kLooseChest, f.categories) == LearnResult::RejectedItem);
}

TEST_CASE("Retrieve by category pattern", "[planner][retrieve]")
{
	Fixture f;
	auto    quest = Weapon(Sk(0x55), WeaponType::Bow);
	quest.questItem = true;
	const std::vector<ContainerContents> contents{
		{ kArmory, { Weapon(Sk(0x50), WeaponType::OneHandSword), Potion(Sk(0x51), 2, 1), quest } },
		{ kBowRack, { Weapon(Sk(0x52), WeaponType::Bow, 1) } },
		{ kMaster, { MakeItem(Sk(0x53), ItemType::Book) } },
		{ kDisplay, { Weapon(Sk(0x54), WeaponType::Bow) } },
		{ kUnmanaged, { Weapon(Sk(0x56), WeaponType::Bow) } },
	};
	const SortPlanner planner(f.layout, f.categories);

	auto moves = planner.Retrieve(contents, "weapons.*");
	REQUIRE(moves.size() == 2);
	CHECK(moves[0] == Move{ 0, Sk(0x50), 1, kArmory, ContainerRef::Player() });
	CHECK(moves[1] == Move{ 0, Sk(0x52), 1, kBowRack, ContainerRef::Player() });

	CHECK(planner.Retrieve(contents, "weapons.bows").size() == 1);
	CHECK(planner.Retrieve(contents, "ALCHEMY.potions").size() == 1);
	CHECK(planner.Retrieve(contents, "armor.*").empty());
	CHECK(planner.Retrieve(contents, "bad pattern").empty());

	moves = planner.Retrieve(contents, "*");
	REQUIRE(moves.size() == 4);  // includes the homeless book, excludes quest/ignored/unmanaged
	std::set<std::uint32_t> ids;
	for (const auto& m : moves) {
		CHECK(m.to.IsPlayer());
		ids.insert(m.form.localId);
	}
	CHECK(ids == std::set<std::uint32_t>{ 0x50, 0x51, 0x52, 0x53 });
}

TEST_CASE("ContainerRef and Move basics", "[planner]")
{
	const ContainerRef player;
	CHECK(player.IsPlayer());
	CHECK(player == ContainerRef::Player());
	CHECK(player.ToString() == "<player>");
	CHECK_THROWS(player.Ref());

	const ContainerRef chest(F("skyrim.esm|0xA000"));
	CHECK_FALSE(chest.IsPlayer());
	CHECK(chest == ContainerRef(kMaster));
	CHECK(chest.Ref() == kMaster);
	CHECK(chest != player);
	CHECK(chest.ToString() == "skyrim.esm|0x0000A000");
}

TEST_CASE("Learn records, updates and rejects", "[planner][learn]")
{
	auto layout = MakeLayout();

	CHECK(Learn(layout, Sk(0x60), kArmory) == LearnResult::Recorded);
	CHECK(layout.learned.at(Sk(0x60)) == kArmory);
	CHECK(Learn(layout, Sk(0x60), kArmory) == LearnResult::Unchanged);
	CHECK(Learn(layout, Sk(0x60), kLooseChest) == LearnResult::Updated);
	CHECK(layout.learned.at(Sk(0x60)) == kLooseChest);

	CHECK(Learn(layout, Sk(0x61), kMaster) == LearnResult::RejectedMaster);
	CHECK(Learn(layout, Sk(0x61), kDisplay) == LearnResult::RejectedIgnored);
	CHECK_FALSE(layout.learned.contains(Sk(0x61)));

	// A container not yet in the layout becomes a managed container without categories.
	const auto before = layout.containers.size();
	CHECK(Learn(layout, Sk(0x62), kUnmanaged) == LearnResult::Recorded);
	REQUIRE(layout.containers.size() == before + 1);
	CHECK(layout.containers.back().ref == kUnmanaged);
	CHECK(layout.containers.back().categories.empty());
	CHECK_FALSE(layout.containers.back().ignored);
}

TEST_CASE("Smart Learn keeps learned data minimal", "[planner][learn]")
{
	const auto categories = CompileCategories(kStandardCategories);
	auto       layout = MakeLayout();
	const auto bow = Weapon(Sk(0x70), WeaponType::Bow);

	// Putting a bow in the bow rack is what categories would do anyway: nothing to learn.
	CHECK(Learn(layout, bow, kBowRack, categories) == LearnResult::Unchanged);
	CHECK(layout.learned.empty());

	// Putting it somewhere else is learned.
	CHECK(Learn(layout, bow, kLooseChest, categories) == LearnResult::Recorded);
	CHECK(layout.learned.at(Sk(0x70)) == kLooseChest);
	CHECK(Learn(layout, bow, kLooseChest, categories) == LearnResult::Unchanged);

	// Moving it back to its natural place removes the stale entry.
	CHECK(Learn(layout, bow, kBowRack, categories) == LearnResult::Removed);
	CHECK(layout.learned.empty());

	// Explicit bindings count as natural destinations too.
	CHECK(Learn(layout, Weapon(Sk(0x777), WeaponType::Bow), kSatchel, categories) == LearnResult::Unchanged);

	// Homeless items are learned (the master is not a "natural" destination for this purpose).
	CHECK(Learn(layout, MakeItem(Sk(0x71), ItemType::Book), kLooseChest, categories) == LearnResult::Recorded);

	auto quest = bow;
	quest.questItem = true;
	CHECK(Learn(layout, quest, kLooseChest, categories) == LearnResult::RejectedItem);
	CHECK(Learn(layout, bow, kMaster, categories) == LearnResult::RejectedMaster);
	CHECK(Learn(layout, bow, kDisplay, categories) == LearnResult::RejectedIgnored);
}
