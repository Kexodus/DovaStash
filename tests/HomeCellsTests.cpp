// Multi-cell homes (DESIGN §6.7): the optional "cells" list, membership and cross-cell reach.

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>

#include "TestHelpers.h"

using namespace dova::core;
using namespace dova::test;

#ifndef DOVA_DIST_DIR
#	error "DOVA_DIST_DIR must be defined by tests/CMakeLists.txt"
#endif

namespace
{
	FormRef Hf(std::uint32_t a_id) { return FormRef{ "HearthFires.esm", a_id }; }

	// Lakeview Manor: main hall, cellar, and the exterior cell with the forge (ids from esm_dump.py).
	const FormRef kMain = Hf(0x2FEC);
	const FormRef kCellar = Hf(0x4849);
	const FormRef kGrounds = Sk(0x9BF7);

	constexpr std::string_view kMultiCell = R"({
		"$schema": "dovastash-layout-1",
		"name": "Lakeview Manor",
		"author": "Test",
		"version": "1.0.0",
		"requires": ["Skyrim.esm", "HearthFires.esm"],
		"cell": "HearthFires.esm|0x00002FEC",
		"cells": [
			"HearthFires.esm|0x00004849",
			{"cell": "Skyrim.esm|0x00009BF7", "label": "Grounds", "note": "forge, smelter and tanning rack", "future": 1}
		],
		"master": "HearthFires.esm|0x0000309E",
		"containers": [
			{"ref": "HearthFires.esm|0x0000BE85", "label": "Cellar chest", "categories": ["weapons.*"]}
		]
	})";

	Layout ParseOk(std::string_view a_text, Diagnostics* a_out = nullptr)
	{
		Diagnostics diags;
		auto        layout = ParseLayoutText(a_text, "test.json", diags);
		INFO((diags.Empty() ? std::string() : diags.Items().front().ToString()));
		REQUIRE(layout);
		REQUIRE_FALSE(diags.HasErrors());
		if (a_out) {
			*a_out = diags;
		}
		return *layout;
	}

	bool Mentions(const Diagnostics& a_diags, std::string_view a_text)
	{
		return std::ranges::any_of(a_diags.Items(), [&](const Diagnostic& d) { return d.message.find(a_text) != std::string::npos; });
	}
}

TEST_CASE("cells: strings and objects parse; unknown keys are kept", "[cells]")
{
	Diagnostics diags;
	const auto  layout = ParseOk(kMultiCell, &diags);
	REQUIRE(layout.cells.size() == 2);
	CHECK(layout.cells[0].cell == kCellar);
	CHECK(layout.cells[0].label.empty());
	CHECK(layout.cells[1].cell == kGrounds);
	CHECK(layout.cells[1].label == "Grounds");
	CHECK(layout.cells[1].note == "forge, smelter and tanning rack");
	CHECK(layout.cells[1].extra.contains("future"));
	CHECK(Mentions(diags, "cells[1].future: unknown field"));
	CHECK_FALSE(layout.extra.contains("cells"));
	CHECK(HomeCellRefs(*layout.cell, layout.cells) == std::vector<FormRef>{ kMain, kCellar, kGrounds });
}

TEST_CASE("cells: a mistyped field gets a near-miss hint", "[cells]")
{
	Diagnostics diags;
	const auto  layout = ParseOk(R"({
		"$schema": "dovastash-layout-1",
		"cell": "HearthFires.esm|0x00002FEC",
		"cells": [ {"cell": "Skyrim.esm|0x00009BF7", "lable": "Grounds"} ]
	})",
		 &diags);
	REQUIRE(layout.cells.size() == 1);
	CHECK(layout.cells[0].extra.contains("lable"));
	CHECK(Mentions(diags, "cells[0].lable: unknown field (preserved); did you mean \"label\"?"));
}

TEST_CASE("cells: written after cell, plain entries as strings, round-trip stable", "[cells]")
{
	const auto layout = ParseOk(kMultiCell);
	const auto text = SerializeLayout(layout);
	const auto cellPos = text.find("\"cell\":");
	const auto cellsPos = text.find("\"cells\":");
	const auto masterPos = text.find("\"master\":");
	REQUIRE(cellPos != std::string::npos);
	REQUIRE(cellsPos != std::string::npos);
	CHECK(cellPos < cellsPos);
	CHECK(cellsPos < masterPos);
	CHECK(text.find("\"HearthFires.esm|0x00004849\"") != std::string::npos);
	CHECK(text.find("\"label\": \"Grounds\"") != std::string::npos);
	CHECK(text.find("\"future\": 1") != std::string::npos);

	const auto again = ParseOk(text);
	CHECK(again.cells == layout.cells);
	CHECK(SerializeLayout(again) == text);
}

