// The homes rail (docs/EDITOR-UX.md §2.2, EDITOR.md §2 steps 2-3): every player home of the active load order
// under the mod whose plugin defines its cell (MO2's names and priorities, the game's Data folder first), one
// search box (home, mod, plugin, Nexus id), a filter popover, a status chip per home, the unsaved dot, and per
// mod "All cells..." for a home the finder misses (every cell with containers of the mod's plugins, scored).
// The title row's home switcher draws the same list in compact form.

#include <algorithm>
#include <format>
#include <unordered_map>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>

#include <imgui.h>

#include "Util.h"
#include "ui/App.h"
#include "ui/Common.h"
#include "ui/WidgetsExtra.h"

namespace dova::editor::ui
{
	using namespace detail;

	namespace
	{
		std::string Summary(int a_containers, int a_displays)
		{
			return a_displays > 0 ? std::format("{} chests · {} displays", a_containers, a_displays) : std::format("{} chests", a_containers);
		}
	}

	void EditorApp::GroupHomes()
	{
		_homeGroups.clear();
		if (!_homes || !_ws) {
			return;
		}
		// Plugin (lower case) -> the mod providing it: the enabled mod with the highest priority (MO2's rule), -1 the game's Data folder.
		std::unordered_map<std::string, int> owner;
		for (const auto& p : _ws->BasePlugins()) {
			owner[core::ToLower(p)] = -1;
		}
		const auto& mods = _ws->Mods();
		for (std::size_t i = 0; i < mods.size(); ++i) {
			const auto& m = mods[i];
			if (m.separator || m.unmanaged || !m.enabled) {
				continue;
			}
			for (const auto& p : m.plugins) {
				owner[core::ToLower(p)] = static_cast<int>(i);  // Mods() is lowest priority first
			}
		}
		std::map<int, std::vector<int>> byMod;
		for (std::size_t i = 0; i < _homes->homes.size(); ++i) {
			const auto it = owner.find(core::ToLower(_homes->homes[i].plugin));
			byMod[it == owner.end() ? -2 : it->second].push_back(static_cast<int>(i));
		}
		// The game's Data folder, then the mods highest priority first (as MO2 lists them), then the rest.
		if (const auto it = byMod.find(-1); it != byMod.end()) {
			_homeGroups.push_back({ -1, std::move(it->second) });
		}
		for (auto it = byMod.rbegin(); it != byMod.rend() && it->first >= 0; ++it) {
			_homeGroups.push_back({ it->first, std::move(it->second) });
		}
		if (const auto it = byMod.find(-2); it != byMod.end()) {
			_homeGroups.push_back({ -2, std::move(it->second) });
		}
	}

	bool EditorApp::HomeMatches(const home::HomeEntry& a_home, const std::string& a_search, bool a_modMatch) const
	{
		if (_onlyWithoutTemplate && !HomeStatus(a_home).empty()) {
			return false;
		}
		return a_search.empty() || a_modMatch || IContains(a_home.name, a_search) || IContains(a_home.editorId, a_search) || IContains(a_home.plugin, a_search) ||
		       IContains(a_home.cellRef, a_search) ||
		       std::ranges::any_of(a_home.extra, [&](const home::HomeEntry::Extra& e) { return IContains(e.name, a_search) || IContains(e.editorId, a_search); });
	}

	namespace
	{
		std::string ModName(const Workspace& a_ws, int a_mod)
		{
			return a_mod >= 0 ? a_ws.Mods()[static_cast<std::size_t>(a_mod)].name : a_mod == -1 ? "Game Data folder" : "Plugins no mod provides";
		}

		bool ModMatches(const Workspace& a_ws, int a_mod, const std::string& a_search)
		{
			if (a_search.empty()) {
				return true;
			}
			if (a_mod >= 0) {
				const auto& m = a_ws.Mods()[static_cast<std::size_t>(a_mod)];
				return IContains(m.name, a_search) || (m.nexusId && std::to_string(*m.nexusId) == a_search);
			}
			return IContains(ModName(a_ws, a_mod), a_search);
		}
	}

