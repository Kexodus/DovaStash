#pragma once

// Core <-> plugin boundary. The plugin fills ItemInfo from game objects; core never sees RE:: types.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "dova/core/FormRef.h"

namespace dova::core
{
	// Subset of Skyrim form types that can live in a container. Names match the record signatures
	// used in category JSON ("formTypes": ["WEAP", ...]).
	enum class ItemType : std::uint8_t
	{
		Weapon,      // WEAP
		Armor,       // ARMO
		Ammo,        // AMMO
		Alchemy,     // ALCH (potions, poisons, food)
		Ingredient,  // INGR
		Book,        // BOOK
		Scroll,      // SCRL
		SoulGem,     // SLGM
		Misc,        // MISC
		Key,         // KEYM
		Light,       // LIGH (torches)
		Other
	};

	enum class WeaponType : std::uint8_t
	{
		HandToHand,
		OneHandSword,
		OneHandDagger,
		OneHandAxe,
		OneHandMace,
		TwoHandSword,
		TwoHandAxe,  // battleaxes and warhammers share this engine type; distinguish via keywords
		Bow,
		Staff,
		Crossbow
	};

	enum class ArmorType : std::uint8_t
	{
		Light,
		Heavy,
		Clothing
	};

	enum class BookTeaches : std::uint8_t
	{
		None,
		Skill,
		Spell
	};

	enum class AlchemyKind : std::uint8_t
	{
		Potion,
		Poison,
		Food
	};

	enum class SoulGemState : std::uint8_t
	{
		Empty,
		Filled
	};

	enum class AmmoKind : std::uint8_t
	{
		Arrow,
		Bolt
	};

	// One inventory stack as seen by core. A single base form may appear as several ItemInfo entries
	// when the game holds distinct extra-data stacks (tempered, enchanted, renamed...). `stackId` is an
	// opaque handle the plugin uses to find that exact stack again when executing a Move.
	struct ItemInfo
	{
		FormRef                  form;
		std::uint32_t            stackId{ 0 };
		ItemType                 type{ ItemType::Other };
		std::string              name;
		std::int32_t             count{ 0 };
		float                    weight{ 0.0f };
		std::int32_t             value{ 0 };
		std::vector<std::string> keywords;  // keyword EditorIDs on the base form

		std::optional<WeaponType>   weaponType;
		std::optional<ArmorType>    armorType;
		std::optional<BookTeaches>  bookTeaches;
		std::optional<AlchemyKind>  alchemyKind;
		std::optional<SoulGemState> soulGemState;
		std::optional<AmmoKind>     ammoKind;

		bool enchanted{ false };
		bool questItem{ false };
		bool equipped{ false };
		bool favorited{ false };
		bool playable{ true };
	};
}
