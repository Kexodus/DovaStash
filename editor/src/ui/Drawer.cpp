// The bottom drawer (docs/EDITOR-UX.md §2.5): Containers · Displays · Doors · Where items go, the wide tables.
// Rows select the same object as the view (Ctrl adds, a double click frames it); on the Doors tab a double click
// opens the cell a door leads to, and the tick joins that cell to the home. Collapsed it is its tab row; drag the top edge to resize.
// Every table sorts by any column (Shift+click adds one) and has a filter box with quick filters (ui/Tables.h);
// rows keep their reference's index, so selection works on the filtered, sorted list.

#include <algorithm>
#include <cstdint>
#include <format>

#include <imgui.h>

#include "Util.h"
#include "ui/App.h"
#include "ui/Common.h"

namespace dova::editor::ui
{
	using namespace detail;

	void EditorApp::DrawDrawer()
	{
		const auto& t = w::Th();
		if (!_cell) {
			return;
		}
		auto&       c = *_cell;
		const bool  open = _state.ui.drawerOpen;
		const auto  W = ImGui::GetContentRegionAvail().x;
		ImGui::PushStyleColor(ImGuiCol_ChildBg, t.bg1);
		ImGui::BeginChild("drawer", ImVec2(W, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleColor();
		if (open) {
			w::SplitterTop("drawersplit", _state.ui.drawerHeight, 120.0f * t.scale, 4000.0f);
		} else {
			const ImVec2 p = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + W, p.y), t.line);
		}
		// The tab row: a tab opens the drawer on it, the active one folds it. That fold waits out the double-click
		// time: folding at once would move the tab row down, and a double click (on any tab) keeps the drawer open.
		const ImGuiIO& io = ImGui::GetIO();
		if (_drawerFoldAt >= 0.0) {
			if (io.MouseClicked[ImGuiMouseButton_Left] && io.MouseClickedCount[ImGuiMouseButton_Left] >= 2) {
				_drawerFoldAt = -1.0;
			} else if (ImGui::GetTime() >= _drawerFoldAt) {
				_drawerFoldAt = -1.0;
				_state.ui.drawerOpen = false;
			}
		}
		int containers = 0, displays = 0;
		for (const auto& r : c.info.refs) {
			containers += r.IsContainer() && !r.display ? 1 : 0;
			displays += r.display ? 1 : 0;
		}
		static constexpr const char* labels[] = { "Containers", "Displays", "Doors", "Where items go" };
		const int                    counts[] = { containers, displays, static_cast<int>(c.info.doors.size()), -1 };
		ImGui::SetCursorPos(ImVec2(t.pad, ImGui::GetCursorPosY() + 1.0f * t.scale));
		for (int i = 0; i < 4; ++i) {
			if (i > 0) {
				ImGui::SameLine(0.0f, 2.0f * t.scale);
			}
			const bool active = static_cast<int>(_drawerTab) == i;
			if (w::TabButton(labels[i], active && open, counts[i])) {
				const bool second = io.MouseReleased[ImGuiMouseButton_Left] && io.MouseClickedLastCount[ImGuiMouseButton_Left] >= 2;
				if (!(active && open)) {
					_drawerTab = static_cast<DrawerTab>(i);
					_state.ui.drawerOpen = true;
					if (!open) {
						w::LayoutClicked();  // the tab row moves up
					}
					_drawerFoldAt = -1.0;
				} else if (!second && _drawerFoldAt < 0.0) {
					_drawerFoldAt = io.MouseClickedTime[ImGuiMouseButton_Left] + static_cast<double>(io.MouseDoubleClickTime);
				}
			}
		}
		if (open && _drawerTab == DrawerTab::Preview) {
			ImGui::SameLine(0.0f, 3.0f * t.pad);
			DrawPreviewControls(c);
		}
		const float b = t.toolbarH - 4.0f * t.scale;
		if (open) {
			// The table's filter at the right, before the chevron: "N of M", the quick filters, the box.
			const auto table = static_cast<DrawerTable>(_drawerTab);
			auto&      view = Table(_drawerTab);
			ImGui::SameLine();
			ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + 2.0f * t.pad, W - b - 2.0f * t.pad - FilterBarWidth(view, table)));
			DrawFilterBar(view, table);
		}
		ImGui::SameLine();
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), W - b - t.pad));
		if (w::IconButton("toggle", open ? w::Icon::ChevronDown : w::Icon::ChevronUp, open ? "Hide the drawer" : "Show the drawer", "Ctrl+2")) {
			_state.ui.drawerOpen = !open;
			_drawerFoldAt = -1.0;
			w::LayoutClicked();  // the chevron moves with the tab row
		}
		if (open) {
			w::BeginPadded("drawerbody", ImVec2(0, 0), ImGuiWindowFlags_NoScrollbar);
			switch (_drawerTab) {
			case DrawerTab::Containers:
				DrawContainers(c);
				break;
			case DrawerTab::Displays:
				DrawDisplays(c);
				break;
			case DrawerTab::Doors:
				DrawDoors(c);
				break;
			case DrawerTab::Preview:
				DrawPreview(c);
				break;
			}
			w::EndPadded();
		}
		ImGui::EndChild();
	}

	void EditorApp::TableRowClick(int a_index)
	{
		const bool twice = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
		if (ImGui::GetIO().KeyCtrl) {
			ToggleMulti(a_index);
		} else {
			SelectInView(a_index, twice);
		}
	}

	namespace
	{
		constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | kSortableTable;

		// The columns of a drawer table (ui/Tables.h names them); `a_setup` gives a column its flags and width.
		template <class F>
		void SetupColumns(DrawerTable a_table, F&& a_setup)
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			int n = 0;
			for (const char* name : TableColumns(a_table)) {
				ImGuiTableColumnFlags flags = ImGuiTableColumnFlags_None;
				float                 width = 0.0f;
				a_setup(n++, flags, width);
				ImGui::TableSetupColumn(name, flags, width);
			}
		}

		// A value in a role-like column: group -1 in accent (the master), 1 muted (ignored, not listed), else text.
		void RoleText(const TableCell& a_cell)
		{
			if (a_cell.group < 0) {
				w::Coloured(w::Kind::Accent, a_cell.text.c_str());
			} else if (a_cell.group > 0) {
				w::Muted(a_cell.text.c_str());
			} else {
				ImGui::TextUnformatted(a_cell.text.c_str());
			}
		}

		TableCell Height(const home::PlacedRef& a_ref)
		{
			return { std::format("{:.0f}", a_ref.position[2]), static_cast<double>(a_ref.position[2]) };
		}

		// The table has rows but the filters hide them all.
		void NoMatch(const TableView& a_view)
		{
			if (a_view.total > 0 && a_view.shown == 0) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				w::Muted("No row matches the filter");
			}
		}

		// The quick filters' bits (ui/Tables.cpp lists the chips).
		enum ContainerTag : std::uint32_t
		{
			kMaster = 1u << 0,
			kWithCategories = 1u << 1,
			kNoCategories = 1u << 2,
			kIgnored = 1u << 3,
			kNotListed = 1u << 4,
			kRespawning = 1u << 5
		};
		enum DisplayTag : std::uint32_t
		{
			kRack = 1u << 0,  // DisplayKind order: rack, mannequin, item (plaque), case, bookshelf
			kUsed = 1u << 5,
			kUnused = 1u << 6
		};
	}

	void EditorApp::DrawContainers(OpenCell& a_cell)
	{
		auto& view = Table(DrawerTab::Containers);
		std::vector<TableRow> rows;
		for (std::size_t i = 0; i < a_cell.info.refs.size(); ++i) {
			const auto& r = a_cell.info.refs[i];
			if (!r.IsContainer()) {
				continue;
			}
			TableRow row;
			row.index = static_cast<int>(i);
			row.cells.resize(6);
			row.cells[0].text = RefTitle(r);
			row.cells[1].text = ContainerKind(r);
			row.cells[1].group = row.cells[1].text == "Other" ? 1 : 0;
			row.kind = row.cells[1].text;
			auto&      role = row.cells[2];
			const auto ref = a_cell.lo->ToFormRef(r.key);
			if (a_cell.layout.IsMaster(ref)) {
				role = Grouped("master", -1);
				row.tags |= kMaster;
			} else if (const auto* b = a_cell.layout.FindContainer(ref)) {
				if (b->ignored) {
					role = Grouped("ignored", 1);
					row.tags |= kIgnored;
				} else if (b->categories.empty() && b->forms.empty()) {
					role = Grouped("no categories", 1);
					row.tags |= kNoCategories;
				} else {
					role.text = Join(b->categories);
					row.tags |= kWithCategories;
				}
			} else {
				role = Grouped("not listed", 1);
				row.tags |= kNotListed;
			}
			row.cells[3].text = a_cell.refText[i];
			row.cells[4] = Height(r);
			std::vector<std::string> notes;
			if (r.respawns) {
				notes.emplace_back("respawns");
				row.tags |= kRespawning;
			}
			if (r.initiallyDisabled) {
				notes.emplace_back("initially disabled");
			}
			if (r.enableParent && r.enableOpposite) {
				notes.emplace_back("goes away with an upgrade");
			}
			for (const auto& s : r.scripts) {
				notes.push_back(s);
			}
			row.cells[5].text = Join(notes, "; ");
			row.cells[5].group = notes.empty() ? 1 : 0;
			rows.push_back(std::move(row));
		}
		if (!ImGui::BeginTable("containers", 6, kTableFlags | ImGuiTableFlags_Resizable)) {
			return;
		}
		SetupColumns(DrawerTable::Containers, [](int a_n, ImGuiTableColumnFlags& a_flags, float& a_width) {
			if (a_n == 1 || a_n == 4) {
				a_flags = ImGuiTableColumnFlags_WidthFixed;
				a_width = ImGui::GetFontSize() * (a_n == 1 ? 6.0f : 3.5f);
			}
		});
		SyncSort(view);
		FilterAndSort(view, rows, DrawerTable::Containers);
		ImGui::TableHeadersRow();
		for (const auto& row : rows) {
			const int   i = row.index;
			const auto& r = a_cell.info.refs[static_cast<std::size_t>(i)];
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::PushID(i);
			w::IconInline(IconOf(r), w::Th().muted);
			ImGui::SameLine(0.0f, w::Th().gap);
			if (ImGui::Selectable(row.cells[0].text.c_str(), IsSelected(i), ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
				TableRowClick(i);
			}
			w::Tip("Click: select · Ctrl: add · Double-click: frame");
			ImGui::PopID();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(row.cells[1].text.c_str());
			ImGui::TableNextColumn();
			RoleText(row.cells[2]);
			ImGui::TableNextColumn();
			w::Mono(row.cells[3].text.c_str());
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(row.cells[4].text.c_str());
			ImGui::TableNextColumn();
			w::Muted(row.cells[5].text.c_str());
		}
		NoMatch(view);
		ImGui::EndTable();
	}

	void EditorApp::DrawDisplays(OpenCell& a_cell)
	{
		auto& view = Table(DrawerTab::Displays);
		std::vector<TableRow> rows;
		for (std::size_t i = 0; i < a_cell.info.refs.size(); ++i) {
			const auto& r = a_cell.info.refs[i];
			if (!r.display) {
				continue;
			}
			TableRow row;
			row.index = static_cast<int>(i);
			row.cells.resize(5);
			row.cells[0].text = RefTitle(r);
			row.cells[1].text = home::DisplayLabel(r);
			row.tags |= kRack << static_cast<std::uint32_t>(*r.display);
			auto& used = row.cells[2];
			if (const auto* d = a_cell.layout.FindDisplay(a_cell.lo->ToFormRef(r.key))) {
				if (d->ignored) {
					used = Grouped("ignored", 1);
					row.tags |= kUnused;
				} else {
					used.text = d->categories.empty() ? "used" : Join(d->categories);
					row.tags |= kUsed;
				}
			} else {
				used = Grouped("-", 1);
				row.tags |= kUnused;
			}
			row.cells[3].text = a_cell.refText[i];
			row.cells[4] = Height(r);
			rows.push_back(std::move(row));
		}
		if (!ImGui::BeginTable("displays", 5, kTableFlags | ImGuiTableFlags_Resizable)) {
			return;
		}
		SetupColumns(DrawerTable::Displays, [](int a_n, ImGuiTableColumnFlags& a_flags, float& a_width) {
			if (a_n == 4) {
				a_flags = ImGuiTableColumnFlags_WidthFixed;
				a_width = ImGui::GetFontSize() * 3.5f;
			}
		});
		SyncSort(view);
		FilterAndSort(view, rows, DrawerTable::Displays);
		ImGui::TableHeadersRow();
		for (const auto& row : rows) {
			const int   i = row.index;
			const auto& r = a_cell.info.refs[static_cast<std::size_t>(i)];
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::PushID(i);
			w::IconInline(IconOf(r), w::Th().muted);
			ImGui::SameLine(0.0f, w::Th().gap);
			if (ImGui::Selectable(row.cells[0].text.c_str(), IsSelected(i), ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
				TableRowClick(i);
			}
			w::Tip("Click: select · Ctrl: add · Double-click: frame");
			ImGui::PopID();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(row.cells[1].text.c_str());
			ImGui::TableNextColumn();
			RoleText(row.cells[2]);
			ImGui::TableNextColumn();
			w::Mono(row.cells[3].text.c_str());
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(row.cells[4].text.c_str());
		}
		NoMatch(view);
		ImGui::EndTable();
	}

	void EditorApp::DrawDoors(OpenCell& a_cell)
	{
		if (a_cell.info.doors.empty()) {
			w::Muted("No doors to other cells.");
			return;
		}
		const bool busy = Busy();
		auto&      layout = a_cell.layout;
		// One line per place a door leads to (a Hearthfire home has a door per building stage).
		std::vector<std::size_t> shown;
		std::vector<int>         count(a_cell.info.doors.size(), 0);
		for (std::size_t i = 0; i < a_cell.info.doors.size(); ++i) {
			const auto& d = a_cell.info.doors[i];
			const auto  same = std::ranges::find_if(shown, [&](std::size_t j) {
                const auto& o = a_cell.info.doors[j];
                return d.destinationCell ? o.destinationCell == d.destinationCell : (!o.destinationCell && o.destinationLabel == d.destinationLabel);
			});
			if (same == shown.end()) {
				shown.push_back(i);
				count[i] = 1;
			} else {
				++count[*same];
			}
		}
		auto& view = Table(DrawerTab::Doors);
		std::vector<TableRow> rows;
		for (const auto i : shown) {
			const auto& d = a_cell.info.doors[i];
			TableRow    row;
			row.index = static_cast<int>(i);
			row.cells.resize(4);
			row.cells[0].text = d.destinationLabel + (count[i] > 1 ? std::format("  ({} doors)", count[i]) : std::string{});
			row.cells[1].text = a_cell.doorText[i];
			// In the home: the main cell, the template's cells, then the others (a tick, so no text).
			double in = 0.0;
			if (d.destinationCell) {
				const auto ref = a_cell.lo->ToFormRef(*d.destinationCell);
				in = layout.cell && *layout.cell == ref ? 2.0 : std::ranges::any_of(layout.cells, [&](const core::HomeCell& c) { return c.cell == ref; }) ? 1.0 : 0.0;
			}
			row.cells[2].number = in;
			row.cells[3].text = d.destinationInterior ? "interior" : "exterior";  // shown after the name; the box finds it
			rows.push_back(std::move(row));
		}
		if (!ImGui::BeginTable("doors", 3, kTableFlags)) {
			return;
		}
		SetupColumns(DrawerTable::Doors, [](int a_n, ImGuiTableColumnFlags& a_flags, float& a_width) {
			static constexpr float stretch[] = { 0.55f, 0.45f };
			if (a_n < 2) {
				a_flags = ImGuiTableColumnFlags_WidthStretch;
				a_width = stretch[a_n];
			} else {
				a_flags = ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending;
				a_width = ImGui::GetFontSize() * 6.0f;
			}
		});
		SyncSort(view);
		FilterAndSort(view, rows, DrawerTable::Doors);
		ImGui::TableHeadersRow();
		for (const auto& row : rows) {
			const auto  i = static_cast<std::size_t>(row.index);
			const auto& d = a_cell.info.doors[i];
			// The door's own reference: a click selects it (Ctrl adds), a double click opens the cell behind it.
			int door = -1;
			for (std::size_t k = 0; k < a_cell.info.refs.size() && door < 0; ++k) {
				door = a_cell.info.refs[k].key == d.door ? static_cast<int>(k) : -1;
			}
			const bool openable = d.destinationCell && d.destinationCell->Valid();
			ImGui::PushID(static_cast<int>(i));
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			w::IconInline(w::Icon::Door, w::Th().muted);
			ImGui::SameLine(0.0f, w::Th().gap);
			if (ImGui::Selectable(row.cells[0].text.c_str(), door >= 0 && IsSelected(door),
					ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap)) {
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					if (openable && !busy) {
						OpenCellAsync(a_cell.lo, *d.destinationCell, true, true);
					}
				} else if (door >= 0) {
					if (ImGui::GetIO().KeyCtrl) {
						ToggleMulti(door);
					} else {
						SelectInView(door);
					}
				}
			}
			w::Tip(openable ? "Click: select · Ctrl: add · Double-click: open the cell (the template comes along; Back returns)" : "Click: select · Ctrl: add");
			ImGui::SameLine();
			w::Muted(row.cells[3].text.c_str());
			ImGui::TableNextColumn();
			w::Mono(row.cells[1].text.c_str());
			ImGui::TableNextColumn();
			if (d.destinationCell) {
				const auto ref = a_cell.lo->ToFormRef(*d.destinationCell);
				const bool main = row.cells[2].number == 2.0;
				bool       in = row.cells[2].number > 0.0;
				ImGui::BeginDisabled(main);
				if (w::Check("##in", in)) {
					if (in) {
						core::HomeCell hc;
						hc.cell = ref;
						layout.cells.push_back(std::move(hc));
					} else {
						std::erase_if(layout.cells, [&](const core::HomeCell& c) { return c.cell == ref; });
					}
					a_cell.dirty = true;
					Revalidate(a_cell);
				}
				ImGui::EndDisabled();
				w::Tip(main ? "The home's main cell" : "Part of the home: its containers and displays can be used (the template's cells)");
			}
			ImGui::PopID();
		}
		NoMatch(view);
		ImGui::EndTable();
	}
}
