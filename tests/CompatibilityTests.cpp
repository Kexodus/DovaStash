// Living next to other home mods (DESIGN §9.1): the list of homes that sort themselves, template `conflicts`, and
// the auto-enable guard (template fit).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>

#include "TestHelpers.h"

using namespace dova::core;
using namespace dova::test;

#ifndef DOVA_DIST_DIR
#	error "DOVA_DIST_DIR must be defined by tests/CMakeLists.txt"
#endif

namespace
{
	bool Mentions(const Diagnostics& a_diags, std::string_view a_text)
	{
		return std::ranges::any_of(a_diags.Items(), [&](const Diagnostic& d) { return d.message.find(a_text) != std::string::npos; });
	}

	constexpr std::string_view kList = R"({
		"$schema": "dovastash-selfsorting-1",
		"homes": [
			{"plugin": "Clockwork.esp", "name": "Clockwork"},
			{"plugin": "LSESD_TundraHomestead.esp", "cells": ["ccEEJSSE001-Hstead.esm|0x0000087F"]},
			{"plugin": "Carpenter Chests.esp", "containers": ["HearthFires.esm|0x00003055"]},
			{"plugin": "Both.esp", "cells": "Skyrim.esm|0x000165A8", "containers": ["Skyrim.esm|0x000CB437"], "note": "both kinds"}
		]
	})";

	const std::vector<std::string> kAllLoaded{ "Skyrim.esm", "HearthFires.esm", "ccEEJSSE001-Hstead.esm", "clockwork.ESP", "LSESD_TundraHomestead.esp",
		"Carpenter Chests.esp", "Both.esp" };
}

TEST_CASE("Self-sorting list: entries parse with their scope", "[compat]")
{
	Diagnostics diags;
	const auto  homes = ParseSelfSortingText(kList, "SelfSorting/test.json", diags);
	CHECK(diags.Empty());
	REQUIRE(homes.size() == 4);
	CHECK(homes[0].WholePlugin());
	CHECK(homes[0].name == "Clockwork");
	CHECK(homes[0].source == "SelfSorting/test.json");
	CHECK_FALSE(homes[1].WholePlugin());
	CHECK(homes[1].cells == std::vector<FormRef>{ F("ccEEJSSE001-Hstead.esm|0x87F") });
	CHECK(homes[2].containers == std::vector<FormRef>{ F("HearthFires.esm|0x3055") });
	CHECK(homes[3].cells.size() == 1);  // a single string is a list of one
	CHECK(homes[3].note == "both kinds");
}

TEST_CASE("Self-sorting list: whole plugin, listed cells, listed containers", "[compat]")
{
	Diagnostics            diags;
	const SelfSortingHomes list(ParseSelfSortingText(kList, "test", diags), kAllLoaded);
	REQUIRE(list.Entries().size() == 4);

	SECTION("whole plugin: the plugin's own cells, case-insensitive")
	{
		const auto* e = list.ForCell(F("CLOCKWORK.esp|0x00001234"));
		REQUIRE(e);
		CHECK(e->plugin == "Clockwork.esp");
	}
	SECTION("listed cells: whichever plugin they come from; the patch's own cells are not meant")
	{
		const auto* e = list.ForCell(F("ccEEJSSE001-Hstead.esm|0x0000087F"));
		REQUIRE(e);
		CHECK(e->plugin == "LSESD_TundraHomestead.esp");
		CHECK_FALSE(list.ForCell(F("ccEEJSSE001-Hstead.esm|0x00000B07")));
		CHECK_FALSE(list.ForCell(F("LSESD_TundraHomestead.esp|0x00000800")));
	}
	SECTION("containers only: the home is not left alone, the containers are")
	{
		CHECK_FALSE(list.ForCell(F("HearthFires.esm|0x00002FEC")));
		CHECK_FALSE(list.ForCell(F("Carpenter Chests.esp|0x00000800")));
		const auto base = F("HearthFires.esm|0x00003055");
		CHECK(list.ForContainer(F("HearthFires.esm|0x00004000"), &base));  // any reference of the base container
		CHECK_FALSE(list.ForContainer(F("HearthFires.esm|0x00004000")));     // no base given: only references match
		CHECK(list.ForContainer(base));
	}
	SECTION("cells and containers together: both apply")
	{
		CHECK(list.ForCell(F("Skyrim.esm|0x000165A8")));
		CHECK(list.ForContainer(F("Skyrim.esm|0x000CB437")));
		CHECK_FALSE(list.ForContainer(F("Skyrim.esm|0x000CB438")));
	}
}

