#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"

using namespace dova::core;
using namespace dova::test;

namespace
{
	// DESIGN §6.3 example, verbatim (including comments).
	constexpr std::string_view kDesignExample = R"({
  "$schema": "dovastash-layout-1",
  "name": "Breezehome",
  "note": "Each container takes the item categories listed...",  // optional, for people reading the file
  "author": "Dova Stash",
  "version": "1.0.0",
  "priority": 0,
  "requires": ["Skyrim.esm"],          // all must be loaded for the template to be offered
  "cell": "Skyrim.esm|0x000165A7",     // illustrative IDs; real ones come from xEdit or esm_dump.py
  "master": "Skyrim.esm|0x000AAAAA",   // a container reference (REFR), not the base CONT
  "containers": [
    {"ref": "Skyrim.esm|0x000BBBBB", "label": "Armory chest",    "categories": ["weapons.*", "armor.heavy", "armor.light", "armor.shields"]},
    {"ref": "Skyrim.esm|0x000CCCCC", "label": "Alchemy satchel", "categories": ["alchemy.ingredients", "alchemy.potions", "alchemy.poisons"]},
    {"ref": "Skyrim.esm|0x000DDDDD", "label": "Barrel",          "ignored": true, "note": "Respawns: what you store here can disappear."}
  ],
  "displays": [                        // optional: the displays this home fills (§8.4)
    {"ref": "Skyrim.esm|0x000EEEEE", "label": "Bow rack", "categories": ["weapons.bows"]},
    {"ref": "Skyrim.esm|0x000EEEF0", "label": "Sword rack", "items": ["MyArmory.esp|0x00000D62"], "only": true},  // only this sword; skipped without MyArmory.esp
    {"ref": "Skyrim.esm|0x000FFFFF", "kind": "item", "label": "Mask wall", "items": ["Skyrim.esm|0x00061CD6"]},  // what its script is made for
    {"ref": "Skyrim.esm|0x000EEEEF", "label": "Mannequin", "set": "Skyrim.esm|0x00013961"}  // wears the Ebony set
  ],
  "keep": {
    "inherit": true,
    "rules": [
      {"category": "ammo.arrows", "quota": 50}
    ]
  },

  // Player layouts only (never in shipped templates):
  "learned": {"Skyrim.esm|0x0001397E": "Skyrim.esm|0x000BBBBB"},
  "basedOn": "Templates/Vanilla/Breezehome.json@1.0.0"
})";

	Layout ParseOk(std::string_view a_text)
	{
		Diagnostics diags;
		auto        layout = ParseLayoutText(a_text, "test.json", diags);
		INFO((diags.Empty() ? std::string() : diags.Items().front().ToString()));
		REQUIRE(layout);
		REQUIRE_FALSE(diags.HasErrors());
		return *layout;
	}
}

TEST_CASE("DESIGN 6.3 layout example parses completely", "[layout]")
{
	Diagnostics diags;
	const auto  layout = ParseLayoutText(kDesignExample, "Breezehome.json", diags);
	REQUIRE(layout);
	CHECK(diags.Empty());

	CHECK(layout->name == "Breezehome");
	CHECK(layout->author == "Dova Stash");
	CHECK(layout->version == "1.0.0");
	CHECK(layout->priority == 0);
	CHECK(layout->requiredPlugins == std::vector<std::string>{ "Skyrim.esm" });
	CHECK(layout->cell == Sk(0x165A7));
	CHECK(layout->master == Sk(0xAAAAA));
	REQUIRE(layout->containers.size() == 3);
	CHECK(layout->containers[0].ref == Sk(0xBBBBB));
	CHECK(layout->containers[0].label == "Armory chest");
	CHECK(layout->containers[0].categories.size() == 4);
	CHECK_FALSE(layout->containers[0].ignored);
	CHECK(layout->containers[2].ignored);
	REQUIRE(layout->displays.size() == 4);
	CHECK(layout->displays[1].items == std::vector<FormRef>{ F("MyArmory.esp|0xD62") });
	CHECK(layout->displays[1].only);
	CHECK(layout->displays[2].items == std::vector<FormRef>{ Sk(0x61CD6) });
	CHECK_FALSE(layout->displays[2].only);
	CHECK(layout->displays[3].set == Sk(0x13961));
	REQUIRE(layout->keep);
	CHECK(layout->keep->inherit == true);
	CHECK(layout->keep->quotasKeyIsRules);
	REQUIRE(layout->keep->quotas.size() == 1);
	CHECK(layout->keep->quotas[0].category == "ammo.arrows");
	CHECK(layout->keep->quotas[0].quota == 50);
	REQUIRE(layout->learned.size() == 1);
	CHECK(layout->learned.at(Sk(0x1397E)) == Sk(0xBBBBB));
	CHECK(layout->basedOn == "Templates/Vanilla/Breezehome.json@1.0.0");
	CHECK(layout->IsMaster(F("skyrim.esm|0xaaaaa")));
	CHECK(layout->FindContainer(Sk(0xCCCCC))->label == "Alchemy satchel");
	CHECK(layout->FindContainer(Sk(0x12345)) == nullptr);
}

