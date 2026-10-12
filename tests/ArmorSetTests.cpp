#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"

using namespace dova::core;
using namespace dova::test;

namespace
{
	constexpr std::uint32_t kHead = 1u << 0;
	constexpr std::uint32_t kBody = 1u << 2;
	constexpr std::uint32_t kHands = 1u << 3;
	constexpr std::uint32_t kFeet = 1u << 7;
	constexpr std::uint32_t kAmulet = 1u << 5;

	FormRef Mod(std::string_view a_plugin, std::uint32_t a_id) { return FormRef{ std::string(a_plugin), a_id }; }

	ArmorPiece Piece(FormRef a_form, std::string a_name, std::uint32_t a_slots, std::initializer_list<const char*> a_keywords = {},
		std::optional<ArmorType> a_type = std::nullopt, std::int32_t a_value = 0, std::string a_family = {})
	{
		ArmorPiece p;
		p.form = std::move(a_form);
		p.name = std::move(a_name);
		p.slots = a_slots;
		for (const auto* k : a_keywords) {
			p.keywords.emplace_back(k);
		}
		p.armorType = a_type;
		p.value = a_value;
		p.family = std::move(a_family);
		return p;
	}

	ItemInfo Armor(FormRef a_form, std::string a_name, std::uint32_t a_slots, std::int32_t a_value, std::initializer_list<const char*> a_keywords = {})
	{
		auto item = MakeItem(std::move(a_form), ItemType::Armor);
		item.name = std::move(a_name);
		item.armorSlots = a_slots;
		item.value = a_value;
		for (const auto* k : a_keywords) {
			item.keywords.emplace_back(k);
		}
		return item;
	}

	DisplayStock OnPlayer(ItemInfo a_item)
	{
		const auto n = a_item.count;
		return DisplayStock{ ContainerRef::Player(), std::move(a_item), n };
	}

	DisplaySpot Mannequin(std::uint32_t a_id)
	{
		DisplaySpot s;
		s.ref = Sk(a_id);
		s.kind = DisplayKind::Mannequin;
		return s;
	}

	// The pieces of the grouping tests: vanilla sets, a mod tagging its armor with a vanilla keyword, a mod
	// without keywords (two sets), a vanilla helmet with a longer name.
	std::vector<ArmorPiece> MixedPieces()
	{
		return {
			Piece(Sk(0x13961), "Ebony Armor", kBody, { "ArmorMaterialEbony" }, ArmorType::Heavy),                       // 0
			Piece(Sk(0x13962), "Ebony Boots", kFeet, { "ArmorMaterialEbony" }, ArmorType::Heavy),                       // 1
			Piece(Sk(0x13963), "Ebony Gauntlets", kHands, { "ArmorMaterialEbony" }, ArmorType::Heavy),                  // 2
			Piece(Sk(0x13965), "Ebony Helmet", kHead, { "ArmorMaterialEbony" }, ArmorType::Heavy),                      // 3
			Piece(Sk(0x52794), "Ebony Mail", kBody, { "ArmorMaterialEbony", "DaedricArtifact" }, ArmorType::Heavy),     // 4
			Piece(Mod("ShadowKnight.esp", 0x800), "Shadow Knight Cuirass", kBody, { "ArmorMaterialEbony" }, ArmorType::Heavy),  // 5
			Piece(Mod("ShadowKnight.esp", 0x801), "Shadow Knight Helm", kHead, { "ArmorMaterialEbony" }, ArmorType::Heavy),     // 6
			Piece(Mod("Valkyrie.esp", 0x810), "Valkyrie Cuirass", kBody, {}, ArmorType::Light),                         // 7
			Piece(Mod("Valkyrie.esp", 0x811), "Valkyrie Winged Helmet", kHead, {}, ArmorType::Light),                   // 8
			Piece(Mod("Valkyrie.esp", 0x812), "Valkyrie Gauntlets", kHands, {}, ArmorType::Light),                      // 9
			Piece(Mod("Valkyrie.esp", 0x813), "Valkyrie Boots", kFeet, {}, ArmorType::Light),                           // 10
			Piece(Mod("Valkyrie.esp", 0x820), "Raven Cuirass", kBody, {}, ArmorType::Light),                            // 11
			Piece(Mod("Valkyrie.esp", 0x821), "Raven Boots", kFeet, {}, ArmorType::Light),                              // 12
			Piece(Sk(0x12E49), "Steel Armor", kBody, { "ArmorMaterialSteel" }, ArmorType::Heavy),                       // 13
			Piece(Sk(0x12E4B), "Steel Horned Helmet", kHead, { "ArmorMaterialSteel" }, ArmorType::Heavy),               // 14
		};
	}

