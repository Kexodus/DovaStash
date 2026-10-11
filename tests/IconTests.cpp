#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "TestHelpers.h"

using namespace dova::core;
using namespace dova::test;

namespace
{
	constexpr std::uint32_t kNoColor = 0xFFFFFFFF;

	std::string Label(const ItemInfo& a_item) { return SkyUIDefaultIcon(a_item).label; }
	std::uint32_t Color(const IconRef& a_icon) { return a_icon.color.value_or(kNoColor); }

	ItemInfo Armor(ArmorType a_type, std::uint32_t a_slots, std::initializer_list<const char*> a_keywords = {})
	{
		auto item = MakeItem(Sk(0x12E4D), ItemType::Armor);
		item.armorType = a_type;
		item.armorSlots = a_slots;
		for (const auto* k : a_keywords) {
			item.keywords.emplace_back(k);
		}
		return item;
	}

	ItemInfo Book(BookTeaches a_teaches = BookTeaches::None, std::initializer_list<const char*> a_keywords = {})
	{
		auto item = MakeItem(Sk(0x1AFD8), ItemType::Book);
		item.bookTeaches = a_teaches;
		for (const auto* k : a_keywords) {
			item.keywords.emplace_back(k);
		}
		return item;
	}

	ItemInfo Gem(std::uint8_t a_size, std::uint8_t a_soul, FormRef a_form = Sk(0x2E4E2))
	{
		auto item = MakeItem(std::move(a_form), ItemType::SoulGem);
		item.gemSize = a_size;
		item.soulSize = a_soul;
		item.soulGemState = a_soul ? SoulGemState::Filled : SoulGemState::Empty;
		return item;
	}

	ItemInfo PotionAV(std::int32_t a_actorValue)
	{
		auto item = Potion(Sk(0x3EADD), 1, 10);
		item.actorValue = a_actorValue;
		return item;
	}

	bool HasMessage(const Diagnostics& a_diags, std::string_view a_needle)
	{
		for (const auto& d : a_diags.Items()) {
			if (d.message.find(a_needle) != std::string::npos) {
				return true;
			}
		}
		return false;
	}

	IconRules Rules(std::string_view a_text, Diagnostics& a_diags, std::string_view a_source = "Test.json")
	{
		IconRules rules;
		rules.AddFileText(a_text, a_source, a_diags);
		return rules;
	}

	IconRules Rules(std::string_view a_text)
	{
		Diagnostics diags;
		auto        rules = Rules(a_text, diags);
		INFO((diags.Empty() ? std::string() : diags.Items().front().ToString()));
		REQUIRE(diags.Empty());
		return rules;
	}

	// Excerpt of Inventory Interface Information Injector's own Skyrim.json / Dragonborn.json.
	constexpr std::string_view kI4Skyrim = R"({
		"$schema": "https://raw.githubusercontent.com/Exit-9B/InventoryInjector/main/docs/InventoryInjector.schema.json",
		"rules": [
			{ "match": { "formType": "MiscItem", "keywords": "VendorItemFireword" }, "assign": { "subType": "Firewood" } },
			{ "match": { "formType": "MiscItem", "formId": { "anyOf": [ "Skyrim.esm|1994F", "Skyrim.esm|59654" ] } },
			  "assign": { "subType": "Gem" } },
			{ "match": { "formType": "Spell", "resistance": "FireResist" }, "assign": { "iconLabel": "magic_fire", "iconColor": "#C73636" } },
			{ "match": { "formType": "Armor", "weightClass": { "anyOf": [ "Jewelry", "Clothing", "Other" ] } },
			  "assign": { "iconColor": "#EDDA87" } },
			{ "match": { "formType": "MiscItem", "formId": "Dragonborn.esm|247F9" }, "assign": { "iconLabel": "misc_trollskull" } },
			{ "match": { "formType": "Weapon", "formId": { "anyOf": [ "Dragonborn.esm|179C9" ] } }, "assign": { "subType": "pickaxe" } }
		]
	})";
}

// --- SkyUI defaults --------------------------------------------------------------------------------

TEST_CASE("SkyUI default icons: weapons", "[icons][skyui]")
{
	CHECK(Label(Weapon(Sk(0x12EB7), WeaponType::OneHandSword)) == "weapon_sword");
	CHECK(Label(Weapon(Sk(0x1), WeaponType::OneHandDagger)) == "weapon_dagger");
	CHECK(Label(Weapon(Sk(0x1), WeaponType::OneHandAxe)) == "weapon_waraxe");
	CHECK(Label(Weapon(Sk(0x1), WeaponType::OneHandMace)) == "weapon_mace");
	CHECK(Label(Weapon(Sk(0x1), WeaponType::TwoHandSword)) == "weapon_greatsword");
	CHECK(Label(Weapon(Sk(0x1), WeaponType::TwoHandAxe)) == "weapon_battleaxe");
	CHECK(Label(Weapon(Sk(0x1), WeaponType::Bow)) == "weapon_bow");
	CHECK(Label(Weapon(Sk(0x1), WeaponType::Staff)) == "weapon_staff");
	CHECK(Label(Weapon(Sk(0x1), WeaponType::Crossbow)) == "weapon_crossbow");
	CHECK(Label(Weapon(Sk(0x1), WeaponType::HandToHand)) == "default_weapon");

	auto hammer = Weapon(Sk(0x13982), WeaponType::TwoHandAxe);
	hammer.keywords = { "WeapTypeWarhammer" };
	CHECK(Label(hammer) == "weapon_hammer");

	CHECK(Label(Weapon(Sk(0x0E3C16), WeaponType::TwoHandAxe)) == "weapon_pickaxe");  // by base id
	CHECK(Label(Weapon(Sk(0x02F2F4), WeaponType::OneHandAxe)) == "weapon_woodaxe");

	auto noType = MakeItem(Sk(0x1), ItemType::Weapon);
	const auto icon = SkyUIDefaultIcon(noType);
	CHECK(icon.label == "default_weapon");
	CHECK(icon.source == kSkyUIItemIcons);
	CHECK(Color(icon) == 0xA4A5BF);
}

