#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "TestHelpers.h"

using namespace dova::core;
using namespace dova::test;

namespace
{
	bool HasMessage(const Diagnostics& a_diags, Diagnostic::Severity a_severity, std::string_view a_needle)
	{
		for (const auto& d : a_diags.Items()) {
			if (d.severity == a_severity && d.message.find(a_needle) != std::string::npos) {
				return true;
			}
		}
		return false;
	}
}

TEST_CASE("Category patterns", "[categories][patterns]")
{
	CHECK(CategorySet::Matches("*", "weapons.swords"));
	CHECK(CategorySet::Matches("*", "keys"));
	CHECK(CategorySet::Matches("weapons.*", "weapons.swords"));
	CHECK(CategorySet::Matches("weapons.*", "weapons.swords.ebony"));
	CHECK(CategorySet::Matches("weapons.*", "weapons"));
	CHECK_FALSE(CategorySet::Matches("weapons.*", "weaponsmith.tools"));
	CHECK_FALSE(CategorySet::Matches("weapons.*", "armor.heavy"));
	CHECK(CategorySet::Matches("weapons.swords", "weapons.swords"));
	CHECK_FALSE(CategorySet::Matches("weapons", "weapons.swords"));
	CHECK_FALSE(CategorySet::Matches("weapons.swords", "weapons"));
	CHECK(CategorySet::Matches("Weapons.*", "weapons.SWORDS"));
	CHECK(CategorySet::Matches("weapons.swords.*", "weapons.swords"));
	CHECK_FALSE(CategorySet::Matches("weapons.swords.*", "weapons.bows"));

	const auto bad = GENERATE(as<std::string>{}, "", ".*", "*.swords", "weap*", "weapons.*.x", "a..b", ".a", "a.", "a b", "**", "weapons.**");
	INFO(bad);
	CHECK_FALSE(IsValidCategoryPattern(bad));
	CHECK_FALSE(CategorySet::Matches(bad, "weapons.swords"));
	CHECK(CategoryPatternSpecificity(bad) == -1);
}

TEST_CASE("Category pattern specificity", "[categories][patterns]")
{
	CHECK(CategoryPatternSpecificity("*") == 0);
	CHECK(CategoryPatternSpecificity("weapons.*") == 1);
	CHECK(CategoryPatternSpecificity("weapons.swords.*") == 2);
	CHECK(CategoryPatternSpecificity("weapons") > CategoryPatternSpecificity("weapons.swords.*"));
	CHECK(CategoryPatternSpecificity("weapons.swords") > CategoryPatternSpecificity("weapons"));
	CHECK(CategoryDepth("a.b.c") == 3);
	CHECK(IsValidCategoryId("misc.ores_ingots"));
	CHECK(IsValidCategoryId("misc.hides-pelts2"));
	CHECK_FALSE(IsValidCategoryId("misc.*"));
}

TEST_CASE("DESIGN 7.1 example parses and classifies", "[categories]")
{
	Diagnostics diags;
	const auto  set = CompileCategories(R"({
		// comments are tolerated
		"$schema": "dovastash-categories-1",
		"categories": [
			{ "id": "misc.gems", "name": "Gems", "priority": 100, "match": { "formTypes": ["MISC"], "keywordsAny": ["VendorItemGem"] } },
			{ "id": "weapons.bows", "name": "Bows", "match": { "formTypes": ["WEAP"], "weaponTypes": ["Bow"] } },
			{ "id": "books.skill", "name": "Skill books", "match": { "formTypes": ["BOOK"], "bookTeaches": "skill" } }
		]
	})",
		diags);
	CHECK(diags.Empty());
	REQUIRE(set.Size() == 3);
	CHECK(set.Find("misc.gems")->name == "Gems");
	CHECK(set.Find("MISC.GEMS") != nullptr);

	CHECK(set.Classify(Misc(Sk(0x63B42), 1, { "vendoritemgem" })) == "misc.gems");  // keyword case-insensitive
	CHECK_FALSE(set.Classify(Misc(Sk(0x1), 1, { "VendorItemClutter" })).has_value());
	CHECK(set.Classify(Weapon(Sk(0x2), WeaponType::Bow)) == "weapons.bows");
	CHECK_FALSE(set.Classify(Weapon(Sk(0x3), WeaponType::OneHandSword)).has_value());

	auto book = MakeItem(Sk(0x4), ItemType::Book);
	book.bookTeaches = BookTeaches::Skill;
	CHECK(set.Classify(book) == "books.skill");
	book.bookTeaches = BookTeaches::None;
	CHECK_FALSE(set.Classify(book).has_value());
	book.bookTeaches.reset();  // unknown -> does not satisfy an enum predicate
	CHECK_FALSE(set.Classify(book).has_value());
}

