// The auto-draft mark of "Draft all homes" (DESIGN §6.8): parsed and written back, dropped where a person takes
// over, ranked below templates someone made, and the guard that never lets a bulk draft replace one.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "TestHelpers.h"

using namespace dova::core;
using namespace dova::test;

namespace
{
	const FormRef kCell = Sk(0x165A7);
	const FormRef kCellar = Sk(0x165A8);

	std::string TemplateJson(std::string_view a_cell, bool a_autoDraft, std::string_view a_extra = {})
	{
		return std::string(R"({ "$schema": "dovastash-layout-1", )") + (a_autoDraft ? R"("autoDraft": true, )" : "") + R"("name": "Home", "author": "A", "version": "1.0.0", "cell": ")" +
		       std::string(a_cell) + "\"" + std::string(a_extra) + R"(, "containers": [] })";
	}
}

TEST_CASE("autoDraft parses, round-trips and is written first", "[templates][autodraft]")
{
	Diagnostics diags;
	auto        layout = ParseLayoutText(TemplateJson("Skyrim.esm|0x165A7", true), "t.json", diags);
	REQUIRE(layout);
	CHECK(layout->autoDraft);
	CHECK(diags.Empty());  // a known field: no "unknown field" warning
	CHECK(layout->extra.empty());

	const auto text = SerializeLayout(*layout);
	const auto mark = text.find("\"autoDraft\": true");
	REQUIRE(mark != std::string::npos);
	CHECK(mark < text.find("\"name\""));
	CHECK(text.find("\"$schema\"") < mark);

	Diagnostics again;
	const auto  reparsed = ParseLayoutText(text, "t.json", again);
	REQUIRE(reparsed);
	CHECK(reparsed->autoDraft);
	CHECK(SerializeLayout(*reparsed) == text);

	// Off: not written at all, and an explicit false reads as off.
	layout->autoDraft = false;
	CHECK(SerializeLayout(*layout).find("autoDraft") == std::string::npos);
	Diagnostics off;
	const auto  explicitFalse = ParseLayoutText(R"({ "$schema": "dovastash-layout-1", "autoDraft": false, "cell": "Skyrim.esm|0x165A7", "containers": [] })", "f.json", off);
	REQUIRE(explicitFalse);
	CHECK_FALSE(explicitFalse->autoDraft);
	CHECK(off.Empty());
}

TEST_CASE("autoDraft of the wrong type is an error kept verbatim", "[templates][autodraft]")
{
	Diagnostics diags;
	const auto  layout = ParseLayoutText(R"({ "$schema": "dovastash-layout-1", "autoDraft": "yes", "cell": "Skyrim.esm|0x165A7", "containers": [] })", "t.json", diags);
	REQUIRE(layout);
	CHECK_FALSE(layout->autoDraft);
	CHECK(diags.ErrorCount() == 1);
	CHECK(SerializeLayout(*layout).find("\"autoDraft\": \"yes\"") != std::string::npos);
}

TEST_CASE("Applying or exporting a template drops the auto-draft mark", "[templates][autodraft]")
{
	TemplateCandidate t;
	t.source = "Templates/Home/Unknown.json";
	t.layout.autoDraft = true;
	t.layout.cell = kCell;
	t.layout.master = Sk(0xAAAAA);
	t.layout.version = "1.0.0";
	const auto applied = ApplyTemplate(t);
	CHECK_FALSE(applied.autoDraft);
	const auto reapplied = ApplyTemplate(t, &applied);
	CHECK_FALSE(reapplied.autoDraft);
	CHECK_FALSE(ExportAsTemplate(t.layout).autoDraft);
}

