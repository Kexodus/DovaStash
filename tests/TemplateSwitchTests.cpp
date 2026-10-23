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

TEST_CASE("FindBasedOnSource and TemplateUpdated: a setup made from an older version is offered the update", "[templates][switch]")
{
	const std::vector list{ Make("Templates/Home/A.json", false, 0, "1.0.0"), Make("Templates/Home/B.json", false, 0, "1.0.3") };
	auto              layout = ApplyTemplate(list[1]);
	CHECK(FindBasedOnSource(layout, list) == &list[1]);
	CHECK_FALSE(TemplateUpdated(layout, list));  // the same version
	layout.basedOn = MakeBasedOn("templates/home/b.json", "1.0.2");
	CHECK(FindBasedOn(layout, list) == nullptr);
	CHECK(FindBasedOnSource(layout, list) == &list[1]);
	CHECK(TemplateUpdated(layout, list));
	layout.basedOn = MakeBasedOn("Templates/Home/B.json", "1.1.0");  // newer than the file now: no update
	CHECK_FALSE(TemplateUpdated(layout, list));
	layout.basedOn = MakeBasedOn("Templates/Home/Gone.json", "0.1.0");
	CHECK(FindBasedOnSource(layout, list) == nullptr);
	CHECK_FALSE(TemplateUpdated(layout, list));
	layout.basedOn.reset();
	CHECK(FindBasedOnSource(layout, list) == nullptr);
	CHECK_FALSE(TemplateUpdated(layout, list));
}

