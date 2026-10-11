#include "dova/core/Icons.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "JsonUtil.h"

// See Icons.h for the behaviour being reproduced and the upstream sources. Comments below name the
// SkyUI / I4 function each block mirrors.

namespace dova::core
{
	using detail::json;

	namespace detail_icons
	{
		// --- Entry model: a SkyUI list entry object ---------------------------------------------
		//
		// Scaleform values as I4 compares them (RE::GFxValue::operator==): same type and same payload.
		// Form ids are kept as FormRef instead of runtime numbers (load-order independent).

		struct Value
		{
			enum class Kind : std::uint8_t
			{
				Undefined,
				Null,
				Bool,
				Number,
				String,
				Form,
				Keywords  // the entry's "keywords" / "effectKeywords" object: names are its members
			};

			Kind                            kind{ Kind::Undefined };
			bool                            boolean{ false };
			double                          number{ 0.0 };
			std::string                     string;
			FormRef                         form;
			const std::vector<std::string>* keywords{ nullptr };

			static Value Undefined() { return {}; }
			static Value Null()
			{
				Value v;
				v.kind = Kind::Null;
				return v;
			}
			static Value Bool(bool a_value)
			{
				Value v;
				v.kind = Kind::Bool;
				v.boolean = a_value;
				return v;
			}
			static Value Number(double a_value)
			{
				Value v;
				v.kind = Kind::Number;
				v.number = a_value;
				return v;
			}
			static Value String(std::string a_value)
			{
				Value v;
				v.kind = Kind::String;
				v.string = std::move(a_value);
				return v;
			}
			static Value Form(FormRef a_value)
			{
				Value v;
				v.kind = Kind::Form;
				v.form = std::move(a_value);
				return v;
			}
			static Value Keywords(const std::vector<std::string>* a_names)
			{
				Value v;
				v.kind = Kind::Keywords;
				v.keywords = a_names;
				return v;
			}

			[[nodiscard]] bool IsNumber() const noexcept { return kind == Kind::Number; }
		};

		using Entry = std::map<std::string, Value, std::less<>>;

		const Value& Get(const Entry& a_entry, std::string_view a_name)
		{
			static const Value undefined;
			const auto         it = a_entry.find(a_name);
			return it != a_entry.end() ? it->second : undefined;
		}

		void Set(Entry& a_entry, std::string_view a_name, Value a_value)
		{
			if (a_value.kind == Value::Kind::Undefined) {
				a_entry.erase(std::string(a_name));
				return;
			}
			a_entry.insert_or_assign(std::string(a_name), std::move(a_value));
		}

		std::optional<double> Num(const Entry& a_entry, std::string_view a_name)
		{
			const auto& v = Get(a_entry, a_name);
			return v.IsNumber() ? std::optional<double>(v.number) : std::nullopt;
		}

		// AS2 `switch` on a number: strict equality with integer constants.
		std::optional<std::int64_t> Int(const Entry& a_entry, std::string_view a_name)
		{
			const auto n = Num(a_entry, a_name);
			if (!n || std::floor(*n) != *n || std::abs(*n) > 9.0e15) {
				return std::nullopt;
			}
			return static_cast<std::int64_t>(*n);
		}

		std::uint32_t ToU32(double a_value) noexcept
		{
			if (!std::isfinite(a_value)) {
				return 0;
			}
			if (a_value < 0) {
				return static_cast<std::uint32_t>(static_cast<std::int64_t>(std::max(a_value, -2147483648.0)));
			}
			return static_cast<std::uint32_t>(std::min(a_value, 4294967295.0));
		}

		// keywords["X"] != undefined (case-sensitive, like AS2 member lookup).
		bool HasKeyword(const Entry& a_entry, std::string_view a_keyword, std::string_view a_member = "keywords")
		{
			const auto& v = Get(a_entry, a_member);
			return v.kind == Value::Kind::Keywords && v.keywords &&
			       std::ranges::find(*v.keywords, a_keyword) != v.keywords->end();
		}

		// --- Constants (skyui/defines/*.as, I4 Data/Defines/*.h) ------------------------------------

		namespace ft
		{
			inline constexpr int kScroll = 23;
			inline constexpr int kArmor = 26;
			inline constexpr int kBook = 27;
			inline constexpr int kIngredient = 30;
			inline constexpr int kLight = 31;
			inline constexpr int kMisc = 32;
			inline constexpr int kWeapon = 41;
			inline constexpr int kAmmo = 42;
			inline constexpr int kKey = 45;
			inline constexpr int kPotion = 46;
			inline constexpr int kSoulGem = 52;
		}

		inline constexpr std::array<int, 11> kItemFormTypes{ ft::kScroll, ft::kArmor, ft::kBook, ft::kIngredient, ft::kLight,
			ft::kMisc, ft::kWeapon, ft::kAmmo, ft::kKey, ft::kPotion, ft::kSoulGem };

		int FormTypeOf(ItemType a_type) noexcept
		{
			switch (a_type) {
			case ItemType::Weapon:
				return ft::kWeapon;
			case ItemType::Armor:
				return ft::kArmor;
			case ItemType::Ammo:
				return ft::kAmmo;
			case ItemType::Alchemy:
				return ft::kPotion;
			case ItemType::Ingredient:
				return ft::kIngredient;
			case ItemType::Book:
				return ft::kBook;
			case ItemType::Scroll:
				return ft::kScroll;
			case ItemType::SoulGem:
				return ft::kSoulGem;
			case ItemType::Misc:
				return ft::kMisc;
			case ItemType::Key:
				return ft::kKey;
			case ItemType::Light:
				return ft::kLight;
			default:
				return 0;
			}
		}

		// Armor.as PARTMASK_PRECEDENCE
		inline constexpr std::array<std::uint32_t, 32> kPartMaskPrecedence{
			0x00000004,  // body
			0x00000002,  // hair
			0x00000008,  // hands
			0x00000010,  // forearms
			0x00000080,  // feet
			0x00000100,  // calves
			0x00000200,  // shield
			0x00000020,  // amulet
			0x00000040,  // ring
			0x00000800,  // long hair
			0x00002000,  // ears
			0x00000001,  // head
			0x00001000,  // circlet
			0x00000400,  // tail
			1u << 14, 1u << 15, 1u << 16, 1u << 17, 1u << 18, 1u << 19, 1u << 20, 1u << 21, 1u << 22, 1u << 23,
			1u << 24, 1u << 25, 1u << 26, 1u << 27, 1u << 28, 1u << 29, 1u << 30, 1u << 31
		};

		std::uint32_t MainPart(std::uint32_t a_mask) noexcept
		{
			for (const auto bit : kPartMaskPrecedence) {
				if (a_mask & bit) {
					return bit;
				}
			}
			return 0;
		}

		namespace equip  // Armor.as EQUIP_*
		{
			inline constexpr int kHead = 0, kHair = 1, kLongHair = 2, kBody = 3, kForearms = 4, kHands = 5, kShield = 6,
								 kCalves = 7, kFeet = 8, kCirclet = 9, kAmulet = 10, kEars = 11, kRing = 12, kTail = 13;
		}

		namespace weight  // Armor.as WEIGHT_*
		{
			inline constexpr int kLight = 0, kHeavy = 1, kNone = 2, kClothing = 3, kJewelry = 4;
		}

		namespace av  // Actor.as AV_*
		{
			inline constexpr int kNone = 0, kHealth = 24, kMagicka = 25, kStamina = 26, kHealRate = 27, kMagickaRate = 28,
								 kStaminaRate = 29, kFireResist = 41, kElectricResist = 42, kFrostResist = 43,
								 kHealRateMult = 155, kMagickaRateMult = 156, kStaminaRateMult = 157;
		}

		struct NamedValue
		{
			std::string_view name;
			std::int64_t     value;
		};
		using Table = std::span<const NamedValue>;

		std::optional<std::int64_t> Lookup(Table a_table, std::string_view a_name)
		{
			for (const auto& nv : a_table) {
				if (IEquals(nv.name, a_name)) {
					return nv.value;
				}
			}
			return std::nullopt;
		}

		// I4 Data/Defines/Form.h FormTypeMap
		inline constexpr std::array<NamedValue, 14> kFormTypeNames{ { { "", 0 }, { "Ammo", ft::kAmmo }, { "Armor", ft::kArmor },
			{ "Book", ft::kBook }, { "Enchantment", 21 }, { "Ingredient", ft::kIngredient }, { "Key", ft::kKey },
			{ "Light", ft::kLight }, { "MiscItem", ft::kMisc }, { "Potion", ft::kPotion }, { "Scroll", ft::kScroll },
			{ "Spell", 22 }, { "SoulGem", ft::kSoulGem }, { "Weapon", ft::kWeapon } } };

		inline constexpr std::array<NamedValue, 14> kEquipTypes{ { { "Head", 0 }, { "Hair", 1 }, { "LongHair", 2 }, { "Body", 3 },
			{ "Forearms", 4 }, { "Hands", 5 }, { "Shield", 6 }, { "Calves", 7 }, { "Feet", 8 }, { "Circlet", 9 }, { "Amulet", 10 },
			{ "Ears", 11 }, { "Ring", 12 }, { "Tail", 13 } } };

		inline constexpr std::array<NamedValue, 4> kWeightClasses{ { { "Light", 0 }, { "Heavy", 1 }, { "Clothing", 3 }, { "Jewelry", 4 } } };

		// RE::WEAPON_TYPE (the "weaponType" member SKSE fills)
		inline constexpr std::array<NamedValue, 10> kWeaponAnimTypes{ { { "HandToHandMelee", 0 }, { "OneHandSword", 1 },
			{ "OneHandDagger", 2 }, { "OneHandAxe", 3 }, { "OneHandMace", 4 }, { "TwoHandSword", 5 }, { "TwoHandAxe", 6 },
			{ "Bow", 7 }, { "Staff", 8 }, { "Crossbow", 9 } } };

		// SkyUI subType values per form type (I4 EnumLookup<WeaponType/MiscType/BookType/AmmoType/PotionType/SoulGemType>)
		inline constexpr std::array<NamedValue, 13> kWeaponSubTypes{ { { "Melee", 0 }, { "Sword", 1 }, { "Dagger", 2 },
			{ "WarAxe", 3 }, { "Mace", 4 }, { "Greatsword", 5 }, { "Battleaxe", 6 }, { "Warhammer", 7 }, { "Bow", 8 },
			{ "Crossbow", 9 }, { "Staff", 10 }, { "Pickaxe", 11 }, { "WoodAxe", 12 } } };

