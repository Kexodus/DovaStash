#include <compare>
#include <map>
#include <set>
#include <unordered_set>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "TestHelpers.h"

using namespace dova::core;
using dova::test::F;

TEST_CASE("FormRef parses the canonical form", "[formref]")
{
	const auto ref = FormRef::Parse("Skyrim.esm|0x000165A7");
	REQUIRE(ref);
	CHECK(ref->plugin == "Skyrim.esm");
	CHECK(ref->localId == 0x000165A7u);
	CHECK(ref->IsValid());
	CHECK(ref->ToString() == "Skyrim.esm|0x000165A7");
}

TEST_CASE("FormRef tolerates whitespace, missing 0x and lower-case hex", "[formref]")
{
	const auto text = GENERATE(as<std::string>{},
		"Skyrim.esm|165a7",
		"Skyrim.esm|0x165A7",
		"Skyrim.esm|0X000165a7",
		"  Skyrim.esm | 0x000165A7  ",
		"\tSkyrim.esm|\t000165A7\n");
	INFO(text);
	const auto ref = FormRef::Parse(text);
	REQUIRE(ref);
	CHECK(ref->plugin == "Skyrim.esm");
	CHECK(ref->localId == 0x165A7u);
	CHECK(ref->ToString() == "Skyrim.esm|0x000165A7");
}

TEST_CASE("FormRef preserves plugin spelling and accepts esp/esl", "[formref]")
{
	CHECK(F("Dawnguard.ESM|0x1").ToString() == "Dawnguard.ESM|0x00000001");
	CHECK(F("My Mod - Homes.esp|0xD62").plugin == "My Mod - Homes.esp");
	CHECK(F("ccBGSSSE001-Fish.esl|0x800").localId == 0x800u);
	CHECK(F("Skyrim.esm|0x00FFFFFF").localId == 0x00FFFFFFu);
	CHECK(F("Skyrim.esm|0").localId == 0u);
}

TEST_CASE("FormRef rejects garbage", "[formref]")
{
	const auto text = GENERATE(as<std::string>{},
		"",
		"Skyrim.esm",
		"Skyrim.esm|",
		"Skyrim.esm|0x",
		"|0x000165A7",
		"   |0x1",
		"Skyrim|0x1",
		".esm|0x1",
		"Skyrim.txt|0x1",
		"Skyrim.esm|0xGG",
		"Skyrim.esm|0x123456789",
		"Skyrim.esm|0x01000000",  // looks like a full runtime id with a load-order byte
		"Skyrim.esm|0xFFFFFFFF",
		"Skyrim.esm|-1",
		"Skyrim.esm|0x1 2",
		"Skyrim.esm||0x1",
		"Skyrim.esm|0x1|0x2",
		"Data/Skyrim.esm|0x1",
		"C:\\Skyrim.esm|0x1",
		"Skyr\x01im.esm|0x1",
		"VendorItemGem");
	INFO(text);
	CHECK_FALSE(FormRef::Parse(text).has_value());
}

TEST_CASE("FormRef equality, hashing and ordering ignore plugin case", "[formref]")
{
	const auto a = F("Skyrim.esm|0x0001397E");
	const auto b = F("SKYRIM.ESM|1397e");
	const auto c = F("Skyrim.esm|0x0001397F");
	const auto d = F("Dawnguard.esm|0x0001397E");

	CHECK(a == b);
	CHECK(a != c);
	CHECK(a != d);
	CHECK(std::hash<FormRef>{}(a) == std::hash<FormRef>{}(b));
	CHECK(FormRefHash{}(a) == FormRefHash{}(b));
	CHECK(std::is_eq(a <=> b));
	CHECK(a < c);
	CHECK(d < a);  // "dawnguard" < "skyrim"
	CHECK(F("a.esp|0x1") < F("B.esp|0x0"));

	std::unordered_set<FormRef> set{ a, b, c, d };
	CHECK(set.size() == 3);

	std::set<FormRef> ordered{ c, a, d, b };
	REQUIRE(ordered.size() == 3);
	CHECK(*ordered.begin() == d);

	std::map<FormRef, int> m;
	m[a] = 1;
	m[b] = 2;
	CHECK(m.size() == 1);
	CHECK(m.at(a) == 2);
}

