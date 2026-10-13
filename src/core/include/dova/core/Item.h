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
		// The item this one is made from, when it is a variant of another (an enchanted "Iron Sword of
		// Burning" made from the Iron Sword: WEAP CNAM / ARMO TNAM, followed to the root). Learned
		// destinations and fixed item bindings of that root item also apply to its variants (DESIGN §7).
		// nullopt: the item is its own root. The plugin fills it; core only compares it.
		std::optional<FormRef>   family;
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
		bool returns{ false };  // a script sent it back the last time it was stored: never moved
		bool customized{ false };  // this copy differs from its base form: tempered, renamed, enchanted by the player, poisoned
		bool elderScroll{ false };  // BOOK: an Elder Scroll (its model is an Elder Scroll's): never put on a bookshelf

		// --- UI detail (icon resolution, Icons.h) ------------------------------------------------
		// What SKSE's scaleform extension puts on a SkyUI inventory entry, so SkyUI's default icon
		// logic and I4 icon rules can be evaluated in core. Sorting never reads these. Defaults mean
		// "unknown"; Icons.cpp then falls back to the coarse fields above where it can.
		std::uint32_t               armorSlots{ 0 };     // ARMO: biped slot mask (bit 0 = slot 30); 0 = unknown
		std::uint32_t               typeFlags{ 0 };      // AMMO: AMMO_DATA flags; ALCH: AlchemyFlag; BOOK: OBJ_BOOK flags
		bool                        bookIsNote{ false };  // BOOK: note/letter (type 0xFF, or "note" in the model path)
		std::optional<FormRef>      equipSlot;           // WEAP: BGSEquipSlot
		std::optional<FormRef>      useSound;            // ALCH: consumption sound (SkyUI tells drinks from food by it)
		std::optional<std::int32_t> actorValue;          // ALCH/INGR/SCRL: primary actor value of the costliest effect
		std::optional<std::int32_t> resistance;          // ALCH/INGR/SCRL: resist actor value of the costliest effect
		std::optional<std::int32_t> magicSchool;         // ALCH/INGR/SCRL: magic skill of the costliest effect
		std::vector<std::string>    effectKeywords;      // ALCH/INGR/SCRL: keyword EditorIDs of every effect's base effect
		std::optional<std::int32_t> teachesSkill;        // BOOK: actor value of the skill it raises
		std::optional<FormRef>      teachesSpell;        // BOOK: spell it teaches
		std::optional<std::uint8_t> gemSize;             // SLGM: capacity (0 none, 1 petty .. 5 grand)
		std::optional<std::uint8_t> soulSize;            // SLGM: soul held by this stack (0 none .. 5 grand)
	};
}