TEST_CASE("SkyUI default icons: armor by weight class and slot", "[icons][skyui]")
{
	struct Case
	{
		std::uint32_t slots;
		const char*   light;
		const char*   heavy;
		const char*   clothing;
	};
	const auto c = GENERATE(
		Case{ 0x4, "lightarmor_body", "armor_body", "clothing_body" },
		Case{ 0x2, "lightarmor_head", "armor_head", "clothing_head" },  // hair slot = helmets
		Case{ 0x1, "lightarmor_head", "armor_head", "clothing_head" },
		Case{ 0x800, "lightarmor_head", "armor_head", "clothing_head" },
		Case{ 0x8, "lightarmor_hands", "armor_hands", "clothing_hands" },
		Case{ 0x10, "lightarmor_forearms", "armor_forearms", "clothing_forearms" },
		Case{ 0x80, "lightarmor_feet", "armor_feet", "clothing_feet" },
		Case{ 0x100, "lightarmor_calves", "armor_calves", "clothing_calves" },
		Case{ 0x200, "lightarmor_shield", "armor_shield", "clothing_shield" },
		Case{ 0x400, "lightarmor_body", "armor_body", "clothing_body" },  // tail
		Case{ 0x4 | 0x2 | 0x8, "lightarmor_body", "armor_body", "clothing_body" },  // body has precedence
		Case{ 0x2 | 0x1, "lightarmor_head", "armor_head", "clothing_head" });
	INFO(c.slots);
	CHECK(Label(Armor(ArmorType::Light, c.slots)) == c.light);
	CHECK(Label(Armor(ArmorType::Heavy, c.slots)) == c.heavy);
	CHECK(Label(Armor(ArmorType::Clothing, c.slots)) == c.clothing);
}

TEST_CASE("SkyUI default icons: armor colours, jewelry, fallbacks", "[icons][skyui]")
{
	CHECK(Color(SkyUIDefaultIcon(Armor(ArmorType::Light, 0x4))) == 0x756000);
	CHECK(Color(SkyUIDefaultIcon(Armor(ArmorType::Heavy, 0x4))) == 0x6B7585);
	CHECK(Color(SkyUIDefaultIcon(Armor(ArmorType::Clothing, 0x4))) == 0xEDDA87);

	// Jewelry: weight class "none" + slot -> SkyUI's jewelry class.
	CHECK(Label(Armor(ArmorType::Clothing, 0x20)) == "armor_amulet");
	CHECK(Label(Armor(ArmorType::Clothing, 0x40)) == "armor_ring");
	CHECK(Label(Armor(ArmorType::Clothing, 0x1000)) == "armor_circlet");
	CHECK(Label(Armor(ArmorType::Clothing, 0x2000)) == "default_armor");  // ears: no label
	// A light-armor ring still shows the ring.
	CHECK(Label(Armor(ArmorType::Light, 0x40)) == "armor_ring");
	// VendorItemJewelry on a clothing item makes it jewelry even on a body slot (no body jewelry icon).
	CHECK(Label(Armor(ArmorType::Clothing, 0x4, { "VendorItemJewelry" })) == "default_armor");
	// Wedding wreath (base id 0x08895A) is jewelry: circlet slot -> circlet icon either way.
	auto wreath = Armor(ArmorType::Clothing, 0x1000);
	wreath.form = Sk(0x08895A);
	CHECK(Label(wreath) == "armor_circlet");
	// Unnamed slots only: default label.
	CHECK(Label(Armor(ArmorType::Heavy, 1u << 22)) == "default_armor");
	// No slot data from the plugin: vanilla slot keywords are used.
	CHECK(Label(Armor(ArmorType::Heavy, 0, { "ArmorGauntlets" })) == "armor_hands");
	CHECK(Label(Armor(ArmorType::Clothing, 0, { "ClothingRing" })) == "armor_ring");
	CHECK(Label(Armor(ArmorType::Heavy, 0)) == "default_armor");
}