	const ArmorSet* Named(const std::vector<ArmorSet>& a_sets, std::string_view a_name)
	{
		const auto it = std::ranges::find_if(a_sets, [&](const ArmorSet& s) { return s.name == a_name; });
		return it != a_sets.end() ? &*it : nullptr;
	}
}

TEST_CASE("Armor name stems name the set, not the piece", "[armorsets]")
{
	CHECK(ArmorNameStem("Ebony Cuirass") == "Ebony");
	CHECK(ArmorNameStem("Ebony Armor") == "Ebony");
	CHECK(ArmorNameStem("Daedric Helmet") == "Daedric");
	CHECK(ArmorNameStem("Ebony Mail") == "Ebony");
	CHECK(ArmorNameStem("Steel Plate Armor") == "Steel Plate");
	CHECK(ArmorNameStem("Shrouded Hand Wraps") == "Shrouded");
	CHECK(ArmorNameStem("Guild Master's Armor") == "Guild Master's");
	// "<piece> of <name>": the name; "<set> <piece> of <enchantment>": the set.
	CHECK(ArmorNameStem("Armor of the Old Gods") == "Old Gods");
	CHECK(ArmorNameStem("Helm of Yngol") == "Yngol");
	CHECK(ArmorNameStem("Ebony Armor of Major Health") == "Ebony");
	// Modded spellings: brackets, dashes, any case.
	CHECK(ArmorNameStem("Valkyrie Armor [Heavy]") == "Valkyrie");
	CHECK(ArmorNameStem("Valkyrie - Cuirass") == "Valkyrie");
	CHECK(ArmorNameStem("VALKYRIE BOOTS") == "VALKYRIE");
	CHECK(ArmorNameStem("Dwarven Armor - Heavy") == "Dwarven Heavy");
	// Only piece words: no stem.
	CHECK(ArmorNameStem("Boots").empty());
	CHECK(ArmorNameStem("Sallet").empty());
	CHECK(ArmorNameStem("").empty());
}

TEST_CASE("Armor material keywords", "[armorsets]")
{
	CHECK(ArmorMaterialKeyword(Piece(Sk(1), "x", kBody, { "VendorItemArmor", "ArmorMaterialEbony" })) == "ArmorMaterialEbony");
	CHECK(ArmorMaterialKeyword(Piece(Sk(1), "x", kBody, { "DLC2ArmorMaterialStalhrimHeavy" })) == "DLC2ArmorMaterialStalhrimHeavy");
	CHECK(ArmorMaterialKeyword(Piece(Sk(1), "x", kBody, { "ArmorHeavy" })).empty());
}

TEST_CASE("Armor pieces group by plugin, material keyword and name", "[armorsets]")
{
	const auto pieces = MixedPieces();
	const auto groups = GroupArmorPieces(pieces);
	using G = std::vector<std::size_t>;
	REQUIRE(groups.size() == 5);
	CHECK(groups[0] == G{ 0, 1, 2, 3, 4 });  // vanilla Ebony, with the Ebony Mail
	CHECK(groups[1] == G{ 5, 6 });           // a mod's own set, even with the vanilla keyword
	CHECK(groups[2] == G{ 7, 8, 9, 10 });    // no keywords: by name ("Valkyrie Winged" joins "Valkyrie")
	CHECK(groups[3] == G{ 11, 12 });         // another set in the same mod
	CHECK(groups[4] == G{ 13, 14 });         // "Steel Horned" joins "Steel"
}

