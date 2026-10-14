// Choosing among a home's templates (DESIGN §6.2): a template someone made always wins over an auto-draft
// (DESIGN §6.8), whatever the priority or version; and whether switching templates would replace changes the
// player made to their setup (HasOwnChanges, the panel's confirmation).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "TestHelpers.h"

using namespace dova::core;
using namespace dova::test;

namespace
{
	const FormRef                  kCell = Sk(0x165A7);
	const std::vector<std::string> kLoaded{ "Skyrim.esm" };

	TemplateCandidate Make(std::string a_source, bool a_autoDraft, std::int32_t a_priority = 0, std::string a_version = "1.0.0",
		std::vector<std::string> a_requires = { "Skyrim.esm" })
	{
		TemplateCandidate t;
		t.source = std::move(a_source);
		t.layout.name = t.source;
		t.layout.autoDraft = a_autoDraft;
		t.layout.priority = a_priority;
		t.layout.version = std::move(a_version);
		t.layout.requiredPlugins = std::move(a_requires);
		t.layout.cell = kCell;
		t.layout.master = Sk(0xAAAAA);
		return t;
	}

	// The order the templates are offered in: the chosen one, then the alternatives.
	std::vector<std::size_t> Order(const LayoutResolution& a_res)
	{
		std::vector<std::size_t> out;
		if (a_res.chosen) {
			out.push_back(*a_res.chosen);
		}
		out.insert(out.end(), a_res.alternatives.begin(), a_res.alternatives.end());
		return out;
	}
}

TEST_CASE("A template someone made wins over an auto-draft at any priority", "[templates][autodraft][ranking]")
{
	SECTION("auto-draft at a higher priority")
	{
		const std::vector list{ Make("Templates/Home/Unknown.json", true, 10), Make("Templates/Home/Me.json", false, 0) };
		const auto        res = ResolveLayout(nullptr, list, kCell, kLoaded);
		CHECK(res.kind == LayoutResolution::Kind::Template);
		CHECK(Order(res) == std::vector<std::size_t>{ 1, 0 });
	}
	SECTION("auto-draft at an equal priority, with a higher version")
	{
		const std::vector list{ Make("Templates/Home/Unknown.json", true, 0, "3.0.0"), Make("Templates/Home/Me.json", false, 0, "1.0.0") };
		CHECK(Order(ResolveLayout(nullptr, list, kCell, kLoaded)) == std::vector<std::size_t>{ 1, 0 });
	}
	SECTION("auto-draft at a lower priority")
	{
		const std::vector list{ Make("Templates/Home/Me.json", false, 0), Make("Templates/Home/Unknown.json", true, -5) };
		CHECK(Order(ResolveLayout(nullptr, list, kCell, kLoaded)) == std::vector<std::size_t>{ 0, 1 });
	}
	SECTION("a person's template at a negative priority still beats an auto-draft")
	{
		const std::vector list{ Make("Templates/Home/Unknown.json", true, 0), Make("Templates/Home/Me.json", false, -100) };
		CHECK(Order(ResolveLayout(nullptr, list, kCell, kLoaded)) == std::vector<std::size_t>{ 1, 0 });
	}
	SECTION("being in Templates/User does not lift an auto-draft above a shipped template")
	{
		const std::vector list{ Make("Templates/User/Home.json", true, 0), Make("Templates/Vanilla/Home.json", false, 0) };
		CHECK(Order(ResolveLayout(nullptr, list, kCell, kLoaded)) == std::vector<std::size_t>{ 1, 0 });
	}
	SECTION("the same with a player layout: the order the panel lists")
	{
		const std::vector list{ Make("Templates/Home/Unknown.json", true, 10), Make("Templates/Home/Me.json", false, 0) };
		Layout            mine;
		mine.cell = kCell;
		const auto res = ResolveLayout(&mine, list, kCell, kLoaded);
		CHECK(res.kind == LayoutResolution::Kind::Player);
		CHECK(Order(res) == std::vector<std::size_t>{ 1, 0 });
	}
}

TEST_CASE("Between templates someone made, the usual ranking applies", "[templates][ranking]")
{
	// Priority, then the player's own, then the newest version, then the path.
	const std::vector list{ Make("Templates/A/Home.json", false, 0, "2.0.0"), Make("Templates/B/Home.json", false, 1, "1.0.0"),
		Make("Templates/User/Home.json", false, 0, "1.0.0"), Make("Templates/C/Home.json", false, 0, "2.0.0") };
	CHECK(Order(ResolveLayout(nullptr, list, kCell, kLoaded)) == std::vector<std::size_t>{ 1, 2, 0, 3 });

	// The same order among auto-drafts, all below every template someone made.
	const std::vector drafts{ Make("Templates/A/Unknown.json", true, 0, "1.0.0"), Make("Templates/B/Unknown.json", true, 2, "1.0.0"),
		Make("Templates/C/Me.json", false, -1, "1.0.0") };
	CHECK(Order(ResolveLayout(nullptr, drafts, kCell, kLoaded)) == std::vector<std::size_t>{ 2, 1, 0 });
}

TEST_CASE("An auto-draft is offered when it is all the home has", "[templates][autodraft][ranking]")
{
	SECTION("only an auto-draft")
	{
		const std::vector list{ Make("Templates/Home/Unknown.json", true) };
		const auto        res = ResolveLayout(nullptr, list, kCell, kLoaded);
		CHECK(res.kind == LayoutResolution::Kind::Template);
		CHECK(res.chosen == 0u);
		CHECK(res.alternatives.empty());
	}
	SECTION("a person's template whose plugins are missing does not count")
	{
		const std::vector list{ Make("Templates/Home/Me.json", false, 0, "1.0.0", { "Skyrim.esm", "Missing.esp" }), Make("Templates/Home/Unknown.json", true) };
		const auto        res = ResolveLayout(nullptr, list, kCell, kLoaded);
		CHECK(res.chosen == 1u);
		REQUIRE(res.rejected.size() == 1);
		CHECK(res.rejected.front().index == 0u);
	}
}

