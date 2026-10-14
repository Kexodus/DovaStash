// The command palette (docs/EDITOR-UX.md §2.9, Ctrl+K): one text field over the viewport. Typing matches, in
// this order, commands, homes (opens), the open home's containers and displays (selects and frames) and, in paint
// mode, categories (sets the brush). Substring first, then the letters in order. Esc closes.

#include <algorithm>
#include <format>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "Util.h"
#include "ui/App.h"
#include "ui/Common.h"
#include "ui/Themes.h"

namespace dova::editor::ui
{
	using namespace detail;

	namespace
	{
		// 2: substring, 1: the letters in order, 0: no match.
		int Match(std::string_view a_text, std::string_view a_query)
		{
			if (a_query.empty() || IContains(a_text, a_query)) {
				return 2;
			}
			std::size_t k = 0;
			for (const char c : a_text) {
				if (k < a_query.size() && std::tolower(static_cast<unsigned char>(c)) == std::tolower(static_cast<unsigned char>(a_query[k]))) {
					++k;
				}
			}
			return k == a_query.size() ? 1 : 0;
		}
	}

	std::vector<EditorApp::Command> EditorApp::Commands()
	{
		const bool cell = _cell != nullptr;
		const bool idle = !Busy();
		std::vector<Command> out;
		auto add = [&](std::string a_label, std::string a_key, bool a_enabled, std::function<void()> a_run) {
			out.push_back({ std::move(a_label), std::move(a_key), a_enabled, std::move(a_run) });
		};
		add("Save", "Ctrl+S", cell, [this] { RequestSave(); });
		add("Save as...", "", cell, [this] { SaveTemplateAs(*_cell); });
		add("Open template file...", "Ctrl+O", _ws != nullptr, [this] { OpenTemplateFile(); });
		add("Rescan load order", "F5", _ws != nullptr, [this] { Rescan(); });
		add("Draft this home", "", cell, [this] { DraftThisHome(); });
		add("Draft all homes", "Ctrl+D", _ws != nullptr, [this] { StartBulkDraft(); });
		add("Accept all suggestions", "", cell && idle, [this] { AcceptAll(); });
		add("Reject the weak suggestions", "", cell && idle, [this] { RejectWeak(); });
		add("Draft again", "", cell && idle, [this] { DraftAgain(); });
		add(std::format("Save {} draft{}...", SaveableDrafts(), SaveableDrafts() == 1 ? "" : "s"), "", SaveableDrafts() > 0, [this] { _confirmSaveAll = true; });
		add("Undo", "Ctrl+Z", !_undo.empty(), [this] { Undo(); });
		add("Redo", "Ctrl+Y", !_redo.empty(), [this] { Redo(); });
		add("3D view", "1", cell, [this] { _state.ui.view = 0; });
		add("Plan", "2", cell, [this] { _state.ui.view = 1; });
		add("Overview", "Home", cell, [this] { Overview(); });
		add("Frame selection", "F", cell && _selectedRef >= 0, [this] { FrameSelected(); });
		add("Select mode", "S", cell, [this] {
			SetPaint(false);
			_selectMode = true;
		});
		add("Paint mode", "P", cell, [this] { SetPaint(true); });
		add("Colours off", "", cell, [this] { _colours = Colours::None; });
		add("Colours by category", "C", cell, [this] { _colours = Colours::Category; });
		add("Colours by fill", "V", cell, [this] { _colours = Colours::Fill; });
		add("Layers off: containers and displays only", "", cell, [this] {
			_view.showStructure = _view.showDoors = _view.showFurniture = _view.showActors = _view.showEffects = false;
			_showMarkers = false;
			_view.showContainers = _view.showDisplays = true;
		});
		add("Layers on: everything", "", cell, [this] {
			_view.showStructure = _view.showContainers = _view.showDisplays = _view.showDoors = _view.showFurniture = _view.showActors = _view.showEffects = true;
		});
		add("Layers...", "L", cell, [this] { w::OpenPopover("layers"); });
		add("Homes rail", "Ctrl+1", true, [this] { _state.ui.railOpen = !_state.ui.railOpen; });
		add("Drawer", "Ctrl+2", cell, [this] { _state.ui.drawerOpen = !_state.ui.drawerOpen; });
		add("Inspector", "Ctrl+3", true, [this] { _state.ui.inspectorOpen = !_state.ui.inspectorOpen; });
		auto drawer = [this](DrawerTab a_tab) {
			return [this, a_tab] {
				_drawerTab = a_tab;
				_state.ui.drawerOpen = true;
			};
		};
		add("Containers list", "", cell, drawer(DrawerTab::Containers));
		add("Displays list", "", cell, drawer(DrawerTab::Displays));
		add("Doors", "", cell, drawer(DrawerTab::Doors));
		add("Where items go", "", cell, drawer(DrawerTab::Preview));
		auto panel = [this](Panel a_panel) {
			return [this, a_panel] {
				_panel = a_panel;
				_state.ui.inspectorOpen = true;
			};
		};
		add("Selection panel", "", cell, panel(Panel::Selection));
		add("Template panel", "", cell, panel(Panel::Template));
		add("Draft panel", "", cell, panel(Panel::Draft));
		add("Checks", "", cell, panel(Panel::Checks));
		add("Back to the cell before", "", !_history.empty() && idle, [this] {
			auto [lo, key] = _history.back();
			_history.pop_back();
			OpenCellAsync(lo, key, false, true);
		});
		add("Clear the selection", "Esc", !Selection().empty(), [this] { Select(-1); });
		add("Remove the selection from the template", "Delete", cell && !Selection().empty(), [this] { RequestDelete(); });
		add("Make the nearest chest the master", "", cell && idle, [this] { MakeNearestMaster(*_cell); });
		add("Start", "", true, [this] { _showStart = true; });
		add("Setup", "", true, [this] { w::OpenPopover("setup"); });
		add("UI size: larger", "Ctrl+=", true, [this] { StepTextScale(1); });
		add("UI size: smaller", "Ctrl+-", true, [this] { StepTextScale(-1); });
		add("UI size: auto", "Ctrl+0", true, [this] { SetTextScale(0.0f); });
		for (const auto& theme : w::AllThemes()) {
			add("Theme: " + theme.name, "", true, [this, name = theme.name] { PickTheme(name); });
		}
		add("Shortcuts", "", true, [this] { _showShortcuts = true; });
		return out;
	}

