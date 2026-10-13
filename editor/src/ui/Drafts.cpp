// The welcome page and the "Draft all homes" view (docs/EDITOR.md §3.4, §3.6): every player home of the active
// load order with its template status and a draft made by core::SuggestDraft; the author accepts or rejects each
// suggestion (the reason is in the tooltip), opens the home in the editor for anything else, and saves, through the
// same confirmation and backup as the Template tab. The welcome page shows the setup, how many homes there are, the
// big actions and the to-do list that persists between runs (ui/State).

#include <algorithm>
#include <format>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ShObjIdl.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "Util.h"
#include "dova/core/Template.h"
#include "home/TemplateFile.h"
#include "ui/App.h"

namespace fs = std::filesystem;

namespace dova::editor::ui
{
	namespace
	{
		const ImVec4 kGood(0.45f, 0.85f, 0.5f, 1.0f);
		const ImVec4 kWarn(1.0f, 0.78f, 0.3f, 1.0f);
		const ImVec4 kBad(1.0f, 0.5f, 0.4f, 1.0f);
		const ImVec4 kInfo(0.55f, 0.75f, 1.0f, 1.0f);

		ImVec4 ConfidenceColour(float a_conf)
		{
			return a_conf >= 0.75f ? kGood : a_conf >= 0.5f ? kWarn : ImVec4(0.85f, 0.6f, 0.45f, 1.0f);
		}

		ImVec4 StageColour(HomeStage a_stage)
		{
			switch (a_stage) {
			case HomeStage::Saved:
				return kGood;
			case HomeStage::Validated:
				return ImVec4(0.6f, 0.9f, 0.7f, 1.0f);
			case HomeStage::Reviewed:
				return kInfo;
			case HomeStage::Drafted:
				return kWarn;
			default:
				return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
			}
		}

		void Tooltip(const std::string& a_text)
		{
			if (ImGui::BeginItemTooltip()) {
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
				ImGui::TextUnformatted(a_text.c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
		}

		std::string Label(const core::DraftObject& a_o) { return a_o.name.empty() ? (a_o.editorId.empty() ? a_o.id : a_o.editorId) : a_o.name; }

		std::optional<std::string> PickJsonFile()
		{
			std::optional<std::string> out;
			IFileOpenDialog*           dlg = nullptr;
			if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) {
				return out;
			}
			const COMDLG_FILTERSPEC filter[] = { { L"Dova Stash template or layout (*.json)", L"*.json" } };
			dlg->SetFileTypes(1, filter);
			dlg->SetTitle(L"Open a Dova Stash template or layout");
			if (SUCCEEDED(dlg->Show(nullptr))) {
				IShellItem* item = nullptr;
				if (SUCCEEDED(dlg->GetResult(&item))) {
					PWSTR path = nullptr;
					if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
						out = Narrow(path);
						::CoTaskMemFree(path);
					}
					item->Release();
				}
			}
			dlg->Release();
			return out;
		}

