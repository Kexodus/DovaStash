#pragma once

// The editor window (docs/EDITOR-UX.md §2, behaviour in docs/EDITOR.md §3): one workspace, no modes. A title
// row (menu, home switcher, 3D | Plan, Draft, Save, Setup), the homes rail on the left (every home of the load
// order under the mod that adds it, the drafts' chips, any cell of a mod on request), the viewport (the 3D view
// or the plan with a floating toolbar; the start panel when no home is open), the inspector on the right
// (Selection, Template, Draft, Checks), the drawer at the bottom (Containers, Displays, Doors, Where items go)
// and the status row. Parsing runs on a worker thread; the UI only reads finished results. The 3D view loads its
// meshes and textures on its own threads (render::Scene).

#include <array>
#include <atomic>
#include <deque>
#include <future>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Workspace.h"
#include "dova/core/Category.h"
#include "dova/core/DisplayCategories.h"
#include "dova/core/Layout.h"
#include "dova/core/Preview.h"
#include "dova/core/Template.h"
#include "esp/Items.h"
#include "home/ArmorSets.h"
#include "home/Cell.h"
#include "home/DovaData.h"
#include "home/Drafts.h"
#include "home/TemplateFile.h"
#include "render/Scene.h"
#include "ui/State.h"
#include "ui/Tables.h"
#include "ui/Widgets.h"

namespace dova::editor
{
	class Assets;
}

namespace dova::editor::render
{
	class Thumbnails;
}

namespace dova::editor::ui
{
	struct GuiOptions
	{
		std::optional<std::string> mod;         // preselect a mod (name or Nexus id): the rail's search
		std::optional<std::string> cell;        // and open one of its cells (EditorID or form ref)
		std::optional<std::string> screenshot;  // save the window once loading is done, then exit (an in-memory state)
		std::optional<std::string> state;       // the state file instead of state.json
		std::optional<std::string> tab;         // 3d, plan (the view); containers, displays, doors, preview (the drawer); template (the inspector)
		std::optional<std::string> select;      // a reference to select once the cell is open
		bool                       drafts{ false };  // start "Draft all homes" and show the homes without a template
		std::optional<std::string> home;        // open the first home whose name contains it
		std::vector<std::string>   moreHomes;   // then these, each once the previous home's 3D view has loaded (tests)
		std::optional<std::string> colours;     // "category" or "fill": colour the containers in 3D and on the plan
		std::optional<std::string> paint;       // turn paint mode on with this category
		std::vector<std::string>   multi;       // more references to select (multi-select)
		std::optional<std::string> inspector;   // selection, template, draft or checks: the inspector's panel
		bool                       palette{ false };  // open the command palette (Ctrl+K)
		std::optional<std::string> popover;   // open a popover once the window is ready (screenshots): menu, setup, switcher, filter, draftmenu, layers, colours, camera, paintcat, savedrafts (a dialog), categories (the selection's category picker)
		std::optional<std::pair<int, int>> window;    // the window's client size (screenshots at 1280 x 720)
		int                        timeout{ 180 };    // --screenshot: seconds before it saves what it has and exits 4
		std::optional<std::string> theme;     // a colour theme for this run (ui/Themes.h); the remembered one stays
		std::optional<float>       textSize;  // the text size for this run (1.5: 150 %, 0: Auto), screenshots too; the remembered one stays
		std::optional<std::string> layers;    // Layers popover toggles for this run: "effects,-structure" (names, - turns off)
		std::vector<std::string>   sort;      // drawer tables' sort for this run: "table:column[:desc]" (ui/Tables.h)
		std::optional<std::string> filter;    // the drawer tab's filter box
		std::optional<std::string> search;    // the rail's search box
		std::vector<std::string>   quick;     // the drawer tab's column filters on (their labels)
	};

	// Win32 + Direct3D 11 + Dear ImGui host (Window.cpp). Returns the process exit code.
	int RunGui(const WorkspaceOptions& a_options, const GuiOptions& a_gui);

	// Colours (Preview.cpp): a category pattern's colour (one hue per top-level group, lighter or darker per
	// subcategory), RGBA8 as ImGui packs it (IM_COL32).
	[[nodiscard]] std::uint32_t CategoryColour(std::string_view a_pattern);

	class EditorApp
	{
	public:
		EditorApp(WorkspaceOptions a_options, const GuiOptions& a_gui, ID3D11Device* a_device, ID3D11DeviceContext* a_context);
		~EditorApp();

		void Frame();  // one ImGui frame: the whole window

		// Nothing loading, nothing waiting to open, the 3D view complete.
		[[nodiscard]] bool Idle() const;

