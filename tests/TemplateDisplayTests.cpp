// Displays in templates (DESIGN §8.4 "Displays in templates", core/TemplateDisplays.h): one entry shape in
// layouts and templates, its lint, soft references for template-chosen items, script-defined displays.

#include <algorithm>
#include <map>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"

using namespace dova::core;
using namespace dova::test;

namespace
{
	Layout ParseOk(std::string_view a_text, Diagnostics* a_diags = nullptr)
	{
		Diagnostics local;
		auto&       diags = a_diags ? *a_diags : local;
		auto        layout = ParseLayoutText(a_text, "test.json", diags);
		INFO((diags.Empty() ? std::string() : diags.Items().front().ToString()));
		REQUIRE(layout);
		REQUIRE_FALSE(diags.HasErrors());
		return *layout;
	}

	std::size_t CountContaining(const Diagnostics& a_diags, std::string_view a_text)
	{
		std::size_t n = 0;
		for (const auto& d : a_diags.Items()) {
			n += d.message.find(a_text) != std::string::npos ? 1u : 0u;
		}
		return n;
	}

	ItemInfo Armor(FormRef a_form, std::uint32_t a_slots, std::int32_t a_value)
	{
		auto item = MakeItem(std::move(a_form), ItemType::Armor);
		item.armorSlots = a_slots;
		item.value = a_value;
		return item;
	}

	ItemInfo Valued(ItemInfo a_item, std::int32_t a_value)
	{
		a_item.value = a_value;
		return a_item;
	}

	DisplayStock OnPlayer(ItemInfo a_item)
	{
		const auto n = a_item.count;
		return DisplayStock{ ContainerRef::Player(), std::move(a_item), n };
	}

	DisplaySpot RackSpot(std::uint32_t a_id, std::vector<FormRef> a_items = {}, bool a_only = false)
	{
		DisplaySpot spot;
		spot.ref = Sk(a_id);
		spot.kind = DisplayKind::Rack;
		spot.items = std::move(a_items);
		spot.only = a_only;
		return spot;
	}

	DisplaySpot MannequinSpot(std::uint32_t a_id, std::vector<FormRef> a_items = {}, bool a_only = false)
	{
		DisplaySpot spot;
		spot.ref = Sk(a_id);
		spot.kind = DisplayKind::Mannequin;
		spot.items = std::move(a_items);
		spot.only = a_only;
		return spot;
	}

	std::vector<std::pair<std::size_t, std::size_t>> Pairs(const std::vector<DisplayAssignment>& a_plan)
	{
		std::vector<std::pair<std::size_t, std::size_t>> out;
		for (const auto& a : a_plan) {
			out.emplace_back(a.spot, a.stock);
		}
		return out;
	}

	constexpr std::uint32_t kHead = 1u << 0;
	constexpr std::uint32_t kBody = 1u << 2;
	constexpr std::uint32_t kHands = 1u << 3;
	constexpr std::uint32_t kFeet = 1u << 7;

	// A template that pre-defines its displays, with items from a mod that is not installed.
	constexpr std::string_view kTemplateWithDisplays = R"({
  "$schema": "dovastash-layout-1",
  "name": "Whitepeak Tower",
  "author": "Someone",
  "version": "1.0.0",
  "requires": ["Tower.esp", "Skyrim.esm"],
  "cell": "Tower.esp|0x00000801",
  "master": "Tower.esp|0x00000802",
  "containers": [
    {"ref": "Tower.esp|0x00000803", "label": "Armory", "categories": ["weapons.*"]}
  ],
  "displays": [
    {"ref": "Tower.esp|0x00000810", "label": "Bow rack",        "categories": ["weapons.bows"]},
    {"ref": "Tower.esp|0x00000811", "label": "Sword rack",      "items": ["MyArmory.esp|0x00000D62", "Skyrim.esm|0x0002ACD2"], "only": true},
    {"ref": "Tower.esp|0x00000812", "label": "Mannequin",       "set": "MyArmor.esp|0x00000D70"},
    {"ref": "Tower.esp|0x00000813", "label": "Mask wall",       "items": ["MyArmor.esp|0x00000D71"]},
    {"ref": "Tower.esp|0x00000814", "label": "Modded rack",     "items": ["MyArmory.esp|0x00000D63"], "only": true},
    {"ref": "Tower.esp|0x00000815", "kind": "case", "label": "Claw case", "items": ["MyArmory.esp|0x00000D64"]},
    {"ref": "Tower.esp|0x00000816", "label": "Spare rack",      "items": ["MyArmory.esp|0x00000D65"], "ignored": true}
  ]
}
)";

	bool ExistsUnlessModded(const FormRef& a_form)
	{
		return !IEquals(a_form.plugin, "MyArmory.esp") && !IEquals(a_form.plugin, "MyArmor.esp");
	}

	// The scripts make 0x813 an item plaque; 0x810-0x812 and 0x814 are racks / a mannequin; 0x815 is not loaded.
	std::optional<DisplayKind> ScriptKinds(const FormRef& a_display)
	{
		static const std::map<std::uint32_t, DisplayKind> kinds{ { 0x810, DisplayKind::Rack }, { 0x811, DisplayKind::Rack },
			{ 0x812, DisplayKind::Mannequin }, { 0x813, DisplayKind::Item }, { 0x814, DisplayKind::Rack }, { 0x816, DisplayKind::Rack } };
		const auto it = kinds.find(a_display.localId);
		return it == kinds.end() ? std::nullopt : std::optional(it->second);
	}
}