		inline constexpr std::array<NamedValue, 22> kMiscSubTypes{ { { "Gem", 0 }, { "DragonClaw", 1 }, { "Artifact", 2 },
			{ "Leather", 3 }, { "LeatherStrips", 4 }, { "Hide", 5 }, { "Remains", 6 }, { "Ingot", 7 }, { "Tool", 8 },
			{ "ChildrensClothes", 9 }, { "Toy", 10 }, { "Firewood", 11 }, { "Fastener", 12 }, { "WeaponRack", 13 }, { "Shelf", 14 },
			{ "Furniture", 15 }, { "Exterior", 16 }, { "Container", 17 }, { "HousePart", 18 }, { "Clutter", 19 },
			{ "Lockpick", 20 }, { "Gold", 21 } } };

		inline constexpr std::array<NamedValue, 3> kBookSubTypes{ { { "SpellTome", 0 }, { "Note", 1 }, { "Recipe", 2 } } };

		inline constexpr std::array<NamedValue, 2> kAmmoSubTypes{ { { "Arrow", 0 }, { "Bolt", 1 } } };

		inline constexpr std::array<NamedValue, 16> kPotionSubTypes{ { { "Health", 0 }, { "HealRate", 1 }, { "HealRateMult", 2 },
			{ "Magicka", 3 }, { "MagickaRate", 4 }, { "MagickaRateMult", 5 }, { "Stamina", 6 }, { "StaminaRate", 7 },
			{ "StaminaRateMult", 8 }, { "FireResist", 9 }, { "ElectricResist", 10 }, { "FrostResist", 11 }, { "Potion", 12 },
			{ "Drink", 13 }, { "Food", 14 }, { "Poison", 15 } } };

		inline constexpr std::array<NamedValue, 7> kSoulGemSizes{ { { "None", 0 }, { "Petty", 1 }, { "Lesser", 2 }, { "Common", 3 },
			{ "Greater", 4 }, { "Grand", 5 }, { "Azura", 6 } } };

		inline constexpr std::array<NamedValue, 3> kSoulGemStatuses{ { { "Empty", 0 }, { "Partial", 1 }, { "Full", 2 } } };

		// RE::OBJ_BOOK::Type
		inline constexpr std::array<NamedValue, 2> kBookTypes{ { { "Book", 0 }, { "Note", 0xFF } } };

		// Bitfields: RE::OBJ_BOOK::Flag, RE::AMMO_DATA::Flag, RE::AlchemyItem::AlchemyFlag
		inline constexpr std::array<NamedValue, 3> kBookFlags{ { { "TeachesSpell", 1 << 2 }, { "TeachesSkill", 1 << 0 }, { "Read", 1 << 3 } } };
		inline constexpr std::array<NamedValue, 3> kAmmoFlags{ { { "IgnoresNormalWeaponResistance", 1 << 0 }, { "NonPlayable", 1 << 1 },
			{ "NonBolt", 1 << 2 } } };
		inline constexpr std::array<NamedValue, 4> kPotionFlags{ { { "ManualCalc", 0x1 }, { "Food", 0x2 }, { "Medicine", 0x10000 },
			{ "Poison", 0x20000 } } };

		// I4 Data/Defines/Actor.h EnumLookup<RE::ActorValue> (values = RE::ActorValue)
		inline constexpr NamedValue kActorValues[] = { { "Aggression", 0 }, { "Confidence", 1 }, { "Energy", 2 }, { "Morality", 3 },
			{ "Mood", 4 }, { "Assistance", 5 }, { "OneHanded", 6 }, { "TwoHanded", 7 }, { "Marksman", 8 }, { "Block", 9 },
			{ "Smithing", 10 }, { "HeavyArmor", 11 }, { "LightArmor", 12 }, { "Pickpocket", 13 }, { "Lockpicking", 14 },
			{ "Sneak", 15 }, { "Alchemy", 16 }, { "Speechcraft", 17 }, { "Alteration", 18 }, { "Conjuration", 19 },
			{ "Destruction", 20 }, { "Illusion", 21 }, { "Restoration", 22 }, { "Enchanting", 23 }, { "Health", 24 },
			{ "Magicka", 25 }, { "Stamina", 26 }, { "HealRate", 27 }, { "MagickaRate", 28 }, { "StaminaRate", 29 },
			{ "SpeedMult", 30 }, { "InventoryWeight", 31 }, { "CarryWeight", 32 }, { "CritChance", 33 }, { "MeleeDamage", 34 },
			{ "UnarmedDamage", 35 }, { "Mass", 36 }, { "VoicePoints", 37 }, { "VoiceRate", 38 }, { "DamageResist", 39 },
			{ "PoisonResist", 40 }, { "FireResist", 41 }, { "ElectricResist", 42 }, { "FrostResist", 43 }, { "MagicResist", 44 },
			{ "DiseaseResist", 45 }, { "PerceptionCondition", 46 }, { "EnduranceCondition", 47 }, { "LeftAttackCondition", 48 },
			{ "RightAttackCondition", 49 }, { "LeftMobilityCondition", 50 }, { "RightMobilityCondition", 51 },
			{ "BrainCondition", 52 }, { "Paralysis", 53 }, { "Invisibility", 54 }, { "NightEye", 55 }, { "DetectLifeRange", 56 },
			{ "WaterBreathing", 57 }, { "WaterWalking", 58 }, { "IgnoreCrippledLimbs", 59 }, { "Fame", 60 }, { "Infamy", 61 },
			{ "JumpingBonus", 62 }, { "WardPower", 63 }, { "RightItemCharge", 64 }, { "ArmorPerks", 65 }, { "ShieldPerks", 66 },
			{ "WardDeflection", 67 }, { "BowSpeedBonus", 78 }, { "FavorActive", 79 }, { "FavorsPerDay", 80 },
			{ "FavorsPerDayTimer", 81 }, { "LeftItemCharge", 82 }, { "AbsorbChance", 83 }, { "Blindness", 84 },
			{ "WeaponSpeedMult", 85 }, { "ShoutRecoveryMult", 86 }, { "BowStaggerBonus", 87 }, { "Telekinesis", 88 },
			{ "FavorPointsBonus", 89 }, { "LastBribedIntimidated", 90 }, { "LastFlattered", 91 }, { "MovementNoiseMult", 92 },
			{ "BypassVendorStolenCheck", 93 }, { "BypassVendorKeywordCheck", 94 }, { "WaitingForPlayer", 95 },
			{ "OneHandedMod", 96 }, { "TwoHandedMod", 97 }, { "MarksmanMod", 98 }, { "BlockMod", 99 }, { "SmithingMod", 100 },
			{ "HeavyArmorMod", 101 }, { "LightArmorMod", 102 }, { "PickpocketMod", 103 }, { "LockpickingMod", 104 },
			{ "SneakMod", 105 }, { "AlchemyMod", 106 }, { "SpeechcraftMod", 107 }, { "AlterationMod", 108 },
			{ "ConjurationMod", 109 }, { "DestructionMod", 110 }, { "IllusionMod", 111 }, { "RestorationMod", 112 },
			{ "EnchantingMod", 113 }, { "OneHandedSkillAdvance", 114 }, { "TwoHandedSkillAdvance", 115 },
			{ "MarksmanSkillAdvance", 116 }, { "BlockSkillAdvance", 117 }, { "SmithingSkillAdvance", 118 },
			{ "HeavyArmorSkillAdvance", 119 }, { "LightArmorSkillAdvance", 120 }, { "PickpocketSkillAdvance", 121 },
			{ "LockpickingSkillAdvance", 122 }, { "SneakSkillAdvance", 123 }, { "AlchemySkillAdvance", 124 },
			{ "SpeechcraftSkillAdvance", 125 }, { "AlterationSkillAdvance", 126 }, { "ConjurationSkillAdvance", 127 },
			{ "DestructionSkillAdvance", 128 }, { "IllusionSkillAdvance", 129 }, { "RestorationSkillAdvance", 130 },
			{ "EnchantingSkillAdvance", 131 }, { "LeftWeaponSpeedMult", 132 }, { "DragonSouls", 133 },
			{ "CombatHealthRegenMult", 134 }, { "OneHandedPowerMod", 135 }, { "TwoHandedPowerMod", 136 },
			{ "MarksmanPowerMod", 137 }, { "BlockPowerMod", 138 }, { "SmithingPowerMod", 139 }, { "HeavyArmorPowerMod", 140 },
			{ "LightArmorPowerMod", 141 }, { "PickpocketPowerMod", 142 }, { "LockpickingPowerMod", 143 },
			{ "SneakPowerMod", 144 }, { "AlchemyPowerMod", 145 }, { "SpeechcraftPowerMod", 146 }, { "AlterationPowerMod", 147 },
			{ "ConjurationPowerMod", 148 }, { "DestructionPowerMod", 149 }, { "IllusionPowerMod", 150 },
			{ "RestorationPowerMod", 151 }, { "EnchantingPowerMod", 152 }, { "DragonRend", 153 }, { "AttackDamageMult", 154 },
			{ "HealRateMult", 155 }, { "MagickaRateMult", 156 }, { "StaminaRateMult", 157 }, { "WerewolfPerks", 158 },
			{ "VampirePerks", 159 }, { "GrabActorOffset", 160 }, { "Grabbed", 161 }, { "ReflectDamage", 163 } };