TEST_CASE("Updating to a newer template version keeps what the player added", "[templates][switch]")
{
	auto old = Make("Templates/Vanilla/Home.json", false, 0, "1.0.2");
	old.layout.containers.push_back({ .ref = Sk(0xB), .label = "Armory", .categories = { "weapons.*" } });
	auto setup = ApplyTemplate(old);
	setup.cell = kCell;
	setup.learned.emplace(Sk(0x1), Sk(0xB));
	setup.keep = KeepConfig{ .always = { "misc.gems" } };
	setup.containers[0].forms.push_back(Sk(0x50));
	auto current = old;
	current.layout.version = "1.0.3";
	current.layout.containers[0].categories.push_back("ammo.*");
	const std::vector list{ current };
	REQUIRE(TemplateUpdated(setup, list));
	const auto updated = ApplyTemplate(current, &setup);
	CHECK(updated.basedOn == MakeBasedOn(current.source, "1.0.3"));
	CHECK(updated.learned.contains(Sk(0x1)));
	CHECK(updated.keep.has_value());
	CHECK(std::ranges::find(updated.containers[0].forms, Sk(0x50)) != updated.containers[0].forms.end());
	CHECK(std::ranges::find(updated.containers[0].categories, std::string("ammo.*")) != updated.containers[0].categories.end());
	CHECK_FALSE(TemplateUpdated(updated, list));
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

TEST_CASE("A template removed from a home is never offered there", "[templates][hidden]")
{
	const std::vector list{ Make("Templates/Vanilla/Home.json", false, 0), Make("Templates/User/Mine.json", false, 0),
		Make("Templates/Home/Unknown.json", true, 0) };
	Layout mine;
	mine.cell = kCell;

	SECTION("hidden: not chosen, not an alternative, listed apart in rank order")
	{
		REQUIRE(HideTemplate(mine, "templates/user/MINE.json"));  // case-insensitive
		REQUIRE(HideTemplate(mine, "Templates/Vanilla/Home.json"));
		const auto res = ResolveLayout(&mine, list, kCell, kLoaded);
		CHECK(Order(res) == std::vector<std::size_t>{ 2 });  // the auto-draft is all that is left
		CHECK(res.hidden == std::vector<std::size_t>{ 1, 0 });  // the player's own first, as when offered
		CHECK(res.rejected.empty());
	}
	SECTION("hidden even when its plugins are missing: it can still be added back")
	{
		const std::vector withMissing{ Make("Templates/Mod/Home.json", false, 0, "1.0.0", { "Skyrim.esm", "Gone.esp" }) };
		REQUIRE(HideTemplate(mine, "Templates/Mod/Home.json"));
		const auto res = ResolveLayout(&mine, withMissing, kCell, kLoaded);
		CHECK(res.hidden == std::vector<std::size_t>{ 0 });
		CHECK(res.rejected.empty());
	}
	SECTION("Add back offers it again; twice is refused")
	{
		REQUIRE(HideTemplate(mine, "Templates/Vanilla/Home.json"));
		CHECK_FALSE(HideTemplate(mine, "Templates/Vanilla/Home.json"));
		CHECK(UnhideTemplate(mine, "TEMPLATES/vanilla/home.json"));
		CHECK_FALSE(UnhideTemplate(mine, "Templates/Vanilla/Home.json"));
		const auto res = ResolveLayout(&mine, list, kCell, kLoaded);
		CHECK(Order(res) == std::vector<std::size_t>{ 1, 0, 2 });
		CHECK(res.hidden.empty());
	}
	SECTION("removing the template in use clears basedOn and keeps the setup")
	{
		auto applied = ApplyTemplate(list[0]);
		REQUIRE(applied.basedOn == "Templates/Vanilla/Home.json@1.0.0");
		const auto before = SerializeLayout(applied);
		REQUIRE(HideTemplate(applied, "Templates/Vanilla/Home.json"));
		CHECK_FALSE(applied.basedOn.has_value());
		CHECK(applied.master == list[0].layout.master);
		applied.hiddenTemplates.clear();
		applied.basedOn = "Templates/Vanilla/Home.json@1.0.0";
		CHECK(SerializeLayout(applied) == before);  // nothing else changed
	}
	SECTION("removing another template leaves basedOn alone")
	{
		auto applied = ApplyTemplate(list[0]);
		REQUIRE(HideTemplate(applied, "Templates/Vanilla/Home.json.bak"));
		CHECK(applied.basedOn == "Templates/Vanilla/Home.json@1.0.0");
	}
	SECTION("applying another template keeps the list; applying a hidden one adds it back")
	{
		REQUIRE(HideTemplate(mine, "Templates/User/Mine.json"));
		REQUIRE(HideTemplate(mine, "Templates/Home/Unknown.json"));
		const auto other = ApplyTemplate(list[0], &mine);
		CHECK(other.hiddenTemplates == mine.hiddenTemplates);
		const auto back = ApplyTemplate(list[1], &mine);
		CHECK(back.hiddenTemplates == std::vector<std::string>{ "Templates/Home/Unknown.json" });
		CHECK_FALSE(ApplyTemplate(list[0]).hiddenTemplates.size() > 0);
	}
	SECTION("the list is the player's: no own changes from it, and Save as template leaves it out")
	{
		auto applied = ApplyTemplate(list[0]);
		REQUIRE(HideTemplate(applied, "Templates/User/Mine.json"));
		CHECK_FALSE(HasOwnChanges(applied, &list[0]));
		CHECK(ExportAsTemplate(applied).hiddenTemplates.empty());
		Diagnostics diags;
		ValidateTemplate(applied, "t.json", diags);
		CHECK(std::ranges::any_of(diags.Items(), [](const Diagnostic& d) { return d.message.find("hiddenTemplates") != std::string::npos; }));
	}
}

TEST_CASE("hiddenTemplates round-trips; a malformed value is kept verbatim", "[templates][hidden][layout]")
{
	SECTION("written after declined and before basedOn, read back the same")
	{
		Layout layout;
		layout.cell = kCell;
		layout.name = "Home";
		layout.version = "1.0.0";
		layout.basedOn = "Templates/Vanilla/Home.json@1.0.0";
		layout.hiddenTemplates = { "Templates/User/Mine.json" };
		const auto text = SerializeLayout(layout);
		CHECK(text.find("\"hiddenTemplates\"") < text.find("\"basedOn\""));
		Diagnostics diags;
		const auto  back = ParseLayoutText(text, "l.json", diags);
		REQUIRE(back);
		CHECK_FALSE(diags.HasErrors());
		CHECK(back->hiddenTemplates == layout.hiddenTemplates);
		CHECK(SerializeLayout(*back) == text);
	}
	SECTION("not a list of names: an error, kept as it is, and nothing can be hidden until it is fixed")
	{
		Diagnostics diags;
		auto        back = ParseLayoutText(R"({"$schema":"dovastash-layout-1","cell":"Skyrim.esm|0x000165A7","hiddenTemplates":["A.json",3]})", "l.json", diags);
		REQUIRE(back);
		CHECK(diags.HasErrors());
		CHECK(back->hiddenTemplates.empty());
		CHECK(back->extra.contains("hiddenTemplates"));
		CHECK_FALSE(HideTemplate(*back, "Templates/User/Mine.json"));
		CHECK(SerializeLayout(*back).find("3]") != std::string::npos);
	}
	SECTION("no list, no key")
	{
		Layout layout;
		layout.cell = kCell;
		CHECK(SerializeLayout(layout).find("hiddenTemplates") == std::string::npos);
	}
}

TEST_CASE("Switching to a template that makes a container the sell chest drops the player's items bound to it", "[templates][switch][sell]")
{
	const FormRef keepsake = Sk(0xC0001);
	const FormRef other = Sk(0xC0002);
	const FormRef ring = Sk(0x1000A);
	const FormRef sword = Sk(0x1000B);

	Layout mine;
	mine.cell = kCell;
	mine.master = Sk(0xAAAAA);
	ContainerBinding chest;
	chest.ref = keepsake;
	chest.forms = { sword };
	mine.containers.push_back(chest);
	ContainerBinding plain;
	plain.ref = other;
	mine.containers.push_back(plain);
	mine.learned.emplace(ring, keepsake);
	mine.learned.emplace(sword, other);
	mine.declined[keepsake] = { "misc.gems" };

	auto tpl = Make("Templates/User/Shop.json", false);
	ContainerBinding sell;
	sell.ref = keepsake;
	sell.sell = true;
	tpl.layout.containers.push_back(sell);

	const auto applied = ApplyTemplate(tpl, &mine);
	REQUIRE(applied.IsSellChest(keepsake));
	CHECK_FALSE(applied.learned.contains(ring));
	CHECK(applied.learned.contains(sword));  // to another container: kept
	CHECK_FALSE(applied.declined.contains(keepsake));
	CHECK(applied.FindContainer(keepsake)->forms.empty());

	SECTION("a container that was already the sell chest keeps them")
	{
		mine.containers[0].sell = true;
		const auto again = ApplyTemplate(tpl, &mine);
		CHECK(again.learned.contains(ring));
		CHECK(again.FindContainer(keepsake)->forms == std::vector<FormRef>{ sword });
	}
}

TEST_CASE("AbsorbableCells: a cellar set up by itself from its template can join a whole-home template", "[templates][cells]")
{
	const FormRef cellar = Sk(0x4849);
	const FormRef grounds = Sk(0x9BF7);
	auto          cellarTpl = Make("Templates/Vanilla/LakeviewManorCellar.json", false);
	cellarTpl.layout.cell = cellar;
	cellarTpl.layout.master = Sk(0xBE8F);
	cellarTpl.layout.containers.push_back({ .ref = Sk(0xC1), .categories = { "misc.ores_ingots" } });
	auto full = Make("Templates/Vanilla/LakeviewManorFull.json", false, -1);
	full.layout.cells = { HomeCell{ cellar, "Cellar" }, HomeCell{ grounds, "Grounds" }, HomeCell{ kCell } };
	const std::vector<TemplateCandidate> templates{ cellarTpl, full };

	auto cellarLayout = ApplyTemplate(cellarTpl);  // what auto-enable wrote on the first visit
	cellarLayout.cell = cellar;
	const auto lookup = [&](const FormRef& a_cell) -> const Layout* { return a_cell == cellar ? &cellarLayout : nullptr; };

	SECTION("untouched: it can join")
	{
		CHECK(IsUntouchedTemplateSetup(cellarLayout, templates));
		CHECK(AbsorbableCells(full.layout, lookup, templates) == std::vector<FormRef>{ cellar });
	}
	SECTION("anything of the player's keeps it a home of its own")
	{
		SECTION("a learned item") { cellarLayout.learned.emplace(Sk(0x1), Sk(0xC1)); }
		SECTION("a declined suggestion") { cellarLayout.declined[Sk(0xC1)] = { "books.*" }; }
		SECTION("an ignored container") { cellarLayout.containers[0].ignored = true; }
		SECTION("an item binding") { cellarLayout.containers[0].forms.push_back(Sk(0x50)); }
		SECTION("a keep block") { cellarLayout.keep = KeepConfig{ .always = { "misc.gems" } }; }
		SECTION("a template removed") { cellarLayout.hiddenTemplates.emplace_back("Templates/User/Mine.json"); }
		SECTION("a container added") { cellarLayout.containers.push_back({ .ref = Sk(0xC2) }); }
		CHECK_FALSE(IsUntouchedTemplateSetup(cellarLayout, templates));
		CHECK(AbsorbableCells(full.layout, lookup, templates).empty());
	}
	SECTION("custom, its template gone, or a newer version than the template now: in doubt it stays")
	{
		cellarLayout.basedOn.reset();
		CHECK_FALSE(IsUntouchedTemplateSetup(cellarLayout, templates));
		cellarLayout.basedOn = MakeBasedOn(cellarTpl.source, "2.0.0");
		CHECK_FALSE(IsUntouchedTemplateSetup(cellarLayout, templates));
		CHECK(AbsorbableCells(full.layout, lookup, std::span<const TemplateCandidate>{}).empty());
	}
	SECTION("made from an older version of its template: judged by structure")
	{
		cellarLayout.basedOn = MakeBasedOn(cellarTpl.source, "0.9.0");
		cellarLayout.containers[0].categories = { "misc.ores_ingots", "misc.gems" };  // the old version's bindings
		SECTION("nothing of the player's: it can join")
		{
			CHECK(IsUntouchedTemplateSetup(cellarLayout, templates));
			CHECK(AbsorbableCells(full.layout, lookup, templates) == std::vector<FormRef>{ cellar });
		}
		SECTION("anything of the player's keeps it a home of its own")
		{
			SECTION("a learned item") { cellarLayout.learned.emplace(Sk(0x1), Sk(0xC1)); }
			SECTION("a declined suggestion") { cellarLayout.declined[Sk(0xC1)] = { "books.*" }; }
			SECTION("an ignored container") { cellarLayout.containers[0].ignored = true; }
			SECTION("an item binding") { cellarLayout.containers[0].forms.push_back(Sk(0x50)); }
			SECTION("a label") { cellarLayout.containers[0].label = "Ores"; }
			SECTION("a keep block") { cellarLayout.keep = KeepConfig{ .always = { "misc.gems" } }; }
			SECTION("a template removed") { cellarLayout.hiddenTemplates.emplace_back("Templates/User/Mine.json"); }
			SECTION("a container the template does not have") { cellarLayout.containers.push_back({ .ref = Sk(0xC2), .categories = { "books.*" } }); }
			SECTION("another master") { cellarLayout.master = Sk(0xC1); }
			CHECK_FALSE(IsUntouchedTemplateSetup(cellarLayout, templates));
		}
	}
	SECTION("what the caller adds after applying counts as the template's")
	{
		const auto record = [](Layout& a_copy) { a_copy.displays.push_back({ .ref = Sk(0xD1), .items = { Sk(0x77) } }); };
		cellarLayout.displays.push_back({ .ref = Sk(0xD1), .items = { Sk(0x77) } });
		CHECK_FALSE(IsUntouchedTemplateSetup(cellarLayout, templates));
		CHECK(IsUntouchedTemplateSetup(cellarLayout, templates, record));
	}
	SECTION("no file: not listed (it joins anyway); the home's main cell listed again: skipped")
	{
		CHECK(AbsorbableCells(full.layout, [](const FormRef&) -> const Layout* { return nullptr; }, templates).empty());
		CHECK(AbsorbableCells(full.layout, {}, templates).empty());
		const auto any = [&](const FormRef&) -> const Layout* { return &cellarLayout; };
		CHECK(AbsorbableCells(full.layout, any, templates) == std::vector<FormRef>{ cellar, grounds });
	}
}

TEST_CASE("Retired templates: a setup made from one is taken in by the template that replaces it", "[templates][cells]")
{
	const FormRef cellar = Sk(0x4849);
	const FormRef cellarChest = Sk(0xBE8F);
	auto          home = Make("Templates/Vanilla/LakeviewManor.json", false, 0, "1.2.0");
	home.layout.cells = { HomeCell{ .cell = cellar, .label = "Cellar", .master = cellarChest }, HomeCell{ .cell = Sk(0x9BF7) } };
	home.layout.containers.push_back({ .ref = Sk(0xC1), .label = "Cellar end table", .categories = { "misc.ores_ingots" } });
	home.layout.containers.push_back({ .ref = Sk(0xC2), .label = "Hall chest", .categories = { "weapons.*" } });
	home.layout.replaces = { "Templates/Vanilla/LakeviewManorCellar.json" };
	const std::vector<TemplateCandidate> templates{ home };

	// What the retired cellar template wrote on the first visit (its file is gone now).
	Layout old;
	old.name = "Lakeview Manor Cellar";
	old.cell = cellar;
	old.master = cellarChest;
	old.containers.push_back({ .ref = Sk(0xC1), .label = "Cellar end table", .categories = { "misc.ores_ingots", "misc.gems" } });
	old.basedOn = MakeBasedOn("Templates/Vanilla/LakeviewManorCellar.json", "1.0.1");
	const auto lookup = [&](const FormRef& a_cell) -> const Layout* { return a_cell == cellar ? &old : nullptr; };

	SECTION("untouched: it joins, judged by structure; the area master stands in for the home's")
	{
		CHECK(FindReplacement(old, templates) == &templates[0]);
		CHECK(IsUntouchedTemplateSetup(old, templates));
		const auto before = SerializeLayout(old);
		CHECK(AbsorbableCells(ApplyTemplate(home), lookup, templates) == std::vector<FormRef>{ cellar });
		CHECK(SerializeLayout(old) == before);  // a query: nothing in the setup changes, no item moves
	}
	SECTION("anything of the player's keeps it a home of its own")
	{
		SECTION("a learned item") { old.learned.emplace(Sk(0x1), Sk(0xC1)); }
		SECTION("a label") { old.containers[0].label = "Ores"; }
		SECTION("an item binding") { old.containers[0].forms.push_back(Sk(0x50)); }
		SECTION("another master") { old.master = Sk(0xC1); }
		SECTION("a container the template does not have") { old.containers.push_back({ .ref = Sk(0xC3), .categories = { "books.*" } }); }
		CHECK_FALSE(IsUntouchedTemplateSetup(old, templates));
		CHECK(AbsorbableCells(ApplyTemplate(home), lookup, templates).empty());
	}
	SECTION("not replaced, or not one of the template's areas: in doubt it stays")
	{
		SECTION("no replaces") { home.layout.replaces.clear(); }
		SECTION("another retired file") { home.layout.replaces = { "Templates/Vanilla/Other.json" }; }
		SECTION("the cell is not listed") { home.layout.cells.erase(home.layout.cells.begin()); }
		const std::vector<TemplateCandidate> changed{ home };
		CHECK_FALSE(FindReplacement(old, changed));
		CHECK_FALSE(IsUntouchedTemplateSetup(old, changed));
	}
	SECTION("the retired file still there: judged against it, as before")
	{
		auto stillThere = Make("Templates/Vanilla/LakeviewManorCellar.json", false, 0, "1.0.1");
		stillThere.layout.cell = cellar;
		const std::vector<TemplateCandidate> both{ home, stillThere };
		CHECK_FALSE(FindReplacement(old, both));
	}
}

TEST_CASE("ResolveLayout: a cell that is only another template's area names that home (partOf)", "[templates][cells]")
{
	const FormRef cellar = Sk(0x4849);
	auto          home = Make("Templates/Vanilla/LakeviewManor.json", false);
	home.layout.cells = { HomeCell{ .cell = cellar, .label = "Cellar" } };
	auto lower = Make("Templates/User/Mine.json", false, -5);
	lower.layout.cell = Sk(0x2FED);
	lower.layout.cells = { HomeCell{ .cell = cellar } };

	SECTION("no setup, no template of its own: the best template listing it")
	{
		const std::vector list{ lower, home };
		const auto        res = ResolveLayout(nullptr, list, cellar, kLoaded);
		CHECK(res.kind == LayoutResolution::Kind::None);
		CHECK_FALSE(res.chosen);
		REQUIRE(res.partOf);
		CHECK(*res.partOf == 1);
		// The home's own cell resolves to it as always.
		const auto main = ResolveLayout(nullptr, list, kCell, kLoaded);
		CHECK(main.chosen == std::optional<std::size_t>{ 1 });
		CHECK_FALSE(main.partOf);
	}
	SECTION("not when the cell has a setup or a template of its own, nor from a template that cannot be used")
	{
		Layout mine;
		mine.cell = cellar;
		const std::vector list{ home };
		CHECK_FALSE(ResolveLayout(&mine, list, cellar, kLoaded).partOf);

		auto own = Make("Templates/User/Cellar.json", false);
		own.layout.cell = cellar;
		const std::vector withOwn{ home, own };
		const auto        res = ResolveLayout(nullptr, withOwn, cellar, kLoaded);
		CHECK(res.chosen == std::optional<std::size_t>{ 1 });
		CHECK_FALSE(res.partOf);

		home.layout.requiredPlugins.push_back("HearthFires.esm");
		const std::vector missing{ home };
		CHECK_FALSE(ResolveLayout(nullptr, missing, cellar, kLoaded).partOf);
	}
}