TEST_CASE("Display entries have one shape: \"only\" is optional and only written when set", "[templates][displays]")
{
	const auto layout = ParseOk(kTemplateWithDisplays);
	REQUIRE(layout.displays.size() == 7);
	CHECK_FALSE(layout.displays[0].only);
	CHECK(layout.displays[1].only);
	CHECK(layout.displays[1].items == std::vector<FormRef>{ F("MyArmory.esp|0xD62"), Sk(0x2ACD2) });
	CHECK(layout.displays[2].set == F("MyArmor.esp|0xD70"));
	CHECK(layout.displays[5].kind == DisplayKind::Case);
	CHECK(layout.displays[6].ignored);

	// Written back in the fixed key order (ref, kind, label, categories, items, only, set, ignored, note).
	const auto json = SerializeLayout(layout);
	CHECK(json.find("\"items\": [\"MyArmory.esp|0x00000D62\", \"Skyrim.esm|0x0002ACD2\"], \"only\": true}") != std::string::npos);
	const auto again = ParseOk(json);
	CHECK(SerializeLayout(again) == json);

	// The wrong type is an error, kept verbatim.
	Diagnostics diags;
	const auto  bad = ParseLayoutText(R"({"$schema": "dovastash-layout-1", "name": "x", "cell": "Skyrim.esm|0x1", "containers": [],
		"displays": [{"ref": "Skyrim.esm|0x5", "only": "yes"}]})",
		 "bad.json", diags);
	REQUIRE(bad);
	CHECK(diags.ErrorCount() == 1);
	REQUIRE(bad->displays.size() == 1);
	CHECK_FALSE(bad->displays[0].only);
	CHECK(bad->displays[0].extra.at("only") == "yes");
	CHECK(SerializeLayout(*bad).find("\"only\": \"yes\"") != std::string::npos);
}

TEST_CASE("Layouts written before \"only\" existed load and are written back unchanged", "[templates][displays]")
{
	// A player layout as the DLL wrote it: plaque items recorded from the script, a preferred rack item, a set.
	constexpr std::string_view kOld =
		"{\n"
		"\t\"$schema\": \"dovastash-layout-1\",\n"
		"\t\"name\": \"Breezehome\",\n"
		"\t\"author\": \"Player\",\n"
		"\t\"version\": \"1.0.0\",\n"
		"\t\"priority\": 0,\n"
		"\t\"cell\": \"Skyrim.esm|0x000165A8\",\n"
		"\t\"master\": \"Skyrim.esm|0x000F3922\",\n"
		"\t\"containers\": [],\n"
		"\t\"displays\": [\n"
		"\t\t{\"ref\": \"Skyrim.esm|0x00000010\", \"label\": \"Weapon Rack\", \"items\": [\"Skyrim.esm|0x0002ACD2\"]},\n"
		"\t\t{\"ref\": \"Skyrim.esm|0x00000011\", \"label\": \"Mask wall\",   \"items\": [\"Skyrim.esm|0x00061CD6\"]},\n"
		"\t\t{\"ref\": \"Skyrim.esm|0x00000012\", \"label\": \"Mannequin\",   \"set\": \"Skyrim.esm|0x00013961\"},\n"
		"\t\t{\"ref\": \"Skyrim.esm|0x00000013\", \"label\": \"Bow rack\",    \"categories\": [\"weapons.bows\"], \"ignored\": true}\n"
		"\t],\n"
		"\t\"learned\": {\"Skyrim.esm|0x0001397E\": \"Skyrim.esm|0x000F3922\"}\n"
		"}\n";
	Diagnostics diags;
	const auto  layout = ParseOk(kOld, &diags);
	CHECK(diags.Empty());
	CHECK(SerializeLayout(layout) == kOld);
	CHECK(std::ranges::none_of(layout.displays, [](const DisplayBinding& d) { return d.only; }));
}

