#include "dova/core/Enums.h"

#include <array>
#include <utility>

namespace dova::core
{
	namespace
	{
		template <class E, std::size_t N>
		using Table = std::array<std::pair<E, std::string_view>, N>;

		constexpr Table<ItemType, 11> kItemTypes{ {
			{ ItemType::Weapon, "WEAP" },
			{ ItemType::Armor, "ARMO" },
			{ ItemType::Ammo, "AMMO" },
			{ ItemType::Alchemy, "ALCH" },
			{ ItemType::Ingredient, "INGR" },
			{ ItemType::Book, "BOOK" },
			{ ItemType::Scroll, "SCRL" },
			{ ItemType::SoulGem, "SLGM" },
			{ ItemType::Misc, "MISC" },
			{ ItemType::Key, "KEYM" },
			{ ItemType::Light, "LIGH" },
		} };

		constexpr Table<WeaponType, 10> kWeaponTypes{ {
			{ WeaponType::HandToHand, "HandToHand" },
			{ WeaponType::OneHandSword, "OneHandSword" },
			{ WeaponType::OneHandDagger, "OneHandDagger" },
			{ WeaponType::OneHandAxe, "OneHandAxe" },
			{ WeaponType::OneHandMace, "OneHandMace" },
			{ WeaponType::TwoHandSword, "TwoHandSword" },
			{ WeaponType::TwoHandAxe, "TwoHandAxe" },
			{ WeaponType::Bow, "Bow" },
			{ WeaponType::Staff, "Staff" },
			{ WeaponType::Crossbow, "Crossbow" },
		} };

		constexpr Table<ArmorType, 3> kArmorTypes{ {
			{ ArmorType::Light, "Light" },
			{ ArmorType::Heavy, "Heavy" },
			{ ArmorType::Clothing, "Clothing" },
		} };

		constexpr Table<BookTeaches, 3> kBookTeaches{ {
			{ BookTeaches::None, "None" },
			{ BookTeaches::Skill, "Skill" },
			{ BookTeaches::Spell, "Spell" },
		} };

		constexpr Table<AlchemyKind, 3> kAlchemyKinds{ {
			{ AlchemyKind::Potion, "Potion" },
			{ AlchemyKind::Poison, "Poison" },
			{ AlchemyKind::Food, "Food" },
		} };

		constexpr Table<SoulGemState, 2> kSoulGemStates{ {
			{ SoulGemState::Empty, "Empty" },
			{ SoulGemState::Filled, "Filled" },
		} };

		constexpr Table<AmmoKind, 2> kAmmoKinds{ {
			{ AmmoKind::Arrow, "Arrow" },
			{ AmmoKind::Bolt, "Bolt" },
		} };

		template <class E, std::size_t N>
		std::optional<E> Lookup(const Table<E, N>& a_table, std::string_view a_name)
		{
			for (const auto& [value, name] : a_table) {
				if (IEquals(name, a_name)) {
					return value;
				}
			}
			return std::nullopt;
		}

		template <class E, std::size_t N>
		std::string_view Name(const Table<E, N>& a_table, E a_value) noexcept
		{
			for (const auto& [value, name] : a_table) {
				if (value == a_value) {
					return name;
				}
			}
			return {};
		}
	}

	std::optional<ItemType>     ParseItemType(std::string_view a_signature) { return Lookup(kItemTypes, a_signature); }
	std::optional<WeaponType>   ParseWeaponType(std::string_view a_name) { return Lookup(kWeaponTypes, a_name); }
	std::optional<ArmorType>    ParseArmorType(std::string_view a_name) { return Lookup(kArmorTypes, a_name); }
	std::optional<BookTeaches>  ParseBookTeaches(std::string_view a_name) { return Lookup(kBookTeaches, a_name); }
	std::optional<AlchemyKind>  ParseAlchemyKind(std::string_view a_name) { return Lookup(kAlchemyKinds, a_name); }
	std::optional<SoulGemState> ParseSoulGemState(std::string_view a_name) { return Lookup(kSoulGemStates, a_name); }
	std::optional<AmmoKind>     ParseAmmoKind(std::string_view a_name) { return Lookup(kAmmoKinds, a_name); }

	std::string_view ToString(ItemType a_value) noexcept { return Name(kItemTypes, a_value); }
	std::string_view ToString(WeaponType a_value) noexcept { return Name(kWeaponTypes, a_value); }
	std::string_view ToString(ArmorType a_value) noexcept { return Name(kArmorTypes, a_value); }
	std::string_view ToString(BookTeaches a_value) noexcept { return Name(kBookTeaches, a_value); }
	std::string_view ToString(AlchemyKind a_value) noexcept { return Name(kAlchemyKinds, a_value); }
	std::string_view ToString(SoulGemState a_value) noexcept { return Name(kSoulGemStates, a_value); }
	std::string_view ToString(AmmoKind a_value) noexcept { return Name(kAmmoKinds, a_value); }
}
