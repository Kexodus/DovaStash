#include <catch2/catch_test_macros.hpp>

#include <algorithm>

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
	tpl.layout.declined[Sk(0xB)] = { "books.skill" };  // same

	const auto applied = ApplyTemplate(tpl);
	CHECK(applied.basedOn == "Templates/Vanilla/Breezehome.json@1.2.0");
	CHECK(applied.learned.empty());
	CHECK(applied.declined.empty());
	CHECK(applied.containers.size() == 2);
	CHECK(applied.master == tpl.layout.master);

	// Re-apply over an existing player layout: keep learned entries whose container is still known.
	Layout existing = applied;
	existing.learned.emplace(Sk(0x1), Sk(0xB));      // still a valid container -> kept
	existing.learned.emplace(Sk(0x2), Sk(0xD));      // ignored container, still in the layout -> kept (inert)
	existing.learned.emplace(Sk(0x3), Sk(0xEEEE));   // container in no layout -> dropped
	existing.learned.emplace(Sk(0x4), Sk(0xAAAAA));  // master -> dropped
	existing.declined[Sk(0xB)] = { "alchemy.potions" };  // the player's No -> kept
	existing.declined[Sk(0xEEEE)] = { "keys" };          // container in no layout -> dropped
	existing.containers[0].categories = { "armor.*" };  // player tweak, reset by re-apply

	const auto reapplied = ApplyTemplate(tpl, &existing);
	CHECK(reapplied.learned.size() == 2);
	CHECK(reapplied.learned.contains(Sk(0x1)));
	CHECK(reapplied.learned.contains(Sk(0x2)));
	CHECK(reapplied.declined.size() == 1);
	CHECK(reapplied.declined.at(Sk(0xB)) == std::vector<std::string>{ "alchemy.potions" });
	CHECK(reapplied.containers[0].categories == std::vector<std::string>{ "weapons.*" });

	const auto exported = ExportAsTemplate(reapplied);
	CHECK(exported.learned.empty());
	CHECK(exported.declined.empty());
	CHECK_FALSE(exported.basedOn.has_value());
	CHECK(exported.containers.size() == 2);

	// The lint flags both player-only keys in a template.
	Diagnostics diags;
	ValidateTemplate(tpl.layout, "tpl", diags);
	CHECK(std::ranges::any_of(diags.Items(), [](const Diagnostic& d) { return d.message.find("\"declined\"") != std::string::npos; }));
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

namespace
{
	// A player layout using every part of the schema (DESIGN §6.3).
	Layout FullPlayerLayout()
	{
		Layout layout;
		layout.name = "Whitepeak Tower";
		layout.note = "My tower";
		layout.author = "Player";
		layout.version = "1.0.0";
		layout.cell = F("Tower.esp|0x801");
		layout.master = F("Tower.esp|0x802");
		layout.containers.push_back({ .ref = F("Tower.esp|0x803"), .label = "Armory", .note = "by the forge", .categories = { "weapons.*", "armor.heavy" },
			.forms = { Sk(0x1397E), F("Mod.esp|0x900") } });
		layout.containers.back().extra["color"] = "red";  // unknown key, kept
		layout.containers.push_back({ .ref = F("Tower.esp|0x804"), .label = "Satchel", .categories = { "alchemy.potions" } });
		layout.containers.push_back({ .ref = F("Tower.esp|0x805"), .label = "Barrel", .note = "Respawns", .ignored = true });
		layout.containers.push_back({ .ref = F("Tower.esp|0x806"), .label = "Keepsakes" });  // added by Learn: no categories
		layout.displays.push_back({ .ref = F("Tower.esp|0x810"), .label = "Bow rack", .note = "upstairs", .categories = { "weapons.bows" } });
		layout.displays.push_back({ .ref = F("Displays.esp|0x811"), .kind = DisplayKind::Item, .label = "Mask wall", .items = { Sk(0x61CD6) } });
		layout.displays.push_back({ .ref = F("Tower.esp|0x812"), .label = "Mannequin", .set = F("Armor.esp|0x813") });
		layout.displays.push_back({ .ref = F("Tower.esp|0x814"), .label = "Shield plaque", .ignored = true });
		layout.displays.back().extra["future"] = 1;
		layout.keep = KeepConfig{ .inherit = false,
			.always = { "equipped", "Mod.esp|0x901" },
			.never = { "misc.clutter" },
			.quotas = { QuotaRule{ .category = "ammo.arrows", .quota = 50 }, QuotaRule{ .forms = std::vector<FormRef>{ F("Other.esp|0x902") }, .quota = 2 } } };
		layout.learned.emplace(Sk(0x12EB7), F("Tower.esp|0x806"));
		layout.learned.emplace(F("Mod.esp|0x903"), F("Tower.esp|0x803"));
		layout.basedOn = "Templates/Someone/Tower.json@0.9.0";
		layout.extra["custom"] = nlohmann::json{ { "a", 1 } };
		return layout;
	}
}