		// A big button with a line under it saying what it does.
		bool BigAction(const char* a_label, const char* a_caption, float a_width, bool a_enabled = true)
		{
			ImGui::BeginGroup();
			ImGui::BeginDisabled(!a_enabled);
			const bool pressed = ImGui::Button(a_label, ImVec2(a_width, ImGui::GetFontSize() * 3.2f));
			ImGui::EndDisabled();
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + a_width);
			ImGui::TextDisabled("%s", a_caption);
			ImGui::PopTextWrapPos();
			ImGui::EndGroup();
			return pressed;
		}
	}

	std::string EditorApp::HomeStatus(const home::HomeEntry& a_home, bool* a_user) const
	{
		if (!_homes) {
			return {};
		}
		auto list = _homes->templates.ForCell(_homes->lo->ToFormRef(a_home.cell));
		for (const auto& e : a_home.extra) {
			for (const auto* t : _homes->templates.ForCell(_homes->lo->ToFormRef(e.cell))) {
				if (std::ranges::find(list, t) == list.end()) {
					list.push_back(t);
				}
			}
		}
		const bool user = std::ranges::any_of(list, [](const home::KnownTemplate* t) { return t->user; });
		const bool shipped = std::ranges::any_of(list, [](const home::KnownTemplate* t) { return !t->user; });
		if (a_user) {
			*a_user = user;
		}
		return user && shipped ? "yours + shipped" : user ? "yours" : shipped ? "shipped" : "";
	}

	bool EditorApp::AnyUnsaved() const
	{
		if (_cell && _cell->dirty) {
			return true;
		}
		if (_homes) {
			for (std::size_t i = 0; i < _homes->entries.size(); ++i) {
				if (_homes->entries[i].reviewed && _state.StageOf(_homes->homes[i].cellRef) != HomeStage::Saved) {
					return true;
				}
			}
		}
		return false;
	}

	void EditorApp::ScanHomes(bool a_drafts)
	{
		if (!_ws || _job.valid()) {
			return;
		}
		LoadCategories();
		_progress = std::make_shared<Progress>();
		const Workspace*         ws = _ws.get();
		const core::CategorySet* cats = _categorySet ? &*_categorySet : nullptr;
		auto                     progress = _progress;
		StartJob("Finding every player home...", [this, ws, cats, progress, a_drafts]() -> std::function<void()> {
			const auto start = std::chrono::steady_clock::now();
			auto       data = std::make_shared<HomesData>();
			auto       setStage = [&](std::string a_stage) {
                std::scoped_lock lock(progress->mutex);
                progress->stage = std::move(a_stage);
			};
			setStage("Opening the load order");
			data->lo = std::shared_ptr<esp::LoadOrder>(ws->OpenLoadOrder(data->errors, [&](std::size_t a_done, std::size_t a_total) {
				progress->done = a_done;
				progress->total = a_total;
			}));
			data->homes = home::FindAllHomes(*data->lo, [&](std::size_t a_done, std::size_t a_total, const std::string& a_stage) {
				progress->done = a_done;
				progress->total = a_total;
				setStage(a_stage);
			});
			setStage("Reading templates and items");
			data->templates = home::LoadTemplateIndex(*ws);
			data->catalog = std::make_shared<esp::ItemCatalog>(esp::BuildItemCatalog(*data->lo));
			if (cats) {
				data->itemNames = home::BuildItemNameIndex(*data->catalog, *cats);
			}
			data->entries.resize(data->homes.size());
			data->ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
			return [this, data, a_drafts]() {
				_homes = data;
				_draftSel = _homes->homes.empty() ? -1 : 0;
				if (_pendingHomeSelect) {
					for (std::size_t i = 0; i < _homes->homes.size(); ++i) {
						if (IContains(_homes->homes[i].name, *_pendingHomeSelect) || IContains(_homes->homes[i].editorId, *_pendingHomeSelect)) {
							_draftSel = static_cast<int>(i);
							break;
						}
					}
					_pendingHomeSelect.reset();
				}
				if (a_drafts) {
					_mode = Mode::Drafts;
				}
			};
		});
	}

	EditorApp::DraftEntry& EditorApp::EnsureDraft(int a_index)
	{
		auto& e = _homes->entries[static_cast<std::size_t>(a_index)];
		if (!e.draft) {
			const auto&              h = _homes->homes[static_cast<std::size_t>(a_index)];
			const home::DraftContext ctx{ _categorySet ? &*_categorySet : nullptr, &_draftRules, &_homes->itemNames };
			e.draft = std::make_unique<home::HomeDraft>(home::DraftHome(*_homes->lo, h, ctx));
			if (!_templateAuthor.empty()) {
				e.draft->layout.author = _templateAuthor;
			}
			e.problems = e.draft->diagnostics;
			_state.Advance(h.cellRef, h.name, HomeStage::Drafted);
		}
		return e;
	}

	void EditorApp::DrawQuitPopup()
	{
		if (_confirmQuit && !ImGui::IsPopupOpen("Quit?")) {
			ImGui::OpenPopup("Quit?");
		}
		if (ImGui::BeginPopupModal("Quit?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted("There are changes that are not saved (a template or reviewed drafts).");
			ImGui::TextUnformatted("Quit and lose them?");
			if (ImGui::Button("Quit without saving")) {
				_quit = true;
				_confirmQuit = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Keep editing") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				_confirmQuit = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void EditorApp::RequestClose()
	{
		if (AnyUnsaved()) {
			_confirmQuit = true;
		} else {
			_quit = true;
		}
	}

	void EditorApp::OpenTemplateFile()
	{
		if (!_ws || _job.valid()) {
			return;
		}
		const auto file = PickJsonFile();
		if (!file) {
			return;
		}
		core::Diagnostics diags;
		auto              layout = core::LoadLayoutFile(PathFromUtf8(*file), *file, diags);
		if (!layout || !layout->cell) {
			::MessageBoxA(nullptr, ("That file is not a Dova Stash template or layout with a cell:\n" + *file).c_str(), "Dova Stash Editor", MB_ICONWARNING);
			return;
		}
		const Workspace* ws = _ws.get();
		auto             lo = _homes ? _homes->lo : nullptr;
		const auto       cellRef = layout->cell->ToString();
		auto             shared = std::make_shared<core::Layout>(std::move(*layout));
		StartJob("Opening the template's home...", [this, ws, lo, cellRef, shared]() mutable -> std::function<void()> {
			std::vector<std::string> errors;
			if (!lo) {
				lo = std::shared_ptr<esp::LoadOrder>(ws->OpenLoadOrder(errors));
			}
			const auto key = lo->ParseFormRef(cellRef);
			if (!key || !lo->FindCell(*key)) {
				return [cellRef] {
					::MessageBoxA(nullptr, ("The template's cell " + cellRef + " is not in the active load order.").c_str(), "Dova Stash Editor", MB_ICONWARNING);
				};
			}
			return [this, lo, k = *key, shared]() {
				_pendingLayout = *shared;
				_pendingLayoutDirty = false;
				_cellDraft = -1;
				_mode = Mode::Browse;
				OpenCellAsync(lo, k, false);
			};
		});
	}

	void EditorApp::OpenDraftInEditor(int a_index)
	{
		if (!_homes || _job.valid()) {
			return;
		}
		auto& e = EnsureDraft(a_index);
		_pendingLayout = e.draft->layout;
		_pendingLayoutDirty = true;
		_cellDraft = a_index;
		_mode = Mode::Browse;
		_history.clear();
		OpenCellAsync(_homes->lo, _homes->homes[static_cast<std::size_t>(a_index)].cell, false);
	}

	void EditorApp::SyncCellToDraft()
	{
		if (!_cell || !_homes || _cellDraft < 0 || static_cast<std::size_t>(_cellDraft) >= _homes->entries.size()) {
			return;
		}
		auto& e = _homes->entries[static_cast<std::size_t>(_cellDraft)];
		if (!e.draft || !_cell->layout.cell || !e.draft->layout.cell || *_cell->layout.cell != *e.draft->layout.cell) {
			return;
		}
		if (core::SerializeLayout(_cell->layout) != core::SerializeLayout(e.draft->layout)) {
			e.draft->layout = _cell->layout;
			e.reviewed = true;
			e.problems = home::ValidateDraft(e.draft->layout, _categorySet ? &*_categorySet : nullptr);
			const auto& h = _homes->homes[static_cast<std::size_t>(_cellDraft)];
			_state.Advance(h.cellRef, h.name, e.problems.empty() ? HomeStage::Validated : HomeStage::Reviewed);
		}
	}

	void EditorApp::SaveDraft(int a_index, bool a_overwrite)
	{
		auto&           e = EnsureDraft(a_index);
		const auto&     h = _homes->homes[static_cast<std::size_t>(a_index)];
		const bool      mod = _ws && _ws->Instance();
		const auto      base = home::SaveBase(*_ws);
		std::error_code ec;
		if (!_templateAuthor.empty()) {
			e.draft->layout.author = _templateAuthor;
		}
		if (!a_overwrite && (fs::exists(home::TemplatePath(base, e.draft->layout), ec) || !HomeStatus(h).empty())) {
			_confirmDraftSave = true;
			return;
		}
		_confirmDraftSave = false;
		const auto r = home::SaveTemplateFile(base, e.draft->layout, mod);
		if (r.ok) {
			e.saveMessage = std::format("Saved version {} to {}{}", r.version, PathToUtf8(r.path), r.replaced ? " (the old file is kept as .bak)" : "");
			if (mod) {
				e.saveMessage += std::format(". In MO2: refresh with F5 and enable the \"{}\" mod", home::kTemplatesMod);
			}
			_state.Advance(h.cellRef, h.name, HomeStage::Saved, PathToUtf8(r.path));
			_homes->templates = home::LoadTemplateIndex(*_ws);
		} else {
			e.saveMessage = "Not saved: " + r.error;
		}
	}

	void EditorApp::DrawWelcome()
	{
		const float width = std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 58.0f);
		const float pad = std::max((ImGui::GetContentRegionAvail().x - width) * 0.5f, 0.0f);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
		ImGui::BeginChild("welcome", ImVec2(width, 0), ImGuiChildFlags_None);
		ImGui::PushTextWrapPos(0.0f);
		ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.6f));
		ImGui::SetWindowFontScale(1.7f);
		ImGui::TextUnformatted("Dova Stash Editor");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::TextDisabled("Make Dova Stash home templates without starting the game: every chest's job, the master, the displays, checked and saved.");
		ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.5f));

		// 1. Setup
		ImGui::SeparatorText("Your setup");
		if (!_ws) {
			ImGui::TextUnformatted("Where is your Mod Organizer 2 instance? (the folder with ModOrganizer.ini)");
			ImGui::SetNextItemWidth(width - ImGui::GetFontSize() * 9.0f);
			ImGui::InputTextWithHint("##mo2w", "E:\\modding\\My Skyrim", _mo2Path, sizeof(_mo2Path));
			ImGui::SameLine();
			if (ImGui::Button("Open##w")) {
				_options.mo2 = _mo2Path[0] ? std::optional(PathFromUtf8(_mo2Path)) : std::nullopt;
				_options.profile.reset();
				OpenWorkspace();
			}
			if (!_wsError.empty()) {
				ImGui::TextColored(kBad, "%s", _wsError.c_str());
			}
			ImGui::TextDisabled("Started from MO2 (Tools > Executables) the editor finds it by itself; the folder is remembered.");
		} else {
			ImGui::TextColored(kGood, "Ready.");
			ImGui::SameLine();
			ImGui::TextWrapped("%s", _ws->Describe().c_str());
		}

		// 2. Homes
		ImGui::SeparatorText("Player homes");
		if (_ws && _job.valid() && _progress) {
			const auto  done = _progress->done.load();
			const auto  total = _progress->total.load();
			std::string stage;
			{
				std::scoped_lock lock(_progress->mutex);
				stage = _progress->stage;
			}
			ImGui::ProgressBar(total ? static_cast<float>(done) / static_cast<float>(total) : 0.0f, ImVec2(width, 0), stage.c_str());
			ImGui::TextDisabled("Reading every cell of the active load order once (a few seconds).");
		} else if (_homes) {
			int shipped = 0, yours = 0;
			for (const auto& h : _homes->homes) {
				bool       user = false;
				const auto s = HomeStatus(h, &user);
				shipped += !s.empty() && !user ? 1 : 0;
				yours += user ? 1 : 0;
			}
			ImGui::SetWindowFontScale(1.25f);
			ImGui::Text("%zu player homes found, %d have templates", _homes->homes.size(), shipped + yours);
			ImGui::SetWindowFontScale(1.0f);
			ImGui::TextDisabled("%d shipped or from other mods, %d yours; %zu plugins read in %lld ms; %zu items in the load order.", shipped, yours,
				_homes->lo->PluginCount(), _homes->ms, _homes->catalog ? _homes->catalog->items.size() : 0);
		} else if (_ws) {
			ImGui::TextDisabled("Not counted yet.");
			ImGui::SameLine();
			if (ImGui::SmallButton("Find homes")) {
				ScanHomes(false);
			}
		} else {
			ImGui::TextDisabled("Open your setup first.");
		}

		// 3. Actions
		ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.4f));
		const float bw = (width - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
		if (BigAction("Open a home", "Pick the home's mod from your mod list, then the home: its 3D view, plan, containers and template.", bw, _ws != nullptr)) {
			_mode = Mode::Browse;
		}
		ImGui::SameLine();
		if (BigAction("Draft all homes  (Ctrl+D)", "A draft template for every home at once: master, categories with reasons. Review, then save.", bw,
				_ws != nullptr && !_job.valid())) {
			if (_homes) {
				_mode = Mode::Drafts;
			} else {
				ScanHomes(true);
			}
		}
		ImGui::SameLine();
		if (BigAction("Open a template...  (Ctrl+O)", "A shipped or downloaded template, or a layout saved in game: open its home and edit it.", bw,
				_ws != nullptr && !_job.valid())) {
			OpenTemplateFile();
		}

		// 4. To do
		ImGui::SeparatorText("Your homes in progress");
		int counts[5]{};
		for (const auto& [cell, r] : _state.homes) {
			++counts[static_cast<int>(r.stage)];
		}
		if (_state.homes.empty()) {
			ImGui::TextDisabled("Nothing yet. Homes you draft, review and save are listed here, also after a restart.");
		} else {
			ImGui::Text("%d drafted, %d reviewed, %d validated, %d saved", counts[1], counts[2], counts[3], counts[4]);
			ImGui::SameLine();
			ImGui::TextDisabled("(drafted -> reviewed -> validated -> saved)");
			if (ImGui::BeginTable("todo", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY,
					ImVec2(width, std::min(ImGui::GetContentRegionAvail().y - ImGui::GetFontSize(), ImGui::GetFontSize() * 16.0f)))) {
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn("Home", ImGuiTableColumnFlags_WidthStretch, 0.35f);
				ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6.0f);
				ImGui::TableSetupColumn("Updated", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.5f);
				ImGui::TableSetupColumn("Saved to", ImGuiTableColumnFlags_WidthStretch, 0.65f);
				ImGui::TableHeadersRow();
				std::vector<std::pair<std::string, const HomeRecord*>> rows;
				for (const auto& [cell, r] : _state.homes) {
					rows.emplace_back(cell, &r);
				}
				std::ranges::sort(rows, [](const auto& a, const auto& b) { return a.second->updated > b.second->updated; });
				for (const auto& [cell, r] : rows) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::PushID(cell.c_str());
					if (ImGui::Selectable(r->name.empty() ? cell.c_str() : r->name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns) && _ws && !_job.valid()) {
						_pendingHomeSelect = r->name;
						if (_homes) {
							for (std::size_t i = 0; i < _homes->homes.size(); ++i) {
								if (_homes->homes[i].cellRef == cell) {
									_draftSel = static_cast<int>(i);
								}
							}
							_pendingHomeSelect.reset();
							_mode = Mode::Drafts;
						} else {
							ScanHomes(true);
						}
					}
					ImGui::SetItemTooltip("%s\nClick to continue with it in the drafts view", cell.c_str());
					ImGui::PopID();
					ImGui::TableNextColumn();
					ImGui::TextColored(StageColour(r->stage), "%s", HomeStageName(r->stage));
					ImGui::TableNextColumn();
					ImGui::TextDisabled("%s", r->updated.c_str());
					ImGui::TableNextColumn();
					ImGui::TextDisabled("%s", r->savedPath.c_str());
				}
				ImGui::EndTable();
			}
		}
		ImGui::PopTextWrapPos();
		ImGui::EndChild();
	}

	void EditorApp::DrawDrafts()
	{
		if (!_ws) {
			ImGui::TextWrapped("Open your MO2 setup first (Start page).");
			return;
		}
		if (!_homes) {
			if (_job.valid() && _progress) {
				const auto  done = _progress->done.load();
				const auto  total = _progress->total.load();
				std::string stage;
				{
					std::scoped_lock lock(_progress->mutex);
					stage = _progress->stage;
				}
				ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 2.0f));
				ImGui::TextUnformatted("Finding every player home of the active load order...");
				ImGui::ProgressBar(total ? static_cast<float>(done) / static_cast<float>(total) : 0.0f, ImVec2(ImGui::GetFontSize() * 30.0f, 0), stage.c_str());
			} else if (!_job.valid()) {
				ScanHomes(true);
			}
			return;
		}
		if (_homes->homes.empty()) {
			ImGui::TextWrapped("No player homes in the %zu active plugins. A home is a cell whose location is a player house, that the player owns, "
			                   "or that has mannequins and nothing that respawns. Enable the home mods in MO2's profile, then Rescan (F5).",
				_homes->lo->PluginCount());
			if (ImGui::Button("Rescan (F5)")) {
				_homes.reset();
				ScanHomes(true);
			}
			return;
		}
		const float listWidth = std::max(ImGui::GetContentRegionAvail().x * 0.34f, ImGui::GetFontSize() * 22.0f);
		DrawDraftList(listWidth);
		ImGui::SameLine();
		ImGui::BeginChild("review", ImVec2(0, 0), ImGuiChildFlags_Borders);
		if (_draftSel >= 0 && static_cast<std::size_t>(_draftSel) < _homes->homes.size()) {
			DrawDraftReview(_draftSel);
		} else {
			ImGui::TextDisabled("Pick a home on the left: its draft shows here.");
		}
		ImGui::EndChild();
	}

	void EditorApp::DrawDraftList(float a_width)
	{
		ImGui::BeginChild("draftlist", ImVec2(a_width, 0), ImGuiChildFlags_Borders);
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##draftfilter", "Filter: home, plugin or cell", _draftFilter, sizeof(_draftFilter));
		ImGui::Checkbox("Only homes without a template", &_hideTemplated);
		ImGui::SameLine();
		if (ImGui::SmallButton("Rescan (F5)") || (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F5, false))) {
			if (!AnyUnsaved() && !_job.valid()) {
				_homes.reset();
				ScanHomes(true);
				ImGui::EndChild();
				return;
			}
		}
		ImGui::SetItemTooltip("Read the load order again (after changing mods in MO2). Asks nothing: it waits until every reviewed draft is saved.");
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
		if (ImGui::InputTextWithHint("##author", "Author (your name)", &_templateAuthor)) {
			_state.author = _templateAuthor;
			_state.Save();
		}
		ImGui::SameLine();
		int drafted = 0, saveable = 0;
		for (std::size_t i = 0; i < _homes->homes.size(); ++i) {
			drafted += _homes->entries[i].draft ? 1 : 0;
			saveable += _homes->entries[i].draft && HomeStatus(_homes->homes[i]).empty() ? 1 : 0;
		}
		ImGui::BeginDisabled(saveable == 0);
		if (ImGui::SmallButton(std::format("Save {} drafts...", saveable).c_str())) {
			_confirmSaveAll = true;
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Saves the drafts you opened of homes that have no template yet (asks first).");

		// Keyboard: Up/Down walk the list.
		std::vector<int> shown;
		const std::string filter = _draftFilter;
		for (std::size_t i = 0; i < _homes->homes.size(); ++i) {
			const auto& h = _homes->homes[i];
			if (!filter.empty() && !IContains(h.name, filter) && !IContains(h.plugin, filter) && !IContains(h.editorId, filter) && !IContains(h.cellRef, filter)) {
				continue;
			}
			if (_hideTemplated && !HomeStatus(h).empty()) {
				continue;
			}
			shown.push_back(static_cast<int>(i));
		}
		if (!ImGui::GetIO().WantTextInput && !shown.empty() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
			const auto pos = std::ranges::find(shown, _draftSel);
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
				_draftSel = pos == shown.end() || pos + 1 == shown.end() ? shown.front() : *(pos + 1);
			} else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
				_draftSel = pos == shown.end() || pos == shown.begin() ? shown.back() : *(pos - 1);
			}
		}
		ImGui::TextDisabled("%zu of %zu homes, %d drafts opened", shown.size(), _homes->homes.size(), drafted);
		if (ImGui::BeginTable("drafthomes", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Home", ImGuiTableColumnFlags_WidthStretch, 0.5f);
			ImGui::TableSetupColumn("Template", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5.0f);
			ImGui::TableSetupColumn("Draft", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.6f);
			ImGui::TableSetupColumn("Plugin", ImGuiTableColumnFlags_WidthStretch, 0.3f);
			ImGui::TableHeadersRow();
			for (const int i : shown) {
				const auto& h = _homes->homes[static_cast<std::size_t>(i)];
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::PushID(i);
				const auto label = h.extra.empty() ? h.name : std::format("{}  (+{})", h.name, h.extra.size());
				if (ImGui::Selectable(label.c_str(), _draftSel == i, ImGuiSelectableFlags_SpanAllColumns)) {
					_draftSel = i;
				}
				if (_draftSel == i && ImGui::IsWindowAppearing()) {
					ImGui::SetScrollHereY();
				}
				if (ImGui::BeginItemTooltip()) {
					ImGui::Text("%s  %s", h.cellRef.c_str(), h.editorId.c_str());
					for (const auto& e : h.extra) {
						ImGui::Text("+ %s (%s)%s", e.name.empty() ? e.editorId.c_str() : e.name.c_str(), e.cellRef.c_str(), e.likely ? "" : ": storage behind a door");
					}
					ImGui::Text("%d containers, %d displays", h.containers, h.displays);
					ImGui::EndTooltip();
				}
				ImGui::PopID();
				ImGui::TableNextColumn();
				bool       user = false;
				const auto status = HomeStatus(h, &user);
				if (status.empty()) {
					ImGui::TextDisabled("none");
				} else {
					ImGui::TextColored(user ? kGood : kInfo, "%s", status.c_str());
				}
				ImGui::TableNextColumn();
				const auto stage = _state.StageOf(h.cellRef);
				ImGui::TextColored(StageColour(stage), "%s", HomeStageName(stage));
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%s", h.plugin.c_str());
			}
			ImGui::EndTable();
		}

		// Save-all confirmation.
		if (_confirmSaveAll && !ImGui::IsPopupOpen("Save drafts")) {
			ImGui::OpenPopup("Save drafts");
		}
		if (ImGui::BeginPopupModal("Save drafts", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			const auto      base = home::SaveBase(*_ws);
			int             fresh = 0, existing = 0;
			std::error_code ec;
			for (std::size_t i = 0; i < _homes->homes.size(); ++i) {
				auto& e = _homes->entries[i];
				if (e.draft && HomeStatus(_homes->homes[i]).empty()) {
					auto copy = e.draft->layout;
					copy.author = _templateAuthor;
					(fs::exists(home::TemplatePath(base, copy), ec) ? existing : fresh) += 1;
				}
			}
			ImGui::Text("Save %d drafts of homes without a template into", fresh + existing);
			ImGui::TextDisabled("%s", PathToUtf8(base).c_str());
			ImGui::Text("%d new files, %d files that exist already (each is backed up as .bak first, its version raised).", fresh, existing);
			if (_templateAuthor.empty()) {
				ImGui::TextColored(kWarn, "No author name: the files go under \"Unknown\". Type your name above first?");
			}
			if (ImGui::Button("Save them")) {
				for (std::size_t i = 0; i < _homes->homes.size(); ++i) {
					if (_homes->entries[i].draft && HomeStatus(_homes->homes[i]).empty()) {
						SaveDraft(static_cast<int>(i), true);
					}
				}
				_confirmSaveAll = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				_confirmSaveAll = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		ImGui::EndChild();
	}

	void EditorApp::DrawDraftReview(int a_index)
	{
		const auto& h = _homes->homes[static_cast<std::size_t>(a_index)];
		auto&       e = EnsureDraft(a_index);
		auto&       d = *e.draft;
		const bool  busy = _job.valid();

		ImGui::SetWindowFontScale(1.35f);
		ImGui::TextUnformatted(h.name.c_str());
		ImGui::SetWindowFontScale(1.0f);
		ImGui::SameLine();
		ImGui::TextDisabled("%s  %s  %s%s", h.cellRef.c_str(), h.editorId.c_str(), h.plugin.c_str(), h.lastPlugin != h.plugin ? (" (last edited by " + h.lastPlugin + ")").c_str() : "");
		if (!h.extra.empty()) {
			std::string cells;
			for (const auto& x : h.extra) {
				cells += (cells.empty() ? "" : ", ") + (x.name.empty() ? x.editorId : x.name) + (x.likely ? "" : " (storage)");
			}
			ImGui::TextDisabled("More cells of this home (joined by their doors): %s", cells.c_str());
		}

		// Templates it already has.
		const auto templates = _homes->templates.ForCell(_homes->lo->ToFormRef(h.cell));
		if (templates.empty()) {
			ImGui::TextColored(kWarn, "No template yet:");
			ImGui::SameLine();
			ImGui::TextUnformatted("this draft is a starting point. Accept or reject the suggestions, then save.");
		}
		for (const auto* t : templates) {
			ImGui::TextColored(t->user ? kGood : kInfo, "%s template:", t->user ? "Your" : "A");
			ImGui::SameLine();
			ImGui::TextUnformatted(t->source.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("(%s, version %s)", t->origin.c_str(), t->layout.version.c_str());
			if (_categorySet && t->layout.cell && d.layout.cell && *t->layout.cell == *d.layout.cell) {
				const auto cmp = home::CompareToTemplate(d.layout, t->layout, *_categorySet);
				ImGui::SameLine();
				ImGui::TextDisabled("- the draft sends %d of %d categories to the same container, master %s", cmp.sameDestination, cmp.categories,
					cmp.sameMaster ? "the same" : "different");
				if (ImGui::BeginItemTooltip()) {
					ImGui::TextUnformatted("Template containers with categories, against the draft:");
					for (const auto& line : cmp.details) {
						ImGui::BulletText("%s", line.c_str());
					}
					ImGui::EndTooltip();
				}
			}
		}

		// Actions.
		ImGui::BeginDisabled(busy);
		if (ImGui::Button("Open in the editor (Enter)") || (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::IsAnyItemActive())) {
			OpenDraftInEditor(a_index);
			ImGui::EndDisabled();
			return;
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Opens the home's 3D view and plan with this draft: change the master, categories, labels, displays in the role editor.");
		ImGui::SameLine();
		if (ImGui::Button("Save template (Ctrl+S)") || (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))) {
			SaveDraft(a_index, false);
		}
		ImGui::SetItemTooltip("Into %s. An existing file is backed up first; asks before anything is replaced.", PathToUtf8(home::SaveBase(*_ws)).c_str());
		ImGui::SameLine();
		if (ImGui::Button("Accept all")) {
			for (std::size_t ci = 0; ci < d.plan.containers.size(); ++ci) {
				for (std::size_t si = 0; si < d.plan.containers[ci].suggestions.size(); ++si) {
					if (d.plan.containers[ci].suggestions[si].lostTo.empty()) {
						home::SetSuggestion(d, ci, si, true);
					}
				}
			}
			e.reviewed = true;
		}
		ImGui::SetItemTooltip("Accepts every suggestion no other container has a stronger claim on.");
		ImGui::SameLine();
		if (ImGui::Button("Reject the weak ones")) {
			for (std::size_t ci = 0; ci < d.plan.containers.size(); ++ci) {
				for (std::size_t si = 0; si < d.plan.containers[ci].suggestions.size(); ++si) {
					if (d.plan.containers[ci].suggestions[si].confidence < 0.5f) {
						home::SetSuggestion(d, ci, si, false);
					}
				}
			}
			e.reviewed = true;
		}
		ImGui::SetItemTooltip("Rejects every suggestion under 50%% (the fill pass's guesses among them).");
		ImGui::SameLine();
		if (ImGui::Button("Draft again")) {
			if (!e.reviewed) {
				e.draft.reset();
				EnsureDraft(a_index);
				return;
			}
			ImGui::OpenPopup("Draft again?");
		}
		ImGui::SetItemTooltip("Makes the draft again from the plugins (after editing DraftRules.json, for example).");
		if (ImGui::BeginPopupModal("Draft again?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted("You changed this draft. Make it again and lose those changes?");
			if (ImGui::Button("Draft again")) {
				e.draft.reset();
				e.reviewed = false;
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				EnsureDraft(a_index);
				return;
			}
			ImGui::SameLine();
			if (ImGui::Button("Keep my changes") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// Save confirmation (file exists, or the home has a template already).
		if (_confirmDraftSave && !ImGui::IsPopupOpen("Save this draft?")) {
			ImGui::OpenPopup("Save this draft?");
		}
		if (ImGui::BeginPopupModal("Save this draft?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			std::error_code ec;
			const auto      path = home::TemplatePath(home::SaveBase(*_ws), d.layout);
			if (fs::exists(path, ec)) {
				ImGui::TextColored(kWarn, "%s exists.", PathToUtf8(path).c_str());
				ImGui::TextUnformatted("Overwrite it? The old file is kept as .bak and the version goes up.");
			} else {
				ImGui::Text("Save to %s?", PathToUtf8(path).c_str());
			}
			for (const auto* t : templates) {
				ImGui::TextColored(kInfo, "This home already has a template: %s (%s).", t->source.c_str(), t->origin.c_str());
			}
			if (!templates.empty()) {
				ImGui::TextDisabled("That file is not touched; in game the template with the higher priority, then the newer version, is offered first.");
			}
			if (ImGui::Button("Save")) {
				SaveDraft(a_index, true);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				_confirmDraftSave = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		if (!e.saveMessage.empty()) {
			ImGui::TextColored(e.saveMessage.starts_with("Saved") ? kGood : kBad, "%s", e.saveMessage.c_str());
		}

		// Master.
		ImGui::SeparatorText("Master");
		if (d.plan.master) {
			ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", Label(d.objects[*d.plan.master]).c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("%s", d.objects[*d.plan.master].id.c_str());
		}
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextDisabled("Why: %s", d.plan.masterReason.c_str());
		ImGui::PopTextWrapPos();
		if (!d.plan.masterRunnersUp.empty()) {
			std::string list;
			for (const auto& r : d.plan.masterRunnersUp) {
				list += (list.empty() ? "" : "; ") + r;
			}
			ImGui::TextDisabled("Next best: %s", list.c_str());
			ImGui::SetItemTooltip("Another master: open the home in the editor, select the container and press Make master.");
		}

		// Containers and their suggestions.
		int accepted = 0, ignored = 0, total = 0;
		for (const auto& c : d.plan.containers) {
			++total;
			ignored += c.ignored ? 1 : 0;
			accepted += std::ranges::any_of(c.suggestions, [](const core::DraftSuggestion& s) { return s.accepted; }) ? 1 : 0;
		}
		ImGui::SeparatorText(std::format("Containers: {} with categories, {} ignored, {} in all", accepted, ignored, total).c_str());
		ImGui::TextDisabled("Tick a suggestion to accept it, untick to reject it. Hover one to see why it was suggested. Green: the name says so; "
		                    "yellow: a station or items nearby; orange: a hint.");
		const float tableH = std::max(ImGui::GetContentRegionAvail().y * 0.62f, ImGui::GetFontSize() * 10.0f);
		if (ImGui::BeginTable("draftcontainers", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable,
				ImVec2(0, tableH))) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Container", ImGuiTableColumnFlags_WidthStretch, 0.28f);
			ImGui::TableSetupColumn("Suggested categories", ImGuiTableColumnFlags_WidthStretch, 0.72f);
			ImGui::TableSetupColumn("Cell", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.0f);
			ImGui::TableHeadersRow();
			// Containers with suggestions first, then the rest, ignored last.
			std::vector<std::size_t> order(d.plan.containers.size());
			for (std::size_t k = 0; k < order.size(); ++k) {
				order[k] = k;
			}
			auto rank = [&](std::size_t k) {
				const auto& c = d.plan.containers[k];
				if (d.plan.master && *d.plan.master == c.object) {
					return 0;
				}
				if (c.ignored) {
					return 3;
				}
				return std::ranges::any_of(c.suggestions, [](const core::DraftSuggestion& s) { return s.accepted; }) ? 1 : 2;
			};
			std::ranges::stable_sort(order, [&](std::size_t a, std::size_t b) { return rank(a) < rank(b); });
			bool changed = false;
			for (const auto ci : order) {
				auto&       c = d.plan.containers[ci];
				const auto& o = d.objects[c.object];
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::PushID(static_cast<int>(ci));
				const bool master = d.plan.master && *d.plan.master == c.object;
				if (c.ignored) {
					ImGui::TextDisabled("%s", Label(o).c_str());
				} else {
					ImGui::TextUnformatted(Label(o).c_str());
				}
				ImGui::SetItemTooltip("%s\nbase %s", o.id.c_str(), o.editorId.c_str());
				ImGui::TableNextColumn();
				if (master) {
					ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "master: takes everything without a place");
				} else if (c.ignored) {
					ImGui::TextDisabled("ignored: %s", c.note.c_str());
				} else if (c.suggestions.empty()) {
					ImGui::TextDisabled("no suggestion: what it would hold goes to the master");
				}
				for (std::size_t si = 0; si < c.suggestions.size() && !c.ignored && !master; ++si) {
					auto& s = c.suggestions[si];
					ImGui::PushID(static_cast<int>(si));
					if (si > 0) {
						ImGui::SameLine();
					}
					if (ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 9.0f) {
						ImGui::NewLine();
					}
					bool on = s.accepted;
					ImGui::PushStyleColor(ImGuiCol_Text, ConfidenceColour(s.confidence));
					if (ImGui::Checkbox(std::format("{} {:.0f}%", s.pattern, s.confidence * 100.0f).c_str(), &on)) {
						home::SetSuggestion(d, ci, si, on);
						changed = true;
					}
					ImGui::PopStyleColor();
					std::string tip = std::format("{} ({}, {:.0f}%)\n\n{}", s.accepted ? "Accepted" : "Not accepted", core::DraftSourceName(s.source), s.confidence * 100.0f, s.reason);
					if (!s.lostTo.empty()) {
						tip += "\n\nAnother container has a stronger claim; accepting it here too is allowed (the first one listed wins in game).";
					}
					tip += "\n\nClick to " + std::string(s.accepted ? "reject" : "accept") + " it.";
					Tooltip(tip);
					ImGui::PopID();
				}
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%s", o.cell == h.cellRef ? "main" : "other");
				ImGui::PopID();
			}
			ImGui::EndTable();
			if (changed) {
				e.reviewed = true;
				e.problems = home::ValidateDraft(d.layout, _categorySet ? &*_categorySet : nullptr);
				_state.Advance(h.cellRef, h.name, e.problems.empty() ? HomeStage::Validated : HomeStage::Reviewed);
			}
		}

		// Displays and checks.
		const auto displays = d.layout.displays.size();
		if (ImGui::CollapsingHeader(std::format("Displays used: {}###displays", displays).c_str())) {
			for (const auto& disp : d.layout.displays) {
				std::string what = disp.kind ? std::string(core::DisplayKindName(*disp.kind)) : std::string("?");
				if (!disp.categories.empty()) {
					what += " for " + disp.categories.front();
				}
				if (!disp.items.empty()) {
					what += std::format(" ({} items from its script)", disp.items.size());
				}
				ImGui::BulletText("%s: %s", disp.label.c_str(), what.c_str());
			}
			if (displays == 0) {
				ImGui::TextDisabled("No racks, mannequins, plaques or cases found.");
			}
		}
		ImGui::SeparatorText("Checks");
		if (e.problems.empty()) {
			ImGui::TextColored(kGood, "No problems: the draft passes the same checks as the DLL's.");
		}
		for (const auto& p : e.problems) {
			ImGui::TextColored(kWarn, "%s", p.c_str());
		}
	}
}