TEST_CASE("Pieces sharing no name group by the family the plugin supplies", "[armorsets]")
{
	const std::string ingot = "Skyrim.esm|0x0005ACE5";
	const std::vector<ArmorPiece> pieces{
		Piece(Mod("Knight.esp", 0x800), "Knight Cuirass", kBody, {}, ArmorType::Heavy, 0, ingot),     // 0
		Piece(Mod("Knight.esp", 0x801), "Sallet", kHead, {}, ArmorType::Heavy, 0, ingot),             // 1 only piece words
		Piece(Mod("Knight.esp", 0x802), "Sabatons", kFeet, {}, ArmorType::Heavy, 0, ingot),           // 2
		Piece(Mod("Knight.esp", 0x803), "Brigand Cuirass", kBody, {}, ArmorType::Heavy, 0, ingot),    // 3 a second body piece
		Piece(Mod("Knight.esp", 0x804), "Gauntlets of Valor", kHands, {}, ArmorType::Heavy, 0, ingot),  // 4
		Piece(Mod("Knight.esp", 0x805), "Hood", kHead, {}, ArmorType::Light),                          // 5 no family
	};
	const auto groups = GroupArmorPieces(pieces);
	using G = std::vector<std::size_t>;
	// One piece per slot group: the second cuirass starts a set of its own.
	REQUIRE(groups.size() == 3);
	CHECK(groups[0] == G{ 0, 1, 2, 4 });
	CHECK(groups[1] == G{ 3 });
	CHECK(groups[2] == G{ 5 });

	// Families never merge pieces that already share a name, nor pieces of another plugin.
	std::vector<ArmorPiece> other{ Piece(Mod("Other.esp", 0x800), "Sallet", kHead, {}, ArmorType::Heavy, 0, ingot), pieces[0] };
	CHECK(GroupArmorPieces(other).size() == 2);
}

TEST_CASE("Armor sets get names and ids", "[armorsets]")
{
	const auto pieces = MixedPieces();
	const auto sets = BuildArmorSets(pieces);
	REQUIRE(sets.size() == 5);
	// Sorted by name.
	CHECK(sets[0].name == "Ebony");
	CHECK(sets[1].name == "Raven");
	CHECK(sets[2].name == "Shadow Knight");
	CHECK(sets[3].name == "Steel");
	CHECK(sets[4].name == "Valkyrie");

	const auto* ebony = Named(sets, "Ebony");
	REQUIRE(ebony);
	CHECK(ebony->id == Sk(0x13961));  // the first body piece (FormRef order), not the Ebony Mail
	CHECK(ebony->plugin == "Skyrim.esm");
	CHECK(ebony->material == "ArmorMaterialEbony");
	CHECK(ebony->armorType == ArmorType::Heavy);
	CHECK(ebony->slots == (kBody | kFeet | kHands | kHead));
	CHECK(Named(sets, "Valkyrie")->id == Mod("Valkyrie.esp", 0x810));
	CHECK(Named(sets, "Valkyrie")->material.empty());

	// A set of one piece is named after the piece; one without a body piece takes its first piece as id.
	const std::vector<ArmorPiece> single{ Piece(Sk(0x60), "Jagged Crown", kHead, { "ArmorMaterialDragonplate" }) };
	const auto                    one = BuildArmorSets(single);
	REQUIRE(one.size() == 1);
	CHECK(one[0].name == "Jagged Crown");
	CHECK(one[0].id == Sk(0x60));
}

TEST_CASE("Armor set names are unique", "[armorsets]")
{
	const std::vector<ArmorPiece> pieces{
		Piece(Sk(0x10), "Imperial Armor", kBody, { "ArmorMaterialImperialHeavy" }, ArmorType::Heavy),
		Piece(Sk(0x11), "Imperial Helmet", kHead, { "ArmorMaterialImperialHeavy" }, ArmorType::Heavy),
		Piece(Sk(0x20), "Imperial Cuirass", kBody, { "ArmorMaterialImperialLight" }, ArmorType::Light),
		Piece(Sk(0x21), "Imperial Bracers", kHands, { "ArmorMaterialImperialLight" }, ArmorType::Light),
		Piece(Mod("Legion.esp", 0x800), "Imperial Cuirass", kBody, {}, ArmorType::Heavy),
		Piece(Mod("Legion.esp", 0x801), "Imperial Boots", kFeet, {}, ArmorType::Heavy),
	};
	const auto sets = BuildArmorSets(pieces);
	REQUIRE(sets.size() == 3);
	CHECK(Named(sets, "Imperial (Light)"));
	CHECK(Named(sets, "Imperial (Heavy) (Skyrim.esm)"));
	CHECK(Named(sets, "Imperial (Heavy) (Legion.esp)"));
}