TEST_CASE("Layout round-trip is stable and ordered", "[layout]")
{
	const auto first = SerializeLayout(ParseOk(kDesignExample));
	const auto second = SerializeLayout(ParseOk(first));
	CHECK(first == second);

	// Fixed key order.
	const auto pos = [&](std::string_view a_key) { return first.find("\"" + std::string(a_key) + "\""); };
	CHECK(pos("$schema") < pos("name"));
	CHECK(pos("name") < pos("author"));
	CHECK(pos("author") < pos("version"));
	CHECK(pos("version") < pos("priority"));
	CHECK(pos("priority") < pos("requires"));
	CHECK(pos("requires") < pos("cell"));
	CHECK(pos("cell") < pos("master"));
	CHECK(pos("master") < pos("containers"));
	CHECK(pos("containers") < pos("keep"));
	CHECK(pos("keep") < pos("learned"));
	CHECK(pos("learned") < pos("basedOn"));
	// "rules" spelling is preserved, tabs are used, trailing newline.
	CHECK(first.find("\"rules\"") != std::string::npos);
	CHECK(first.find("\"quotas\"") == std::string::npos);
	CHECK(first.find("\n\t\"name\"") != std::string::npos);
	CHECK(first.back() == '\n');
}

TEST_CASE("SameSetup: formatting and the cell do not count, the content does", "[layout]")
{
	const auto layout = ParseOk(kDesignExample);
	auto       reformatted = ParseOk(LayoutToJson(layout).dump());  // one line, no tabs
	CHECK(SameSetup(layout, reformatted));
	CHECK(SameSetup(layout, ParseOk(SerializeLayout(layout))));

	// A backup copy is loaded with the cell forced to the home's cell: the cell never decides.
	reformatted.cell = Sk(0x1);
	CHECK(SameSetup(layout, reformatted));

	// One category more, and they differ.
	auto changed = layout;
	REQUIRE_FALSE(changed.containers.empty());
	changed.containers.front().categories.emplace_back("keys");
	CHECK_FALSE(SameSetup(layout, changed));

	// A learned item more, and they differ (restoring would lose it).
	auto learned = layout;
	learned.learned[Sk(0x12EB7)] = layout.master.value_or(Sk(0x1));
	CHECK_FALSE(SameSetup(layout, learned));
}

TEST_CASE("Layouts are written compactly, one container per line", "[layout]")
{
	const auto text = SerializeLayout(ParseOk(R"({
		"$schema": "dovastash-layout-1", "name": "Home", "note": "Read me", "cell": "Skyrim.esm|0x1",
		"containers": [
			{ "ref": "Skyrim.esm|0x2", "label": "Chest", "categories": ["weapons.*", "ammo.*"] },
			{ "ref": "Skyrim.esm|0x3", "label": "Big wardrobe", "categories": ["armor.clothing"] },
			{ "ref": "Skyrim.esm|0x4", "label": "Barrel", "ignored": true, "note": "respawns" }
		],
		"displays": [ { "ref": "Skyrim.esm|0x5", "note": "Leave the mask wall to me", "ignored": true } ],
		"keep": { "rules": [ { "category": "ammo.arrows", "quota": 50 } ] }
	})"));
	CHECK(text == "{\n"
	              "\t\"$schema\": \"dovastash-layout-1\",\n"
	              "\t\"name\": \"Home\",\n"
	              "\t\"note\": \"Read me\",\n"
	              "\t\"author\": \"\",\n"
	              "\t\"version\": \"\",\n"
	              "\t\"priority\": 0,\n"
	              "\t\"cell\": \"Skyrim.esm|0x00000001\",\n"
	              "\t\"containers\": [\n"
	              "\t\t{\"ref\": \"Skyrim.esm|0x00000002\", \"label\": \"Chest\",        \"categories\": [\"weapons.*\", \"ammo.*\"]},\n"
	              "\t\t{\"ref\": \"Skyrim.esm|0x00000003\", \"label\": \"Big wardrobe\", \"categories\": [\"armor.clothing\"]},\n"
	              "\t\t{\"ref\": \"Skyrim.esm|0x00000004\", \"label\": \"Barrel\",       \"ignored\": true, \"note\": \"respawns\"}\n"
	              "\t],\n"
	              "\t\"displays\": [\n"
	              "\t\t{\"ref\": \"Skyrim.esm|0x00000005\", \"ignored\": true, \"note\": \"Leave the mask wall to me\"}\n"
	              "\t],\n"
	              "\t\"keep\": {\n"
	              "\t\t\"rules\": [\n"
	              "\t\t\t{\"category\": \"ammo.arrows\", \"quota\": 50}\n"
	              "\t\t]\n"
	              "\t}\n"
	              "}\n");

	const auto layout = ParseOk(text);
	CHECK(layout.note == "Read me");
	CHECK(layout.containers[2].note == "respawns");
	CHECK(layout.displays[0].note == "Leave the mask wall to me");
	CHECK(SerializeLayout(layout) == text);
}

