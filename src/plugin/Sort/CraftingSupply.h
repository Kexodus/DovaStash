#pragma once

// Materials at the workbench (DESIGN §8.5).
//
// Two modes (Sort/CraftingStorage.h decides which; [Sorting] iCraftingSupply):
// - Craft from storage (Sort/CraftingStorage.cpp): the menu counts the home's stored materials and crafting
//   takes the shortfall from the containers; nothing moves when the menu opens or closes. Recipe stations and
//   alchemy labs; enchanters always use Move.
// - Move (this file): sitting down at a crafting station in an enabled home brings the stored materials its
//   recipes use into the player's inventory (core::crafting decides what; game::Recipes indexes the load
//   order's recipes); when the crafting menu closes (or the player gets up without it opening), what is left
//   of them goes back to the container each came from. What was crafted meanwhile always stays with the
//   player, and so do the copies the player carried before.
//
// Triggers (own sinks, registered at kDataLoaded):
//   TESFurnitureEvent kEnter (player)  -> start (storage session or pull), before the crafting menu builds its
//                                          lists
//   MenuOpenCloseEvent "Crafting Menu"  -> open: pull if the furniture event was missed (the player's occupied
//                                          furniture; always Move, the menu's lists are already built);
//                                          close: end the storage session / return
//   TESFurnitureEvent kExit (player)    -> the same, unless the crafting menu is still open
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