TEST_CASE("Outfits take a body piece first and one piece per slot", "[armorsets]")
{
	const std::vector<ArmorPiece> pieces{
		Piece(Sk(0x1), "Cuirass", kBody, {}, ArmorType::Heavy),          // 0
		Piece(Sk(0x2), "Hood", kHead, {}, ArmorType::Light),             // 1
		Piece(Sk(0x3), "Helmet", kHead, {}, ArmorType::Heavy),           // 2
		Piece(Sk(0x4), "Gauntlets", kHands, {}, ArmorType::Heavy),       // 3
		Piece(Sk(0x5), "Wraps", kHands | kFeet, {}, ArmorType::Light),   // 4
		Piece(Sk(0x6), "Robes", kBody, {}, ArmorType::Light),            // 5
		Piece(Sk(0x7), "Amulet", kAmulet, {}, ArmorType::Clothing),      // 6 no slot a mannequin shows
	};
	using P = std::vector<std::size_t>;
	const std::vector<std::size_t> order{ 1, 0, 2, 3, 4, 5, 6 };  // best first
	// The best body piece first, then its armor type (the heavy helmet before the better light hood).
	CHECK(PickOutfit(pieces, order) == P{ 0, 2, 3 });
	// Slots it wears are skipped; without a body piece to find, the rest still goes on when allowed.
	CHECK(PickOutfit(pieces, order, kHead, false) == P{ 0, 3 });
	CHECK(PickOutfit(pieces, order, kBody, false) == P{ 1, 3 });
	const std::vector<std::size_t> noBody{ 1, 2, 3 };
	CHECK(PickOutfit(pieces, noBody).empty());
	CHECK(PickOutfit(pieces, noBody, 0, false) == P{ 1, 3 });
	CHECK(PickOutfit(pieces, std::vector<std::size_t>{ 6 }, 0, false).empty());
}

TEST_CASE("Set coverage and search", "[armorsets]")
{
	const auto pieces = MixedPieces();
	const auto sets = BuildArmorSets(pieces);
	const auto& ebony = *Named(sets, "Ebony");

	const auto some = CoverageOf(ebony, pieces, { Sk(0x13961), Sk(0x13962), Sk(0x13965), Sk(0x99999) });
	CHECK(some.have == 3);
	CHECK(some.total == 4);
	CHECK(some.pieces == 3);
	CHECK_FALSE(some.Complete());
	CHECK(CoverageOf(ebony, pieces, { Sk(0x52794), Sk(0x13962), Sk(0x13963), Sk(0x13965) }).Complete());
	CHECK(CoverageOf(ebony, pieces, {}).pieces == 0);

	CHECK(ArmorSetMatches(ebony, pieces, ""));
	CHECK(ArmorSetMatches(ebony, pieces, "ebony"));
	CHECK(ArmorSetMatches(ebony, pieces, "SKYRIM eb"));
	CHECK(ArmorSetMatches(ebony, pieces, "gauntlets"));  // a piece's name
	CHECK_FALSE(ArmorSetMatches(ebony, pieces, "daedric"));
	CHECK_FALSE(ArmorSetMatches(ebony, pieces, "ebony valkyrie"));
	CHECK(ArmorSetMatches(*Named(sets, "Valkyrie"), pieces, "valkyrie.esp"));
}

TEST_CASE("A mannequin with a chosen set is open while it wears only that set", "[armorsets]")
{
	const auto                    all = MixedPieces();
	const std::vector<ArmorPiece> ebony(all.begin(), all.begin() + 5);
	CHECK(SetMannequinOpen(ebony, {}, 0));
	CHECK(SetMannequinOpen(ebony, std::vector{ Sk(0x13961) }, kBody));  // boots, gauntlets, helmet still fit
	CHECK_FALSE(SetMannequinOpen(ebony, std::vector{ Sk(0x13961), Sk(0x13962), Sk(0x13963), Sk(0x13965) }, kBody | kFeet | kHands | kHead));
	CHECK_FALSE(SetMannequinOpen(ebony, std::vector{ Sk(0x777) }, kHead));  // wears something else
}

