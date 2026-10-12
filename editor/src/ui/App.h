#pragma once

// The editor window (docs/EDITOR.md §3): mods of the MO2 profile -> a mod's plugins and the player homes
// found in them -> a cell: its floor plan (top-down, select mode), containers, displays, doors to other
// cells, and a draft template. Parsing runs on a worker thread; the UI only reads finished results.

#include <future>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Workspace.h"
#include "home/Cell.h"

namespace dova::editor::ui
{
	// Win32 + Direct3D 11 + Dear ImGui host (Window.cpp). Returns the process exit code.
	// `a_mod` / `a_cell` (optional) preselect a mod (name or Nexus id) and open one of its cells (EditorID or form ref).
	// `a_screenshot` (optional): save the window as a .bmp once loading is done, then exit.
	int RunGui(const WorkspaceOptions& a_options, std::optional<std::string> a_mod = {}, std::optional<std::string> a_cell = {},
		std::optional<std::string> a_screenshot = {});

	class EditorApp
	{
	public:
		EditorApp(WorkspaceOptions a_options, std::optional<std::string> a_mod, std::optional<std::string> a_cell);
		~EditorApp();

		void Frame();  // one ImGui frame: the whole window

		// Nothing loading and nothing waiting to open.
		[[nodiscard]] bool Idle() const { return !_job.valid() && !_pendingCell; }

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
			std::vector<std::string>        refText;   // formatted ref, parallel to info.refs
			std::vector<std::string>        doorText;  // formatted destination cell, parallel to info.doors
			std::string                     cellText;
			std::string                     draft;     // draft template JSON
			std::vector<std::string>        draftDiagnostics;
			long long                       ms{ 0 };
		};

		void OpenWorkspace();
		void StartJob(std::string a_label, std::function<std::function<void()>()> a_work);
		void PollJob();

		void SelectMod(int a_index);  // -1: the game's Data folder
		void Scan();
		void OpenCellAsync(std::shared_ptr<esp::LoadOrder> a_lo, esp::FormKey a_cell);

		void DrawTopBar();
		void DrawMods();
		void DrawHomes();
		void DrawCell();
		void DrawPlan(OpenCell& a_cell);
		void DrawRefDetails(const OpenCell& a_cell);
		void DrawContainers(OpenCell& a_cell);
		void DrawDisplays(OpenCell& a_cell);
		void DrawDoors(OpenCell& a_cell);
		void DrawDraft(OpenCell& a_cell);
		void FitPlan(const OpenCell& a_cell);

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

		std::optional<std::string> _pendingCell;  // --cell: opened when the scan finds it

		std::vector<PluginScan>   _scans;
		std::unique_ptr<OpenCell> _cell;

		// Floor plan state
		bool  _selectMode{ true };
		int   _selectedRef{ -1 };
		int   _hoveredRef{ -1 };
		float _zoom{ 1.0f };
		float _panX{ 0.0f }, _panY{ 0.0f };
		float _centerX{ 0.0f }, _centerY{ 0.0f };
		float _zMin{ 0.0f }, _zMax{ 0.0f }, _zLo{ 0.0f }, _zHi{ 0.0f };
		bool  _fitPending{ true };
		bool  _showStatics{ true }, _showContainers{ true }, _showDisplays{ true }, _showDoors{ true }, _showFurniture{ true }, _showOther{ false };
	};
}