TEST_CASE("cells: a layout without cells writes no cells key", "[cells]")
{
	Layout layout;
	layout.name = "Breezehome";
	layout.cell = Sk(0x165A8);
	layout.master = Sk(0xF3922);
	const auto text = SerializeLayout(layout);
	CHECK(text.find("cells") == std::string::npos);
	const auto again = ParseOk(text);
	CHECK(again.cells.empty());
	CHECK(again.unparsedCells.empty());
}

TEST_CASE("cells: malformed entries are reported and kept verbatim", "[cells]")
{
	Diagnostics diags;
	const auto  layout = ParseLayoutText(R"({
		"$schema": "dovastash-layout-1", "name": "x", "cell": "Skyrim.esm|0x000165A8",
		"cells": ["Skyrim.esm|0x00009BF7", "not a ref", 42, {"label": "no cell"}, {"cell": "Skyrim.esm|0x00009BD6", "label": 7}],
		"containers": []
	})",
		"bad.json", diags);
	REQUIRE(layout);
	CHECK(diags.HasErrors());
	REQUIRE(layout->cells.size() == 2);
	CHECK(layout->cells[0].cell == Sk(0x9BF7));
	CHECK(layout->cells[1].cell == Sk(0x9BD6));
	CHECK(layout->cells[1].label.empty());
	CHECK(layout->cells[1].extra.contains("label"));  // wrong type: kept verbatim
	REQUIRE(layout->unparsedCells.size() == 3);

	const auto text = SerializeLayout(*layout);
	CHECK(text.find("not a ref") != std::string::npos);
	CHECK(text.find("no cell") != std::string::npos);
	Diagnostics again;
	const auto  reparsed = ParseLayoutText(text, "bad.json", again);
	REQUIRE(reparsed);
	CHECK(SerializeLayout(*reparsed) == text);
}

TEST_CASE("cells: a value of the wrong type is kept as it is; a single entry is accepted", "[cells]")
{
	Diagnostics diags;
	const auto  wrong = ParseLayoutText(R"({"$schema": "dovastash-layout-1", "name": "x", "cell": "Skyrim.esm|0x000165A8", "cells": 5, "containers": []})",
		"w.json", diags);
	REQUIRE(wrong);
	CHECK(diags.HasErrors());
	CHECK(wrong->cells.empty());
	REQUIRE(wrong->extra.contains("cells"));
	CHECK(SerializeLayout(*wrong).find("\"cells\": 5") != std::string::npos);

	const auto single = ParseOk(R"({"$schema": "dovastash-layout-1", "name": "x", "cell": "Skyrim.esm|0x000165A8", "cells": "Skyrim.esm|0x00009BF7", "containers": []})");
	REQUIRE(single.cells.size() == 1);
	CHECK(single.cells[0].cell == Sk(0x9BF7));
}

TEST_CASE("cells: duplicates and the main cell are warned about", "[cells]")
{
	Diagnostics diags;
	const auto  layout = ParseLayoutText(R"({
		"$schema": "dovastash-layout-1", "name": "x", "cell": "Skyrim.esm|0x000165A8",
		"cells": ["Skyrim.esm|0x000165A8", "Skyrim.esm|0x00009BF7", "skyrim.esm|0x9bf7"],
		"containers": []
	})",
		"dup.json", diags);
	REQUIRE(layout);
	CHECK_FALSE(diags.HasErrors());
	CHECK(Mentions(diags, "is the home's main cell"));
	CHECK(Mentions(diags, "duplicate cell"));
	CHECK(HomeCellRefs(*layout->cell, layout->cells) == std::vector<FormRef>{ Sk(0x165A8), Sk(0x9BF7) });
}