	std::vector<int> EditorApp::VisibleHomes(const std::string& a_search) const
	{
		std::vector<int> out;
		if (!_homes || !_ws) {
			return out;
		}
		if (!_groupByMod) {
			for (std::size_t i = 0; i < _homes->homes.size(); ++i) {
				if (HomeMatches(_homes->homes[i], a_search, false)) {
					out.push_back(static_cast<int>(i));
				}
			}
			std::ranges::stable_sort(out, [&](int a, int b) { return _homes->homes[static_cast<std::size_t>(a)].name < _homes->homes[static_cast<std::size_t>(b)].name; });
			return out;
		}
		for (const auto& g : _homeGroups) {
			if (_collapsedGroups.contains(g.mod) && a_search.empty()) {
				continue;
			}
			const bool modMatch = !a_search.empty() && ModMatches(*_ws, g.mod, a_search);
			for (const int i : g.homes) {
				if (HomeMatches(_homes->homes[static_cast<std::size_t>(i)], a_search, modMatch)) {
					out.push_back(i);
				}
			}
		}
		return out;
	}

	std::string EditorApp::HomeChip(int a_index, w::Kind* a_kind) const
	{
		const auto& h = _homes->homes[static_cast<std::size_t>(a_index)];
		const auto& e = _homes->entries[static_cast<std::size_t>(a_index)];
		const auto  stage = _state.StageOf(h.cellRef);
		bool        user = false;
		const auto  status = HomeStatus(h, &user);
		auto        set = [&](w::Kind a_k) {
            if (a_kind) {
                *a_kind = a_k;
            }
		};
		if (stage == HomeStage::Saved) {
			set(w::Kind::Ok);
			return "saved";
		}
		if (user) {
			set(w::Kind::Ok);
			return "yours";
		}
		if (!status.empty()) {
			set(w::Kind::Info);
			return "shipped";
		}
		if (e.draft || stage != HomeStage::None) {
			set(w::Kind::Warn);
			return "drafted";
		}
		set(w::Kind::Neutral);
		return "none";
	}

	bool EditorApp::IsOpenHome(int a_index) const
	{
		if (!_cell || !_homes) {
			return false;
		}
		if (_cellDraft == a_index) {
			return true;
		}
		if (_cell->lo != _homes->lo) {
			return false;
		}
		const auto& h = _homes->homes[static_cast<std::size_t>(a_index)];
		return _cell->info.key == h.cell || std::ranges::any_of(h.extra, [&](const home::HomeEntry::Extra& x) { return x.cell == _cell->info.key; });
	}

	bool EditorApp::HomeUnsaved(int a_index) const
	{
		if (IsOpenHome(a_index) && _cell->dirty) {
			return true;
		}
		const auto& e = _homes->entries[static_cast<std::size_t>(a_index)];
		return e.reviewed && _state.StageOf(_homes->homes[static_cast<std::size_t>(a_index)].cellRef) != HomeStage::Saved;
	}

	int EditorApp::ActiveFilterCount() const
	{
		return (_onlyWithoutTemplate ? 1 : 0) + (_onlyEnabled ? 0 : 1) + (_likelyOnly ? 0 : 1) + (_exteriors ? 1 : 0) + (_groupByMod ? 0 : 1);
	}

	void EditorApp::HomeTooltip(const home::HomeEntry& a_home) const
	{
		if (!ImGui::BeginItemTooltip()) {
			return;
		}
		ImGui::TextUnformatted(a_home.name.c_str());
		w::Mono((a_home.cellRef + "  " + a_home.editorId).c_str());
		for (const auto& e : a_home.extra) {
			w::Muted(std::format("+ {} ({}){}", e.name.empty() ? e.editorId : e.name, e.cellRef, e.likely ? "" : ": storage behind a door").c_str());
		}
		w::Muted(("Why: " + (a_home.candidate.reasons.empty() ? std::string("only containers") : Join(a_home.candidate.reasons))).c_str());
		w::Muted(a_home.plugin.c_str());
		if (!a_home.lastPlugin.empty() && !core::IEquals(a_home.lastPlugin, a_home.plugin)) {
			w::Muted(("Last edited by " + a_home.lastPlugin).c_str());
		}
		ImGui::EndTooltip();
	}