		// The window's close button: asks first when there are unsaved changes.
		void               RequestClose();
		[[nodiscard]] bool ShouldQuit() const noexcept { return _quit; }
		// A --home matched no home (a screenshot run then exits 1, still writing its image).
		[[nodiscard]] bool HomeMissing() const noexcept { return _homeMissing; }
		// A screenshot run: a job failed (stderr and the log said why; exit 1, no message box).
		[[nodiscard]] bool Failed() const noexcept { return _failed; }
		[[nodiscard]] std::string WaitingFor() const;  // what Idle() waits on, in words

	private:
		enum class Panel
		{
			Selection,
			Template,
			Draft,
			Checks
		};

		enum class DrawerTab
		{
			Containers,
			Displays,
			Doors,
			Preview
		};

		// One home's draft (made when the home is opened or by "Draft all homes").
		struct DraftEntry
		{
			std::unique_ptr<home::HomeDraft> draft;
			bool                             reviewed{ false };  // the author changed it (accepted, rejected, edited)
			std::string                      saveMessage;
			std::vector<std::string>         problems;
		};

		// Every home of the active load order (the rail, the start panel and the drafts share it).
		struct HomesData
		{
			std::shared_ptr<esp::LoadOrder>   lo;
			std::vector<home::HomeEntry>      homes;
			home::TemplateIndex               templates;
			std::shared_ptr<esp::ItemCatalog> catalog;
			home::ItemNameIndex               itemNames;
			std::vector<DraftEntry>           entries;  // parallel to homes
			struct Storage
			{
				int containers{ 0 }, displays{ 0 };  // what a template can use over the home's cells
			};
			std::vector<Storage>              storage;  // parallel to homes
			std::vector<std::string>          errors;
			long long                         ms{ 0 };
		};

		// The homes of HomesData under the mod that provides the plugin defining each one.
		struct HomeModGroup
		{
			int              mod{ -1 };  // index into Mods(); -1 the game's Data folder, -2 a plugin no mod provides
			std::vector<int> homes;      // indices into HomesData::homes
		};

		struct Progress
		{
			std::atomic<std::size_t> done{ 0 }, total{ 0 };
			std::mutex               mutex;
			std::string              stage;
		};

		struct PreviewState
		{
			std::string                 json;  // the layout it was run for (re-run when it changes)
			int                         kind{ 0 };
			std::string                 pattern;
			std::vector<core::ItemInfo> items;
			core::PreviewResult         result;
			long long                   ms{ 0 };
			std::map<core::FormRef, int> refIndex;  // container/display ref -> index into the open cell's refs (-1: another cell)
			const void*                 cell{ nullptr };  // the OpenCell it was run in
		};

		enum class Colours
		{
			None,
			Category,  // each container in its first category's colour
			Fill       // the preview: empty / light / heavy / master
		};

		// "All cells..." of a mod: the cells with containers in each of its plugins, scored.
		struct PluginScan
		{
			std::string                         plugin;
			std::shared_ptr<esp::LoadOrder>     lo;
			std::vector<home::HomeCandidate>    homes;
			std::vector<std::string>            cellRefs;  // formatted, parallel to homes
			std::vector<std::string>            errors;
			long long                           ms{ 0 };
		};

		struct OpenCell
		{
			std::shared_ptr<esp::LoadOrder> lo;
			home::CellInfo                  info;
			std::vector<home::CellInfo>     context;   // exterior: the cells around it, drawn in 3D
			std::vector<std::string>        refText;   // formatted ref, parallel to info.refs
			std::vector<std::string>        doorText;  // formatted destination cell, parallel to info.doors
			std::string                     cellText;
			long long                       ms{ 0 };
			std::vector<home::CellInfo>     homeCells;       // the template's other cells, read for the checks (ReadHomeCells)
			std::vector<core::FormRef>      homeCellsAsked;  // each asked for once (one that cannot be read is not asked again)

			// The template being made (starts as the draft, home::DraftTemplate or the home's HomeDraft).
			core::Layout             layout;
			bool                     dirty{ false };
			std::string              json;          // SerializeLayout(layout), refreshed on change
			std::vector<std::string> diagnostics;   // ValidateTemplate (+ categories), "error: ..." / "warning: ..."
			std::string              savedPath;
			std::string              saveMessage;
			std::string              filePath;      // opened from a template file (Open a template...)
		};

		// A command of the palette and the menus.
		struct Command
		{
			std::string           label;
			std::string           shortcut;
			bool                  enabled{ true };
			std::function<void()> run;
		};