TEST_CASE("ReferencedPlugins lists every plugin a layout points into", "[templates][export]")
{
	// First use first: cell, master, containers and their forms, displays, keep forms. Not the displays' items
	// or set (Armor.esp): soft references, skipped where they don't exist (TemplateDisplays.h).
	CHECK(ReferencedPlugins(FullPlayerLayout()) == std::vector<std::string>{ "Tower.esp", "Skyrim.esm", "Mod.esp", "Displays.esp", "Other.esp" });

	// Case-insensitively unique; learned pairs are not looked at.
	Layout mixedCase;
	mixedCase.cell = F("skyrim.esm|0x165A7");
	mixedCase.master = Sk(0xAAAAA);
	mixedCase.learned.emplace(F("Learned.esp|0x1"), Sk(0xAAAAA));
	CHECK(ReferencedPlugins(mixedCase) == std::vector<std::string>{ "skyrim.esm" });
}

TEST_CASE("Exporting a layout and applying it to an empty home gives the same setup back", "[templates][export]")
{
	const auto layout = FullPlayerLayout();
	auto       tpl = ExportAsTemplate(layout);
	tpl.name = "My Tower";
	tpl.author = "Dovahkiin";
	tpl.version = "1.0.0";

	// Player-only data is dropped; requires lists every referenced plugin, so the template validates.
	CHECK(tpl.learned.empty());
	CHECK_FALSE(tpl.basedOn.has_value());
	CHECK(tpl.requiredPlugins == std::vector<std::string>{ "Tower.esp", "Skyrim.esm", "Mod.esp", "Displays.esp", "Other.esp" });
	Diagnostics lint;
	ValidateTemplate(tpl, "Templates/User/My Tower.json", lint);
	INFO((lint.Empty() ? std::string() : lint.Items().front().ToString()));
	CHECK(lint.Empty());
	Diagnostics catLint;
	ValidateLayoutCategories(tpl, CompileCategories(kStandardCategories), "Templates/User/My Tower.json", catLint);
	CHECK(catLint.Empty());

	// Written where the plugin writes player exports, and loaded the way the plugin loads templates.
	TempDir dir;
	REQUIRE(SaveLayoutFile(dir.Path() / "User" / "My Tower.json", tpl));
	Diagnostics loadDiags;
	const auto  templates = LoadTemplateDirectory(dir.Path(), loadDiags);
	INFO((loadDiags.Empty() ? std::string() : loadDiags.Items().front().ToString()));
	CHECK_FALSE(loadDiags.HasErrors());
	CHECK(loadDiags.WarningCount() == 3);  // only the three unknown keys ("preserved"); no template lint
	REQUIRE(templates.size() == 1);
	CHECK(templates[0].source == "Templates/User/My Tower.json");
	CHECK(IsUserTemplate(templates[0].source));

	// The resolver offers it for its cell when its plugins are loaded, and names what is missing otherwise.
	const std::vector<std::string> loaded{ "Skyrim.esm", "Tower.esp", "Mod.esp", "Displays.esp", "Armor.esp", "Other.esp" };
	const auto                     res = ResolveLayout(nullptr, templates, *layout.cell, loaded);
	CHECK(res.kind == LayoutResolution::Kind::Template);
	CHECK(res.chosen == 0u);
	const std::vector<std::string> withoutOther{ "Skyrim.esm", "Tower.esp", "Mod.esp", "Displays.esp" };  // the mannequin's Armor.esp set is soft
	const auto                     rejected = ResolveLayout(nullptr, templates, *layout.cell, withoutOther);
	REQUIRE(rejected.rejected.size() == 1);
	CHECK(rejected.rejected[0].missingPlugins == std::vector<std::string>{ "Other.esp" });

	// Reset home, then use the template: everything but the learned destinations comes back.
	const auto applied = ApplyTemplate(templates[0]);
	Layout     expected = layout;
	expected.learned.clear();
	expected.basedOn = "Templates/User/My Tower.json@1.0.0";
	expected.name = "My Tower";
	expected.author = "Dovahkiin";
	expected.requiredPlugins = tpl.requiredPlugins;
	CHECK(SerializeLayout(applied) == SerializeLayout(expected));
	REQUIRE(applied.keep);
	CHECK(applied.keep->inherit == false);
	REQUIRE(applied.FindDisplay(F("Tower.esp|0x812")));
	CHECK(applied.FindDisplay(F("Tower.esp|0x812"))->set == F("Armor.esp|0x813"));
	REQUIRE(applied.FindDisplay(F("Displays.esp|0x811")));
	CHECK(applied.FindDisplay(F("Displays.esp|0x811"))->kind == DisplayKind::Item);
	REQUIRE(applied.FindContainer(F("Tower.esp|0x805")));
	CHECK(applied.FindContainer(F("Tower.esp|0x805"))->ignored);
	CHECK(applied.FindContainer(F("Tower.esp|0x806")) != nullptr);  // the learned-only container stays known

	// Re-applying it over the layout it came from changes nothing but the identity (learned kept).
	const auto reapplied = ApplyTemplate(templates[0], &layout);
	expected.learned = layout.learned;
	CHECK(SerializeLayout(reapplied) == SerializeLayout(expected));
}