TEST_CASE("Display lint: fields that do nothing for the kind, only without items, displays used for storage", "[templates][displays]")
{
	Layout layout;
	layout.cell = Sk(0x1);
	layout.master = Sk(0x2);
	layout.requiredPlugins = { "Skyrim.esm" };
	layout.containers.push_back({ .ref = Sk(0x3), .label = "Chest" });
	layout.containers.push_back({ .ref = Sk(0x4), .label = "Barrel", .ignored = true });
	layout.displays.push_back({ .ref = Sk(0x10), .kind = DisplayKind::Rack, .set = Sk(0x99) });                          // set on a rack
	layout.displays.push_back({ .ref = Sk(0x11), .kind = DisplayKind::Item, .categories = { "weapons.*" } });         // categories on a plaque
	layout.displays.push_back({ .ref = Sk(0x12), .kind = DisplayKind::Case, .items = { Sk(0x98) }, .only = true });  // only on a case
	layout.displays.push_back({ .ref = Sk(0x13), .only = true });                                                   // only without items
	layout.displays.push_back({ .ref = Sk(0x14), .items = { Sk(0x97) }, .only = true, .set = Sk(0x96) });           // only next to a set
	layout.displays.push_back({ .ref = Sk(0x3), .kind = DisplayKind::Case });                                        // the chest: storage
	layout.displays.push_back({ .ref = Sk(0x2) });                                                                   // the master: storage
	layout.displays.push_back({ .ref = Sk(0x4), .kind = DisplayKind::Case });                                        // ignored container: fine
	// Clean entries: no warnings.
	layout.displays.push_back({ .ref = Sk(0x20), .label = "Bow rack", .categories = { "weapons.bows" } });
	layout.displays.push_back({ .ref = Sk(0x21), .label = "Sword rack", .items = { F("Mod.esp|0x1") }, .only = true });
	layout.displays.push_back({ .ref = Sk(0x22), .kind = DisplayKind::Mannequin, .categories = { "armor.heavy" }, .set = F("Armor.esp|0x2") });
	layout.displays.push_back({ .ref = Sk(0x23), .kind = DisplayKind::Item, .items = { F("Masks.esp|0x3") } });

	Diagnostics diags;
	ValidateDisplays(layout, "t.json", diags);
	for (const auto& d : diags.Items()) {
		UNSCOPED_INFO(d.ToString());
	}
	CHECK_FALSE(diags.HasErrors());
	CHECK(diags.WarningCount() == 7);
	CHECK(CountContaining(diags, "\"set\" is only used by mannequins") == 1);
	CHECK(CountContaining(diags, "\"categories\" are only used by racks and mannequins") == 1);
	CHECK(CountContaining(diags, "\"only\" is for racks and mannequins") == 1);
	CHECK(CountContaining(diags, "\"only\" without \"items\"") == 1);
	CHECK(CountContaining(diags, "\"only\" is ignored next to \"set\"") == 1);
	CHECK(CountContaining(diags, "also listed as a container") == 1);
	CHECK(CountContaining(diags, "also listed as the master") == 1);

	// ValidateTemplate runs the same lint, and never asks for the plugins of display items or sets in requires
	// (Mod.esp, Armor.esp, Masks.esp are soft references); the display refs themselves still need theirs.
	Diagnostics tpl;
	ValidateTemplate(layout, "t.json", tpl);
	CHECK(tpl.WarningCount() == 7);
	CHECK(CountContaining(tpl, "requires") == 0);
	layout.displays.push_back({ .ref = F("Other.esp|0x30") });
	Diagnostics other;
	ValidateTemplate(layout, "t.json", other);
	CHECK(CountContaining(other, "references Other.esp, which is not listed in \"requires\"") == 1);
}