		// ---- Workspace, jobs, cells (App.cpp) ----
		void OpenWorkspace();
		void StartJob(std::string a_label, std::function<std::function<void()>()> a_work);
		void Queue(std::function<void()> a_start);  // runs when no job is running (one job at a time: the load order is not thread-safe)
		void PollJob();
		[[nodiscard]] bool Busy() const { return _job.valid(); }

		void SelectMod(int a_index);  // "All cells..." of a mod (-1: the game's Data folder)
		void Scan();
		// `a_keepTemplate`: the template being made follows (another cell of the same home: a door, Back);
		// otherwise the cell starts a new draft (a home picked from the list).
		void OpenCellAsync(std::shared_ptr<esp::LoadOrder> a_lo, esp::FormKey a_cell, bool a_remember = true, bool a_keepTemplate = false);
		void RequestNewHome(std::shared_ptr<esp::LoadOrder> a_lo, esp::FormKey a_cell);  // asks first when the template has unsaved changes
		// A home of the rail: on the template the game offers first (HomeTemplates' order), or on its draft when it
		// has none, when its draft has unsaved changes, or with `a_draft`.
		void OpenHome(int a_index, std::optional<esp::FormKey> a_cell = std::nullopt, bool a_draft = false);
		void UseDraft();  // the Draft panel's "Use the draft": the open template gives way to the home's draft
		[[nodiscard]] bool OpenUnsaved() const;  // the open template has changes no draft keeps (opening another home asks first)
		void ResolvePendingCell();   // --cell once the homes are known
		void DrawDiscardPopup();
		[[nodiscard]] bool CellInHome(const OpenCell& a_cell) const;  // the open cell is the template's main cell or one of its `cells`
		void StartScene();
		void Select(int a_index, bool a_frame = false);      // no panel change (the Draft and Checks panels select too)
		void SelectInView(int a_index, bool a_frame = false);  // and show the Selection panel
		void ApplyWindowSize();
		void SaveLayoutState();  // the panels' sizes, when they changed

		// ---- Frame layout ----
		void DrawTitleRow();      // TitleBar.cpp
		void DrawStatusRow();     // TitleBar.cpp
		void DrawMainMenu();      // TitleBar.cpp: the ≡ popover
		void DrawDraftMenu();     // TitleBar.cpp: Draft ▾
		void DrawSetupPopover();  // TitleBar.cpp: ⚙
		void SetTextScale(float a_setting);  // TitleBar.cpp: Text size (0 = Auto), remembered and applied next frame
		void StepTextScale(int a_dir);       // Ctrl+= / Ctrl+-: 10 % a step
		void PickTheme(const std::string& a_name);  // TitleBar.cpp: a colour theme, remembered and applied next frame
		void ExportTheme();                         // TitleBar.cpp: the theme in effect into a file under Themes (ui/Themes.h)
		float _uiSizeDrag{ 100.0f };         // the Setup slider while it is dragged (percent)
		bool  _uiSizeHeld{ false };          // the Setup slider was held last frame (its value is not resynced)
		void DrawSetupConfirm();  // TitleBar.cpp
		void DrawHelpPopups();    // TitleBar.cpp: Shortcuts..., About
		void ApplySetup(std::optional<std::filesystem::path> a_mo2, std::optional<std::string> a_profile);
		// The folder picked in the start card or Setup: an MO2 instance (ModOrganizer.ini), else a Skyrim Data folder
		// (Skyrim.esm) or the game's folder holding one; anything else is tried as an instance (its error says why).
		void SetSetupFolder(std::optional<std::filesystem::path> a_folder);
		void DrawViewport(ImVec2 a_size);  // App.cpp
		// The panels' bounds: the view keeps at least 40% of the window's width and half its height.
		[[nodiscard]] float RailMaxWidth() const;
		[[nodiscard]] float InspectorMaxWidth() const;
		float _drawerMinH{ 0.0f }, _drawerMaxH{ 0.0f }, _layoutH{ 0.0f };  // this frame's (the drawer's splitter)
		void DrawOverlay(OpenCell& a_cell, ImVec2 a_min, ImVec2 a_max);  // App.cpp: the floating toolbar and the hint line
		void DrawLayersPopover(OpenCell& a_cell);
		void DrawCameraPopover(OpenCell& a_cell);
		void HandleShortcuts();  // App.cpp: docs/EDITOR-UX.md §4
		void FindAHome();        // Ctrl+F, the start screen's first card: the rail open, its search focused