		// Form.as BASEID_* (baseId = formId & 0x00FFFFFF)
		inline constexpr std::uint32_t kBaseWeddingWreath = 0x08895A;
		inline constexpr std::uint32_t kBaseVampireLordArmor = 0x011A84;
		inline constexpr std::array<std::uint32_t, 3> kBasePickaxes{ 0x0E3C16, 0x06A707, 0x1019D4 };
		inline constexpr std::array<std::uint32_t, 2> kBaseWoodAxes{ 0x02F2F4, 0x0AE086 };
		inline constexpr std::uint32_t kBaseFlawlessAmethyst = 0x06851E;
		inline constexpr std::array<std::uint32_t, 11> kBaseDragonClaws{ 0x04B56C, 0x0AB7BB, 0x07C260, 0x05AF48, 0x0ED417, 0x0AB375,
			0x08CDFA, 0x0B634C, 0x0999E7, 0x0663D7, 0x039647 };
		inline constexpr std::uint32_t kBaseLockpick = 0x00000A;
		inline constexpr std::uint32_t kBaseGold = 0x00000F;
		inline constexpr std::uint32_t kBaseLeather = 0x0DB5D2;
		inline constexpr std::uint32_t kBaseLeatherStrips = 0x0800E4;
		inline constexpr std::array<std::uint32_t, 2> kBaseAzuraStars{ 0x063B29, 0x063B27 };
		inline constexpr std::uint32_t kITMPotionUse = 0x0B6435;  // Skyrim.esm sound descriptor

		template <std::size_t N>
		bool OneOf(const std::array<std::uint32_t, N>& a_ids, std::uint32_t a_id)
		{
			return std::ranges::find(a_ids, a_id) != a_ids.end();
		}

		// --- SKSE scaleform extension + SkyUI InventoryDataSetter --------------------------------

		// Fallback when the plugin did not fill armorSlots: the vanilla slot keywords.
		std::uint32_t GuessArmorSlots(const std::vector<std::string>& a_keywords)
		{
			static constexpr std::array<std::pair<std::string_view, std::uint32_t>, 12> kSlotKeywords{ {
				{ "ArmorCuirass", 0x4 }, { "ClothingBody", 0x4 }, { "ArmorHelmet", 0x1 }, { "ClothingHead", 0x1 },
				{ "ArmorGauntlets", 0x8 }, { "ClothingHands", 0x8 }, { "ArmorBoots", 0x80 }, { "ClothingFeet", 0x80 },
				{ "ArmorShield", 0x200 }, { "ClothingRing", 0x40 }, { "ClothingNecklace", 0x20 }, { "ClothingCirclet", 0x1000 },
			} };
			std::uint32_t mask = 0;
			for (const auto& kw : a_keywords) {
				for (const auto& [name, bit] : kSlotKeywords) {
					if (IEquals(kw, name)) {
						mask |= bit;
					}
				}
			}
			return mask;
		}

		void ProcessArmorClass(Entry& a_e)  // InventoryDataSetter.processArmorClass
		{
			if (const auto wc = Int(a_e, "weightClass"); wc && *wc == weight::kNone) {
				Set(a_e, "weightClass", Value::Null());
			}
			const auto wc = Int(a_e, "weightClass");
			if (wc && (*wc == weight::kLight || *wc == weight::kHeavy)) {
				return;
			}
			if (HasKeyword(a_e, "VendorItemClothing")) {
				Set(a_e, "weightClass", Value::Number(weight::kClothing));
			} else if (HasKeyword(a_e, "VendorItemJewelry")) {
				Set(a_e, "weightClass", Value::Number(weight::kJewelry));
			}
		}

		void ProcessArmorPartMask(Entry& a_e)  // InventoryDataSetter.processArmorPartMask
		{
			const auto mask = Num(a_e, "partMask");
			if (!mask) {
				return;
			}
			const auto main = MainPart(ToU32(*mask));
			if (main == 0) {
				return;
			}
			Set(a_e, "mainPartMask", Value::Number(main));
			int subType = -1;
			switch (main) {
			case 0x1:
				subType = equip::kHead;
				break;
			case 0x2:
				subType = equip::kHair;
				break;
			case 0x800:
				subType = equip::kLongHair;
				break;
			case 0x4:
				subType = equip::kBody;
				break;
			case 0x8:
				subType = equip::kHands;
				break;
			case 0x10:
				subType = equip::kForearms;
				break;
			case 0x20:
				subType = equip::kAmulet;
				break;
			case 0x40:
				subType = equip::kRing;
				break;
			case 0x80:
				subType = equip::kFeet;
				break;
			case 0x100:
				subType = equip::kCalves;
				break;
			case 0x200:
				subType = equip::kShield;
				break;
			case 0x1000:
				subType = equip::kCirclet;
				break;
			case 0x2000:
				subType = equip::kEars;
				break;
			case 0x400:
				subType = equip::kTail;
				break;
			default:
				Set(a_e, "subType", Value::Number(main));
				return;
			}
			Set(a_e, "subType", Value::Number(subType));
		}

		void ProcessArmorOther(Entry& a_e)  // InventoryDataSetter.processArmorOther
		{
			const auto& wc = Get(a_e, "weightClass");
			if (wc.kind != Value::Kind::Undefined && wc.kind != Value::Kind::Null) {
				return;
			}
			const auto main = Int(a_e, "mainPartMask");
			if (!main) {
				return;
			}
			switch (*main) {
			case 0x1:
			case 0x2:
			case 0x800:
			case 0x4:
			case 0x8:
			case 0x10:
			case 0x80:
			case 0x100:
			case 0x200:
			case 0x400:
				Set(a_e, "weightClass", Value::Number(weight::kClothing));
				break;
			case 0x20:
			case 0x40:
			case 0x1000:
			case 0x2000:
				Set(a_e, "weightClass", Value::Number(weight::kJewelry));
				break;
			default:
				break;
			}
		}

		void ProcessArmorBaseId(Entry& a_e)  // InventoryDataSetter.processArmorBaseId
		{
			const auto baseId = Int(a_e, "baseId");
			if (!baseId) {
				return;
			}
			if (*baseId == kBaseWeddingWreath) {
				Set(a_e, "weightClass", Value::Number(weight::kJewelry));
			} else if (*baseId == kBaseVampireLordArmor) {
				Set(a_e, "subType", Value::Number(equip::kBody));
			}
		}

		void ProcessWeaponType(Entry& a_e)  // InventoryDataSetter.processWeaponType + processWeaponBaseId
		{
			Set(a_e, "subType", Value::Null());
			if (const auto anim = Int(a_e, "weaponType")) {
				std::optional<int> subType;
				switch (*anim) {
				case 0:
				case 10:
					subType = 0;  // melee
					break;
				case 1:
				case 11:
					subType = 1;  // sword
					break;
				case 2:
				case 12:
					subType = 2;  // dagger
					break;
				case 3:
				case 13:
					subType = 3;  // war axe
					break;
				case 4:
				case 14:
					subType = 4;  // mace
					break;
				case 5:
				case 15:
					subType = 5;  // greatsword
					break;
				case 6:
				case 16:
					subType = HasKeyword(a_e, "WeapTypeWarhammer") ? 7 : 6;  // warhammer / battleaxe
					break;
				case 7:
				case 17:
					subType = 8;  // bow
					break;
				case 8:
				case 18:
					subType = 10;  // staff
					break;
				case 9:
				case 19:
					subType = 9;  // crossbow
					break;
				default:
					break;
				}
				if (subType) {
					Set(a_e, "subType", Value::Number(*subType));
				}
			}
			if (const auto baseId = Int(a_e, "baseId")) {
				const auto id = static_cast<std::uint32_t>(*baseId);
				if (OneOf(kBasePickaxes, id)) {
					Set(a_e, "subType", Value::Number(11));
				} else if (OneOf(kBaseWoodAxes, id)) {
					Set(a_e, "subType", Value::Number(12));
				}
			}
		}

		void ProcessBookType(Entry& a_e, bool a_modelIsNote)  // InventoryDataSetter.processBookType + I4 FixNote
		{
			Set(a_e, "subType", Value::Undefined());  // Item.OTHER is undefined
			if (Int(a_e, "bookType") == 0xFF) {
				Set(a_e, "subType", Value::Number(1));  // note
			}
			if (HasKeyword(a_e, "VendorItemRecipe")) {
				Set(a_e, "subType", Value::Number(2));
			} else if (HasKeyword(a_e, "VendorItemSpellTome")) {
				Set(a_e, "subType", Value::Number(0));
			}
			if (a_modelIsNote) {
				Set(a_e, "bookType", Value::Number(0xFF));
				if (Get(a_e, "subType").kind == Value::Kind::Undefined) {
					Set(a_e, "subType", Value::Number(1));
				}
			}
		}

		void ProcessMiscType(Entry& a_e)  // InventoryDataSetter.processMiscType + processMiscBaseId
		{
			Set(a_e, "subType", Value::Undefined());
			std::optional<int> subType;
			if (HasKeyword(a_e, "BYOHAdoptionClothesKeyword")) {
				subType = 9;
			} else if (HasKeyword(a_e, "BYOHAdoptionToyKeyword")) {
				subType = 10;
			} else if (HasKeyword(a_e, "BYOHHouseCraftingCategoryWeaponRacks") || HasKeyword(a_e, "BYOHHouseCraftingCategoryShelf") ||
					   HasKeyword(a_e, "BYOHHouseCraftingCategoryFurniture") || HasKeyword(a_e, "BYOHHouseCraftingCategoryExterior") ||
					   HasKeyword(a_e, "BYOHHouseCraftingCategoryContainers") || HasKeyword(a_e, "BYOHHouseCraftingCategoryBuilding") ||
					   HasKeyword(a_e, "BYOHHouseCraftingCategorySmithing")) {
				subType = 18;
			} else if (HasKeyword(a_e, "VendorItemDaedricArtifact")) {
				subType = 2;
			} else if (HasKeyword(a_e, "VendorItemGem")) {
				subType = 0;
			} else if (HasKeyword(a_e, "VendorItemAnimalHide")) {
				subType = 5;
			} else if (HasKeyword(a_e, "VendorItemTool")) {
				subType = 8;
			} else if (HasKeyword(a_e, "VendorItemAnimalPart")) {
				subType = 6;
			} else if (HasKeyword(a_e, "VendorItemOreIngot")) {
				subType = 7;
			} else if (HasKeyword(a_e, "VendorItemClutter")) {
				subType = 19;
			} else if (HasKeyword(a_e, "VendorItemFirewood")) {
				subType = 11;
			}
			if (const auto baseId = Int(a_e, "baseId")) {
				const auto id = static_cast<std::uint32_t>(*baseId);
				if (id == kBaseFlawlessAmethyst) {
					subType = 0;
				} else if (OneOf(kBaseDragonClaws, id)) {
					subType = 1;
				} else if (id == kBaseLockpick) {
					subType = 20;
				} else if (id == kBaseGold) {
					subType = 21;
				} else if (id == kBaseLeather) {
					subType = 3;
				} else if (id == kBaseLeatherStrips) {
					subType = 4;
				}
			}
			if (subType) {
				Set(a_e, "subType", Value::Number(*subType));
			}
		}