TEST_CASE("Mannequins find modded sets by name", "[armorsets][display]")
{
	const std::vector<DisplayStock> stocks{
		OnPlayer(Armor(Mod("Mod.esp", 0x800), "Valkyrie Cuirass", kBody, 500)),  // 0
		OnPlayer(Armor(Mod("Mod.esp", 0x801), "Valkyrie Helmet", kHead, 200)),   // 1
		OnPlayer(Armor(Mod("Mod.esp", 0x802), "Raven Boots", kFeet, 900)),       // 2
		OnPlayer(Armor(Mod("Mod.esp", 0x803), "Valkyrie Boots", kFeet, 100)),    // 3
		OnPlayer(Armor(Mod("Mod.esp", 0x804), "Raven Cuirass", kBody, 100)),     // 4
	};
	const auto plan = PlanDisplays(std::vector{ Mannequin(0xC1) }, stocks, {});
	// No keywords: Valkyrie (three slot groups) beats the more valuable Raven pair.
	REQUIRE(plan.size() == 3);
	CHECK(plan[0] == DisplayAssignment{ 0, 0, 1 });
	CHECK(plan[1] == DisplayAssignment{ 0, 1, 1 });
	CHECK(plan[2] == DisplayAssignment{ 0, 3, 1 });
}

TEST_CASE("A chosen set takes only its pieces, before mannequins that pick their own", "[armorsets][display]")
{
	const std::vector<DisplayStock> stocks{
		OnPlayer(Armor(Sk(0x100), "Ebony Armor", kBody, 1000, { "ArmorMaterialEbony" })),       // 0
		OnPlayer(Armor(Sk(0x101), "Ebony Boots", kFeet, 300, { "ArmorMaterialEbony" })),        // 1
		OnPlayer(Armor(Sk(0x200), "Daedric Armor", kBody, 3000, { "ArmorMaterialDaedric" })),   // 2
		OnPlayer(Armor(Sk(0x201), "Daedric Boots", kFeet, 600, { "ArmorMaterialDaedric" })),    // 3
		OnPlayer(Armor(Sk(0x102), "Ebony Helmet", kHead, 400, { "ArmorMaterialEbony" })),       // 4
	};
	auto chosen = Mannequin(0xC2);
	chosen.chosen = true;
	chosen.set = { Sk(0x100), Sk(0x101), Sk(0x102), Sk(0x103) };
	const auto plan = PlanDisplays(std::vector{ Mannequin(0xC1), chosen }, stocks, {});
	// The chosen Ebony set goes first (Auto would have taken it: three slot groups), then Auto gets Daedric.
	REQUIRE(plan.size() == 5);
	CHECK(plan[0] == DisplayAssignment{ 1, 0, 1 });
	CHECK(plan[1] == DisplayAssignment{ 1, 4, 1 });
	CHECK(plan[2] == DisplayAssignment{ 1, 1, 1 });
	CHECK(plan[3] == DisplayAssignment{ 0, 2, 1 });
	CHECK(plan[4] == DisplayAssignment{ 0, 3, 1 });

	// With no piece of the set here, the mannequin stays empty: no other set instead.
	auto waiting = Mannequin(0xC3);
	waiting.chosen = true;
	waiting.set = { Sk(0x300) };
	CHECK(PlanDisplays(std::vector{ waiting }, stocks, {}).empty());
	// A set that is not loaded (no pieces known) gets nothing either.
	auto unknown = Mannequin(0xC4);
	unknown.chosen = true;
	CHECK(PlanDisplays(std::vector{ unknown }, stocks, {}).empty());
	// Categories still apply.
	auto light = chosen;
	light.categories = { "armor.light" };
	CHECK(PlanDisplays(std::vector{ light }, stocks, {}).empty());
}

