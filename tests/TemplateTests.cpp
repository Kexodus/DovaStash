#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"

using namespace dova::core;
using namespace dova::test;

namespace
{
	const FormRef kCell = Sk(0x165A7);

	TemplateCandidate T(std::string a_source, std::int32_t a_priority, std::string a_version, std::vector<std::string> a_requires = { "Skyrim.esm" },
		std::optional<FormRef> a_cell = kCell)
	{
		TemplateCandidate t;
		t.source = std::move(a_source);
		t.layout.name = t.source;
		t.layout.priority = a_priority;
		t.layout.version = std::move(a_version);
		t.layout.requiredPlugins = std::move(a_requires);
		t.layout.cell = std::move(a_cell);
		t.layout.master = Sk(0xAAAAA);
		return t;
	}

	const std::vector<std::string> kLoaded{ "Skyrim.esm", "Update.esm", "Dawnguard.esm" };
}

TEST_CASE("No candidates resolves to None", "[templates]")
{
	const auto res = ResolveLayout(nullptr, {}, kCell, kLoaded);
	CHECK(res.kind == LayoutResolution::Kind::None);
	CHECK_FALSE(res.chosen.has_value());
	CHECK(res.alternatives.empty());
	CHECK(res.rejected.empty());
}

TEST_CASE("Only templates for the cell are considered", "[templates]")
{
	const std::vector<TemplateCandidate> templates{
		T("other", 100, "9.0.0", { "Skyrim.esm" }, Sk(0x1)),
		T("nocell", 100, "9.0.0", { "Skyrim.esm" }, std::nullopt),
		T("mine", 0, "1.0.0"),
	};
	const auto res = ResolveLayout(nullptr, templates, F("SKYRIM.ESM|0x165a7"), kLoaded);
	CHECK(res.kind == LayoutResolution::Kind::Template);
	CHECK(res.chosen == 2u);
	CHECK(res.alternatives.empty());
}

TEST_CASE("Templates with missing requirements are rejected", "[templates]")
{
	const std::vector<TemplateCandidate> templates{
		T("needs-hf", 10, "1.0.0", { "Skyrim.esm", "HearthFires.esm", "Missing.esp" }),
		T("vanilla", 0, "1.0.0", { "skyrim.esm", "DAWNGUARD.ESM" }),  // case-insensitive
	};
	const auto res = ResolveLayout(nullptr, templates, kCell, kLoaded);
	CHECK(res.kind == LayoutResolution::Kind::Template);
	CHECK(res.chosen == 1u);
	REQUIRE(res.rejected.size() == 1);
	CHECK(res.rejected[0].index == 0u);
	CHECK(res.rejected[0].missingPlugins == std::vector<std::string>{ "HearthFires.esm", "Missing.esp" });

	const std::vector<TemplateCandidate> onlyMissing{ templates[0] };
	const auto none = ResolveLayout(nullptr, onlyMissing, kCell, kLoaded);
	CHECK(none.kind == LayoutResolution::Kind::None);
	CHECK(none.rejected.size() == 1);

	// No requires at all = always eligible.
	const std::vector<TemplateCandidate> noRequires{ T("free", 0, "1.0.0", {}) };
	CHECK(ResolveLayout(nullptr, noRequires, kCell, {}).chosen == 0u);
}

TEST_CASE("Priority beats version; version breaks priority ties", "[templates]")
{
	const std::vector<TemplateCandidate> templates{
		T("a", 0, "9.9.9"),
		T("b", 5, "1.0.0"),
		T("c", 5, "1.10.0"),
		T("d", 5, "1.9.0"),
		T("e", 5, "1.10.0-beta"),
		T("f", 5, "not-a-version"),
	};
	const auto res = ResolveLayout(nullptr, templates, kCell, kLoaded);
	REQUIRE(res.chosen);
	CHECK(*res.chosen == 2u);  // c: 1.10.0
	CHECK(res.alternatives == std::vector<std::size_t>{ 4, 3, 1, 5, 0 });
}

TEST_CASE("Exact ties are broken by source path, deterministically", "[templates]")
{
	const std::vector<TemplateCandidate> templates{
		T("Templates/Zed/Breezehome.json", 0, "1.0.0"),
		T("Templates/alice/Breezehome.json", 0, "1.0"),
		T("Templates/Bob/Breezehome.json", 0, "1.0.0+build"),
	};
	const auto res = ResolveLayout(nullptr, templates, kCell, kLoaded);
	CHECK(res.chosen == 1u);
	CHECK(res.alternatives == std::vector<std::size_t>{ 2, 0 });
}

TEST_CASE("A player layout wins but alternatives are still listed", "[templates]")
{
	Layout                               player;
	const std::vector<TemplateCandidate> templates{ T("a", 0, "1.0.0"), T("b", 1, "1.0.0") };
	const auto                           res = ResolveLayout(&player, templates, kCell, kLoaded);
	CHECK(res.kind == LayoutResolution::Kind::Player);
	CHECK(res.chosen == 1u);
	CHECK(res.alternatives == std::vector<std::size_t>{ 0 });

	CHECK(ResolveLayout(&player, {}, kCell, kLoaded).kind == LayoutResolution::Kind::Player);
}