		void ProcessPotionType(Entry& a_e, bool a_isDrinkSound)  // InventoryDataSetter.processPotionType
		{
			const auto flags = ToU32(Num(a_e, "flags").value_or(0));
			int        subType = 12;  // potion
			if (flags & 0x2) {
				subType = a_isDrinkSound ? 13 : 14;
			} else if (flags & 0x20000) {
				subType = 15;
			} else if (const auto actorValue = Int(a_e, "actorValue")) {
				switch (*actorValue) {
				case av::kHealth:
					subType = 0;
					break;
				case av::kMagicka:
					subType = 3;
					break;
				case av::kStamina:
					subType = 6;
					break;
				case av::kHealRate:
					subType = 1;
					break;
				case av::kMagickaRate:
					subType = 4;
					break;
				case av::kStaminaRate:
					subType = 7;
					break;
				case av::kHealRateMult:
					subType = 2;
					break;
				case av::kMagickaRateMult:
					subType = 5;
					break;
				case av::kStaminaRateMult:
					subType = 8;
					break;
				case av::kFireResist:
					subType = 9;
					break;
				case av::kElectricResist:
					subType = 10;
					break;
				case av::kFrostResist:
					subType = 11;
					break;
				default:
					break;
				}
			}
			Set(a_e, "subType", Value::Number(subType));
		}

		void ProcessSoulGem(Entry& a_e)  // InventoryDataSetter.processSoulGemType/Status/BaseId
		{
			Set(a_e, "subType", Value::Undefined());
			const auto gemSize = Int(a_e, "gemSize");
			const auto soulSize = Int(a_e, "soulSize");
			if (gemSize && *gemSize != 0) {
				Set(a_e, "subType", Value::Number(static_cast<double>(*gemSize)));
			}
			int status = 0;  // empty
			if (gemSize && soulSize && *soulSize != 0) {
				status = *soulSize >= *gemSize ? 2 : 1;
			}
			Set(a_e, "status", Value::Number(status));
			if (const auto baseId = Int(a_e, "baseId"); baseId && OneOf(kBaseAzuraStars, static_cast<std::uint32_t>(*baseId))) {
				Set(a_e, "subType", Value::Number(6));
			}
		}

		// The entry as SkyUI's icon setter sees it.
		Entry BuildEntry(const ItemInfo& a_item)
		{
			Entry      e;
			const auto formType = FormTypeOf(a_item.type);
			if (formType != 0) {
				Set(e, "formType", Value::Number(formType));
			}
			Set(e, "formId", Value::Form(a_item.form));
			if (a_item.form.IsValid()) {
				Set(e, "baseId", Value::Number(a_item.form.localId));  // == formId & 0xFFFFFF for full plugins
			}
			Set(e, "keywords", Value::Keywords(&a_item.keywords));
			Set(e, "text", Value::String(a_item.name));
			Set(e, "count", Value::Number(a_item.count));
			Set(e, "value", Value::Number(a_item.value));
			Set(e, "weight", Value::Number(a_item.weight));
			Set(e, "isEquipped", Value::Bool(a_item.equipped));

			const auto setMagic = [&] {
				if (a_item.actorValue) {
					Set(e, "actorValue", Value::Number(*a_item.actorValue));
				}
				if (a_item.resistance) {
					Set(e, "resistance", Value::Number(*a_item.resistance));
				}
				if (a_item.magicSchool) {
					Set(e, "school", Value::Number(*a_item.magicSchool));
				}
				Set(e, "effectKeywords", Value::Keywords(&a_item.effectKeywords));
			};

			switch (formType) {
			case ft::kArmor:
				{
					const auto slots = a_item.armorSlots != 0 ? a_item.armorSlots : GuessArmorSlots(a_item.keywords);
					Set(e, "partMask", Value::Number(slots));
					if (a_item.armorType) {
						const int wc = *a_item.armorType == ArmorType::Light ? weight::kLight :
						               *a_item.armorType == ArmorType::Heavy ? weight::kHeavy :
						                                                      weight::kNone;
						Set(e, "weightClass", Value::Number(wc));
					}
					Set(e, "isEnchanted", Value::Bool(a_item.enchanted));
					ProcessArmorClass(e);
					ProcessArmorPartMask(e);
					ProcessArmorOther(e);
					ProcessArmorBaseId(e);
				}
				break;
			case ft::kWeapon:
				if (a_item.weaponType) {
					Set(e, "weaponType", Value::Number(static_cast<int>(*a_item.weaponType)));
				}
				if (a_item.equipSlot) {
					Set(e, "equipSlot", Value::Form(*a_item.equipSlot));
				}
				Set(e, "isEnchanted", Value::Bool(a_item.enchanted));
				ProcessWeaponType(e);
				break;
			case ft::kAmmo:
				{
					auto flags = a_item.typeFlags;
					if (a_item.ammoKind == AmmoKind::Arrow) {
						flags |= 0x4;  // NonBolt
					}
					Set(e, "flags", Value::Number(flags));
					Set(e, "isEnchanted", Value::Bool(a_item.enchanted));
					Set(e, "subType", Value::Number((flags & 0x4) != 0 ? 0 : 1));  // processAmmoType
				}
				break;
			case ft::kBook:
				{
					auto flags = a_item.typeFlags;
					if (a_item.bookTeaches == BookTeaches::Spell) {
						flags |= 0x4;
					} else if (a_item.bookTeaches == BookTeaches::Skill) {
						flags |= 0x1;
					}
					Set(e, "flags", Value::Number(flags));
					Set(e, "bookType", Value::Number(0));
					if (a_item.teachesSkill) {
						Set(e, "teachesSkill", Value::Number(*a_item.teachesSkill));
					}
					if (a_item.teachesSpell) {
						Set(e, "teachesSpell", Value::Form(*a_item.teachesSpell));
					}
					ProcessBookType(e, a_item.bookIsNote);
				}
				break;
			case ft::kMisc:
				ProcessMiscType(e);
				break;
			case ft::kPotion:
				{
					auto flags = a_item.typeFlags;
					if (a_item.alchemyKind == AlchemyKind::Food) {
						flags |= 0x2;
					} else if (a_item.alchemyKind == AlchemyKind::Poison) {
						flags |= 0x20000;
					}
					Set(e, "flags", Value::Number(flags));
					setMagic();
					const bool drink = a_item.useSound && IEquals(a_item.useSound->plugin, "Skyrim.esm") && a_item.useSound->localId == kITMPotionUse;
					ProcessPotionType(e, drink);
				}
				break;
			case ft::kIngredient:
			case ft::kScroll:
				setMagic();
				break;
			case ft::kSoulGem:
				{
					if (a_item.gemSize) {
						Set(e, "gemSize", Value::Number(*a_item.gemSize));
					}
					std::uint8_t soul = 0;
					if (a_item.soulSize) {
						soul = *a_item.soulSize;
					} else if (a_item.soulGemState == SoulGemState::Filled && a_item.gemSize) {
						soul = *a_item.gemSize;
					}
					if (a_item.gemSize || a_item.soulSize) {
						Set(e, "soulSize", Value::Number(soul));
					}
					ProcessSoulGem(e);
				}
				break;
			default:
				break;
			}
			return e;
		}

		// --- SkyUI InventoryIconSetter ----------------------------------------------------------

		void SetIcon(Entry& a_e, std::string_view a_label)
		{
			Set(a_e, "iconLabel", Value::String(std::string(a_label)));
		}

		void SetColor(Entry& a_e, std::uint32_t a_color)
		{
			Set(a_e, "iconColor", Value::Number(a_color));
		}

		void JewelryIcon(Entry& a_e)
		{
			switch (Int(a_e, "subType").value_or(-1)) {
			case equip::kAmulet:
				SetIcon(a_e, "armor_amulet");
				break;
			case equip::kRing:
				SetIcon(a_e, "armor_ring");
				break;
			case equip::kCirclet:
				SetIcon(a_e, "armor_circlet");
				break;
			default:
				break;
			}
		}

		void BodyPartIcon(Entry& a_e, std::string_view a_prefix, bool a_jewelryFallsThrough)
		{
			const auto        subType = Int(a_e, "subType").value_or(-1);
			const std::string prefix(a_prefix);
			switch (subType) {
			case equip::kHead:
			case equip::kHair:
			case equip::kLongHair:
				SetIcon(a_e, prefix + "_head");
				break;
			case equip::kBody:
			case equip::kTail:
				SetIcon(a_e, prefix + "_body");
				break;
			case equip::kHands:
				SetIcon(a_e, prefix + "_hands");
				break;
			case equip::kForearms:
				SetIcon(a_e, prefix + "_forearms");
				break;
			case equip::kFeet:
				SetIcon(a_e, prefix + "_feet");
				break;
			case equip::kCalves:
				SetIcon(a_e, prefix + "_calves");
				break;
			case equip::kShield:
				SetIcon(a_e, prefix + "_shield");
				break;
			case equip::kAmulet:
			case equip::kRing:
			case equip::kCirclet:
				if (a_jewelryFallsThrough) {
					JewelryIcon(a_e);
				}
				break;
			default:
				break;
			}
		}

		void ArmorIcon(Entry& a_e)
		{
			SetIcon(a_e, "default_armor");
			SetColor(a_e, 0xEDDA87);
			switch (Int(a_e, "weightClass").value_or(-1)) {
			case weight::kLight:
				SetColor(a_e, 0x756000);
				BodyPartIcon(a_e, "lightarmor", true);
				break;
			case weight::kHeavy:
				SetColor(a_e, 0x6B7585);
				BodyPartIcon(a_e, "armor", true);
				break;
			case weight::kJewelry:
				JewelryIcon(a_e);
				break;
			default:  // clothing, other
				BodyPartIcon(a_e, "clothing", false);
				break;
			}
		}

		void BookIcon(Entry& a_e)
		{
			SetIcon(a_e, "default_book");
			switch (Int(a_e, "subType").value_or(-1)) {
			case 2:  // recipe
			case 1:  // note
				SetIcon(a_e, "book_note");
				break;
			case 0:
				SetIcon(a_e, "book_tome");
				break;
			default:
				break;
			}
		}