TEST_CASE("SkyUI default icons: ammo, keys, torches, ingredients, scrolls, other", "[icons][skyui]")
{
	auto arrow = MakeItem(Sk(0x1397D), ItemType::Ammo);
	arrow.ammoKind = AmmoKind::Arrow;
	CHECK(Label(arrow) == "weapon_arrow");
	CHECK(Color(SkyUIDefaultIcon(arrow)) == 0xA89E8C);
	auto bolt = MakeItem(Sk(0xCEE9E), ItemType::Ammo);
	bolt.ammoKind = AmmoKind::Bolt;
	CHECK(Label(bolt) == "weapon_bolt");

	CHECK(Label(MakeItem(Sk(0x1), ItemType::Key)) == "default_key");
	CHECK(Label(MakeItem(Sk(0x1D4EC), ItemType::Light)) == "misc_torch");
	CHECK(Label(MakeItem(Sk(0x1), ItemType::Ingredient)) == "default_ingredient");
	CHECK(Label(MakeItem(Sk(0x1), ItemType::Other)) == "default_misc");

	auto scroll = MakeItem(Sk(0x1), ItemType::Scroll);
	CHECK(Label(scroll) == "default_scroll");
	CHECK_FALSE(SkyUIDefaultIcon(scroll).color);
	scroll.resistance = 41;  // FireResist
	CHECK(Color(SkyUIDefaultIcon(scroll)) == 0xC73636);
	scroll.resistance = 42;
	CHECK(Color(SkyUIDefaultIcon(scroll)) == 0xFFFF00);
	scroll.resistance = 43;
	CHECK(Color(SkyUIDefaultIcon(scroll)) == 0x1FFBFF);
}

TEST_CASE("SkyUI default icons: potions, poisons, food and drink", "[icons][skyui]")
{
	CHECK(Label(Potion(Sk(0x1), 1, 1)) == "default_potion");
	CHECK(Label(PotionAV(24)) == "potion_health");
	CHECK(Color(SkyUIDefaultIcon(PotionAV(24))) == 0xDB2E73);
	CHECK(Label(PotionAV(27)) == "potion_health");   // heal rate
	CHECK(Label(PotionAV(155)) == "potion_health");  // heal rate mult
	CHECK(Label(PotionAV(25)) == "potion_magic");
	CHECK(Label(PotionAV(156)) == "potion_magic");
	CHECK(Label(PotionAV(26)) == "potion_stam");
	CHECK(Label(PotionAV(29)) == "potion_stam");
	CHECK(Label(PotionAV(41)) == "potion_fire");
	CHECK(Label(PotionAV(42)) == "potion_shock");
	CHECK(Label(PotionAV(43)) == "potion_frost");
	CHECK(Label(PotionAV(44)) == "default_potion");  // magic resist: no special icon

	auto poison = PotionAV(24);
	poison.alchemyKind = AlchemyKind::Poison;
	CHECK(Label(poison) == "potion_poison");
	CHECK(Color(SkyUIDefaultIcon(poison)) == 0xAD00B3);

	auto food = PotionAV(24);
	food.alchemyKind = AlchemyKind::Food;
	CHECK(Label(food) == "default_food");
	food.useSound = FormRef{ "Skyrim.esm", 0x0B6435 };  // ITMPotionUse: a drink
	CHECK(Label(food) == "food_wine");
	food.useSound = FormRef{ "Skyrim.esm", 0x0B6436 };
	CHECK(Label(food) == "default_food");

	// Raw ALCH flags from the plugin agree with the coarse kind.
	auto flagged = Potion(Sk(0x1), 1, 1);
	flagged.alchemyKind.reset();
	flagged.typeFlags = 0x20000;
	CHECK(Label(flagged) == "potion_poison");
}

TEST_CASE("SkyUI default icons: books", "[icons][skyui]")
{
	CHECK(Label(Book()) == "default_book");
	CHECK(Label(Book(BookTeaches::Skill)) == "default_book");
	CHECK(Label(Book(BookTeaches::Spell, { "VendorItemSpellTome" })) == "book_tome");
	CHECK(Label(Book(BookTeaches::Spell)) == "default_book");  // SkyUI keys tomes on the keyword
	CHECK(Label(Book(BookTeaches::None, { "VendorItemRecipe" })) == "book_note");
	auto note = Book();
	note.bookIsNote = true;
	CHECK(Label(note) == "book_note");
	CHECK_FALSE(SkyUIDefaultIcon(note).color);
}

