#include "ui/App.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <numbers>
#include <unordered_map>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ShObjIdl.h>

#include <imgui.h>

#include "Util.h"
#include "data/Assets.h"
#include "dova/core/Layout.h"

namespace dova::editor::ui
{
	using esp::MakeSig;

	namespace
	{
		using Clock = std::chrono::steady_clock;
		long long Ms(Clock::time_point a_start)
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - a_start).count();
		}

		enum class Layer
		{
			Static,
			Container,
			Display,
			Door,
			Furniture,
			Other
		};

		Layer LayerOf(const home::PlacedRef& a_ref)
		{
			if (a_ref.display) {
				return Layer::Display;
			}
			if (a_ref.IsContainer()) {
				return Layer::Container;
			}
			switch (a_ref.baseType) {
			case MakeSig("DOOR"):
				return Layer::Door;
			case MakeSig("FURN"):
			case MakeSig("ACTI"):
				return Layer::Furniture;
			case MakeSig("STAT"):
			case MakeSig("MSTT"):
				return a_ref.hasBounds && (a_ref.bounds[3] - a_ref.bounds[0]) > 0 ? Layer::Static : Layer::Other;  // markers have no bounds
			default:
				return Layer::Other;
			}
		}

		ImU32 ColorOf(const home::PlacedRef& a_ref, Layer a_layer)
		{
			switch (a_layer) {
			case Layer::Container:
				return a_ref.respawns ? IM_COL32(230, 70, 60, 255) : IM_COL32(90, 200, 110, 255);
			case Layer::Display:
				switch (*a_ref.display) {
				case core::DisplayKind::Rack:
					return IM_COL32(80, 190, 230, 255);
				case core::DisplayKind::Mannequin:
					return IM_COL32(220, 110, 220, 255);
				default:
					return IM_COL32(240, 170, 60, 255);
				}
			case Layer::Door:
				return IM_COL32(240, 220, 80, 255);
			case Layer::Furniture:
				return IM_COL32(140, 150, 220, 200);
			case Layer::Static:
				return IM_COL32(150, 150, 150, 70);
			default:
				return IM_COL32(120, 120, 120, 120);
			}
		}

		std::string RefTitle(const home::PlacedRef& a_ref)
		{
			if (!a_ref.baseName.empty()) {
				return a_ref.baseName;
			}
			if (!a_ref.baseEditorId.empty()) {
				return a_ref.baseEditorId;
			}
			return a_ref.editorId.empty() ? std::string("(unnamed)") : a_ref.editorId;
		}

		std::optional<std::string> PickFolder()
		{
			std::optional<std::string> out;
			IFileOpenDialog*           dlg = nullptr;
			if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) {
				return out;
			}
			DWORD opts = 0;
			dlg->GetOptions(&opts);
			dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
			dlg->SetTitle(L"MO2 instance folder (with ModOrganizer.ini)");
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

		void HelpMarker(const char* a_text)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::BeginItemTooltip()) {
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
				ImGui::TextUnformatted(a_text);
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
		}
	}

	EditorApp::EditorApp(WorkspaceOptions a_options, const GuiOptions& a_gui, ID3D11Device* a_device, ID3D11DeviceContext* a_context) :
		_options(std::move(a_options)),
		_device(a_device),
		_context(a_context)
	{
		::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		_state.Load();
		_templateAuthor = _state.author;
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
		if (a_gui.tab) {
			static constexpr const char* tabs[] = { "3d", "plan", "containers", "displays", "doors", "template", "preview" };
			for (int i = 0; i < 7; ++i) {
				if (core::IEquals(*a_gui.tab, tabs[i])) {
					_requestTab = i;
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
		_pendingHomeSelect = a_gui.home;
		_mode = a_gui.mod || a_gui.cell ? Mode::Browse : a_gui.drafts ? Mode::Drafts : Mode::Welcome;
		_listsHidden = a_gui.cell.has_value();  // a cell asked for by name gets the whole window; picking from the lists keeps them
		OpenWorkspace();
		if (_ws && _mode != Mode::Browse) {
			ScanHomes(_mode == Mode::Drafts);
		}
		bool opened = false;
		if (_ws && a_gui.mod) {
			// --mod: its homes (By mod tab), or with --cell its plugins' cells (Any cell tab), as the --cell command finds them.
			std::snprintf(_modFilter, sizeof(_modFilter), "%s", a_gui.mod->c_str());
			std::snprintf(_homeFilter, sizeof(_homeFilter), "%s", a_gui.mod->c_str());
			_requestListTab = a_gui.cell ? 1 : 0;
			if (const auto found = _ws->FindMods(*a_gui.mod); !found.empty()) {
				_pendingCell = a_gui.cell;
				SelectMod(static_cast<int>(found.front() - _ws->Mods().data()));
				opened = true;
			}
		}
		if (_ws && a_gui.cell && !opened) {
			// --cell without a mod: the base game and its DLCs, as the --cell command loads it.
			const Workspace* ws = _ws.get();
			const auto       wanted = *a_gui.cell;
			StartJob("Opening cell...", [this, ws, wanted]() -> std::function<void()> {
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
					return [wanted] { ::MessageBoxA(nullptr, ("Cell not found in the base game: " + wanted).c_str(), "Dova Stash Editor", MB_ICONWARNING); };
				}
				return [this, lo, k = *key]() { OpenCellAsync(lo, k, false); };
			});
		}
	}

	EditorApp::~EditorApp()
	{
		if (_job.valid()) {
			_job.wait();
		}
		_scene.reset();
		::CoUninitialize();
	}

	bool EditorApp::Idle() const
	{
		if ((_mode == Mode::Drafts || (_mode == Mode::Browse && !_listsHidden && _listTab == 0)) && !_homes && _ws) {
			return false;
		}
		return !_job.valid() && !_pendingCell && (_mode != Mode::Browse || !_scene || _scene->Progress().finished);
	}

	void EditorApp::Select(int a_index, bool a_frame)
	{
		_selectedRef = a_index;
		_multi.clear();
		if (a_frame && a_index >= 0) {
			_frameSelection = true;
		}
	}

	void EditorApp::StartScene()
	{
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
		_preview.reset();
		_catalog.reset();
		_catalogLo.reset();
		if (!_ws->Open(_options, _wsError)) {
			_ws.reset();
		} else if (_ws->Instance()) {
			if (_mo2Path[0] == '\0') {
				std::snprintf(_mo2Path, sizeof(_mo2Path), "%s", PathToUtf8(_ws->Instance()->root).c_str());
			}
			// Remembered for the next start (asked once).
			_state.mo2 = PathToUtf8(_ws->Instance()->root);
			_state.profile = _ws->Profile();
			_state.Save();
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

	void EditorApp::PollJob()
	{
		if (_job.valid() && _job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
			auto apply = _job.get();
			_jobLabel.clear();
			if (apply) {
				apply();
			}
		}
	}

	void EditorApp::SelectMod(int a_index)
	{
		_selectedMod = a_index;
		_scans.clear();
		_scene.reset();
		_cell.reset();
		_history.clear();
		Scan();
	}

	void EditorApp::Scan()
	{
		if (!_ws || _selectedMod < -1) {
			return;
		}
		std::vector<std::string> plugins = _selectedMod == -1 ? _ws->BasePlugins() : _ws->Mods()[static_cast<std::size_t>(_selectedMod)].plugins;
		const Workspace*         ws = _ws.get();
		const bool               exteriors = _exteriors;
		StartJob("Finding player homes...", [this, ws, plugins, exteriors]() -> std::function<void()> {
			auto scans = std::make_shared<std::vector<PluginScan>>();
			for (const auto& plugin : plugins) {
				const auto start = Clock::now();
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
				s.ms = Ms(start);
				scans->push_back(std::move(s));
			}
			return [this, scans]() {
				_scans = std::move(*scans);
				if (!_pendingCell) {
					return;
				}
				const auto wanted = std::exchange(_pendingCell, std::nullopt);
				for (const auto& s : _scans) {
					for (std::size_t i = 0; i < s.homes.size(); ++i) {
						if (core::IEquals(s.homes[i].editorId, *wanted) || core::IEquals(s.cellRefs[i], *wanted)) {
							OpenCellAsync(s.lo, s.homes[i].cell);
							return;
						}
					}
				}
				// Not one of the homes (an exterior, a cellar): any cell of the mod's plugins and their masters.
				for (const auto& s : _scans) {
					if (!s.lo) {
						continue;
					}
					auto key = s.lo->ParseFormRef(*wanted);
					if (!key || !s.lo->FindCell(*key)) {
						key = s.lo->FindCellByEditorId(*wanted);
					}
					if (key) {
						OpenCellAsync(s.lo, *key);
						return;
					}
				}
			};
		});
	}

	void EditorApp::RequestNewHome(std::shared_ptr<esp::LoadOrder> a_lo, esp::FormKey a_cell)
	{
		if (_cell && _cell->dirty) {
			_pendingNewHome = std::pair{ std::move(a_lo), a_cell };
			return;
		}
		OpenCellAsync(std::move(a_lo), a_cell);
	}

	void EditorApp::DrawDiscardPopup()
	{
		if (_pendingNewHome && !ImGui::IsPopupOpen("Unsaved template")) {
			ImGui::OpenPopup("Unsaved template");
		}
		if (ImGui::BeginPopupModal("Unsaved template", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted("The template has changes that are not saved. Open the other home and drop them?");
			if (ImGui::Button("Drop the changes")) {
				auto [lo, key] = *std::exchange(_pendingNewHome, std::nullopt);
				_history.clear();
				OpenCellAsync(lo, key, false);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Keep editing")) {
				_pendingNewHome.reset();
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
		if (a_remember && _cell && !(_cell->info.key == a_cell && _cell->lo == a_lo)) {
			_history.emplace_back(_cell->lo, _cell->info.key);
		}
		LoadCategories();
		const core::CategorySet* categories = _categorySet ? &*_categorySet : nullptr;
		const core::DraftRules*  rules = &_draftRules;
		StartJob("Opening cell...", [this, a_lo, a_cell, a_keepTemplate, categories, rules]() -> std::function<void()> {
			const auto start = Clock::now();
			auto       cell = home::LoadCell(*a_lo, a_cell);
			if (!cell) {
				return [] { ::MessageBoxA(nullptr, "That cell is not in the loaded plugins.", "Dova Stash Editor", MB_ICONWARNING); };
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
			core::Diagnostics diags;
			oc->layout = home::DraftTemplate(*a_lo, oc->info, diags, categories, rules);
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
				} else if (!_templateAuthor.empty()) {
					_cell->layout.author = _templateAuthor;
				}
				if (_pendingLayout) {
					// A draft from the drafts view, or a template file opened from the start page.
					_cell->layout = std::move(*_pendingLayout);
					_cell->dirty = _pendingLayoutDirty;
					_pendingLayout.reset();
				}
				Revalidate(*_cell);
				_multi.clear();
				_selectedRef = -1;
				_hoveredRef = -1;
				_fitPending = true;
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
					const auto wanted = std::exchange(_pendingSelect, std::nullopt);
					for (std::size_t i = 0; i < _cell->refText.size(); ++i) {
						if (core::IEquals(_cell->refText[i], *wanted)) {
							Select(static_cast<int>(i), true);
						}
					}
				}
				for (const auto& m : std::exchange(_pendingMulti, {})) {
					for (std::size_t i = 0; i < _cell->refText.size(); ++i) {
						if (core::IEquals(_cell->refText[i], m) && !IsSelected(static_cast<int>(i))) {
							ToggleMulti(static_cast<int>(i));
						}
					}
				}
				StartScene();
			};
		});
	}

	void EditorApp::Frame()
	{
		PollJob();
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(vp->WorkPos);
		ImGui::SetNextWindowSize(vp->WorkSize);
		ImGui::Begin("Dova Stash Editor", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
		HandleEditShortcuts();
		DrawTopBar();
		DrawDiscardPopup();
		DrawQuitPopup();
		ImGui::Separator();
		if (_mode == Mode::Welcome) {
			DrawWelcome();
		} else if (_mode == Mode::Drafts) {
			DrawDrafts();
		} else if (_cell && _listsHidden) {
			ImGui::BeginChild("cell");
			DrawCell();
			ImGui::EndChild();
		} else if (ImGui::BeginTable("columns", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImGui::GetContentRegionAvail())) {
			ImGui::TableSetupColumn("Homes", ImGuiTableColumnFlags_WidthStretch, 0.36f);
			ImGui::TableSetupColumn("Cell", ImGuiTableColumnFlags_WidthStretch, 0.64f);
			ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetContentRegionAvail().y);
			ImGui::TableNextColumn();
			ImGui::BeginChild("lists");
			DrawLists();
			ImGui::EndChild();
			ImGui::TableNextColumn();
			ImGui::BeginChild("cell");
			DrawCell();
			ImGui::EndChild();
			ImGui::EndTable();
		}
		ImGui::End();
		TrackUndo();
	}

	void EditorApp::DrawTopBar()
	{
		const bool busy = _job.valid();
		// Where you are: the start page, the homes by mod (and the open cell), every home's draft.
		auto modeButton = [&](const char* a_label, Mode a_mode, const char* a_tip) {
			const bool on = _mode == a_mode;
			if (on) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			}
			if (ImGui::Button(a_label) && !on) {
				if (_mode == Mode::Browse) {
					SyncCellToDraft();
				}
				if (a_mode == Mode::Drafts && !_homes) {
					ScanHomes(true);
				} else {
					_mode = a_mode;
				}
			}
			if (on) {
				ImGui::PopStyleColor();
			}
			ImGui::SetItemTooltip("%s", a_tip);
			ImGui::SameLine();
		};
		modeButton("Start", Mode::Welcome, "The start page: your setup, the homes found, what you are working on.");
		modeButton(_cell ? "Home editor" : "Homes by mod", Mode::Browse, "Pick a home under the mod that adds it, open its cells, give its containers their jobs.");
		ImGui::BeginDisabled(!_ws || (busy && !_homes));
		modeButton("Draft all homes (Ctrl+D)", Mode::Drafts, "Every player home of the load order with a draft template to review and save.");
		ImGui::EndDisabled();
		if (AnyUnsaved()) {
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "* unsaved changes");
			ImGui::SetItemTooltip("The open template or reviewed drafts are not saved yet (Ctrl+S in the editor, Save in the drafts view).");
			ImGui::SameLine();
		}
		if (busy) {
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", _jobLabel.c_str());
			ImGui::SameLine();
		}
		ImGui::NewLine();
		ImGui::BeginDisabled(busy);
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 28.0f);
		ImGui::InputTextWithHint("##mo2", "MO2 instance folder (with ModOrganizer.ini)", _mo2Path, sizeof(_mo2Path));
		ImGui::SameLine();
		if (ImGui::Button("Browse...")) {
			if (auto p = PickFolder()) {
				std::snprintf(_mo2Path, sizeof(_mo2Path), "%s", p->c_str());
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Open")) {
			_options.mo2 = _mo2Path[0] ? std::optional(PathFromUtf8(_mo2Path)) : std::nullopt;
			_options.profile.reset();
			OpenWorkspace();
		}
		if (_ws && _ws->Instance() && !_ws->Instance()->profiles.empty()) {
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
			if (ImGui::BeginCombo("Profile", _ws->Profile().c_str())) {
				for (const auto& p : _ws->Instance()->profiles) {
					if (ImGui::Selectable(p.c_str(), p == _ws->Profile())) {
						_options.mo2 = _ws->Instance()->root;
						_options.profile = p;
						OpenWorkspace();
						break;
					}
				}
				ImGui::EndCombo();
			}
		}
		ImGui::EndDisabled();
		if (_ws) {
			ImGui::TextDisabled("%s", _ws->Describe().c_str());
		} else if (!_wsError.empty()) {
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", _wsError.c_str());
		}
	}

	void EditorApp::DrawLists()
	{
		ImGui::SeparatorText("1. Player homes");
		if (!_ws) {
			ImGui::TextWrapped("Open an MO2 instance above, or add the editor to MO2 as an executable and start it from there.");
			return;
		}
		if (ImGui::BeginTabBar("lists")) {
			const int request = std::exchange(_requestListTab, -1);
			if (ImGui::BeginTabItem("By mod", nullptr, request == 0 ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
				_listTab = 0;
				DrawHomeMods();
				ImGui::EndTabItem();
			}
			ImGui::SetItemTooltip("Every player home of your load order, under the mod that adds it.");
			if (ImGui::BeginTabItem("Any cell", nullptr, request == 1 ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
				_listTab = 1;
				ImGui::BeginChild("mods", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.5f), ImGuiChildFlags_ResizeY);
				DrawMods();
				ImGui::EndChild();
				ImGui::BeginChild("cells");
				DrawHomes();
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			ImGui::SetItemTooltip("Any mod, then any cell with containers in its plugins: for a home the finder misses (owned by a faction, an exterior).");
			ImGui::EndTabBar();
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

	void EditorApp::DrawHomeMods()
	{
		const bool busy = _job.valid();
		if (!_homes) {
			if (busy && _progress) {
				const auto  done = _progress->done.load();
				const auto  total = _progress->total.load();
				std::string stage;
				{
					std::scoped_lock lock(_progress->mutex);
					stage = _progress->stage;
				}
				ImGui::TextUnformatted("Finding every player home of the active load order...");
				ImGui::ProgressBar(total ? static_cast<float>(done) / static_cast<float>(total) : 0.0f, ImVec2(-FLT_MIN, 0), stage.c_str());
			} else if (busy) {
				ImGui::TextDisabled("%s", _jobLabel.c_str());
			} else {
				ScanHomes(false);
			}
			return;
		}
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##homefilter", "Filter: home, mod name or Nexus mod id", _homeFilter, sizeof(_homeFilter));

		const std::string filter = _homeFilter;
		auto modName = [&](int a_mod) -> std::string {
			return a_mod >= 0 ? _ws->Mods()[static_cast<std::size_t>(a_mod)].name : a_mod == -1 ? "(Game Data folder: base game, Creation Club)" : "(Plugins no mod provides)";
		};
		// What the filter leaves of each group: all of a matching mod, else its matching homes.
		std::vector<std::vector<int>> shown(_homeGroups.size());
		std::size_t                   homes = 0, groups = 0;
		for (std::size_t g = 0; g < _homeGroups.size(); ++g) {
			const auto&      group = _homeGroups[g];
			const mo2::Mod*  mod = group.mod >= 0 ? &_ws->Mods()[static_cast<std::size_t>(group.mod)] : nullptr;
			const bool       modMatch = filter.empty() || IContains(modName(group.mod), filter) || (mod && mod->nexusId && std::to_string(*mod->nexusId) == filter);
			for (const int i : group.homes) {
				const auto& h = _homes->homes[static_cast<std::size_t>(i)];
				if (modMatch || IContains(h.name, filter) || IContains(h.editorId, filter) || IContains(h.plugin, filter) || IContains(h.cellRef, filter)) {
					shown[g].push_back(i);
				}
			}
			homes += shown[g].size();
			groups += shown[g].empty() ? 0 : 1;
		}
		ImGui::TextDisabled("%zu %s in %zu %s", homes, homes == 1 ? "home" : "homes", groups, groups == 1 ? "mod" : "mods");
		HelpMarker("Every player home of the active load order (as Draft all homes finds them: a player-house location, owned by the player, or "
		           "mannequins and nothing that respawns), under the mod whose plugin adds its cell. Click a home to open it. Not listed? The Any cell "
		           "tab shows every cell with containers of a mod's plugins.");
		ImGui::SameLine();
		ImGui::BeginDisabled(busy);
		if (ImGui::SmallButton("Rescan") || (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F5, false) && !busy)) {
			if (!AnyUnsaved()) {
				_homes.reset();
				_homeGroups.clear();
				ScanHomes(false);
				ImGui::EndDisabled();
				return;
			}
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Read the load order again after changing mods in MO2 (F5). Waits until nothing is unsaved.");

		if (ImGui::BeginTable("homemods", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Home", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Storage", ImGuiTableColumnFlags_WidthStretch, 0.55f);
			ImGui::TableSetupColumn("Template", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5.0f);
			ImGui::TableHeadersRow();
			for (std::size_t g = 0; g < _homeGroups.size(); ++g) {
				if (shown[g].empty()) {
					continue;
				}
				const int       modIndex = _homeGroups[g].mod;
				const mo2::Mod* mod = modIndex >= 0 ? &_ws->Mods()[static_cast<std::size_t>(modIndex)] : nullptr;
				const auto      name = modName(modIndex);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::PushID(modIndex);
				if (!filter.empty()) {
					ImGui::SetNextItemOpen(true);
				}
				const auto modLabel = mod && mod->nexusId ? std::format("{}   (Nexus {})", name, *mod->nexusId) : name;
				const bool open = ImGui::TreeNodeEx(modLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_LabelSpanAllColumns);
				if (ImGui::BeginItemTooltip()) {
					std::string plugins;
					for (const auto& p : mod ? mod->plugins : _ws->BasePlugins()) {
						plugins += (plugins.empty() ? "" : ", ") + p;
					}
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
					ImGui::TextUnformatted(name.c_str());
					ImGui::Text("%zu %s", shown[g].size(), shown[g].size() == 1 ? "home" : "homes");
					if (modIndex != -2) {
						ImGui::Text("Plugins: %s", plugins.c_str());
					}
					ImGui::PopTextWrapPos();
					ImGui::EndTooltip();
				}
				if (open) {
					for (const int i : shown[g]) {
						const auto& h = _homes->homes[static_cast<std::size_t>(i)];
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						ImGui::PushID(i);
						const auto label = h.extra.empty() ? h.name : std::format("{}  (+{})", h.name, h.extra.size());
						const bool selected = _cell && _cell->lo == _homes->lo &&
						                      (_cell->info.key == h.cell || std::ranges::any_of(h.extra, [&](const home::HomeEntry::Extra& e) { return e.cell == _cell->info.key; }));
						ImGui::BeginDisabled(busy);
						if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
							RequestNewHome(_homes->lo, h.cell);
						}
						ImGui::EndDisabled();
						if (ImGui::BeginItemTooltip()) {
							ImGui::Text("%s  %s", h.cellRef.c_str(), h.editorId.c_str());
							for (const auto& e : h.extra) {
								ImGui::Text("+ %s (%s)%s", e.name.empty() ? e.editorId.c_str() : e.name.c_str(), e.cellRef.c_str(), e.likely ? "" : ": storage behind a door");
							}
							std::string why;
							for (const auto& r : h.candidate.reasons) {
								why += (why.empty() ? "" : ", ") + r;
							}
							ImGui::Text("Why: %s", why.empty() ? "only containers" : why.c_str());
							if (!h.lastPlugin.empty() && !core::IEquals(h.lastPlugin, h.plugin)) {
								ImGui::Text("Last edited by %s", h.lastPlugin.c_str());
							}
							ImGui::EndTooltip();
						}
						ImGui::PopID();
						ImGui::TableNextColumn();
						ImGui::Text("%d chests", h.containers);
						if (h.displays > 0) {
							ImGui::SameLine();
							ImGui::TextDisabled("%d displays", h.displays);
						}
						ImGui::TableNextColumn();
						bool       user = false;
						const auto status = HomeStatus(h, &user);
						if (status.empty()) {
							ImGui::TextDisabled("none");
						} else {
							ImGui::TextColored(user ? ImVec4(0.45f, 0.85f, 0.5f, 1.0f) : ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "%s", status.c_str());  // the drafts list's colours
						}
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
	}

	void EditorApp::DrawMods()
	{
		ImGui::SeparatorText("Any mod");
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##filter", "Filter: mod name or Nexus mod id", _modFilter, sizeof(_modFilter));
		ImGui::Checkbox("Enabled only", &_onlyEnabled);
		ImGui::SameLine();
		ImGui::Checkbox("With plugins only", &_onlyWithPlugins);

		const bool busy = _job.valid();
		ImGui::BeginDisabled(busy);
		if (ImGui::BeginTable("modtable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Mod", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Nexus", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.0f);
			ImGui::TableSetupColumn("Plugins", ImGuiTableColumnFlags_WidthStretch, 0.6f);
			ImGui::TableHeadersRow();

			const std::string filter = _modFilter;
			if (filter.empty() || IContains("game data folder", filter)) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (ImGui::Selectable("(Game Data folder: base game, Creation Club)", _selectedMod == -1, ImGuiSelectableFlags_SpanAllColumns)) {
					SelectMod(-1);
				}
				ImGui::TableNextColumn();
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%zu plugins", _ws->BasePlugins().size());
			}
			const auto& mods = _ws->Mods();
			// Highest priority first, as MO2 shows it.
			for (int i = static_cast<int>(mods.size()) - 1; i >= 0; --i) {
				const auto& m = mods[static_cast<std::size_t>(i)];
				if (m.separator || m.unmanaged || (_onlyEnabled && !m.enabled) || (_onlyWithPlugins && m.plugins.empty())) {
					continue;
				}
				if (!filter.empty()) {
					const bool idMatch = m.nexusId && std::to_string(*m.nexusId) == filter;
					if (!idMatch && !IContains(m.name, filter)) {
						continue;
					}
				}
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::PushID(i);
				if (!m.enabled) {
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				}
				if (ImGui::Selectable(m.name.c_str(), _selectedMod == i, ImGuiSelectableFlags_SpanAllColumns)) {
					SelectMod(i);
				}
				if (!m.enabled) {
					ImGui::PopStyleColor();
				}
				ImGui::PopID();
				ImGui::TableNextColumn();
				if (m.nexusId) {
					ImGui::Text("%d", *m.nexusId);
				}
				ImGui::TableNextColumn();
				std::string plugins;
				for (const auto& p : m.plugins) {
					plugins += (plugins.empty() ? "" : ", ") + p;
				}
				ImGui::TextDisabled("%s", plugins.c_str());
			}
			ImGui::EndTable();
		}
		ImGui::EndDisabled();
	}

	void EditorApp::DrawHomes()
	{
		ImGui::SeparatorText("Cells in its plugins");
		if (_selectedMod < -1 || !_ws) {
			ImGui::TextWrapped("Pick a mod above. Its plugins are searched for cells with containers, likely homes first: cells whose location is "
			                   "a player house, owned by the player, that never reset, with containers and displays.");
			return;
		}
		const bool busy = _job.valid();
		ImGui::BeginDisabled(busy);
		if (ImGui::Checkbox("Exterior cells too", &_exteriors)) {
			Scan();
		}
		HelpMarker("Exterior cells the plugin adds or edits that have containers (a balcony, a garden). Doors from a home lead to its exteriors as well (Doors tab).");
		ImGui::SameLine();
		ImGui::Checkbox("Likely homes only", &_likelyOnly);
		HelpMarker("A likely home's location is a player house (LocTypePlayerHouse) or the player owns the cell. Untick to see every cell with containers: "
		           "a home owned by a faction, a cellar without its own location.");
		ImGui::EndDisabled();
		if (busy && _scans.empty()) {
			ImGui::TextDisabled("%s", _jobLabel.c_str());
			return;
		}
		for (std::size_t s = 0; s < _scans.size(); ++s) {
			auto& scan = _scans[s];
			ImGui::PushID(static_cast<int>(s));
			const auto likely = std::ranges::count_if(scan.homes, [](const home::HomeCandidate& h) { return h.Likely(); });
			const auto header = std::format("{} ({} likely homes of {} cells, {} ms)###scan", scan.plugin, likely, scan.homes.size(), scan.ms);
			if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
				for (const auto& e : scan.errors) {
					ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", e.c_str());
				}
				if (scan.homes.empty()) {
					ImGui::TextDisabled("No cells with containers.");
				}
				if (!scan.homes.empty() && ImGui::BeginTable("homes", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
					ImGui::TableSetupColumn("Cell", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 2.5f);
					ImGui::TableSetupColumn("Storage", ImGuiTableColumnFlags_WidthStretch, 0.8f);
					ImGui::TableHeadersRow();
					for (std::size_t i = 0; i < scan.homes.size(); ++i) {
						const auto& h = scan.homes[i];
						if (_likelyOnly && !h.Likely()) {
							continue;
						}
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						ImGui::PushID(static_cast<int>(i));
						if (!h.Likely()) {
							ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
						}
						const auto label = std::format("{}{}", h.name.empty() ? h.editorId : h.name, h.interior ? "" : " (exterior)");
						const bool selected = _cell && _cell->info.key == h.cell && _cell->lo == scan.lo;
						ImGui::BeginDisabled(busy);
						if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns) && scan.lo) {
							RequestNewHome(scan.lo, h.cell);
						}
						ImGui::EndDisabled();
						if (!h.Likely()) {
							ImGui::PopStyleColor();
						}
						if (ImGui::BeginItemTooltip()) {
							ImGui::Text("%s  %s", scan.cellRefs[i].c_str(), h.editorId.c_str());
							std::string why;
							for (const auto& r : h.reasons) {
								why += (why.empty() ? "" : ", ") + r;
							}
							ImGui::Text("Why: %s", why.empty() ? "only containers" : why.c_str());
							if (h.overridesOther) {
								ImGui::TextUnformatted("This plugin edits a cell defined by another plugin.");
							}
							ImGui::EndTooltip();
						}
						ImGui::PopID();
						ImGui::TableNextColumn();
						ImGui::Text("%d", h.score);
						ImGui::TableNextColumn();
						ImGui::Text("%d chests", h.containers);
						if (h.racks + h.mannequins + h.plaques > 0) {
							ImGui::SameLine();
							ImGui::TextDisabled("%d displays", h.racks + h.mannequins + h.plaques);
						}
					}
					ImGui::EndTable();
				}
			}
			ImGui::PopID();
		}
	}

	void EditorApp::DrawCell()
	{
		ImGui::SeparatorText("2. Cell");
		if (!_cell) {
			ImGui::TextWrapped(_job.valid() ? _jobLabel.c_str() : "Open a home from the list.");
			return;
		}
		auto& c = *_cell;
		if (ImGui::SmallButton(_listsHidden ? "> Homes" : "< Hide lists")) {
			_listsHidden = !_listsHidden;
		}
		ImGui::SetItemTooltip(_listsHidden ? "Show the list of homes" : "Give the cell the whole window");
		ImGui::SameLine();
		if (!_history.empty()) {
			ImGui::BeginDisabled(_job.valid());
			if (ImGui::SmallButton("< Back")) {
				auto [lo, key] = _history.back();
				_history.pop_back();
				OpenCellAsync(lo, key, false, true);
			}
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Back to the cell opened before (%zu)", _history.size());
			ImGui::SameLine();
		}
		ImGui::BeginDisabled(_undo.empty());
		if (ImGui::SmallButton("Undo")) {
			Undo();
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Undo the last change to the template (Ctrl+Z), %zu steps", _undo.size());
		ImGui::SameLine();
		ImGui::BeginDisabled(_redo.empty());
		if (ImGui::SmallButton("Redo")) {
			Redo();
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Redo (Ctrl+Y or Ctrl+Shift+Z), %zu steps", _redo.size());
		ImGui::SameLine();
		ImGui::Text("%s", c.info.name.empty() ? c.info.editorId.c_str() : c.info.name.c_str());
		if (c.dirty) {
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "(template not saved)");
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%s  %s  (%s, last edited by %s, %lld ms)", c.cellText.c_str(), c.info.editorId.c_str(),
			c.info.interior ? "interior" : "exterior", c.info.lastOverride.c_str(), c.ms);
		std::string kws;
		for (const auto& k : c.info.locationKeywords) {
			kws += (kws.empty() ? "" : ", ") + k;
		}
		ImGui::TextDisabled("Location: %s [%s]   Owner: %s%s   Encounter zone: %s%s", c.info.location ? c.info.locationLabel.c_str() : "none", kws.c_str(),
			c.info.owner ? c.info.ownerLabel.c_str() : "none", c.info.ownedByPlayer ? " (player)" : "", c.info.encounterZone.empty() ? "none" : c.info.encounterZone.c_str(),
			c.info.neverResets ? " (never resets)" : "");

		if (ImGui::BeginTabBar("celltabs")) {
			const int request = std::exchange(_requestTab, -1);
			auto      flags = [&](int a_tab) { return request == a_tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; };
			if (ImGui::BeginTabItem("3D view", nullptr, flags(0))) {
				DrawView3D(c);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Floor plan", nullptr, flags(1))) {
				DrawPlan(c);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Containers", nullptr, flags(2))) {
				DrawContainers(c);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Displays", nullptr, flags(3))) {
				DrawDisplays(c);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Doors", nullptr, flags(4))) {
				DrawDoors(c);
				ImGui::EndTabItem();
			}
			const auto templateLabel = std::format("Template{}###template", c.dirty ? " *" : "");
			if (ImGui::BeginTabItem(templateLabel.c_str(), nullptr, flags(5))) {
				DrawTemplate(c);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Where items go", nullptr, flags(6))) {
				DrawPreview(c);
				ImGui::EndTabItem();
			}
			ImGui::SetItemTooltip("Run Dova Stash's planner over a sample of items: what each container would receive.");
			ImGui::EndTabBar();
		}
	}

	void EditorApp::FitPlan(const OpenCell& a_cell)
	{
		float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
		_zMin = FLT_MAX;
		_zMax = -FLT_MAX;
		for (const auto& r : a_cell.info.refs) {
			const auto layer = LayerOf(r);
			if (layer == Layer::Other) {
				continue;
			}
			minX = std::min(minX, r.position[0]);
			maxX = std::max(maxX, r.position[0]);
			minY = std::min(minY, r.position[1]);
			maxY = std::max(maxY, r.position[1]);
			_zMin = std::min(_zMin, r.position[2]);
			_zMax = std::max(_zMax, r.position[2]);
		}
		if (minX > maxX) {
			minX = minY = -1000.0f;
			maxX = maxY = 1000.0f;
			_zMin = -1000.0f;
			_zMax = 1000.0f;
		}
		_centerX = (minX + maxX) * 0.5f;
		_centerY = (minY + maxY) * 0.5f;
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const float  w = std::max(maxX - minX, 200.0f);
		const float  h = std::max(maxY - minY, 200.0f);
		_zoom = std::min(avail.x * 0.62f / w, std::max(avail.y - 80.0f, 100.0f) * 0.9f / h);
		_panX = _panY = 0.0f;
		_zLo = std::floor(_zMin) - 1.0f;
		_zHi = std::ceil(_zMax) + 1.0f;
	}

	void EditorApp::DrawPlan(OpenCell& a_cell)
	{
		if (_fitPending) {
			FitPlan(a_cell);
			_fitPending = false;
		}
		// Toolbar
		ImGui::Checkbox("Select mode (S)", &_selectMode);
		HelpMarker("On: click a container, display, door or piece of furniture to select it. Off: drag with the left button to move the plan. "
		           "The right or middle button always moves it, the wheel zooms.");
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
			_selectMode = !_selectMode;
		}
		ImGui::SameLine();
		if (ImGui::Button("Fit")) {
			FitPlan(a_cell);
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
		ImGui::DragFloatRange2("Height", &_zLo, &_zHi, 4.0f, std::floor(_zMin) - 1.0f, std::ceil(_zMax) + 1.0f, "%.0f", "%.0f", ImGuiSliderFlags_AlwaysClamp);
		HelpMarker("Only references between these heights are drawn: narrow it to see one floor of a tower.");
		ImGui::Checkbox("Structure", &_showStatics);
		ImGui::SameLine();
		ImGui::Checkbox("Containers", &_showContainers);
		ImGui::SameLine();
		ImGui::Checkbox("Displays", &_showDisplays);
		ImGui::SameLine();
		ImGui::Checkbox("Doors", &_showDoors);
		ImGui::SameLine();
		ImGui::Checkbox("Furniture", &_showFurniture);
		ImGui::SameLine();
		ImGui::Checkbox("Other", &_showOther);
		ImGui::SameLine();
		DrawPaintBar(a_cell);
		DrawColourLegend(a_cell);
		UpdateTints(a_cell);

		const float detailsWidth = ImGui::GetFontSize() * 19.0f;
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const ImVec2 canvasSize(std::max(avail.x - detailsWidth - ImGui::GetStyle().ItemSpacing.x, 100.0f), std::max(avail.y, 100.0f));
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("plan", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		const bool    hovered = ImGui::IsItemHovered();
		ImGuiIO&      io = ImGui::GetIO();
		const ImVec2  center(origin.x + canvasSize.x * 0.5f, origin.y + canvasSize.y * 0.5f);

		// Pan / zoom
		if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) ||
										 (!_selectMode && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)))) {
			_panX += io.MouseDelta.x;
			_panY += io.MouseDelta.y;
		}
		if (hovered && io.MouseWheel != 0.0f) {
			const float dx = (io.MousePos.x - center.x - _panX) / _zoom;
			const float dy = -(io.MousePos.y - center.y - _panY) / _zoom;
			_zoom = std::clamp(_zoom * std::pow(1.2f, io.MouseWheel), 0.005f, 50.0f);
			_panX = io.MousePos.x - center.x - dx * _zoom;
			_panY = io.MousePos.y - center.y + dy * _zoom;
		}
		auto toScreen = [&](float a_x, float a_y) {
			return ImVec2(center.x + (a_x - _centerX) * _zoom + _panX, center.y - (a_y - _centerY) * _zoom + _panY);
		};

		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(origin, ImVec2(origin.x + canvasSize.x, origin.y + canvasSize.y), IM_COL32(22, 22, 26, 255));
		dl->PushClipRect(origin, ImVec2(origin.x + canvasSize.x, origin.y + canvasSize.y), true);

		struct Hit
		{
			int    index;
			ImVec2 pts[4];
			bool   poly;
			ImVec2 at;
			int    priority;
			float  area;
		};
		std::vector<Hit> hits;
		_hoveredRef = -1;

		// Draw in layer order: structure below, what matters on top.
		const Layer order[] = { Layer::Static, Layer::Other, Layer::Furniture, Layer::Door, Layer::Container, Layer::Display };
		for (const auto layer : order) {
			const bool show = (layer == Layer::Static && _showStatics) || (layer == Layer::Container && _showContainers) ||
			                  (layer == Layer::Display && _showDisplays) || (layer == Layer::Door && _showDoors) ||
			                  (layer == Layer::Furniture && _showFurniture) || (layer == Layer::Other && _showOther);
			if (!show) {
				continue;
			}
			for (std::size_t i = 0; i < a_cell.info.refs.size(); ++i) {
				const auto& r = a_cell.info.refs[i];
				if (LayerOf(r) != layer || r.position[2] < _zLo || r.position[2] > _zHi) {
					continue;
				}
				ImU32 col = ColorOf(r, layer);
				if (_colours != Colours::None && (layer == Layer::Container || layer == Layer::Display)) {
					// Colour by category or fill; what has no colour fades so the coloured ones stand out.
					const auto t = i < _view.tint.size() ? _view.tint[i] : 0u;
					col = t ? t : (col & 0x00FFFFFF) | 0x50000000;
				}
				const ImVec2 at = toScreen(r.position[0], r.position[1]);
				Hit          hit{ static_cast<int>(i), {}, false, at, static_cast<int>(layer), 0.0f };
				const float  w = static_cast<float>(r.bounds[3] - r.bounds[0]) * r.scale;
				const float  h = static_cast<float>(r.bounds[4] - r.bounds[1]) * r.scale;
				if (r.hasBounds && w > 0.0f && h > 0.0f) {
					// Footprint: the base's bounds turned by the reference's Z rotation (clockwise seen from above).
					const float c = std::cos(r.rotation[2]);
					const float s = std::sin(r.rotation[2]);
					const float xs[4] = { r.bounds[0] * r.scale, r.bounds[3] * r.scale, r.bounds[3] * r.scale, r.bounds[0] * r.scale };
					const float ys[4] = { r.bounds[1] * r.scale, r.bounds[1] * r.scale, r.bounds[4] * r.scale, r.bounds[4] * r.scale };
					for (int k = 0; k < 4; ++k) {
						hit.pts[k] = toScreen(r.position[0] + xs[k] * c + ys[k] * s, r.position[1] - xs[k] * s + ys[k] * c);
					}
					hit.poly = true;
					hit.area = w * h;
					if (layer == Layer::Static || layer == Layer::Furniture || layer == Layer::Other) {
						dl->AddPolyline(hit.pts, 4, col, ImDrawFlags_Closed, 1.0f);
					} else {
						dl->AddConvexPolyFilled(hit.pts, 4, (col & 0x00FFFFFF) | 0x90000000);
						dl->AddPolyline(hit.pts, 4, col, ImDrawFlags_Closed, 1.5f);
					}
				} else {
					dl->AddCircleFilled(at, layer == Layer::Other ? 2.0f : 4.0f, col);
				}
				if (layer != Layer::Static && layer != Layer::Other) {
					dl->AddCircleFilled(at, 2.5f, col);
				}
				hits.push_back(hit);
			}
		}

		// Hover / select: the most important layer under the cursor, then the smallest footprint.
		const float clickDx = io.MousePos.x - io.MouseClickedPos[ImGuiMouseButton_Left].x;
		const float clickDy = io.MousePos.y - io.MouseClickedPos[ImGuiMouseButton_Left].y;
		const bool  clicked = ImGui::IsMouseReleased(ImGuiMouseButton_Left) && clickDx * clickDx + clickDy * clickDy <= io.MouseDragThreshold * io.MouseDragThreshold;
		if (hovered) {
			const ImVec2 m = io.MousePos;
			const Hit*   best = nullptr;
			for (const auto& h : hits) {
				bool inside = false;
				if (h.poly) {
					float sign = 0.0f;
					inside = true;
					for (int k = 0; k < 4 && inside; ++k) {
						const ImVec2& a = h.pts[k];
						const ImVec2& b = h.pts[(k + 1) % 4];
						const float   cross = (b.x - a.x) * (m.y - a.y) - (b.y - a.y) * (m.x - a.x);
						if (cross != 0.0f) {
							if (sign == 0.0f) {
								sign = cross;
							} else if ((cross > 0.0f) != (sign > 0.0f)) {
								inside = false;
							}
						}
					}
				}
				if (!inside) {
					const float dx = m.x - h.at.x, dy = m.y - h.at.y;
					inside = dx * dx + dy * dy <= 36.0f;
				}
				if (!inside) {
					continue;
				}
				if (!best || h.priority > best->priority || (h.priority == best->priority && h.area < best->area)) {
					best = &h;
				}
			}
			if (best) {
				_hoveredRef = best->index;
				const auto& r = a_cell.info.refs[static_cast<std::size_t>(best->index)];
				if (best->poly) {
					dl->AddPolyline(best->pts, 4, IM_COL32(255, 255, 255, 200), ImDrawFlags_Closed, 2.0f);
				}
				ImGui::SetTooltip("%s\n%s  %s\nz %.0f%s", RefTitle(r).c_str(), a_cell.refText[static_cast<std::size_t>(best->index)].c_str(),
					r.baseEditorId.c_str(), r.position[2], r.display ? ("\n" + home::DisplayLabel(r)).c_str() : "");
				if ((_selectMode || _paint) && clicked) {
					ClickRef(a_cell, best->index);
				}
			} else if (_selectMode && clicked && !io.KeyCtrl) {
				Select(-1);
			}
		}
		// Box selection: a left drag in select mode selects the containers and displays inside (Ctrl adds).
		if (_selectMode && !_paint && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !io.KeyAlt) {
			const ImVec2 a = io.MouseClickedPos[ImGuiMouseButton_Left];
			const ImVec2 b = io.MousePos;
			dl->AddRectFilled(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)), ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)), IM_COL32(255, 230, 120, 40));
			dl->AddRect(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)), ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)), IM_COL32(255, 230, 120, 200));
			_boxing = true;
		} else if (_boxing && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			_boxing = false;
			const ImVec2     a = io.MouseClickedPos[ImGuiMouseButton_Left];
			const ImVec2     b = io.MousePos;
			std::vector<int> inside;
			for (const auto& h : hits) {
				const auto layer = static_cast<Layer>(h.priority);
				if ((layer == Layer::Container || layer == Layer::Display) && h.at.x >= std::min(a.x, b.x) && h.at.x <= std::max(a.x, b.x) &&
					h.at.y >= std::min(a.y, b.y) && h.at.y <= std::max(a.y, b.y)) {
					inside.push_back(h.index);
				}
			}
			BoxSelect(inside, io.KeyCtrl);
		}
		for (const auto& h : hits) {
			if (h.index != _selectedRef && IsSelected(h.index)) {
				if (h.poly) {
					dl->AddPolyline(h.pts, 4, IM_COL32(255, 230, 120, 255), ImDrawFlags_Closed, 2.5f);
				}
				dl->AddCircle(h.at, 7.0f, IM_COL32(255, 230, 120, 255), 0, 2.0f);
			}
		}
		if (_selectedRef >= 0) {
			for (const auto& h : hits) {
				if (h.index == _selectedRef) {
					if (h.poly) {
						dl->AddPolyline(h.pts, 4, IM_COL32(255, 255, 255, 255), ImDrawFlags_Closed, 3.0f);
					}
					dl->AddCircle(h.at, 9.0f, IM_COL32(255, 255, 255, 255), 0, 2.0f);
				}
			}
		}
		// Scale bar: 512 units (a little over 7 m).
		const float bar = 512.0f * _zoom;
		const ImVec2 b0(origin.x + 12.0f, origin.y + canvasSize.y - 14.0f);
		dl->AddLine(b0, ImVec2(b0.x + bar, b0.y), IM_COL32(200, 200, 200, 200), 2.0f);
		dl->AddText(ImVec2(b0.x, b0.y - ImGui::GetFontSize() - 2.0f), IM_COL32(200, 200, 200, 200), "512 units");
		dl->AddText(ImVec2(origin.x + canvasSize.x - ImGui::GetFontSize() * 1.2f, origin.y + 6.0f), IM_COL32(200, 200, 200, 200), "N");
		dl->PopClipRect();

		ImGui::SameLine();
		DrawDetails(a_cell, detailsWidth, canvasSize.y);
	}

	void EditorApp::DrawDetails(OpenCell& a_cell, float a_width, float a_height)
	{
		ImGui::BeginChild("details", ImVec2(a_width, a_height), ImGuiChildFlags_Borders);
		DrawRefDetails(a_cell);
		ImGui::EndChild();
	}

	void EditorApp::DrawRefDetails(OpenCell& a_cell)
	{
		if (Selection().size() > 1) {
			DrawBulkEditor(a_cell);
			return;
		}
		const int idx = _selectedRef >= 0 ? _selectedRef : _hoveredRef;
		if (idx < 0 || static_cast<std::size_t>(idx) >= a_cell.info.refs.size()) {
			ImGui::TextWrapped("Select something in the 3D view, on the plan or in the tables.");
			ImGui::Spacing();
			ImGui::TextColored(ImVec4(0.35f, 0.78f, 0.43f, 1.0f), "container");
			ImGui::TextColored(ImVec4(0.9f, 0.27f, 0.24f, 1.0f), "respawning container");
			ImGui::TextColored(ImVec4(0.31f, 0.75f, 0.9f, 1.0f), "weapon rack");
			ImGui::TextColored(ImVec4(0.86f, 0.43f, 0.86f, 1.0f), "mannequin");
			ImGui::TextColored(ImVec4(0.94f, 0.67f, 0.24f, 1.0f), "plaque / display case");
			ImGui::TextColored(ImVec4(0.94f, 0.86f, 0.31f, 1.0f), "door");
			ImGui::TextColored(ImVec4(0.55f, 0.59f, 0.86f, 1.0f), "furniture, activators");
			return;
		}
		const auto& r = a_cell.info.refs[static_cast<std::size_t>(idx)];
		ImGui::TextWrapped("%s", RefTitle(r).c_str());
		if (idx != _selectedRef) {
			ImGui::SameLine();
			ImGui::TextDisabled("(hover)");
		}
		ImGui::Separator();
		ImGui::TextDisabled("Reference");
		ImGui::TextUnformatted(a_cell.refText[static_cast<std::size_t>(idx)].c_str());
		if (ImGui::SmallButton("Copy ref")) {
			ImGui::SetClipboardText(a_cell.refText[static_cast<std::size_t>(idx)].c_str());
		}
		if (!r.editorId.empty()) {
			ImGui::Text("EditorID: %s", r.editorId.c_str());
		}
		ImGui::TextDisabled("Base");
		ImGui::Text("%s %s", esp::SigToString(r.baseType).c_str(), r.baseEditorId.c_str());
		if (!r.model.empty()) {
			ImGui::TextWrapped("Mesh %s", r.model.c_str());
		}
		ImGui::Text("Position %.0f, %.0f, %.0f", r.position[0], r.position[1], r.position[2]);
		ImGui::Text("Rotation %.0f deg, scale %.2f", r.rotation[2] * 180.0f / std::numbers::pi_v<float>, r.scale);
		if (std::abs(r.rotation[0]) > 1e-4f || std::abs(r.rotation[1]) > 1e-4f) {
			ImGui::TextDisabled("Tilt X %.0f, Y %.0f deg", r.rotation[0] * 180.0f / std::numbers::pi_v<float>, r.rotation[1] * 180.0f / std::numbers::pi_v<float>);
		}
		if (r.display) {
			ImGui::Text("Display: %s", home::DisplayLabel(r).c_str());
		}
		if (r.respawns) {
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Respawns: never bind it.");
		}
		if (r.initiallyDisabled) {
			ImGui::TextUnformatted("Initially disabled (appears later)");
		}
		if (r.enableParent) {
			ImGui::TextUnformatted(r.enableOpposite ? "Disappears when its enable parent appears (upgrade)" : "Appears with its enable parent");
		}
		if (r.opens) {
			ImGui::TextUnformatted("Opens a container behind it (Dova Stash binds the container)");
		}
		if (!r.scripts.empty()) {
			ImGui::TextDisabled("Scripts");
			for (const auto& s : r.scripts) {
				ImGui::BulletText("%s", s.c_str());
			}
		}
		if (idx == _selectedRef) {
			DrawRoleEditor(a_cell, idx);
		}
		DrawPreviewDetails(a_cell, idx);
	}

	namespace
	{
		std::string Join(const std::vector<std::string>& a_items)
		{
			std::string out;
			for (const auto& i : a_items) {
				out += (out.empty() ? "" : ", ") + i;
			}
			return out;
		}
	}

	void EditorApp::DrawContainers(OpenCell& a_cell)
	{
		ImGui::TextDisabled("Click to select, double-click to look at it in the 3D view.");
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
				const bool twice = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
				if (ImGui::GetIO().KeyCtrl) {
					ToggleMulti(static_cast<int>(i));
				} else {
					Select(static_cast<int>(i), twice);
				}
				if (twice) {
					_requestTab = 0;
				}
			}
			ImGui::PopID();
			ImGui::TableNextColumn();
			const auto ref = a_cell.lo->ToFormRef(r.key);
			if (a_cell.layout.IsMaster(ref)) {
				ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "master");
			} else if (const auto* b = a_cell.layout.FindContainer(ref)) {
				if (b->ignored) {
					ImGui::TextDisabled("ignored");
				} else if (b->categories.empty() && b->forms.empty()) {
					ImGui::TextDisabled("no categories");
				} else {
					ImGui::TextUnformatted(Join(b->categories).c_str());
				}
			} else {
				ImGui::TextDisabled("not listed");
			}
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(a_cell.refText[i].c_str());
			ImGui::TableNextColumn();
			ImGui::Text("%.0f", r.position[2]);
			ImGui::TableNextColumn();
			std::string notes;
			if (r.respawns) {
				notes += "respawns; ";
			}
			if (r.initiallyDisabled) {
				notes += "initially disabled; ";
			}
			if (r.enableParent && r.enableOpposite) {
				notes += "goes away with an upgrade; ";
			}
			for (const auto& s : r.scripts) {
				notes += s + "; ";
			}
			ImGui::TextDisabled("%s", notes.c_str());
		}
		ImGui::EndTable();
	}

	void EditorApp::DrawDisplays(OpenCell& a_cell)
	{
		ImGui::TextDisabled("Click to select, double-click to look at it in the 3D view.");
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
				const bool twice = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
				if (ImGui::GetIO().KeyCtrl) {
					ToggleMulti(static_cast<int>(i));
				} else {
					Select(static_cast<int>(i), twice);
				}
				if (twice) {
					_requestTab = 0;
				}
			}
			ImGui::PopID();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(home::DisplayLabel(r).c_str());
			ImGui::TableNextColumn();
			if (const auto* d = a_cell.layout.FindDisplay(a_cell.lo->ToFormRef(r.key))) {
				if (d->ignored) {
					ImGui::TextDisabled("ignored");
				} else {
					ImGui::TextUnformatted(d->categories.empty() ? "used" : Join(d->categories).c_str());
				}
			} else {
				ImGui::TextDisabled("-");
			}
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(a_cell.refText[i].c_str());
			ImGui::TableNextColumn();
			ImGui::Text("%.0f", r.position[2]);
		}
		ImGui::EndTable();
	}

	void EditorApp::DrawDoors(OpenCell& a_cell)
	{
		if (a_cell.info.doors.empty()) {
			ImGui::TextDisabled("No doors to other cells.");
			return;
		}
		ImGui::TextWrapped("Doors lead to the home's other cells (a cellar, a cave) and to the exterior around it. Open one to see it "
		                   "(the template comes along; Back returns). Tick \"In the home\" to make it part of this home: its containers "
		                   "and displays can then be used, and the template lists it in `cells` (DESIGN 6.7).");
		const bool busy = _job.valid();
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
		for (const auto i : shown) {
			const auto& d = a_cell.info.doors[i];
			ImGui::PushID(static_cast<int>(i));
			ImGui::BeginDisabled(busy || !d.destinationCell);
			if (ImGui::SmallButton("Open")) {
				OpenCellAsync(a_cell.lo, *d.destinationCell, true, true);
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (d.destinationCell) {
				const auto ref = a_cell.lo->ToFormRef(*d.destinationCell);
				const bool main = layout.cell && *layout.cell == ref;
				bool       in = main || std::ranges::any_of(layout.cells, [&](const core::HomeCell& c) { return c.cell == ref; });
				ImGui::BeginDisabled(main);
				if (ImGui::Checkbox("In the home", &in)) {
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
				ImGui::SameLine();
			}
			ImGui::Text("%s %s", d.destinationInterior ? "interior" : "exterior", d.destinationLabel.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("%s%s", a_cell.doorText[i].c_str(), count[i] > 1 ? std::format("  ({} doors)", count[i]).c_str() : "");
			ImGui::PopID();
		}
	}
}
