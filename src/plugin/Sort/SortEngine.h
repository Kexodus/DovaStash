#pragma once

// Sort operations (DESIGN §8.1): snapshot inventories -> core::SortPlanner -> ItemMover, all inside
// one main-thread task so the stack table never goes stale between planning and moving.
//
// TODO(M5): chunk very large operations across frames (DESIGN §5 "Threading") if hitches show up.

#include "Home/HomeRegistry.h"

namespace dova::sort
{
	enum class Op : std::uint8_t
	{
		Deposit,     // player -> destinations (keep rules)
		Distribute,  // master -> destinations
		SortAll,     // every managed container -> its destination
		Retrieve     // destinations -> player, by category pattern
	};

	[[nodiscard]] std::string_view OpName(Op a_op) noexcept;

	struct Result
	{
		bool        ran{ false };     // false: nothing was attempted (see message)
		std::int32_t items{ 0 };
		std::string message;          // one HUD line
	};

	// `a_pattern` is used by Retrieve only. The home must be the player's current cell.
	[[nodiscard]] Result Run(home::Home& a_home, Op a_op, std::string_view a_pattern = {});

	// Fills one display the layout uses (DisplayFiller; "Fill now" in its menu): from what Deposit would
	// move off the player and what the managed containers hold. `message` says why nothing
	// went up; what does go up is reported by the filler.
	[[nodiscard]] Result FillDisplay(home::Home& a_home, const core::FormRef& a_display);

	// Item count per primary category id ("" = homeless) across the home's managed, available
	// containers. Used by the Retrieve menu.
	[[nodiscard]] std::map<std::string, std::int32_t> CountByCategory(home::Home& a_home);
}