TEST_CASE("SkyUI default icons: misc items", "[icons][skyui]")
{
	CHECK(Label(Misc(Sk(0x1))) == "default_misc");
	CHECK(Label(Misc(Sk(0x1), 1, { "VendorItemGem" })) == "misc_gem");
	CHECK(Color(SkyUIDefaultIcon(Misc(Sk(0x1), 1, { "VendorItemGem" }))) == 0xFFB0D1);
	CHECK(Label(Misc(Sk(0x1), 1, { "VendorItemAnimalHide" })) == "misc_hide");
	CHECK(Label(Misc(Sk(0x1), 1, { "VendorItemAnimalPart" })) == "misc_remains");
	CHECK(Label(Misc(Sk(0x1), 1, { "VendorItemOreIngot" })) == "misc_ingot");
	CHECK(Label(Misc(Sk(0x1), 1, { "VendorItemClutter" })) == "misc_clutter");
	CHECK(Label(Misc(Sk(0x1), 1, { "VendorItemFirewood" })) == "misc_wood");
	CHECK(Label(Misc(Sk(0x1), 1, { "VendorItemFireword" })) == "default_misc");  // the vanilla typo: SkyUI misses it
	CHECK(Label(Misc(Sk(0x1), 1, { "VendorItemDaedricArtifact" })) == "misc_artifact");
	CHECK(Label(Misc(Sk(0x1), 1, { "VendorItemTool" })) == "default_misc");
	CHECK(Label(Misc(Sk(0x1), 1, { "BYOHAdoptionClothesKeyword" })) == "clothing_body");
	CHECK(Label(Misc(Sk(0x1), 1, { "BYOHHouseCraftingCategorySmithing" })) == "default_misc");
	// Keyword lookups are case-sensitive, as in SkyUI (AS2 member names).
	CHECK(Label(Misc(Sk(0x1), 1, { "vendoritemgem" })) == "default_misc");
	// Keyword order of precedence: artifact before gem.
	CHECK(Label(Misc(Sk(0x1), 1, { "VendorItemGem", "VendorItemDaedricArtifact" })) == "misc_artifact");

	CHECK(Label(Misc(Sk(0x0000000F))) == "misc_gold");
	CHECK(Label(Misc(Sk(0x0000000A))) == "misc_lockpick");
	CHECK(Label(Misc(Sk(0x0DB5D2))) == "misc_leather");
	CHECK(Label(Misc(Sk(0x0800E4))) == "misc_strips");
	CHECK(Label(Misc(Sk(0x04B56C))) == "misc_dragonclaw");
	CHECK(Label(Misc(Sk(0x06851E))) == "misc_gem");
	// Base ids are matched on the low 24 bits, whatever the plugin (SkyUI: formId & 0xFFFFFF).
	CHECK(Label(Misc(FormRef{ "Dawnguard.esm", 0x0000000F })) == "misc_gold");
}

TEST_CASE("SkyUI default icons: soul gems", "[icons][skyui]")
{
	CHECK(Label(Gem(1, 0)) == "soulgem_empty");
	CHECK(Label(Gem(1, 1)) == "soulgem_full");
	CHECK(Label(Gem(3, 2)) == "soulgem_partial");
	CHECK(Label(Gem(3, 5)) == "soulgem_full");
	CHECK(Label(Gem(4, 0)) == "soulgem_grandempty");
	CHECK(Label(Gem(5, 3)) == "soulgem_grandpartial");
	CHECK(Label(Gem(5, 5)) == "soulgem_grandfull");
	CHECK(Color(SkyUIDefaultIcon(Gem(1, 0))) == 0xD7D4FF);
	CHECK(Color(SkyUIDefaultIcon(Gem(5, 0))) == 0x7569FF);
	CHECK(Label(Gem(5, 5, Sk(0x063B27))) == "soulgem_azura");
	CHECK(Label(Gem(0, 0)) == "misc_soulgem");

	// No size data: SkyUI's plain soul gem icon.
	auto unknown = MakeItem(Sk(0x1), ItemType::SoulGem);
	unknown.soulGemState = SoulGemState::Filled;
	CHECK(Label(unknown) == "misc_soulgem");
	// Size known, soul not filled in: the coarse state decides.
	unknown.gemSize = 2;
	CHECK(Label(unknown) == "soulgem_full");
}

// --- ParseIconRef / IconRefToJson ---------------------------------------------------------------------