		void WeaponIcon(Entry& a_e)
		{
			SetIcon(a_e, "default_weapon");
			SetColor(a_e, 0xA4A5BF);
			static constexpr std::array<std::string_view, 13> kLabels{ "", "weapon_sword", "weapon_dagger", "weapon_waraxe",
				"weapon_mace", "weapon_greatsword", "weapon_battleaxe", "weapon_hammer", "weapon_bow", "weapon_crossbow",
				"weapon_staff", "weapon_pickaxe", "weapon_woodaxe" };
			const auto subType = Int(a_e, "subType");
			if (subType && *subType >= 1 && *subType < static_cast<std::int64_t>(kLabels.size())) {
				SetIcon(a_e, kLabels[static_cast<std::size_t>(*subType)]);
			}
		}

		void AmmoIcon(Entry& a_e)
		{
			SetIcon(a_e, "weapon_arrow");
			SetColor(a_e, 0xA89E8C);
			if (Int(a_e, "subType") == 1) {
				SetIcon(a_e, "weapon_bolt");
			}
		}

		void PotionIcon(Entry& a_e)
		{
			SetIcon(a_e, "default_potion");
			switch (Int(a_e, "subType").value_or(-1)) {
			case 13:
				SetIcon(a_e, "food_wine");
				break;
			case 14:
				SetIcon(a_e, "default_food");
				break;
			case 15:
				SetIcon(a_e, "potion_poison");
				SetColor(a_e, 0xAD00B3);
				break;
			case 0:
			case 1:
			case 2:
				SetIcon(a_e, "potion_health");
				SetColor(a_e, 0xDB2E73);
				break;
			case 3:
			case 4:
			case 5:
				SetIcon(a_e, "potion_magic");
				SetColor(a_e, 0x2E9FDB);
				break;
			case 6:
			case 7:
			case 8:
				SetIcon(a_e, "potion_stam");
				SetColor(a_e, 0x51DB2E);
				break;
			case 9:
				SetIcon(a_e, "potion_fire");
				SetColor(a_e, 0xC73636);
				break;
			case 10:
				SetIcon(a_e, "potion_shock");
				SetColor(a_e, 0xEAAB00);
				break;
			case 11:
				SetIcon(a_e, "potion_frost");
				SetColor(a_e, 0x1FFBFF);
				break;
			default:
				break;
			}
		}

		void SoulGemStatusIcon(Entry& a_e, bool a_grand)
		{
			switch (Int(a_e, "status").value_or(-1)) {
			case 0:
				SetIcon(a_e, a_grand ? "soulgem_grandempty" : "soulgem_empty");
				break;
			case 2:
				SetIcon(a_e, a_grand ? "soulgem_grandfull" : "soulgem_full");
				break;
			case 1:
				SetIcon(a_e, a_grand ? "soulgem_grandpartial" : "soulgem_partial");
				break;
			default:
				break;
			}
		}

		void SoulGemIcon(Entry& a_e)
		{
			SetIcon(a_e, "misc_soulgem");
			SetColor(a_e, 0xE3E0FF);
			switch (Int(a_e, "subType").value_or(-1)) {
			case 1:
				SetColor(a_e, 0xD7D4FF);
				SoulGemStatusIcon(a_e, false);
				break;
			case 2:
				SetColor(a_e, 0xC0BAFF);
				SoulGemStatusIcon(a_e, false);
				break;
			case 3:
				SetColor(a_e, 0xABA3FF);
				SoulGemStatusIcon(a_e, false);
				break;
			case 4:
				SetColor(a_e, 0x948BFC);
				SoulGemStatusIcon(a_e, true);
				break;
			case 5:
				SetColor(a_e, 0x7569FF);
				SoulGemStatusIcon(a_e, true);
				break;
			case 6:
				SetColor(a_e, 0x7569FF);
				SetIcon(a_e, "soulgem_azura");
				break;
			default:
				break;
			}
		}

		void MiscIcon(Entry& a_e)
		{
			SetIcon(a_e, "default_misc");
			switch (Int(a_e, "subType").value_or(-1)) {
			case 2:
				SetIcon(a_e, "misc_artifact");
				break;
			case 0:
				SetIcon(a_e, "misc_gem");
				SetColor(a_e, 0xFFB0D1);
				break;
			case 5:
				SetIcon(a_e, "misc_hide");
				SetColor(a_e, 0xDBB36E);
				break;
			case 6:
				SetIcon(a_e, "misc_remains");
				break;
			case 7:
				SetIcon(a_e, "misc_ingot");
				SetColor(a_e, 0x828282);
				break;
			case 19:
				SetIcon(a_e, "misc_clutter");
				break;
			case 11:
				SetIcon(a_e, "misc_wood");
				SetColor(a_e, 0xA89E8C);
				break;
			case 1:
				SetIcon(a_e, "misc_dragonclaw");
				break;
			case 20:
				SetIcon(a_e, "misc_lockpick");
				break;
			case 21:
				SetIcon(a_e, "misc_gold");
				SetColor(a_e, 0xCCCC33);
				break;
			case 3:
				SetIcon(a_e, "misc_leather");
				SetColor(a_e, 0xBA8D23);
				break;
			case 4:
				SetIcon(a_e, "misc_strips");
				SetColor(a_e, 0xBA8D23);
				break;
			case 9:
				SetColor(a_e, 0xEDDA87);
				SetIcon(a_e, "clothing_body");
				break;
			default:
				break;
			}
		}

		void ScrollResistColor(Entry& a_e)  // InventoryIconSetter.processResist
		{
			const auto resistance = Int(a_e, "resistance");
			if (!resistance || *resistance == av::kNone) {
				return;
			}
			switch (*resistance) {
			case av::kFireResist:
				SetColor(a_e, 0xC73636);
				break;
			case av::kElectricResist:
				SetColor(a_e, 0xFFFF00);
				break;
			case av::kFrostResist:
				SetColor(a_e, 0x1FFBFF);
				break;
			default:
				break;
			}
		}

		// InventoryIconSetter.processEntry without the noColor step (that is applied by the caller,
		// because I4's re-run of these functions does not apply it either).
		void SkyUIIcon(Entry& a_e)
		{
			const auto formType = Int(a_e, "formType");
			if (!formType) {
				return;
			}
			switch (*formType) {
			case ft::kScroll:
				SetIcon(a_e, "default_scroll");
				ScrollResistColor(a_e);
				break;
			case ft::kArmor:
				ArmorIcon(a_e);
				break;
			case ft::kBook:
				BookIcon(a_e);
				break;
			case ft::kIngredient:
				SetIcon(a_e, "default_ingredient");
				break;
			case ft::kLight:
				SetIcon(a_e, "misc_torch");
				break;
			case ft::kMisc:
				MiscIcon(a_e);
				break;
			case ft::kWeapon:
				WeaponIcon(a_e);
				break;
			case ft::kAmmo:
				AmmoIcon(a_e);
				break;
			case ft::kKey:
				SetIcon(a_e, "default_key");
				break;
			case ft::kPotion:
				PotionIcon(a_e);
				break;
			case ft::kSoulGem:
				SoulGemIcon(a_e);
				break;
			default:
				break;
			}
		}

		IconRef ToIconRef(const Entry& a_e, std::string_view a_defaultSource)
		{
			IconRef     icon;
			const auto& source = Get(a_e, "iconSource");
			icon.source = source.kind == Value::Kind::String && !source.string.empty() ? source.string : std::string(a_defaultSource);
			const auto& label = Get(a_e, "iconLabel");
			icon.label = label.kind == Value::Kind::String && !label.string.empty() ? label.string : "default_misc";
			if (const auto color = Num(a_e, "iconColor")) {
				icon.color = ToU32(*color) & 0xFFFFFF;
			}
			return icon;
		}

		// --- I4 rules -----------------------------------------------------------------------------

		struct Prop
		{
			enum class Kind : std::uint8_t
			{
				Never,
				Match,     // MatchProperty
				Range,     // RangeProperty
				Bitfield,  // BitfieldProperty
				Keyword,   // KeywordsProperty
				MainPart,  // MainPartProperty
				AnyOf      // AnyOfProperty
			};

			Kind                  kind{ Kind::Never };
			Value                 value;
			std::optional<double> min;
			std::optional<double> max;
			std::uint32_t         bits{ 0 };
			std::string           keyword;
			std::vector<Prop>     any;
		};

		// MatchProperty::Match + GFxValue::operator==
		bool ValueMatches(const Value& a_spec, const Value& a_entry)
		{
			if (a_entry.kind == Value::Kind::Null) {
				return a_spec.kind == Value::Kind::Undefined || a_spec.kind == Value::Kind::Null;
			}
			if (a_spec.kind != a_entry.kind) {
				return false;
			}
			switch (a_spec.kind) {
			case Value::Kind::Undefined:
			case Value::Kind::Null:
				return true;
			case Value::Kind::Bool:
				return a_spec.boolean == a_entry.boolean;
			case Value::Kind::Number:
				return a_spec.number == a_entry.number;
			case Value::Kind::String:
				return a_spec.string == a_entry.string;
			case Value::Kind::Form:
				return a_spec.form.IsValid() && a_spec.form == a_entry.form;
			case Value::Kind::Keywords:
			default:
				return false;
			}
		}

		bool PropMatches(const Prop& a_prop, const Value& a_value)
		{
			switch (a_prop.kind) {
			case Prop::Kind::Match:
				return ValueMatches(a_prop.value, a_value);
			case Prop::Kind::Range:
				return a_value.IsNumber() && (!a_prop.min || a_value.number >= *a_prop.min) && (!a_prop.max || a_value.number <= *a_prop.max);
			case Prop::Kind::Bitfield:
				return a_value.IsNumber() && (ToU32(a_value.number) & a_prop.bits) != 0;
			case Prop::Kind::Keyword:
				return a_value.kind == Value::Kind::Keywords && a_value.keywords &&
				       std::ranges::find(*a_value.keywords, a_prop.keyword) != a_value.keywords->end();
			case Prop::Kind::MainPart:
				{
					if (!a_value.IsNumber()) {
						return false;
					}
					const auto mask = ToU32(a_value.number);
					if ((mask & a_prop.bits) == 0) {
						return false;
					}
					const auto main = MainPart(mask);
					return main != 0 && main == a_prop.bits;
				}
			case Prop::Kind::AnyOf:
				return std::ranges::any_of(a_prop.any, [&](const Prop& a_sub) { return PropMatches(a_sub, a_value); });
			case Prop::Kind::Never:
			default:
				return false;
			}
		}