		// ---- Homes rail (Rail.cpp) ----
		void DrawRail();
		void DrawHomeList(bool a_compact, std::string& a_search);  // the rail's list, or the switcher's
		void DrawAnyCells(int a_mod);  // "All cells..." of the mod under its group
		void GroupHomes();
		[[nodiscard]] std::vector<int> VisibleHomes(const std::string& a_search) const;  // rows in list order (keyboard)
		[[nodiscard]] bool HomeMatches(const home::HomeEntry& a_home, const std::string& a_search, bool a_modMatch) const;
		[[nodiscard]] std::string HomeChip(int a_index, w::Kind* a_kind) const;  // yours / shipped / auto-draft / draft / none
		[[nodiscard]] bool        HomeHasDraft(int a_index) const;              // the rail's and the title row's "draft"
		[[nodiscard]] bool        DraftExists() const;                          // the open cell's draft is made (OpenDraft makes it)
		[[nodiscard]] bool IsOpenHome(int a_index) const;
		void               CheckRefs(const OpenCell& a_cell, core::Diagnostics& a_diags) const;  // Roles.cpp: home::CheckTemplateRefs on the home's cells
		void               ReadHomeCells(OpenCell& a_cell);  // Roles.cpp: a job reading the template's cells not read yet (exteriors too), for CheckRefs
		[[nodiscard]] std::string HomeRefName(const OpenCell& a_cell, const core::FormRef& a_ref) const;  // a reference of any read cell of the home
		[[nodiscard]] bool HomeUnsaved(int a_index) const;
		[[nodiscard]] int  ActiveFilterCount() const;
		void HomeTooltip(const home::HomeEntry& a_home) const;
		void HomeContextMenu(int a_index);

		// ---- Viewport ----
		void DrawPlan(OpenCell& a_cell, ImVec2 a_size);    // Plan.cpp
		void FitPlan(const OpenCell& a_cell, ImVec2 a_size);
		void CentrePlanOn(const OpenCell& a_cell, int a_index);
		void DrawView3D(OpenCell& a_cell, ImVec2 a_size);  // View3D.cpp
		void DrawStart(ImVec2 a_min, ImVec2 a_max);        // Start.cpp
		void Overview();      // Home: the 3D overview or the plan fitted
		void FrameSelected();  // F

		// ---- Inspector (Inspector.cpp) ----
		void DrawInspector();
		void DrawSelectionPanel(OpenCell& a_cell);
		void DrawRefDetails(OpenCell& a_cell, int a_index);
		void DrawChecksPanel(OpenCell& a_cell);
		void MakeNearestMaster(OpenCell& a_cell);
		[[nodiscard]] int RefIndexOf(const OpenCell& a_cell, std::string_view a_ref) const;  // -1: not in the open cell

		// ---- Drawer (Drawer.cpp) ----
		void DrawDrawer();
		void DrawContainers(OpenCell& a_cell);
		void DrawDisplays(OpenCell& a_cell);
		void DrawDoors(OpenCell& a_cell);
		void TableRowClick(int a_index);  // a row of a table: select (Ctrl: add), double click frames
		// Each table's sort and filter for the session (ui/Tables.h), by DrawerTab; not in state.json.
		std::array<TableView, static_cast<std::size_t>(DrawerTable::Count_)> _tables;
		TableView& Table(DrawerTab a_tab) { return _tables[static_cast<std::size_t>(a_tab)]; }

		// ---- Command palette (Palette.cpp) ----
		void DrawPalette();
		[[nodiscard]] std::vector<Command> Commands();