TEST_CASE("ParseIconRef accepts a label or an object", "[icons][parse]")
{
	Diagnostics diags;
	auto        a = ParseIconRef(nlohmann::json("weapon_sword"), "t", diags);
	REQUIRE(a);
	CHECK(*a == IconRef{ std::string(kSkyUIItemIcons), "weapon_sword", std::nullopt });

	auto b = ParseIconRef(nlohmann::json::parse(R"({"label":"inv_weapons","source":"skyui\\icons_category_psychosteve.swf","color":"#a4A5bf"})"), "t", diags);
	REQUIRE(b);
	CHECK(b->source == kSkyUICategoryIcons);
	CHECK(b->label == "inv_weapons");
	CHECK(b->color == 0xA4A5BFu);

	auto c = ParseIconRef(nlohmann::json::parse(R"({"label":"x","color":"0x00FF00","_comment":"ok"})"), "t", diags);
	REQUIRE(c);
	CHECK(c->color == 0x00FF00u);
	CHECK(c->source == kSkyUIItemIcons);

	auto d = ParseIconRef(nlohmann::json::parse(R"({"label":"x","color":255})"), "t", diags);
	REQUIRE(d);
	CHECK(d->color == 255u);
	CHECK(diags.Empty());

	auto e = ParseIconRef(nlohmann::json::parse(R"({"label":"x","colour":"#000000"})"), "t", diags);
	REQUIRE(e);
	CHECK(HasMessage(diags, "unknown icon field \"colour\""));
	CHECK_FALSE(diags.HasErrors());
}

TEST_CASE("ParseIconRef rejects malformed icons", "[icons][parse]")
{
	const auto bad = GENERATE(as<std::string>{}, R"("")", R"(42)", R"([])", R"({})", R"({"label":""})", R"({"label":"x","color":"red"})",
		R"({"label":"x","color":"#1234567"})", R"({"label":"x","source":""})", R"({"label":"x","color":-1})", R"({"source":"a.swf"})");
	INFO(bad);
	Diagnostics diags;
	CHECK_FALSE(ParseIconRef(nlohmann::json::parse(bad), "cat.icon", diags));
	CHECK(diags.HasErrors());
	CHECK(diags.Items().front().source == "cat.icon");
}

TEST_CASE("IconRefToJson round-trips", "[icons][parse]")
{
	const IconRef plain{ std::string(kSkyUIItemIcons), "misc_gem", std::nullopt };
	CHECK(IconRefToJson(plain) == nlohmann::json("misc_gem"));
	const IconRef full{ std::string(kSkyUICategoryIcons), "inv_misc", 0x0A0B0C };
	const auto    j = IconRefToJson(full);
	CHECK(j["color"] == "#0A0B0C");
	for (const auto& icon : { plain, full, IconRef{ std::string(kSkyUIItemIcons), "x", 0xFFFFFF } }) {
		Diagnostics diags;
		const auto  back = ParseIconRef(IconRefToJson(icon), "t", diags);
		REQUIRE(back);
		CHECK(*back == icon);
	}
	CHECK(ParseIconColor(" #ff00FF ") == 0xFF00FFu);
	CHECK(FormatIconColor(0x1A) == "#00001A");
	CHECK_FALSE(ParseIconColor("#"));
}

// --- Category icons ------------------------------------------------------------------------------------

TEST_CASE("Category icon field", "[icons][categories]")
{
	Diagnostics diags;
	const auto  defs = ParseCategoryText(R"({
		"$schema": "dovastash-categories-1",
		"categories": [
			{ "id": "weapons", "name": "Weapons", "icon": { "source": "skyui/icons_category_psychosteve.swf", "label": "inv_weapons" } },
			{ "id": "weapons.swords", "icon": "weapon_sword", "match": { "formTypes": "WEAP", "weaponTypes": "OneHandSword" } },
			{ "id": "weapons.bows", "icon": { "label": "" }, "match": { "formTypes": "WEAP", "weaponTypes": "Bow" } }
		]
	})",
		"cats.json", diags);
	REQUIRE(defs.size() == 3);
	REQUIRE(defs[0].icon);
	CHECK(defs[0].icon->source == kSkyUICategoryIcons);
	CHECK(defs[1].icon->label == "weapon_sword");
	CHECK_FALSE(defs[2].icon);  // bad icon: dropped with a warning, category kept
	CHECK_FALSE(diags.HasErrors());
	CHECK(HasMessage(diags, "icon ignored"));

	const auto set = CategorySet::Compile(defs, {}, diags);
	REQUIRE(set.Find("weapons.swords"));
	CHECK(set.Find("weapons.swords")->icon->label == "weapon_sword");
	CHECK_FALSE(set.Find("weapons.bows")->icon);

	// Serialization round-trip.
	for (const auto& def : defs) {
		const auto  j = CategoryToJson(def);
		Diagnostics again;
		const auto  doc = nlohmann::json{ { "$schema", "dovastash-categories-1" }, { "categories", nlohmann::json::array({ nlohmann::json::parse(j.dump()) }) } };
		const auto  back = ParseCategoryDocument(doc, "round", again);
		INFO(j.dump());
		REQUIRE(back.size() == 1);
		CHECK(back[0].id == def.id);
		CHECK(back[0].icon == def.icon);
		CHECK(back[0].match.Empty() == def.match.Empty());
		CHECK(CategoryToJson(back[0]) == j);
	}
}

// --- I4 rules ---------------------------------------------------------------------------------------------

TEST_CASE("I4: real-shaped rules, formId and subType chaining", "[icons][i4]")
{
	const auto rules = Rules(kI4Skyrim);
	// Spell rule dropped (cannot match items); the other five are kept.
	CHECK(rules.Size() == 5);

	// formId "Skyrim.esm|1994F" (no 0x) -> subType Gem -> SkyUI re-run gives the gem icon + colour.
	const auto gem = rules.Resolve(Misc(Sk(0x1994F)));
	CHECK(gem.label == "misc_gem");
	CHECK(Color(gem) == 0xFFB0D1);
	CHECK(gem.source == kSkyUIItemIcons);
	CHECK(rules.Resolve(Misc(Sk(0x19950))).label == "default_misc");
	CHECK(rules.Resolve(Misc(FormRef{ "Update.esm", 0x1994F })).label == "default_misc");

	// The vanilla firewood typo keyword, fixed by I4.
	CHECK(rules.Resolve(Misc(Sk(0x6F993), 1, { "VendorItemFireword" })).label == "misc_wood");

	// iconLabel without iconSource: SkyUI's item SWF.
	const auto troll = rules.Resolve(Misc(FormRef{ "Dragonborn.esm", 0x247F9 }));
	CHECK(troll.label == "misc_trollskull");
	CHECK(troll.source == kSkyUIItemIcons);

	// Lower-case subType name, weapon pickaxe.
	CHECK(rules.Resolve(Weapon(FormRef{ "dragonborn.esm", 0x179C9 }, WeaponType::TwoHandAxe)).label == "weapon_pickaxe");

	// weightClass anyOf with "Other" (= no weight class): jewelry/clothing colour.
	CHECK(Color(rules.Resolve(Armor(ArmorType::Clothing, 0x40))) == 0xEDDA87);
	CHECK(Color(rules.Resolve(Armor(ArmorType::Heavy, 0x4))) == 0x6B7585);
}

TEST_CASE("I4: precedence, iconSource and iconColor", "[icons][i4]")
{
	const auto rules = Rules(R"({ "rules": [
		{ "match": { "formType": "Weapon", "keywords": ["WeapMaterialIron"] },
		  "assign": { "iconSource": "Mod/icons.swf", "iconLabel": "iron", "iconColor": "#112233" } },
		{ "match": { "formType": "Weapon", "keywords": ["WeapMaterialSteel"] }, "assign": { "iconLabel": "steel" } },
		{ "match": { "formType": "Weapon", "formId": "Skyrim.esm|0x12EB7" }, "assign": { "iconColor": "#445566" } },
		{ "match": { "formType": "Weapon", "keywords": ["Late"] }, "assign": { "iconSource": "Late/late.swf", "iconLabel": "late" } },
	] })");

	auto iron = Weapon(Sk(0x12EB7), WeaponType::OneHandSword);
	iron.keywords = { "WeapMaterialIron" };
	auto icon = rules.Resolve(iron);
	CHECK(icon == IconRef{ "Mod/icons.swf", "iron", 0x445566 });  // later colour rule wins the colour only

	// A later rule with only a label resets the source to SkyUI's SWF.
	iron.keywords = { "WeapMaterialIron", "WeapMaterialSteel" };
	icon = rules.Resolve(iron);
	CHECK(icon.label == "steel");
	CHECK(icon.source == kSkyUIItemIcons);
	CHECK(Color(icon) == 0x445566);

	// Last matching rule wins.
	iron.keywords = { "WeapMaterialSteel", "Late", "WeapMaterialIron" };
	icon = rules.Resolve(iron);
	CHECK(icon.label == "late");
	CHECK(icon.source == "Late/late.swf");

	// No rule: SkyUI default, with SkyUI's colour.
	const auto plain = rules.Resolve(Weapon(Sk(0x1), WeaponType::Bow));
	CHECK(plain == IconRef{ std::string(kSkyUIItemIcons), "weapon_bow", 0xA4A5BF });
}

TEST_CASE("I4: iconSource validation drops source and label", "[icons][i4]")
{
	IconRules rules;
	int       asked = 0;
	rules.SetIconSourceValidator([&asked](std::string_view a_path) {
		++asked;
		return IEquals(a_path, "Good/icons.swf");  // the game's file system is case-insensitive
	});
	Diagnostics diags;
	rules.AddFileText(R"({ "rules": [
		{ "match": { "formType": "Key" }, "assign": { "iconSource": "Missing/icons.swf", "iconLabel": "nope", "iconColor": "#010203" } },
		{ "match": { "formType": "Key" }, "assign": { "iconSource": "missing/ICONS.swf", "iconLabel": "nope" } },
		{ "match": { "formType": "Ingredient" }, "assign": { "iconSource": "good/ICONS.swf", "iconLabel": "yes" } }
	] })",
		"Test.json", diags);
	CHECK(rules.Size() == 2);  // the second rule has nothing left to assign
	CHECK(asked == 2);         // results are cached per case-insensitive path
	const auto key = rules.Resolve(MakeItem(Sk(0x1), ItemType::Key));
	CHECK(key == IconRef{ std::string(kSkyUIItemIcons), "default_key", 0x010203 });
	CHECK(rules.Resolve(MakeItem(Sk(0x1), ItemType::Ingredient)) == IconRef{ "good/ICONS.swf", "yes", std::nullopt });
	CHECK(HasMessage(diags, "1 rule(s) ignored"));
}