TEST_CASE("Entries too long for one line are spread out", "[layout]")
{
	std::string categories;
	for (int i = 0; i < 12; ++i) {
		categories += (i ? ", " : "") + std::string("\"misc.category_number_") + std::to_string(i) + "\"";
	}
	const auto text = SerializeLayout(ParseOk(R"({ "$schema": "dovastash-layout-1", "cell": "Skyrim.esm|0x1",
		"containers": [ { "ref": "Skyrim.esm|0x2", "label": "Everything", "categories": [)" + categories + R"(] } ] })"));
	CHECK(text.find("\t\t{\n\t\t\t\"ref\": \"Skyrim.esm|0x00000002\",\n\t\t\t\"label\": \"Everything\",\n\t\t\t\"categories\": [\n"
	                "\t\t\t\t\"misc.category_number_0\",\n") != std::string::npos);
	CHECK(SerializeLayout(ParseOk(text)) == text);
}

TEST_CASE("Layout serialization canonicalizes form refs and sorts learned", "[layout]")
{
	const auto a = ParseOk(R"({ "$schema": "dovastash-layout-1", "cell": "skyrim.esm|165a7",
		"learned": { "Skyrim.esm|0x2": "Skyrim.esm|0xB", "Dawnguard.esm|0x1": "Skyrim.esm|0xB", "Skyrim.esm|0x1": "Skyrim.esm|0xB" } })");
	const auto b = ParseOk(R"({ "learned": { "Skyrim.esm|0x1": "Skyrim.esm|0xB", "Skyrim.esm|0x2": "Skyrim.esm|0xB", "Dawnguard.esm|0x1": "Skyrim.esm|0xB" },
		"cell": "skyrim.esm|0x000165A7", "$schema": "dovastash-layout-1" })");
	const auto text = SerializeLayout(a);
	CHECK(text == SerializeLayout(b));
	CHECK(text.find("\"skyrim.esm|0x000165A7\"") != std::string::npos);
	CHECK(text.find("Dawnguard.esm|0x00000001") < text.find("Skyrim.esm|0x00000001"));
	CHECK(text.find("Skyrim.esm|0x00000001") < text.find("Skyrim.esm|0x00000002"));
}

TEST_CASE("Unknown fields survive a round-trip", "[layout]")
{
	const auto layout = ParseOk(R"({
		"$schema": "dovastash-layout-1",
		"cell": "Skyrim.esm|0x1",
		"zzFuture": { "x": [1, 2] },
		"aaFuture": "yes",
		"containers": [ { "ref": "Skyrim.esm|0x2", "remote": "central", "forms": ["Skyrim.esm|0x3"] } ],
		"keep": { "always": ["quest"], "quotas": [ { "category": "ammo.*", "quota": 5, "note": "hi" } ], "mode": "strict" }
	})");
	REQUIRE(layout.containers.size() == 1);
	CHECK(layout.containers[0].forms == std::vector<FormRef>{ Sk(0x3) });

	const auto text = SerializeLayout(layout);
	CHECK(text.find("\"zzFuture\"") != std::string::npos);
	CHECK(text.find("\"aaFuture\"") != std::string::npos);
	CHECK(text.find("\"remote\": \"central\"") != std::string::npos);
	CHECK(text.find("\"note\": \"hi\"") != std::string::npos);
	CHECK(text.find("\"mode\": \"strict\"") != std::string::npos);
	CHECK(text.find("\"quotas\"") != std::string::npos);
	// Unknown keys come after the known ones.
	CHECK(text.find("\"containers\"") < text.find("\"aaFuture\""));
	CHECK(SerializeLayout(ParseOk(text)) == text);
}

