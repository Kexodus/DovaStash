// The drawer's tables: sort, the filter box and the filters in the column headers (Tables.h, docs/EDITOR-UX.md §2.5).

#include "ui/Tables.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <format>

#include <imgui_internal.h>

#include "dova/core/FormRef.h"
#include "ui/Widgets.h"

namespace dova::editor::ui
{
	namespace
	{
		constexpr const char* kContainerColumns[] = { "Container", "Kind", "Role", "Reference", "Height (m)", "Notes" };
		constexpr const char* kDisplayColumns[] = { "Display", "Kind", "Used", "Reference", "Height (m)" };
		constexpr const char* kDoorColumns[] = { "Leads to", "Cell", "In the home" };
		constexpr const char* kPreviewColumns[] = { "Container", "Fill", "Items", "What it receives" };

		// The bits match the tags Drawer.cpp and Preview.cpp give their rows; the column is the header that holds the
		// filter (Role, Notes, Kind, Used, Fill).
		constexpr QuickFilter kContainerQuick[] = {
			{ "Master", 1u << 0, 0, 2, "The container for what has no other place" },
			{ "With categories", 1u << 1, 0, 2, "Containers given categories or items" },
			{ "No categories", 1u << 2, 0, 2, "Containers in the template with nothing to take" },
			{ "Ignored", 1u << 3, 0, 2, "Containers the template ignores" },
			{ "Not listed", 1u << 4, 0, 2, "Containers the template leaves out" },
			{ "Respawning", 1u << 5, 1, 5, "Containers whose contents respawn" },
		};
		constexpr QuickFilter kDisplayQuick[] = {
			{ "Rack", 1u << 0, 0, 1, "Weapon racks and shield plaques", true },
			{ "Mannequin", 1u << 1, 0, 1, nullptr, true },
			{ "Plaque", 1u << 2, 0, 1, "Plaques made for one item", true },
			{ "Case", 1u << 3, 0, 1, "Display cases", true },
			{ "Bookshelf", 1u << 4, 0, 1, nullptr, true },
			{ "Used", 1u << 5, 1, 2, "Displays the template uses" },
			{ "Unused", 1u << 6, 1, 2, "Displays the template ignores or leaves out" },
		};
		constexpr QuickFilter kPreviewQuick[] = {
			{ "Master", 1u << 1, 0, 1, nullptr },
			{ "Receives items", 1u << 2, 0, 1, "Containers some of the sample goes to" },
			{ "Receives nothing", 1u << 0, 0, 1, "Containers no item of the sample goes to" },
		};
		constexpr int kKindColumn = 1;  // Containers: the objects' kinds (chest, sack, barrel...) filter the Kind column

		int CompareNoCase(std::string_view a_lhs, std::string_view a_rhs)
		{
			const std::size_t n = std::min(a_lhs.size(), a_rhs.size());
			for (std::size_t i = 0; i < n; ++i) {
				const int l = std::tolower(static_cast<unsigned char>(a_lhs[i]));
				const int r = std::tolower(static_cast<unsigned char>(a_rhs[i]));
				if (l != r) {
					return l < r ? -1 : 1;
				}
			}
			return a_lhs.size() == a_rhs.size() ? 0 : a_lhs.size() < a_rhs.size() ? -1 : 1;
		}

		int CompareCells(const TableCell& a_lhs, const TableCell& a_rhs)
		{
			if (!std::isnan(a_lhs.number) && !std::isnan(a_rhs.number)) {
				return a_lhs.number < a_rhs.number ? -1 : a_lhs.number > a_rhs.number ? 1 : 0;
			}
			return CompareNoCase(a_lhs.text, a_rhs.text);
		}

		std::vector<std::string> Words(std::string_view a_text)
		{
			std::vector<std::string> words;
			std::string              word;
			for (const char c : a_text) {
				if (std::isspace(static_cast<unsigned char>(c))) {
					if (!word.empty()) {
						words.push_back(std::move(word));
						word.clear();
					}
				} else {
					word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				}
			}
			if (!word.empty()) {
				words.push_back(std::move(word));
			}
			return words;
		}