		// ---- Template editing (Roles.cpp) ----
		void LoadCategories();
		void Revalidate(OpenCell& a_cell);
		void RequestSave();  // Save / Ctrl+S: asks first when the file exists or the home has a template
		void DrawSaveButton();  // the title row's and the Template panel's (TitleBar.cpp): filled only with unsaved work
		[[nodiscard]] bool SaveMakesCopy();  // Save writes a copy: an unchanged template that is not the file Save writes (a shipped one)
		void SaveTemplate(OpenCell& a_cell);
		void SaveTemplateAs(OpenCell& a_cell);
		void DrawSavePopup();
		[[nodiscard]] std::filesystem::path SaveBase() const;  // the chosen save target
		[[nodiscard]] std::vector<std::string> SaveTargets() const;  // "" (Dova Stash Templates) and mods with templates
		[[nodiscard]] std::filesystem::path TemplatePath(const OpenCell& a_cell) const;
		[[nodiscard]] const home::HomeEntry* HomeEntryFor(const OpenCell& a_cell) const;  // the rail home of the open cell, else null
		[[nodiscard]] std::optional<HomesData::Storage> HomeStorageFor(const OpenCell& a_cell) const;  // the rail's usable counts over the home's cells
		[[nodiscard]] std::string           HomeNameFor(const OpenCell& a_cell) const;  // the rail home's name (the template folder), else the template's
		[[nodiscard]] std::vector<const home::KnownTemplate*> TemplatesFor(const OpenCell& a_cell) const;  // every known template of the home (all its cells)
		void                                DrawTemplateList(OpenCell& a_cell);  // Roles.cpp: the home's templates, the open one marked
		void DrawRoleEditor(OpenCell& a_cell, int a_index);
		void DrawRemoveButton(OpenCell& a_cell, int a_index);  // "Remove from the template", last in the Selection panel
		void MakeMaster(OpenCell& a_cell, int a_index);  // the old master becomes a plain container
		void MakeAreaMaster(OpenCell& a_cell, int a_index, bool a_on);  // the open joined cell's (cells[].master), or not
		void DrawTemplate(OpenCell& a_cell);  // the inspector's Template panel
		bool DrawKeepRules(core::Layout& a_layout);
		// `a_fit`: a display's categories: the picker offers only what it can take (core::DisplayCategoryChoices's
		// rule), and a chosen one it can't take shows as a warning.
		bool CategoryEditor(const char* a_id, std::vector<std::string>& a_patterns, bool a_title = true, const core::DisplayFit* a_fit = nullptr);
		// A mannequin's armor set: Auto or one of the load order's sets (searchable), as the in-game picker.
		bool ArmorSetEditor(OpenCell& a_cell, std::optional<core::FormRef>& a_set);
		[[nodiscard]] int ErrorCount(const OpenCell& a_cell) const;
		[[nodiscard]] int WarningCount(const OpenCell& a_cell) const;

		WorkspaceOptions           _options;
		std::unique_ptr<Workspace> _ws;
		std::vector<std::string>   _activePlugins;  // the workspace's load order (Workspace::ActivePlugins), read on opening it
		bool                       _dataPicked{ false };  // the Data folder was picked in the window: remembered (state.json "data")
		std::string                _wsError;
		char                       _mo2Path[1024]{};
		std::optional<std::vector<std::filesystem::path>> _registeredInstances;  // MO2's own list (nxmhandler.ini), read once for the start panel

		std::future<std::function<void()>> _job;
		std::string                        _jobLabel;
		std::deque<std::function<void()>>  _queue;

		// "All cells..." (any cell of a mod) and the rail's filters (docs/EDITOR-UX.md §2.2).
		int  _selectedMod{ -2 };  // -2 none, -1 game Data folder, else index into Mods()
		bool _exteriors{ false };
		bool _likelyOnly{ true };
		bool _onlyEnabled{ true };
		bool _onlyWithoutTemplate{ false };
		bool _showEmptyHomes{ false };  // the rail's "Show homes with no containers" (no template, no usable chest: hidden)
		bool _groupByMod{ true };
		std::string               _railSearch;
		std::string               _switcherSearch;
		int                       _railCursor{ -1 };  // keyboard: a home index
		bool                      _railFocusSearch{ false };
		bool                      _railKeys{ false };  // the rail has the keyboard
		bool                      _railReveal{ false };  // scroll the rail to the cursor (a home was opened)
		int                       _railRevealFrames{ 0 };  // frames spent scrolling to it (the scroll limit settles over a few)
		std::set<int>             _collapsedGroups;  // mod indices
		std::vector<HomeModGroup> _homeGroups;
		std::vector<bool>         _homeNameClash;  // per home: another home in the scan has its name (the rail adds the EditorID)
		std::vector<PluginScan>   _scans;
		long long                 _scanMs{ 0 };

		std::optional<std::string> _pendingCell;    // --cell: opened when the scan finds it
		std::optional<std::string> _pendingMod;     // --mod with --cell: the mod whose cells are searched
		std::optional<std::string> _pendingSelect;  // --select: selected when the cell opens
		std::vector<std::string>   _pendingMulti;   // --multi: also selected when the cell opens
		std::optional<std::string> _pendingHome;    // --home: opened once the homes are known
		std::vector<std::string>   _homeQueue;      // --home given again: the next homes, in turn
		std::optional<Panel>       _pendingPanel;   // --inspector: shown once the home is open
		bool                       _pendingDrafts{ false };  // --drafts: the bulk draft once the homes are known
		std::optional<std::string> _pendingPopover;
		bool                       _openCategoryPicker{ false };  // --popover categories: the selection's category picker opens once
		std::optional<std::pair<int, int>> _windowSize;
		bool                       _windowApplied{ false };

		std::unique_ptr<OpenCell> _cell;
		std::vector<std::pair<std::shared_ptr<esp::LoadOrder>, esp::FormKey>> _history;  // cells opened before (Back)