TEST_CASE("cells: templates reference their plugins; applying merges the player's cells", "[cells][templates]")
{
	auto tpl = ParseOk(kMultiCell);
	tpl.requiredPlugins = { "HearthFires.esm" };  // Skyrim.esm (the grounds) missing
	Diagnostics lint;
	ValidateTemplate(tpl, "t.json", lint);
	CHECK(Mentions(lint, "cells references Skyrim.esm"));

	const auto plugins = ReferencedPlugins(tpl);
	CHECK(std::ranges::find(plugins, std::string("Skyrim.esm")) != plugins.end());

	// Applying to a new home copies the cells.
	const TemplateCandidate candidate{ "Templates/Test/Lakeview.json", tpl };
	const auto              fresh = ApplyTemplate(candidate);
	CHECK(fresh.cells == tpl.cells);

	// Over a player layout: the template's cells, then the ones the player added; labels fill blanks.
	Layout player;
	player.cell = kMain;
	player.cells = { HomeCell{ kCellar, "My cellar" }, HomeCell{ Sk(0x9BD6), "Garden" } };
	player.unparsedCells = nlohmann::json::array({ "broken entry" });
	const auto merged = ApplyTemplate(candidate, &player);
	REQUIRE(merged.cells.size() == 3);
	CHECK(merged.cells[0].cell == kCellar);
	CHECK(merged.cells[0].label == "My cellar");
	CHECK(merged.cells[1].cell == kGrounds);
	CHECK(merged.cells[1].label == "Grounds");
	CHECK(merged.cells[2].cell == Sk(0x9BD6));
	CHECK(merged.unparsedCells.size() == 1);

	// Exporting keeps them and requires their plugins.
	const auto exported = ExportAsTemplate(merged);
	CHECK(exported.cells == merged.cells);
	CHECK(std::ranges::find(exported.requiredPlugins, std::string("Skyrim.esm")) != exported.requiredPlugins.end());
}

TEST_CASE("cells: naming an area sets its label; an unlisted cell changes nothing", "[cells]")
{
	auto layout = ParseOk(kMultiCell);
	REQUIRE(SetHomeCellLabel(layout.cells, kCellar, "  Wine cellar \t"));
	CHECK(layout.cells[0].label == "Wine cellar");
	CHECK(layout.cells[1].label == "Grounds");  // the others keep theirs
	CHECK(layout.cells[1].extra.contains("future"));

	// Clearing the label: the cell's own name shows again, and the entry is written as a plain string.
	REQUIRE(SetHomeCellLabel(layout.cells, kGrounds, "   "));
	CHECK(layout.cells[1].label.empty());
	CHECK_FALSE(SetHomeCellLabel(layout.cells, Sk(0x9BD6), "Garden"));
	CHECK(layout.cells.size() == 2);

	const auto text = SerializeLayout(layout);
	CHECK(text.find("\"label\": \"Wine cellar\"") != std::string::npos);
	CHECK(text.find("Grounds") == std::string::npos);
}

TEST_CASE("cells: a cell with its own layout stays its own home", "[cells]")
{
	const std::vector<HomeCell> cells{ HomeCell{ kCellar }, HomeCell{ kGrounds } };
	std::vector<FormRef>        excluded;
	const auto                  effective = EffectiveHomeCells(kMain, cells, [](const FormRef& a_cell) { return a_cell == kCellar; }, &excluded);
	CHECK(effective == std::vector<FormRef>{ kMain, kGrounds });
	CHECK(excluded == std::vector<FormRef>{ kCellar });
	CHECK(EffectiveHomeCells(kMain, cells, {}) == std::vector<FormRef>{ kMain, kCellar, kGrounds });
}

TEST_CASE("cells: which home a cell belongs to", "[cells]")
{
	const std::vector<HomeCellsEntry> index{
		{ kMain, { kCellar, kGrounds } },
		{ Sk(0x165A8), { kGrounds } },  // a second home that also lists the grounds (later in FormRef order)
	};

	SECTION("a cell no layout lists is its own home (as before)")
	{
		const auto m = ResolveCellMembership(Sk(0x16BDD), false, index);
		CHECK(m.kind == CellMembership::Kind::Own);
		CHECK(m.home == Sk(0x16BDD));
		CHECK(ResolveCellMembership(Sk(0x16BDD), false, {}).kind == CellMembership::Kind::Own);
	}
	SECTION("a listed cell is part of that home")
	{
		const auto m = ResolveCellMembership(kCellar, false, index);
		CHECK(m.kind == CellMembership::Kind::Extra);
		CHECK(m.home == kMain);
		CHECK(m.otherClaims.empty());
	}
	SECTION("its own layout wins")
	{
		const auto m = ResolveCellMembership(kCellar, true, index);
		CHECK(m.kind == CellMembership::Kind::Own);
		CHECK(m.home == kCellar);
	}
	SECTION("several claims: the first main cell in FormRef order, the others reported")
	{
		const auto m = ResolveCellMembership(kGrounds, false, index);
		CHECK(m.kind == CellMembership::Kind::Extra);
		CHECK(m.home == kMain);  // "HearthFires.esm" sorts before "Skyrim.esm"
		CHECK(m.otherClaims == std::vector<FormRef>{ Sk(0x165A8) });
	}
	SECTION("a home's main cell is never an extra cell of itself")
	{
		const std::vector<HomeCellsEntry> self{ { kMain, { kMain, kCellar } } };
		CHECK(ResolveCellMembership(kMain, false, self).kind == CellMembership::Kind::Own);
	}
}