TEST_CASE("Every predicate in the vocabulary", "[categories]")
{
	Diagnostics diags;
	const auto  set = CompileCategories(R"({
		"$schema": "dovastash-categories-1",
		"categories": [
			{ "id": "t.kwall",  "match": { "keywordsAll": ["A", "B"] } },
			{ "id": "t.kwnone", "match": { "formTypes": "INGR", "keywordsNone": ["Bad"] } },
			{ "id": "t.forms",  "match": { "forms": ["Skyrim.esm|0x0000AAAA", "Dawnguard.esm|0x00000BBB"] } },
			{ "id": "t.plugin", "match": { "plugins": ["dragonborn.esm"] } },
			{ "id": "t.armor",  "match": { "armorTypes": ["light", "Clothing"] } },
			{ "id": "t.alch",   "match": { "alchemy": "food" } },
			{ "id": "t.gem",    "match": { "soulGem": ["Filled"] } },
			{ "id": "t.ench",   "match": { "formTypes": ["WEAP"], "enchanted": true } },
			{ "id": "t.ammo",   "match": { "ammo": "Bolt" } },
			{ "id": "t.types",  "match": { "formTypes": ["SCRL", "KEYM", "LIGH"] } }
		]
	})",
		diags);
	INFO((diags.Empty() ? std::string() : diags.Items().front().ToString()));
	REQUIRE(diags.Empty());

	auto kw = MakeItem(Sk(0x10), ItemType::Misc);
	kw.keywords = { "a", "B", "c" };
	CHECK(set.Classify(kw) == "t.kwall");
	kw.keywords = { "A" };
	CHECK_FALSE(set.Classify(kw).has_value());

	auto ingr = MakeItem(Sk(0x11), ItemType::Ingredient);
	CHECK(set.Classify(ingr) == "t.kwnone");
	ingr.keywords = { "BAD" };
	CHECK_FALSE(set.Classify(ingr).has_value());

	CHECK(set.Classify(MakeItem(F("skyrim.ESM|aaaa"), ItemType::Other)) == "t.forms");
	CHECK(set.Classify(MakeItem(F("Dawnguard.esm|0xBBB"), ItemType::Other)) == "t.forms");
	CHECK_FALSE(set.Classify(MakeItem(F("Dawnguard.esm|0xAAAA"), ItemType::Other)).has_value());

	CHECK(set.Classify(MakeItem(F("Dragonborn.esm|0x123"), ItemType::Other)) == "t.plugin");

	auto armor = MakeItem(Sk(0x12), ItemType::Armor);
	armor.armorType = ArmorType::Clothing;
	CHECK(set.Classify(armor) == "t.armor");
	armor.armorType = ArmorType::Heavy;
	CHECK_FALSE(set.Classify(armor).has_value());

	auto food = MakeItem(Sk(0x13), ItemType::Alchemy);
	food.alchemyKind = AlchemyKind::Food;
	CHECK(set.Classify(food) == "t.alch");

	auto gem = MakeItem(Sk(0x14), ItemType::SoulGem);
	gem.soulGemState = SoulGemState::Filled;
	CHECK(set.Classify(gem) == "t.gem");
	gem.soulGemState = SoulGemState::Empty;
	CHECK_FALSE(set.Classify(gem).has_value());

	auto sword = Weapon(Sk(0x15), WeaponType::OneHandSword);
	CHECK_FALSE(set.Classify(sword).has_value());
	sword.enchanted = true;
	CHECK(set.Classify(sword) == "t.ench");

	auto bolt = MakeItem(Sk(0x16), ItemType::Ammo);
	bolt.ammoKind = AmmoKind::Bolt;
	CHECK(set.Classify(bolt) == "t.ammo");

	CHECK(set.Classify(MakeItem(Sk(0x17), ItemType::Light)) == "t.types");
	CHECK(set.Classify(MakeItem(Sk(0x18), ItemType::Key)) == "t.types");
	CHECK(set.ItemMatches("t.types", MakeItem(Sk(0x18), ItemType::Scroll)));
	CHECK_FALSE(set.ItemMatches("t.unknown", MakeItem(Sk(0x18), ItemType::Scroll)));
}

