#pragma once

// Displays (DESIGN §8.4): weapon racks, shield plaques, mannequins, plaques made for one item (masks,
// claws, Elder Scrolls) and scripted display cases. Deposit and Sort all put gear on the empty ones, so a
// home looks as if the player had placed it by hand. Core picks what goes where; the plugin finds the
// displays in the cell, places the items and never takes anything off a display.
//
// Decisions:
// - One copy of each base form on display in the whole home: forms already shown (loose weapons and
//   armor in the cell, mannequin outfits, full plaques, case contents) are not put up again.
// - Items a plaque or a case is made for are reserved for it: racks and mannequins never take them.
// - Item plaques swap the item for a model and recreate it when taken back, so they only get copies
//   without extra data of their own (`ItemInfo::customized`). Racks, mannequins and cases keep the very
//   copy (tempering, enchantment, name).
// - Order: item plaques, cases, mannequins (one matching set each), shield plaques, dagger racks, racks
//   without daggers or bows, then racks for any weapon. Each takes the best remaining item: preferred
//   items (the display's `items`) first, then Daedric artifacts, then the highest value; the player's
//   own copy before a stored one; ties keep input order.
// - Mannequins take one set: the armor sharing a material keyword (ArmorMaterial*) with the highest total
//   value that includes a body piece. Each slot is filled once (no overlapping slot masks). Shields,
//   jewelry other than circlets, and armor without a standard slot are left for other displays.

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include "dova/core/DisplayKind.h"
#include "dova/core/FormRef.h"
#include "dova/core/Item.h"
#include "dova/core/SortPlanner.h"

namespace dova::core
{
	// Node of a vanilla rack trigger that holds `a_item` ("SwordPivot01"), empty when no rack takes it.
	[[nodiscard]] std::string_view RackPivot(const ItemInfo& a_item);
	[[nodiscard]] bool             RackTakes(RackType a_rack, const ItemInfo& a_item);
	[[nodiscard]] bool             MannequinTakes(const ItemInfo& a_item);

	struct DisplaySpot
	{
		FormRef              ref;
		DisplayKind          kind{ DisplayKind::Rack };
		RackType             rack{ RackType::Any };  // Rack only
		std::vector<FormRef> items;  // Item/Case: what it is made for. Rack/Mannequin: preferred items
		bool                 full{ false };  // Rack/Mannequin/Item: something is on it already
		std::vector<FormRef> holds;  // Case: what is inside already
	};

	// A stack that may go on display: `available` copies of `item`, held by `from`.
	struct DisplayStock
	{
		ContainerRef from;
		ItemInfo     item;
		std::int32_t available{ 0 };
	};

	struct DisplayAssignment
	{
		std::size_t  spot{ 0 };   // index into the spots
		std::size_t  stock{ 0 };  // index into the stocks
		std::int32_t count{ 1 };

		friend bool operator==(const DisplayAssignment&, const DisplayAssignment&) = default;
	};

	// What goes on which display. `a_shown`: base forms already on display in the home.
	[[nodiscard]] std::vector<DisplayAssignment> PlanDisplays(std::span<const DisplaySpot> a_spots, std::span<const DisplayStock> a_stocks,
		const std::set<FormRef>& a_shown);

	// Takes what goes on display out of the moves: every assignment lowers the move of its stack (same
	// stack id and holder; dropped at zero).
	void ReserveForDisplays(std::vector<Move>& a_moves, std::span<const DisplayStock> a_stocks, std::span<const DisplayAssignment> a_assignments);
}
