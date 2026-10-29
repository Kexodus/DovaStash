// The homes rail (docs/EDITOR-UX.md §2.2, EDITOR.md §2 steps 2-3): every player home of the active load order
// under the mod whose plugin defines its cell (MO2's names and priorities, the game's Data folder first), one
// search box (home, mod, plugin, Nexus id), a filter popover, a status chip per home, the unsaved dot, and per
// mod "All cells..." for a home the finder misses (every cell with containers of the mod's plugins, scored).
// The title row's home switcher draws the same list in compact form.

#include <algorithm>
#include <cmath>
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

namespace dova::editor::ui
{
	using namespace detail;

	namespace
	{
		enum class SummaryForm
		{
			Full,    // "24 chests · 9 displays · 2 cells · 2 templates"
			Short,   // a narrow rail: without the displays
			Counts   // the title row's switcher: only the cells and templates
		};

		// The row's muted line, in words: the storage, then the cells and the templates when the home has more than one.
		std::string Summary(int a_containers, int a_displays, std::size_t a_cells = 1, std::size_t a_templates = 0, SummaryForm a_form = SummaryForm::Full)
		{
			std::string out;
			auto        add = [&](std::size_t a_n, const char* a_one, const char* a_many) {
				out += std::format("{}{} {}", out.empty() ? "" : " · ", a_n, a_n == 1 ? a_one : a_many);
			};
			if (a_form != SummaryForm::Counts) {
				add(static_cast<std::size_t>(std::max(a_containers, 0)), "chest", "chests");
				if (a_displays > 0 && a_form == SummaryForm::Full) {
					add(static_cast<std::size_t>(a_displays), "display", "displays");
				}
			}
			if (a_cells > 1) {
				add(a_cells, "cell", "cells");
			}
			if (a_templates > 1) {
				add(a_templates, "template", "templates");
			}
			return out;
		}
	}

