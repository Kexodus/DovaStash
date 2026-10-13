#pragma once

// The editor window (docs/EDITOR.md §3): mods of the MO2 profile -> a mod's plugins and the player homes
// found in them -> a cell: its 3D view and floor plan (select mode on both, one selection), containers,
// displays, doors to other cells, and the template being made. Parsing runs on a worker thread; the UI only
// reads finished results. The 3D view loads its meshes and textures on its own threads (render::Scene).

#include <future>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Workspace.h"
#include "dova/core/Category.h"
#include "dova/core/Layout.h"
#include "home/Cell.h"
#include "render/Scene.h"

namespace dova::editor
{
	class Assets;
}

namespace dova::editor::ui
{
	struct GuiOptions
	{
		std::optional<std::string> mod;         // preselect a mod (name or Nexus id)
		std::optional<std::string> cell;        // and open one of its cells (EditorID or form ref)
		std::optional<std::string> screenshot;  // save the window once loading is done, then exit
		std::optional<std::string> tab;         // "3d" (default), "plan", "containers", "displays", "doors", "template"
		std::optional<std::string> select;      // a reference to select once the cell is open
	};

	// Win32 + Direct3D 11 + Dear ImGui host (Window.cpp). Returns the process exit code.
	int RunGui(const WorkspaceOptions& a_options, const GuiOptions& a_gui);

	class EditorApp
	{
	public:
		EditorApp(WorkspaceOptions a_options, const GuiOptions& a_gui, ID3D11Device* a_device, ID3D11DeviceContext* a_context);
		~EditorApp();

		void Frame();  // one ImGui frame: the whole window

		// Nothing loading, nothing waiting to open, the 3D view complete.
		[[nodiscard]] bool Idle() const;

	private:
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

			// The template being made (starts as the draft, home::DraftTemplate).
			core::Layout             layout;
			bool                     dirty{ false };
			std::string              json;          // SerializeLayout(layout), refreshed on change
			std::vector<std::string> diagnostics;   // ValidateTemplate (+ categories)
			std::string              savedPath;
			std::string              saveMessage;
		};

		void OpenWorkspace();
		void StartJob(std::string a_label, std::function<std::function<void()>()> a_work);
		void PollJob();

		void SelectMod(int a_index);  // -1: the game's Data folder
		void Scan();
		// `a_keepTemplate`: the template being made follows (another cell of the same home: a door, Back);
		// otherwise the cell starts a new draft (a home picked from the list).
		void OpenCellAsync(std::shared_ptr<esp::LoadOrder> a_lo, esp::FormKey a_cell, bool a_remember = true, bool a_keepTemplate = false);
		void RequestNewHome(std::shared_ptr<esp::LoadOrder> a_lo, esp::FormKey a_cell);  // asks first when the template has unsaved changes
		void DrawDiscardPopup();
		[[nodiscard]] bool CellInHome(const OpenCell& a_cell) const;  // the open cell is the template's main cell or one of its `cells`
		void StartScene();

		void DrawTopBar();
		void DrawMods();
		void DrawHomes();
		void DrawCell();
		void DrawPlan(OpenCell& a_cell);
		void DrawView3D(OpenCell& a_cell);       // View3D.cpp
		void DrawDetails(OpenCell& a_cell, float a_width, float a_height);
		void DrawRefDetails(OpenCell& a_cell);
		void DrawRoleEditor(OpenCell& a_cell, int a_index);  // Roles.cpp
		void DrawContainers(OpenCell& a_cell);
		void DrawDisplays(OpenCell& a_cell);
		void DrawDoors(OpenCell& a_cell);
		void DrawTemplate(OpenCell& a_cell);  // Roles.cpp
		void FitPlan(const OpenCell& a_cell);
		void Select(int a_index, bool a_frame = false);

		// Template editing (Roles.cpp)
		void LoadCategories();
		void Revalidate(OpenCell& a_cell);
		void SaveTemplate(OpenCell& a_cell, bool a_overwrite);
		[[nodiscard]] std::filesystem::path TemplatePath(const OpenCell& a_cell) const;
		bool CategoryEditor(const char* a_id, std::vector<std::string>& a_patterns);

		WorkspaceOptions           _options;
		std::unique_ptr<Workspace> _ws;
		std::string                _wsError;
		char                       _mo2Path[1024]{};

		std::future<std::function<void()>> _job;
		std::string                        _jobLabel;

		char _modFilter[128]{};
		bool _onlyEnabled{ true };
		bool _onlyWithPlugins{ true };
		int  _selectedMod{ -2 };  // -2 none, -1 game Data folder, else index into Mods()
		bool _exteriors{ false };
		bool _likelyOnly{ true };
		bool _listsHidden{ false };  // the cell takes the whole window (set when a cell opens)

		std::optional<std::string> _pendingCell;    // --cell: opened when the scan finds it
		std::optional<std::string> _pendingSelect;  // --select: selected when the cell opens
		int                        _requestTab{ -1 };  // tab to switch to on the next frame

		std::vector<PluginScan>   _scans;
		std::unique_ptr<OpenCell> _cell;
		std::vector<std::pair<std::shared_ptr<esp::LoadOrder>, esp::FormKey>> _history;  // cells opened before (Back)

		// Shared selection: the plan, the 3D view and the tables.
		bool _selectMode{ true };
		int  _selectedRef{ -1 };
		int  _hoveredRef{ -1 };

		// Floor plan state
		float _zoom{ 1.0f };
		float _panX{ 0.0f }, _panY{ 0.0f };
		float _centerX{ 0.0f }, _centerY{ 0.0f };
		float _zMin{ 0.0f }, _zMax{ 0.0f }, _zLo{ 0.0f }, _zHi{ 0.0f };
		bool  _fitPending{ true };
		bool  _showStatics{ true }, _showContainers{ true }, _showDisplays{ true }, _showDoors{ true }, _showFurniture{ true }, _showOther{ false };

		// 3D view state
		ID3D11Device*                  _device{ nullptr };
		ID3D11DeviceContext*           _context{ nullptr };
		std::shared_ptr<Assets>        _assets;
		std::unique_ptr<render::Scene> _scene;
		render::RenderTarget           _target;
		render::Camera                 _camera;
		render::ViewOptions            _view;
		bool                           _cameraPending{ true };
		bool                           _frameSelection{ false };
		bool                           _heightCut3d{ false };
		float                          _flySpeed{ 500.0f };  // units per second
		float                          _viewAspect{ 16.0f / 9.0f };
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
		std::string                     _categorySource;
		std::string                     _templateAuthor;
		int                             _saveTarget{ 0 };  // index into the save targets
		bool                            _confirmOverwrite{ false };
		std::optional<std::pair<std::shared_ptr<esp::LoadOrder>, esp::FormKey>> _pendingNewHome;  // waiting for "discard changes?"
	};
}
