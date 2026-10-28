#pragma once

// The drawer's tables (docs/EDITOR-UX.md §2.5): sort by any column (Shift+click adds a column), a filter box and
// quick filters per table, remembered for the session. A table builds one TableRow per object with a value per
// column; FilterAndSort gives the rows to draw in order. Each row keeps its own index (a reference, a door, a
// preview container), so selection, Ctrl+click and double-click never depend on the row's position.

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

namespace dova::editor::ui
{
	// The drawer's tables, in the tab order (EditorApp::DrawerTab).
	enum class DrawerTable
	{
		Containers,
		Displays,
		Doors,
		Preview,
		Count_
	};

	// One column's value in a row: the text (what the filter box searches; text columns sort by it, ignoring case),
	// a number (sorts numerically when set) and a group that orders before the value in either direction (1: after
	// every real value, such as "ignored" or an empty note; -1: before, the master).
	struct TableCell
	{
		std::string text;
		double      number{ std::numeric_limits<double>::quiet_NaN() };
		int         group{ 0 };
	};

	// A cell that sorts before (-1) or after (1) the real values, whichever the direction.
	[[nodiscard]] inline TableCell Grouped(std::string a_text, int a_group)
	{
		TableCell c;
		c.text = std::move(a_text);
		c.group = a_group;
		return c;
	}

	struct TableRow
	{
		int                    index{ -1 };  // what the row shows (a reference index, a door index, a preview container)
		std::vector<TableCell> cells;        // one per column
		std::uint32_t          tags{ 0 };    // the quick filters it matches (bits)
		std::string            kind;         // the object's kind (its base name) for the Kind popover; empty: none
	};

	struct SortKey
	{
		int  column{ 0 };
		bool descending{ false };
	};

	// A quick filter, drawn as a toggle chip next to the box. Chips of one group combine with "or" (a role, a kind),
	// groups with "and". `ifPresent`: shown only when a row matches it (the display kinds of the home).
	struct QuickFilter
	{
		const char*   label;
		std::uint32_t bit;
		int           group;
		const char*   tip;
		bool          ifPresent{ false };
	};

	// One table's sort and filter, for the session (not in state.json).
	struct TableView
	{
		std::vector<SortKey> sort;             // empty: the table's own order
		bool                 synced{ false };  // `sort` given to ImGui's table (its first draw, or --sort)
		std::string          filter;           // the box: every word must be in one of the row's columns
		std::uint32_t        quick{ 0 };       // the quick filters on
		std::uint32_t        present{ 0 };     // the quick filters some row matches (last frame)
		std::vector<std::string> kinds;        // the Kind popover's picks (none: every kind)
		std::vector<std::pair<std::string, int>> kindList;  // the kinds in the table and their rows (last frame)
		int                  shown{ 0 }, total{ 0 };  // rows after the filters, rows in all (last frame: "N of M")
	};

	// The table's column headers (TableSetupColumn uses them; --sort names them) and quick filters.
	[[nodiscard]] std::span<const char* const> TableColumns(DrawerTable a_table);
	[[nodiscard]] std::span<const QuickFilter> TableQuickFilters(DrawerTable a_table);
	[[nodiscard]] const char*                  TableName(DrawerTable a_table);  // containers, displays, doors, preview (--tab)

	// The flags every drawer table adds: sortable, Shift+click for more columns, a third click back to the table's order.
	inline constexpr ImGuiTableFlags kSortableTable = ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_SortTristate;

	// Call between TableSetupColumn and the rows: the first time it gives `a_view.sort` to ImGui's table, afterwards
	// it takes the header clicks into it.
	void SyncSort(TableView& a_view);
	// The header row (TableHeadersRow) with the columns in `a_rightMask` (bit n: column n) right-aligned, as their
	// cells are: numbers sit on one right edge (EDITOR-UX.md §3.5). Draw those cells with RightText.
	void TableHeaders(DrawerTable a_table, std::uint32_t a_rightMask);
	void RightText(const char* a_text);
	// Drops the rows the box and the quick filters hide, then sorts the rest stably by `a_view.sort` (the rows come
	// in the table's own order). Updates the view's counts.
	void FilterAndSort(TableView& a_view, std::vector<TableRow>& a_rows, DrawerTable a_table);
	// The box's words, all found (ignoring case) in the row's text.
	[[nodiscard]] bool MatchesText(const TableRow& a_row, std::string_view a_filter);

	// The tab row's right side: "N of M" when rows are hidden, the quick filters and the box. Width first (to
	// right-align it), then Draw at the cursor. `a_folded`: the quick filters in a "Filters" popover with a count
	// of those on (a row too narrow for the chips; the Kind list and the box always show).
	[[nodiscard]] float FilterBarWidth(const TableView& a_view, DrawerTable a_table, bool a_folded = false);
	void                DrawFilterBar(TableView& a_view, DrawerTable a_table, bool a_folded = false);

	// The command line: "--sort table:column[:desc]" (column by header or number from 1) and "--quick chip".
	struct SortArg
	{
		DrawerTable table{ DrawerTable::Containers };
		SortKey     key;
	};
	[[nodiscard]] std::optional<SortArg>      ParseSortArg(std::string_view a_text, std::string* a_error = nullptr);
	[[nodiscard]] std::optional<DrawerTable>  ParseTableName(std::string_view a_text);
	[[nodiscard]] std::optional<std::uint32_t> QuickFilterBit(DrawerTable a_table, std::string_view a_label);  // nullopt: none of that name
}