TEST_CASE("Self-sorting list: an entry applies only while its plugin is loaded", "[compat]")
{
	Diagnostics                    diags;
	const std::vector<std::string> loaded{ "Skyrim.esm", "HearthFires.esm", "ccEEJSSE001-Hstead.esm" };
	const SelfSortingHomes         list(ParseSelfSortingText(kList, "test", diags), loaded);
	CHECK(list.Empty());
	CHECK_FALSE(list.ForCell(F("ccEEJSSE001-Hstead.esm|0x0000087F")));  // Tundra Homestead without the Legacy patch: ours
	const auto base = F("HearthFires.esm|0x00003055");
	CHECK_FALSE(list.ForContainer(F("HearthFires.esm|0x00004000"), &base));
}

TEST_CASE("Self-sorting list: bad entries are skipped, never widened", "[compat]")
{
	Diagnostics diags;
	const auto  homes = ParseSelfSortingText(R"({
		"$schema": "dovastash-selfsorting-1",
		"homes": [
			{"name": "no plugin"},
			{"plugin": "NotAPlugin.txt"},
			"Clockwork.esp",
			{"plugin": "Narrow.esp", "cells": ["bad ref"]},
			{"plugin": "Partly.esp", "cells": ["bad ref", "Skyrim.esm|0x000165A8"], "extra": 1},
			{"plugin": "Ok.esp", "name": 5}
		]
	})",
		"test", diags);
	REQUIRE(homes.size() == 2);
	CHECK(homes[0].plugin == "Partly.esp");
	CHECK(homes[0].cells == std::vector<FormRef>{ Sk(0x165A8) });
	CHECK(homes[1].plugin == "Ok.esp");
	CHECK(homes[1].WholePlugin());
	CHECK(Mentions(diags, "homes[0]: \"plugin\" must be a plugin file name"));
	CHECK(Mentions(diags, "homes[1]: \"plugin\" must be a plugin file name"));
	CHECK(Mentions(diags, "homes[2]: expected an object"));
	CHECK(Mentions(diags, "homes[3]: no usable \"cells\" or \"containers\""));  // never the whole of Narrow.esp
	CHECK(Mentions(diags, "homes[4].extra: unknown field"));
	CHECK(Mentions(diags, "homes[5].name: expected a string"));
	CHECK(diags.HasErrors());
}

TEST_CASE("Self-sorting list: unusable documents yield nothing", "[compat]")
{
	Diagnostics wrongSchema;
	CHECK(ParseSelfSortingText(R"({"$schema": "dovastash-layout-1", "homes": []})", "test", wrongSchema).empty());
	CHECK(wrongSchema.HasErrors());
	Diagnostics noHomes;
	CHECK(ParseSelfSortingText(R"({"$schema": "dovastash-selfsorting-1", "homes": {}})", "test", noHomes).empty());
	CHECK(noHomes.HasErrors());
	Diagnostics notJson;
	CHECK(ParseSelfSortingText("{", "test", notJson).empty());
	CHECK(notJson.HasErrors());
}

TEST_CASE("Self-sorting list: a folder loads in file-name order", "[compat]")
{
	TempDir dir;
	dir.Write("50_Patch.json", R"({"$schema": "dovastash-selfsorting-1", "homes": [{"plugin": "Patch.esp"}]})");
	dir.Write("00_Default.json", R"({"$schema": "dovastash-selfsorting-1", "homes": [{"plugin": "Clockwork.esp"}]})");
	dir.Write("readme.txt", "not loaded");
	Diagnostics diags;
	const auto  homes = LoadSelfSortingDirectory(dir.Path(), diags);
	CHECK(diags.Empty());
	REQUIRE(homes.size() == 2);
	CHECK(homes[0].plugin == "Clockwork.esp");
	CHECK(homes[0].source == "SelfSorting/00_Default.json");
	CHECK(homes[1].plugin == "Patch.esp");

	Diagnostics missing;
	CHECK(LoadSelfSortingDirectory(dir.Path() / "nope", missing).empty());
	CHECK(missing.WarningCount() == 1);
}

