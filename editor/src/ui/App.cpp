// The workspace (docs/EDITOR-UX.md §2): one Frame() draws the title row, the homes rail, the viewport with its
// floating toolbar, the inspector, the drawer and the status row. This file owns the workspace, the background
// jobs, opening cells and homes, the viewport and the keyboard (§4).

#include "ui/App.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <numbers>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ShObjIdl.h>

#include <imgui.h>

#include "Util.h"
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
		_state.Load();
		_templateAuthor = _state.author;
		const auto& t = w::Th();
		if (_state.ui.railWidth <= 0.0f) {
			_state.ui.railWidth = 280.0f * t.scale;  // docs/EDITOR-UX.md §2.2
		}
		if (_state.ui.inspectorWidth <= 0.0f) {
			_state.ui.inspectorWidth = 340.0f * t.scale;
		}
		// The instance remembered from last time, when none is given and none is found (asked once, ui/State).
		if (!_options.mo2 && !_options.data && !mo2::DetectInstance() && _state.mo2) {
			_options.mo2 = PathFromUtf8(*_state.mo2);
			if (!_options.profile && _state.profile) {
				_options.profile = _state.profile;
			}
		}
		if (_options.mo2) {
			std::snprintf(_mo2Path, sizeof(_mo2Path), "%s", PathToUtf8(*_options.mo2).c_str());
		}
		// Before the command line's overrides: opening saves the state (the instance), and they are for this run.
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
		// The command line's view and drawer are for this run: the file keeps the remembered layout until the author
		// changes a panel (SaveLayoutState).
		if (!(remembered == _state.ui)) {
			_state.uiOnDisk = remembered;
		}
		_savedUi = _state.ui;
		if (_ws) {
			ScanHomes();
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
		if (!_homes || _job.valid() || !_queue.empty() || _pendingCell || _pendingHome || _pendingDrafts) {
			return false;
		}
		if (!_cell || _showStart) {
			return !ArtPending();  // the start panel's art
		}
		return _state.ui.view != 0 || !_scene || _scene->Progress().finished;
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
		_scene->Start();
		_cameraPending = true;
	}

	void EditorApp::OpenWorkspace()
	{
		_ws = std::make_unique<Workspace>();
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
			_ws.reset();
		} else if (_ws->Instance()) {
			std::snprintf(_mo2Path, sizeof(_mo2Path), "%s", PathToUtf8(_ws->Instance()->root).c_str());
			// Remembered for the next start (asked once).
			_state.mo2 = PathToUtf8(_ws->Instance()->root);
			_state.profile = _ws->Profile();
			_state.Save();
		}
		// The game-read art (the start panel's backdrop): the same Assets the 3D view uses.
		ReleaseArt();
		if (_ws && _device) {
			_assets = std::make_shared<Assets>(_ws->Files().Roots(), Assets::ArchiveList(*_ws));
			LoadArt(_assets, _device);
		}
	}

	void EditorApp::StartJob(std::string a_label, std::function<std::function<void()>()> a_work)
	{
		_jobLabel = std::move(a_label);
		_job = std::async(std::launch::async, [work = std::move(a_work)]() -> std::function<void()> {
			try {
				return work();
			} catch (const std::exception& e) {
				std::string what = e.what();
				return [what]() { ::MessageBoxA(nullptr, what.c_str(), "Dova Stash Editor", MB_ICONERROR); };
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
		if (_cell && _cell->dirty && _cellDraft < 0) {
			_pendingOpen = open;
			return;
		}
		SyncCellToDraft();  // a home of the rail keeps its changes in its draft
		open();
	}

	void EditorApp::OpenHome(int a_index, std::optional<esp::FormKey> a_cell)
	{
		if (!_homes || a_index < 0 || static_cast<std::size_t>(a_index) >= _homes->homes.size()) {
			return;
		}
		if (Busy()) {
			Queue([this, a_index, a_cell] { OpenHome(a_index, a_cell); });
			return;
		}
		auto open = [this, a_index, a_cell]() {
			auto&       e = EnsureDraft(a_index);
			const auto& h = _homes->homes[static_cast<std::size_t>(a_index)];
			_pendingLayout = e.draft->layout;
			_pendingLayoutDirty = e.reviewed;
			_pendingFilePath.clear();
			_cellDraft = a_index;
			_railCursor = a_index;
			_railReveal = true;
			_history.clear();
			OpenCellAsync(_homes->lo, a_cell ? *a_cell : h.cell, false);
		};
		if (_cell && _cell->dirty && _cellDraft < 0) {
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
				// Height range (shared by the plan and the 3D view).
				_zMin = FLT_MAX;
				_zMax = -FLT_MAX;
				for (const auto& r : _cell->info.refs) {
					if (LayerOf(r) != Layer::Other) {
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

	void EditorApp::Frame()
	{
		PollJob();
		ApplyWindowSize();
		if (_pendingPopover && _homes && !Busy() && _queue.empty() && !_pendingCell && !_pendingHome) {
			const auto id = *std::exchange(_pendingPopover, std::nullopt);  // --popover
			w::OpenPopover(id.c_str());
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
		HandleShortcuts();

		const ImVec2 o = ImGui::GetCursorScreenPos();
		const ImVec2 size = ImGui::GetContentRegionAvail();
		const float  headerH = t.toolbarH + 2.0f * t.scale;
		float        drawerH = headerH;
		if (_state.ui.drawerOpen && _cell) {
			if (_state.ui.drawerHeight <= 0.0f) {
				_state.ui.drawerHeight = size.y / 3.0f;
				_savedUi.drawerHeight = _state.ui.drawerHeight;  // the default, not a change to write
			}
			drawerH = std::clamp(_state.ui.drawerHeight, 120.0f * t.scale, std::max(120.0f * t.scale, size.y * 0.6f));
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
			const float strip = 28.0f * t.scale;
			const float inspW = _state.ui.inspectorOpen ? std::clamp(_state.ui.inspectorWidth, 280.0f * t.scale, std::max(280.0f * t.scale, size.x * 0.45f)) : strip;
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
			if (_state.ui.view == 0) {
				DrawView3D(c, a_size);
			} else {
				DrawPlan(c, a_size);
			}
			DrawOverlay(c, mn, mx);
		}
		ImGui::EndChild();
	}

	void EditorApp::DrawOverlay(OpenCell& a_cell, ImVec2 a_min, ImVec2 a_max)
	{
		const auto& t = w::Th();
		const bool  threeD = _state.ui.view == 0;
		if (w::BeginOverlay("toolbar", ImVec2(t.pad, t.pad))) {
			if (w::ToolButton("Select", !_paint && _selectMode, "Select: click picks, Ctrl+click adds, drag boxes", "S")) {
				SetPaint(false);
				_selectMode = true;
			}
			ImGui::SameLine(0.0f, 2.0f * t.scale);
			if (w::ToolButton("Paint", _paint, "Paint a category onto containers", "P")) {
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
				if (w::BeginPopover("layers", "Layers", w::Icon::Layers, 0, "What is drawn", "L")) {
					DrawLayersPopover(a_cell);
					w::EndPopover();
				}
				ImGui::SameLine(0.0f, 2.0f * t.scale);
				if (w::BeginPopover("colours", "Colours", w::Icon::Palette, 0, "Colour the containers", "C / V")) {
					DrawColourLegend(a_cell);
					w::EndPopover();
				}
				ImGui::SameLine(0.0f, 2.0f * t.scale);
				if (w::BeginPopover("camera", "Camera", w::Icon::Camera, 0, threeD ? "Overview, frame, speed, light" : "Fit, centre", "Home / F")) {
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
			hint = _selectMode ? "Right mouse: look · WASD fly · Wheel: forward · Middle: pan · Alt+drag: orbit · Drag: box · F frame · Home overview"
			                   : "Picks anything (walls, clutter) · Right mouse: look · WASD fly · S: select mode";
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
		w::Check("People", _view.showActors, "Actors (3D view)");
		w::Check("Disabled", _view.showDisabled, "Initially disabled references: house upgrades, things a quest adds later (3D view)");
		w::Check("Effects", _view.showEffects, "Glows, flames, fog and light rays (3D view)");
		w::Check("Other", _showMarkers, "Markers and references without bounds (plan)");
		if (!a_cell.info.interior) {
			w::Check(std::format("Terrain (+{} cells around)", a_cell.context.size()).c_str(), _view.showTerrain);
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
		if (w::Shortcut(ImGuiKey_Equal, true) || w::Shortcut(ImGuiKey_KeypadAdd, true)) {
			StepTextScale(1);
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
