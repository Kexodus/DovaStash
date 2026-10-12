#pragma once

// Putting gear on the home's displays (DESIGN §8.4). Only the displays the layout uses are filled: the
// ones the player turned on with the hotkey, or that a template lists. Deposit and Sort all plan them
// before their own moves (core::PlanDisplays, so what goes up is taken out of the deposit); "Fill now" in
// a display's menu fills just that one. Fill then places the items over the next frames. Main thread only.
// A rack or mannequin with categories takes only items in them (the layout entry's `categories`).
//
// How each kind is filled (always what the display's own script would do, or the script itself):
// - Rack: one at a time, exactly like WeaponRackActivateSCRIPT places the equipped weapon: the very copy
//   (tempering, enchantment, name) is dropped in the open in front of the player, made keyframed once its
//   3D is loaded, and moved onto the trigger's pivot node. The rack's trigger script must then notice it
//   and disable the activator: that is how the game knows the rack holds something and lets the player
//   take the weapon down. If that does not happen within 2 seconds, the weapon goes into the player's
//   inventory and the rack is left alone for the session.
// - Mannequin: the armor is moved into the mannequin's inventory; its script equips it.
// - Case: the items are moved into the case; its script shows them.
// - Item plaque: the item is brought to the player, and the plaque is activated for the player, so the
//   home's own script takes it and shows the model. Its script removes "one of this item", so this only
//   happens while every copy the player carries is plain (no tempered, renamed, enchanted, worn or
//   favorited copy the script could take instead). If the count went up instead (the plaque was full
//   and gave its item back), it is activated once more. If nothing happens within 3 seconds, the item
//   goes back where it came from and that plaque is left alone for the session.
// Nothing is ever taken off a display. If the player leaves the cell, filling stops (a weapon dropped for
// a rack and not up yet goes into the player's inventory).

#include "Game/Displays.h"
#include "Game/ItemAdapter.h"
#include "Home/HomeRegistry.h"

namespace dova::display
{
	struct Prepared
	{
		std::vector<game::FoundDisplay>      found;  // parallel to spots
		std::vector<core::DisplaySpot>       spots;
		std::vector<core::DisplayStock>      stocks;
		std::vector<core::DisplayAssignment> plan;
	};

	// True while a Fill is still placing items.
	[[nodiscard]] bool Busy() noexcept;

	// Finds the displays the layout uses (its `displays` entries that are not ignored) and plans what of
	// `a_stocks` goes on them; `a_only`: that one display. Empty plan when the setting is off, no display
	// is used, nothing fits, or a Fill is running.
	[[nodiscard]] Prepared Prepare(const home::Home& a_home, RE::TESObjectCELL* a_cell, std::vector<core::DisplayStock> a_stocks,
		std::optional<RE::FormID> a_only = std::nullopt);

	// Places the planned items (stack ids resolve through `a_stacks`), then notifies the player.
	void Fill(RE::FormID a_cellId, const Prepared& a_prepared, const game::StackTable& a_stacks);
}