TEST_CASE("ApplyTemplate copies with basedOn and handles learned data", "[templates]")
{
	auto tpl = T("Templates/Vanilla/Breezehome.json", 0, "1.2.0");
	tpl.layout.containers.push_back({ .ref = Sk(0xB), .label = "Armory", .categories = { "weapons.*" } });
	tpl.layout.containers.push_back({ .ref = Sk(0xD), .label = "Display", .ignored = true });
	tpl.layout.learned.emplace(Sk(0x99), Sk(0xB));  // should never ship, dropped on apply

	const auto applied = ApplyTemplate(tpl);
	CHECK(applied.basedOn == "Templates/Vanilla/Breezehome.json@1.2.0");
	CHECK(applied.learned.empty());
	CHECK(applied.containers.size() == 2);
	CHECK(applied.master == tpl.layout.master);

	// Re-apply over an existing player layout: keep learned entries whose container is still known.
	Layout existing = applied;
	existing.learned.emplace(Sk(0x1), Sk(0xB));      // still a valid container -> kept
	existing.learned.emplace(Sk(0x2), Sk(0xD));      // ignored container, still in the layout -> kept (inert)
	existing.learned.emplace(Sk(0x3), Sk(0xEEEE));   // container in no layout -> dropped
	existing.learned.emplace(Sk(0x4), Sk(0xAAAAA));  // master -> dropped
	existing.containers[0].categories = { "armor.*" };  // player tweak, reset by re-apply

	const auto reapplied = ApplyTemplate(tpl, &existing);
	CHECK(reapplied.learned.size() == 2);
	CHECK(reapplied.learned.contains(Sk(0x1)));
	CHECK(reapplied.learned.contains(Sk(0x2)));
	CHECK(reapplied.containers[0].categories == std::vector<std::string>{ "weapons.*" });

	const auto exported = ExportAsTemplate(reapplied);
	CHECK(exported.learned.empty());
	CHECK_FALSE(exported.basedOn.has_value());
	CHECK(exported.containers.size() == 2);
}

TEST_CASE("ApplyTemplate over a player layout keeps the player's data", "[templates]")
{
	auto tpl = T("Templates/Vanilla/Breezehome.json", 0, "2.0.0");
	tpl.layout.containers.push_back({ .ref = Sk(0xB), .label = "Armory", .categories = { "weapons.*" }, .forms = { Sk(0x50) } });
	tpl.layout.containers.push_back({ .ref = Sk(0xC), .categories = { "alchemy.*" } });
	tpl.layout.containers.push_back({ .ref = Sk(0xD), .label = "Display", .ignored = true });
	tpl.layout.keep = KeepConfig{ .always = { "misc.gems" } };
	tpl.layout.extra["templateKey"] = 1;

	Layout existing;
	existing.cell = kCell;
	existing.master = Sk(0x99999);  // the player used another master
	existing.containers.push_back({ .ref = Sk(0xB), .label = "My armory", .categories = { "armor.*" }, .forms = { Sk(0x51), Sk(0x50) },
		.unparsedForms = { "bad|ref" } });
	existing.containers.push_back({ .ref = Sk(0xC), .label = "Satchel", .ignored = true });  // player ignored it
	existing.containers.back().extra["note"] = "mine";
	existing.containers.push_back({ .ref = Sk(0xF), .label = "Keepsakes", .categories = { "books.*" }, .forms = { Sk(0x52) } });  // not in template
	existing.containers.push_back({ .ref = Sk(0xE), .label = "Junk", .ignored = true });  // not in template, ignored
	existing.learned.emplace(Sk(0x1), Sk(0xF));       // to a player-only container -> kept
	existing.learned.emplace(Sk(0x2), Sk(0xB));       // kept
	existing.learned.emplace(Sk(0x3), Sk(0x99999));   // to the old master (not the new master) -> kept
	existing.learned.emplace(Sk(0x4), Sk(0xAAAAA));   // to the template master -> dropped
	existing.learned.emplace(Sk(0x5), Sk(0x12345));   // container unknown -> dropped
	existing.unparsedLearned["broken"] = "Skyrim.esm|0xB";
	existing.unparsedContainers.push_back("just a string");
	existing.keep = KeepConfig{ .inherit = false, .always = { "ammo.*" } };
	existing.extra["playerKey"] = true;
	existing.extra["templateKey"] = 2;  // template value wins on conflicts

	const auto merged = ApplyTemplate(tpl, &existing);
	CHECK(merged.basedOn == "Templates/Vanilla/Breezehome.json@2.0.0");
	CHECK(merged.master == tpl.layout.master);

	const auto* armory = merged.FindContainer(Sk(0xB));
	REQUIRE(armory);
	CHECK(armory->categories == std::vector<std::string>{ "weapons.*" });  // template categories win
	CHECK(armory->label == "Armory");
	CHECK(armory->forms == std::vector<FormRef>{ Sk(0x50), Sk(0x51) });   // united, no duplicates
	CHECK(armory->unparsedForms == std::vector<std::string>{ "bad|ref" });
	CHECK_FALSE(armory->ignored);

	const auto* satchel = merged.FindContainer(Sk(0xC));
	REQUIRE(satchel);
	CHECK(satchel->ignored);  // the player's ignore survives
	CHECK(satchel->categories == std::vector<std::string>{ "alchemy.*" });
	CHECK(satchel->label == "Satchel");  // template had no label
	CHECK(satchel->extra.value("note", "") == "mine");

	CHECK(merged.FindContainer(Sk(0xD))->ignored);  // template ignore kept

	const auto* keepsakes = merged.FindContainer(Sk(0xF));
	REQUIRE(keepsakes);
	CHECK(keepsakes->categories.empty());  // categories come from the template only
	CHECK(keepsakes->label == "Keepsakes");
	CHECK(keepsakes->forms == std::vector<FormRef>{ Sk(0x52) });
	const auto* junk = merged.FindContainer(Sk(0xE));
	REQUIRE(junk);
	CHECK(junk->ignored);
	const auto* oldMaster = merged.FindContainer(Sk(0x99999));
	REQUIRE(oldMaster);
	CHECK(oldMaster->categories.empty());
	CHECK_FALSE(oldMaster->ignored);

	CHECK(merged.learned.size() == 3);
	CHECK(merged.learned.at(Sk(0x1)) == Sk(0xF));
	CHECK(merged.learned.at(Sk(0x2)) == Sk(0xB));
	CHECK(merged.learned.at(Sk(0x3)) == Sk(0x99999));
	CHECK(merged.unparsedLearned.contains("broken"));
	CHECK(merged.unparsedContainers.size() == 1);

	REQUIRE(merged.keep);
	CHECK(merged.keep->always == std::vector<std::string>{ "ammo.*" });
	CHECK(merged.keep->inherit == false);
	CHECK(merged.extra.value("playerKey", false));
	CHECK(merged.extra.value("templateKey", 0) == 1);

	// Without a player keep block the template's is used.
	existing.keep.reset();
	CHECK(ApplyTemplate(tpl, &existing).keep->always == std::vector<std::string>{ "misc.gems" });

	// A round-trip through JSON keeps everything.
	Diagnostics diags;
	const auto  reparsed = ParseLayoutText(SerializeLayout(merged), "merged.json", diags);
	REQUIRE(reparsed);
	CHECK(SerializeLayout(*reparsed) == SerializeLayout(merged));
}