TEST_CASE("Display items and sets never make a template depend on their plugins", "[templates][displays][export]")
{
	const auto tpl = ParseOk(kTemplateWithDisplays);
	CHECK(ReferencedPlugins(tpl) == std::vector<std::string>{ "Tower.esp" });  // cell, master, containers, display refs

	Diagnostics lint;
	ValidateTemplate(tpl, "Templates/Someone/Tower.json", lint);
	INFO((lint.Empty() ? std::string() : lint.Items().front().ToString()));
	CHECK(lint.Empty());

	// Offered without MyArmory.esp / MyArmor.esp: only `requires` decides.
	const std::vector<TemplateCandidate> candidates{ { "Templates/Someone/Tower.json", tpl } };
	const std::vector<std::string>       loaded{ "Skyrim.esm", "Tower.esp" };
	const auto                           res = ResolveLayout(nullptr, candidates, F("Tower.esp|0x801"), loaded);
	CHECK(res.kind == LayoutResolution::Kind::Template);
	CHECK(res.rejected.empty());

	// Exporting a player layout keeps its display items and set but does not require their plugins.
	Layout player = tpl;
	player.requiredPlugins.clear();
	const auto exported = ExportAsTemplate(player);
	CHECK(exported.requiredPlugins == std::vector<std::string>{ "Tower.esp" });
	CHECK(exported.FindDisplay(F("Tower.esp|0x812"))->set == F("MyArmor.esp|0xD70"));
	CHECK(exported.FindDisplay(F("Tower.esp|0x811"))->only);
}

