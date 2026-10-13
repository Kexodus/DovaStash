#pragma once

// "Where items go" (docs/EDITOR.md §3.5): sample inventories built from the load order's items and the
// displays of a home as core's display planner sees them, for core::PreviewWhereItemsGo.

#include <span>
#include <string>
#include <vector>

#include "dova/core/Category.h"
#include "dova/core/Display.h"
#include "dova/core/Layout.h"
#include "dova/core/Preview.h"
#include "esp/Items.h"
#include "home/Cell.h"

namespace dova::editor::home
{
	enum class SampleKind
	{
		Haul,      // a typical haul after a few dungeons: a handful of each kind of loot, base game items first
		Category,  // every playable item of the load order in a category pattern ("alchemy.*")
		All        // every playable item of the load order, one of each
	};

	[[nodiscard]] const char* SampleName(SampleKind a_kind);

	// The sample as inventory stacks (stack ids are the catalog indices). `a_pattern` for SampleKind::Category.
	[[nodiscard]] std::vector<core::ItemInfo> MakeSample(const esp::ItemCatalog& a_catalog, const core::CategorySet& a_categories, SampleKind a_kind,
		const std::string& a_pattern = {});

	// The displays the layout uses (listed, not ignored) as display spots: kind and rack type from the cells' references,
	// items, categories and `only` from the layout. Mannequins pick a set themselves (a chosen `set` is not resolved here).
	[[nodiscard]] std::vector<core::DisplaySpot> DisplaySpots(esp::LoadOrder& a_lo, std::span<const CellInfo> a_cells, const core::Layout& a_layout);

	// How full a container is in the preview, for the colours of the 3D view and the plan.
	enum class Fill
	{
		None,    // not in the preview (not listed, ignored)
		Empty,   // receives nothing
		Light,   // a few items
		Heavy,   // many items
		Master   // the master (what has no place of its own)
	};
	[[nodiscard]] Fill FillOf(const core::PreviewResult& a_result, const core::FormRef& a_ref, std::int32_t a_heavyAt);
}