TEST_CASE("LoadTemplateDirectory walks subdirectories", "[templates][files]")
{
	TempDir dir;
	dir.Write("Vanilla/Breezehome.json", R"({ "$schema": "dovastash-layout-1", "name": "Breezehome", "version": "1.0.0",
		"requires": ["Skyrim.esm"], "cell": "Skyrim.esm|0x165A7", "master": "Skyrim.esm|0xAAAAA", "containers": [] })");
	dir.Write("Someone/Deep/Nested/Alt.json", R"({ "$schema": "dovastash-layout-1", "name": "Alt", "version": "2.0.0",
		"cell": "Skyrim.esm|0x165A7", "master": "Skyrim.esm|0xAAAAA",
		"containers": [ { "ref": "Mod.esp|0x801" } ] })");
	dir.Write("Broken.json", "{");
	dir.Write("notes.md", "# not a template");

	Diagnostics diags;
	const auto  templates = LoadTemplateDirectory(dir.Path(), diags);
	REQUIRE(templates.size() == 2);
	CHECK(templates[0].source == "Templates/Someone/Deep/Nested/Alt.json");
	CHECK(templates[1].source == "Templates/Vanilla/Breezehome.json");
	CHECK(diags.ErrorCount() == 1);  // Broken.json
	// Alt references Skyrim.esm and Mod.esp without requiring them.
	CHECK(diags.WarningCount() == 2);

	const auto res = ResolveLayout(nullptr, templates, kCell, kLoaded);
	CHECK(res.chosen == 0u);  // Alt 2.0.0 > Breezehome 1.0.0, both priority 0

	Diagnostics none;
	CHECK(LoadTemplateDirectory(dir.Path() / "missing", none).empty());
	CHECK(none.Empty());
}

TEST_CASE("Template and category validation lint", "[templates]")
{
	Layout layout;
	layout.cell = kCell;
	layout.requiredPlugins = { "Skyrim.esm" };
	layout.learned.emplace(Sk(0x1), Sk(0x2));
	layout.basedOn = "x@1";
	layout.containers.push_back({ .ref = Sk(0xB), .categories = { "weapons.*", "wepons.swords" } });
	layout.keep = KeepConfig{};
	layout.keep->never = { "misc.clutter", "misc.junk" };

	Diagnostics diags;
	ValidateTemplate(layout, "t.json", diags);
	CHECK(diags.WarningCount() == 3);  // learned, basedOn, no master
	CHECK_FALSE(diags.HasErrors());

	const auto  set = CompileCategories(kStandardCategories);
	Diagnostics catDiags;
	ValidateLayoutCategories(layout, set, "t.json", catDiags);
	CHECK(catDiags.WarningCount() == 2);  // wepons.swords, misc.junk
}