TEST_CASE("I4: keyword and formId matching details", "[icons][i4]")
{
	const auto rules = Rules(R"({ "rules": [
		{ "match": { "keywords": ["A", "B"] }, "assign": { "iconLabel": "array_last_wins" } },
		{ "match": { "keywords": { "anyOf": ["C", "D"] } }, "assign": { "iconLabel": "any_c_d" } },
		{ "match": { "keywords": "E", "formType": "MiscItem" }, "assign": { "iconLabel": "e_misc" } },
		{ "match": { "formId": { "anyOf": ["MyMod.esl|801", "Bad Id", "Missing.esp|ZZ"] } }, "assign": { "iconLabel": "esl" } },
		{ "match": { "value": { "min": 100, "max": 200 }, "formType": "MiscItem" }, "assign": { "iconLabel": "pricey" } },
		{ "match": { "text": "Exact Name" }, "assign": { "iconLabel": "by_name" } }
	] })");

	CHECK(rules.Resolve(Misc(Sk(0x1), 1, { "B" })).label == "array_last_wins");
	CHECK(rules.Resolve(Misc(Sk(0x1), 1, { "A" })).label == "default_misc");  // I4 keeps only the last array entry
	CHECK(rules.Resolve(Misc(Sk(0x1), 1, { "D" })).label == "any_c_d");
	CHECK(rules.Resolve(Misc(Sk(0x1), 1, { "d" })).label == "default_misc");  // keywords are case-sensitive
	CHECK(rules.Resolve(Misc(Sk(0x1), 1, { "E" })).label == "e_misc");
	CHECK(rules.Resolve(MakeItem(Sk(0x1), ItemType::Key)).label == "default_key");

	auto esl = Misc(FormRef{ "MyMod.esl", 0x801 });
	CHECK(rules.Resolve(esl).label == "esl");

	auto pricey = Misc(Sk(0x1));
	pricey.value = 150;
	CHECK(rules.Resolve(pricey).label == "pricey");
	pricey.value = 201;
	CHECK(rules.Resolve(pricey).label == "default_misc");

	auto named = MakeItem(Sk(0x1), ItemType::Key);
	named.name = "Exact Name";
	CHECK(rules.Resolve(named).label == "by_name");
}