TEST_CASE("CompareTemplateRank is the ResolveLayout order without the path", "[templates][ranking]")
{
	const auto mine = Make("Templates/Home/Me.json", false, 0).layout;
	const auto draft = Make("Templates/Home/Unknown.json", true, 9).layout;
	CHECK(CompareTemplateRank(mine, false, draft, false) < 0);
	CHECK(CompareTemplateRank(draft, false, mine, false) > 0);
	CHECK(CompareTemplateRank(draft, true, mine, false) > 0);  // being the player's own does not lift an auto-draft
	CHECK(CompareTemplateRank(mine, false, mine, false) == 0);
	CHECK(CompareTemplateRank(mine, true, mine, false) < 0);
	auto newer = mine;
	newer.version = "1.1.0";
	CHECK(CompareTemplateRank(newer, false, mine, false) < 0);
	auto higher = mine;
	higher.priority = 1;
	CHECK(CompareTemplateRank(higher, false, newer, true) < 0);
}

TEST_CASE("FindBasedOn finds the exact template a layout was made from", "[templates][switch]")
{
	const std::vector list{ Make("Templates/Home/A.json", false, 0, "1.0.0"), Make("Templates/Home/B.json", false, 0, "2.0.0") };
	auto              layout = ApplyTemplate(list[1]);
	CHECK(FindBasedOn(layout, list) == &list[1]);
	layout.basedOn = MakeBasedOn("Templates/Home/B.json", "1.0.0");  // the template has been updated since
	CHECK(FindBasedOn(layout, list) == nullptr);
	layout.basedOn.reset();
	CHECK(FindBasedOn(layout, list) == nullptr);
}

TEST_CASE("HasOwnChanges: what switching templates would replace", "[templates][switch]")
{
	auto tpl = Make("Templates/Vanilla/Home.json", false);
	tpl.layout.containers.push_back({ .ref = Sk(0xB), .label = "Armory", .categories = { "weapons.*" } });
	tpl.layout.containers.push_back({ .ref = Sk(0xC), .categories = { "alchemy.*" } });
	tpl.layout.displays.push_back({ .ref = Sk(0xD0), .categories = { "weapons.swords" } });
	tpl.layout.keep = KeepConfig{ .always = { "misc.gems" } };
	auto applied = ApplyTemplate(tpl);
	applied.cell = kCell;

	SECTION("as applied: nothing of the player's own")
	{
		CHECK_FALSE(HasOwnChanges(applied, &tpl));
	}
	SECTION("what applying a template always keeps does not count")
	{
		applied.learned.emplace(Sk(0x1), Sk(0xB));
		applied.containers.push_back({ .ref = Sk(0xF) });  // added by Learn, no categories
		applied.learned.emplace(Sk(0x2), Sk(0xF));
		applied.containers[1].ignored = true;             // Ignore
		applied.containers[0].forms.push_back(Sk(0x50));  // an explicit item binding
		applied.keep = KeepConfig{ .always = { "ammo.*" } };
		applied.displays.front().ignored = true;  // Stop using this rack
		applied.declined[Sk(0xB)] = { "armor" };
		CHECK_FALSE(HasOwnChanges(applied, &tpl));
	}
	SECTION("a category the player bound")
	{
		applied.containers[0].categories.push_back("armor.*");
		CHECK(HasOwnChanges(applied, &tpl));
	}
	SECTION("a container the player added with categories")
	{
		applied.containers.push_back({ .ref = Sk(0xF), .categories = { "books.*" } });
		CHECK(HasOwnChanges(applied, &tpl));
	}
	SECTION("another master")
	{
		applied.master = Sk(0xC);
		CHECK(HasOwnChanges(applied, &tpl));
	}
	SECTION("a rack's categories")
	{
		applied.displays.front().categories = { "weapons.daggers" };
		CHECK(HasOwnChanges(applied, &tpl));
	}
	SECTION("a container the template labels, renamed")
	{
		applied.containers[0].label = "Swords";
		CHECK(HasOwnChanges(applied, &tpl));
	}
	SECTION("a container of the template the player removed")
	{
		applied.containers.erase(applied.containers.begin() + 1);
		CHECK(HasOwnChanges(applied, &tpl));
	}
	SECTION("a custom setup, or a template that is gone or updated: in doubt, changes")
	{
		CHECK(HasOwnChanges(applied, nullptr));
		auto updated = tpl;
		updated.layout.version = "1.0.1";
		CHECK(HasOwnChanges(applied, &updated));
		auto custom = applied;
		custom.basedOn.reset();
		CHECK(HasOwnChanges(custom, &tpl));
	}
	SECTION("the caller's own step after applying runs on the copy")
	{
		// The plugin writes what a plaque is made for after applying, in place of the template's list; the same step
		// on the copy keeps them equal.
		auto plaque = tpl;
		plaque.layout.displays.front().items = { Sk(0x66) };
		applied.displays.front().items = { Sk(0x77) };
		CHECK(HasOwnChanges(applied, &plaque));
		CHECK_FALSE(HasOwnChanges(applied, &plaque, [](Layout& a_layout) { a_layout.displays.front().items = { Sk(0x77) }; }));
		// Where the template names no items, the player's stay as they are: nothing to replace.
		CHECK_FALSE(HasOwnChanges(applied, &tpl));
	}
}
