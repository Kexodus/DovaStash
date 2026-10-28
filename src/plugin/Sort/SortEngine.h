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
		Retrieve,    // destinations -> player, by category pattern
		Restock      // destinations -> player, up to the restocking keep quotas (DESIGN §8.7)
	};

	[[nodiscard]] std::string_view OpName(Op a_op) noexcept;

	struct Result
	{
		bool        ran{ false };     // false: nothing was attempted (see message)
		std::int32_t items{ 0 };
		std::string message;          // one HUD line
		bool        failed{ false };  // the action could not run (the panel shows `message` red)
	};

	// `a_pattern` is used by Retrieve only; `a_source` by Distribute only: the master (the default) or an area
	// master (Layout::IsAnyMaster, DESIGN §6.7), which is an inbox too. The home must be the player's current cell. Deposit restocks
	// afterwards, in the same pass, when [Sorting] bRestockAfterDeposit is on and a quota rule restocks; what
	// goes on display is never restocked.
	[[nodiscard]] Result Run(home::Home& a_home, Op a_op, std::string_view a_pattern = {}, const std::optional<core::FormRef>& a_source = std::nullopt);

	// A keep quota in force in this home restocks (the hotkey menus then offer Restock).
	[[nodiscard]] bool Restocks(const home::Home& a_home);

	// Fills one display the layout uses (DisplayFiller; "Fill now" in its menu): from what Deposit would
	// move off the player and what the managed containers hold. `message` says why nothing
	// went up; what does go up is reported by the filler.
	[[nodiscard]] Result FillDisplay(home::Home& a_home, const core::FormRef& a_display);

	// Item count per primary category id ("" = homeless) across the home's managed, available
	// containers. Used by the Retrieve menu.
	[[nodiscard]] std::map<std::string, std::int32_t> CountByCategory(home::Home& a_home);

	// Learned destinations pointing at `a_container`, counted per primary category id of the item (items
	// that resolve to no category are left out). The category picker and the learned-only icon use it.
	[[nodiscard]] std::map<std::string, std::int32_t> LearnedByCategory(const core::Layout& a_layout, const core::FormRef& a_container);
}