TEST_CASE("I4: armor parts, weight class and soul gem fields", "[icons][i4]")
{
	const auto rules = Rules(R"({ "rules": [
		{ "match": { "formType": "Armor", "parts": [47] }, "assign": { "iconLabel": "backpack" } },
		{ "match": { "formType": "Armor", "mainPart": 32, "weightClass": "Heavy" }, "assign": { "iconLabel": "heavy_main_body" } },
		{ "match": { "formType": "Armor", "subType": "Shield", "weightClass": "Light" }, "assign": { "iconLabel": "light_shield" } },
		{ "match": { "formType": "SoulGem", "status": "Partial", "gemSize": "Grand" }, "assign": { "iconLabel": "grand_partial" } },
		{ "match": { "formType": "Potion", "flags": "Food", "subType": "Drink" }, "assign": { "iconColor": "#00FF00" } },
		{ "match": { "formType": "Weapon", "weaponType": "TwoHandAxe", "subType": "Warhammer" }, "assign": { "iconLabel": "maul" } }
	] })");

	CHECK(rules.Resolve(Armor(ArmorType::Clothing, 1u << 17)).label == "backpack");          // slot 47
	CHECK(rules.Resolve(Armor(ArmorType::Heavy, 0x4 | 0x2)).label == "heavy_main_body");     // slot 32 is the main part
	CHECK(rules.Resolve(Armor(ArmorType::Heavy, 0x2 | 0x1)).label == "armor_head");          // body not worn
	CHECK(rules.Resolve(Armor(ArmorType::Light, 0x200)).label == "light_shield");
	CHECK(rules.Resolve(Gem(5, 2)).label == "grand_partial");
	CHECK(rules.Resolve(Gem(5, 5)).label == "soulgem_grandfull");

	auto drink = Potion(Sk(0x1), 1, 1);
	drink.alchemyKind = AlchemyKind::Food;
	drink.useSound = FormRef{ "Skyrim.esm", 0x0B6435 };
	const auto d = rules.Resolve(drink);
	CHECK(d.label == "food_wine");
	CHECK(Color(d) == 0x00FF00);

	auto hammer = Weapon(Sk(0x1), WeaponType::TwoHandAxe);
	hammer.keywords = { "WeapTypeWarhammer" };
	CHECK(rules.Resolve(hammer).label == "maul");
}

TEST_CASE("I4: assigned subType chains into later matches and SkyUI's default icon", "[icons][i4]")
{
	const auto rules = Rules(R"({ "rules": [
		{ "match": { "formType": "MiscItem", "keywords": "OCF_IngrRemains" }, "assign": { "subType": "Remains", "subTypeDisplay": "Remains" } },
		{ "match": { "formType": "MiscItem", "subType": "Remains" }, "assign": { "iconColor": "#ABCDEF" } },
		{ "match": { "formType": "Weapon", "keywords": "WeapTypeSpear" }, "assign": { "subType": "Spear" } },
		{ "match": { "formType": "Weapon", "subType": "Spear" }, "assign": { "iconSource": "Spears/icons.swf", "iconLabel": "spear" } },
		{ "match": { "itemClass": "Trophy" }, "assign": { "iconLabel": "trophy" } },
		{ "match": { "formType": "MiscItem", "keywords": "Trophy" }, "assign": { "itemClass": "Trophy" } }
	] })");

	const auto remains = rules.Resolve(Misc(Sk(0x1), 1, { "OCF_IngrRemains" }));
	CHECK(remains.label == "misc_remains");
	CHECK(Color(remains) == 0xABCDEF);

	// Unknown subType names stay strings; SkyUI falls back to its default weapon icon, a later rule
	// matches the string.
	auto spear = Weapon(Sk(0x1), WeaponType::OneHandSword);
	spear.keywords = { "WeapTypeSpear" };
	CHECK(rules.Resolve(spear) == IconRef{ "Spears/icons.swf", "spear", 0xA4A5BF });

	// Custom members written by one rule can be matched by another (icons are applied after all data).
	CHECK(rules.Resolve(Misc(Sk(0x1), 1, { "Trophy" })).label == "trophy");
}