	void EditorApp::HomeContextMenu(int a_index)
	{
		if (!ImGui::BeginPopupContextItem("homectx")) {
			return;
		}
		const auto& h = _homes->homes[static_cast<std::size_t>(a_index)];
		if (wx::MenuItem("Open")) {
			OpenHome(a_index);
		}
		if (wx::MenuItem("Draft")) {
			if (IsOpenHome(a_index)) {
				_panel = Panel::Draft;
			} else {
				_pendingPanel = Panel::Draft;
				OpenHome(a_index);
			}
			_state.ui.inspectorOpen = true;
		}
		// Its template file: the player's own first.
		const auto templates = _homes->templates.ForCell(_homes->lo->ToFormRef(h.cell));
		const home::KnownTemplate* file = nullptr;
		for (const auto* tp : templates) {
			if (!file || (tp->user && !file->user)) {
				file = tp;
			}
		}
		if (wx::MenuItem("Open in Explorer", nullptr, file != nullptr)) {
			const auto args = L"/select,\"" + file->path.wstring() + L"\"";
			::ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
		}
		w::Tip(file ? PathToUtf8(file->path).c_str() : "No template file yet");
		if (wx::MenuItem("Copy form ref")) {
			ImGui::SetClipboardText(h.cellRef.c_str());
		}
		ImGui::EndPopup();
	}

	void EditorApp::DrawRail()
	{
		const auto&       t = w::Th();
		const std::string collapsed = _homes ? std::format("{} homes", _homes->homes.size()) : std::string("Homes");
		if (!w::BeginRail("rail", w::Side::Left, _state.ui.railWidth, _state.ui.railOpen, 200.0f * t.scale, 560.0f * t.scale, collapsed.c_str())) {
			return;
		}
		if (wx::BeginPadded("railpad", ImVec2(0, 0), ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
			if (!_ws) {
				w::EmptyState("No setup", "Point the editor at your MO2 instance (Start).");
			} else {
				if (!_homes && !_homesRequested) {
					ScanHomes();
				}
				// "Draft all homes" is done: offer to save the drafts of homes without a template.
				const int saveable = SaveableDrafts();
				if (_bulkDone && !_bannerClosed && saveable > 0) {
					const int r = wx::Banner("drafted", std::format("{} draft{}", saveable, saveable == 1 ? "" : "s").c_str(), std::format("Save {} draft{}...", saveable, saveable == 1 ? "" : "s").c_str());
					if (r == 1) {
						_confirmSaveAll = true;
					} else if (r == 2) {
						_bannerClosed = true;
					}
				}
				// Search and filters (the rail focused: Up/Down/Enter walk the list).
				_railKeys = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
				const float filterW = t.toolbarH - 4.0f * t.scale;
				if (_railFocusSearch) {
					ImGui::SetKeyboardFocusHere();
					_railFocusSearch = false;
				}
				w::SearchBox("railsearch", _railSearch, "Home, mod, plugin, Nexus id", ImGui::GetContentRegionAvail().x - filterW - t.gap);
				ImGui::SameLine(0.0f, t.gap);
				if (w::BeginPopover("filter", nullptr, w::Icon::Filter, ActiveFilterCount(), "Filter")) {
					wx::CheckItem("Only homes without a template", &_onlyWithoutTemplate);
					wx::CheckItem("Only enabled mods", &_onlyEnabled, "Mods without homes found by the search: only the enabled ones");
					if (wx::CheckItem("Likely homes only", &_likelyOnly, "All cells...: a player-house location, owned by the player, or mannequins where nothing respawns")) {
					}
					if (wx::CheckItem("Exteriors", &_exteriors, "All cells...: exterior cells with containers too (a balcony, a garden)") && _selectedMod >= -1) {
						Scan();
					}
					wx::CheckItem("Group by mod", &_groupByMod);
					w::EndPopover();
				}
				// The list, then the footer.
				const float footerH = ImGui::GetTextLineHeightWithSpacing() + t.gap;
				ImGui::BeginChild("homelist", ImVec2(0, std::max(ImGui::GetContentRegionAvail().y - footerH, 40.0f)), ImGuiChildFlags_None, ImGuiWindowFlags_NoNavInputs);
				DrawHomeList(false, _railSearch);
				ImGui::EndChild();
				ImGui::PushFont(t.smallFont);
				ImGui::BeginDisabled(Busy());
				if (w::LinkButton("Rescan")) {
					Rescan();
				}
				ImGui::EndDisabled();
				w::Tip("Read the load order again after a change in MO2  F5");
				if (_homes) {
					ImGui::SameLine(0.0f, t.gap);
					const std::string summary =
						std::format("{} homes · {} plugins · {:.1f} s", _homes->homes.size(), _homes->lo ? _homes->lo->PluginCount() : 0, static_cast<double>(_homes->ms) / 1000.0);
					ImGui::PushClipRect(ImGui::GetCursorScreenPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 30.0f * t.scale, ImGui::GetCursorScreenPos().y + 50.0f), true);
					w::Muted(summary.c_str());
					ImGui::PopClipRect();
				}
				ImGui::PopFont();
			}
		}
		wx::EndPadded();
		w::EndRail();
	}

