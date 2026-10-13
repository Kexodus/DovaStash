#pragma once

// Homes that span several cells (DESIGN §6.7): the main `cell` of a layout plus an optional `cells` list of
// more cells that belong to the same home (a cellar behind a load door, an upper floor, the exterior cells
// with the forge, smelter or garden outside). Containers, displays and stations keep being named by their
// references; a reference already says which cell it is in.
//
// Decisions:
// - Schema: `"cells"` is optional and additive (schema 1 stays "dovastash-layout-1"). Each entry is a cell
//   form reference, either as a plain string or as an object {"cell", "label", "note"} (unknown keys
//   preserved). Interior and exterior cells are both named by their CELL record ("Skyrim.esm|0x00009BF7"),
//   which is stable across load orders; the player's current cell is compared by form id, so an exterior
//   cell never needs to be in memory to be recognised. A whole worldspace is never a home: only the listed
//   cells are.
// - A file without `cells` parses, serializes and resolves exactly as before (no key is written).
// - Malformed entries are reported and kept verbatim (`unparsedCells`), like containers.
// - Membership (ResolveCellMembership): a cell with its own player layout is its own home (the player's
//   setup always wins); otherwise a player layout that lists it in `cells` makes it part of that home (the
//   first by main-cell order when several do; the others are reported); otherwise it is its own home and
//   templates resolve by `cell` as before. Templates' `cells` take effect once applied (copied into the
//   player layout), never by themselves, so a shipped `*Cellar.json` keeps working unchanged.
// - Reach (JudgeReach): an operation run in one cell can use a container in another cell of the home only
//   while that cell is loaded (attached) or when the reference is persistent (persistent references stay
//   in memory, and their inventory changes are kept in the save). Anything else is skipped, never lost.

#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "dova/core/Diagnostics.h"
#include "dova/core/FormRef.h"

namespace dova::core
{
	// One more cell of a home besides its main `cell`.
	struct HomeCell
	{
		FormRef        cell;
		std::string    label;  // what the player sees for this part of the home ("Grounds", "Cellar")
		std::string    note;   // for people reading the file
		nlohmann::json extra = nlohmann::json::object();  // unknown keys, preserved

		friend bool operator==(const HomeCell&, const HomeCell&) = default;
	};

	// --- JSON (used by ParseLayout / LayoutToJson) ---------------------------------------------------------

	// Parses the value of "cells" (an array of entries, or a single entry). Good entries go to `a_out`, bad
	// ones are reported and kept verbatim in `a_unparsed` (an array). Returns false (after an error) when the
	// value has the wrong type: the caller then keeps the whole value as an unknown key.
	bool ParseHomeCells(const nlohmann::json& a_value, std::string_view a_source, std::vector<HomeCell>& a_out, nlohmann::json& a_unparsed,
		Diagnostics& a_diags);
	// The "cells" array: an entry with only a cell is written as a plain string. Unparsed entries follow.
	[[nodiscard]] nlohmann::ordered_json HomeCellsToJson(std::span<const HomeCell> a_cells, const nlohmann::json& a_unparsed);

	// Warnings: the same cell listed twice (the first entry is used), the main cell listed again (ignored).
	void ValidateHomeCells(const std::optional<FormRef>& a_main, std::span<const HomeCell> a_cells, std::string_view a_source, Diagnostics& a_diags);

	// Applying a template over a player layout: the template's entries, then the player's entries the template
	// does not list; the player's label and note fill a template entry's blanks, and unknown keys are merged.
	[[nodiscard]] std::vector<HomeCell> MergeHomeCells(std::span<const HomeCell> a_template, std::span<const HomeCell> a_player);

	// Naming an area in game ("Grounds", "Cellar"): sets the `label` of the entry for `a_cell` (trimmed; empty
	// clears it, so the cell's own name shows again). False when the cell is not listed (nothing changed).
	bool SetHomeCellLabel(std::vector<HomeCell>& a_cells, const FormRef& a_cell, std::string_view a_label);

	// Every cell of a home: the main cell first, then each listed cell once (the main cell is not repeated).
	[[nodiscard]] std::vector<FormRef> HomeCellRefs(const FormRef& a_main, std::span<const HomeCell> a_cells);

	// The cells that make up a home right now: HomeCellRefs without the listed cells that have a player layout of
	// their own (they are homes of their own); those are added to `a_excluded` when given.
	[[nodiscard]] std::vector<FormRef> EffectiveHomeCells(const FormRef& a_main, std::span<const HomeCell> a_cells,
		const std::function<bool(const FormRef&)>& a_hasOwnLayout, std::vector<FormRef>* a_excluded = nullptr);

	// --- Which home a cell belongs to ------------------------------------------------------------------------

	// One player layout that lists extra cells: its main cell and those cells.
	struct HomeCellsEntry
	{
		FormRef              home;
		std::vector<FormRef> cells;
	};

	struct CellMembership
	{
		enum class Kind : std::uint8_t
		{
			Own,   // the cell is a home of its own (its own layout, templates by `cell`)
			Extra  // the cell is part of `home`, whose layout lists it
		};
		Kind                 kind{ Kind::Own };
		FormRef              home;         // the home's main cell (the cell itself for Own)
		std::vector<FormRef> otherClaims;  // further homes that list the cell too (ignored)
	};

	// `a_hasOwnLayout`: a player layout file exists for the cell (also a broken one: it is never taken over).
	[[nodiscard]] CellMembership ResolveCellMembership(const FormRef& a_cell, bool a_hasOwnLayout, std::span<const HomeCellsEntry> a_index);

	// --- Reaching containers in the home's other cells -------------------------------------------------------

	enum class Reach : std::uint8_t
	{
		Here,         // in the player's cell
		Loaded,       // in another cell of the home that is loaded right now (a neighbouring exterior cell)
		Remote,       // in another cell of the home that is not loaded, but the reference is persistent
		Unreachable,  // in another cell of the home that is not loaded, and the reference is not persistent
		Outside       // not in any cell of the home
	};

	struct RefPlacement
	{
		bool inHome{ false };        // its cell is one of the home's cells
		bool inPlayerCell{ false };  // its cell is the player's cell
		bool cellAttached{ false };  // its cell is loaded (attached) right now
		bool persistent{ false };    // the reference is persistent
	};

	[[nodiscard]] Reach            JudgeReach(const RefPlacement& a_place) noexcept;
	[[nodiscard]] bool             IsReachable(Reach a_reach) noexcept;
	[[nodiscard]] std::string_view ReachName(Reach a_reach) noexcept;

	// Exterior grid cell of a world position (4096 units per cell, as the game and xEdit count).
	struct GridCell
	{
		std::int32_t x{ 0 };
		std::int32_t y{ 0 };

		friend auto operator<=>(const GridCell&, const GridCell&) = default;
	};
	[[nodiscard]] GridCell GridOf(float a_x, float a_y) noexcept;

	// The cell a player layout file is for: "Skyrim.esm_000165A7.json" -> Skyrim.esm|0x000165A7 (the inverse of
	// LayoutFileName). nullopt for any other name.
	[[nodiscard]] std::optional<FormRef> CellFromLayoutFileName(std::string_view a_fileName);
}