TEST_CASE("Default FormRef is invalid", "[formref]")
{
	FormRef ref;
	CHECK_FALSE(ref.IsValid());
	CHECK(ref.ToString() == "|0x00000000");
}

TEST_CASE("Case-insensitive helpers", "[formref]")
{
	CHECK(IEquals("Skyrim.ESM", "skyrim.esm"));
	CHECK_FALSE(IEquals("Skyrim.esm", "Skyrim.es"));
	CHECK(ICompare("abc", "ABD") < 0);
	CHECK(ICompare("ABC", "abc") == 0);
	CHECK(ICompare("ab", "abc") < 0);
	CHECK(ToLower("VendorItemGem") == "vendoritemgem");
}

TEST_CASE("SemVer parsing and ordering", "[version]")
{
	CHECK(SemVer::Parse("1.0.0").has_value());
	CHECK(SemVer::Parse("1.2")->ToString() == "1.2.0");
	CHECK(SemVer::Parse("v2")->ToString() == "2.0.0");
	CHECK(SemVer::Parse("1.0.0-beta.2+build.5")->ToString() == "1.0.0-beta.2");
	CHECK_FALSE(SemVer::Parse("").has_value());
	CHECK_FALSE(SemVer::Parse("abc").has_value());
	CHECK_FALSE(SemVer::Parse("1.0.0.0").has_value());
	CHECK_FALSE(SemVer::Parse("1..0").has_value());
	CHECK_FALSE(SemVer::Parse("1.0.0-").has_value());

	const auto v = [](std::string_view a_text) { return *SemVer::Parse(a_text); };
	CHECK(v("1.10.0") > v("1.9.0"));
	CHECK(v("2.0.0") > v("1.99.99"));
	CHECK(v("1.0.0") > v("1.0.0-rc.1"));
	CHECK(v("1.0.0-beta.11") > v("1.0.0-beta.2"));
	CHECK(v("1.0.0-beta") > v("1.0.0-alpha"));
	CHECK(v("1.0.0-alpha.beta") > v("1.0.0-alpha.1"));
	CHECK(v("1.0.0-alpha.1") > v("1.0.0-alpha"));
	CHECK(v("1.0") == v("1.0.0"));
	CHECK(v("1.0.0+a") == v("1.0.0+b"));

	CHECK(std::is_gt(CompareVersionStrings("1.0.0", "garbage")));
	CHECK(std::is_lt(CompareVersionStrings("", "0.0.1")));
	CHECK(std::is_eq(CompareVersionStrings("x", "y")));
}

TEST_CASE("Enum spellings round-trip", "[enums]")
{
	CHECK(ParseItemType("weap") == ItemType::Weapon);
	CHECK(ParseItemType("LIGH") == ItemType::Light);
	CHECK_FALSE(ParseItemType("Weapon").has_value());
	CHECK_FALSE(ParseItemType("Other").has_value());
	CHECK(ParseWeaponType("onehandsword") == WeaponType::OneHandSword);
	CHECK(ParseArmorType("HEAVY") == ArmorType::Heavy);
	CHECK(ParseBookTeaches("skill") == BookTeaches::Skill);
	CHECK(ParseAlchemyKind("Poison") == AlchemyKind::Poison);
	CHECK(ParseSoulGemState("filled") == SoulGemState::Filled);
	CHECK(ParseAmmoKind("bolt") == AmmoKind::Bolt);
	CHECK(ToString(ItemType::Scroll) == "SCRL");
	CHECK(ToString(ItemType::Other).empty());
	CHECK(ToString(WeaponType::TwoHandAxe) == "TwoHandAxe");
	CHECK(ParseWeaponType(ToString(WeaponType::Crossbow)) == WeaponType::Crossbow);
}
