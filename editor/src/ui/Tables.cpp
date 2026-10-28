// The drawer's tables: sort, filter box and quick filters (Tables.h, docs/EDITOR-UX.md §2.5).

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

		// The bits match the tags Drawer.cpp and Preview.cpp give their rows.
		constexpr QuickFilter kContainerQuick[] = {
			{ "Master", 1u << 0, 0, "The container for what has no other place" },
			{ "With categories", 1u << 1, 0, "Containers given categories or items" },
			{ "No categories", 1u << 2, 0, "Containers in the template with nothing to take" },
			{ "Ignored", 1u << 3, 0, "Containers the template ignores" },
			{ "Not listed", 1u << 4, 0, "Containers the template leaves out" },
			{ "Respawning", 1u << 5, 1, "Containers whose contents respawn" },
		};
		constexpr QuickFilter kDisplayQuick[] = {
			{ "Rack", 1u << 0, 0, "Weapon racks and shield plaques", true },
			{ "Mannequin", 1u << 1, 0, nullptr, true },
			{ "Plaque", 1u << 2, 0, "Plaques made for one item", true },
			{ "Case", 1u << 3, 0, "Display cases", true },
			{ "Bookshelf", 1u << 4, 0, nullptr, true },
			{ "Used", 1u << 5, 1, "Displays the template uses" },
			{ "Unused", 1u << 6, 1, "Displays the template ignores or leaves out" },
		};
		constexpr QuickFilter kPreviewQuick[] = {
			{ "Master", 1u << 1, 0, nullptr },
			{ "Receives items", 1u << 2, 0, "Containers some of the sample goes to" },
			{ "Receives nothing", 1u << 0, 0, "Containers no item of the sample goes to" },
		};

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
		float ChipGap() { return std::round(4.0f * w::Th().scale); }
	}

	void RightText(const char* a_text)
	{
		const float w = ImGui::CalcTextSize(a_text).x;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - w));
		ImGui::TextUnformatted(a_text);
	}

	void TableHeaders(DrawerTable a_table, std::uint32_t a_rightMask)
	{
		ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
		int n = 0;
		for (const char* name : TableColumns(a_table)) {
			const int column = n++;
			if (!ImGui::TableSetColumnIndex(column)) {
				continue;
			}
			if (a_rightMask & (1u << column)) {
				// TableHeader keeps the sort arrow's room at the cell's right edge: the label ends just before it.
				const ImGuiStyle& s = ImGui::GetStyle();
				const float       arrow = ImGui::GetFontSize() + s.FramePadding.x;
				const float       w = ImGui::CalcTextSize(name).x;
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - arrow - w));
			}
			ImGui::TableHeader(name);
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

	float FilterBarWidth(const TableView& a_view, DrawerTable a_table, bool a_folded)
	{
		const auto& t = w::Th();
		float       width = BoxWidth();
		int         visible = 0, on = 0;
		for (const auto& q : TableQuickFilters(a_table)) {
			if (Visible(q, a_view)) {
				++visible;
				on += (a_view.quick & q.bit) ? 1 : 0;
				width += a_folded ? 0.0f : w::ChipWidth(q.label) + ChipGap();
			}
		}
		if (a_folded && visible > 0) {
			width += w::PopoverWidth("Filters", w::Icon::ChevronDown, on) + t.gap;
		}
		if (const auto count = CountText(a_view); !count.empty()) {
			width += w::TextWidth(count.c_str()) + t.gap;
		}
		if (!a_view.kindList.empty()) {
			width += w::PopoverWidth("Kind", w::Icon::ChevronDown, static_cast<int>(a_view.kinds.size())) + t.gap;
		}
		return width + t.gap;  // between the chips and the box
	}

	void DrawFilterBar(TableView& a_view, DrawerTable a_table, bool a_folded)
	{
		const auto& t = w::Th();
		ImGui::PushID(TableName(a_table));
		if (const auto count = CountText(a_view); !count.empty()) {
			ImGui::AlignTextToFramePadding();
			w::Muted(count.c_str());
			ImGui::SameLine(0.0f, t.gap);
		}
		bool any = false;
		if (a_folded) {
			int visible = 0, on = 0;
			for (const auto& q : TableQuickFilters(a_table)) {
				if (Visible(q, a_view)) {
					++visible;
					on += (a_view.quick & q.bit) ? 1 : 0;
				}
			}
			if (visible > 0) {
				if (w::BeginPopover("quickfilters", "Filters", w::Icon::ChevronDown, on, "Quick filters")) {
					for (const auto& q : TableQuickFilters(a_table)) {
						if (!Visible(q, a_view)) {
							continue;
						}
						bool v = (a_view.quick & q.bit) != 0;
						if (w::Check(q.label, v, q.tip)) {
							a_view.quick ^= q.bit;
						}
					}
					if (on > 0 && w::LinkButton("Clear")) {
						a_view.quick = 0;
					}
					w::EndPopover();
				}
				any = true;
			}
		} else {
			for (const auto& q : TableQuickFilters(a_table)) {
				if (!Visible(q, a_view)) {
					continue;
				}
				const bool on = (a_view.quick & q.bit) != 0;
				if (w::ChipButton(q.label, w::Kind::Neutral, on)) {
					a_view.quick ^= q.bit;
				}
				w::Tip(q.tip);
				ImGui::SameLine(0.0f, ChipGap());
				any = true;
			}
		}
		if (any) {
			ImGui::SameLine(0.0f, t.gap);
		}
		// The objects' kinds (their base names: chest, sack, barrel...), a check each.
		if (!a_view.kindList.empty()) {
			if (w::BeginPopover("kind", "Kind", w::Icon::ChevronDown, static_cast<int>(a_view.kinds.size()), "Show only these kinds")) {
				// The drawer is at the window's bottom: the list scrolls rather than leave the window.
				const ImGuiViewport* vp = ImGui::GetMainViewport();
				const float          room = vp->WorkPos.y + vp->WorkSize.y - ImGui::GetCursorScreenPos().y - 3.0f * t.rowH;
				ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, std::max(room, 4.0f * t.rowH)));
				ImGui::BeginChild("kinds", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY);
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
					ImGui::SameLine();
					w::Muted(std::to_string(n).c_str());
				}
				ImGui::EndChild();
				if (!a_view.kinds.empty() && w::LinkButton("All kinds")) {
					a_view.kinds.clear();
				}
				w::EndPopover();
			}
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