TEST_CASE("cells: what an operation can reach in the home's other cells", "[cells]")
{
	using enum Reach;
	CHECK(JudgeReach({ .inHome = false, .inPlayerCell = false, .cellAttached = true, .persistent = true }) == Outside);
	CHECK(JudgeReach({ .inHome = true, .inPlayerCell = true, .cellAttached = true, .persistent = false }) == Here);
	CHECK(JudgeReach({ .inHome = true, .inPlayerCell = false, .cellAttached = true, .persistent = false }) == Loaded);
	CHECK(JudgeReach({ .inHome = true, .inPlayerCell = false, .cellAttached = false, .persistent = true }) == Remote);
	CHECK(JudgeReach({ .inHome = true, .inPlayerCell = false, .cellAttached = false, .persistent = false }) == Unreachable);
	CHECK(IsReachable(Here));
	CHECK(IsReachable(Loaded));
	CHECK(IsReachable(Remote));
	CHECK_FALSE(IsReachable(Unreachable));
	CHECK_FALSE(IsReachable(Outside));
}

TEST_CASE("cells: exterior grid and layout file names", "[cells]")
{
	CHECK(GridOf(0.0f, 0.0f) == GridCell{ 0, 0 });
	CHECK(GridOf(4095.9f, 4096.0f) == GridCell{ 0, 1 });
	CHECK(GridOf(-1.0f, -4096.0f) == GridCell{ -1, -1 });
	CHECK(GridOf(-4097.0f, 20480.5f) == GridCell{ -2, 5 });

	for (const auto& cell : { Sk(0x165A7), Hf(0x4849), FormRef{ "ccEEJSSE001-Hstead.esm", 0xB06 }, FormRef{ "My_Home_Mod.esp", 0xABCDEF } }) {
		INFO(cell.ToString());
		const auto parsed = CellFromLayoutFileName(LayoutFileName(cell));
		REQUIRE(parsed);
		CHECK(*parsed == cell);
	}
	CHECK_FALSE(CellFromLayoutFileName("Skyrim.esm_000165A7.json.20261012-140312.bak"));
	CHECK_FALSE(CellFromLayoutFileName("Skyrim.esm_165A7.json"));
	CHECK_FALSE(CellFromLayoutFileName("notes.json"));
	CHECK_FALSE(CellFromLayoutFileName("_000165A7.json"));
}