		struct Rule
		{
			std::map<std::string, Prop, std::less<>>  props;  // I4 Rule::_properties (std::map: one per name)
			std::map<std::string, Value, std::less<>> data;   // I4 Rule::_customData
			bool                                       hasInfo{ false };

			[[nodiscard]] bool Matches(const Entry& a_e) const
			{
				for (const auto& [name, prop] : props) {
					if (!PropMatches(prop, Get(a_e, name))) {
						return false;
					}
				}
				return true;
			}
		};

		bool IsIconData(std::string_view a_name) { return a_name.starts_with("icon"); }

		// CustomDataManager::ProcessEntry
		void ProcessEntry(const std::vector<Rule>& a_rules, Entry& a_e, bool a_withSkyUICallback)
		{
			bool needsIconUpdate = false;
			for (const auto& rule : a_rules) {
				if (!rule.Matches(a_e)) {
					continue;
				}
				needsIconUpdate |= rule.hasInfo;
				for (const auto& [name, value] : rule.data) {
					if (!IsIconData(name)) {
						Set(a_e, name, value);
					}
				}
			}
			if (needsIconUpdate && a_withSkyUICallback) {
				a_e.erase("iconLabel");
				a_e.erase("iconColor");
				SkyUIIcon(a_e);
			}
			for (const auto& rule : a_rules) {
				if (!rule.Matches(a_e)) {
					continue;
				}
				for (const auto& [name, value] : rule.data) {
					if (IsIconData(name)) {
						Set(a_e, name, value);
					}
				}
			}
		}

		// ParseUtil::ParseColor: optional '#', then std::stoll(..., 16) (leading spaces, sign and 0x
		// accepted, stops at the first non-hex character). Empty -> 0. No digits -> nullopt (I4 throws).
		std::optional<std::uint32_t> I4Color(std::string_view a_text)
		{
			if (a_text.empty()) {
				return 0u;
			}
			if (a_text.front() == '#') {
				a_text.remove_prefix(1);
			}
			while (!a_text.empty() && (a_text.front() == ' ' || a_text.front() == '\t')) {
				a_text.remove_prefix(1);
			}
			bool negative = false;
			if (!a_text.empty() && (a_text.front() == '+' || a_text.front() == '-')) {
				negative = a_text.front() == '-';
				a_text.remove_prefix(1);
			}
			if (a_text.size() >= 3 && a_text[0] == '0' && (a_text[1] == 'x' || a_text[1] == 'X')) {
				a_text.remove_prefix(2);
			}
			std::uint64_t value = 0;
			std::size_t   digits = 0;
			for (const char ch : a_text) {
				int d = -1;
				if (ch >= '0' && ch <= '9') {
					d = ch - '0';
				} else if (ch >= 'a' && ch <= 'f') {
					d = ch - 'a' + 10;
				} else if (ch >= 'A' && ch <= 'F') {
					d = ch - 'A' + 10;
				}
				if (d < 0) {
					break;
				}
				value = (value << 4) | static_cast<std::uint64_t>(d);
				if (++digits > 15) {
					return std::nullopt;
				}
			}
			if (digits == 0) {
				return std::nullopt;
			}
			const auto signedValue = negative ? -static_cast<std::int64_t>(value) : static_cast<std::int64_t>(value);
			return static_cast<std::uint32_t>(signedValue);
		}

		// Which I4 PropertyParser handles a match key (RuleParser.cpp PropertyParserMap).
		struct Parser
		{
			enum class Kind : std::uint8_t
			{
				Generic,
				FormType,
				FormId,
				Keywords,
				Parts,
				MainPart,
				Enum,
				Bitfield,
				Color
			};
			Kind  kind{ Kind::Generic };
			Table table{};
		};

		Parser GetParser(std::string_view a_name, int a_formType)
		{
			using K = Parser::Kind;
			if (a_name == "formType") {
				return { K::FormType };
			}
			if (a_name == "formId" || a_name == "equipSlot" || a_name == "teachesSpell") {
				return { K::FormId };
			}
			if (a_name == "keywords" || a_name == "effectKeywords") {
				return { K::Keywords };
			}
			if (a_name == "mainPart") {
				return { K::MainPart };
			}
			if (a_name == "parts") {
				return { K::Parts };
			}
			if (a_name == "weightClass") {
				return { K::Enum, kWeightClasses };
			}
			if (a_name == "weaponType") {
				return { K::Enum, kWeaponAnimTypes };
			}
			if (a_name == "gemSize") {
				return { K::Enum, kSoulGemSizes };
			}
			if (a_name == "status") {
				return { K::Enum, kSoulGemStatuses };
			}
			if (a_name == "bookType") {
				return { K::Enum, kBookTypes };
			}
			if (a_name == "teachesSkill" || a_name == "actorValue" || a_name == "resistance" || a_name == "school") {
				return { K::Enum, kActorValues };
			}
			if (a_name == "iconColor") {
				return { K::Color };
			}
			if (a_name == "flags") {
				switch (a_formType) {
				case ft::kBook:
					return { K::Bitfield, kBookFlags };
				case ft::kAmmo:
					return { K::Bitfield, kAmmoFlags };
				case ft::kPotion:
					return { K::Bitfield, kPotionFlags };
				default:
					return { K::Generic };
				}
			}
			if (a_name == "subType") {
				switch (a_formType) {
				case ft::kArmor:
					return { K::Enum, kEquipTypes };
				case ft::kBook:
					return { K::Enum, kBookSubTypes };
				case ft::kMisc:
					return { K::Enum, kMiscSubTypes };
				case ft::kWeapon:
					return { K::Enum, kWeaponSubTypes };
				case ft::kAmmo:
					return { K::Enum, kAmmoSubTypes };
				case ft::kPotion:
					return { K::Enum, kPotionSubTypes };
				case ft::kSoulGem:
					return { K::Enum, kSoulGemSizes };
				default:
					return { K::Generic };
				}
			}
			// spellType, archetype, deliveryType, castType, effectFlags have I4 parsers too, but those
			// fields are not evaluable here (see kSupportedMatchFields), so their exact parse is moot.
			return { K::Generic };
		}

		struct ParseState
		{
			bool needsRuntimeId{ false };  // a numeric form id: compared with load-order dependent ids
			bool badValue{ false };
		};

		using Sink = std::function<void(const std::string&, Prop)>;

		Prop MatchProp(Value a_value)
		{
			Prop p;
			p.kind = Prop::Kind::Match;
			p.value = std::move(a_value);
			return p;
		}

		void ParseString(const std::string& a_name, const Parser& a_parser, const std::string& a_text, const Sink& a_sink, ParseState& a_state)
		{
			using K = Parser::Kind;
			switch (a_parser.kind) {
			case K::FormType:
				a_sink(a_name, MatchProp(Value::Number(static_cast<double>(Lookup(kFormTypeNames, a_text).value_or(-1)))));
				return;
			case K::FormId:
				if (auto ref = FormRef::Parse(a_text)) {
					a_sink(a_name, MatchProp(Value::Form(std::move(*ref))));
				} else {
					a_sink(a_name, Prop{});  // I4: unknown plugin / bad id -> form id 0, matches nothing
				}
				return;
			case K::Keywords:
				{
					Prop p;
					p.kind = Prop::Kind::Keyword;
					p.keyword = a_text;
					a_sink(a_name, std::move(p));
				}
				return;
			case K::Color:
				if (const auto color = I4Color(a_text)) {
					a_sink(a_name, MatchProp(Value::Number(*color)));
				} else {
					a_state.badValue = true;
					a_sink(a_name, Prop{});
				}
				return;
			case K::Enum:
				if (const auto v = Lookup(a_parser.table, a_text)) {
					a_sink(a_name, MatchProp(Value::Number(static_cast<double>(*v))));
				} else if (IEquals(a_text, "Other")) {
					a_sink(a_name, MatchProp(Value::Undefined()));
				} else {
					a_sink(a_name, MatchProp(Value::String(a_text)));
				}
				return;
			case K::Bitfield:
				if (const auto v = Lookup(a_parser.table, a_text)) {
					Prop p;
					p.kind = Prop::Kind::Bitfield;
					p.bits = static_cast<std::uint32_t>(*v);
					a_sink(a_name, std::move(p));
				} else {
					a_sink(a_name, MatchProp(Value::String(a_text)));
				}
				return;
			case K::Generic:
			case K::Parts:
			case K::MainPart:
			default:
				a_sink(a_name, MatchProp(Value::String(a_text)));
				return;
			}
		}

		void ParseNumber(const std::string& a_name, const Parser& a_parser, double a_number, const Sink& a_sink, ParseState& a_state)
		{
			using K = Parser::Kind;
			switch (a_parser.kind) {
			case K::Parts:
			case K::MainPart:
				{
					// PartsParser / MainPartParser: slot number 30..61 -> bit, stored as "partMask".
					Prop       p;
					const auto part = ToU32(a_number);
					if (part >= 30 && part <= 61) {
						p.kind = a_parser.kind == K::Parts ? Prop::Kind::Bitfield : Prop::Kind::MainPart;
						p.bits = 1u << (part - 30);
					} else {
						a_state.badValue = true;
					}
					a_sink("partMask", std::move(p));
				}
				return;
			case K::FormId:
				a_state.needsRuntimeId = true;
				a_sink(a_name, Prop{});
				return;
			default:
				a_sink(a_name, MatchProp(Value::Number(a_number)));
				return;
			}
		}