TEST_CASE("Malformed entries are reported but never dropped", "[layout]")
{
	Diagnostics diags;
	const auto  layout = ParseLayoutText(R"({
		"$schema": "dovastash-layout-1",
		"cell": "Skyrim.esm|0x1",
		"master": "garbage",
		"containers": [
			{ "ref": "Skyrim.esm|0x2", "forms": ["Skyrim.esm|0x3", "bad|ref"] },
			{ "ref": "NotAPlugin|0x2", "label": "keep me" },
			"just a string"
		],
		"learned": { "Skyrim.esm|0x10": "Skyrim.esm|0x2", "broken": "Skyrim.esm|0x2", "Skyrim.esm|0x11": 5 },
		"keep": { "quotas": [ { "quota": 3 }, { "category": "ammo.*", "quota": -1 }, { "category": "ammo.*", "quota": 2 } ] }
	})",
		"bad.json", diags);
	REQUIRE(layout);
	CHECK(diags.ErrorCount() >= 7);
	CHECK_FALSE(layout->master.has_value());
	CHECK(layout->containers.size() == 1);
	CHECK(layout->containers[0].unparsedForms == std::vector<std::string>{ "bad|ref" });
	CHECK(layout->unparsedContainers.size() == 2);
	CHECK(layout->learned.size() == 1);
	CHECK(layout->unparsedLearned.size() == 2);
	REQUIRE(layout->keep);
	CHECK(layout->keep->quotas.size() == 1);
	CHECK(layout->keep->unparsedQuotas.size() == 2);

	const auto text = SerializeLayout(*layout);
	CHECK(text.find("\"master\": \"garbage\"") != std::string::npos);
	CHECK(text.find("NotAPlugin|0x2") != std::string::npos);
	CHECK(text.find("keep me") != std::string::npos);
	CHECK(text.find("just a string") != std::string::npos);
	CHECK(text.find("\"broken\"") != std::string::npos);
	CHECK(text.find("\"bad|ref\"") != std::string::npos);
	CHECK(text.find("\"quota\": -1") != std::string::npos);

	// And the second round-trip is stable.
	Diagnostics again;
	const auto  reparsed = ParseLayoutText(text, "bad.json", again);
	REQUIRE(reparsed);
	CHECK(SerializeLayout(*reparsed) == text);
}