		bool Visible(const QuickFilter& a_quick, const TableView& a_view)
		{
			return !a_quick.ifPresent || (a_view.present & a_quick.bit) != 0 || (a_view.quick & a_quick.bit) != 0;
		}

		std::string CountText(const TableView& a_view)
		{
			return a_view.shown < a_view.total ? std::format("{} of {}", a_view.shown, a_view.total) : std::string{};
		}

		float BoxWidth() { return std::round(170.0f * w::Th().scale); }

		// The Kind column lists the objects' kinds (Containers only: the other tables' rows carry no kind).
		bool KindColumn(DrawerTable a_table, const TableView& a_view, int a_column)
		{
			return a_table == DrawerTable::Containers && a_column == kKindColumn && !a_view.kindList.empty();
		}

		// A column with a funnel: quick filters some row can match, or the kinds.
		bool Filterable(DrawerTable a_table, const TableView& a_view, int a_column)
		{
			if (KindColumn(a_table, a_view, a_column)) {
				return true;
			}
			return std::ranges::any_of(TableQuickFilters(a_table), [&](const QuickFilter& q) { return q.column == a_column && Visible(q, a_view); });
		}

		// A filter of the column is on.
		bool FilterOn(DrawerTable a_table, const TableView& a_view, int a_column)
		{
			if (KindColumn(a_table, a_view, a_column) && !a_view.kinds.empty()) {
				return true;
			}
			return std::ranges::any_of(TableQuickFilters(a_table), [&](const QuickFilter& q) { return q.column == a_column && (a_view.quick & q.bit) != 0; });
		}