		// PropertyParser::Parse
		void ParseSpec(const std::string& a_name, const Parser& a_parser, const json& a_spec, const Sink& a_sink, ParseState& a_state)
		{
			if (a_spec.is_string()) {
				ParseString(a_name, a_parser, a_spec.get<std::string>(), a_sink, a_state);
			} else if (a_spec.is_number()) {
				ParseNumber(a_name, a_parser, a_spec.get<double>(), a_sink, a_state);
			} else if (a_spec.is_boolean()) {
				a_sink(a_name, MatchProp(Value::Bool(a_spec.get<bool>())));
			} else if (a_spec.is_array()) {
				for (const auto& el : a_spec) {
					ParseSpec(a_name, a_parser, el, a_sink, a_state);
				}
			} else if (a_spec.is_object()) {
				if (const auto it = a_spec.find("anyOf"); it != a_spec.end() && it->is_array()) {
					Prop any;
					any.kind = Prop::Kind::AnyOf;
					const Sink anySink = [&any](const std::string&, Prop a_prop) { any.any.push_back(std::move(a_prop)); };
					for (const auto& el : *it) {
						ParseSpec(a_name, a_parser, el, anySink, a_state);
					}
					a_sink(a_name, std::move(any));
				}
				std::optional<double> min;
				std::optional<double> max;
				if (const auto it = a_spec.find("min"); it != a_spec.end() && it->is_number()) {
					min = it->get<double>();
				}
				if (const auto it = a_spec.find("max"); it != a_spec.end() && it->is_number()) {
					max = it->get<double>();
				}
				if (min || max) {
					Prop range;
					range.kind = Prop::Kind::Range;
					range.min = min;
					range.max = max;
					a_sink(a_name, std::move(range));
				}
			} else if (a_spec.is_null()) {
				a_sink(a_name, MatchProp(Value::Undefined()));
			}
		}

		// Entry members this port fills from ItemInfo (or that only rules ever write).
		bool IsSupportedMatchField(std::string_view a_name)
		{
			static constexpr std::array<std::string_view, 32> kSupported{ "formType", "formId", "baseId", "keywords", "value",
				"weight", "text", "count", "isEquipped", "isEnchanted", "iconLabel", "iconSource", "iconColor", "subType", "partMask",
				"parts", "mainPart", "mainPartMask", "weightClass", "weaponType", "equipSlot", "flags", "gemSize", "soulSize",
				"status", "bookType", "teachesSkill", "teachesSpell", "actorValue", "resistance", "school", "effectKeywords" };
			return std::ranges::find(kSupported, a_name) != kSupported.end();
		}

		// CustomDataParser / IconColorParser / SubTypeParser / WeightClassParser
		bool ParseAssign(Rule& a_rule, const std::string& a_name, const json& a_value, int a_formType, ParseState& a_state)
		{
			if (a_value.is_boolean()) {
				a_rule.data.insert_or_assign(a_name, Value::Bool(a_value.get<bool>()));
			} else if (a_value.is_number()) {
				a_rule.data.insert_or_assign(a_name, Value::Number(a_value.get<double>()));
			} else if (a_value.is_string()) {
				const auto text = a_value.get<std::string>();
				if (a_name == "iconColor") {
					if (const auto color = I4Color(text)) {
						a_rule.data.insert_or_assign(a_name, Value::Number(*color));
					} else {
						a_state.badValue = true;
						return false;
					}
				} else if (a_name == "subType" || a_name == "weightClass") {
					Table table{};
					if (a_name == "weightClass") {
						table = kEquipTypes;  // sic: I4's WeightClassParser looks names up in EquipType
					} else {
						switch (a_formType) {
						case ft::kArmor:
							table = kEquipTypes;
							break;
						case ft::kBook:
							table = kBookSubTypes;
							break;
						case ft::kMisc:
							table = kMiscSubTypes;
							break;
						case ft::kWeapon:
							table = kWeaponSubTypes;
							break;
						case ft::kAmmo:
							table = kAmmoSubTypes;
							break;
						case ft::kPotion:
							table = kPotionSubTypes;
							break;
						case ft::kSoulGem:
							table = kSoulGemSizes;
							break;
						default:
							break;
						}
					}
					if (const auto v = table.empty() ? std::nullopt : Lookup(table, text)) {
						a_rule.data.insert_or_assign(a_name, Value::Number(static_cast<double>(*v)));
					} else {
						a_rule.data.insert_or_assign(a_name, Value::String(text));
					}
				} else {
					a_rule.data.insert_or_assign(a_name, Value::String(text));
				}
			} else {
				return false;  // null / array / object: I4 ignores the entry
			}
			return true;
		}

		std::string Join(const std::set<std::string>& a_names)
		{
			std::string out;
			for (const auto& n : a_names) {
				if (!out.empty()) {
					out += ", ";
				}
				out += n;
			}
			return out;
		}

		// Only whitespace and/or a UTF-8 BOM.
		bool IsBlankFile(std::string_view a_text)
		{
			if (a_text.size() >= 3 && static_cast<unsigned char>(a_text[0]) == 0xEF && static_cast<unsigned char>(a_text[1]) == 0xBB &&
				static_cast<unsigned char>(a_text[2]) == 0xBF) {
				a_text.remove_prefix(3);
			}
			return std::ranges::all_of(a_text, [](char a_ch) { return a_ch == ' ' || a_ch == '\t' || a_ch == '\r' || a_ch == '\n'; });
		}

		// Trailing commas -> spaces (outside strings and comments), so nlohmann accepts what jsoncpp does.
		std::string StripTrailingCommas(std::string_view a_text)
		{
			std::string out(a_text);
			const auto  n = out.size();

			const auto skipBlank = [&](std::size_t a_i) {
				while (a_i < n) {
					const char c = out[a_i];
					if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
						++a_i;
					} else if (c == '/' && a_i + 1 < n && out[a_i + 1] == '/') {
						while (a_i < n && out[a_i] != '\n') {
							++a_i;
						}
					} else if (c == '/' && a_i + 1 < n && out[a_i + 1] == '*') {
						a_i += 2;
						while (a_i + 1 < n && !(out[a_i] == '*' && out[a_i + 1] == '/')) {
							++a_i;
						}
						a_i = std::min(n, a_i + 2);
					} else {
						break;
					}
				}
				return a_i;
			};

