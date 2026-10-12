#pragma once

// Putting gear on the home's displays (DESIGN §8.4). Deposit and Sort all plan it before their own moves
// (core::PlanDisplays, so what goes up is taken out of the deposit), then Fill places the items over the
// next frames. Main thread only.
//
// How each kind is filled (always what the display's own script would do, or the script itself):
// - Rack: the item is dropped from its holder at the rack's pivot node, made keyframed and moved onto
//   the node once its 3D is loaded: exactly WeaponRackActivateSCRIPT's placement, without having to
//   equip it first. The very copy (tempering, enchantment, name) goes up. The rack's trigger script
//   then marks the rack as taken, and the player takes the weapon down by activating it.
// - Mannequin: the armor is moved into the mannequin's inventory; its script equips it.
// - Case: the items are moved into the case; its script shows them.
// - Item plaque: the item is brought to the player, and the plaque is activated for the player, so the
//   home's own script takes it and shows the model. Its script removes "one of this item", so this only
//   happens while every copy the player carries is plain (no tempered, renamed, enchanted, worn or
//   favorited copy the script could take instead). If the count went up instead (the plaque was full
//   and gave its item back), it is activated once more. If nothing happens within 3 seconds, the item
//   goes back where it came from and that plaque is left alone for the session.
// Nothing is ever taken off a display. If the player leaves the cell, filling stops.

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

	// Finds the home's displays (scripts, then the layout's `displays` entries) and plans what of
	// `a_stocks` goes on them. Empty plan when the setting is off, nothing fits, or a Fill is running.
	[[nodiscard]] Prepared Prepare(const home::Home& a_home, RE::TESObjectCELL* a_cell, std::vector<core::DisplayStock> a_stocks);

	// Places the planned items (stack ids resolve through `a_stacks`), then notifies the player.
	void Fill(RE::FormID a_cellId, const Prepared& a_prepared, const game::StackTable& a_stacks);
}
