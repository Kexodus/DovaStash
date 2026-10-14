// The bottom drawer (docs/EDITOR-UX.md §2.5): Containers · Displays · Doors · Where items go, the wide tables.
// Rows select the same object as the view (Ctrl adds, a double click frames it); the Doors tab opens the cells
// the home's doors lead to and joins them to the home. Collapsed it is its tab row; drag the top edge to resize.

#include <algorithm>
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
		// The tab row: a tab opens the drawer on it, the active one closes it.
		int containers = 0, displays = 0;
		for (const auto& r : c.info.refs) {
			containers += r.IsContainer() && !r.display ? 1 : 0;
			displays += r.display ? 1 : 0;
		}
		const std::string labels[] = { std::format("Containers {}###c", containers), std::format("Displays {}###d", displays),
			std::format("Doors {}###o", c.info.doors.size()), std::string("Where items go###p") };
		ImGui::SetCursorPos(ImVec2(t.pad, ImGui::GetCursorPosY() + 1.0f * t.scale));
		for (int i = 0; i < 4; ++i) {
			if (i > 0) {
				ImGui::SameLine(0.0f, 2.0f * t.scale);
			}
			const bool active = static_cast<int>(_drawerTab) == i;
			if (w::ToolButton(labels[i].c_str(), active && open)) {
				if (active && open) {
					_state.ui.drawerOpen = false;
				} else {
					_drawerTab = static_cast<DrawerTab>(i);
					_state.ui.drawerOpen = true;
				}
			}
		}
		if (open && _drawerTab == DrawerTab::Preview) {
			ImGui::SameLine(0.0f, 3.0f * t.pad);
			DrawPreviewControls(c);
		}
		ImGui::SameLine();
		const float b = t.toolbarH - 4.0f * t.scale;
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), W - b - t.pad));
		if (w::IconButton("toggle", open ? w::Icon::ChevronDown : w::Icon::ChevronUp, open ? "Hide the drawer" : "Show the drawer", "Ctrl+2")) {
			_state.ui.drawerOpen = !open;
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

	void EditorApp::DrawContainers(OpenCell& a_cell)
	{
		if (!ImGui::BeginTable("containers", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
			return;
		}
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Container");
		ImGui::TableSetupColumn("Role");
		ImGui::TableSetupColumn("Reference");
		ImGui::TableSetupColumn("Height", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.5f);
		ImGui::TableSetupColumn("Notes");
		ImGui::TableHeadersRow();
		for (std::size_t i = 0; i < a_cell.info.refs.size(); ++i) {
			const auto& r = a_cell.info.refs[i];
			if (!r.IsContainer()) {
				continue;
			}
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::PushID(static_cast<int>(i));
			if (ImGui::Selectable(RefTitle(r).c_str(), IsSelected(static_cast<int>(i)), ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
				TableRowClick(static_cast<int>(i));
			}
			w::Tip("Click: select · Ctrl: add · Double-click: frame");
			ImGui::PopID();
			ImGui::TableNextColumn();
			const auto ref = a_cell.lo->ToFormRef(r.key);
			if (a_cell.layout.IsMaster(ref)) {
				w::Coloured(w::Kind::Accent, "master");
			} else if (const auto* b = a_cell.layout.FindContainer(ref)) {
				if (b->ignored) {
					w::Muted("ignored");
				} else if (b->categories.empty() && b->forms.empty()) {
					w::Muted("no categories");
				} else {
					ImGui::TextUnformatted(Join(b->categories).c_str());
				}
			} else {
				w::Muted("not listed");
			}
			ImGui::TableNextColumn();
			w::Mono(a_cell.refText[i].c_str());
			ImGui::TableNextColumn();
			ImGui::Text("%.0f", r.position[2]);
			ImGui::TableNextColumn();
			std::vector<std::string> notes;
			if (r.respawns) {
				notes.emplace_back("respawns");
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
			w::Muted(Join(notes, "; ").c_str());
		}
		ImGui::EndTable();
	}

	void EditorApp::DrawDisplays(OpenCell& a_cell)
	{
		if (!ImGui::BeginTable("displays", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
			return;
		}
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Display");
		ImGui::TableSetupColumn("Kind");
		ImGui::TableSetupColumn("In the template");
		ImGui::TableSetupColumn("Reference");
		ImGui::TableSetupColumn("Height", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.5f);
		ImGui::TableHeadersRow();
		for (std::size_t i = 0; i < a_cell.info.refs.size(); ++i) {
			const auto& r = a_cell.info.refs[i];
			if (!r.display) {
				continue;
			}
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::PushID(static_cast<int>(i));
			if (ImGui::Selectable(RefTitle(r).c_str(), IsSelected(static_cast<int>(i)), ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
				TableRowClick(static_cast<int>(i));
			}
			ImGui::PopID();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(home::DisplayLabel(r).c_str());
			ImGui::TableNextColumn();
			if (const auto* d = a_cell.layout.FindDisplay(a_cell.lo->ToFormRef(r.key))) {
				if (d->ignored) {
					w::Muted("ignored");
				} else {
					ImGui::TextUnformatted(d->categories.empty() ? "used" : Join(d->categories).c_str());
				}
			} else {
				w::Muted("-");
			}
			ImGui::TableNextColumn();
			w::Mono(a_cell.refText[i].c_str());
			ImGui::TableNextColumn();
			ImGui::Text("%.0f", r.position[2]);
		}
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
		if (!ImGui::BeginTable("doors", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV)) {
			return;
		}
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Leads to", ImGuiTableColumnFlags_WidthStretch, 0.45f);
		ImGui::TableSetupColumn("Cell", ImGuiTableColumnFlags_WidthStretch, 0.3f);
		ImGui::TableSetupColumn("In the home", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6.0f);
		ImGui::TableSetupColumn("##open", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.0f);
		ImGui::TableHeadersRow();
		for (const auto i : shown) {
			const auto& d = a_cell.info.doors[i];
			ImGui::PushID(static_cast<int>(i));
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("%s%s", d.destinationLabel.c_str(), count[i] > 1 ? std::format("  ({} doors)", count[i]).c_str() : "");
			ImGui::SameLine();
			w::Muted(d.destinationInterior ? "interior" : "exterior");
			ImGui::TableNextColumn();
			w::Mono(a_cell.doorText[i].c_str());
			ImGui::TableNextColumn();
			if (d.destinationCell) {
				const auto ref = a_cell.lo->ToFormRef(*d.destinationCell);
				const bool main = layout.cell && *layout.cell == ref;
				bool       in = main || std::ranges::any_of(layout.cells, [&](const core::HomeCell& c) { return c.cell == ref; });
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
			ImGui::TableNextColumn();
			ImGui::BeginDisabled(busy || !d.destinationCell || !d.destinationCell->Valid());
			if (ImGui::SmallButton("Open")) {
				OpenCellAsync(a_cell.lo, *d.destinationCell, true, true);
			}
			ImGui::EndDisabled();
			w::Tip("The template comes along; Back returns");
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
}
