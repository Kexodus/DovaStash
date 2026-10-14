#pragma once

// Putting gear on the home's displays (DESIGN §8.4). Only the displays the layout uses are filled: the
// ones the player turned on with the hotkey, or that a template lists. Deposit and Sort all plan them
// before their own moves (core::PlanDisplays, so what goes up is taken out of the deposit); "Fill now" in
// a display's menu fills just that one. Fill then places the items over the next frames. Main thread only.
// A rack, mannequin or bookshelf with categories takes only items in them (the layout entry's `categories`). A
// mannequin with a `set` takes only that armor set (game::ApplyChosenSet), and more of it as it arrives.
//
// How each kind is filled (always what the display's own script would do, or the script itself):
// - Rack: one at a time, exactly like WeaponRackActivateSCRIPT places the equipped weapon: the very copy
//   (tempering, enchantment, name) is dropped in the open in front of the player, made keyframed once its
//   3D is loaded, and moved onto the trigger's pivot node with Papyrus' own MoveToNode
//   (game::PapyrusMoveToNode; game::MoveToNodeDirect if the VM can't run it within 2 seconds). The node
//   is the one the script picks (game::RackNode: the Unofficial Patch's script picks some bow, staff and
//   shield nodes from the weapon's own 3D); a weapon that script would turn away there (a staff too long
//   for a display case) goes into the player's inventory, and that rack is not offered that item again
//   this session. A tick after that move comes the bookkeeping the rack's
//   script does (game::DisableRack, game::RecordRackItem): the activator is disabled, which is how the
//   game knows the rack holds something and lets the player take the weapon down. If it is still enabled after
//   2 seconds (a rack without that script whose trigger did not disable it), the weapon goes into the
//   player's inventory and Deposit and Sort all leave the rack alone for the session ("Fill now" tries
//   again). The weapon is kept out of sight from its drop until it is up (game::HideDropped: only its 3D
//   is culled, collision and trigger events are untouched), so it appears on the rack instead of being
//   seen falling or hanging in the air; every way out (placed, taken back, the player leaving) shows it
//   again (game::ShowDropped).
// - Mannequin: the armor is moved into the mannequin's inventory; its script equips it.
// - Case: the items are moved into the case; its script shows them.
// - Item plaque: the item is brought to the player, and the plaque is activated for the player, so the
//   home's own script takes it and shows the model. Its script removes "one of this item", so this only
//   happens while every copy the player carries is plain (no tempered, renamed, enchanted, worn or
//   favorited copy the script could take instead). If the count went up instead (the plaque was full
//   and gave its item back), it is activated once more. If nothing happens within 3 seconds, the item
//   goes back where it came from and Deposit and Sort all leave that plaque alone for the session.
// - Bookshelf: one shelf at a time, never more books than the room its script leaves (read again just
//   before): the books are moved into the shelf's container, as the player puts them in, so its script's
//   OnItemAdded writes each into a slot. Once it took them, its own UpdateBooks shows them on its markers
//   (game::RefreshBookshelf; the script itself only does that when its menu closes). A book its script hands
//   back (to the player) goes back where it came from; one it does not take within 3 seconds stays in the
//   shelf (open it to take it); either way Deposit and Sort all leave that shelf alone for the session.
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

	// kPreLoadGame / kNewGame (main.cpp): a Fill still running belongs to the old game; it stops touching
	// anything (its handles, refs and stacks are gone) and the busy flag is cleared. Any thread.
	void Reset(std::string_view a_why);

	// Finds the displays the layout uses (its `displays` entries that are not ignored) and plans what of
	// `a_stocks` goes on them; `a_only`: that one display (tried even if it failed earlier this session).
	// Empty plan when the setting is off, no display is used, nothing fits, or a Fill is running.
	[[nodiscard]] Prepared Prepare(const home::Home& a_home, RE::TESObjectCELL* a_cell, std::vector<core::DisplayStock> a_stocks,
		std::optional<RE::FormID> a_only = std::nullopt);

	// Places the planned items (stack ids resolve through `a_stacks`), then notifies the player.
	void Fill(RE::FormID a_cellId, const Prepared& a_prepared, const game::StackTable& a_stacks);
}