TEST_CASE("I4: unsupported fields skip the rule with one diagnostic per file", "[icons][i4]")
{
	Diagnostics diags;
	const auto  rules = Rules(R"({ "rules": [
		{ "match": { "formType": "Armor", "armor": { "min": 50 } }, "assign": { "iconLabel": "heavy_hitter" } },
		{ "match": { "formType": "Weapon", "damage": 10 }, "assign": { "iconLabel": "x" } },
		{ "match": { "formType": "Weapon", "isStolen": true }, "assign": { "iconLabel": "y" } },
		{ "match": { "formType": "Spell", "spellType": "Power" }, "assign": { "iconLabel": "power" } },
		{ "match": { "formId": 1234 }, "assign": { "iconLabel": "runtime" } },
		{ "match": { "formType": "Key" }, "assign": { "iconLabel": "kept" } }
	] })",
		 diags);
	CHECK(rules.Size() == 1);
	REQUIRE(diags.Items().size() == 1);
	const auto& msg = diags.Items().front().message;
	CHECK(diags.Items().front().severity == Diagnostic::Severity::Warning);
	CHECK(diags.Items().front().source == "Test.json");
	CHECK(msg.find("4 rule(s) skipped") != std::string::npos);
	CHECK(msg.find("armor") != std::string::npos);
	CHECK(msg.find("damage") != std::string::npos);
	CHECK(msg.find("isStolen") != std::string::npos);
	CHECK(msg.find("spellType") == std::string::npos);  // spell rules are irrelevant, not unsupported
	CHECK(rules.Resolve(MakeItem(Sk(0x1), ItemType::Key)).label == "kept");
}

TEST_CASE("I4: invalid rules, lenient JSON and file order", "[icons][i4]")
{
	Diagnostics diags;
	IconRules   rules;
	rules.AddFileText(
		"\xEF\xBB\xBF{ // I4 tolerates comments\n"
		"  \"rules\": [\n"
		"    { \"match\": { \"formType\": \"Key\" }, \"assign\": { \"iconLabel\": \"first\", } },\n"
		"    { \"match\": { }, \"assign\": { \"iconLabel\": \"no_match\" } },\n"
		"    { \"match\": { \"formType\": \"Key\" }, \"assign\": { } },\n"
		"    { \"match\": { \"formType\": \"Key\" } },\n"
		"    { \"match\": { \"$comment\": \"x\", \"formType\": \"Key\" }, \"assign\": { \"iconLabel\": [\"array ignored\"] } },\n"
		"    \"not a rule\",\n"
		"  ], /* trailing comma above */\n"
		"}\n",
		"A.json", diags);
	CHECK(rules.Size() == 1);
	CHECK(HasMessage(diags, "5 rule(s) ignored"));
	CHECK_FALSE(diags.HasErrors());

	rules.AddFileText(R"({ "rules": [ { "match": { "formType": "Key" }, "assign": { "iconLabel": "second" } } ] })", "B.json", diags);
	CHECK(rules.Size() == 2);
	CHECK(rules.Resolve(MakeItem(Sk(0x1), ItemType::Key)).label == "second");  // later file wins

	Diagnostics bad;
	rules.AddFileText("{ \"rules\": [ ", "C.json", bad);
	CHECK(bad.HasErrors());
	rules.AddFileText(R"({ "notRules": [] })", "D.json", bad);
	CHECK(HasMessage(bad, "no \"rules\" array"));
	CHECK(rules.Size() == 2);

	// Empty files are how patches switch another mod's rules off: silently nothing.
	Diagnostics none;
	rules.AddFileText("", "Gourmet.json", none);
	rules.AddFileText("\xEF\xBB\xBF \r\n", "Gourmet.json", none);
	CHECK(none.Empty());
	CHECK(rules.Size() == 2);

	// Copies are independent.
	IconRules copy = rules;
	copy.AddFileText(R"({ "rules": [ { "match": { "formType": "Key" }, "assign": { "iconLabel": "third" } } ] })", "E.json", bad);
	CHECK(copy.Resolve(MakeItem(Sk(0x1), ItemType::Key)).label == "third");
	CHECK(rules.Resolve(MakeItem(Sk(0x1), ItemType::Key)).label == "second");
	IconRules moved = std::move(copy);
	CHECK(moved.Size() == 3);
}

TEST_CASE("I4: SkyUI noColor and default item source", "[icons][i4]")
{
	IconRules rules;
	rules.SetNoIconColors(true);
	rules.SetDefaultItemSource("skyui\\icons_item_custom.swf");
	Diagnostics diags;
	rules.AddFileText(R"({ "rules": [
		{ "match": { "formType": "Key" }, "assign": { "iconColor": "#123456" } },
		{ "match": { "formType": "Ingredient" }, "assign": { "iconSource": "Mod/i.swf", "iconLabel": "herb" } }
	] })",
		"Test.json", diags);

	const auto weapon = rules.Resolve(Weapon(Sk(0x1), WeaponType::Bow));
	CHECK(weapon.source == "skyui/icons_item_custom.swf");
	CHECK_FALSE(weapon.color);  // SkyUI colour removed
	CHECK_FALSE(rules.Resolve(MakeItem(Sk(0x1), ItemType::Key)).color);  // rule colour removed too
	CHECK(rules.Resolve(MakeItem(Sk(0x1), ItemType::Ingredient)).source == "Mod/i.swf");

	// KIT-style default colour rule: what an empty entry gets replaces every colour.
	rules.AddFileText(R"({ "rules": [ { "match": { "iconColor": { "anyOf": [ null, "#FFFFFF" ] } }, "assign": { "iconColor": "#EEEEEE" } } ] })", "KIT.json", diags);
	CHECK(Color(rules.Resolve(MakeItem(Sk(0x1), ItemType::Key))) == 0xEEEEEE);
	CHECK(Color(rules.Resolve(Weapon(Sk(0x1), WeaponType::Bow))) == 0xEEEEEE);
	CHECK(diags.Empty());
}