	void EditorApp::DrawPalette()
	{
		if (!_paletteOpen) {
			return;
		}
		const auto& t = w::Th();
		struct Item
		{
			std::string           label;
			std::string           detail;  // muted, after the label
			std::string           right;   // shortcut or kind
			bool                  enabled{ true };
			int                   score{ 0 };
			std::function<void()> run;
		};
		std::vector<Item> items;
		const std::string query = std::string(Trim(_palette));
		for (auto& c : Commands()) {
			if (const int s = Match(c.label, query); s > 0) {
				items.push_back({ c.label, {}, c.shortcut, c.enabled, s, std::move(c.run) });
			}
		}
		if (!query.empty()) {
			if (_homes) {
				int n = 0;
				for (std::size_t i = 0; i < _homes->homes.size() && n < 40; ++i) {
					const auto& h = _homes->homes[i];
					if (const int s = std::max(Match(h.name, query), IContains(h.plugin, query) ? 1 : 0); s > 0) {
						items.push_back({ h.name, h.plugin, "home", true, s, [this, i] { OpenHome(static_cast<int>(i)); } });
						++n;
					}
				}
			}
			if (_cell) {
				int n = 0;
				for (std::size_t i = 0; i < _cell->info.refs.size() && n < 40; ++i) {
					const auto& r = _cell->info.refs[i];
					if (!r.IsContainer() && !r.display) {
						continue;
					}
					std::string label = RefTitle(r);
					if (const auto* b = _cell->layout.FindContainer(_cell->lo->ToFormRef(r.key)); b && !b->label.empty() && b->label != label) {
						label = b->label + " (" + label + ")";
					}
					if (const int s = std::max(Match(label, query), IContains(_cell->refText[i], query) ? 1 : 0); s > 0) {
						items.push_back({ label, _cell->refText[i], r.display ? "display" : "container", true, s, [this, i] { SelectInView(static_cast<int>(i), true); } });
						++n;
					}
				}
			}
			if (_paint) {
				for (const auto& c : _categories) {
					const auto pattern = c.match.Empty() ? c.id + ".*" : c.id;
					if (const int s = std::max(Match(pattern, query), Match(c.name, query)); s > 0) {
						items.push_back({ pattern, c.name, "category", true, s, [this, pattern] {
											 _paintCategory = pattern;
											 _paintMessage.clear();
										 } });
					}
				}
			}
			// Substring matches first, keeping the kinds' order (commands, homes, containers, categories).
			std::ranges::stable_sort(items, [](const Item& a, const Item& b) { return a.score > b.score; });
		}
		_paletteSel = std::clamp(_paletteSel, 0, std::max(static_cast<int>(items.size()) - 1, 0));

		const float  vw = _viewportMax.x - _viewportMin.x;
		const float  width = std::clamp(vw - 40.0f * t.scale, 300.0f * t.scale, 600.0f * t.scale);
		const ImVec2 pos(_viewportMin.x + std::max((vw - width) * 0.5f, 0.0f), _viewportMin.y + 48.0f * t.scale);
		ImGui::SetNextWindowPos(pos);
		ImGui::SetNextWindowSize(ImVec2(width, 0));
		if (_paletteFocus) {
			ImGui::SetNextWindowFocus();
		}
		ImGui::PushStyleColor(ImGuiCol_WindowBg, t.bg2);
		ImGui::PushStyleColor(ImGuiCol_Border, t.accentDim);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(t.pad, t.pad));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f * t.scale);
		ImGui::Begin("##palette", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNavInputs);
		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor(2);
		if (_paletteFocus) {
			ImGui::SetKeyboardFocusHere();
		}
		bool run = false;
		if (w::SearchBox("palette", _palette, "Command, home, container, category")) {
			_paletteSel = 0;
		}
		const bool justOpened = std::exchange(_paletteFocus, false);
		if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
			_paletteSel = items.empty() ? 0 : (_paletteSel + 1) % static_cast<int>(items.size());
		} else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
			_paletteSel = items.empty() ? 0 : (_paletteSel + static_cast<int>(items.size()) - 1) % static_cast<int>(items.size());
		} else if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
			run = true;
		}
		w::Spacer(2.0f);
		const float rowH = t.rowH;
		const float listH = std::min(static_cast<float>(items.size()), 12.0f) * rowH;
		int         clicked = -1;
		if (!items.empty()) {
			ImGui::BeginChild("paletteitems", ImVec2(0, listH), ImGuiChildFlags_None, ImGuiWindowFlags_NoNavInputs);
			for (int i = 0; i < static_cast<int>(items.size()); ++i) {
				const auto& it = items[static_cast<std::size_t>(i)];
				ImGui::PushID(i);
				ImGui::BeginDisabled(!it.enabled);
				if (w::Row("item", it.label.c_str(), i == _paletteSel, it.right.c_str())) {
					clicked = i;
				}
				ImGui::EndDisabled();
				if (!it.detail.empty()) {
					const ImVec2 mn = ImGui::GetItemRectMin();
					ImGui::PushFont(t.smallFont);
					const float x = mn.x + t.pad + ImGui::CalcTextSize(it.label.c_str()).x + 2.0f * t.pad;
					ImGui::GetWindowDrawList()->AddText(ImVec2(x, mn.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f), t.muted, it.detail.c_str());
					ImGui::PopFont();
				}
				if (i == _paletteSel && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow))) {
					ImGui::SetScrollHereY(0.5f);
				}
				ImGui::PopID();
			}
			ImGui::EndChild();
		} else {
			w::Muted("Nothing matches");
		}
		const bool lostFocus = !justOpened && !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		ImGui::End();
		if (clicked >= 0) {
			_paletteSel = clicked;
			run = true;
		}
		if (run && _paletteSel < static_cast<int>(items.size()) && items[static_cast<std::size_t>(_paletteSel)].enabled) {
			_paletteOpen = false;
			items[static_cast<std::size_t>(_paletteSel)].run();
		} else if (ImGui::IsKeyPressed(ImGuiKey_Escape) || lostFocus) {
			_paletteOpen = false;
		}
	}
}