TEST_CASE("formLists are resolved through the callback", "[categories]")
{
	const auto list = F("Skyrim.esm|0x000A0000");
	const auto missing = F("Missing.esp|0x00000800");
	int        calls = 0;
	const FormListResolver resolver = [&](const FormRef& a_list) -> std::vector<FormRef> {
		++calls;
		if (a_list == list) {
			return { Sk(0x1), Sk(0x2) };
		}
		return {};
	};

	Diagnostics diags;
	const auto  set = CompileCategories(R"({
		"$schema": "dovastash-categories-1",
		"categories": [
			{ "id": "lists.ok", "match": { "formLists": ["Skyrim.esm|0x000A0000"], "forms": ["Skyrim.esm|0x00000009"] } },
			{ "id": "lists.missing", "match": { "formLists": "Missing.esp|0x800" } }
		]
	})",
		diags, resolver);

	CHECK(calls == 2);
	CHECK_FALSE(diags.HasErrors());
	CHECK(HasMessage(diags, Diagnostic::Severity::Warning, missing.ToString()));
	CHECK(set.Classify(MakeItem(Sk(0x1), ItemType::Misc)) == "lists.ok");
	CHECK(set.Classify(MakeItem(Sk(0x2), ItemType::Misc)) == "lists.ok");
	CHECK(set.Classify(MakeItem(Sk(0x9), ItemType::Misc)) == "lists.ok");  // forms ∪ formLists
	CHECK_FALSE(set.Classify(MakeItem(Sk(0x3), ItemType::Misc)).has_value());  // unresolved list fails closed

	// No resolver at all: warning, nothing matches.
	Diagnostics diags2;
	const auto  set2 = CompileCategories(R"({ "$schema": "dovastash-categories-1",
		"categories": [ { "id": "x", "match": { "formLists": ["Skyrim.esm|0xA0000"] } } ] })",
		diags2);
	CHECK(diags2.WarningCount() == 1);
	CHECK_FALSE(diags2.HasErrors());
	CHECK_FALSE(set2.Classify(MakeItem(Sk(0x1), ItemType::Misc)).has_value());
}

TEST_CASE("Empty predicate lists fail closed; group nodes match nothing", "[categories]")
{
	Diagnostics diags;
	const auto  set = CompileCategories(R"({
		"$schema": "dovastash-categories-1",
		"categories": [
			{ "id": "group", "name": "Just a group" },
			{ "id": "group2", "match": {} },
			{ "id": "empty.types", "match": { "formTypes": [] } },
			{ "id": "empty.kw", "match": { "keywordsAny": [] } },
			{ "id": "empty.forms", "match": { "forms": ["not a form ref"] } },
			{ "id": "vacuous", "priority": -1, "match": { "keywordsAll": [], "keywordsNone": [] } }
		]
	})",
		diags);
	CHECK_FALSE(diags.HasErrors());
	CHECK(diags.WarningCount() >= 3);
	CHECK(set.Size() == 6);
	// Only the vacuous category (no constraints except empty All/None) matches.
	CHECK(set.MatchAll(MakeItem(Sk(0x1), ItemType::Misc)) == std::vector<std::string>{ "vacuous" });
}