TEST_CASE("Shipped self-sorting list parses and keeps the homes that sorted themselves before", "[compat][dist]")
{
	Diagnostics diags;
	const auto  homes = LoadSelfSortingDirectory(std::filesystem::path(DOVA_DIST_DIR) / "SelfSorting", diags);
	for (const auto& d : diags.Items()) {
		UNSCOPED_INFO(d.ToString());
	}
	CHECK(diags.Empty());
	const std::vector<std::string> loaded{ "Clockwork.esp", "LegacyoftheDragonborn.esm", "LSESD_TundraHomestead.esp", "Carpenter Chests.esp" };
	const SelfSortingHomes         list(homes, loaded);
	CHECK(list.ForCell(F("Clockwork.esp|0x00000D62")));
	CHECK(list.ForCell(F("LegacyoftheDragonborn.esm|0x00000D62")));
	// The shipped Tundra Homestead templates' cells.
	CHECK(list.ForCell(F("ccEEJSSE001-Hstead.esm|0x0000087F")));
	CHECK(list.ForCell(F("ccEEJSSE001-Hstead.esm|0x00000B06")));
	// Carpenter Storage Chests: the building-supply chests only, not the Hearthfire homes.
	const auto supplies = F("HearthFires.esm|0x00003055");
	CHECK(list.ForContainer(F("HearthFires.esm|0x00004000"), &supplies));
	CHECK_FALSE(list.ForCell(F("HearthFires.esm|0x00002FEC")));
	CHECK_FALSE(list.ForCell(F("Skyrim.esm|0x000165A8")));  // PlayerHouse.esp is not loaded
}

// --- conflicts -------------------------------------------------------------------------------------------------------

TEST_CASE("Template conflicts round-trip after requires", "[compat][layout]")
{
	Diagnostics diags;
	const auto  layout = ParseLayoutText(R"({
		"$schema": "dovastash-layout-1",
		"name": "Breezehome",
		"requires": ["Skyrim.esm"],
		"conflicts": ["WynterBreezehomeRedux.esp", "PlayerHouse.esp"],
		"cell": "Skyrim.esm|0x000165A8",
		"containers": []
	})",
		"test", diags);
	REQUIRE(layout);
	CHECK(diags.Empty());
	CHECK(layout->conflictPlugins == std::vector<std::string>{ "WynterBreezehomeRedux.esp", "PlayerHouse.esp" });
	CHECK(layout->extra.empty());
	const auto text = SerializeLayout(*layout);
	CHECK(text.find("\"conflicts\"") > text.find("\"requires\""));
	CHECK(text.find("\"conflicts\"") < text.find("\"cell\""));
	Diagnostics again;
	const auto  reparsed = ParseLayoutText(text, "test", again);
	REQUIRE(reparsed);
	CHECK(reparsed->conflictPlugins == layout->conflictPlugins);

	// None: not written.
	auto none = *layout;
	none.conflictPlugins.clear();
	CHECK(SerializeLayout(none).find("conflicts") == std::string::npos);

	// The wrong type is kept verbatim.
	Diagnostics bad;
	const auto  wrong = ParseLayoutText(R"({"$schema": "dovastash-layout-1", "cell": "Skyrim.esm|0x000165A8", "containers": [], "conflicts": 5})", "test", bad);
	REQUIRE(wrong);
	CHECK(bad.HasErrors());
	CHECK(wrong->conflictPlugins.empty());
	CHECK(SerializeLayout(*wrong).find("\"conflicts\": 5") != std::string::npos);
}

