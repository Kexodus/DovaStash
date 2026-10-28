#pragma once

// Multi-cell homes in the game (DESIGN §6.7; the core rules are in dova/core/HomeCells.h).
//
// A home is its main cell plus the cells its layout lists in `cells` (a cellar, an upper floor, the exterior
// cells with the forge outside). The player is "in the home" in any of them. What an operation run in one
// cell may touch in the others (core::JudgeReach):
// - the player's own cell: everything, exactly as a single-cell home (and a home with one cell takes the
//   very same code path as before: game::CheckContainer against the player's cell);
// - another cell of the home that is loaded (attached, e.g. the neighbouring exterior cells of the loaded
//   grid): its containers, with the usual safety checks against their own cell;
// - another cell that is not loaded: only persistent references. They stay in memory with their cell
//   unloaded, the inventory changes the game makes to them are kept in the save, and RemoveItem works on
//   them without 3D (InventoryChanges only; GetInventoryChanges initialises a never-opened one). A
//   non-persistent reference of an unloaded cell is not in memory, or is about to be dropped with its cell:
//   it is never touched (logged at debug, shown in the panel as "only usable there").
// - displays: only in the player's cell (they need 3D and the cell's scripts).
// Exterior persistent references whose own cell is not loaded sit in their worldspace's persistent cell; they
// are matched to an exterior area by worldspace and grid position (the area's grid is known once that cell
// has been in memory this session; until then such references count as outside the home: skipped).

#include "Game/Eligibility.h"

namespace dova::home
{
	struct Home;

	// One cell of a home (Home::areas): the main cell first, then the layout's extra cells in use.
	struct Area
	{
		core::FormRef                 cell;
		RE::FormID                    id{ 0 };  // 0 when its plugin is not loaded
		std::string                   name;     // the entry's label, else the cell's name or editor id, else the form ref
		bool                          main{ false };
		std::optional<bool>           interior;          // unknown while an exterior cell has not been in memory
		RE::FormID                    worldspace{ 0 };   // exterior cells, once known
		std::optional<core::GridCell> grid;              // exterior cells, once known
	};

	// Fills Home::areas, areasExcluded and areasFrom from the home's layout (the main cell only without one).
	// Cells that have a layout of their own are left out (they are homes of their own).
	void BuildAreas(Home& a_home);

	// The area `a_ref` is in, or nullptr.
	[[nodiscard]] const Area* AreaOf(const Home& a_home, RE::TESObjectREFR* a_ref);

	// game::CheckContainer for a container of `a_home`, wherever it is: in the player's cell exactly as
	// before; elsewhere in the home only when it can be reached from here (see the header comment), then
	// checked against its own cell. `a_reach` (optional) receives where it is.
	[[nodiscard]] game::Verdict CheckHomeContainer(RE::TESObjectREFR* a_ref, const Home& a_home, core::Reach* a_reach = nullptr);

	// The chest that takes what no container takes when an operation runs here (core::SortPlanner::CatchAll with
	// SetPlayerArea): the home's master when it can be reached from here, else the master of the player's area
	// (cells[].master), else the first reachable area master in `cells` order; nullptr when there is none. Main thread.
	[[nodiscard]] RE::TESObjectREFR* CatchAllHere(const Home& a_home);

	// An extra cell may be part of a home (any interior or exterior cell the layout lists) unless it is a
	// temporary cell or owned by someone else (the player, the player's factions or a player house are fine).
	[[nodiscard]] game::Verdict CheckExtraCell(RE::TESObjectCELL* a_cell);

	// An exterior cell that is in memory (loaded, or looked up): its worldspace and grid square are remembered for
	// the session, so its persistent references can be matched to an area while they sit in the worldspace's
	// persistent cell (AreaOf). Interior cells are ignored. Any thread (guarded); cheap.
	void RememberCell(RE::TESObjectCELL* a_cell);

	// Display name of a cell for the panel and the log: its name, else its editor id, else the form ref.
	[[nodiscard]] std::string AreaName(RE::TESObjectCELL* a_cell, const core::FormRef& a_ref);
	// Name of a cell the home's layout lists: its entry's label, else AreaName.
	[[nodiscard]] std::string ListedCellName(const Home& a_home, const core::FormRef& a_cell);

	// Debug: each container of a multi-cell home with its area and whether the other areas reach it
	// (persistent). Once per load of the home (a state change), never per frame.
	void LogAreas(const Home& a_home);
}
