#pragma once

// Armor sets (DESIGN §8.4 "Mannequins"): which armor pieces belong together, for mannequins. Works from
// plain item data, so it covers modded armor that has no vanilla keywords.
//
// Decisions:
// - Every piece belongs to exactly one set. Pieces are first split by plugin (the file that defines the
//   form) and material keyword (the first keyword containing "ArmorMaterial", e.g. ArmorMaterialEbony,
//   DLC2ArmorMaterialStalhrimHeavy; none is fine). A mod that tags its own armor with a vanilla material
//   keyword therefore gets its own set, and so does every plugin.
// - Within that, pieces sharing a name stem form a set (ArmorNameStem: "Ebony Cuirass" and "Ebony Boots"
//   -> "Ebony"; "Armor of the Old Gods" -> "Old Gods"; "Ebony Armor of Major Health" -> "Ebony"). A stem
//   joins a shorter stem that starts it ("Steel Horned" joins "Steel"). With a material keyword, stems
//   that share their first word are one set too ("Ebony Mail" is Ebony).
// - Pieces left alone that way (no keyword, no name shared) are put together by `family` when the plugin
//   supplies one (the item that tempers them), one piece per slot group, so a mod's "Knight Cuirass",
//   "Sallet" and "Sabatons" still make a set.
// - A set is named after its stem ("Ebony"); a set of one piece after the piece. Names are made unique:
//   " (Light)" / " (Heavy)" / " (Clothing)", then " (Plugin.esp)".
// - A set's id (the layout's `set`, §6.3) is one of its pieces: its first body piece in FormRef order,
//   else its first piece. Ids survive load-order changes and game languages; any piece of a set names it.
// - What a mannequin shows: four slot groups (head: head, hair, circlet; body; hands: hands, forearms;
//   feet: feet, calves). PickOutfit puts one piece on each slot.

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dova/core/FormRef.h"
#include "dova/core/Item.h"

namespace dova::core
{
	// Slot groups (masks over ItemInfo::armorSlots, bit 0 = slot 30).
	inline constexpr std::uint32_t kSetSlotHead = (1u << 0) | (1u << 1) | (1u << 12);  // 30 head, 31 hair, 42 circlet
	inline constexpr std::uint32_t kSetSlotBody = 1u << 2;                             // 32 body
	inline constexpr std::uint32_t kSetSlotHands = (1u << 3) | (1u << 4);              // 33 hands, 34 forearms
	inline constexpr std::uint32_t kSetSlotFeet = (1u << 7) | (1u << 8);               // 37 feet, 38 calves
	inline constexpr std::uint32_t kSetSlots = kSetSlotHead | kSetSlotBody | kSetSlotHands | kSetSlotFeet;

	// How many of the four slot groups `a_slots` touches (0-4).
	[[nodiscard]] int SlotGroupCount(std::uint32_t a_slots) noexcept;

	struct ArmorPiece
	{
		FormRef                  form;
		std::string              name;
		std::vector<std::string> keywords;  // keyword EditorIDs
		std::optional<ArmorType> armorType;
		std::uint32_t            slots{ 0 };
		std::int32_t             value{ 0 };
		std::string              family;  // pieces sharing no keyword or name: what the plugin knows ties them ("" = nothing)
	};

	[[nodiscard]] ArmorPiece ArmorPieceOf(const ItemInfo& a_item);

	// The part of an armor name that names its set: "Ebony" for "Ebony Cuirass", "Old Gods" for "Armor
	// of the Old Gods", "Ebony" for "Ebony Armor of Major Health"; empty when the name is only piece words
	// ("Boots"). Bracketed parts ("[Heavy]") are left out.
	[[nodiscard]] std::string ArmorNameStem(std::string_view a_name);
	// The first keyword containing "ArmorMaterial" (any case), or empty.
	[[nodiscard]] std::string ArmorMaterialKeyword(const ArmorPiece& a_piece);

	// The sets, as indices into `a_pieces`; every piece in exactly one. Members in input order; sets in
	// the order of their first member.
	[[nodiscard]] std::vector<std::vector<std::size_t>> GroupArmorPieces(std::span<const ArmorPiece> a_pieces);

	struct ArmorSet
	{
		FormRef                  id;        // see the header comment
		std::string              name;      // "Ebony", unique among the sets built together
		std::string              plugin;    // the file that defines the pieces
		std::string              material;  // material keyword ("" = none)
		std::vector<std::size_t> pieces;    // indices into the pieces
		std::uint32_t            slots{ 0 };  // every slot its pieces cover
		std::optional<ArmorType> armorType;   // the most common type among its pieces
	};

	// GroupArmorPieces with names and ids, sorted by name (case-insensitive), then id.
	[[nodiscard]] std::vector<ArmorSet> BuildArmorSets(std::span<const ArmorPiece> a_pieces);

	// What a mannequin wears from `a_order` (indices into `a_pieces`, best first), one piece per slot: a
	// body piece first (the best one whose slots are free), then the rest, those of the body piece's
	// armor type first. Slots in `a_worn` are taken already. Empty when `a_needBody` and no body piece fits.
	[[nodiscard]] std::vector<std::size_t> PickOutfit(std::span<const ArmorPiece> a_pieces, std::span<const std::size_t> a_order, std::uint32_t a_worn = 0,
		bool a_needBody = true);

	// "(3/4)" in the menus: slot groups the set covers, and those the pieces in `a_have` cover.
	struct SetCoverage
	{
		int have{ 0 };
		int total{ 0 };
		int pieces{ 0 };  // distinct pieces of the set in `a_have`

		[[nodiscard]] bool Complete() const noexcept { return total > 0 && have >= total; }
	};
	[[nodiscard]] SetCoverage CoverageOf(const ArmorSet& a_set, std::span<const ArmorPiece> a_pieces, const std::set<FormRef>& a_have);

	// Search: every word of `a_query` appears (any case) in the set's name, its plugin or a piece's name.
	// An empty query matches everything.
	[[nodiscard]] bool ArmorSetMatches(const ArmorSet& a_set, std::span<const ArmorPiece> a_pieces, std::string_view a_query);

	// A mannequin with a chosen set (`a_set`: its pieces) can take more of it: it wears nothing outside the
	// set (`a_holds`), and a piece fits the slots it leaves free (`a_worn`).
	[[nodiscard]] bool SetMannequinOpen(std::span<const ArmorPiece> a_set, std::span<const FormRef> a_holds, std::uint32_t a_worn);
}