// The hard rule for V2: every template shipped before multi-cell support loads and resolves exactly as it did.
// This list is the state of dist/ when "cells" was added (source, main cell, master), without the homes that now ship as
// one template over several cells (the next test).
TEST_CASE("cells: every shipped single-cell template resolves exactly as before", "[cells][dist]")
{
	struct Golden
	{
		const char* source;
		const char* cell;
		const char* master;
	};
	static constexpr Golden kShipped[]{
		{ "Templates/CreationClub/BloodchillManor.json", "Update.esm|0x00002FD1", "ccEEJSSE005-Cave.esm|0x00038832" },
		{ "Templates/CreationClub/GallowsHall.json", "Update.esm|0x00002FD0", "ccRMSSSE001-NecroHouse.esl|0x00000968" },
		{ "Templates/CreationClub/GoldenhillsPlantation.json", "Update.esm|0x000030EC", "ccVSVSSE004-BeAFarmer.esl|0x000009F8" },
		{ "Templates/CreationClub/Hendraheim.json", "Update.esm|0x00002FD9", "ccEEJSSE004-Hall.esl|0x00000A81" },
		{ "Templates/CreationClub/Nchuanthumz.json", "ccAFDSSE001-DweSanctuary.esm|0x00000A5E", "ccAFDSSE001-DweSanctuary.esm|0x00000DB9" },
		{ "Templates/CreationClub/ShadowfootSanctum.json", "Update.esm|0x00002F98", "ccEEJSSE003-Hollow.esl|0x00000EB7" },
		{ "Templates/Vanilla/Breezehome.json", "Skyrim.esm|0x000165A8", "Skyrim.esm|0x000F3922" },
		{ "Templates/Vanilla/Hjerim.json", "Skyrim.esm|0x00016778", "Skyrim.esm|0x000DF5AC" },
		{ "Templates/Vanilla/Honeyside.json", "Skyrim.esm|0x00016BDD", "Skyrim.esm|0x000D721E" },
		{ "Templates/Vanilla/ProudspireManor.json", "Skyrim.esm|0x00016A06", "Skyrim.esm|0x0009DA07" },
		{ "Templates/Vanilla/SeverinManor.json", "Dragonborn.esm|0x00017EBE", "Dragonborn.esm|0x00036EF2" },
		{ "Templates/Vanilla/VlindrelHall.json", "Skyrim.esm|0x00016DFA", "Skyrim.esm|0x00077CAB" },
	};

	const std::filesystem::path dir = std::filesystem::path{ DOVA_DIST_DIR } / "Templates";
	Diagnostics                 diags;
	const auto                  templates = LoadTemplateDirectory(dir, diags);
	CHECK_FALSE(diags.HasErrors());
	CHECK_FALSE(Mentions(diags, "cells"));

	// Everything the shipped templates need is loaded (as in a game with the CC homes).
	std::vector<std::string> loaded;
	for (const auto& t : templates) {
		for (const auto& p : t.layout.requiredPlugins) {
			if (std::ranges::none_of(loaded, [&](const std::string& l) { return IEquals(l, p); })) {
				loaded.push_back(p);
			}
		}
	}

	for (const auto& g : kShipped) {
		INFO(g.source);
		const auto it = std::ranges::find_if(templates, [&](const TemplateCandidate& t) { return t.source == g.source; });
		REQUIRE(it != templates.end());
		const auto& layout = it->layout;
		REQUIRE(layout.cell);
		CHECK(*layout.cell == F(g.cell));
		REQUIRE(layout.master);
		CHECK(*layout.master == F(g.master));

		// Single-cell: no cells, nothing kept for them, the home is its own cell.
		CHECK(layout.cells.empty());
		CHECK(layout.unparsedCells.empty());
		CHECK_FALSE(layout.extra.contains("cells"));
		CHECK(HomeCellRefs(*layout.cell, layout.cells) == std::vector<FormRef>{ *layout.cell });

		// It is still the template the home gets, and its cell still belongs to no other home.
		const auto resolution = ResolveLayout(nullptr, templates, *layout.cell, loaded);
		CHECK(resolution.kind == LayoutResolution::Kind::Template);
		REQUIRE(resolution.chosen);
		CHECK(templates[*resolution.chosen].source == g.source);
		CHECK_FALSE(resolution.partOf);

		// The index the plugin keeps comes from player layouts: a template's `cells` count once it is applied.
		std::vector<HomeCellsEntry> index;
		for (const auto& t : templates) {
			if (t.layout.cell && t.layout.cells.empty()) {
				index.push_back({ *t.layout.cell, HomeCellRefs(*t.layout.cell, t.layout.cells) });
			}
		}
		const auto membership = ResolveCellMembership(*layout.cell, false, index);
		CHECK(membership.kind == CellMembership::Kind::Own);
		CHECK(membership.home == *layout.cell);

		// Applied, it is a single-cell player layout that writes no "cells".
		const auto applied = ApplyTemplate(*it);
		CHECK(applied.cells.empty());
		CHECK(SerializeLayout(applied).find("\"cells\"") == std::string::npos);
	}
}