TEST_CASE("Template-chosen display items that don't exist are listed once per display, never a plaque's or case's", "[templates][displays]")
{
	const auto tpl = ParseOk(kTemplateWithDisplays);

	SECTION("the template on its own")
	{
		const auto missing = MissingTemplateItems(tpl, tpl, ScriptKinds, ExistsUnlessModded);
		REQUIRE(missing.size() == 3);

		// Sword rack: the modded sword is skipped, Dawnbreaker still exists, so it does not wait.
		CHECK(missing[0].display == F("Tower.esp|0x811"));
		CHECK(missing[0].label == "Sword rack");
		CHECK(missing[0].items == std::vector<FormRef>{ F("MyArmory.esp|0xD62") });
		CHECK_FALSE(missing[0].set);
		CHECK_FALSE(missing[0].waits);
		CHECK(MissingItemsMessage("Whitepeak Tower", missing[0]) ==
			  "Template Whitepeak Tower: Sword rack: 1 item doesn't exist in this load order (MyArmory.esp|0x00000D62); skipped");

		// Mannequin: its set's plugin is missing; it waits, as a chosen set does.
		CHECK(missing[1].display == F("Tower.esp|0x812"));
		CHECK(missing[1].items.empty());
		CHECK(missing[1].set == F("MyArmor.esp|0xD70"));
		CHECK(missing[1].waits);
		CHECK(MissingItemsMessage("Whitepeak Tower", missing[1]) ==
			  "Template Whitepeak Tower: Mannequin: its armor set doesn't exist in this load order (MyArmor.esp|0x00000D70); skipped, nothing else goes "
			  "on it until it does");

		// Modded rack: "only" and none of its items exists.
		CHECK(missing[2].display == F("Tower.esp|0x814"));
		CHECK(missing[2].waits);

		// Not listed: the bow rack (nothing chosen), the mask wall (an item plaque by its script), the claw case
		// (declared a case; not loaded), the spare rack (ignored).
	}

	SECTION("a layout made from it: only what the template chose and the layout still names")
	{
		auto layout = ApplyTemplate(TemplateCandidate{ "Templates/Someone/Tower.json", tpl });
		layout.FindDisplay(F("Tower.esp|0x812"))->set = Sk(0x13961);  // the player picked the Ebony set
		layout.FindDisplay(F("Tower.esp|0x814"))->items.push_back(F("MyArmory.esp|0xD66"));  // added by hand: not the template's
		layout.FindDisplay(F("Tower.esp|0x811"))->ignored = true;  // turned off
		const auto missing = MissingTemplateItems(layout, tpl, ScriptKinds, ExistsUnlessModded);
		REQUIRE(missing.size() == 1);
		CHECK(missing[0].display == F("Tower.esp|0x814"));
		CHECK(missing[0].items == std::vector<FormRef>{ F("MyArmory.esp|0xD63") });
		CHECK(missing[0].waits);
		CHECK(MissingItemsMessage("Whitepeak Tower", missing[0]) ==
			  "Template Whitepeak Tower: Modded rack: 1 item doesn't exist in this load order (MyArmory.esp|0x00000D63); skipped, nothing else goes on "
			  "it until it does");
	}

	SECTION("kinds come from the scripts first, then the entry")
	{
		// Without the scripts (nothing loaded), entries without a kind are not judged yet; the declared case never is.
		const auto none = MissingTemplateItems(tpl, tpl, {}, ExistsUnlessModded);
		CHECK(none.empty());

		// A display the entry calls a rack but whose script makes it an item plaque: the script decides.
		auto plaque = tpl;
		plaque.FindDisplay(F("Tower.esp|0x811"))->kind = DisplayKind::Rack;
		const auto kinds = [](const FormRef& a_display) -> std::optional<DisplayKind> {
			return a_display.localId == 0x811 ? std::optional(DisplayKind::Item) : std::nullopt;
		};
		CHECK(MissingTemplateItems(plaque, plaque, kinds, ExistsUnlessModded).empty());
	}

	SECTION("everything there: nothing to report")
	{
		CHECK(MissingTemplateItems(tpl, tpl, ScriptKinds, [](const FormRef&) { return true; }).empty());
	}

	SECTION("message wording")
	{
		MissingDisplayItems both{ .display = Sk(0x1), .label = "Mannequin", .items = { F("A.esp|0x1"), F("A.esp|0x2") }, .set = F("A.esp|0x3"), .waits = true };
		CHECK(MissingItemsMessage("T", both) ==
			  "Template T: Mannequin: 2 items and its armor set don't exist in this load order (A.esp|0x00000001, A.esp|0x00000002, A.esp|0x00000003); "
			  "skipped, nothing else goes on it until they do");
		MissingDisplayItems two{ .display = Sk(0x1), .label = "Rack", .items = { F("A.esp|0x1"), F("A.esp|0x2") } };
		CHECK(MissingItemsMessage("T", two) == "Template T: Rack: 2 items don't exist in this load order (A.esp|0x00000001, A.esp|0x00000002); skipped");
	}
}

TEST_CASE("A template's items on an item plaque or case are recognised as its copy", "[templates][displays]")
{
	const auto tpl = ParseOk(kTemplateWithDisplays);
	const std::vector<TemplateCandidate> candidates{ { "Templates/Other.json", Layout{} }, { "Templates/Someone/Tower.json", tpl } };

	auto layout = ApplyTemplate(candidates[1]);
	CHECK(BasedOnTemplate(layout, candidates) == &candidates[1]);
	layout.basedOn = "Templates/Someone/Tower.json@0.9.0";  // an older version of it: still that template
	CHECK(BasedOnTemplate(layout, candidates) == &candidates[1]);
	layout.basedOn = "Templates/Someone/Tower.json.bak@1.0.0";
	CHECK(BasedOnTemplate(layout, candidates) == nullptr);
	layout.basedOn.reset();
	CHECK(BasedOnTemplate(layout, candidates) == nullptr);

	// The mask wall's list is the template's copy: the plugin replaces it with what the plaque's script is made for.
	const auto* templateEntry = tpl.FindDisplay(F("Tower.esp|0x813"));
	auto        entry = *layout.FindDisplay(F("Tower.esp|0x813"));
	CHECK(ItemsCopiedFromTemplate(entry, templateEntry));
	// The player's own edit (or the script's items written before) is not.
	entry.items = { Sk(0x61CD6) };
	CHECK_FALSE(ItemsCopiedFromTemplate(entry, templateEntry));
	// A template entry without items has no copy to replace.
	CHECK_FALSE(ItemsCopiedFromTemplate(*layout.FindDisplay(F("Tower.esp|0x810")), tpl.FindDisplay(F("Tower.esp|0x810"))));
	CHECK_FALSE(ItemsCopiedFromTemplate(entry, nullptr));

	CHECK(ItemsChosenByFile(DisplayKind::Rack));
	CHECK(ItemsChosenByFile(DisplayKind::Mannequin));
	CHECK(ItemsChosenByFile(std::nullopt));
	CHECK_FALSE(ItemsChosenByFile(DisplayKind::Item));
	CHECK_FALSE(ItemsChosenByFile(DisplayKind::Case));
}

