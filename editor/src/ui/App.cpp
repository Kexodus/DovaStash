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
		if (_options.mo2) {
			std::snprintf(_mo2Path, sizeof(_mo2Path), "%s", PathToUtf8(*_options.mo2).c_str());
		}
		if (a_gui.tab) {
			static constexpr const char* tabs[] = { "3d", "plan", "containers", "displays", "doors", "template" };
			for (int i = 0; i < 6; ++i) {
				if (core::IEquals(*a_gui.tab, tabs[i])) {
					_requestTab = i;
				}
			}
		}
		_pendingSelect = a_gui.select;
		OpenWorkspace();
		if (_ws && a_gui.mod) {
			std::snprintf(_modFilter, sizeof(_modFilter), "%s", a_gui.mod->c_str());
			if (const auto found = _ws->FindMods(*a_gui.mod); !found.empty()) {
				_pendingCell = a_gui.cell;
				SelectMod(static_cast<int>(found.front() - _ws->Mods().data()));
			}
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
		return !_job.valid() && !_pendingCell && (!_scene || _scene->Progress().finished);
	}

	void EditorApp::Select(int a_index, bool a_frame)
	{
		_selectedRef = a_index;
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
		_scene = std::make_unique<render::Scene>(_device, _assets, render::BuildSceneDesc(_cell->info));
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
		if (!_ws->Open(_options, _wsError)) {
			_ws.reset();
		} else if (_ws->Instance() && _mo2Path[0] == '\0') {
			std::snprintf(_mo2Path, sizeof(_mo2Path), "%s", PathToUtf8(_ws->Instance()->root).c_str());
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
			};
		});
	}

	void EditorApp::OpenCellAsync(std::shared_ptr<esp::LoadOrder> a_lo, esp::FormKey a_cell, bool a_remember)
	{
		if (a_remember && _cell && !(_cell->info.key == a_cell && _cell->lo == a_lo)) {
			_history.emplace_back(_cell->lo, _cell->info.key);
		}
		StartJob("Opening cell...", [this, a_lo, a_cell]() -> std::function<void()> {
			const auto start = Clock::now();
			auto       cell = home::LoadCell(*a_lo, a_cell);
			if (!cell) {
				return [] { ::MessageBoxA(nullptr, "That cell is not in the loaded plugins.", "Dova Stash Editor", MB_ICONWARNING); };
			}
			auto oc = std::make_shared<OpenCell>();
			oc->lo = a_lo;
			oc->info = std::move(*cell);
			oc->cellText = a_lo->Format(oc->info.key);
			for (const auto& r : oc->info.refs) {
				oc->refText.push_back(a_lo->Format(r.key));
			}
			for (const auto& d : oc->info.doors) {
				oc->doorText.push_back(d.destinationCell ? a_lo->Format(*d.destinationCell) : std::string{});
			}
			core::Diagnostics diags;
			oc->layout = home::DraftTemplate(*a_lo, oc->info, diags);
			oc->ms = Ms(start);
			return [this, oc]() {
				_cell = std::make_unique<OpenCell>(std::move(*oc));
				if (!_templateAuthor.empty()) {
					_cell->layout.author = _templateAuthor;
				}
				Revalidate(*_cell);
				_selectedRef = -1;
				_hoveredRef = -1;
				_fitPending = true;
				_listsHidden = true;
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
		DrawTopBar();
		ImGui::Separator();
		if (_cell && _listsHidden) {
			ImGui::BeginChild("cell");
			DrawCell();
			ImGui::EndChild();
		} else if (ImGui::BeginTable("columns", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImGui::GetContentRegionAvail())) {
			ImGui::TableSetupColumn("Mods", ImGuiTableColumnFlags_WidthStretch, 0.22f);
			ImGui::TableSetupColumn("Homes", ImGuiTableColumnFlags_WidthStretch, 0.20f);
			ImGui::TableSetupColumn("Cell", ImGuiTableColumnFlags_WidthStretch, 0.58f);
			ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetContentRegionAvail().y);
			ImGui::TableNextColumn();
			ImGui::BeginChild("mods");
			DrawMods();
			ImGui::EndChild();
			ImGui::TableNextColumn();
			ImGui::BeginChild("homes");
			DrawHomes();
			ImGui::EndChild();
			ImGui::TableNextColumn();
			ImGui::BeginChild("cell");
			DrawCell();
			ImGui::EndChild();
			ImGui::EndTable();
		}
		ImGui::End();
	}

	void EditorApp::DrawTopBar()
	{
		const bool busy = _job.valid();
		ImGui::TextUnformatted("Dova Stash Editor");
		ImGui::SameLine();
		ImGui::TextDisabled("in development: reads your setup, shows homes, drafts templates (docs/EDITOR.md)");
		if (busy) {
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", _jobLabel.c_str());
		}
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

	void EditorApp::DrawMods()
	{
		ImGui::SeparatorText("1. Home mod");
		if (!_ws) {
			ImGui::TextWrapped("Open an MO2 instance above, or add the editor to MO2 as an executable and start it from there.");
			return;
		}
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
		ImGui::SeparatorText("2. Player homes in its plugins");
		if (_selectedMod < -1 || !_ws) {
			ImGui::TextWrapped("Pick the mod that adds the home. Its plugins are searched for player homes: cells whose location is a player "
			                   "house, owned by the player, that never reset, with containers and displays.");
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
							OpenCellAsync(scan.lo, h.cell);
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
		ImGui::SeparatorText("3. Cell");
		if (!_cell) {
			ImGui::TextWrapped(_job.valid() ? _jobLabel.c_str() : "Open a cell from the list of homes.");
			return;
		}
		auto& c = *_cell;
		if (ImGui::SmallButton(_listsHidden ? "> Mods and homes" : "< Hide lists")) {
			_listsHidden = !_listsHidden;
		}
		ImGui::SetItemTooltip(_listsHidden ? "Show the mod list and the homes found in it" : "Give the cell the whole window");
		ImGui::SameLine();
		if (!_history.empty()) {
			ImGui::BeginDisabled(_job.valid());
			if (ImGui::SmallButton("< Back")) {
				auto [lo, key] = _history.back();
				_history.pop_back();
				OpenCellAsync(lo, key, false);
			}
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Back to the cell opened before (%zu)", _history.size());
			ImGui::SameLine();
		}
		ImGui::Text("%s", c.info.name.empty() ? c.info.editorId.c_str() : c.info.name.c_str());
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
				const ImU32  col = ColorOf(r, layer);
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
				if (_selectMode && clicked) {
					Select(best->index);
				}
			} else if (_selectMode && clicked) {
				Select(-1);
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
			if (ImGui::Selectable(RefTitle(r).c_str(), _selectedRef == static_cast<int>(i), ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
				const bool twice = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
				Select(static_cast<int>(i), twice);
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
			if (ImGui::Selectable(RefTitle(r).c_str(), _selectedRef == static_cast<int>(i), ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
				const bool twice = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
				Select(static_cast<int>(i), twice);
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
		ImGui::TextWrapped("Doors lead to the home's other cells (a cellar, a cave) and to the exterior around it.");
		const bool busy = _job.valid();
		for (std::size_t i = 0; i < a_cell.info.doors.size(); ++i) {
			const auto& d = a_cell.info.doors[i];
			ImGui::PushID(static_cast<int>(i));
			ImGui::BeginDisabled(busy || !d.destinationCell);
			if (ImGui::SmallButton("Open")) {
				OpenCellAsync(a_cell.lo, *d.destinationCell);
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::Text("%s %s", d.destinationInterior ? "interior" : "exterior", d.destinationLabel.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("%s", a_cell.doorText[i].c_str());
			ImGui::PopID();
		}
	}
}