// DESIGN §6.7: a home over several cells ships as one template. Each of the five has exactly one, under the home's own
// name; its `cells` lists the cellar, grounds or gallery (with the retired template's master as that area's master); no
// shipped template's main cell is another's area; an area entered first offers nothing of its own (partOf: its home).
TEST_CASE("cells: a multi-cell home ships as one template; applied, its areas join the home", "[cells][dist]")
{
	struct Home
	{
		const char*                 source;
		const char*                 main;
		std::vector<const char*>    cells;
		const char*                 areaMaster;  // of cells[0], the area the retired template was for
		const char*                 replaces;
	};
	const std::vector<Home> kHomes{
		{ "Templates/Vanilla/LakeviewManor.json", "HearthFires.esm|0x00002FEC", { "HearthFires.esm|0x00004849", "Skyrim.esm|0x00009BF7" },
			"HearthFires.esm|0x0000BE8F", "Templates/Vanilla/LakeviewManorCellar.json" },
		{ "Templates/Vanilla/WindstadManor.json", "HearthFires.esm|0x00011CB3",
			{ "HearthFires.esm|0x00015154", "Skyrim.esm|0x000092B2", "Skyrim.esm|0x00009291" }, "HearthFires.esm|0x00015189",
			"Templates/Vanilla/WindstadManorCellar.json" },
		{ "Templates/Vanilla/HeljarchenHall.json", "HearthFires.esm|0x00016A56",
			{ "HearthFires.esm|0x00016A55", "Skyrim.esm|0x000090C4", "Skyrim.esm|0x000090A4" }, "HearthFires.esm|0x00017489",
			"Templates/Vanilla/HeljarchenHallCellar.json" },
		{ "Templates/CreationClub/TundraHomestead.json", "ccEEJSSE001-Hstead.esm|0x0000087F", { "ccEEJSSE001-Hstead.esm|0x00000B06" },
			"ccEEJSSE001-Hstead.esm|0x00000F85", "Templates/CreationClub/TundraHomesteadCellar.json" },
		{ "Templates/CreationClub/Myrwatch.json", "ccEEJSSE002-Tower.esl|0x0000083B", { "ccEEJSSE002-Tower.esl|0x0000092C" },
			"ccEEJSSE002-Tower.esl|0x00000E5D", "Templates/CreationClub/MyrwatchGallery.json" },
	};

	const std::filesystem::path dir = std::filesystem::path{ DOVA_DIST_DIR } / "Templates";
	Diagnostics                 diags;
	const auto                  templates = LoadTemplateDirectory(dir, diags);
	REQUIRE_FALSE(diags.HasErrors());
	std::vector<std::string> loaded;
	for (const auto& t : templates) {
		for (const auto& p : t.layout.requiredPlugins) {
			loaded.push_back(p);
		}
	}

	// No cell is in two shipped templates, as main cell or area: one template per home.
	std::vector<std::pair<FormRef, std::string>> owner;
	for (const auto& t : templates) {
		REQUIRE(t.layout.cell);
		for (const auto& c : HomeCellRefs(*t.layout.cell, t.layout.cells)) {
			INFO(t.source << " " << c.ToString());
			const auto it = std::ranges::find(owner, c, &std::pair<FormRef, std::string>::first);
			CHECK(it == owner.end());
			owner.emplace_back(c, t.source);
		}
		for (const auto& r : t.layout.replaces) {
			INFO(t.source << " replaces " << r);
			CHECK(std::ranges::none_of(templates, [&](const TemplateCandidate& o) { return IEquals(o.source, r); }));
		}
	}

	for (const auto& h : kHomes) {
		INFO(h.source);
		const auto main = F(h.main);
		const auto mine = std::ranges::count_if(templates, [&](const TemplateCandidate& t) { return t.layout.cell == main; });
		CHECK(mine == 1);
		const auto it = std::ranges::find_if(templates, [&](const TemplateCandidate& t) { return t.source == h.source; });
		REQUIRE(it != templates.end());
		const auto& t = *it;
		CHECK(t.layout.cell == main);
		CHECK(t.layout.priority == 0);
		REQUIRE(t.layout.cells.size() == h.cells.size());
		for (std::size_t i = 0; i < h.cells.size(); ++i) {
			CHECK(t.layout.cells[i].cell == F(h.cells[i]));
			CHECK_FALSE(t.layout.cells[i].label.empty());
		}
		CHECK(t.layout.cells[0].master == F(h.areaMaster));
		CHECK(t.layout.replaces == std::vector<std::string>{ h.replaces });

		// Shipped, it is the home's template; an area entered first has none of its own and names its home.
		const auto resolution = ResolveLayout(nullptr, templates, main, loaded);
		REQUIRE(resolution.chosen);
		CHECK(templates[*resolution.chosen].source == h.source);
		for (const auto& c : t.layout.cells) {
			const auto area = ResolveLayout(nullptr, templates, c.cell, loaded);
			CHECK(area.kind == LayoutResolution::Kind::None);
			REQUIRE(area.partOf);
			CHECK(templates[*area.partOf].source == h.source);
			CHECK(ResolveCellMembership(c.cell, false, {}).kind == CellMembership::Kind::Own);
		}

		// Applied, its areas belong to the home; `replaces` stays the template's.
		const auto applied = ApplyTemplate(t);
		CHECK(applied.replaces.empty());
		REQUIRE(applied.cells.size() == t.layout.cells.size());
		CHECK(applied.cells[0].master == F(h.areaMaster));
		const std::vector<HomeCellsEntry> index{ { *applied.cell, HomeCellRefs(*applied.cell, applied.cells) } };
		for (const auto& c : applied.cells) {
			const auto membership = ResolveCellMembership(c.cell, false, index);
			CHECK(membership.kind == CellMembership::Kind::Extra);
			CHECK(membership.home == *applied.cell);
		}
	}
}