TEST_CASE("Applying a template with pre-defined displays over a player layout keeps the player's choices", "[templates][displays]")
{
	const auto tpl = ParseOk(kTemplateWithDisplays);

	Layout player;
	player.cell = F("Tower.esp|0x801");
	player.master = F("Tower.esp|0x802");
	player.displays.push_back({ .ref = F("Tower.esp|0x810"), .label = "My bows", .ignored = true });              // turned off by the player
	player.displays.push_back({ .ref = F("Tower.esp|0x812"), .label = "Mannequin", .set = Sk(0x13961) });          // the player's set
	player.displays.push_back({ .ref = F("Tower.esp|0x813"), .items = { Sk(0x61CD6) } });                          // recorded from the script
	player.displays.push_back({ .ref = F("Tower.esp|0x820"), .label = "Shield plaque", .items = { Sk(0x1) }, .only = true });  // only the player's

	auto withoutSet = tpl;
	withoutSet.FindDisplay(F("Tower.esp|0x812"))->set.reset();
	withoutSet.FindDisplay(F("Tower.esp|0x811"))->items.clear();
	withoutSet.FindDisplay(F("Tower.esp|0x811"))->only = false;
	player.displays.push_back({ .ref = F("Tower.esp|0x811"), .items = { Sk(0x2) }, .only = true });  // the player's rack choice

	SECTION("the template names a set and items: they win; ignored stays")
	{
		const auto merged = ApplyTemplate(TemplateCandidate{ "Templates/Someone/Tower.json", tpl }, &player);
		CHECK(merged.FindDisplay(F("Tower.esp|0x810"))->ignored);
		CHECK(merged.FindDisplay(F("Tower.esp|0x810"))->categories == std::vector<std::string>{ "weapons.bows" });
		CHECK(merged.FindDisplay(F("Tower.esp|0x810"))->label == "Bow rack");
		CHECK(merged.FindDisplay(F("Tower.esp|0x812"))->set == F("MyArmor.esp|0xD70"));
		CHECK(merged.FindDisplay(F("Tower.esp|0x811"))->items == std::vector<FormRef>{ F("MyArmory.esp|0xD62"), Sk(0x2ACD2) });
		CHECK(merged.FindDisplay(F("Tower.esp|0x811"))->only);
		// The mask wall gets the template's copy here; the plugin swaps it for the script's (ItemsCopiedFromTemplate).
		CHECK(ItemsCopiedFromTemplate(*merged.FindDisplay(F("Tower.esp|0x813")), tpl.FindDisplay(F("Tower.esp|0x813"))));
		REQUIRE(merged.FindDisplay(F("Tower.esp|0x820")));  // only in the player layout: kept as it is
		CHECK(merged.FindDisplay(F("Tower.esp|0x820"))->only);
		CHECK(merged.displays.size() == 8);  // the template's 7 + the player's shield plaque
	}

	SECTION("the template names none: the player's set, items and their \"only\" stay")
	{
		const auto merged = ApplyTemplate(TemplateCandidate{ "Templates/Someone/Tower.json", withoutSet }, &player);
		CHECK(merged.FindDisplay(F("Tower.esp|0x812"))->set == Sk(0x13961));
		CHECK(merged.FindDisplay(F("Tower.esp|0x811"))->items == std::vector<FormRef>{ Sk(0x2) });
		CHECK(merged.FindDisplay(F("Tower.esp|0x811"))->only);
	}

	SECTION("re-applying onto the layout it made changes nothing")
	{
		const auto first = ApplyTemplate(TemplateCandidate{ "Templates/Someone/Tower.json", tpl }, &player);
		const auto again = ApplyTemplate(TemplateCandidate{ "Templates/Someone/Tower.json", tpl }, &first);
		CHECK(SerializeLayout(again) == SerializeLayout(first));
	}
}

