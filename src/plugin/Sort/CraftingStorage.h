#pragma once

// Materials at the workbench: the mode, and "Craft from storage" sessions (DESIGN §8.5).
//
// Mode (ResolveMode): [Sorting] iCraftingSupply, whether the hooks are in (Hooks/CraftingHooks), and the
// other crafting-from-storage mods that are loaded decide it (core::crafting::ResolveMode). Resolved at
// kDataLoaded and again whenever the settings change; one log line each time it changes.
//
// A storage session lives from sitting down at a station (TESFurnitureEvent kEnter, before the menu builds
// its lists) until the crafting menu closes or the player gets up. Begin snapshots the home's containers
// once and keeps, per material, how many copies they can give and where (core::crafting::MakeSupply); the
// hooks read that cache, so no container is scanned while the menu is open. Consumption (Consume) brings
// exactly the shortfall from the containers (core::crafting::PlanTake, through the ItemMover), lets the game
// remove as usual, and puts back anything the craft did not use. If the containers cannot give what they
// promised, the session falls back to Move for the rest of the visit (a task), with a log line.
//
// Threads (DESIGN §8.5, "Threads"): Begin and End run in SKSE tasks; the hooks, and so Stored, the list
// functions and Consume, run on whatever thread the engine calls them from (on AE the crafting menu's thread is
// not the tasks' one). The session is shared under a lock that is never held while calling into the game;
// Consume works from a copy of the home taken by Begin, never the live Home.

#include <dova/core/CraftingStorage.h>

namespace dova::home
{
	struct Home;
}

namespace dova::sort::crafting
{
	struct ModeStatus
	{
		core::crafting::ModeResolution resolution;
		std::vector<std::string>       otherMods;       // crafting-from-storage mods that are loaded
		bool                           hooksAvailable{ false };  // SE/AE
		bool                           hooksInstalled{ false };
	};

	// Main thread (kDataLoaded, settings change). Installs the hooks when the mode becomes Storage.
	void ResolveMode(std::string_view a_why);
	// Any thread.
	[[nodiscard]] ModeStatus Mode();

	namespace storage
	{
		// Starts a session at `a_station` in `a_home`. False when nothing could be set up (the caller falls
		// back to Move). When the crafting menu is already open (it built its lists before the session), its
		// recipe list is built again (a UI task). SKSE task.
		bool Begin(RE::TESObjectREFR* a_station, const home::Home& a_home, const core::crafting::SupplyRequest& a_request);
		// Any thread.
		[[nodiscard]] bool       Active() noexcept;
		[[nodiscard]] RE::FormID StationId() noexcept;
		// Ends the session (summary in the log). Nothing moves. SKSE task.
		void End(std::string_view a_why);

		// --- for the hooks (any thread) ------------------------------------------------------------------

		// A session is active, the player sits at the session's station, and this thread is not inside Dova
		// Stash's own counting or moving (Busy). On any thread: the crafting menu's is not the tasks' one.
		[[nodiscard]] bool Hooked() noexcept;
		// Hooked() at an alchemy lab: the ingredient list is extended.
		[[nodiscard]] bool ListsHooked() noexcept;

		// Copies of `a_object` the containers can give (0 for anything else).
		[[nodiscard]] std::int32_t Stored(const RE::TESBoundObject* a_object) noexcept;

		// The alchemy list: `a_playerEntries` is what the game counts for the player; returns that plus the
		// stored ingredients the player does not carry. Remembers the arguments for PlayerListArgs.
		[[nodiscard]] std::int32_t ListCount(std::int32_t a_playerEntries, bool a_viewing, bool a_playable);
		[[nodiscard]] std::pair<bool, bool> PlayerListArgs() noexcept;
		// Entry `a_index` past the player's own entries (a new entry the caller owns), or nullptr.
		[[nodiscard]] RE::InventoryEntryData* ListExtra(std::int32_t a_index);
		// One of the player's own entries (caller-owned copy): returned as is, or replaced by a plain entry
		// that counts the stored copies too.
		[[nodiscard]] RE::InventoryEntryData* ListMerge(RE::InventoryEntryData* a_entry);

		// The game removes `a_count` of `a_item` from the player while crafting (no extra list given).
		// Brings the shortfall, then calls `a_remove` (the game's RemoveItem on the player) and returns what
		// it returned.
		using RemoveFn = std::function<RE::ObjectRefHandle*()>;
		RE::ObjectRefHandle* Consume(RE::TESBoundObject* a_item, std::int32_t a_count, const RemoveFn& a_remove);

		// While alive, the hooks leave everything to the game on this thread (our own moves and counts); the
		// hooks on other threads (the menu's) still answer. thread_local.
		struct Busy
		{
			Busy() noexcept;
			~Busy();
			Busy(const Busy&) = delete;
			Busy& operator=(const Busy&) = delete;
		};
	}
}
