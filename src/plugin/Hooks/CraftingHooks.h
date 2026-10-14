#pragma once

// Craft from storage (DESIGN §8.5): the hooks that let a crafting menu count the home's stored materials and
// take what the player lacks from the containers. Installed only when the mode resolves to "craft from
// storage", on SE and AE (VR: never; the mode falls back to Move). Every hook calls the game first and only
// changes the answer while sort::crafting::storage::Hooked() (a session is active, on the main thread, the
// player sits at the session's station, and Dova Stash is not itself moving or counting items).
//
//   InventoryChanges, count of one object (SE 15869 / AE 16109, MinHook detour)
//       What the recipe lists, the requirement lines ("Iron Ingot (3/2)"), the craftable check before a
//       craft and recipe conditions ask. For the player's inventory: + what the containers can give.
//   TESObjectREFR::GetInventoryItemCount (SE 19274 / AE 19700, MinHook detour)
//   TESObjectREFR::GetInventoryItemAt    (SE 19273 / AE 19699, MinHook detour)
//       How the alchemy menu lists the player's ingredients (count, then one caller-owned copy of each
//       entry). At an alchemy lab only: stored ingredients the player does not carry are listed after the
//       player's own entries, and the player's own plain entries show the stored copies too.
//   PlayerCharacter::RemoveItem (vtable slot 0x56, CommonLib write_vfunc)
//       How every crafting menu consumes materials (reason kRemove, no target, no drop point). When the
//       player carries fewer than asked, the shortfall is first brought from the containers, then the game
//       removes as usual.
//
// Address Library IDs (RELOCATION_ID(SE, AE)): 19273/19699 and 19274/19700 are the IDs CommonLibSSE-NG's own
// TESObjectREFR::GetInventoryItemAt / GetInventoryItemCount wrappers use; 15869/16109 is the next function
// after CommonLib's InventoryChanges::GetItemCount (15868/16108). The same four hooks, with the same IDs and
// signatures, are what Crafting Pulls Resources From Containers (JerryYOJ/CraftingPullFromContainer-SKSE)
// and Skyrim Crafting Inventory Extender (ohfor/scie, MIT) install on SE and AE.

namespace dova::hooks::crafting
{
	// True when the hooks are in (installs them the first time; SE/AE only). Main thread. Never throws.
	bool EnsureInstalled();
	[[nodiscard]] bool Installed() noexcept;
	// The runtime has the functions (SE or AE). False on VR.
	[[nodiscard]] bool Available() noexcept;
}
