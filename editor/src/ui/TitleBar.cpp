// The title row and the status row (docs/EDITOR-UX.md §2.1, §2.7, §2.10): the ≡ menu, the emblem (start panel),
// the home switcher (the rail's list in compact form), 3D | Plan, Draft ▾, Save, the Setup popover; and below
// everything the open home, the checks' count, the background work and the one useful shortcut.

#include <algorithm>
#include <format>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "Util.h"
#include "home/TemplateFile.h"
#include "ui/App.h"
#include "ui/Common.h"
#include "ui/WidgetsExtra.h"

namespace dova::editor::ui
{
	using namespace detail;

	namespace
	{
		// A vertical divider between groups of the title row.
		void Divider()
		{
			const auto&  t = w::Th();
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float  h = ImGui::GetFrameHeight();
			ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + 3.0f * t.scale), ImVec2(p.x, p.y + h - 3.0f * t.scale), t.line);
			ImGui::Dummy(ImVec2(1.0f, h));
		}
	}

	void EditorApp::DrawTitleRow()
	{
		const auto& t = w::Th();
		const float W = ImGui::GetContentRegionAvail().x;
		ImGui::PushStyleColor(ImGuiCol_ChildBg, t.bg1);
		ImGui::BeginChild("titlerow", ImVec2(W, t.titleH), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleColor();
		const ImVec2 o = ImGui::GetWindowPos();
		ImGui::GetWindowDrawList()->AddLine(ImVec2(o.x, o.y + t.titleH - 1.0f), ImVec2(o.x + W, o.y + t.titleH - 1.0f), t.line);
		const float y = (t.titleH - ImGui::GetFrameHeight()) * 0.5f;
		ImGui::SetCursorPos(ImVec2(t.pad * 0.5f, y));
		const float leftStart = ImGui::GetCursorScreenPos().x;

		// ≡
		if (w::BeginPopover("menu", nullptr, w::Icon::Menu, 0, "Menu")) {
			DrawMainMenu();
			w::EndPopover();
		}
		// Emblem and name: the start panel.
		ImGui::SameLine(0.0f, t.gap);
		{
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float  es = 18.0f * t.scale;
			ImGui::PushFont(t.title);
			const ImVec2 ts = ImGui::CalcTextSize("Dova Stash Editor");
			ImGui::PopFont();
			const float h = ImGui::GetFrameHeight();
			if (ImGui::InvisibleButton("emblem", ImVec2(es + t.gap + ts.x, h))) {
				_showStart = _cell ? !_showStart : true;
			}
			const bool hov = ImGui::IsItemHovered();
			w::Tip("Start");
			ImDrawList* dl = ImGui::GetWindowDrawList();
			w::DrawIcon(dl, w::Icon::Emblem, ImVec2(p.x, p.y + (h - es) * 0.5f), es, t.accent);
			ImGui::PushFont(t.title);
			dl->AddText(ImVec2(p.x + es + t.gap, p.y + (h - ts.y) * 0.5f), hov ? t.select : t.text, "Dova Stash Editor");
			ImGui::PopFont();
		}
		ImGui::SameLine(0.0f, t.pad);
		Divider();
		ImGui::SameLine(0.0f, t.pad);

		// Back (the cell opened before), the home switcher, the unsaved dot and the template's status.
		if (!_history.empty()) {
			ImGui::BeginDisabled(Busy());
			if (w::IconButton("back", w::Icon::ChevronLeft, std::format("Back ({})", _history.size()).c_str())) {
				auto [lo, key] = _history.back();
				_history.pop_back();
				OpenCellAsync(lo, key, false, true);
			}
			ImGui::EndDisabled();
			ImGui::SameLine(0.0f, 2.0f * t.scale);
		}
		std::string homeName = "Open a home";
		if (_cell) {
			homeName = _cellDraft >= 0 && _homes ? _homes->homes[static_cast<std::size_t>(_cellDraft)].name
			           : !_cell->layout.name.empty() ? _cell->layout.name
			           : !_cell->info.name.empty()   ? _cell->info.name
			                                         : _cell->info.editorId;
			const bool other = !CellInHome(*_cell);
			if (other || (_cell->layout.cell && _cell->lo->ToFormRef(_cell->info.key) != *_cell->layout.cell)) {
				homeName += " · " + (_cell->info.name.empty() ? _cell->info.editorId : _cell->info.name);  // another cell of the home
			}
		}
		if (w::BeginPopover("switcher", (homeName + "###switch").c_str(), w::Icon::Home, 0, "Homes  (type to filter)")) {
			if (ImGui::IsWindowAppearing()) {
				_switcherSearch.clear();
			}
			ImGui::BeginChild("switchlist", ImVec2(380.0f * t.scale, 440.0f * t.scale), ImGuiChildFlags_None, ImGuiWindowFlags_None);
			DrawHomeList(true, _switcherSearch);
			ImGui::EndChild();
			w::EndPopover();
		}
		if (_cell) {
			if (_cell->dirty || AnyUnsaved()) {
				ImGui::SameLine(0.0f, t.gap);
				ImGui::SetCursorPosY(y + (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f);
				w::StatusDot(w::Kind::Warn, _cell->dirty ? "The template has unsaved changes" : "Reviewed drafts are not saved");
				ImGui::SameLine(0.0f, 3.0f * t.scale);
				ImGui::SetCursorPosY(y + (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f);
				w::Muted("unsaved");
			}
			// The template's status: none, shipped, yours, draft.
			std::string chip = "none";
			w::Kind     kind = w::Kind::Neutral;
			bool        user = false;
			const auto  existing = _cellDraft >= 0 && _homes ? HomeStatus(_homes->homes[static_cast<std::size_t>(_cellDraft)], &user) : std::string{};
			if (!_cell->filePath.empty()) {
				const bool mine = IContains(_cell->filePath, home::kTemplatesMod) || IContains(_cell->filePath, "Templates\\User") || IContains(_cell->filePath, "Templates/User");
				chip = mine ? "yours" : "shipped";
				kind = mine ? w::Kind::Ok : w::Kind::Info;
			} else if (!_cell->savedPath.empty() && !_cell->dirty) {
				chip = "yours";
				kind = w::Kind::Ok;
			} else if (_cell->dirty) {
				chip = "draft";
				kind = w::Kind::Warn;
			} else if (!existing.empty()) {
				chip = user ? "yours" : "shipped";
				kind = user ? w::Kind::Ok : w::Kind::Info;
			}
			ImGui::SameLine(0.0f, t.gap);
			ImGui::SetCursorPosY(y + (ImGui::GetFrameHeight() - 18.0f * t.scale) * 0.5f);
			w::Chip(chip.c_str(), kind);
			w::Tip(chip == "draft"     ? "A draft: not saved yet"
			       : chip == "none"    ? "No template yet: the draft is untouched"
			       : chip == "shipped" ? (existing.empty() ? "Opened from a shipped template" : ("This home has a template: " + existing).c_str())
			                           : "Your template");
		}
		ImGui::SameLine();
		_titleLeftW = ImGui::GetCursorScreenPos().x - leftStart;

		// Right: Draft ▾, Save, ⚙ (placed with last frame's width).
		const float rightX = W - _titleRightW - t.pad * 0.5f;
		// Centre: 3D | Plan, unless it would touch either side.
		{
			static constexpr const char* views[] = { "3D", "Plan" };
			static constexpr const char* tips[] = { "3D view  1", "Floor plan  2" };
			const float segW = ImGui::CalcTextSize("3D").x + ImGui::CalcTextSize("Plan").x + 4.0f * ImGui::GetStyle().FramePadding.x + 2.0f;
			float       x = (W - segW) * 0.5f;
			x = std::max(x, _titleLeftW + t.pad * 1.5f);
			if (x + segW < rightX - t.pad) {
				ImGui::SetCursorPos(ImVec2(x, y));
				ImGui::BeginDisabled(!_cell);
				w::Segmented("view", _state.ui.view, views, tips);
				ImGui::EndDisabled();
			}
		}
		ImGui::SetCursorPos(ImVec2(std::max(rightX, _titleLeftW + t.pad), y));
		const float rightStart = ImGui::GetCursorScreenPos().x;
		if (w::BeginPopover("draftmenu", "Draft", w::Icon::Draft, 0, "Suggestions for the containers", "Ctrl+D: all homes")) {
			DrawDraftMenu();
			w::EndPopover();
		}
		ImGui::SameLine(0.0f, t.gap);
		const int errors = _cell ? ErrorCount(*_cell) : 0;
		if (w::PrimaryButton("Save", _cell != nullptr && errors == 0, !_cell ? "Open a home first" : "The checks find errors: see Checks")) {
			RequestSave();
		}
		if (_cell && errors == 0) {
			w::Tip(std::format("Save the template  Ctrl+S\n{}", PathToUtf8(TemplatePath(*_cell))).c_str());
		}
		ImGui::SameLine(0.0f, t.gap);
		const bool setupOpen = w::BeginPopover("setup", nullptr, w::Icon::Settings, 0, "Setup");
		if (setupOpen) {
			if (!_setupWasOpen) {
				_setupPathAtOpen = _mo2Path;
			}
			DrawSetupPopover();
			w::EndPopover();
		} else if (_setupWasOpen && _setupPathAtOpen != _mo2Path) {
			// Closing applies (EDITOR-UX.md §2.7): another instance folder.
			ApplySetup(_mo2Path[0] ? std::optional(PathFromUtf8(_mo2Path)) : std::nullopt, std::nullopt);
		}
		_setupWasOpen = setupOpen;
		ImGui::SameLine(0.0f, 0.0f);
		_titleRightW = ImGui::GetCursorScreenPos().x - rightStart;
		ImGui::EndChild();
	}

	void EditorApp::DrawMainMenu()
	{
		const bool busy = Busy();
		if (wx::MenuItem("Open template file...", "Ctrl+O", _ws && !busy)) {
			OpenTemplateFile();
		}
		if (wx::MenuItem("Rescan load order", "F5", _ws != nullptr)) {
			Rescan();
		}
		ImGui::Separator();
		if (wx::MenuItem("Draft all homes", "Ctrl+D", _ws != nullptr)) {
			StartBulkDraft();
		}
		const int saveable = SaveableDrafts();
		if (wx::MenuItem(std::format("Save {} drafts...", saveable).c_str(), nullptr, saveable > 0)) {
			_confirmSaveAll = true;
		}
		ImGui::Separator();
		if (wx::MenuItem("Undo", "Ctrl+Z", !_undo.empty())) {
			Undo();
		}
		if (wx::MenuItem("Redo", "Ctrl+Y", !_redo.empty())) {
			Redo();
		}
		ImGui::Separator();
		if (wx::MenuItem("Commands...", "Ctrl+K")) {
			_paletteOpen = true;
			_paletteFocus = true;
			_palette.clear();
			_paletteSel = 0;
		}
		if (wx::MenuItem("Shortcuts...")) {
			_showShortcuts = true;
		}
		if (wx::MenuItem("About")) {
			_showAbout = true;
		}
	}

	void EditorApp::DrawDraftMenu()
	{
		const bool canEdit = _cell && !Busy();
		if (wx::MenuItem("Draft this home", nullptr, _cell != nullptr)) {
			DraftThisHome();
		}
		if (wx::MenuItem("Accept all", nullptr, canEdit)) {
			AcceptAll();
		}
		w::Tip("Every suggestion no other container has a stronger claim on");
		if (wx::MenuItem("Reject the weak ones", nullptr, canEdit)) {
			RejectWeak();
		}
		w::Tip("Every suggestion under 50%");
		if (wx::MenuItem("Draft again", nullptr, canEdit)) {
			DraftAgain();
		}
		w::Tip("Make the draft again from the plugins (after editing DraftRules.json)");
		ImGui::Separator();
		if (wx::MenuItem("Draft all homes", "Ctrl+D", _ws != nullptr)) {
			StartBulkDraft();
		}
		const int saveable = SaveableDrafts();
		if (wx::MenuItem(std::format("Save {} drafts...", saveable).c_str(), nullptr, saveable > 0)) {
			_confirmSaveAll = true;
		}
	}

	void EditorApp::DrawSetupPopover()
	{
		const auto& t = w::Th();
		ImGui::PushItemWidth(360.0f * t.scale);
		wx::FieldLabel("MO2 instance");
		ImGui::InputTextWithHint("##mo2", "Folder with ModOrganizer.ini", _mo2Path, sizeof(_mo2Path));
		ImGui::SameLine();
		if (ImGui::Button("Browse...")) {
			if (auto p = PickFolder()) {
				std::snprintf(_mo2Path, sizeof(_mo2Path), "%s", p->c_str());
			}
		}
		if (_ws && _ws->Instance() && !_ws->Instance()->profiles.empty()) {
			wx::FieldLabel("Profile");
			if (ImGui::BeginCombo("##profile", _ws->Profile().c_str())) {
				for (const auto& p : _ws->Instance()->profiles) {
					if (ImGui::Selectable(p.c_str(), p == _ws->Profile()) && p != _ws->Profile()) {
						ApplySetup(_ws->Instance()->root, p);
						break;
					}
				}
				ImGui::EndCombo();
			}
		}
		wx::FieldLabel("Author");
		if (ImGui::InputTextWithHint("##author", "Your name (the template folder)", &_templateAuthor)) {
			_state.author = _templateAuthor;
			_state.Save();
			if (_cell) {
				_cell->layout.author = _templateAuthor;
				_cell->dirty = true;
				Revalidate(*_cell);
			}
		}
		wx::FieldLabel("Save templates to");
		{
			const auto targets = SaveTargets();
			auto       name = [](const std::string& a_t) { return a_t.empty() ? std::string(home::kTemplatesMod) + " (its own mod)" : a_t; };
			if (ImGui::BeginCombo("##target", name(_state.saveTarget).c_str())) {
				for (const auto& tg : targets) {
					if (ImGui::Selectable(name(tg).c_str(), tg == _state.saveTarget)) {
						_state.saveTarget = tg;
						_state.Save();
					}
				}
				ImGui::EndCombo();
			}
			w::Tip(PathToUtf8(SaveBase()).c_str());
		}
		ImGui::PopItemWidth();
		w::Spacer();
		if (ImGui::Button("Rescan  F5")) {
			Rescan();
		}
		w::Spacer();
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 440.0f * t.scale);
		if (_ws) {
			w::MutedWrapped(_ws->Describe().c_str());
		} else if (!_wsError.empty()) {
			wx::TextWrapped(w::Kind::Error, _wsError.c_str());
		}
		ImGui::PopTextWrapPos();
	}

	void EditorApp::ApplySetup(std::optional<std::filesystem::path> a_mo2, std::optional<std::string> a_profile)
	{
		if (AnyUnsaved()) {
			_pendingSetup = std::pair{ std::move(a_mo2), std::move(a_profile) };
			return;
		}
		if (Busy()) {
			_job.wait();  // a scan of the old setup: its result is dropped with the workspace
			PollJob();
		}
		_options.mo2 = std::move(a_mo2);
		_options.profile = std::move(a_profile);
		_queue.clear();
		OpenWorkspace();
		if (_ws) {
			ScanHomes();
		}
	}

	void EditorApp::DrawSetupConfirm()
	{
		if (_pendingSetup && !ImGui::IsPopupOpen("Change the setup?")) {
			ImGui::OpenPopup("Change the setup?");
		}
		if (ImGui::BeginPopupModal("Change the setup?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted("The template or reviewed drafts have unsaved changes.");
			w::Muted("Open the other setup and drop them?");
			w::Spacer();
			if (ImGui::Button("Drop the changes")) {
				auto setup = *std::exchange(_pendingSetup, std::nullopt);
				if (_cell) {
					_cell->dirty = false;
				}
				if (_homes) {
					for (auto& e : _homes->entries) {
						e.reviewed = false;
					}
				}
				ImGui::CloseCurrentPopup();
				ApplySetup(std::move(setup.first), std::move(setup.second));
			}
			ImGui::SameLine();
			if (ImGui::Button("Keep editing") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				_pendingSetup.reset();
				std::snprintf(_mo2Path, sizeof(_mo2Path), "%s", _setupPathAtOpen.c_str());
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void EditorApp::DrawHelpPopups()
	{
		if (_showShortcuts) {
			ImGui::OpenPopup("Shortcuts");
			_showShortcuts = false;
		}
		if (ImGui::BeginPopupModal("Shortcuts", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			static constexpr const char* keys[][2] = { { "Ctrl+K", "commands" }, { "Ctrl+1 / Ctrl+2 / Ctrl+3", "rail / drawer / inspector" }, { "1 / 2", "3D / plan" },
				{ "S / P", "select / paint" }, { "C / V", "colours by category / by fill" }, { "F / Home", "frame selection / overview" }, { "L", "layers" },
				{ "Ctrl+S / Ctrl+O / Ctrl+D", "save / open template / draft all" }, { "Ctrl+Z / Ctrl+Y", "undo / redo" }, { "Ctrl+click, drag", "add to selection, box select" },
				{ "Esc", "close, then clear the selection" }, { "F5", "rescan" }, { "Delete", "remove the selection from the template" },
				{ "Right mouse + WASD / QE", "fly (3D)" }, { "Alt + drag", "orbit (3D)" } };
			if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_SizingFixedFit)) {
				for (const auto& k : keys) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					w::Mono(k[0]);
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(k[1]);
				}
				ImGui::EndTable();
			}
			w::Spacer();
			if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		if (_showAbout) {
			ImGui::OpenPopup("About");
			_showAbout = false;
		}
		if (ImGui::BeginPopupModal("About", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			w::Title("Dova Stash Editor");
			w::Muted("Home templates for Dova Stash, made outside the game.");
			if (!_categorySource.empty()) {
				w::Small(std::format("{} categories from {}", _categories.size(), _categorySource).c_str());
			}
			if (!_draftRulesSource.empty()) {
				w::Small(("Draft rules: " + _draftRulesSource).c_str());
			}
			w::Spacer();
			if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void EditorApp::DrawStatusRow()
	{
		const auto& t = w::Th();
		const float W = ImGui::GetContentRegionAvail().x;
		ImGui::PushStyleColor(ImGuiCol_ChildBg, t.bg1);
		ImGui::BeginChild("statusrow", ImVec2(W, t.statusH), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleColor();
		const ImVec2 o = ImGui::GetWindowPos();
		ImGui::GetWindowDrawList()->AddLine(o, ImVec2(o.x + W, o.y), t.line);
		ImGui::PushFont(t.smallFont);
		const float y = (t.statusH - ImGui::GetTextLineHeight()) * 0.5f;
		ImGui::SetCursorPos(ImVec2(t.pad, y));

		// Left: home · cell, the unsaved dot.
		if (_cell) {
			const std::string home = _cellDraft >= 0 && _homes ? _homes->homes[static_cast<std::size_t>(_cellDraft)].name : _cell->layout.name;
			ImGui::TextUnformatted(home.c_str());
			ImGui::SameLine(0.0f, t.gap);
			w::Muted(("· " + _cell->info.editorId).c_str());
			w::Tip(std::format("{}  {}\n{}, last edited by {}, {} ms", _cell->cellText, _cell->info.editorId, _cell->info.interior ? "interior" : "exterior",
				_cell->info.lastOverride, _cell->ms)
					.c_str());
			if (_cell->dirty) {
				ImGui::SameLine(0.0f, t.gap);
				w::StatusDot(w::Kind::Warn, "Unsaved changes");
			}
		} else if (_ws) {
			w::Muted(_homes ? std::format("{} homes", _homes->homes.size()).c_str() : "No home open");
		} else {
			w::Muted("No MO2 instance");
		}

		// Middle: the checks (click: the Checks panel), or the last result for a few seconds.
		const float mid = W * 0.36f;
		ImGui::SameLine();
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + t.pad, mid));
		if (!_status.empty() && ImGui::GetTime() - _statusTime < 8.0) {
			wx::Text(_statusBad ? w::Kind::Error : w::Kind::Ok, _status.c_str());
			w::Tip(_status.c_str());
		} else if (_cell) {
			const int  errors = ErrorCount(*_cell);
			const int  warnings = WarningCount(*_cell);
			const auto text = std::format("{} {} · {} {}", errors, errors == 1 ? "error" : "errors", warnings, warnings == 1 ? "warning" : "warnings");
			ImGui::PushStyleColor(ImGuiCol_Text, errors > 0 ? t.error : warnings > 0 ? t.warn : t.muted);
			ImGui::TextUnformatted(text.c_str());
			ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) {
				ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			}
			if (ImGui::IsItemClicked()) {
				_panel = Panel::Checks;
				_state.ui.inspectorOpen = true;
			}
			w::Tip("Checks");
		}

		// Right: background work, then the one most useful shortcut.
		std::string hint = !_cell ? "Ctrl+K commands" : _cell->dirty ? "Ctrl+S save" : _paint ? "P paint off" : "Ctrl+K commands";
		const float hintW = ImGui::CalcTextSize(hint.c_str()).x;
		float       workW = 0.0f;
		std::string work;
		std::size_t done = 0, total = 0;
		if (Busy()) {
			work = _jobLabel;
			if (_progress && (_jobLabel == "Finding homes" || _jobLabel == "Drafting all homes")) {
				done = _progress->done.load();
				total = _progress->total.load();
				std::scoped_lock lock(_progress->mutex);
				if (!_progress->stage.empty() && _jobLabel == "Finding homes") {
					work = _progress->stage;
				}
			}
		} else if (_scene && _cell && !_showStart) {
			const auto p = _scene->Progress();
			if (!p.finished) {
				if (p.archivesTotal == 0 || p.archivesDone < p.archivesTotal) {
					work = "opening archives";
					done = p.archivesDone;
					total = p.archivesTotal;
				} else if (p.modelsDone < p.modelsTotal) {
					work = "loading meshes";
					done = p.modelsDone;
					total = p.modelsTotal;
				} else {
					work = "loading textures";
					done = p.texturesDone;
					total = p.texturesTotal;
				}
			}
		}
		if (!work.empty()) {
			workW = std::max(ImGui::CalcTextSize(std::format("{} {}/{}", work, done, total).c_str()).x, 160.0f * t.scale);
		}
		float x = W - t.pad - hintW;
		// Missing meshes or textures of the open cell (the list in the tooltip).
		if (_scene && _cell && work.empty()) {
			const auto p = _scene->Progress();
			if (p.finished && p.missingModels + p.missingTextures > 0) {
				const auto miss = std::format("{} missing", p.missingModels + p.missingTextures);
				const float mw = ImGui::CalcTextSize(miss.c_str()).x;
				x -= mw + 2.0f * t.pad;
				ImGui::SameLine();
				ImGui::SetCursorPosX(x);
				wx::Text(w::Kind::Warn, miss.c_str());
				if (ImGui::BeginItemTooltip()) {
					ImGui::Text("%zu meshes, %zu textures missing: boxes and a gray checker", p.missingModels, p.missingTextures);
					const auto problems = _scene->Problems();
					for (std::size_t i = 0; i < problems.size() && i < 30; ++i) {
						ImGui::BulletText("%s", problems[i].c_str());
					}
					if (problems.size() > 30) {
						w::Muted(std::format("... {} more", problems.size() - 30).c_str());
					}
					ImGui::EndTooltip();
				}
			}
		}
		if (!work.empty()) {
			x -= workW + 2.0f * t.pad;
			ImGui::SameLine();
			ImGui::SetCursorPos(ImVec2(x, y - 1.0f * t.scale));
			wx::StatusProgress(work.c_str(), done, total, workW);
		}
		ImGui::SameLine();
		ImGui::SetCursorPos(ImVec2(W - t.pad - hintW, y));
		wx::KeyHint(hint.c_str());
		ImGui::PopFont();
		ImGui::EndChild();
	}
}