TEST_CASE("Category errors skip the category, not the file", "[categories]")
{
	Diagnostics diags;
	const auto  defs = ParseCategoryText(R"({
		"$schema": "dovastash-categories-1",
		"categories": [
			{ "id": "ok.one", "match": { "formTypes": ["MISC"] } },
			{ "id": "bad.predicate", "match": { "keywordAny": ["Typo"] } },
			{ "id": "bad.enum", "match": { "weaponTypes": ["Sword"] } },
			{ "id": "bad.type", "match": { "formTypes": ["WEAPON"] } },
			{ "id": "bad id with spaces", "match": {} },
			{ "id": "bad.*", "match": {} },
			{ "name": "no id" },
			{ "id": "bad.priority", "priority": "high" },
			{ "id": "bad.enchanted", "match": { "enchanted": "yes" } },
			42,
			{ "id": "ok.two", "description": "fine", "_comment": "fine", "match": { "formTypes": "KEYM" } },
			{ "id": "ok.three", "colour": "blue" }
		]
	})",
		"cats.json", diags);
	REQUIRE(defs.size() == 3);
	CHECK(defs[0].id == "ok.one");
	CHECK(defs[1].id == "ok.two");
	CHECK(defs[2].id == "ok.three");
	CHECK(HasMessage(diags, Diagnostic::Severity::Error, "unknown predicate \"keywordAny\""));
	CHECK(HasMessage(diags, Diagnostic::Severity::Error, "unknown value \"Sword\""));
	CHECK(HasMessage(diags, Diagnostic::Severity::Error, "unknown value \"WEAPON\""));
	CHECK(HasMessage(diags, Diagnostic::Severity::Warning, "unknown field \"colour\""));
	for (const auto& d : diags.Items()) {
		CHECK(d.source == "cats.json");
	}
}

TEST_CASE("Category document-level errors", "[categories]")
{
	SECTION("invalid JSON")
	{
		Diagnostics diags;
		CHECK(ParseCategoryText("{ not json", "x.json", diags).empty());
		CHECK(diags.HasErrors());
	}
	SECTION("wrong schema")
	{
		Diagnostics diags;
		CHECK(ParseCategoryText(R"({ "$schema": "dovastash-layout-1", "categories": [ { "id": "a" } ] })", "x.json", diags).empty());
		CHECK(diags.HasErrors());
	}
	SECTION("missing schema is only a warning")
	{
		Diagnostics diags;
		CHECK(ParseCategoryText(R"({ "categories": [ { "id": "a" } ] })", "x.json", diags).size() == 1);
		CHECK_FALSE(diags.HasErrors());
		CHECK(diags.WarningCount() == 1);
	}
	SECTION("missing categories array")
	{
		Diagnostics diags;
		CHECK(ParseCategoryText(R"({ "$schema": "dovastash-categories-1" })", "x.json", diags).empty());
		CHECK(diags.HasErrors());
	}
	SECTION("ids are lower-cased")
	{
		Diagnostics diags;
		const auto  defs = ParseCategoryText(R"({ "categories": [ { "id": "Misc.Gems" } ] })", "x.json", diags);
		REQUIRE(defs.size() == 1);
		CHECK(defs[0].id == "misc.gems");
		CHECK(defs[0].name == "misc.gems");
	}
}

TEST_CASE("Ordering: priority, then specificity, then definition order", "[categories]")
{
	const auto set = CompileCategories(R"({
		"$schema": "dovastash-categories-1",
		"categories": [
			{ "id": "weapons", "match": { "formTypes": ["WEAP"] } },
			{ "id": "weapons.blades", "match": { "formTypes": ["WEAP"], "weaponTypes": ["OneHandSword", "OneHandDagger"] } },
			{ "id": "weapons.swords", "match": { "formTypes": ["WEAP"], "weaponTypes": ["OneHandSword"] } },
			{ "id": "weapons.swords.fancy", "match": { "formTypes": ["WEAP"], "enchanted": true } },
			{ "id": "special", "priority": 10, "match": { "forms": ["Skyrim.esm|0x00000777"] } }
		]
	})");

	auto sword = Weapon(Sk(0x1), WeaponType::OneHandSword);
	// Same priority & depth: blades defined before swords wins.
	CHECK(set.Classify(sword) == "weapons.blades");
	CHECK(set.MatchAll(sword) == std::vector<std::string>{ "weapons.blades", "weapons.swords", "weapons" });

	// Deeper id wins over shallower ones regardless of definition order.
	sword.enchanted = true;
	CHECK(set.Classify(sword) == "weapons.swords.fancy");

	// Higher priority beats depth.
	auto special = Weapon(Sk(0x777), WeaponType::OneHandSword);
	special.enchanted = true;
	CHECK(set.Classify(special) == "special");

	CHECK(set.Classify(Weapon(Sk(0x2), WeaponType::Bow)) == "weapons");

	// Categories() keeps definition order.
	REQUIRE(set.Categories().size() == 5);
	CHECK(set.Categories()[0].id == "weapons");
	CHECK(set.Categories()[4].id == "special");
	CHECK(set.AnyMatches("weapons.swords.*"));
	CHECK_FALSE(set.AnyMatches("armor.*"));
}

