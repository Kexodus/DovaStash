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
// - A rack or mannequin with categories (patterns, as for containers) only takes items in one of them; the
//   item's categories are every category it matches (CategorySet::MatchAll), like containers use.
// - Item plaques swap the item for a model and recreate it when taken back, so they only get copies
//   without extra data of their own (`ItemInfo::customized`). Racks, mannequins and cases keep the very
//   copy (tempering, enchantment, name).
// - Racks take what WeaponRackActivateSCRIPT takes (RackTakes), on the node it picks from the item's
//   keywords (RackPivot). Crossbows only go on racks whose trigger has a crossbow node (the Unofficial
//   Patch's), and a rack never gets a form its script refuses there (DisplaySpot::refuses: the plugin
//   reads that from the rack's script and its trigger's nodes).
// - Order: item plaques, cases, mannequins (one matching set each), shield plaques, dagger racks, racks
//   without daggers or bows, large display cases, racks for any weapon, then bookshelves. Each takes the best remaining item: preferred
//   items (the display's `items`) first, then Daedric artifacts, then the highest value; the player's
//   own copy before a stored one; ties keep input order.
// - Mannequins take one set (ArmorSets.h: same plugin and material keyword or name stem): the one with a
//   body piece covering the most slot groups (head, body, hands, feet), then the highest total value. Each
//   slot is filled once (no overlapping slot masks). Shields, jewelry other than circlets, and armor
//   without a standard slot are left for other displays.
// - A mannequin with a chosen set (DisplaySpot::set, the layout's `set`) takes only pieces of it, and
//   goes before the others. It takes more while it wears nothing else and has free slots (`holds`,
//   `worn`), so a set fills up as the pieces arrive.
// - A rack or mannequin with `only` (DisplaySpot::only) takes nothing but its `items`, and those are kept
//   for it like a plaque's: no other rack or mannequin takes them. Such a mannequin is filled like one with
//   a chosen set made of its items (unless it has a chosen set, which decides). With none of its items
//   here (or in the load order) it gets nothing: it waits for them, as a chosen set does.
// - Bookshelves (PlayerBookShelfContainerScript and home mods' copies) take books only, never more than the
//   room their script leaves (BookshelfRoom), and only books worth showing: never an Elder Scroll, never a
//   copy with extra data of its own (the script shows a plain copy and destroys the stored one when the book
//   is taken off the shelf), never items a plaque or case is made for; without categories no notes or
//   letters. Its preferred `items` first (every shelf takes its own before any shelf fills up), then skill
//   books, then spell tomes, then the highest value. With `only` it takes nothing but its `items`. One copy
//   of each title in the home, as for every display (books already on a shelf are `holds`).

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dova/core/DisplayKind.h"
#include "dova/core/FormRef.h"
#include "dova/core/Item.h"
#include "dova/core/SortPlanner.h"

namespace dova::core
{
	// Node of a rack trigger that holds `a_item` ("SwordPivot01"), empty when no rack takes it. Like
	// WeaponRackActivateSCRIPT: by the first WeapType* keyword in the script's order (sword, war axe, mace,
	// greatsword, battleaxe, warhammer, bow, staff, dagger), else by the weapon type; ArmorShield is a
	// shield. Crossbows (WeapTypeBow) get BowPivot01. The Unofficial Patch's script picks other nodes for
	// some bows, staves and shields from the item's own 3D; the plugin refines this choice
	// (game::RackNode).
	[[nodiscard]] std::string_view RackPivot(const ItemInfo& a_item);
	// Whether a rack of type `a_rack` takes `a_item`. `a_crossbows`: its trigger has a crossbow node (the
	// Unofficial Patch's racks); crossbows go nowhere else.
	[[nodiscard]] bool             RackTakes(RackType a_rack, const ItemInfo& a_item, bool a_crossbows = false);
	[[nodiscard]] bool             MannequinTakes(const ItemInfo& a_item);
	// Whether a bookshelf may show `a_item` at all: a book without extra data of its own that is not an
	// Elder Scroll. (Notes and letters too; a shelf without categories leaves them out, see PlanDisplays.)
	[[nodiscard]] bool             BookshelfTakes(const ItemInfo& a_item);

	// What a bookshelf's script holds, as the plugin reads it (PlayerBookShelfContainerScript: OnItemAdded
	// accepts a book while CurrentBookAmount + count <= MaxBooksAllowed and writes it into the first empty
	// PlacedBookNN slot; a book over the limit goes back to the player).
	struct BookshelfState
	{
		std::int32_t      capacity{ 0 };  // MaxBooksAllowed: its book markers, counted by the script on its first cell load
		std::int32_t      markers{ -1 };  // book markers linked to the shelf (BookShelfBook01... in a row), counted by the plugin; -1 unknown
		std::int32_t      counted{ 0 };   // CurrentBookAmount: the books the script counts
		std::vector<bool> slots;          // PlacedBook01...: which slots hold a book
		std::int32_t      inside{ 0 };    // books in the shelf's container
	};
	// How many more books the shelf takes without its script handing any back or placing one on a slot
	// without a marker: the smallest of what its count allows (capacity - counted), its empty slots among the
	// first `capacity` (a slot it lacks counts as empty), and capacity - inside. `capacity` is never more
	// than `markers` when they are known. Never negative.
	[[nodiscard]] std::int32_t BookshelfRoom(const BookshelfState& a_state);

	struct DisplaySpot
	{
		FormRef                  ref;
		DisplayKind              kind{ DisplayKind::Rack };
		RackType                 rack{ RackType::Any };  // Rack only
		std::vector<FormRef>     items;       // Item/Case: what it is made for. Rack/Mannequin/Bookshelf: preferred items
		std::vector<std::string> categories;  // Rack/Mannequin/Bookshelf: patterns it takes (empty: anything that fits)
		bool                     full{ false };  // Rack/Mannequin/Item: something is on it already. Bookshelf: no room
		std::vector<FormRef>     holds;  // Case/Bookshelf: what is inside already. Mannequin: the armor it wears
		bool                     crossbows{ false };  // Rack: its trigger has a crossbow node
		std::vector<FormRef>     refuses;  // Rack: forms its script refuses here (staves that do not fit)
		std::vector<FormRef>     set;  // Mannequin: the pieces of its chosen set (empty: it picks a set itself)
		std::uint32_t            worn{ 0 };  // Mannequin with a set: slots taken by what it wears (`holds`)
		bool                     chosen{ false };  // Mannequin: it has a chosen set (even one with no known pieces)
		bool                     only{ false };  // Rack/Mannequin/Bookshelf: takes nothing but its `items` (the layout's "only"),
		                                         // which are kept for it; with no `items` left it gets nothing
		std::int32_t             room{ 0 };  // Bookshelf: how many more books it takes (BookshelfRoom)
	};

	// A stack that may go on display: `available` copies of `item`, held by `from`.
	struct DisplayStock
	{
		ContainerRef             from;
		ItemInfo                 item;
		std::int32_t             available{ 0 };
		std::vector<std::string> categories;  // category ids the item matches (needed by spots with categories)
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