TEST_CASE("Exporting keeps requires entries and unparsed data", "[templates][export]")
{
	Layout layout;
	layout.cell = kCell;
	layout.master = Sk(0xAAAAA);
	layout.requiredPlugins = { "Update.esm", "SKYRIM.ESM" };
	layout.containers.push_back({ .ref = Sk(0xB), .unparsedForms = { "bad|ref" } });
	layout.unparsedContainers.push_back("just a string");
	layout.unparsedLearned["broken"] = "x";

	const auto tpl = ExportAsTemplate(layout);
	CHECK(tpl.requiredPlugins == std::vector<std::string>{ "Update.esm", "SKYRIM.ESM" });  // Skyrim.esm is already listed
	REQUIRE(tpl.containers.size() == 1);
	CHECK(tpl.containers[0].unparsedForms == std::vector<std::string>{ "bad|ref" });
	CHECK(tpl.unparsedContainers.size() == 1);
	CHECK(tpl.unparsedLearned.empty());
}

TEST_CASE("Exporting can keep learned items as item bindings", "[templates][export]")
{
	auto layout = FullPlayerLayout();
	// FullPlayerLayout learned: Skyrim.esm|0x12EB7 -> Keepsakes (0x806), Mod.esp|0x903 -> Armory (0x803).
	layout.learned.emplace(F("Loot.esp|0x1"), F("Tower.esp|0x806"));    // a second item for Keepsakes
	layout.learned.emplace(F("Loot.esp|0x2"), F("Tower.esp|0x802"));    // the master: never used for learned items
	layout.learned.emplace(F("Loot.esp|0x3"), F("Tower.esp|0x805"));    // the ignored barrel: never used either
	layout.learned.emplace(F("Mod.esp|0x900"), F("Tower.esp|0x804"));   // Armory already binds it explicitly: that wins
	layout.learned.emplace(F("Loose.esp|0x4"), F("Tower.esp|0x807"));   // a container the layout does not list
	layout.unparsedLearned["bad"] = "pair";

	SECTION("off by default: learned destinations are dropped")
	{
		std::size_t kept = 99;
		const auto  tpl = ExportAsTemplate(layout, {}, &kept);
		CHECK(kept == 0);
		CHECK(tpl.learned.empty());
		CHECK(tpl.FindContainer(F("Tower.esp|0x806"))->forms.empty());
		CHECK(tpl.FindContainer(F("Tower.esp|0x807")) == nullptr);
		CHECK(std::ranges::find(tpl.requiredPlugins, "Loot.esp") == tpl.requiredPlugins.end());
	}

	SECTION("on: each learned item becomes a binding of its container")
	{
		std::size_t kept = 0;
		auto        tpl = ExportAsTemplate(layout, { .learnedAsForms = true }, &kept);
		CHECK(kept == 4);  // 0x12EB7, Loot 0x1 (Keepsakes), Mod 0x903 (Armory), Loose 0x4 (new entry)
		CHECK(tpl.learned.empty());
		CHECK(tpl.unparsedLearned.empty());
		CHECK_FALSE(tpl.basedOn.has_value());

		// Added in learned order (FormRef order: plugin, then id).
		REQUIRE(tpl.FindContainer(F("Tower.esp|0x806")));
		CHECK(tpl.FindContainer(F("Tower.esp|0x806"))->forms == std::vector<FormRef>{ F("Loot.esp|0x1"), Sk(0x12EB7) });
		REQUIRE(tpl.FindContainer(F("Tower.esp|0x803")));
		CHECK(tpl.FindContainer(F("Tower.esp|0x803"))->forms == std::vector<FormRef>{ Sk(0x1397E), F("Mod.esp|0x900"), F("Mod.esp|0x903") });
		CHECK(tpl.FindContainer(F("Tower.esp|0x803"))->categories == std::vector<std::string>{ "weapons.*", "armor.heavy" });
		CHECK(tpl.FindContainer(F("Tower.esp|0x804"))->forms.empty());  // Mod 0x900 stays with the Armory
		CHECK(tpl.FindContainer(F("Tower.esp|0x805"))->forms.empty());  // ignored
		CHECK(tpl.FindContainer(F("Tower.esp|0x802")) == nullptr);      // the master gets no entry
		REQUIRE(tpl.FindContainer(F("Tower.esp|0x807")));
		CHECK(tpl.FindContainer(F("Tower.esp|0x807"))->forms == std::vector<FormRef>{ F("Loose.esp|0x4") });
		CHECK(tpl.FindContainer(F("Tower.esp|0x807"))->categories.empty());

		// requires gains the plugins of the bound items, so the template still passes the lint.
		CHECK(tpl.requiredPlugins == std::vector<std::string>{ "Tower.esp", "Skyrim.esm", "Mod.esp", "Loot.esp", "Loose.esp", "Displays.esp", "Other.esp" });
		Diagnostics lint;
		ValidateTemplate(tpl, "Templates/User/Mine.json", lint);
		CHECK(lint.Empty());

		// Round trip: written, loaded as a template and applied to an empty home, the items are bindings.
		tpl.name = "Mine";
		tpl.author = "Dovahkiin";
		tpl.version = "1.0.0";
		TempDir dir;
		REQUIRE(SaveLayoutFile(dir.Path() / "User" / "Mine.json", tpl));
		Diagnostics loadDiags;
		const auto  templates = LoadTemplateDirectory(dir.Path(), loadDiags);
		CHECK_FALSE(loadDiags.HasErrors());
		REQUIRE(templates.size() == 1);
		const auto applied = ApplyTemplate(templates[0]);
		CHECK(applied.learned.empty());
		REQUIRE(applied.FindContainer(F("Tower.esp|0x806")));
		CHECK(applied.FindContainer(F("Tower.esp|0x806"))->forms == std::vector<FormRef>{ F("Loot.esp|0x1"), Sk(0x12EB7) });
		REQUIRE(applied.FindContainer(F("Tower.esp|0x807")));
		CHECK(applied.FindContainer(F("Tower.esp|0x807"))->forms == std::vector<FormRef>{ F("Loose.esp|0x4") });
		CHECK(applied.requiredPlugins == tpl.requiredPlugins);
	}

	SECTION("a learned item that is bound explicitly elsewhere is not counted twice")
	{
		Layout small;
		small.cell = kCell;
		small.master = Sk(0xAAAAA);
		small.containers.push_back({ .ref = Sk(0xB), .forms = { Sk(0x100) } });
		small.learned.emplace(Sk(0x100), Sk(0xB));  // same container: a duplicate
		small.learned.emplace(Sk(0x101), Sk(0xB));
		std::size_t kept = 0;
		const auto  tpl = ExportAsTemplate(small, { .learnedAsForms = true }, &kept);
		CHECK(kept == 1);
		CHECK(tpl.containers[0].forms == std::vector<FormRef>{ Sk(0x100), Sk(0x101) });
		CHECK(tpl.requiredPlugins == std::vector<std::string>{ "Skyrim.esm" });
	}
}