TEST_CASE("Layout document-level validation", "[layout]")
{
	SECTION("wrong schema is rejected")
	{
		Diagnostics diags;
		CHECK_FALSE(ParseLayoutText(R"({ "$schema": "dovastash-categories-1", "cell": "Skyrim.esm|0x1" })", "x", diags).has_value());
		CHECK(diags.HasErrors());
	}
	SECTION("not an object")
	{
		Diagnostics diags;
		CHECK_FALSE(ParseLayoutText("[1,2]", "x", diags).has_value());
		CHECK(diags.HasErrors());
	}
	SECTION("missing cell is an error")
	{
		Diagnostics diags;
		CHECK(ParseLayoutText(R"({ "$schema": "dovastash-layout-1" })", "x", diags).has_value());
		CHECK(diags.HasErrors());
	}
	SECTION("wrong-typed containers or learned make the document unusable (never overwritten)")
	{
		Diagnostics diags;
		CHECK_FALSE(ParseLayoutText(R"({ "$schema": "dovastash-layout-1", "cell": "Skyrim.esm|0x1",
			"containers": { "ref": "Skyrim.esm|0x2" } })", "x", diags).has_value());
		CHECK(diags.HasErrors());

		Diagnostics diags2;
		CHECK_FALSE(ParseLayoutText(R"({ "$schema": "dovastash-layout-1", "cell": "Skyrim.esm|0x1",
			"containers": [], "learned": [ "Skyrim.esm|0x3" ] })", "x", diags2).has_value());
		CHECK(diags2.HasErrors());

		Diagnostics diags3;
		CHECK_FALSE(ParseLayoutText(R"({ "$schema": "dovastash-layout-1", "cell": "Skyrim.esm|0x1", "containers": "chest" })", "x", diags3)
				.has_value());

		// Correct types (including empty ones) still parse.
		Diagnostics ok;
		CHECK(ParseLayoutText(R"({ "$schema": "dovastash-layout-1", "cell": "Skyrim.esm|0x1", "containers": [], "learned": {} })", "x", ok)
				.has_value());
		CHECK_FALSE(ok.HasErrors());
	}
	SECTION("non-semver version is a warning")
	{
		Diagnostics diags;
		CHECK(ParseLayoutText(R"({ "$schema": "dovastash-layout-1", "cell": "Skyrim.esm|0x1", "version": "first" })", "x", diags).has_value());
		CHECK_FALSE(diags.HasErrors());
		CHECK(diags.WarningCount() == 1);
	}
	SECTION("invalid keep entries are warnings and kept")
	{
		Diagnostics diags;
		const auto  layout = ParseLayoutText(R"({ "$schema": "dovastash-layout-1", "cell": "Skyrim.esm|0x1",
			"keep": { "always": ["equipped", "Skyrim.esm|0xF", "ammo.*", "bad pattern!"], "never": "misc.clutter" } })",
			"x", diags);
		REQUIRE(layout);
		CHECK_FALSE(diags.HasErrors());
		CHECK(diags.WarningCount() == 1);
		CHECK(layout->keep->always.size() == 4);
		CHECK(layout->keep->never == std::vector<std::string>{ "misc.clutter" });
		CHECK_FALSE(layout->keep->inherit.has_value());
		CHECK(layout->keep->Inherits());
	}
}

TEST_CASE("Keep selectors", "[layout][keep]")
{
	CHECK(KeepSelector::Parse("Equipped").kind == KeepSelector::Kind::Equipped);
	CHECK(KeepSelector::Parse("favorited").kind == KeepSelector::Kind::Favorited);
	CHECK(KeepSelector::Parse("QUEST").kind == KeepSelector::Kind::Quest);
	const auto form = KeepSelector::Parse("Skyrim.esm|0x0000000F");
	CHECK(form.kind == KeepSelector::Kind::Form);
	CHECK(form.form == Sk(0xF));
	CHECK(KeepSelector::Parse("Skyrim.esm|zz").kind == KeepSelector::Kind::Invalid);
	const auto cat = KeepSelector::Parse("alchemy.*");
	CHECK(cat.kind == KeepSelector::Kind::Category);
	CHECK(cat.pattern == "alchemy.*");
	CHECK(KeepSelector::Parse("not valid").kind == KeepSelector::Kind::Invalid);
}

TEST_CASE("Layout file helpers", "[layout][files]")
{
	CHECK(LayoutFileName(Sk(0x165A7)) == "Skyrim.esm_000165A7.json");
	CHECK(LayoutFileName(F("ccBGSSSE001-Fish.esl|0x800")) == "ccBGSSSE001-Fish.esl_00000800.json");

	TempDir    dir;
	const auto layout = ParseOk(kDesignExample);
	const auto path = dir.Path() / "Layouts" / LayoutFileName(*layout.cell);
	REQUIRE(SaveLayoutFile(path, layout));
	CHECK_FALSE(std::filesystem::exists(path.string() + ".tmp"));

	Diagnostics diags;
	const auto  loaded = LoadLayoutFile(path, "Layouts/x.json", diags);
	REQUIRE(loaded);
	CHECK_FALSE(diags.HasErrors());
	CHECK(SerializeLayout(*loaded) == SerializeLayout(layout));

	Diagnostics missing;
	CHECK_FALSE(LoadLayoutFile(dir.Path() / "nope.json", "nope", missing).has_value());
	CHECK(missing.HasErrors());
}

TEST_CASE("Keep config JSON round-trip", "[layout][keep]")
{
	const auto  defaults = DefaultKeepConfig();
	const auto  json = KeepConfigToJson(defaults);
	Diagnostics diags;
	const auto  parsed = ParseKeepConfig(nlohmann::json::parse(json.dump()), "settings", diags);
	REQUIRE(parsed);
	CHECK(diags.Empty());
	CHECK(parsed->always == defaults.always);
	CHECK(KeepConfigToJson(*parsed).dump() == json.dump());
}