		// Workspace layout
		Panel     _panel{ Panel::Selection };
		DrawerTab _drawerTab{ DrawerTab::Containers };
		double    _drawerFoldAt{ -1.0 };  // a click on the open tab folds the drawer then, unless a double click (Drawer.cpp)
		bool      _showStart{ false };  // the emblem: the start panel over an open home
		bool      _paletteOpen{ false };
		bool      _paletteFocus{ false };
		std::string _palette;
		int       _paletteSel{ 0 };
		UiLayout  _savedUi;
		float     _titleLeftW{ 0.0f }, _titleRightW{ 0.0f };  // measured last frame
		bool        _setupWasOpen{ false };
		std::string _setupPathAtOpen;
		bool        _showShortcuts{ false };
		bool        _showAbout{ false };
		std::string _status;  // the last result (saved, not saved), in the status row
		double      _statusTime{ 0.0 };
		bool        _statusBad{ false };
		void SetStatus(std::string a_text, bool a_bad = false);

		// Shared selection: the plan, the 3D view and the tables.
		bool _selectMode{ true };  // false: the 3D view picks anything, the plan's left drag pans
		int  _selectedRef{ -1 };
		int  _hoveredRef{ -1 };

		// Floor plan state
		float _zoom{ 1.0f };
		float _panX{ 0.0f }, _panY{ 0.0f };
		float _centerX{ 0.0f }, _centerY{ 0.0f };
		float _zMin{ 0.0f }, _zMax{ 0.0f }, _zLo{ 0.0f }, _zHi{ 0.0f };
		bool  _fitPending{ true };
		bool  _planMoved{ false };  // panned or zoomed since the last fit (no more re-fits on resize)
		ImVec2 _fitSize{};          // the viewport size of the last fit
		bool  _showMarkers{ false };  // the plan's "Other": markers, trigger volumes, references without a mesh
		// The plan's layers (Plan.cpp): its own Effects, off at first (fog and light volumes cover the rooms; the 3D
		// view's is _view.showEffects), the others shared with the 3D view.
		bool                   _planEffects{ false };
		unsigned               _planFitLayers{ 0 };  // the layers of the last fit: a change re-fits until the author pans or zooms
		[[nodiscard]] bool     PlanShows(home::RefLayer a_layer) const;
		[[nodiscard]] unsigned PlanLayers() const;

		// 3D view state
		ID3D11Device*                  _device{ nullptr };
		ID3D11DeviceContext*           _context{ nullptr };
		std::shared_ptr<Assets>        _assets;
		std::unique_ptr<render::Scene> _scene;
		std::unique_ptr<render::Thumbnails> _thumbs;  // the Selection panel's mesh pictures
		render::RenderTarget           _target;
		render::Camera                 _camera;
		render::ViewOptions            _view;
		bool                           _cameraPending{ true };
		bool                           _frameSelection{ false };
		bool                           _heightCut{ false };  // both views
		// The roof cut (3D view, interiors): about 3 m above each area's floor, never above the highest container + 120
		// units (`_roofZ`; _view.roofGrid has the areas), only containers and displays are drawn, so an overview shows
		// the rooms and a cave below them, not the rock or a house's roof. Off by default (Layers > Roof on): with the
		// cut, a home's walls and ceilings above it looked like missing meshes.
		bool                           _showRoof{ true };
		float                          _roofZ{ 1e9f };
		float                          _flySpeed{ 500.0f };  // units per second
		float                          _viewAspect{ 16.0f / 9.0f };
		ImVec2                         _viewportMin{}, _viewportMax{};
		struct PickCache
		{
			float  mouseX{ -1.0f }, mouseY{ -1.0f };
			float  camX{ 0.0f }, camY{ 0.0f }, camZ{ 0.0f }, yaw{ 0.0f }, pitch{ 0.0f };
			bool   selectMode{ false };
			std::size_t loaded{ 0 };
			int    result{ -1 };
		} _pick;  // hover picking only reruns when the mouse, the camera or the scene changed