TEST_CASE("A template whose conflicts are loaded is not offered", "[compat][templates]")
{
	TemplateCandidate breezehome;
	breezehome.source = "Templates/Vanilla/Breezehome.json";
	breezehome.layout.cell = Sk(0x165A8);
	breezehome.layout.requiredPlugins = { "Skyrim.esm" };
	breezehome.layout.conflictPlugins = { "WynterBreezehomeRedux.esp", "PlayerHouse.esp" };
	auto other = breezehome;
	other.source = "Templates/Someone/Breezehome.json";
	other.layout.conflictPlugins.clear();
	const std::vector<TemplateCandidate> templates{ breezehome, other };

	const std::vector<std::string> vanilla{ "Skyrim.esm" };
	const auto                     plain = ResolveLayout(nullptr, templates, Sk(0x165A8), vanilla);
	CHECK(plain.rejected.empty());
	CHECK(plain.alternatives.size() == 1);

	const std::vector<std::string> wynter{ "Skyrim.esm", "wynterbreezehomeredux.ESP" };
	const auto                     res = ResolveLayout(nullptr, templates, Sk(0x165A8), wynter);
	CHECK(res.kind == LayoutResolution::Kind::Template);
	CHECK(res.chosen == 1u);  // the one without conflicts
	REQUIRE(res.rejected.size() == 1);
	CHECK(res.rejected[0].index == 0u);
	CHECK(res.rejected[0].missingPlugins.empty());
	CHECK(res.rejected[0].conflictingPlugins == std::vector<std::string>{ "WynterBreezehomeRedux.esp" });
	CHECK(LoadedConflicts(breezehome.layout, wynter) == std::vector<std::string>{ "WynterBreezehomeRedux.esp" });

	// A player layout still wins; the template only stops being offered.
	Layout player;
	player.cell = Sk(0x165A8);
	const std::vector<TemplateCandidate> onlyBreezehome{ breezehome };
	const auto                           mine = ResolveLayout(&player, onlyBreezehome, Sk(0x165A8), wynter);
	CHECK(mine.kind == LayoutResolution::Kind::Player);
	CHECK_FALSE(mine.chosen.has_value());
}

TEST_CASE("Template lint warns about conflicts that can never be right", "[compat][templates]")
{
	Layout t;
	t.cell = F("Wynter.esp|0x00000800");
	t.master = F("Wynter.esp|0x00000801");
	t.requiredPlugins = { "Wynter.esp", "Skyrim.esm" };
	t.conflictPlugins = { "skyrim.esm", "wynter.ESP", "Wynter's Breezehome", "Fine.esp" };
	Diagnostics diags;
	ValidateTemplate(t, "test", diags);
	CHECK(Mentions(diags, "conflicts: skyrim.esm is also in \"requires\""));
	CHECK(Mentions(diags, "conflicts: wynter.ESP is also in \"requires\""));
	CHECK(Mentions(diags, "\"Wynter's Breezehome\" is not a plugin file name"));
	CHECK_FALSE(Mentions(diags, "Fine.esp"));
	CHECK_FALSE(diags.HasErrors());

	Layout own;
	own.cell = F("Home.esp|0x00000800");
	own.master = F("Home.esp|0x00000801");
	own.conflictPlugins = { "Home.esp" };
	Diagnostics ownDiags;
	ValidateTemplate(own, "test", ownDiags);
	CHECK(Mentions(ownDiags, "the plugin of the template's own cell"));
}

TEST_CASE("Conflicts come with a template and do not count as the player's own change", "[compat][templates]")
{
	TemplateCandidate t;
	t.source = "Templates/Vanilla/Breezehome.json";
	t.layout.version = "1.0.0";
	t.layout.cell = Sk(0x165A8);
	t.layout.master = Sk(0xF3922);
	t.layout.conflictPlugins = { "WynterBreezehomeRedux.esp" };
	auto applied = ApplyTemplate(t);
	CHECK(applied.conflictPlugins == t.layout.conflictPlugins);
	CHECK_FALSE(HasOwnChanges(applied, &t));
	// A layout applied before the template had conflicts is still the template's setup.
	applied.conflictPlugins.clear();
	CHECK_FALSE(HasOwnChanges(applied, &t));
	// Exported again, the conflicts stay with it.
	CHECK(ExportAsTemplate(ApplyTemplate(t)).conflictPlugins == t.layout.conflictPlugins);
}

TEST_CASE("Shipped city-home templates conflict with the mods that rebuild them", "[compat][dist]")
{
	Diagnostics diags;
	const auto  templates = LoadTemplateDirectory(std::filesystem::path(DOVA_DIST_DIR) / "Templates", diags);
	const auto  find = [&](std::string_view a_source) {
		const auto it = std::ranges::find(templates, a_source, &TemplateCandidate::source);
		REQUIRE(it != templates.end());
		return it->layout;
	};
	const auto breezehome = find("Templates/Vanilla/Breezehome.json");
	CHECK(std::ranges::find(breezehome.conflictPlugins, "WynterBreezehomeRedux.esp") != breezehome.conflictPlugins.end());
	for (const auto* source : { "Templates/Vanilla/Breezehome.json", "Templates/Vanilla/Honeyside.json", "Templates/Vanilla/Hjerim.json",
			 "Templates/Vanilla/ProudspireManor.json", "Templates/Vanilla/VlindrelHall.json" }) {
		INFO(source);
		const auto t = find(source);
		CHECK(std::ranges::find(t.conflictPlugins, "PlayerHouse.esp") != t.conflictPlugins.end());
	}
}