TEST_CASE("The player's own exports rank first at equal priority", "[templates][export]")
{
	CHECK(IsUserTemplate("Templates/User/Mine.json"));
	CHECK(IsUserTemplate("templates\\user\\Mine.json"));
	CHECK_FALSE(IsUserTemplate("Templates/Users/Mine.json"));
	CHECK_FALSE(IsUserTemplate("Templates/User/"));
	CHECK_FALSE(IsUserTemplate("Templates/Vanilla/Breezehome.json"));

	const std::vector<TemplateCandidate> templates{
		T("Templates/CreationClub/Home.json", 0, "2.0.0"),
		T("Templates/User/Mine.json", 0, "1.0.0"),
		T("Templates/User/Older.json", 0, "0.9.0"),
		T("Templates/Replacer/Home.json", 1, "1.0.0"),
	};
	const auto res = ResolveLayout(nullptr, templates, kCell, kLoaded);
	CHECK(res.chosen == 3u);  // a higher priority still wins
	CHECK(res.alternatives == std::vector<std::size_t>{ 1, 2, 0 });
}

TEST_CASE("Template file stems and re-save versions", "[templates][export]")
{
	CHECK(TemplateFileStem("Whitepeak Tower") == "Whitepeak Tower");
	CHECK(TemplateFileStem("My/Home: v2?") == "My_Home_ v2_");
	CHECK(TemplateFileStem("  .Home.  ") == "Home");
	CHECK(TemplateFileStem("") == "Template");
	CHECK(TemplateFileStem("...") == "Template");
	CHECK(TemplateFileStem("con") == "con_");
	CHECK(TemplateFileStem("Tour d'\xC3\xA9t\xC3\xA9") == "Tour d___t__");  // every non-ASCII byte becomes '_'
	CHECK(TemplateFileStem(std::string(80, 'a')).size() == 64);

	CHECK(NextPatchVersion("1.0.0") == "1.0.1");
	CHECK(NextPatchVersion("1.2") == "1.2.1");
	CHECK(NextPatchVersion("v2.3.9") == "2.3.10");
	CHECK(NextPatchVersion("1.2.3-beta+5") == "1.2.4");
	CHECK(NextPatchVersion("") == "1.0.0");
	CHECK(NextPatchVersion("first") == "1.0.0");
	CHECK(std::is_gt(CompareVersionStrings(NextPatchVersion("1.0.0-rc.1"), "1.0.0-rc.1")));
}