TEST_CASE("An auto-draft ranks below a template someone made", "[templates][autodraft]")
{
	auto make = [](std::string a_source, std::string a_version, bool a_auto, std::int32_t a_priority = 0) {
		TemplateCandidate t;
		t.source = std::move(a_source);
		t.layout.version = std::move(a_version);
		t.layout.autoDraft = a_auto;
		t.layout.priority = a_priority;
		t.layout.cell = kCell;
		t.layout.requiredPlugins = { "Skyrim.esm" };
		return t;
	};
	const std::vector<std::string> loaded{ "Skyrim.esm" };
	// The auto-draft's version is higher (refreshed several times) and its path sorts first: the person's still wins.
	std::vector<TemplateCandidate> list{ make("Templates/Home/Auto.json", "1.0.5", true), make("Templates/Home/Zed.json", "1.0.0", false) };
	auto res = ResolveLayout(nullptr, list, kCell, loaded);
	CHECK(res.chosen == 1u);
	// Over the player's own exports too.
	list.push_back(make("Templates/User/Home.json", "1.0.0", true));
	res = ResolveLayout(nullptr, list, kCell, loaded);
	CHECK(res.chosen == 1u);
	// Nor does its priority: an auto-draft is offered only when no template someone made fits (TemplateSwitchTests).
	list.push_back(make("Templates/Home/High.json", "1.0.0", true, 5));
	res = ResolveLayout(nullptr, list, kCell, loaded);
	CHECK(res.chosen == 1u);
	REQUIRE(res.alternatives.size() == 3);
	CHECK(res.alternatives[0] == 3u);  // the auto-drafts follow, in their own priority order
}

TEST_CASE("The bulk draft never replaces a template someone made", "[templates][autodraft][files]")
{
	TempDir dir;
	const auto base = dir.Path();
	dir.Write("Breezehome/Unknown.json", TemplateJson("Skyrim.esm|0x165A7", true));
	dir.Write("Other/Unknown.json", TemplateJson("Skyrim.esm|0x99999", true));
	dir.Write("Hjerim/Me.json", TemplateJson("Skyrim.esm|0x16781", false));
	dir.Write("Broken/Unknown.json", "{");
	dir.Write("Breezehome/Unknown.json.20261014-120000.bak", TemplateJson("Skyrim.esm|0x165A7", false));  // not a .json: never read

	const std::vector<FormRef> home{ kCell, kCellar };
	auto files = ScanTemplateFiles(base);
	REQUIRE(files.size() == 4);
	CHECK(std::ranges::count_if(files, [](const TemplateFileInfo& f) { return !f.parsed; }) == 1);

	// Its own earlier auto-draft: refreshed.
	CHECK(CheckAutoDraftTarget(files, base / "Breezehome" / "Unknown.json", home).verdict == AutoDraftVerdict::Refresh);
	// Nothing there: new.
	CHECK(CheckAutoDraftTarget(files, base / "Breezehome" / "Someone.json", home).verdict == AutoDraftVerdict::New);
	// A person's file at the target, or one the bulk draft cannot read: left alone.
	auto check = CheckAutoDraftTarget(files, base / "Hjerim" / "Me.json", std::vector<FormRef>{ Sk(0x16781) });
	CHECK(check.verdict == AutoDraftVerdict::Person);
	CHECK(check.blocker == base / "Hjerim" / "Me.json");
	CHECK(CheckAutoDraftTarget(files, base / "Broken" / "Unknown.json", home).verdict == AutoDraftVerdict::Person);
	// Another home's auto-draft at the target (same home name): left alone.
	check = CheckAutoDraftTarget(files, base / "Other" / "Unknown.json", home);
	CHECK(check.verdict == AutoDraftVerdict::OtherHome);
	CHECK(check.blocker == base / "Other" / "Unknown.json");
	// A person's template of the home anywhere in the destination (another author's file, the old Author/Name
	// layout, or one that lists the cellar in "cells"): the home is left alone, nothing written next to it.
	dir.Write("Me/Breezehome.json", TemplateJson("Skyrim.esm|0x12345", false, R"(, "cells": ["Skyrim.esm|0x165A8"])"));
	files = ScanTemplateFiles(base);
	check = CheckAutoDraftTarget(files, base / "Breezehome" / "Unknown.json", home);
	CHECK(check.verdict == AutoDraftVerdict::Person);
	CHECK(check.blocker == base / "Me" / "Breezehome.json");
	// A person's template of an unrelated home does not matter.
	CHECK(CheckAutoDraftTarget(files, base / "Riften" / "Unknown.json", std::vector<FormRef>{ Sk(0x55555) }).verdict == AutoDraftVerdict::New);

	// The target is read again from disk: a list made before a person saved there does not let a write through.
	const auto stale = files;
	dir.Write("Riften/Unknown.json", TemplateJson("Skyrim.esm|0x55555", false));
	CHECK(CheckAutoDraftTarget(stale, base / "Riften" / "Unknown.json", std::vector<FormRef>{ Sk(0x55555) }).verdict == AutoDraftVerdict::Person);
	CHECK_FALSE(ReadTemplateFileInfo(base / "Nowhere.json"));
	CHECK(ScanTemplateFiles(base / "missing").empty());
}