TEST_CASE("cells: an area master round-trips, is checked, and counts as a reference", "[cells]")
{
	constexpr std::string_view kText = R"({
		"$schema": "dovastash-layout-1",
		"name": "Tundra Homestead",
		"author": "Test",
		"version": "1.0.0",
		"requires": ["Skyrim.esm", "HearthFires.esm"],
		"replaces": ["Templates/CreationClub/TundraHomesteadCellar.json"],
		"cell": "HearthFires.esm|0x00002FEC",
		"cells": [
			{"cell": "HearthFires.esm|0x00004849", "label": "Cellar", "master": "HearthFires.esm|0x0000BE8F"},
			{"cell": "Skyrim.esm|0x00009BF7", "master": "HearthFires.esm|0x0000309E"},
			{"cell": "Skyrim.esm|0x00009BD6", "master": "nope"}
		],
		"master": "HearthFires.esm|0x0000309E",
		"containers": []
	})";
	Diagnostics diags;
	const auto  layout = ParseLayoutText(kText, "t.json", diags);
	REQUIRE(layout);
	REQUIRE(layout->cells.size() == 3);
	CHECK(layout->cells[0].master == Hf(0xBE8F));
	CHECK(layout->cells[1].master == Hf(0x309E));
	CHECK_FALSE(layout->cells[2].master);
	CHECK(layout->replaces == std::vector<std::string>{ "Templates/CreationClub/TundraHomesteadCellar.json" });
	CHECK(Mentions(diags, "cells[1].master: HearthFires.esm|0x0000309E is the home's master (ignored)"));
	CHECK(Mentions(diags, "cells[2].master: invalid form reference \"nope\" (kept verbatim, ignored)"));

	// Written back as read: the bad value stays verbatim, the home's master is never an area master.
	const auto written = nlohmann::json::parse(LayoutToJson(*layout).dump());
	CHECK(written["cells"][0]["master"] == "HearthFires.esm|0x0000BE8F");
	CHECK(written["cells"][2]["master"] == "nope");
	CHECK(written["replaces"][0] == "Templates/CreationClub/TundraHomesteadCellar.json");
	CHECK(AreaMasters(layout->cells, layout->master) == std::vector<FormRef>{ Hf(0xBE8F) });

	// A template's area masters need their plugins in requires, as any reference.
	Layout tpl = *layout;
	tpl.cells[0].master = FormRef{ "Other.esp", 0x801 };
	Diagnostics lint;
	ValidateTemplate(tpl, "t.json", lint);
	CHECK(Mentions(lint, "cells master references Other.esp"));
	const auto plugins = ReferencedPlugins(tpl);
	CHECK(std::ranges::find(plugins, std::string("Other.esp")) != plugins.end());

	// Applying over a player layout: the template's area master wins, the player's fills a blank.
	std::vector<HomeCell> player{ HomeCell{ .cell = kCellar, .master = Hf(0x1) }, HomeCell{ .cell = kGrounds, .master = Hf(0x2) } };
	std::vector<HomeCell> fromTemplate{ HomeCell{ .cell = kCellar, .master = Hf(0xBE8F) }, HomeCell{ .cell = kGrounds } };
	const auto            merged = MergeHomeCells(fromTemplate, player);
	CHECK(merged[0].master == Hf(0xBE8F));
	CHECK(merged[1].master == Hf(0x2));
}