TEST_CASE("Template file stems avoid Windows device names with any extension", "[templates][export]")
{
	CHECK(TemplateFileStem("CON") == "CON_");
	CHECK(TemplateFileStem("con.txt") == "con_.txt");
	CHECK(TemplateFileStem("COM1.backup") == "COM1_.backup");
	CHECK(TemplateFileStem("Console") == "Console");
	CHECK(TemplateFileStem("my.con") == "my.con");
}

TEST_CASE("Template lint: container forms and keep forms need their plugins in requires too", "[templates]")
{
	Layout tpl;
	tpl.cell = kCell;
	tpl.master = Sk(0xAAAAA);
	tpl.requiredPlugins = { "Skyrim.esm" };
	tpl.containers.push_back({ .ref = Sk(0xB), .forms = { F("Mod.esp|0x9") } });
	tpl.keep = KeepConfig{};
	tpl.keep->always = { "Other.esp|0x5", "equipped" };
	tpl.keep->quotas = { QuotaRule{ .forms = std::vector<FormRef>{ F("Third.esp|0x6") }, .quota = 2 } };
	Diagnostics diags;
	ValidateTemplate(tpl, "t.json", diags);
	const auto mentions = [&](std::string_view a_text) {
		return std::ranges::any_of(diags.Items(), [&](const Diagnostic& d) { return d.message.find(a_text) != std::string::npos; });
	};
	CHECK(mentions("container forms references Mod.esp"));
	CHECK(mentions("keep references Other.esp"));
	CHECK(mentions("keep quota forms references Third.esp"));
	CHECK(diags.WarningCount() == 3);

	// What ExportAsTemplate lists in requires (ReferencedPlugins) is exactly what the lint asks for.
	const auto exported = ExportAsTemplate(tpl);
	Diagnostics clean;
	ValidateTemplate(exported, "t.json", clean);
	CHECK(clean.Empty());
}