// --- template fit (the auto-enable guard) ----------------------------------------------------------------------------

namespace
{
	Layout FitTemplate(std::size_t a_containers)
	{
		Layout t;
		t.cell = Sk(0x165A8);
		t.master = Sk(0x100);
		for (std::size_t i = 0; i < a_containers; ++i) {
			ContainerBinding c;
			c.ref = Sk(0x200 + static_cast<std::uint32_t>(i));
			c.ignored = i % 3 == 0;  // ignored entries are the home's furniture too
			t.containers.push_back(std::move(c));
		}
		return t;
	}

	auto Probe(std::map<FormRef, RefPresence> a_states, RefPresence a_default = RefPresence::Present)
	{
		return [states = std::move(a_states), a_default](const FormRef& a_ref) {
			const auto it = states.find(a_ref);
			return it != states.end() ? it->second : a_default;
		};
	}
}

TEST_CASE("Template fit: the references a template names, each once", "[compat][fit]")
{
	auto t = FitTemplate(2);
	ContainerBinding masterAgain;
	masterAgain.ref = Sk(0x100);
	t.containers.push_back(masterAgain);
	DisplayBinding rack;
	rack.ref = Sk(0x300);
	t.displays.push_back(rack);
	CHECK(TemplateFitRefs(t) == std::vector<FormRef>{ Sk(0x100), Sk(0x200), Sk(0x201), Sk(0x300) });
}

TEST_CASE("Template fit: a home another mod rebuilt is not enabled on its own", "[compat][fit]")
{
	const auto t = FitTemplate(9);  // 10 references with the master

	SECTION("all there")
	{
		const auto fit = JudgeTemplateFit(t, Probe({}));
		CHECK(fit.fits);
		CHECK(fit.checked == 10);
		CHECK(fit.present == 10);
	}
	SECTION("exactly the threshold: 6 of 10 there")
	{
		std::map<FormRef, RefPresence> states;
		for (std::uint32_t i = 0; i < 4; ++i) {
			states[Sk(0x200 + i)] = i % 2 ? RefPresence::Missing : RefPresence::Disabled;
		}
		const auto fit = JudgeTemplateFit(t, Probe(states));
		CHECK(fit.fits);
		CHECK(fit.disabled == 2);
		CHECK(fit.missing == 2);
	}
	SECTION("under the threshold: 5 of 10 there")
	{
		std::map<FormRef, RefPresence> states;
		for (std::uint32_t i = 0; i < 5; ++i) {
			states[Sk(0x200 + i)] = RefPresence::Disabled;
		}
		const auto fit = JudgeTemplateFit(t, Probe(states));
		CHECK_FALSE(fit.fits);
		CHECK(fit.present == 5);
		CHECK(fit.disabled == 5);
	}
	SECTION("not built yet counts as there (Hearthfire)")
	{
		const auto fit = JudgeTemplateFit(t, Probe({ { Sk(0x100), RefPresence::Present } }, RefPresence::NotBuilt));
		CHECK(fit.fits);
		CHECK(fit.notBuilt == 9);
	}
	SECTION("unknown references are not counted")
	{
		std::map<FormRef, RefPresence> states;
		for (std::uint32_t i = 0; i < 8; ++i) {
			states[Sk(0x200 + i)] = RefPresence::Unknown;
		}
		states[Sk(0x208)] = RefPresence::Missing;
		const auto fit = JudgeTemplateFit(t, Probe(states));
		CHECK(fit.unknown == 8);
		CHECK(fit.checked == 2);  // the master there, one missing: 50%
		CHECK_FALSE(fit.fits);
	}
	SECTION("nothing to count fits")
	{
		const auto fit = JudgeTemplateFit(t, Probe({}, RefPresence::Unknown));
		CHECK(fit.checked == 0);
		CHECK(fit.fits);
		CHECK(JudgeTemplateFit(Layout{}, Probe({})).fits);
	}
}