		// Categories for the role editor (Data/SKSE/Plugins/DovaStash/Categories).
		bool                            _categoriesLoaded{ false };
		std::vector<core::CategoryDef>  _categories;
		std::optional<core::CategorySet> _categorySet;
		// A category pattern in words: its name ("Swords"), a group's name + " (all)" ("Books (all)" for books.*),
		// the id when the categories do not know it. Tables and chips show this; their tooltips keep the id.
		[[nodiscard]] std::string CategoryText(std::string_view a_pattern) const;
		[[nodiscard]] std::string CategoryTexts(const std::vector<std::string>& a_patterns) const;  // joined with ", "
		std::string                     _categorySource;
		core::DraftRules                _draftRules;
		std::string                     _draftRulesSource;
		// The Draft panel's "by the <station>" per object of the open draft (home::NearestStations), made once per draft.
		struct StationNames
		{
			const home::HomeDraft*   draft{ nullptr };
			std::size_t              objects{ 0 };
			std::vector<std::string> names;
		};
		StationNames _stationNames;
		std::string                     _templateAuthor;
		std::string                     _conflictsEdit;  // the Template panel's "Conflicts" field while it is being typed in
		bool                            _conflictsActive{ false };
		bool                            _confirmSave{ false };
		std::string                     _saveName;  // the save dialog's "Your name" (no author yet)
		mutable std::optional<std::vector<std::string>> _saveTargets;  // per workspace
		std::function<void()>           _pendingOpen;  // another home, waiting for "drop the changes?"
		std::optional<std::pair<std::optional<std::filesystem::path>, std::optional<std::string>>> _pendingSetup;  // waiting for "drop changes?"

		// ---- Homes and drafts (Drafts.cpp) ----
		EditorState                 _state;
		std::shared_ptr<HomesData>  _homes;
		std::shared_ptr<Progress>   _progress;
		bool                        _homesRequested{ false };  // the scan ran (or runs) for this workspace
		bool                        _confirmSaveAll{ false };
		// *Save N drafts...*: what saving does per home, worked out from the files on disk when the dialog opens.
		struct BulkSaveItem
		{
			int                  home{ -1 };
			core::AutoDraftCheck check;              // New / Refresh: saved; Person / OtherHome: left alone
			bool                 reviewed{ false };  // the author changed it: saved as theirs, without the auto-draft mark
			bool                 empty{ false };     // no container to sort into (no master): left alone
			std::string          fileHome;           // the name its file is saved under (home::TemplateHomeName)
		};
		std::vector<BulkSaveItem>   _bulkSave;
		bool                        _confirmRescan{ false };
		bool                        _confirmDraftAgain{ false };
		bool                        _confirmUseDraft{ false };
		bool                        _bulkDone{ false };       // "Draft all homes" finished: the banner offers to save
		bool                        _bannerClosed{ false };
		int                         _cellDraft{ -1 };  // the rail home whose draft is open (index into HomesData)
		std::unique_ptr<home::HomeDraft> _localDraft;  // the open cell's draft when it is not a home of the rail
		const void*                 _localDraftCell{ nullptr };
		std::optional<core::Layout> _pendingLayout;    // replaces the draft of the next cell opened
		bool                        _pendingLayoutDirty{ true };  // false: an opened template file (nothing to save yet)
		std::string                 _pendingFilePath;
		bool                        _quit{ false };
		bool                        _unattended{ false };  // --screenshot: errors to stderr and the log, never a message box
		// Started from MO2: the start-up file work (the start panel's art, the home scan) waits for the first frames, so
		// the main thread has loaded the graphics driver's libraries before workers call MO2's hooked file functions
		// (a run that hung on "Finding homes" left MO2's file-hook lock held by the main thread).
		int                         _holdFrames{ 0 };
		bool                        _heldArt{ false };
		bool                        _heldHomes{ false };
		bool                        _failed{ false };
		bool                        _homeMissing{ false };
		int                         _listHoveredRef{ -1 };   // a row hovered in a panel this frame (the Draft panel's)
		int                         _listHoverShown{ -1 };   // last frame's: the 3D view highlights it
		void                        NoHomeMatches(const std::string& a_wanted);
		bool                        _confirmQuit{ false };
		void ScanHomes();
		void Rescan();
		void StartBulkDraft();  // "Draft all homes": a draft for every home, on the worker
		DraftEntry& EnsureDraft(int a_index);
		home::HomeDraft* OpenDraft();  // the open home's draft (made when needed)
		void DraftThisHome();
		void DraftAgain();
		void AcceptAll();
		void RejectWeak();
		void SetSuggestionOn(OpenCell& a_cell, home::HomeDraft& a_draft, std::size_t a_container, std::size_t a_suggestion, bool a_on);
		void PlanBulkSave();  // _bulkSave for the drafts *Save N drafts...* covers
		// The bulk save of one draft (never over a template someone made); `a_fileHome`: the name its file goes under
		// (empty: the home's name).
		home::SaveResult SaveDraft(int a_index, bool a_autoDraft, const std::string& a_fileHome = {});
		[[nodiscard]] int SaveableDrafts() const;
		void SyncCellToDraft();
		void OpenTemplateFile();
		void OpenTemplateAt(std::string a_path);  // opens that template file on its home (asks about unsaved changes first)
		void DrawDraftPanel(OpenCell& a_cell);
		void DrawQuitPopup();
		void DrawSaveDraftsPopup();
		void DrawRescanPopup();
		void DrawDraftAgainPopup();
		// "yours" / "shipped" / both, from the templates someone made (auto-drafts do not count: *Save N drafts...* may
		// refresh them); `a_autoDraft`: the home has an auto-draft.
		[[nodiscard]] std::string HomeStatus(const home::HomeEntry& a_home, bool* a_user = nullptr, bool* a_autoDraft = nullptr) const;
		[[nodiscard]] std::vector<const home::KnownTemplate*> HomeTemplates(const home::HomeEntry& a_home) const;  // Rail.cpp: over all its cells, in the game's order
		[[nodiscard]] std::size_t TemplateCount(const home::HomeEntry& a_home) const;  // Rail.cpp: templates over all its cells
		// One template's word, the same in the rail, the title row and the Template panel: auto-draft (Draft all homes
		// saved it, nobody since), yours (Templates/User, the Dova Stash Templates mod or the mod picked under Save to)
		// or shipped (with Dova Stash or another mod).
		// "off" instead when a plugin in its "conflicts" is in the load order (the game does not offer it).
		[[nodiscard]] const char* TemplateWord(const home::KnownTemplate& a_template, w::Kind* a_kind = nullptr) const;
		// The template's "conflicts" plugins that the workspace loads, joined ("" when none): the game does not offer it.
		[[nodiscard]] std::string LoadedConflict(const core::Layout& a_layout) const;
		[[nodiscard]] bool AnyUnsaved() const;
		[[nodiscard]] bool UnsavedDrafts() const;