TEST_CASE("Priorities outside the 32-bit range are clamped, never wrapped", "[categories]")
{
	Diagnostics diags;
	const auto  defs = ParseCategoryText(R"({
		"$schema": "dovastash-categories-1",
		"categories": [
			{ "id": "huge",     "priority": 18446744073709551615, "match": { "formTypes": ["MISC"] } },
			{ "id": "big",      "priority": 9223372036854775807,  "match": { "formTypes": ["MISC"] } },
			{ "id": "tiny",     "priority": -9223372036854775808, "match": { "formTypes": ["MISC"] } },
			{ "id": "fraction", "priority": 1.5,                  "match": { "formTypes": ["MISC"] } }
		]
	})",
		"prio.json", diags);
	REQUIRE(defs.size() == 3);
	CHECK(defs[0].priority == INT32_MAX);  // a uint64 above INT64_MAX must not wrap to -1
	CHECK(defs[1].priority == INT32_MAX);
	CHECK(defs[2].priority == INT32_MIN);
	CHECK(HasMessage(diags, Diagnostic::Severity::Error, "priority: expected an integer"));
}

TEST_CASE("Later files override categories by id, in place", "[categories]")
{
	Diagnostics diags;
	const auto  base = ParseCategoryText(R"({ "$schema": "dovastash-categories-1", "categories": [
		{ "id": "a", "match": { "formTypes": ["MISC"] } },
		{ "id": "b", "match": { "formTypes": ["MISC"] } }
	] })",
		"Categories/00_Default.json", diags);
	const auto pack = ParseCategoryText(R"({ "$schema": "dovastash-categories-1", "categories": [
		{ "id": "A", "name": "Overridden", "match": { "formTypes": ["KEYM"] } },
		{ "id": "c", "match": { "formTypes": ["MISC"] } }
	] })",
		"Categories/50_Pack.json", diags);
	REQUIRE_FALSE(diags.HasErrors());

	const std::vector<std::vector<CategoryDef>> files{ base, pack };
	auto merged = MergeCategoryFiles(files, diags);
	REQUIRE(merged.size() == 3);
	CHECK(merged[0].id == "a");
	CHECK(merged[0].name == "Overridden");
	CHECK(merged[0].source == "Categories/50_Pack.json");
	CHECK(merged[1].id == "b");
	CHECK(merged[2].id == "c");
	CHECK(HasMessage(diags, Diagnostic::Severity::Warning, "overrides"));

	const auto set = CategorySet::Compile(merged, {}, diags);
	// "a" now only matches keys, so a MISC item falls through to "b" (next in definition order).
	CHECK(set.Classify(MakeItem(Sk(0x1), ItemType::Misc)) == "b");
	CHECK(set.Classify(MakeItem(Sk(0x1), ItemType::Key)) == "a");
}

TEST_CASE("LoadCategoryDirectory merges *.json in filename order", "[categories][files]")
{
	TempDir dir;
	dir.Write("50_pack.json", R"({ "$schema": "dovastash-categories-1", "categories": [
		{ "id": "misc.gems", "name": "Pack gems", "match": { "formTypes": ["MISC"] } } ] })");
	dir.Write("00_Default.json", R"({ "$schema": "dovastash-categories-1", "categories": [
		{ "id": "misc.gems", "name": "Gems", "match": { "formTypes": ["MISC"], "keywordsAny": ["VendorItemGem"] } },
		{ "id": "keys", "match": { "formTypes": ["KEYM"] } } ] })");
	dir.Write("10_broken.json", "{ nope");
	dir.Write("readme.txt", "not json");
	dir.Write("sub/99_ignored.json", R"({ "categories": [ { "id": "sub" } ] })");

	Diagnostics diags;
	const auto  defs = LoadCategoryDirectory(dir.Path(), diags);
	REQUIRE(defs.size() == 2);
	CHECK(defs[0].id == "misc.gems");
	CHECK(defs[0].name == "Pack gems");
	CHECK(defs[1].id == "keys");
	CHECK(diags.ErrorCount() == 1);
	CHECK(HasMessage(diags, Diagnostic::Severity::Error, "invalid JSON"));

	Diagnostics missing;
	CHECK(LoadCategoryDirectory(dir.Path() / "nope", missing).empty());
	CHECK_FALSE(missing.HasErrors());
}
