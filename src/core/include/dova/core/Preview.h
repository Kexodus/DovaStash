#pragma once

// "Where items go" (docs/EDITOR.md §3.5): what a layout would do with a pile of items, worked out by the same
// planner the DLL runs (SortPlanner::ResolveDestination: explicit item bindings, learned destinations,
// categories best-first, then the master) and the same displays planner (PlanDisplays) for the displays the
// layout uses. Nothing is moved; the result says, per container, which items it would receive and why.
//
// Decisions:
// - Every input stack is one entry: items that can never be moved (quest, non-playable, nameless, count 0) are
//   listed apart, as are items with no destination at all (no master and no category container).
// - Displays first, as Deposit does: an item a display takes (one copy) is not counted for a container; the
//   rest of its stack goes where the planner sends it.
// - `byContainer` has an entry for the master and for every container of the layout that is not ignored, also
//   the ones that receive nothing (`items` empty), in layout order (master first).

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "dova/core/Category.h"
#include "dova/core/Display.h"
#include "dova/core/FormRef.h"
#include "dova/core/Item.h"
#include "dova/core/Layout.h"
#include "dova/core/SortPlanner.h"

namespace dova::core
{
	struct PreviewEntry
	{
		std::size_t         item{ 0 };   // index into the input items
		std::int32_t        count{ 0 };
		Destination::Reason reason{ Destination::Reason::Master };
		std::string         category;  // the category that decided it; for the master: the item's best category ("" homeless)
	};

	struct PreviewContainer
	{
		FormRef                             ref;
		bool                                master{ false };
		std::vector<PreviewEntry>           items;
		std::int32_t                        total{ 0 };  // copies
		std::map<std::string, std::int32_t> perCategory;  // copies per category ("" = no category: homeless)
	};

	struct PreviewDisplay
	{
		FormRef     ref;
		std::size_t item{ 0 };
	};

	struct PreviewResult
	{
		std::vector<PreviewContainer> byContainer;
		std::vector<PreviewDisplay>   onDisplays;
		std::vector<std::size_t>      neverMoved;     // quest, non-playable, nameless, empty stacks
		std::vector<std::size_t>      noDestination;  // nowhere to go (no master)
		std::int32_t                  toMaster{ 0 };      // copies that fall to the master without a category binding
		std::int32_t                  toContainers{ 0 };  // copies placed by explicit, learned or category bindings

		[[nodiscard]] const PreviewContainer* Find(const FormRef& a_ref) const;
	};

	// `a_spots`: the displays the layout uses, as the caller sees them (kind, rack type, items, categories).
	[[nodiscard]] PreviewResult PreviewWhereItemsGo(const Layout& a_layout, const CategorySet& a_categories, std::span<const ItemInfo> a_items,
		std::span<const DisplaySpot> a_spots = {});
}