TEST_CASE("A chosen set fills up as the pieces arrive, never over other armor", "[armorsets][display]")
{
	const std::vector<DisplayStock> stocks{
		OnPlayer(Armor(Sk(0x100), "Ebony Armor", kBody, 1000, { "ArmorMaterialEbony" })),  // 0 another copy of what it wears
		OnPlayer(Armor(Sk(0x101), "Ebony Boots", kFeet, 300, { "ArmorMaterialEbony" })),   // 1
		OnPlayer(Armor(Sk(0x102), "Ebony Helmet", kHead, 400, { "ArmorMaterialEbony" })),  // 2
	};
	auto partly = Mannequin(0xC1);
	partly.chosen = true;
	partly.set = { Sk(0x100), Sk(0x101), Sk(0x102) };
	partly.full = true;
	partly.holds = { Sk(0x100) };
	partly.worn = kBody;
	const auto plan = PlanDisplays(std::vector{ partly }, stocks, { Sk(0x100) });
	REQUIRE(plan.size() == 2);
	CHECK(plan[0] == DisplayAssignment{ 0, 2, 1 });
	CHECK(plan[1] == DisplayAssignment{ 0, 1, 1 });

	// Wearing armor of another set: left alone (Dova Stash never takes anything off).
	auto other = partly;
	other.holds = { Sk(0x900) };
	other.worn = kHead;
	CHECK(PlanDisplays(std::vector{ other }, stocks, { Sk(0x900) }).empty());
	// Full, and what it wears is unknown: left alone.
	auto unknown = partly;
	unknown.holds.clear();
	CHECK(PlanDisplays(std::vector{ unknown }, stocks, {}).empty());
}

TEST_CASE("Display entries keep their armor set", "[armorsets][layout]")
{
	constexpr std::string_view kText = R"({
		"$schema": "dovastash-layout-1",
		"cell": "Skyrim.esm|0x000165A7",
		"containers": [],
		"displays": [
			{"ref": "Skyrim.esm|0x000EEEEE", "label": "Mannequin", "set": "Skyrim.esm|0x00013961"},
			{"ref": "Skyrim.esm|0x000EEEEF", "set": "not a form"}
		]
	})";
	Diagnostics diags;
	const auto  layout = ParseLayoutText(kText, "set.json", diags);
	REQUIRE(layout);
	CHECK(diags.HasErrors());  // the malformed set
	REQUIRE(layout->displays.size() == 2);
	CHECK(layout->displays[0].set == Sk(0x13961));
	CHECK_FALSE(layout->displays[1].set);

	const auto json = LayoutToJson(*layout);
	CHECK(json["displays"][0]["set"] == "Skyrim.esm|0x00013961");
	CHECK(json["displays"][1]["set"] == "not a form");  // kept verbatim

	const auto  text = SerializeLayout(*layout);
	Diagnostics again;
	const auto  reparsed = ParseLayoutText(text, "again.json", again);
	REQUIRE(reparsed);
	CHECK(reparsed->displays[0].set == Sk(0x13961));
	CHECK(SerializeLayout(*reparsed) == text);
}

TEST_CASE("Templates and armor sets", "[armorsets][template]")
{
	Layout tmpl;
	tmpl.cell = Sk(0x165A7);
	tmpl.requiredPlugins = { "Skyrim.esm" };
	DisplayBinding a;
	a.ref = Sk(0xA01);
	DisplayBinding b;
	b.ref = Sk(0xA02);
	b.set = Mod("Valkyrie.esp", 0x810);
	tmpl.displays = { a, b };

	// A set is a soft reference (TemplateDisplays.h): its plugin need not be required, and is not reported.
	Diagnostics diags;
	ValidateTemplate(tmpl, "T.json", diags);
	CHECK(std::ranges::none_of(diags.Items(), [](const Diagnostic& d) { return d.message.find("Valkyrie.esp") != std::string::npos; }));
	CHECK(ReferencedPlugins(tmpl) == std::vector<std::string>{ "Skyrim.esm" });

	// Applying it keeps the player's set where the template names none; the template's wins otherwise.
	Layout player;
	player.cell = Sk(0x165A7);
	DisplayBinding pa;
	pa.ref = Sk(0xA01);
	pa.set = Sk(0x13961);
	DisplayBinding pb;
	pb.ref = Sk(0xA02);
	pb.set = Sk(0x13961);
	player.displays = { pa, pb };
	const auto merged = ApplyTemplate(TemplateCandidate{ "Templates/T.json", tmpl }, &player);
	REQUIRE(merged.displays.size() == 2);
	CHECK(merged.displays[0].set == Sk(0x13961));
	CHECK(merged.displays[1].set == Mod("Valkyrie.esp", 0x810));
}