			for (std::size_t i = 0; i < n; ++i) {
				const char c = out[i];
				if (c == '"') {
					for (++i; i < n && out[i] != '"'; ++i) {
						if (out[i] == '\\') {
							++i;
						}
					}
				} else if (c == '/' && i + 1 < n && (out[i + 1] == '/' || out[i + 1] == '*')) {
					i = skipBlank(i) - 1;
				} else if (c == ',') {
					const auto next = skipBlank(i + 1);
					if (next < n && (out[next] == '}' || out[next] == ']')) {
						out[i] = ' ';
					}
				}
			}
			return out;
		}
	}

	using namespace detail_icons;

	// --- IconRef helpers --------------------------------------------------------------------------

	std::optional<std::uint32_t> ParseIconColor(std::string_view a_text)
	{
		while (!a_text.empty() && (a_text.front() == ' ' || a_text.front() == '\t')) {
			a_text.remove_prefix(1);
		}
		while (!a_text.empty() && (a_text.back() == ' ' || a_text.back() == '\t')) {
			a_text.remove_suffix(1);
		}
		if (!a_text.empty() && a_text.front() == '#') {
			a_text.remove_prefix(1);
		} else if (a_text.size() >= 2 && a_text[0] == '0' && (a_text[1] == 'x' || a_text[1] == 'X')) {
			a_text.remove_prefix(2);
		}
		if (a_text.empty() || a_text.size() > 6) {
			return std::nullopt;
		}
		std::uint32_t value = 0;
		for (const char ch : a_text) {
			std::uint32_t d = 0;
			if (ch >= '0' && ch <= '9') {
				d = static_cast<std::uint32_t>(ch - '0');
			} else if (ch >= 'a' && ch <= 'f') {
				d = static_cast<std::uint32_t>(ch - 'a' + 10);
			} else if (ch >= 'A' && ch <= 'F') {
				d = static_cast<std::uint32_t>(ch - 'A' + 10);
			} else {
				return std::nullopt;
			}
			value = (value << 4) | d;
		}
		return value;
	}

	std::string FormatIconColor(std::uint32_t a_color)
	{
		char buf[8]{};
		std::snprintf(buf, sizeof(buf), "#%06X", static_cast<unsigned>(a_color & 0xFFFFFF));
		return buf;
	}

	std::optional<IconRef> ParseIconRef(const json& a_json, std::string_view a_where, Diagnostics& a_diags)
	{
		const std::string where(a_where);
		IconRef           icon;
		icon.source = std::string(kSkyUIItemIcons);

		if (a_json.is_string()) {
			icon.label = a_json.get<std::string>();
			if (icon.label.empty()) {
				a_diags.Error(where, "icon label is empty");
				return std::nullopt;
			}
			return icon;
		}
		if (!a_json.is_object()) {
			a_diags.Error(where, "expected an icon label string or an object with \"label\", got " + detail::TypeName(a_json));
			return std::nullopt;
		}

		bool ok = true;
		for (const auto& [key, value] : a_json.items()) {
			if (key == "label") {
				if (value.is_string() && !value.get<std::string>().empty()) {
					icon.label = value.get<std::string>();
				} else {
					a_diags.Error(where, ".label: expected a non-empty string");
					ok = false;
				}
			} else if (key == "source") {
				if (value.is_string() && !value.get<std::string>().empty()) {
					icon.source = value.get<std::string>();
					std::ranges::replace(icon.source, '\\', '/');
				} else {
					a_diags.Error(where, ".source: expected a non-empty SWF path relative to Data/Interface");
					ok = false;
				}
			} else if (key == "color") {
				if (value.is_string()) {
					if (const auto color = ParseIconColor(value.get<std::string>())) {
						icon.color = *color;
					} else {
						a_diags.Error(where, ".color: expected \"#RRGGBB\", got \"" + value.get<std::string>() + "\"");
						ok = false;
					}
				} else if (value.is_number_unsigned() && value.get<std::uint64_t>() <= 0xFFFFFF) {
					icon.color = static_cast<std::uint32_t>(value.get<std::uint64_t>());
				} else if (value.is_number_integer() && value.get<std::int64_t>() >= 0 && value.get<std::int64_t>() <= 0xFFFFFF) {
					icon.color = static_cast<std::uint32_t>(value.get<std::int64_t>());
				} else {
					a_diags.Error(where, ".color: expected \"#RRGGBB\"");
					ok = false;
				}
			} else if (key.starts_with('_') || key.starts_with('$')) {
				// comment-style keys
			} else {
				a_diags.Warn(where, "unknown icon field \"" + key + "\" (ignored)");
			}
		}
		if (icon.label.empty()) {
			if (ok) {
				a_diags.Error(where, "missing \"label\"");
			}
			return std::nullopt;
		}
		if (!ok) {
			return std::nullopt;
		}
		return icon;
	}

	json IconRefToJson(const IconRef& a_icon)
	{
		if (a_icon.source == kSkyUIItemIcons && !a_icon.color) {
			return json(a_icon.label);
		}
		json out = json::object();
		out["label"] = a_icon.label;
		if (a_icon.source != kSkyUIItemIcons) {
			out["source"] = a_icon.source;
		}
		if (a_icon.color) {
			out["color"] = FormatIconColor(*a_icon.color);
		}
		return out;
	}

	IconRef SkyUIDefaultIcon(const ItemInfo& a_item)
	{
		auto e = BuildEntry(a_item);
		SkyUIIcon(e);
		e.erase("iconSource");
		return ToIconRef(e, kSkyUIItemIcons);
	}

	std::optional<json> ParseI4Json(std::string_view a_text, std::string_view a_source, Diagnostics& a_diags)
	{
		if (a_text.size() >= 3 && static_cast<unsigned char>(a_text[0]) == 0xEF && static_cast<unsigned char>(a_text[1]) == 0xBB &&
			static_cast<unsigned char>(a_text[2]) == 0xBF) {
			a_text.remove_prefix(3);
		}
		const auto cleaned = StripTrailingCommas(a_text);
		return detail::ParseJson(cleaned, a_source, a_diags);
	}

	// --- IconRules --------------------------------------------------------------------------------

	struct IconRules::Impl
	{
		std::vector<Rule>                     rules;
		std::function<bool(std::string_view)> sourceExists;
		std::unordered_map<std::string, bool> sourceCache;  // lower-cased path -> exists
		std::string                           defaultSource{ kSkyUIItemIcons };
		bool                                  noColors{ false };
		Value                                 noColorDefault;  // iconColor the rules give an empty entry
		std::set<std::string, std::less<>>    assignedNames;   // non-icon members some rule writes

		bool SourceExists(const std::string& a_source)
		{
			if (!sourceExists) {
				return true;
			}
			const auto key = ToLower(a_source);
			if (const auto it = sourceCache.find(key); it != sourceCache.end()) {
				return it->second;
			}
			const bool exists = sourceExists(a_source);
			sourceCache.emplace(key, exists);
			return exists;
		}

		void RecomputeNoColorDefault()
		{
			Entry dummy;
			ProcessEntry(rules, dummy, false);
			noColorDefault = Get(dummy, "iconColor");
		}
	};

	IconRules::IconRules() :
		_impl(std::make_unique<Impl>())
	{}

	IconRules::~IconRules() = default;

	IconRules::IconRules(const IconRules& a_rhs) :
		_impl(a_rhs._impl ? std::make_unique<Impl>(*a_rhs._impl) : std::make_unique<Impl>())
	{}

	IconRules::IconRules(IconRules&& a_rhs) noexcept = default;

	IconRules& IconRules::operator=(const IconRules& a_rhs)
	{
		if (this != &a_rhs) {
			_impl = a_rhs._impl ? std::make_unique<Impl>(*a_rhs._impl) : std::make_unique<Impl>();
		}
		return *this;
	}

	IconRules& IconRules::operator=(IconRules&& a_rhs) noexcept = default;

	std::size_t IconRules::Size() const noexcept
	{
		return _impl ? _impl->rules.size() : 0;
	}

	void IconRules::SetIconSourceValidator(std::function<bool(std::string_view)> a_exists)
	{
		if (!_impl) {
			_impl = std::make_unique<Impl>();
		}
		_impl->sourceExists = std::move(a_exists);
		_impl->sourceCache.clear();
	}

	void IconRules::SetDefaultItemSource(std::string_view a_source)
	{
		if (!_impl) {
			_impl = std::make_unique<Impl>();
		}
		_impl->defaultSource = a_source.empty() ? std::string(kSkyUIItemIcons) : std::string(a_source);
		std::ranges::replace(_impl->defaultSource, '\\', '/');
	}

	void IconRules::SetNoIconColors(bool a_noColors) noexcept
	{
		if (_impl) {
			_impl->noColors = a_noColors;
		}
	}

	void IconRules::AddFileText(std::string_view a_text, std::string_view a_source, Diagnostics& a_diags)
	{
		// An empty file is how patches switch another mod's rules off (MO2 overwrite): no rules, no noise.
		if (IsBlankFile(a_text)) {
			return;
		}
		if (const auto doc = ParseI4Json(a_text, a_source, a_diags)) {
			AddFile(*doc, a_source, a_diags);
		}
	}

	void IconRules::AddFile(const json& a_json, std::string_view a_source, Diagnostics& a_diags)
	{
		if (!_impl) {
			_impl = std::make_unique<Impl>();
		}
		auto&             impl = *_impl;
		const std::string source(a_source);

		if (!a_json.is_object()) {
			a_diags.Warn(source, "not a JSON object; no rules loaded (I4 ignores it too)");
			return;
		}
		const auto rulesIt = a_json.find("rules");
		if (rulesIt == a_json.end() || !rulesIt->is_array()) {
			a_diags.Warn(source, "no \"rules\" array; no rules loaded");
			return;
		}

		// Members rules write: matching on them is evaluable even though SkyUI never sets them.
		for (const auto& rule : *rulesIt) {
			if (!rule.is_object()) {
				continue;
			}
			if (const auto assign = rule.find("assign"); assign != rule.end() && assign->is_object()) {
				for (const auto& [name, value] : assign->items()) {
					if (!name.empty() && name[0] != '$' && !IsIconData(name)) {
						impl.assignedNames.insert(name);
					}
				}
			}
		}

		std::size_t           invalid = 0;
		std::size_t           unsupported = 0;
		std::size_t           badValues = 0;
		std::set<std::string> unsupportedFields;

		for (const auto& ruleJson : *rulesIt) {
			// RuleParser::ParseRule
			if (!ruleJson.is_object()) {
				++invalid;
				continue;
			}
			const auto matchIt = ruleJson.find("match");
			const auto assignIt = ruleJson.find("assign");
			if (matchIt == ruleJson.end() || assignIt == ruleJson.end() || !matchIt->is_object() || !assignIt->is_object()) {
				++invalid;
				continue;
			}
			const auto& match = *matchIt;
			const auto& assign = *assignIt;

			int formType = 0;
			if (const auto ftIt = match.find("formType"); ftIt != match.end() && ftIt->is_string()) {
				formType = static_cast<int>(Lookup(kFormTypeNames, ftIt->get<std::string>()).value_or(0));
			}

			Rule                  rule;
			ParseState            state;
			std::set<std::string> ruleUnsupported;
			const Sink            ruleSink = [&rule](const std::string& a_name, Prop a_prop) { rule.props.insert_or_assign(a_name, std::move(a_prop)); };
			for (const auto& [name, spec] : match.items()) {
				if (name.empty() || name[0] == '$') {
					continue;
				}
				if (!IsSupportedMatchField(name) && !impl.assignedNames.contains(name)) {
					ruleUnsupported.insert(name);
				}
				ParseSpec(name, GetParser(name, formType), spec, ruleSink, state);
			}
			for (const auto& [name, value] : assign.items()) {
				if (name.empty() || name[0] == '$') {
					continue;
				}
				ParseAssign(rule, name, value, formType, state);
			}
			if (state.badValue) {
				++badValues;
			}

			// Rule::Validate
			if (const auto it = rule.data.find("iconSource"); it != rule.data.end()) {
				const bool valid = it->second.kind == Value::Kind::String && impl.SourceExists(it->second.string);
				if (!valid) {
					rule.data.erase(it);
					rule.data.erase("iconLabel");
				}
			} else if (rule.data.contains("iconLabel")) {
				rule.data.insert_or_assign("iconSource", Value::Undefined());  // null: back to the default SWF
			}
			if (rule.props.empty() || rule.data.empty()) {
				++invalid;
				continue;
			}

			// Rules that cannot touch an inventory item (spells, enchantments, unknown form types).
			if (const auto it = rule.props.find("formType"); it != rule.props.end()) {
				const bool anyItem = std::ranges::any_of(kItemFormTypes, [&](int a_type) {
					return PropMatches(it->second, Value::Number(a_type));
				});
				if (!anyItem) {
					continue;
				}
			}
			if (state.needsRuntimeId) {
				ruleUnsupported.insert("formId (numeric)");
			}
			if (!ruleUnsupported.empty()) {
				++unsupported;
				unsupportedFields.insert(ruleUnsupported.begin(), ruleUnsupported.end());
				continue;
			}

			rule.hasInfo = std::ranges::any_of(rule.data, [](const auto& a_kv) { return !IsIconData(a_kv.first); });
			impl.rules.push_back(std::move(rule));
		}

		if (unsupported > 0) {
			a_diags.Warn(source, std::to_string(unsupported) + " rule(s) skipped: they match on fields Dova Stash cannot evaluate (" +
			                         Join(unsupportedFields) + "); container icons may differ from the inventory for those items");
		}
		if (invalid > 0) {
			a_diags.Warn(source, std::to_string(invalid) + " rule(s) ignored as I4 ignores them (no \"match\"/\"assign\" object, nothing usable to match, or nothing left to assign)");
		}
		if (badValues > 0) {
			a_diags.Warn(source, std::to_string(badValues) + " rule(s) contain an unreadable colour or body slot number (that part is ignored)");
		}
		impl.RecomputeNoColorDefault();
	}

	IconRef IconRules::Resolve(const ItemInfo& a_item) const
	{
		static const Impl empty{};
		const Impl&       impl = _impl ? *_impl : empty;

		auto e = BuildEntry(a_item);
		SkyUIIcon(e);  // SkyUI's processList runs before I4's hook
		if (impl.noColors) {
			e.erase("iconColor");
		}
		ProcessEntry(impl.rules, e, true);
		if (impl.noColors && e.contains("iconColor")) {
			if (impl.noColorDefault.kind == Value::Kind::Undefined) {
				e.erase("iconColor");
			} else {
				Set(e, "iconColor", impl.noColorDefault);
			}
		}
		return ToIconRef(e, impl.defaultSource);
	}
}