		// ---- Where items go (Preview.cpp) ----
		std::optional<PreviewState>       _preview;
		std::shared_ptr<esp::LoadOrder>   _catalogLo;
		std::shared_ptr<esp::ItemCatalog> _catalog;
		std::shared_ptr<esp::LoadOrder>   _armorSetsLo;
		std::shared_ptr<home::ArmorSetIndex> _armorSets;
		std::string                       _armorSetFilter;
		const esp::ItemCatalog&           Catalog(OpenCell& a_cell);       // the cell's load order's items (built once)
		const home::ArmorSetIndex&        ArmorSets(OpenCell& a_cell);     // its armor sets (built once)
		int                               _sampleKind{ 0 };
		std::string                       _samplePattern{ "alchemy.*" };
		Colours                           _colours{ Colours::None };
		void DrawPreview(OpenCell& a_cell);          // the drawer tab: what each container would receive
		void DrawPreviewControls(OpenCell& a_cell);  // its sample picker, in the drawer's tab row
		void RunPreview(OpenCell& a_cell);
		void DrawPreviewDetails(OpenCell& a_cell, int a_index);
		void DrawColourLegend(OpenCell& a_cell);  // the Colours popover
		[[nodiscard]] std::uint32_t ContainerColour(const OpenCell& a_cell, int a_index) const;  // 0: none
		void UpdateTints(OpenCell& a_cell);

		// ---- Editing helpers (Edit.cpp): undo/redo, multi-select, paint mode, delete ----
		std::vector<core::Layout> _undo, _redo;
		core::Layout              _lastLayout;
		std::string               _lastJson;
		const void*               _undoOwner{ nullptr };
		std::vector<int>          _multi;  // more selected references (Ctrl+click, box)
		bool                      _paint{ false };
		std::string               _paintCategory;
		std::string               _paintFilter;
		bool                      _boxing{ false };
		bool                      _confirmDelete{ false };
		void TrackUndo();
		void Undo();
		void Redo();
		void ToggleMulti(int a_index);
		[[nodiscard]] bool IsSelected(int a_index) const;
		[[nodiscard]] std::vector<int> Selection() const;  // the selected reference and the multi-selection
		bool PaintAt(OpenCell& a_cell, int a_index);  // paint mode click: toggles the category on the container
		void SetPaint(bool a_on);
		void DrawPaintBar(OpenCell& a_cell);       // the toolbar's second half in paint mode
		bool DrawCategoryList(std::string& a_filter, std::string& a_chosen);  // a searchable category list (paint)
		void DrawBulkEditor(OpenCell& a_cell);
		void ClickRef(OpenCell& a_cell, int a_index);  // a click on the plan or in 3D: paint, Ctrl+click, or select
		void BoxSelect(const std::vector<int>& a_indices, bool a_add);  // the containers and displays inside a dragged box
		void RequestDelete();  // Delete: removes the selection from the template (asks when more than one)
		void DeleteSelection(OpenCell& a_cell);
		void DrawDeletePopup();
		std::string _paintMessage;
	};
}
