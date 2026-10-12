#pragma once

// Materials at the workbench (DESIGN §8.5).
//
// Sitting down at a crafting station in an enabled home brings the stored materials its recipes use into
// the player's inventory (core::crafting decides what; game::Recipes indexes the load order's recipes);
// when the crafting menu closes (or the player gets up without it opening), what is left of them goes
// back to the container each came from. What was crafted meanwhile always stays with the player, and so
// do the copies the player carried before.
//
// Triggers (own sinks, registered at kDataLoaded):
//   TESFurnitureEvent kEnter (player)  -> pull, before the crafting menu builds its lists
//   MenuOpenCloseEvent "Crafting Menu"  -> open: pull if the furniture event was missed (the player's occupied
//                                          furniture); close: return
//   TESFurnitureEvent kExit (player)    -> return, unless the crafting menu is still open
//   TESContainerChangedEvent            -> copies that reach the player from anywhere but a pull ("crafted")
//
// Saving and loading: the ledger lives in memory only (no co-save). Loading a game or starting a new one
// drops it: what was brought stays in the player's inventory, never lost. Leaving the home before the
// return does the same.

namespace dova::sort::crafting
{
	void RegisterSinks();

	// kPreLoadGame / kNewGame: forget what was brought (it stays with the player). Any thread.
	void Reset(std::string_view a_why);
}