	void EditorApp::GroupHomes()
	{
		_homeGroups.clear();
		_homeNameClash.clear();
		if (!_homes || !_ws) {
			return;
		}
		// Two homes of one name (a vanilla home and a mod's copy of it): their rows add the cell's EditorID.
		std::unordered_map<std::string, int> names;
		for (const auto& h : _homes->homes) {
			++names[core::ToLower(h.name)];
		}
		for (const auto& h : _homes->homes) {
			_homeNameClash.push_back(names[core::ToLower(h.name)] > 1 && !h.editorId.empty());
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
		// No usable container and no template: nothing to sort into, hidden unless asked for (or open).
		if (const auto i = static_cast<std::size_t>(&a_home - _homes->homes.data());
			!_showEmptyHomes && i < _homes->storage.size() && _homes->storage[i].containers == 0 && HomeStatus(a_home).empty() && !IsOpenHome(static_cast<int>(i))) {
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

	std::vector<const home::KnownTemplate*> EditorApp::HomeTemplates(const home::HomeEntry& a_home) const
	{
		// Over every cell of the home, each template once (the same set the Template panel lists), in its order.
		std::vector<const home::KnownTemplate*> all;
		auto add = [&](const esp::FormKey& a_cell) {
			for (const auto* t : _homes->templates.ForCell(_homes->lo->ToFormRef(a_cell))) {
				if (std::ranges::find(all, t) == all.end()) {
					all.push_back(t);
				}
			}
		};
		add(a_home.cell);
		for (const auto& e : a_home.extra) {
			add(e.cell);
		}
		std::ranges::stable_sort(all, [](const home::KnownTemplate* a, const home::KnownTemplate* b) {
			return core::CompareTemplateRank(a->layout, a->user, b->layout, b->user) < 0;
		});
		return all;
	}

	std::size_t EditorApp::TemplateCount(const home::HomeEntry& a_home) const
	{
		return HomeTemplates(a_home).size();
	}

	std::string EditorApp::LoadedConflict(const core::Layout& a_layout) const
	{
		return a_layout.conflictPlugins.empty() ? std::string{} : Join(core::LoadedConflicts(a_layout, _activePlugins));
	}

	const char* EditorApp::TemplateWord(const home::KnownTemplate& a_template, w::Kind* a_kind) const
	{
		const bool mine = a_template.user || (!_state.saveTarget.empty() && core::IEquals(a_template.origin, _state.saveTarget));
		const bool off = !LoadedConflict(a_template.layout).empty();
		const auto kind = off || a_template.layout.autoDraft ? w::Kind::Warn : mine ? w::Kind::Ok : w::Kind::Info;
		if (a_kind) {
			*a_kind = kind;
		}
		return off ? "off" : a_template.layout.autoDraft ? "auto-draft" : mine ? "yours" : "shipped";
	}

	std::string EditorApp::HomeChip(int a_index, w::Kind* a_kind) const
	{
		// One word from one small vocabulary, the same in the title row and the Template panel. How many templates
		// and cells a home has is said in words in the row's summary, not on the chip.
		const auto& h = _homes->homes[static_cast<std::size_t>(a_index)];
		const auto  stage = _state.StageOf(h.cellRef);
		bool        user = false;
		bool        autoDraft = false;
		const auto  status = HomeStatus(h, &user, &autoDraft);
		auto        set = [&](w::Kind a_k) {
			if (a_kind) {
				*a_kind = a_k;
			}
		};
		// Every template of the home names a loaded plugin under "conflicts": the game offers none of them.
		if (const auto list = HomeTemplates(h); !list.empty() && std::ranges::all_of(list, [&](const home::KnownTemplate* tp) { return !LoadedConflict(tp->layout).empty(); })) {
			set(w::Kind::Warn);
			return "off";
		}
		if (user || stage == HomeStage::Saved) {
			set(w::Kind::Ok);  // a person's template of yours, or saved this session (it is yours)
			return "yours";
		}
		if (!status.empty()) {
			set(w::Kind::Info);
			return "shipped";
		}
		if (autoDraft) {
			set(w::Kind::Warn);  // saved by Draft all homes, nobody's yet (DESIGN §6.8)
			return "auto-draft";
		}
		if (HomeHasDraft(a_index)) {
			set(w::Kind::Warn);
			return "draft";
		}
		set(w::Kind::Neutral);
		return "none";
	}

	bool EditorApp::HomeHasDraft(int a_index) const
	{
		// The editor's draft of the home exists (made on opening it or by Draft all homes) or was reviewed.
		const auto& h = _homes->homes[static_cast<std::size_t>(a_index)];
		return _homes->entries[static_cast<std::size_t>(a_index)].draft || _state.StageOf(h.cellRef) != HomeStage::None;
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
		return (_onlyWithoutTemplate ? 1 : 0) + (_showEmptyHomes ? 1 : 0) + (_onlyEnabled ? 0 : 1) + (_likelyOnly ? 0 : 1) + (_exteriors ? 1 : 0) + (_groupByMod ? 0 : 1);
	}

	void EditorApp::HomeTooltip(const home::HomeEntry& a_home) const
	{
		if (!ImGui::BeginItemTooltip()) {
			return;
		}
		ImGui::TextUnformatted(a_home.name.c_str());
		w::Mono((a_home.cellRef + "  " + a_home.editorId).c_str());
		if (const auto i = static_cast<std::size_t>(&a_home - _homes->homes.data()); i < _homes->storage.size()) {
			const auto& st = _homes->storage[i];
			w::IconInline(w::Icon::Container, w::Th().muted);
			ImGui::SameLine(0.0f, w::Th().gap);
			ImGui::Text("%d", st.containers);
			ImGui::SameLine(0.0f, 2.0f * w::Th().pad);
			w::IconInline(w::Icon::Display, w::Th().muted);
			ImGui::SameLine(0.0f, w::Th().gap);
			ImGui::Text("%d", st.displays);
			ImGui::SameLine(0.0f, 2.0f * w::Th().pad);
			w::IconInline(w::Icon::Door, w::Th().muted);
			ImGui::SameLine(0.0f, w::Th().gap);
			ImGui::Text("%zu", a_home.doors.size());
			w::Muted("usable chests · displays · load doors");
			// The home's templates in the game's order, each with its word and where it comes from (the chip is the
			// first's); without one, what the chip's word stands for.
			const auto list = HomeTemplates(a_home);
			if (list.empty()) {
				w::Kind    kind = w::Kind::Neutral;
				const auto chip = HomeChip(static_cast<int>(i), &kind);
				w::Coloured(kind, chip == "yours" ? "Saved this session" : chip == "draft" ? "A draft, not saved yet" : "No template yet");
			}
			for (const auto* tp : list) {
				w::Kind     kind = w::Kind::Neutral;
				const char* word = TemplateWord(*tp, &kind);
				w::Coloured(kind, word);
				ImGui::SameLine(0.0f, w::Th().gap);
				w::Muted(std::format("{} · {} · {}", tp->layout.name, tp->layout.author.empty() ? "no author" : tp->layout.author, tp->origin).c_str());
				if (const auto conflict = LoadedConflict(tp->layout); !conflict.empty()) {
					w::Muted(("Not offered: a mod that rebuilds this home is loaded (" + conflict + ")").c_str());
				}
			}
			if (list.size() > 1) {
				w::Muted("In game the first is offered");
			}
		}
		for (const auto& e : a_home.extra) {
			w::Muted(std::format("Also {} ({}){}", e.name.empty() ? e.editorId : e.name, e.cellRef, e.likely || e.exterior ? "" : ": storage behind a door").c_str());
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
		if (w::MenuRow("Open")) {
			OpenHome(a_index);
		}
		if (w::MenuRow("Draft")) {
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
		if (w::MenuRow("Open in Explorer", nullptr, file != nullptr)) {
			const auto args = L"/select,\"" + file->path.wstring() + L"\"";
			::ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
		}
		w::Tip(file ? PathToUtf8(file->path).c_str() : "No template file yet");
		if (w::MenuRow("Copy form ref")) {
			ImGui::SetClipboardText(h.cellRef.c_str());
		}
		ImGui::EndPopup();
	}

	void EditorApp::DrawRail()
	{
		const auto&       t = w::Th();
		const std::string collapsed = _homes ? std::format("{} homes", _homes->homes.size()) : std::string("Homes");
		if (!w::BeginRail("rail", w::Side::Left, _state.ui.railWidth, _state.ui.railOpen, 200.0f * t.scale, RailMaxWidth(), collapsed.c_str())) {
			return;
		}
		if (w::BeginPadded("railpad", ImVec2(0, 0), ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
			if (!_ws) {
				w::EmptyState("No setup", "Pick your MO2 instance or Data folder in the middle panel.");
			} else {
				if (!_homes && !_homesRequested && !_heldHomes) {
					ScanHomes();
				}
				// "Draft all homes" is done: offer to save the drafts of homes without a template.
				const int saveable = SaveableDrafts();
				if (_bulkDone && !_bannerClosed && saveable > 0) {
					const int r = w::Banner("drafted", std::format("{} draft{}", saveable, saveable == 1 ? "" : "s").c_str(), "Save all...");
					if (r == 1) {
						_confirmSaveAll = true;
					} else if (r == 2) {
						_bannerClosed = true;
					}
				}
				// Search and filters (the rail focused: Up/Down/Enter walk the list).
				_railKeys = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
				// The funnel's width with its count badge: the badge widens the button, and the rail clips what does not fit.
				const int   filters = ActiveFilterCount();
				const float filterW = w::PopoverWidth(nullptr, w::Icon::Filter, filters);
				if (_railFocusSearch) {
					ImGui::SetKeyboardFocusHere();
					_railFocusSearch = false;
				}
				w::SearchBox("railsearch", _railSearch, "Home, mod, plugin, Nexus id", ImGui::GetContentRegionAvail().x - filterW - t.gap);
				ImGui::SameLine(0.0f, t.gap);
				if (w::BeginPopover("filter", nullptr, w::Icon::Filter, filters, "Filter")) {
					w::Check("Only homes without a template", _onlyWithoutTemplate);
					w::Check("Show homes with no containers", _showEmptyHomes, "Cells found as homes with no usable chest and no template: nothing to sort into");
					w::Check("Only enabled mods", _onlyEnabled, "Mods without homes found by the search: only the enabled ones");
					w::Check("Likely homes only", _likelyOnly, "All cells...: a player-house location, owned by the player or its mod's faction, or mannequins where nothing respawns");
					if (w::Check("Exteriors", _exteriors, "All cells...: exterior cells with containers too (a balcony, a garden)") && _selectedMod >= -1) {
						Scan();
					}
					w::Check("Group by mod", _groupByMod);
					w::EndPopover();
				}
				// The list, then the footer.
				const float footerH = ImGui::GetTextLineHeightWithSpacing() + t.gap;
				ImGui::BeginChild("homelist", ImVec2(0, std::max(ImGui::GetContentRegionAvail().y - footerH, 40.0f)), ImGuiChildFlags_None, ImGuiWindowFlags_NoNavInputs);
				const float listTop = ImGui::GetCursorPosY();
				DrawHomeList(false, _railSearch);
				// A search that matches nothing (no home, no mod) says so, with the way back, instead of an empty rail.
				if (_homes && !_railSearch.empty() && ImGui::GetCursorPosY() == listTop) {
					ImGui::Dummy(ImVec2(0.0f, t.pad));
					ImGui::Indent(t.pad);
					ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - t.pad);
					ImGui::TextUnformatted(std::format("No home or mod matches \"{}\"", _railSearch).c_str());
					ImGui::PopTextWrapPos();
					w::Muted("Search by home, mod, plugin or Nexus id.");
					if (w::LinkButton("Clear the search")) {
						_railSearch.clear();
					}
					ImGui::Unindent(t.pad);
				}
				const bool scrolled = ImGui::GetScrollY() > 0.0f;
				ImGui::EndChild();
				// A hairline on the list's bottom edge, so a row the list cuts ends on a rule, not on the footer; the item
				// spacing EndChild leaves puts Rescan 4 px under it. Scrolled, the top edge cuts a row too: the same rule
				// there, the pixel above the list, under the search box.
				{
					const ImVec2 lmin = ImGui::GetItemRectMin(), lmax = ImGui::GetItemRectMax();
					const float  y = std::floor(lmax.y);  // AddLine adds the half pixel itself
					ImGui::GetWindowDrawList()->AddLine(ImVec2(lmin.x, y), ImVec2(lmax.x, y), t.line);
					if (scrolled) {
						const float top = std::floor(lmin.y) - 1.0f;
						ImGui::GetWindowDrawList()->AddLine(ImVec2(lmin.x, top), ImVec2(lmax.x, top), t.line);
					}
				}
				ImGui::PushFont(t.smallFont);
				ImGui::BeginDisabled(Busy());
				if (w::LinkButton("Rescan")) {
					Rescan();
				}
				ImGui::EndDisabled();
				// The scan time is a fact about the last scan, not the homes: it goes in Rescan's tip.
				w::Tip(_homes ? std::format("Read the load order again after a change in MO2  F5\nLast scan {:.1f} s", static_cast<double>(_homes->ms) / 1000.0).c_str()
				              : "Read the load order again after a change in MO2  F5");
				if (_homes) {
					ImGui::SameLine(0.0f, t.gap);
					const std::string summary = std::format("{} homes · {} plugins", _homes->homes.size(), _homes->lo ? _homes->lo->PluginCount() : 0);
					// A narrow rail (a large text size): the home count only, the rest in the tooltip.
					const float right = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 30.0f * t.scale;
					const bool  fits = ImGui::GetCursorScreenPos().x + ImGui::CalcTextSize(summary.c_str()).x <= right;
					const auto  shown = fits ? summary : std::format("{} homes", _homes->homes.size());
					ImGui::PushClipRect(ImGui::GetCursorScreenPos(), ImVec2(right, ImGui::GetCursorScreenPos().y + 50.0f), true);
					w::Muted(shown.c_str());
					ImGui::PopClipRect();
					if (!fits) {
						w::Tip(summary.c_str());
					}
				}
				ImGui::PopFont();
			}
		}
		w::EndPadded();
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

		// A narrow list (a large text size, a narrow rail) drops the status word on every row, not on the rows whose
		// names it would cut: the dot stays, a quiet "none" goes (the tooltip says it), and the status reads one way.
		const int narrow = ImGui::GetContentRegionAvail().x < std::round(260.0f * t.scale) ? 1 : 0;
		auto row = [&](int a_i) {
			const auto& h = _homes->homes[static_cast<std::size_t>(a_i)];
			w::Kind     kind = w::Kind::Neutral;
			const auto  chip = HomeChip(a_i, &kind);
			const auto  st = a_i < static_cast<int>(_homes->storage.size()) ? _homes->storage[static_cast<std::size_t>(a_i)] : HomesData::Storage{ h.containers, h.displays };
			const auto  list = HomeTemplates(h);
			const auto  templates = list.size();
			// The cells of the template it opens on (the open one, else the game's first: a Grounds exterior the
			// home finder does not join counts), else the finder's.
			const core::Layout* tl = IsOpenHome(a_i) && _cell ? &_cell->layout : !list.empty() ? &list.front()->layout : nullptr;
			const auto          cells = tl && tl->cell ? core::HomeCellRefs(*tl->cell, tl->cells).size() : h.extra.size() + 1;
			auto        summary = Summary(st.containers, st.displays, cells, templates, a_compact ? SummaryForm::Counts : SummaryForm::Full);
			auto        summaryShort = Summary(st.containers, st.displays, cells, templates, a_compact ? SummaryForm::Counts : SummaryForm::Short);
			auto        counts = Summary(st.containers, st.displays, cells, templates, SummaryForm::Counts);  // narrower still: the cells and templates win
			if (static_cast<std::size_t>(a_i) < _homeNameClash.size() && _homeNameClash[static_cast<std::size_t>(a_i)]) {
				// Another home has this name: its cell's EditorID tells them apart, kept down to the narrowest form.
				summary += " · " + h.editorId;
				summaryShort += " · " + h.editorId;
				counts = h.editorId;
			}
			ImGui::PushID(a_i);
			if (w::HomeRow("home", h.name.c_str(), summary.c_str(), summaryShort.c_str(), chip.c_str(), kind, IsOpenHome(a_i), _railCursor == a_i, HomeUnsaved(a_i), false,
					counts.c_str(), _groupByMod, narrow)) {
				_railCursor = a_i;
				OpenHome(a_i);
				if (a_compact) {
					ImGui::CloseCurrentPopup();
				}
			}
			if (_railCursor == a_i && (moved || (_railReveal && !a_compact))) {
				// A home opened: scroll until its row shows (the scroll limit can take a frame or two to settle).
				const bool seen = ImGui::IsItemVisible() && _railRevealFrames > 0;
				ImGui::SetScrollHereY(0.5f);
				if (!a_compact && _railReveal && (seen || ++_railRevealFrames > 3)) {
					_railReveal = false;
					_railRevealFrames = 0;
				}
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
		// A group's children (its homes, "All cells...") start on its label's keyline: past the 12 px chevron and 6 px gap.
		const float   childX = std::round(18.0f * t.scale);
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
			const auto      homes = std::format("{} home{}", shown.size(), shown.size() == 1 ? "" : "s");
			const auto      note = mod && mod->nexusId ? std::format("Nexus {} · {}", *mod->nexusId, homes) : homes;
			ImGui::PushID(g.mod);
			if (w::GroupHeader("group", ModName(*_ws, g.mod).c_str(), note.c_str(), open, homes.c_str()) && a_search.empty()) {
				if (open) {
					_collapsedGroups.erase(g.mod);
				} else {
					_collapsedGroups.insert(g.mod);
				}
			}
			// "All cells..." (a home the finder misses): in the group's right-click menu, and as a link under the group of
			// the open home or whose cells are listed, not under every group.
			const bool cellsMenu = !a_compact && g.mod >= -1;
			const auto toggleCells = [&] {
				if (anyCells) {
					_selectedMod = -2;
					_scans.clear();
				} else {
					SelectMod(g.mod);
				}
			};
			if (ImGui::BeginItemTooltip()) {
				ImGui::TextUnformatted(ModName(*_ws, g.mod).c_str());
				if (mod && mod->nexusId) {
					w::Muted(std::format("Nexus {}", *mod->nexusId).c_str());
				}
				if (g.mod != -2) {
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
					w::Muted(Join(mod ? mod->plugins : _ws->BasePlugins()).c_str());
					ImGui::PopTextWrapPos();
				}
				if (cellsMenu) {
					w::Muted("Right-click: all cells");
				}
				ImGui::EndTooltip();
			}
			if (cellsMenu && ImGui::BeginPopupContextItem("groupctx")) {
				if (w::MenuRow(anyCells ? "Hide the other cells" : "All cells...")) {
					toggleCells();
				}
				ImGui::EndPopup();
			}
			if (open) {
				for (const int i : shown) {
					row(i);
				}
				if (cellsMenu && (anyCells || std::ranges::any_of(g.homes, [&](int i) { return IsOpenHome(i); }))) {
					ImGui::PushFont(t.smallFont);
					ImGui::Indent(childX);
					// A row of the rail's height with the link centred in it, so the rows keep one pitch.
					// The link first, then the row's Dummy from the row's top: the last item submitted is the row, so the
					// list ends on an item (a SetCursorPos left last is an ImGui error when this row ends a search).
					const ImVec2 rowAt = ImGui::GetCursorPos();
					ImGui::SetCursorPos(ImVec2(rowAt.x, rowAt.y + std::floor((t.rowH - ImGui::GetTextLineHeight()) * 0.5f)));
					if (w::LinkButton(anyCells ? "Hide the other cells" : "All cells...")) {
						toggleCells();
					}
					w::Tip("Every cell with containers in this mod's plugins, likely homes first: for a home the finder misses");
					ImGui::SetCursorPos(rowAt);
					ImGui::Dummy(ImVec2(1.0f, t.rowH));
					ImGui::Unindent(childX);
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
				const auto note = m.nexusId ? std::format("Nexus {} · no homes", *m.nexusId) : std::string("no homes");
				w::GroupHeader("group", m.name.c_str(), note.c_str(), open, "no homes");
				ImGui::PushFont(t.smallFont);
				ImGui::Indent(childX);
				const bool anyCells = _selectedMod == i;
				if (w::LinkButton(anyCells ? "Hide the cells" : "All cells...")) {
					if (anyCells) {
						_selectedMod = -2;
						_scans.clear();
					} else {
						SelectMod(i);
					}
				}
				ImGui::Unindent(childX);
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
		// Under "All cells...": its messages on the link's keyline, the cells' rows from it (their names one padding in).
		const float indent = std::round(18.0f * t.scale);
		ImGui::Indent(indent);
		if (Busy() && _scans.empty()) {
			w::Muted("Reading the cells...");
			ImGui::Unindent(indent);
			return;
		}
		int shownTotal = 0;
		for (std::size_t s = 0; s < _scans.size(); ++s) {
			auto& scan = _scans[s];
			ImGui::PushID(static_cast<int>(s));
			for (const auto& e : scan.errors) {
				w::Coloured(w::Kind::Warn, e.c_str());
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
				const int  usable = h.containers - h.respawning;
				if (w::HomeRow("cell", label.c_str(), Summary(usable, h.racks + h.mannequins + h.plaques).c_str(), Summary(usable, 0, 1, 0, SummaryForm::Short).c_str(), score.c_str(),
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
		ImGui::Unindent(indent);
	}
}