		// The funnel's popover: the column's checks, the kinds (with their row counts) in the Kind column, Clear.
		// `a_roomAbove` > 0: the popover opens above the header and the kinds' list fits in that room.
		void DrawColumnFilter(TableView& a_view, DrawerTable a_table, int a_column, float a_roomAbove)
		{
			const auto& t = w::Th();
			bool        any = false;
			for (const auto& q : TableQuickFilters(a_table)) {
				if (q.column != a_column || !Visible(q, a_view)) {
					continue;
				}
				bool v = (a_view.quick & q.bit) != 0;
				if (w::Check(q.label, v, q.tip)) {
					a_view.quick ^= q.bit;
				}
				any = any || v;
			}
			if (KindColumn(a_table, a_view, a_column)) {
				// The list scrolls rather than leave the window: below the header to the window's bottom, or above it (a
				// low drawer) to the window's top, less what the popover holds before the list and Clear after it.
				const ImGuiViewport* vp = ImGui::GetMainViewport();
				const float          room = a_roomAbove > 0.0f ? a_roomAbove - ImGui::GetCursorPosY() - 1.5f * t.rowH :
				                                                 vp->WorkPos.y + vp->WorkSize.y - ImGui::GetCursorScreenPos().y - 1.5f * t.rowH;
				ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, std::max(room, 4.0f * t.rowH)));
				ImGui::BeginChild("kinds", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY);
				// The counts on one right edge after the widest kind (a check is its 16 px box, 8 px, then the label).
				float labelW = 0.0f, countW = 0.0f;
				for (const auto& [kind, n] : a_view.kindList) {
					labelW = std::max(labelW, ImGui::CalcTextSize(kind.c_str()).x);
					countW = std::max(countW, ImGui::CalcTextSize(std::to_string(n).c_str()).x);
				}
				const float right = ImGui::GetCursorPosX() + std::round(24.0f * t.scale + labelW) + t.pad + countW;
				for (const auto& [kind, n] : a_view.kindList) {
					const auto it = std::ranges::find_if(a_view.kinds, [&](const std::string& k) { return core::IEquals(k, kind); });
					bool       on = it != a_view.kinds.end();
					if (w::Check(std::format("{}##{}", kind, kind).c_str(), on)) {
						if (on) {
							a_view.kinds.push_back(kind);
						} else {
							a_view.kinds.erase(it);
						}
					}
					const auto count = std::to_string(n);
					ImGui::SameLine(right - ImGui::CalcTextSize(count.c_str()).x);
					w::Muted(count.c_str());
				}
				ImGui::EndChild();
				any = any || !a_view.kinds.empty();
			}
			if (any && w::LinkButton("Clear")) {
				for (const auto& q : TableQuickFilters(a_table)) {
					if (q.column == a_column) {
						a_view.quick &= ~q.bit;
					}
				}
				if (KindColumn(a_table, a_view, a_column)) {
					a_view.kinds.clear();
				}
			}
		}
	}

	void RightText(const char* a_text)
	{
		const float w = ImGui::CalcTextSize(a_text).x;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - w));
		ImGui::TextUnformatted(a_text);
	}

	void TableHeaders(DrawerTable a_table, TableView& a_view, std::uint32_t a_rightMask)
	{
		const auto&       t = w::Th();
		const ImGuiStyle& s = ImGui::GetStyle();
		// TableHeader keeps the sort arrow's room at the cell's right edge; the funnel sits before that room.
		const float arrow = ImGui::GetFontSize() + s.FramePadding.x;
		const float funnel = std::round(14.0f * t.scale);
		ImGuiTable* table = ImGui::GetCurrentTable();
		ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
		int n = 0;
		for (const char* name : TableColumns(a_table)) {
			const int column = n++;
			if (!ImGui::TableSetColumnIndex(column)) {
				continue;
			}
			const bool  filterable = Filterable(a_table, a_view, column);
			const float funnelW = filterable ? funnel + 4.0f * t.scale : 0.0f;
			const bool  right = (a_rightMask & (1u << column)) != 0;
			if (filterable) {
				ImGui::SetNextItemAllowOverlap();  // the funnel over the header's right end takes the click there
			}
			if (right) {
				// A number column: the header without its own label or arrow (drawn into an empty clip), then the name
				// flush right on the figures' edge (RightText's) and the sort arrow at the name's left.
				const ImVec2 lp = ImGui::GetCursorScreenPos();
				const float  edge = lp.x + ImGui::GetContentRegionAvail().x - funnelW;
				ImDrawList*  dl = ImGui::GetWindowDrawList();
				dl->PushClipRect(lp, lp, false);
				ImGui::TableHeader(std::format("##{}", name).c_str());
				dl->PopClipRect();
				const ImVec2 cmin = ImGui::GetItemRectMin(), cmax = ImGui::GetItemRectMax();
				const float  tw = ImGui::CalcTextSize(name).x;
				const float  x = std::max(lp.x, edge - tw);
				dl->PushClipRect(cmin, cmax, true);
				dl->AddText(ImVec2(std::floor(x), lp.y), ImGui::GetColorU32(ImGuiCol_Text), name);
				if (table && column < table->ColumnsCount && table->Columns[column].SortOrder != -1) {
					// TableHeader's arrow (0.65 of the font), its visible part ending 4 px before the name, centred on the line.
					const auto& c = table->Columns[column];
					const float fs = ImGui::GetFontSize();
					const float ax = x - 0.76f * fs - 4.0f * t.scale;
					if (c.SortOrder > 0) {
						// A second or later sort column: its place in the order before the arrow, as TableHeader shows it.
						const auto  order = std::to_string(c.SortOrder + 1);
						const float ow = ImGui::CalcTextSize(order.c_str()).x;
						dl->AddText(ImVec2(std::floor(ax + 0.24f * fs - s.ItemInnerSpacing.x - ow), lp.y), ImGui::GetColorU32(ImGuiCol_Text, 0.7f), order.c_str());
					}
					ImGui::RenderArrow(dl, ImVec2(std::floor(ax), std::floor(lp.y + 0.5f * fs * (1.0f - 0.65f))), ImGui::GetColorU32(ImGuiCol_Text),
						c.SortDirection == ImGuiSortDirection_Ascending ? ImGuiDir_Up : ImGuiDir_Down, 0.65f);
				}
				dl->PopClipRect();
			} else {
				ImGui::TableHeader(name);
			}
			if (static_cast<std::size_t>(column) < a_view.headerTips.size() && !a_view.headerTips[static_cast<std::size_t>(column)].empty()) {
				w::Tip(a_view.headerTips[static_cast<std::size_t>(column)].c_str());
			}
			if (!filterable) {
				continue;
			}
			const ImVec2 hmin = ImGui::GetItemRectMin(), hmax = ImGui::GetItemRectMax();
			const ImVec2 bmin(std::floor(hmax.x - (right ? 0.0f : arrow) - funnelW), hmin.y);
			ImGui::PushID(column);
			ImGui::SetCursorScreenPos(bmin);
			const bool pressed = ImGui::InvisibleButton("funnel", ImVec2(funnelW, std::max(hmax.y - hmin.y, 1.0f)));
			const bool hovered = ImGui::IsItemHovered();
			const bool on = FilterOn(a_table, a_view, column);
			w::DrawIcon(ImGui::GetWindowDrawList(), w::Icon::Filter, ImVec2(bmin.x + 2.0f * t.scale, std::floor(hmin.y + (hmax.y - hmin.y - funnel) * 0.5f)), funnel,
				on ? t.accent : hovered ? t.text : t.muted);
			if (hovered && !ImGui::IsPopupOpen("filter")) {
				ImGui::SetTooltip("%s", on ? "Filter on: click to change" : "Filter");
			}
			if (pressed || a_view.openFilter == column) {
				a_view.openFilter = -1;
				ImGui::OpenPopup("filter");
			}
			// Under the funnel, as BeginPopover places its popup: left-aligned, right-aligned in the right half. A long
			// list (the kinds) in a low drawer opens above the header instead, where there is room.
			const ImGuiViewport* vp = ImGui::GetMainViewport();
			const bool           atRight = bmin.x + funnelW * 0.5f > vp->WorkPos.x + vp->WorkSize.x * 0.5f;
			const float          below = vp->WorkPos.y + vp->WorkSize.y - hmax.y - 4.0f * t.scale;
			const float          above = hmin.y - 4.0f * t.scale - vp->WorkPos.y;
			const bool           up = KindColumn(a_table, a_view, column) && below < 10.0f * t.rowH && above > below;
			ImGui::SetNextWindowPos(ImVec2(atRight ? bmin.x + funnelW : bmin.x, up ? hmin.y - 4.0f * t.scale : hmax.y + 4.0f * t.scale), ImGuiCond_Always,
				ImVec2(atRight ? 1.0f : 0.0f, up ? 1.0f : 0.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f * t.scale, 6.0f * t.scale));
			const bool open = ImGui::BeginPopup("filter", ImGuiWindowFlags_NoMove);
			ImGui::PopStyleVar();
			if (open) {
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * t.scale, 4.0f * t.scale));
				if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::GetIO().WantTextInput) {
					ImGui::CloseCurrentPopup();
				}
				DrawColumnFilter(a_view, a_table, column, up ? above : 0.0f);
				ImGui::PopStyleVar();
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}
	}

	std::span<const char* const> TableColumns(DrawerTable a_table)
	{
		switch (a_table) {
		case DrawerTable::Containers:
			return kContainerColumns;
		case DrawerTable::Displays:
			return kDisplayColumns;
		case DrawerTable::Doors:
			return kDoorColumns;
		case DrawerTable::Preview:
			return kPreviewColumns;
		default:
			return {};
		}
	}

	std::span<const QuickFilter> TableQuickFilters(DrawerTable a_table)
	{
		switch (a_table) {
		case DrawerTable::Containers:
			return kContainerQuick;
		case DrawerTable::Displays:
			return kDisplayQuick;
		case DrawerTable::Preview:
			return kPreviewQuick;
		default:
			return {};
		}
	}

	const char* TableName(DrawerTable a_table)
	{
		static constexpr const char* names[] = { "containers", "displays", "doors", "preview" };
		const auto                   i = static_cast<std::size_t>(a_table);
		return i < std::size(names) ? names[i] : "";
	}

	void SyncSort(TableView& a_view)
	{
		if (!a_view.synced) {
			// After the layout: ImGui works out each column's sort directions there (a direction set before it is lost).
			(void)ImGui::TableGetSortSpecs();
			a_view.synced = true;
			const int columns = ImGui::TableGetColumnCount();
			for (int n = 0; n < columns; ++n) {
				ImGui::TableSetColumnSortDirection(n, ImGuiSortDirection_None, false);
			}
			bool append = false;
			for (const auto& k : a_view.sort) {
				if (k.column >= 0 && k.column < columns) {
					ImGui::TableSetColumnSortDirection(k.column, k.descending ? ImGuiSortDirection_Descending : ImGuiSortDirection_Ascending, append);
					append = true;
				}
			}
		}
		if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
			a_view.sort.clear();
			for (int i = 0; i < specs->SpecsCount; ++i) {
				const auto& s = specs->Specs[i];
				a_view.sort.push_back({ s.ColumnIndex, s.SortDirection == ImGuiSortDirection_Descending });
			}
			specs->SpecsDirty = false;
		}
	}

	bool MatchesText(const TableRow& a_row, std::string_view a_filter)
	{
		const auto words = Words(a_filter);
		if (words.empty()) {
			return true;
		}
		std::string hay;
		for (const auto& c : a_row.cells) {
			hay += core::ToLower(c.text);
			hay += '\n';
		}
		return std::ranges::all_of(words, [&](const std::string& w) { return hay.find(w) != std::string::npos; });
	}

	void FilterAndSort(TableView& a_view, std::vector<TableRow>& a_rows, DrawerTable a_table)
	{
		a_view.total = static_cast<int>(a_rows.size());
		a_view.present = 0;
		a_view.kindList.clear();
		for (const auto& r : a_rows) {
			a_view.present |= r.tags;
			if (!r.kind.empty()) {
				const auto it = std::ranges::find_if(a_view.kindList, [&](const auto& k) { return core::IEquals(k.first, r.kind); });
				if (it == a_view.kindList.end()) {
					a_view.kindList.emplace_back(r.kind, 1);
				} else {
					++it->second;
				}
			}
		}
		std::ranges::sort(a_view.kindList, [](const auto& a, const auto& b) {
			const bool ao = a.first == "Other", bo = b.first == "Other";  // the catch-all last
			return ao != bo ? bo : CompareNoCase(a.first, b.first) < 0;
		});
		// Quick filters: a row needs one chip that is on in every group that has one on.
		const auto quick = TableQuickFilters(a_table);
		std::uint32_t groups = 0;
		for (const auto& q : quick) {
			if (a_view.quick & q.bit) {
				groups |= 1u << q.group;
			}
		}
		const auto words = Words(a_view.filter);
		if (groups != 0 || !words.empty() || !a_view.kinds.empty()) {
			std::erase_if(a_rows, [&](const TableRow& a_row) {
				if (!a_view.kinds.empty() && std::ranges::none_of(a_view.kinds, [&](const std::string& k) { return core::IEquals(k, a_row.kind); })) {
					return true;
				}
				std::uint32_t matched = 0;
				for (const auto& q : quick) {
					if ((a_view.quick & q.bit) && (a_row.tags & q.bit)) {
						matched |= 1u << q.group;
					}
				}
				return matched != groups || !MatchesText(a_row, a_view.filter);
			});
		}
		a_view.shown = static_cast<int>(a_rows.size());
		if (a_view.sort.empty()) {
			return;
		}
		std::ranges::stable_sort(a_rows, [&](const TableRow& a_lhs, const TableRow& a_rhs) {
			for (const auto& k : a_view.sort) {
				const auto col = static_cast<std::size_t>(k.column);
				if (col >= a_lhs.cells.size() || col >= a_rhs.cells.size()) {
					continue;
				}
				const auto& l = a_lhs.cells[col];
				const auto& r = a_rhs.cells[col];
				if (l.group != r.group) {
					return l.group < r.group;  // "ignored", empty notes: after the real values either way
				}
				int c = CompareCells(l, r);
				if (k.descending) {
					c = -c;
				}
				if (c != 0) {
					return c < 0;
				}
			}
			return false;
		});
	}

	float FilterBarWidth(const TableView& a_view)
	{
		const auto& t = w::Th();
		float       width = BoxWidth();
		if (const auto count = CountText(a_view); !count.empty()) {
			width += w::TextWidth(count.c_str()) + t.gap;
		}
		return width;
	}

	void DrawFilterBar(TableView& a_view, DrawerTable a_table)
	{
		const auto& t = w::Th();
		ImGui::PushID(TableName(a_table));
		if (const auto count = CountText(a_view); !count.empty()) {
			ImGui::AlignTextToFramePadding();
			w::Muted(count.c_str());
			ImGui::SameLine(0.0f, t.gap);
		}
		w::SearchBox("filter", a_view.filter, "Filter rows", BoxWidth());
		ImGui::PopID();
	}

	std::optional<DrawerTable> ParseTableName(std::string_view a_text)
	{
		for (int i = 0; i < static_cast<int>(DrawerTable::Count_); ++i) {
			if (core::IEquals(a_text, TableName(static_cast<DrawerTable>(i)))) {
				return static_cast<DrawerTable>(i);
			}
		}
		return std::nullopt;
	}

	std::optional<SortArg> ParseSortArg(std::string_view a_text, std::string* a_error)
	{
		auto fail = [&](std::string a_why) -> std::optional<SortArg> {
			if (a_error) {
				*a_error = std::move(a_why);
			}
			return std::nullopt;
		};
		const auto colon = a_text.find(':');
		if (colon == std::string_view::npos) {
			return fail("--sort wants table:column[:desc] (containers, displays, doors or preview)");
		}
		const auto table = ParseTableName(a_text.substr(0, colon));
		if (!table) {
			return fail("--sort: no table named \"" + std::string(a_text.substr(0, colon)) + "\" (containers, displays, doors, preview)");
		}
		auto column = a_text.substr(colon + 1);
		bool desc = false;
		if (const auto last = column.rfind(':'); last != std::string_view::npos) {
			const auto dir = column.substr(last + 1);
			if (core::IEquals(dir, "desc")) {
				desc = true;
			} else if (!core::IEquals(dir, "asc")) {
				return fail("--sort: the direction is asc or desc, not \"" + std::string(dir) + "\"");
			}
			column = column.substr(0, last);
		}
		const auto columns = TableColumns(*table);
		int        index = -1;
		for (std::size_t i = 0; i < columns.size(); ++i) {
			// The header, or the header without its unit: "Height" names "Height (m)".
			const std::string_view name(columns[i]);
			const auto             unit = name.find(" (");
			if (!name.starts_with("##") && (core::IEquals(column, name) || (unit != std::string_view::npos && core::IEquals(column, name.substr(0, unit))))) {
				index = static_cast<int>(i);
			}
		}
		if (index < 0 && !column.empty() && column.size() <= 2 && std::ranges::all_of(column, [](char c) { return c >= '0' && c <= '9'; })) {
			const int n = std::stoi(std::string(column));
			if (n >= 1 && n <= static_cast<int>(columns.size()) && !std::string_view(columns[static_cast<std::size_t>(n - 1)]).starts_with("##")) {
				index = n - 1;
			}
		}
		if (index < 0) {
			std::string names;
			for (const char* c : columns) {
				if (!std::string_view(c).starts_with("##")) {
					names += names.empty() ? "" : ", ";
					names += core::ToLower(c);
				}
			}
			return fail("--sort: " + std::string(TableName(*table)) + " has no column \"" + std::string(column) + "\" (" + names + ")");
		}
		return SortArg{ *table, { index, desc } };
	}

	std::optional<std::uint32_t> QuickFilterBit(DrawerTable a_table, std::string_view a_label)
	{
		for (const auto& q : TableQuickFilters(a_table)) {
			if (core::IEquals(a_label, q.label)) {
				return q.bit;
			}
		}
		return std::nullopt;
	}
}