TEST_CASE("A rack or mannequin with \"only\" takes nothing but its items, which are kept for it", "[display][displays]")
{
	const auto dawnbreaker = Valued(Weapon(Sk(0x2ACD2), WeaponType::OneHandSword), 740);
	const auto steel = Valued(Weapon(Sk(0x13989), WeaponType::OneHandSword), 45);
	const auto daedric = Valued(Weapon(Sk(0x139B9), WeaponType::OneHandSword), 1250);

	SECTION("racks")
	{
		const std::vector<DisplayStock> stocks{ OnPlayer(daedric), OnPlayer(dawnbreaker), OnPlayer(steel) };
		// The plain rack comes first in the list and would take the best sword; Dawnbreaker is kept for its rack.
		const std::vector<DisplaySpot> spots{ RackSpot(0xA0), RackSpot(0xA1, { Sk(0x2ACD2) }, true), RackSpot(0xA2, { Sk(0x2ACD2) }) };
		const auto                     plan = PlanDisplays(spots, stocks, {});
		CHECK(Pairs(plan) == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 0 }, { 1, 1 }, { 2, 2 } });
	}

	SECTION("without its items it waits; a plain preferred rack takes something else")
	{
		const std::vector<DisplayStock> stocks{ OnPlayer(steel) };
		const std::vector<DisplaySpot>  spots{ RackSpot(0xA1, { Sk(0x2ACD2) }, true), RackSpot(0xA2, { Sk(0x2ACD2) }) };
		const auto                      plan = PlanDisplays(spots, stocks, {});
		CHECK(Pairs(plan) == std::vector<std::pair<std::size_t, std::size_t>>{ { 1, 0 } });

		// Its items all missing from the load order (the plugin passes none): it stays empty too.
		const std::vector<DisplaySpot> missing{ RackSpot(0xA1, {}, true) };
		CHECK(PlanDisplays(missing, stocks, {}).empty());
	}

	SECTION("mannequins: only its items, on the slots it leaves free")
	{
		const std::vector<DisplayStock> stocks{
			OnPlayer(Armor(F("Mod.esp|0x1"), kBody, 100)),   // 0 its cuirass
			OnPlayer(Armor(F("Mod.esp|0x2"), kHead, 50)),    // 1 its helmet
			OnPlayer(Armor(Sk(0x13961), kBody, 1000)),       // 2 a better cuirass, not its own
			OnPlayer(Armor(Sk(0x13962), kHands, 300)),       // 3 gauntlets, not its own
			OnPlayer(Armor(Sk(0x13963), kFeet, 300)),        // 4 boots for the other mannequin
		};
		const std::vector<DisplaySpot> spots{ MannequinSpot(0xB0), MannequinSpot(0xB1, { F("Mod.esp|0x1"), F("Mod.esp|0x2") }, true) };
		const auto                     plan = PlanDisplays(spots, stocks, {});
		std::vector<std::size_t>       onOnly;
		for (const auto& a : plan) {
			if (a.spot == 1) {
				onOnly.push_back(a.stock);
			} else {
				CHECK(a.stock != 0);  // its pieces are kept for it
				CHECK(a.stock != 1);
			}
		}
		std::ranges::sort(onOnly);
		CHECK(onOnly == std::vector<std::size_t>{ 0, 1 });

		// Wearing its cuirass already: only the helmet goes on.
		auto wearing = spots;
		wearing[1].full = true;
		wearing[1].holds = { F("Mod.esp|0x1") };
		wearing[1].worn = kBody;
		const std::vector<DisplayStock> rest{ OnPlayer(Armor(F("Mod.esp|0x2"), kHead, 50)) };
		const std::vector<DisplaySpot>  one{ wearing[1] };
		CHECK(Pairs(PlanDisplays(one, rest, {})) == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 0 } });

		// Wearing something that is not its own: left alone.
		wearing[1].holds = { Sk(0x13961) };
		const std::vector<DisplaySpot> other{ wearing[1] };
		CHECK(PlanDisplays(other, rest, {}).empty());
	}
}