	void EditorApp::DrawHomeList(bool a_compact, std::string& a_search)
	{
		const auto& t = w::Th();
		if (a_compact) {
			if (ImGui::IsWindowAppearing()) {
				ImGui::SetKeyboardFocusHere();
			}
			w::SearchBox("switchsearch", a_search, "Home, mod, plugin, Nexus id");
			w::Spacer(2.0f * t.scale);
		}
		if (!_homes) {
			w::Muted(Busy() ? "Finding homes..." : "No homes yet");
			return;
		}
		if (_homes->homes.empty()) {
			w::MutedWrapped("No player homes in the active plugins. Enable the home mods in MO2's profile, then Rescan (F5).");
		}

		// Keyboard: Up/Down walk the rows, Enter opens (the search box or the list focused).
		const auto visible = VisibleHomes(a_search);
		bool       moved = false;
		const bool focused = a_compact ? ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) : _railKeys;
		if (focused && !visible.empty() && (a_compact || !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))) {
			const auto pos = std::ranges::find(visible, _railCursor);
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
				_railCursor = pos == visible.end() || pos + 1 == visible.end() ? visible.front() : *(pos + 1);
				moved = true;
			} else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
				_railCursor = pos == visible.end() || pos == visible.begin() ? visible.back() : *(pos - 1);
				moved = true;
			} else if ((ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) && pos != visible.end()) {
				OpenHome(*pos);
				if (a_compact) {
					ImGui::CloseCurrentPopup();
				}
			}
		}

		auto row = [&](int a_i) {
			const auto& h = _homes->homes[static_cast<std::size_t>(a_i)];
			w::Kind     kind = w::Kind::Neutral;
			const auto  chip = HomeChip(a_i, &kind);
			const auto  name = h.extra.empty() ? h.name : std::format("{} +{}", h.name, h.extra.size());
			const auto  summary = Summary(h.containers, h.displays);
			ImGui::PushID(a_i);
			if (wx::HomeRow("home", name.c_str(), a_compact ? "" : summary.c_str(), chip.c_str(), kind, IsOpenHome(a_i), _railCursor == a_i, HomeUnsaved(a_i))) {
				_railCursor = a_i;
				OpenHome(a_i);
				if (a_compact) {
					ImGui::CloseCurrentPopup();
				}
			}
			if (_railCursor == a_i && (moved || (_railReveal && !a_compact))) {
				ImGui::SetScrollHereY(0.5f);
				_railReveal = a_compact && _railReveal;
			}
			HomeTooltip(h);
			if (!a_compact) {
				HomeContextMenu(a_i);
			}
			ImGui::PopID();
		};

		if (!_groupByMod) {
			for (const int i : visible) {
				row(i);
			}
			return;
		}
		std::set<int> listed;
		for (const auto& g : _homeGroups) {
			listed.insert(g.mod);
			const bool       modMatch = !a_search.empty() && ModMatches(*_ws, g.mod, a_search);
			std::vector<int> shown;
			for (const int i : g.homes) {
				if (HomeMatches(_homes->homes[static_cast<std::size_t>(i)], a_search, modMatch)) {
					shown.push_back(i);
				}
			}
			const bool anyCells = !a_compact && _selectedMod == g.mod;
			if (shown.empty() && !anyCells) {
				continue;
			}
			const mo2::Mod* mod = g.mod >= 0 ? &_ws->Mods()[static_cast<std::size_t>(g.mod)] : nullptr;
			bool            open = !_collapsedGroups.contains(g.mod) || !a_search.empty();
			const auto      note = mod && mod->nexusId ? std::format("{}  ·  {}", *mod->nexusId, shown.size()) : std::to_string(shown.size());
			ImGui::PushID(g.mod);
			if (wx::GroupHeader("group", ModName(*_ws, g.mod).c_str(), note.c_str(), open) && a_search.empty()) {
				if (open) {
					_collapsedGroups.erase(g.mod);
				} else {
					_collapsedGroups.insert(g.mod);
				}
			}
			if (ImGui::BeginItemTooltip()) {
				ImGui::TextUnformatted(ModName(*_ws, g.mod).c_str());
				if (g.mod != -2) {
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
					w::Muted(Join(mod ? mod->plugins : _ws->BasePlugins()).c_str());
					ImGui::PopTextWrapPos();
				}
				ImGui::EndTooltip();
			}
			if (open) {
				for (const int i : shown) {
					row(i);
				}
				if (!a_compact && g.mod >= -1) {
					ImGui::PushFont(t.smallFont);
					ImGui::Indent(t.pad);
					if (w::LinkButton(anyCells ? "Hide the other cells" : "All cells...")) {
						if (anyCells) {
							_selectedMod = -2;
							_scans.clear();
						} else {
							SelectMod(g.mod);
						}
					}
					w::Tip("Every cell with containers in this mod's plugins, likely homes first: for a home the finder misses");
					ImGui::Unindent(t.pad);
					ImGui::PopFont();
					if (anyCells) {
						DrawAnyCells(g.mod);
					}
				}
			}
			ImGui::PopID();
		}
		// Mods the search names that have no home found: their cells on request.
		if (!a_compact && !a_search.empty() && a_search.size() >= 2) {
			const auto& mods = _ws->Mods();
			for (int i = static_cast<int>(mods.size()) - 1; i >= 0; --i) {
				const auto& m = mods[static_cast<std::size_t>(i)];
				if (listed.contains(i) || m.separator || m.unmanaged || m.plugins.empty() || (_onlyEnabled && !m.enabled) || !ModMatches(*_ws, i, a_search)) {
					continue;
				}
				ImGui::PushID(i);
				bool       open = true;
				const auto note = m.nexusId ? std::format("{}  ·  no homes", *m.nexusId) : std::string("no homes");
				wx::GroupHeader("group", m.name.c_str(), note.c_str(), open);
				ImGui::PushFont(t.smallFont);
				ImGui::Indent(t.pad);
				const bool anyCells = _selectedMod == i;
				if (w::LinkButton(anyCells ? "Hide the cells" : "All cells...")) {
					if (anyCells) {
						_selectedMod = -2;
						_scans.clear();
					} else {
						SelectMod(i);
					}
				}
				ImGui::Unindent(t.pad);
				ImGui::PopFont();
				if (anyCells) {
					DrawAnyCells(i);
				}
				ImGui::PopID();
			}
		}
	}

	void EditorApp::DrawAnyCells(int a_mod)
	{
		const auto& t = w::Th();
		(void)a_mod;
		ImGui::Indent(t.pad);
		if (Busy() && _scans.empty()) {
			w::Muted("Reading the cells...");
			ImGui::Unindent(t.pad);
			return;
		}
		int shownTotal = 0;
		for (std::size_t s = 0; s < _scans.size(); ++s) {
			auto& scan = _scans[s];
			ImGui::PushID(static_cast<int>(s));
			for (const auto& e : scan.errors) {
				wx::Text(w::Kind::Warn, e.c_str());
			}
			for (std::size_t i = 0; i < scan.homes.size(); ++i) {
				const auto& h = scan.homes[i];
				if (_likelyOnly && !h.Likely()) {
					continue;
				}
				++shownTotal;
				ImGui::PushID(static_cast<int>(i));
				const auto label = std::format("{}{}", h.name.empty() ? h.editorId : h.name, h.interior ? "" : " (exterior)");
				const bool open = _cell && _cell->info.key == h.cell && _cell->lo == scan.lo;
				const auto score = std::format("score {}", h.score);
				if (wx::HomeRow("cell", label.c_str(), Summary(h.containers, h.racks + h.mannequins + h.plaques).c_str(), score.c_str(),
						h.Likely() ? w::Kind::Accent : w::Kind::Neutral, open, false, false, !h.Likely()) &&
					scan.lo) {
					RequestNewHome(scan.lo, h.cell);
				}
				if (ImGui::BeginItemTooltip()) {
					w::Mono((scan.cellRefs[i] + "  " + h.editorId).c_str());
					w::Muted(("Why: " + (h.reasons.empty() ? std::string("only containers") : Join(h.reasons))).c_str());
					w::Muted(scan.plugin.c_str());
					if (h.overridesOther) {
						w::Muted("Edits a cell defined by another plugin");
					}
					ImGui::EndTooltip();
				}
				ImGui::PopID();
			}
			ImGui::PopID();
		}
		if (!Busy() && shownTotal == 0) {
			w::Muted(_likelyOnly ? "No likely homes (filter: Likely homes only)" : "No cells with containers");
		}
		ImGui::Unindent(t.pad);
	}
}
