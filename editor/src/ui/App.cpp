// The workspace (docs/EDITOR-UX.md §2): one Frame() draws the title row, the homes rail, the viewport with its
// floating toolbar, the inspector, the drawer and the status row. This file owns the workspace, the background
// jobs, opening cells and homes, the viewport and the keyboard (§4).

#include "ui/App.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <numbers>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ShObjIdl.h>

#include <imgui.h>

#include "Util.h"
#include "Log.h"
#include "data/Assets.h"
#include "dova/core/Layout.h"
#include "render/Thumbnail.h"
#include "ui/Art.h"
#include "ui/Common.h"

namespace dova::editor::ui
{
	using namespace detail;

	namespace
	{
		using Clock = std::chrono::steady_clock;
		long long Ms(Clock::time_point a_start)
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - a_start).count();
		}

		std::optional<std::string> RunFileDialog(bool a_save, bool a_folder, const wchar_t* a_title, const std::string& a_defaultName)
		{
			std::optional<std::string> out;
			IFileDialog*               dlg = nullptr;
			if (FAILED(::CoCreateInstance(a_save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, a_save ? IID_IFileSaveDialog : IID_IFileOpenDialog,
					reinterpret_cast<void**>(&dlg)))) {
				return out;
			}
			DWORD opts = 0;
			dlg->GetOptions(&opts);
			dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | (a_folder ? FOS_PICKFOLDERS : 0));
			dlg->SetTitle(a_title);
			if (!a_folder) {
				const COMDLG_FILTERSPEC filter[] = { { L"Dova Stash template or layout (*.json)", L"*.json" } };
				dlg->SetFileTypes(1, filter);
				dlg->SetDefaultExtension(L"json");
			}
			if (!a_defaultName.empty()) {
				dlg->SetFileName(Widen(a_defaultName).c_str());
			}
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
	}

	namespace
	{
		// --window: a client area larger than the screen (screenshots on a small or headless display). Windows caps
		// a window at the screen size through WM_GETMINMAXINFO; the window procedure is wrapped to lift that cap.
		WNDPROC g_windowProc = nullptr;

		LRESULT CALLBACK LargeWindowProc(HWND a_hwnd, UINT a_msg, WPARAM a_wparam, LPARAM a_lparam)
		{
			const LRESULT r = ::CallWindowProcW(g_windowProc, a_hwnd, a_msg, a_wparam, a_lparam);
			if (a_msg == WM_GETMINMAXINFO) {
				auto* info = reinterpret_cast<MINMAXINFO*>(a_lparam);
				info->ptMaxTrackSize = POINT{ 16384, 16384 };
				info->ptMaxSize = POINT{ 16384, 16384 };
			}
			return r;
		}
	}

	namespace detail
	{
		std::optional<std::string> PickFolder() { return RunFileDialog(false, true, L"MO2 instance folder (with ModOrganizer.ini)", {}); }
		std::optional<std::string> PickJsonFile() { return RunFileDialog(false, false, L"Open a Dova Stash template or layout", {}); }
		std::optional<std::string> PickSaveJsonFile(const std::string& a_defaultName) { return RunFileDialog(true, false, L"Save the template as", a_defaultName); }
	}

	EditorApp::EditorApp(WorkspaceOptions a_options, const GuiOptions& a_gui, ID3D11Device* a_device, ID3D11DeviceContext* a_context) :
		_options(std::move(a_options)),
		_device(a_device),
		_context(a_context)
	{
		::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		_unattended = a_gui.screenshot.has_value();
		// A screenshot run (tests, the guide) neither reads nor writes the author's state.json: its homes'
		// stages and panel sizes stay theirs. --state or DOVASTASH_EDITOR_STATE give it a file of its own.
		if (a_gui.state) {
			_state.path = PathFromUtf8(*a_gui.state);
		} else if (a_gui.screenshot && ::GetEnvironmentVariableW(L"DOVASTASH_EDITOR_STATE", nullptr, 0) == 0) {
			_state.memoryOnly = true;
		}
		_state.Load();
		_templateAuthor = _state.author;
		const auto& t = w::Th();
		if (_state.ui.railWidth <= 0.0f) {
			_state.ui.railWidth = 320.0f * t.scale;  // docs/EDITOR-UX.md §2.2
		}
		if (_state.ui.inspectorWidth <= 0.0f) {
			_state.ui.inspectorWidth = 380.0f * t.scale;
		}
		// The instance remembered from last time, when none is given and none is found (asked once, ui/State).
		if (!_options.mo2 && !_options.data && !mo2::DetectInstance() && _state.mo2) {
			_options.mo2 = PathFromUtf8(*_state.mo2);
			if (!_options.profile && _state.profile) {
				_options.profile = _state.profile;
			}
		} else if (!_options.mo2 && !_options.data && !mo2::DetectInstance() && _state.data) {
			_options.data = PathFromUtf8(*_state.data);  // a Data folder picked last time, without MO2
		}
		if (_options.mo2 || _options.data) {
			std::snprintf(_mo2Path, sizeof(_mo2Path), "%s", PathToUtf8(_options.mo2 ? *_options.mo2 : *_options.data).c_str());
		}
		// Before the command line's overrides: opening saves the state (the instance), and they are for this run.
		_holdFrames = 3;  // OpenWorkspace and the scan below: under MO2, after the first frames (App.h)
		OpenWorkspace();
		const UiLayout remembered = _state.ui;
		// --tab: the view, a drawer tab or the inspector's Template panel (docs/EDITOR-UX.md §4).
		if (a_gui.tab) {
			const auto& tab = *a_gui.tab;
			if (core::IEquals(tab, "3d")) {
				_state.ui.view = 0;
			} else if (core::IEquals(tab, "plan")) {
				_state.ui.view = 1;
			} else if (core::IEquals(tab, "template")) {
				_pendingPanel = Panel::Template;
			} else {
				static constexpr std::pair<const char*, DrawerTab> tabs[] = { { "containers", DrawerTab::Containers }, { "displays", DrawerTab::Displays },
					{ "doors", DrawerTab::Doors }, { "preview", DrawerTab::Preview } };
				for (const auto& [name, value] : tabs) {
					if (core::IEquals(tab, name)) {
						_drawerTab = value;
						_state.ui.drawerOpen = true;
					}
				}
			}
		}
		if (a_gui.inspector) {
			static constexpr std::pair<const char*, Panel> panels[] = { { "selection", Panel::Selection }, { "template", Panel::Template }, { "draft", Panel::Draft },
				{ "checks", Panel::Checks } };
			for (const auto& [name, value] : panels) {
				if (core::IEquals(*a_gui.inspector, name)) {
					_pendingPanel = value;
				}
			}
		}
		// --sort, --filter, --quick: the drawer tables' sort, and the drawer tab's filter (ui/Tables.h).
		for (const auto& s : a_gui.sort) {
			if (const auto arg = ParseSortArg(s)) {
				auto& view = _tables[static_cast<std::size_t>(arg->table)];
				view.sort.push_back(arg->key);
				view.synced = false;
			}
		}
		if (a_gui.search) {
			_railSearch = *a_gui.search;
		}
		if (a_gui.filter) {
			Table(_drawerTab).filter = *a_gui.filter;
		}
		for (const auto& q : a_gui.quick) {
			if (const auto bit = QuickFilterBit(static_cast<DrawerTable>(_drawerTab), q)) {
				Table(_drawerTab).quick |= *bit;
			}
		}
		_pendingSelect = a_gui.select;
		_pendingMulti = a_gui.multi;
		if (a_gui.colours) {
			_colours = core::IEquals(*a_gui.colours, "fill") ? Colours::Fill : core::IEquals(*a_gui.colours, "category") ? Colours::Category : Colours::None;
		}
		if (a_gui.paint) {
			_paint = true;
			_paintCategory = core::ToLower(*a_gui.paint);
		}
		_pendingHome = a_gui.home;
		_homeQueue = a_gui.moreHomes;
		_pendingDrafts = a_gui.drafts;
		_pendingCell = a_gui.cell;
		if (a_gui.cell) {
			_pendingMod = a_gui.mod;
		}
		if (a_gui.mod) {
			_railSearch = *a_gui.mod;  // --mod: the rail shows that mod's homes
		}
		if (a_gui.palette) {
			_paletteOpen = true;
			_paletteFocus = true;
		}
		_windowSize = a_gui.window;
		_pendingPopover = a_gui.popover;
		// --layers: the Layers popover's toggles for this run ("effects,-structure"; Effects is the view's own).
		if (a_gui.layers) {
			std::string_view rest = *a_gui.layers;
			while (!rest.empty()) {
				const auto       comma = rest.find(',');
				std::string_view name = rest.substr(0, comma);
				rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
				const bool on = !name.starts_with('-');
				if (!on || name.starts_with('+')) {
					name.remove_prefix(1);
				}
				static constexpr const char* names[] = { "structure", "containers", "displays", "doors", "furniture", "people", "disabled", "effects", "other", "terrain", "roof",
					"missing" };
				bool* flags[] = { &_view.showStructure, &_view.showContainers, &_view.showDisplays, &_view.showDoors, &_view.showFurniture, &_view.showActors,
					&_view.showDisabled, _state.ui.view == 0 ? &_view.showEffects : &_planEffects, &_showMarkers, &_view.showTerrain, &_showRoof, &_view.showMissing };
				for (std::size_t i = 0; i < std::size(names); ++i) {
					if (core::IEquals(name, names[i])) {
						*flags[i] = on;
					}
				}
			}
		}
		// The command line's view and drawer are for this run: the file keeps the remembered layout until the author
		// changes a panel (SaveLayoutState).
		if (!(remembered == _state.ui)) {
			_state.uiOnDisk = remembered;
		}
		_savedUi = _state.ui;
		if (_ws && _holdFrames > 0 && _ws->UnderVfs()) {
			_heldHomes = true;
			Log("started from MO2: the home scan starts after the first frames");
		} else if (_ws) {
			ScanHomes();
		} else if (const auto error = NoHomeOpenError(_unattended && (a_gui.home || a_gui.cell || a_gui.mod || a_gui.drafts || a_gui.select), false, _wsError)) {
			// A screenshot run that cannot honour --home and the like: stderr and the log, the image still saved, exit 1.
			Log("{}", *error);
			std::fputs((*error + "\n").c_str(), stderr);
			std::fflush(stderr);
			_homeMissing = true;
		}
	}

	EditorApp::~EditorApp()
	{
		if (_job.valid()) {
			_job.wait();
		}
		if (!(_state.ui == _savedUi)) {
			_state.uiOnDisk.reset();
			_state.Save();
		}
		_thumbs.reset();
		_scene.reset();
		::CoUninitialize();
	}

	bool EditorApp::Idle() const
	{
		if (!_ws) {
			return !_job.valid();
		}
		if (_heldHomes || _heldArt || !_homes || _job.valid() || !_queue.empty() || _pendingCell || _pendingHome || _pendingDrafts || !_homeQueue.empty()) {
			return false;
		}
		if (!_cell || _showStart) {
			return !ArtPending();  // the start panel's art
		}
		return _state.ui.view != 0 || !_scene || _scene->Progress().finished;
	}

	std::string EditorApp::WaitingFor() const
	{
		// What Idle() waits on, in words (a screenshot run that times out logs it).
		if (_job.valid()) {
			return _jobLabel.empty() ? std::string("a job") : _jobLabel;
		}
		if (!_ws) {
			return "the workspace";
		}
		if (!_homes) {
			return "the homes";
		}
		if (!_queue.empty() || !_homeQueue.empty() || _pendingCell || _pendingHome || _pendingDrafts) {
			return "a home or cell to open";
		}
		if (!_cell || _showStart) {
			return "the start panel's art";
		}
		return "the 3D view";
	}

	void EditorApp::SetStatus(std::string a_text, bool a_bad)
	{
		_status = std::move(a_text);
		_statusBad = a_bad;
		_statusTime = ImGui::GetTime();
	}

	void EditorApp::Select(int a_index, bool a_frame)
	{
		_selectedRef = a_index;
		_multi.clear();
		if (a_frame && a_index >= 0) {
			_frameSelection = true;
			if (_cell) {
				CentrePlanOn(*_cell, a_index);
			}
		}
	}

	void EditorApp::SelectInView(int a_index, bool a_frame)
	{
		Select(a_index, a_frame);
		if (a_index >= 0) {
			_panel = Panel::Selection;
		}
	}

	void EditorApp::StartScene()
	{
		if (_thumbs) {
			_thumbs->Clear();
		}
		_scene.reset();
		if (!_cell || !_device) {
			return;
		}
		if (!_assets && _ws) {
			_assets = std::make_shared<Assets>(_ws->Files().Roots(), Assets::ArchiveList(*_ws));
		}
		if (!_assets) {
			return;
		}
		_scene = std::make_unique<render::Scene>(_device, _assets, render::BuildSceneDesc(_cell->info, _cell->context));
		Log("3D view: {} ({} references)", _cell->cellText, _cell->info.refs.size());
		_scene->Start();
		_cameraPending = true;
	}

	void EditorApp::OpenWorkspace()
	{
		_ws = std::make_unique<Workspace>();
		_activePlugins.clear();
		_wsError.clear();
		_scans.clear();
		_scene.reset();
		_cell.reset();
		_assets.reset();
		_history.clear();
		_selectedMod = -2;
		_homeGroups.clear();
		_categoriesLoaded = false;
		_categories.clear();
		_categorySet.reset();
		_homes.reset();
		_homesRequested = false;
		_bulkDone = false;
		_cellDraft = -1;
		_localDraft.reset();
		_preview.reset();
		_catalog.reset();
		_catalogLo.reset();
		_armorSets.reset();
		_armorSetsLo.reset();
		_saveTargets.reset();
		_undo.clear();
		_redo.clear();
		if (!_ws->Open(_options, _wsError)) {
			Log("workspace not opened: {}", _wsError);
			_ws.reset();
		} else if (Log("workspace: {}", _ws->Describe()), _activePlugins = _ws->ActivePlugins(); _ws->Instance()) {
			std::snprintf(_mo2Path, sizeof(_mo2Path), "%s", PathToUtf8(_ws->Instance()->root).c_str());
			// Remembered for the next start (asked once).
			_state.mo2 = PathToUtf8(_ws->Instance()->root);
			_state.profile = _ws->Profile();
			_state.data.reset();
			_state.Save();
		} else if (_dataPicked) {
			// A Data folder picked in the window (no MO2): the next start opens it.
			_state.data = PathToUtf8(_ws->DataDir());
			_state.mo2.reset();
			_state.profile.reset();
			_state.Save();
		}
		// The game-read art (the start panel's backdrop): the same Assets the 3D view uses.
		ReleaseArt();
		if (_ws && _device && _holdFrames > 0 && _ws->UnderVfs()) {
			_heldArt = true;
		} else if (_ws && _device) {
			_assets = std::make_shared<Assets>(_ws->Files().Roots(), Assets::ArchiveList(*_ws));
			LoadArt(_assets, _device);
		}
	}

	void EditorApp::StartJob(std::string a_label, std::function<std::function<void()>()> a_work)
	{
		_jobLabel = std::move(a_label);
		_job = std::async(std::launch::async, [this, work = std::move(a_work)]() -> std::function<void()> {
			try {
				return work();
			} catch (const std::exception& e) {
				std::string what = e.what();
				return [this, what]() {
					if (_unattended) {
						// A screenshot run: nobody can dismiss a message box (session 0, a test run): stderr, the log, exit 1.
						Log("error: {}", what);
						std::fprintf(stderr, "error: %s\n", what.c_str());
						std::fflush(stderr);
						_failed = true;
						_quit = true;
						return;
					}
					::MessageBoxA(nullptr, what.c_str(), "Dova Stash Editor", MB_ICONERROR);
				};
			}
		});
	}

	void EditorApp::Queue(std::function<void()> a_start)
	{
		if (!_job.valid() && _queue.empty()) {
			a_start();
		} else {
			_queue.push_back(std::move(a_start));
		}
	}

	void EditorApp::PollJob()
	{
		if (_job.valid() && _job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
			auto apply = _job.get();
			_jobLabel.clear();
			if (apply) {
				apply();
			}
		}
		// The next queued job once nothing runs (each may start a job of its own).
		while (!_job.valid() && !_queue.empty()) {
			auto next = std::move(_queue.front());
			_queue.pop_front();
			next();
		}
	}

	void EditorApp::SelectMod(int a_index)
	{
		_selectedMod = a_index;
		_scans.clear();
		Scan();
	}

	void EditorApp::Scan()
	{
		if (!_ws || _selectedMod < -1) {
			return;
		}
		if (Busy()) {
			Queue([this] { Scan(); });
			return;
		}
		std::vector<std::string> plugins = _selectedMod == -1 ? _ws->BasePlugins() : _ws->Mods()[static_cast<std::size_t>(_selectedMod)].plugins;
		const Workspace*         ws = _ws.get();
		const bool               exteriors = _exteriors;
		StartJob("Reading the mod's cells", [this, ws, plugins, exteriors]() -> std::function<void()> {
			const auto start = Clock::now();
			auto       scans = std::make_shared<std::vector<PluginScan>>();
			for (const auto& plugin : plugins) {
				const auto pluginStart = Clock::now();
				PluginScan s;
				s.plugin = plugin;
				auto lo = ws->OpenPlugin(plugin, s.errors);
				if (lo) {
					s.homes = home::FindHomes(*lo, *lo->PluginIndex(plugin), exteriors);
					for (const auto& h : s.homes) {
						s.cellRefs.push_back(lo->Format(h.cell));
					}
					s.lo = std::shared_ptr<esp::LoadOrder>(std::move(lo));
				}
				s.ms = Ms(pluginStart);
				scans->push_back(std::move(s));
			}
			const auto ms = Ms(start);
			return [this, scans, ms]() {
				_scans = std::move(*scans);
				_scanMs = ms;
				if (!_pendingCell) {
					return;
				}
				const auto wanted = *std::exchange(_pendingCell, std::nullopt);
				for (const auto& s : _scans) {
					for (std::size_t i = 0; i < s.homes.size(); ++i) {
						if (core::IEquals(s.homes[i].editorId, wanted) || core::IEquals(s.cellRefs[i], wanted)) {
							_cellDraft = -1;
							OpenCellAsync(s.lo, s.homes[i].cell, false);
							return;
						}
					}
				}
				// Not one of the scored cells (an exterior, a cellar): any cell of the mod's plugins and their masters.
				for (const auto& s : _scans) {
					if (!s.lo) {
						continue;
					}
					auto key = s.lo->ParseFormRef(wanted);
					if (!key || !s.lo->FindCell(*key)) {
						key = s.lo->FindCellByEditorId(wanted);
					}
					if (key) {
						_cellDraft = -1;
						OpenCellAsync(s.lo, *key, false);
						return;
					}
				}
				SetStatus("Cell not found: " + wanted, true);
			};
		});
	}

	void EditorApp::RequestNewHome(std::shared_ptr<esp::LoadOrder> a_lo, esp::FormKey a_cell)
	{
		auto open = [this, a_lo, a_cell]() {
			_cellDraft = -1;
			_history.clear();
			OpenCellAsync(a_lo, a_cell, false);
		};
		if (OpenUnsaved()) {
			_pendingOpen = open;
			return;
		}
		SyncCellToDraft();  // a home of the rail keeps its changes in its draft
		open();
	}

	void EditorApp::OpenHome(int a_index, std::optional<esp::FormKey> a_cell, bool a_draft)
	{
		if (!_homes || a_index < 0 || static_cast<std::size_t>(a_index) >= _homes->homes.size()) {
			return;
		}
		if (Busy()) {
			Queue([this, a_index, a_cell, a_draft] { OpenHome(a_index, a_cell, a_draft); });
			return;
		}
		auto open = [this, a_index, a_cell, a_draft]() {
			const auto& h = _homes->homes[static_cast<std::size_t>(a_index)];
			auto&       e = _homes->entries[static_cast<std::size_t>(a_index)];
			// A home with a template opens on the one the game offers first, as a file (nothing to save); its draft
			// stays in the Draft panel. A draft with unsaved changes opens instead (they are not lost).
			std::optional<core::Layout> file;
			std::string                 filePath;
			if (!a_draft && !e.reviewed) {
				if (const auto list = HomeTemplates(h); !list.empty()) {
					filePath = PathToUtf8(list.front()->path);
					core::Diagnostics diags;
					file = core::LoadLayoutFile(list.front()->path, filePath, diags);
					if (!file || !file->cell) {
						file.reset();  // unreadable since the scan: the draft
					}
				}
			}
			if (file) {
				_pendingLayout = std::move(*file);
				_pendingLayoutDirty = false;
				_pendingFilePath = filePath;
			} else {
				auto& d = EnsureDraft(a_index);
				_pendingLayout = d.draft->layout;
				_pendingLayoutDirty = d.reviewed;
				_pendingFilePath.clear();
			}
			_cellDraft = a_index;
			_railCursor = a_index;
			_railReveal = true;
			_railRevealFrames = 0;
			_history.clear();
			OpenCellAsync(_homes->lo, a_cell ? *a_cell : h.cell, false);
		};
		if (OpenUnsaved()) {
			_pendingOpen = open;
			return;
		}
		SyncCellToDraft();
		open();
	}

	void EditorApp::ResolvePendingCell()
	{
		if (!_pendingCell || !_ws) {
			return;
		}
		const auto wanted = *_pendingCell;
		// A home of the rail (its main cell or one of its cells): opened with its draft.
		if (_homes) {
			for (std::size_t i = 0; i < _homes->homes.size(); ++i) {
				const auto& h = _homes->homes[i];
				if (core::IEquals(h.cellRef, wanted) || core::IEquals(h.editorId, wanted)) {
					_pendingCell.reset();
					OpenHome(static_cast<int>(i));
					return;
				}
				for (const auto& x : h.extra) {
					if (core::IEquals(x.cellRef, wanted) || core::IEquals(x.editorId, wanted)) {
						_pendingCell.reset();
						OpenHome(static_cast<int>(i), x.cell);
						return;
					}
				}
			}
		}
		// Else any cell of --mod's plugins (Scan opens it), or of the base game.
		if (_pendingMod) {
			if (const auto found = _ws->FindMods(*_pendingMod); !found.empty()) {
				SelectMod(static_cast<int>(found.front() - _ws->Mods().data()));
				return;
			}
		}
		_pendingCell.reset();
		const Workspace* ws = _ws.get();
		StartJob("Opening cell", [this, ws, wanted]() -> std::function<void()> {
			auto                     lo = std::shared_ptr<esp::LoadOrder>(ws->NewLoadOrder());
			std::vector<std::string> errors;
			for (const char* p : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" }) {
				lo->AddWithMasters(p, errors);
			}
			auto key = lo->ParseFormRef(wanted);
			if (!key) {
				key = lo->FindCellByEditorId(wanted);
			}
			if (!key) {
				return [this, wanted] { SetStatus("Cell not found in the base game: " + wanted, true); };
			}
			return [this, lo, k = *key]() {
				_cellDraft = -1;
				OpenCellAsync(lo, k, false);
			};
		});
	}

	void EditorApp::DrawDiscardPopup()
	{
		if (_pendingOpen && !ImGui::IsPopupOpen("Unsaved template")) {
			ImGui::OpenPopup("Unsaved template");
		}
		if (ImGui::BeginPopupModal("Unsaved template", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted("The template has unsaved changes.");
			w::Muted("Open the other home and drop them?");
			w::Spacer();
			if (ImGui::Button("Drop the changes")) {
				auto open = std::exchange(_pendingOpen, nullptr);
				if (_cell) {
					_cell->dirty = false;
				}
				ImGui::CloseCurrentPopup();
				if (open) {
					open();
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Keep editing") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				_pendingOpen = nullptr;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	bool EditorApp::CellInHome(const OpenCell& a_cell) const
	{
		const auto ref = a_cell.lo->ToFormRef(a_cell.info.key);
		return (a_cell.layout.cell && *a_cell.layout.cell == ref) ||
		       std::ranges::any_of(a_cell.layout.cells, [&](const core::HomeCell& c) { return c.cell == ref; });
	}

	void EditorApp::OpenCellAsync(std::shared_ptr<esp::LoadOrder> a_lo, esp::FormKey a_cell, bool a_remember, bool a_keepTemplate)
	{
		if (Busy()) {
			Queue([this, a_lo, a_cell, a_remember, a_keepTemplate] { OpenCellAsync(a_lo, a_cell, a_remember, a_keepTemplate); });
			return;
		}
		if (a_remember && _cell && !(_cell->info.key == a_cell && _cell->lo == a_lo)) {
			_history.emplace_back(_cell->lo, _cell->info.key);
		}
		LoadCategories();
		const core::CategorySet* categories = _categorySet ? &*_categorySet : nullptr;
		const core::DraftRules*  rules = &_draftRules;
		const bool               needDraft = !_pendingLayout && !a_keepTemplate;  // a home's draft or a file replaces it anyway
		StartJob("Opening cell", [this, a_lo, a_cell, a_keepTemplate, categories, rules, needDraft]() -> std::function<void()> {
			const auto start = Clock::now();
			auto       cell = home::LoadCell(*a_lo, a_cell);
			if (!cell) {
				return [this] { SetStatus("That cell is not in the loaded plugins", true); };
			}
			auto oc = std::make_shared<OpenCell>();
			oc->lo = a_lo;
			oc->info = std::move(*cell);
			oc->context = home::LoadNeighbourCells(*a_lo, oc->info);
			oc->cellText = a_lo->Format(oc->info.key);
			for (const auto& r : oc->info.refs) {
				oc->refText.push_back(a_lo->Format(r.key));
			}
			for (const auto& d : oc->info.doors) {
				oc->doorText.push_back(d.destinationCell ? a_lo->Format(*d.destinationCell) : std::string{});
			}
			if (needDraft) {
				core::Diagnostics diags;
				oc->layout = home::DraftTemplate(*a_lo, oc->info, diags, categories, rules);
			}
			oc->ms = Ms(start);
			return [this, oc, a_keepTemplate]() {
				std::unique_ptr<OpenCell> previous = std::move(_cell);
				_cell = std::make_unique<OpenCell>(std::move(*oc));
				if (a_keepTemplate && previous && previous->lo == _cell->lo) {
					// Same home: the template being made comes along.
					_cell->layout = std::move(previous->layout);
					_cell->dirty = previous->dirty;
					_cell->savedPath = std::move(previous->savedPath);
					_cell->saveMessage = std::move(previous->saveMessage);
					_cell->filePath = std::move(previous->filePath);
				} else if (!_templateAuthor.empty()) {
					_cell->layout.author = _templateAuthor;
				}
				if (_pendingLayout) {
					// A home's draft, or a template file.
					_cell->layout = std::move(*_pendingLayout);
					_cell->dirty = _pendingLayoutDirty;
					_cell->filePath = std::exchange(_pendingFilePath, {});
					_pendingLayout.reset();
				}
				Revalidate(*_cell);
				_localDraft.reset();
				_localDraftCell = nullptr;
				_multi.clear();
				_selectedRef = -1;
				_hoveredRef = -1;
				_fitPending = true;
				_showStart = false;
				if (_cellDraft >= 0) {
					_railReveal = true;  // again: the drawer and the title row have just changed the rail's height
					_railRevealFrames = 0;
				}
				// Height range (shared by the plan and the 3D view).
				_zMin = FLT_MAX;
				_zMax = -FLT_MAX;
				for (const auto& r : _cell->info.refs) {
					if (const auto l = LayerOf(r); l != Layer::Other && l != Layer::Effect) {
						_zMin = std::min(_zMin, r.position[2]);
						_zMax = std::max(_zMax, r.position[2]);
					}
				}
				if (_zMin > _zMax) {
					_zMin = -1000.0f;
					_zMax = 1000.0f;
				}
				_zLo = std::floor(_zMin) - 1.0f;
				_zHi = std::ceil(_zMax) + 1.0f;
				// The roof cut (interiors): per area, never above the highest container + 120 (detail::RoofTop).
				_roofZ = RoofTop(_cell->info);
				_view.roofGrid = render::RoofGrid::Build(RoofFloor(_cell->info), _roofZ, kRoofAboveFloor);
				if (_roofZ < 1e8f) {
					const auto& g = _view.roofGrid;
					const auto [lo, hi] = g.cuts.empty() ? std::pair{ _roofZ, _roofZ } : std::pair{ *std::ranges::min_element(g.cuts), *std::ranges::max_element(g.cuts) };
					Log("roof cut: {} per area ({} x {} squares of {:.0f} units) at z {:.0f} to {:.0f}, above it only containers and displays in 3D{}", _cell->cellText,
						g.nx, g.ny, g.size, lo, hi, _showRoof ? " (off: Layers > Roof is on)" : "");
				}
				if (_pendingSelect) {
					const auto wanted = *std::exchange(_pendingSelect, std::nullopt);
					if (const int i = RefIndexOf(*_cell, wanted); i >= 0) {
						SelectInView(i, true);
					}
				}
				for (const auto& m : std::exchange(_pendingMulti, {})) {
					if (const int i = RefIndexOf(*_cell, m); i >= 0 && !IsSelected(i)) {
						ToggleMulti(i);
					}
				}
				if (_pendingPanel) {
					_panel = *std::exchange(_pendingPanel, std::nullopt);
				}
				StartScene();
			};
		});
	}

	void EditorApp::ApplyWindowSize()
	{
		if (_windowApplied || !_windowSize) {
			return;
		}
		_windowApplied = true;
		auto* hwnd = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
		if (!hwnd) {
			return;
		}
		if (!g_windowProc) {
			g_windowProc = reinterpret_cast<WNDPROC>(::SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&LargeWindowProc)));
		}
		// The client area at the asked size (--window 1280x720): the frame added around it.
		RECT r{ 0, 0, _windowSize->first, _windowSize->second };
		::AdjustWindowRectEx(&r, static_cast<DWORD>(::GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE, static_cast<DWORD>(::GetWindowLongPtrW(hwnd, GWL_EXSTYLE)));
		::SetWindowPos(hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	void EditorApp::SaveLayoutState()
	{
		// Written when a panel's size or state changed and the mouse is up (not every frame of a drag).
		if (_state.ui == _savedUi || ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			return;
		}
		_savedUi = _state.ui;
		_state.uiOnDisk.reset();
		_state.Save();
	}

	void EditorApp::NoHomeMatches(const std::string& a_wanted)
	{
		Log("--home: no home matches {}", a_wanted);
		std::fputs(("error: no home matches " + a_wanted + "\n").c_str(), stderr);  // DovaStashEditor.com passes it on
		std::fflush(stderr);
		_homeMissing = true;
	}

	void EditorApp::Frame()
	{
		_listHoverShown = std::exchange(_listHoveredRef, -1);
		// The start-up file work held back under MO2 (App.h): once the first frames are on screen.
		if (_holdFrames > 0 && --_holdFrames == 0) {
			if (std::exchange(_heldArt, false) && _ws && _device) {
				_assets = std::make_shared<Assets>(_ws->Files().Roots(), Assets::ArchiveList(*_ws));
				LoadArt(_assets, _device);
			}
			if (std::exchange(_heldHomes, false) && _ws) {
				ScanHomes();
			}
		}
		PollJob();
		ApplyWindowSize();
		// --home given more than once: the next home once the open one's 3D view has loaded.
		if (!_homeQueue.empty() && _homes && _cell && !_pendingHome && !Busy() && _queue.empty() && _scene && _scene->Progress().finished) {
			const auto wanted = _homeQueue.front();
			_homeQueue.erase(_homeQueue.begin());
			if (const auto i = home::FindHome(_homes->homes, wanted)) {
				OpenHome(static_cast<int>(*i));
			} else {
				NoHomeMatches(wanted);
			}
		}
		if (_pendingPopover && _homes && !Busy() && _queue.empty() && !_pendingCell && !_pendingHome) {
			const auto id = *std::exchange(_pendingPopover, std::nullopt);  // --popover
			if (id == "savedrafts") {
				_confirmSaveAll = true;  // the *Save N drafts...* dialog (with --drafts: once the bulk draft is done)
			} else if (id == "categories") {
				_openCategoryPicker = true;  // the selection's "+ add" category picker (a combo, not a popover)
			} else if (id == "kind") {
				Table(DrawerTab::Containers).openFilter = 1;  // the Kind column's funnel (ui/Tables.cpp)
			} else {
				w::OpenPopover(id.c_str());
			}
		}
		const auto&          t = w::Th();
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(vp->WorkPos);
		ImGui::SetNextWindowSize(vp->WorkSize);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::Begin("Dova Stash Editor", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
				ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleVar();
		w::ClaimLayoutDoubleClick();  // before any item: a double click on a panel toggle stays on the toggle
		HandleShortcuts();

		const ImVec2 o = ImGui::GetCursorScreenPos();
		const ImVec2 size = ImGui::GetContentRegionAvail();
		const float  headerH = t.toolbarH + 2.0f * t.scale;
		float        drawerH = headerH;
		// The drawer's height is a fraction of the window's, so a smaller window keeps its view; the view keeps half of
		// the room between the title and status rows, and at least 240 px (a small window at a large text size).
		_layoutH = std::max(size.y, 1.0f);
		_drawerMinH = 120.0f * t.scale;
		const float between = size.y - t.titleH - t.statusH;
		_drawerMaxH = std::max(_drawerMinH, std::min(size.y * 0.5f - t.titleH - t.statusH, between - std::max(0.5f * between, 240.0f * t.scale)));
		if (_state.ui.drawerOpen && _cell) {
			if (_state.ui.drawerHeight <= 0.0f) {
				_state.ui.drawerHeight = 1.0f / 3.0f;
				_savedUi.drawerHeight = _state.ui.drawerHeight;  // the default, not a change to write
			} else if (_state.ui.drawerHeight > 1.0f) {
				_state.ui.drawerHeight /= _layoutH;  // pixels, from an older state.json
			}
			drawerH = std::clamp(_state.ui.drawerHeight * _layoutH, _drawerMinH, _drawerMaxH);
		}
		if (!_cell) {
			drawerH = 0.0f;  // nothing to list without a home
		}
		const float middleH = std::max(size.y - t.titleH - t.statusH - drawerH, 50.0f);

		ImGui::SetCursorScreenPos(o);
		DrawTitleRow();
		ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + t.titleH));
		ImGui::BeginChild("middle", ImVec2(size.x, middleH), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		{
			DrawRail();
			ImGui::SameLine(0.0f, 0.0f);
			const float strip = t.stripW;  // BeginRail's collapsed strip
			const float inspW = _state.ui.inspectorOpen ? std::clamp(_state.ui.inspectorWidth, 280.0f * t.scale, InspectorMaxWidth()) : strip;
			DrawViewport(ImVec2(std::max(ImGui::GetContentRegionAvail().x - inspW, 64.0f), middleH));
			ImGui::SameLine(0.0f, 0.0f);
			DrawInspector();
		}
		ImGui::EndChild();
		if (drawerH > 0.0f) {
			ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + t.titleH + middleH));
			ImGui::BeginChild("drawerarea", ImVec2(size.x, drawerH), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			DrawDrawer();
			ImGui::EndChild();
		}
		ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + size.y - t.statusH));
		DrawStatusRow();

		DrawDiscardPopup();
		DrawQuitPopup();
		DrawSavePopup();
		DrawSaveDraftsPopup();
		DrawRescanPopup();
		DrawDraftAgainPopup();
		DrawDeletePopup();
		DrawSetupConfirm();
		DrawHelpPopups();
		DrawPalette();
		ImGui::End();
		TrackUndo();
		SaveLayoutState();
	}

	// ---- Viewport (docs/EDITOR-UX.md §2.3) ----

	void EditorApp::DrawViewport(ImVec2 a_size)
	{
		ImGui::PushStyleColor(ImGuiCol_ChildBg, w::Th().bg0);
		ImGui::BeginChild("viewport", a_size, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleColor();
		const ImVec2 mn = ImGui::GetCursorScreenPos();
		const ImVec2 mx(mn.x + a_size.x, mn.y + a_size.y);
		_viewportMin = mn;
		_viewportMax = mx;
		if (!_ws || !_cell || _showStart) {
			if (_ws && !_cell && Busy() && _jobLabel == "Opening cell") {
				w::CentredMessage(mn, mx, "Opening the home", nullptr);
			} else {
				DrawStart(mn, mx);
			}
		} else {
			auto& c = *_cell;
			UpdateTints(c);
			_view.multi = _multi;
			_view.heightCut = _heightCut;
			_view.zLo = _zLo;
			_view.zHi = _zHi;
			_view.roofZ = _showRoof ? 1e9f : _roofZ;
			if (_state.ui.view == 0) {
				DrawView3D(c, a_size);
			} else {
				DrawPlan(c, a_size);
			}
			DrawOverlay(c, mn, mx);
		}
		ImGui::EndChild();
	}

	namespace
	{
		// What the home keeps between the rail and the inspector: 45 % of the window, and at least 560 px (a small
		// window at a large text size would otherwise give the panels most of it).
		float ViewportFloor(float a_width) { return std::max(0.45f * a_width, 560.0f * w::Th().scale); }
	}

	float EditorApp::RailMaxWidth() const
	{
		const auto& t = w::Th();
		const float W = ImGui::GetMainViewport()->WorkSize.x;
		const float insp = _state.ui.inspectorOpen ? std::clamp(_state.ui.inspectorWidth, 280.0f * t.scale, std::max(280.0f * t.scale, W * 0.45f)) : t.stripW;
		return std::max(200.0f * t.scale, std::min({ 560.0f * t.scale, W * 0.6f - insp, W - ViewportFloor(W) - insp }));
	}

	float EditorApp::InspectorMaxWidth() const
	{
		// From the window and the rail, not from the space left for the inspector (that is its own width and would
		// shrink the saved width every run).
		const auto& t = w::Th();
		const float W = ImGui::GetMainViewport()->WorkSize.x;
		const float rail = _state.ui.railOpen ? std::clamp(_state.ui.railWidth, 200.0f * t.scale, RailMaxWidth()) : t.stripW;
		const float regular = std::max(280.0f * t.scale, std::min(W * 0.45f, W * 0.6f - rail));
		const float room = W - ViewportFloor(W) - rail;  // what the viewport's floor leaves it
		if (room >= regular) {
			return regular;
		}
		// The floor bites: the inspector gives way down to 30 % of the window (never under its 280 px).
		return std::max(280.0f * t.scale, std::min(regular, std::max(room, 0.30f * W)));
	}

	void EditorApp::DrawOverlay(OpenCell& a_cell, ImVec2 a_min, ImVec2 a_max)
	{
		const auto& t = w::Th();
		const bool  threeD = _state.ui.view == 0;
		const bool  narrow = a_max.x - a_min.x < 900.0f * t.scale;  // icons only, their names in the tooltips
		if (w::BeginOverlay("toolbar", ImVec2(t.pad, t.pad))) {
			if (narrow ? w::IconButton("Select", w::Icon::Cursor, "Select: click picks, Ctrl+click adds, drag boxes", "S", !_paint && _selectMode)
			           : w::ToolButton("Select", !_paint && _selectMode, "Select: click picks, Ctrl+click adds, drag boxes", "S")) {
				SetPaint(false);
				_selectMode = true;
			}
			ImGui::SameLine(0.0f, 2.0f * t.scale);
			if (narrow ? w::IconButton("Paint", w::Icon::Brush, "Paint: a category onto containers", "P", _paint) : w::ToolButton("Paint", _paint, "Paint a category onto containers", "P")) {
				SetPaint(!_paint);
			}
			ImGui::SameLine(0.0f, t.gap);
			{
				// A thin divider between the mode pair and the rest.
				const ImVec2 p = ImGui::GetCursorScreenPos();
				const float  h = ImGui::GetFrameHeight();
				ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + 4.0f * t.scale), ImVec2(p.x, p.y + h - 4.0f * t.scale), t.line);
				ImGui::Dummy(ImVec2(1.0f, h));
			}
			ImGui::SameLine(0.0f, t.gap);
			if (_paint) {
				DrawPaintBar(a_cell);
			} else {
				if (w::BeginPopover("layers", narrow ? "" : "Layers", w::Icon::Layers, 0, narrow ? "Layers: what is drawn" : "What is drawn", "L")) {
					DrawLayersPopover(a_cell);
					w::EndPopover();
				}
				ImGui::SameLine(0.0f, 2.0f * t.scale);
				if (w::BeginPopover("colours", narrow ? "" : "Colours", w::Icon::Palette, 0, narrow ? "Colours: colour the containers" : "Colour the containers", "C / V")) {
					DrawColourLegend(a_cell);
					w::EndPopover();
				}
				ImGui::SameLine(0.0f, 2.0f * t.scale);
				if (w::BeginPopover("camera", narrow ? "" : "Camera", w::Icon::Camera, 0,
						narrow ? (threeD ? "Camera: overview, frame, speed, light" : "Camera: fit, centre") : (threeD ? "Overview, frame, speed, light" : "Fit, centre"), "Home / F")) {
					DrawCameraPopover(a_cell);
					w::EndPopover();
				}
			}
			w::EndOverlay();
		}
		// The hint line: what the mouse does now.
		std::string hint;
		if (_paint) {
			hint = !_paintMessage.empty() ? _paintMessage
			       : _paintCategory.empty() ? std::string("Pick a category, then click containers")
			                                : std::format("Click: give {} · again: take it away · P: done · Right mouse: look", _paintCategory);
		} else if (_boxing) {
			hint = "Release: select what is inside · Ctrl: add";
		} else if (threeD) {
			hint = _selectMode ? "Right mouse: look · WASD: fly · Wheel: forward · Middle: pan · Alt+drag: orbit · Drag: box · F: frame · Home: overview"
			                   : "Picks anything (walls, clutter) · Right mouse: look · WASD: fly · S: select mode";
		} else {
			hint = _selectMode ? "Click: select · Ctrl+click: add · Drag: box · Right drag: pan · Wheel: zoom · Home: fit"
			                   : "Drag: pan · Wheel: zoom · S: select mode";
		}
		w::HintLine(hint.c_str(), a_min, a_max);
	}

	void EditorApp::DrawLayersPopover(OpenCell& a_cell)
	{
		w::SectionHead("Layers");
		w::Check("Structure", _view.showStructure);
		w::Check("Containers", _view.showContainers);
		w::Check("Displays", _view.showDisplays);
		w::Check("Doors", _view.showDoors);
		w::Check("Furniture", _view.showFurniture);
		w::Check("People", _view.showActors, "NPCs placed in the cell (mannequins are displays)");
		w::Check("Disabled", _view.showDisabled, "Initially disabled references: house upgrades, things a quest adds later");
		if (_state.ui.view == 0) {
			w::Check("Effects", _view.showEffects, "Glows, flames, fog, mist and light rays (3D view; the plan has its own)");
		} else {
			w::Check("Effects", _planEffects, "Fog, mist, glows, light rays and lights: their volumes (plan; the 3D view has its own)");
		}
		w::Check("Other", _showMarkers, "Markers, trigger volumes, room bounds, references without a mesh (plan)");
		if (_state.ui.view == 0) {
			w::Check("Missing meshes", _view.showMissing, "Walls and clutter whose mesh is not installed: see-through boxes (containers and displays stay solid). The status row's N missing turns it on");
		}
		if (!a_cell.info.interior) {
			w::Check(std::format("Terrain (+{} cells around)", a_cell.context.size()).c_str(), _view.showTerrain);
		} else if (_state.ui.view == 0 && _roofZ < 1e9f) {
			w::Check("Roof", _showRoof, "Off: about 3 m above each area's floor only containers and displays are drawn, so rooms and caves show from above");
		}
		w::SectionHead("Height cut");
		w::Check("Only between", _heightCut, "Only references between these heights, in both views: one floor of a tower");
		ImGui::BeginDisabled(!_heightCut);
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
		ImGui::DragFloatRange2("##height", &_zLo, &_zHi, 4.0f, std::floor(_zMin) - 1.0f, std::ceil(_zMax) + 1.0f, "%.0f", "%.0f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::EndDisabled();
	}

	void EditorApp::DrawCameraPopover(OpenCell& a_cell)
	{
		(void)a_cell;
		const bool threeD = _state.ui.view == 0;
		if (w::MenuRow(threeD ? "Overview" : "Fit", "Home")) {
			Overview();
		}
		if (w::MenuRow(threeD ? "Frame selection" : "Centre on selection", "F", _selectedRef >= 0)) {
			FrameSelected();
		}
		if (threeD) {
			w::SectionHead("Camera");
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 13.0f);
			w::Slider("Fly speed", _flySpeed, 50.0f, 8000.0f, "%.0f", true, "Units per second (Shift: 4x, Alt: 1/4; the wheel with the right button held)");
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 13.0f);
			w::Slider("Light", _view.brightness, 0.4f, 2.5f, "%.1f", false, "The editor's light (the game's own lights are not used)");
			w::Check("Outlines", _view.showMarkers, "A box around everything select mode can pick, in its plan colour");
		}
		bool anything = !_selectMode;
		if (w::Check(threeD ? "Pick anything" : "Left drag pans", anything, threeD ? "The left button picks walls and clutter too  S" : "Left drag moves the plan instead of box selecting  S")) {
			_selectMode = !anything;
		}
		if (threeD && _scene) {
			const auto p = _scene->Progress();
			w::Spacer();
			w::Small(std::format("{} meshes · {} textures · {} draws · {:.0f} fps", p.modelsTotal, p.texturesTotal, _scene->DrawCalls(), ImGui::GetIO().Framerate).c_str());
		}
	}

	void EditorApp::Overview()
	{
		if (_state.ui.view == 0) {
			_cameraPending = true;
		} else {
			_fitPending = true;
		}
	}

	void EditorApp::FrameSelected()
	{
		if (_selectedRef < 0 || !_cell) {
			return;
		}
		_frameSelection = true;
		CentrePlanOn(*_cell, _selectedRef);
	}

	// ---- Keyboard (docs/EDITOR-UX.md §4) ----

	void EditorApp::FindAHome()
	{
		_state.ui.railOpen = true;
		_railFocusSearch = true;
		_showStart = false;
	}

	void EditorApp::HandleShortcuts()
	{
		const ImGuiIO& io = ImGui::GetIO();
		if (_paletteOpen || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
			return;  // the palette and the popovers take the keys (Esc closes them)
		}
		if (w::Shortcut(ImGuiKey_K, true)) {
			_paletteOpen = true;
			_paletteFocus = true;
			_palette.clear();
			_paletteSel = 0;
			return;
		}
		if (w::Shortcut(ImGuiKey_1, true)) {
			_state.ui.railOpen = !_state.ui.railOpen;
		}
		if (w::Shortcut(ImGuiKey_2, true)) {
			_state.ui.drawerOpen = !_state.ui.drawerOpen;
		}
		if (w::Shortcut(ImGuiKey_3, true)) {
			_state.ui.inspectorOpen = !_state.ui.inspectorOpen;
		}
		if (w::Shortcut(ImGuiKey_S, true)) {
			RequestSave();
		}
		if (w::Shortcut(ImGuiKey_O, true)) {
			OpenTemplateFile();
		}
		if (w::Shortcut(ImGuiKey_D, true)) {
			StartBulkDraft();
		}
		if (w::Shortcut(ImGuiKey_F, true)) {
			FindAHome();  // Ctrl+F; the plain F below frames the selection
		}
		if (w::Shortcut(ImGuiKey_Equal, true) || w::Shortcut(ImGuiKey_Equal, true, true) || w::Shortcut(ImGuiKey_KeypadAdd, true)) {
			StepTextScale(1);  // Ctrl+= and Ctrl++ (Shift on the same key)
		}
		if (w::Shortcut(ImGuiKey_Minus, true) || w::Shortcut(ImGuiKey_KeypadSubtract, true)) {
			StepTextScale(-1);
		}
		if (w::Shortcut(ImGuiKey_0, true) || w::Shortcut(ImGuiKey_Keypad0, true)) {
			SetTextScale(0.0f);
		}
		if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, true)) {
			io.KeyShift ? Redo() : Undo();
		}
		if (!io.WantTextInput && io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Y, true)) {
			Redo();
		}
		if (w::Shortcut(ImGuiKey_F5)) {
			Rescan();
		}
		if (io.WantTextInput || ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
			return;  // typing, or flying in the 3D view (WASD)
		}
		if (w::Shortcut(ImGuiKey_1) || w::Shortcut(ImGuiKey_Keypad1)) {
			_state.ui.view = 0;
		}
		if (w::Shortcut(ImGuiKey_2) || w::Shortcut(ImGuiKey_Keypad2)) {
			_state.ui.view = 1;
		}
		if (w::Shortcut(ImGuiKey_Escape)) {
			if (!Selection().empty()) {
				Select(-1);
			} else if (_paint) {
				SetPaint(false);
			} else if (_showStart && _cell) {
				_showStart = false;
			}
		}
		if (!_cell || _showStart) {
			return;
		}
		if (w::Shortcut(ImGuiKey_S)) {
			if (_paint) {
				SetPaint(false);
				_selectMode = true;
			} else {
				_selectMode = !_selectMode;  // pick anything (3D) / drag pans (plan)
			}
		}
		if (w::Shortcut(ImGuiKey_P)) {
			SetPaint(!_paint);
		}
		if (w::Shortcut(ImGuiKey_C)) {
			_colours = _colours == Colours::Category ? Colours::None : Colours::Category;
		}
		if (w::Shortcut(ImGuiKey_V)) {
			_colours = _colours == Colours::Fill ? Colours::None : Colours::Fill;
		}
		if (w::Shortcut(ImGuiKey_F)) {
			FrameSelected();
		}
		if (w::Shortcut(ImGuiKey_Home)) {
			Overview();
		}
		if (w::Shortcut(ImGuiKey_L) && !_paint) {
			w::OpenPopover("layers");
		}
		if (w::Shortcut(ImGuiKey_Delete)) {
			RequestDelete();
		}
		if (w::Shortcut(ImGuiKey_R) && _state.ui.drawerOpen && _drawerTab == DrawerTab::Preview) {
			RunPreview(*_cell);
		}
	}
}
