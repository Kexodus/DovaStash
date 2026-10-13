#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Workspace.h"

namespace dova::editor
{
	struct Args
	{
		WorkspaceOptions           workspace;
		bool                       help{ false };
		bool                       listMods{ false };
		bool                       all{ false };  // --list-mods: also disabled mods and mods without plugins
		std::string                filter;
		std::optional<std::string> mod;     // name or Nexus id
		std::optional<std::string> plugin;  // plugin file name
		bool                       homes{ false };
		bool                       exteriors{ false };
		std::optional<std::string> cell;  // EditorID or "Plugin.esm|0x..."
		bool                       draft{ false };
		bool                       gui{ false };  // open the window even with --mod/--cell (they preselect)
		std::optional<std::string> screenshot;  // --gui: save the window once loading is done, then exit; --render-cell: the image
		std::optional<std::string> nifInfo;     // --nif-info <meshes\...nif>: dump a mesh as the 3D view reads it
		bool                       nifColumns{ false };
		std::optional<std::string> renderCell;  // --render-cell <cell>: draw the cell in 3D off-screen into --screenshot
		unsigned                   width{ 1600 }, height{ 900 };  // --size WxH
		std::optional<std::string> camera;      // --camera x,y,z,yaw,pitch (degrees)
		std::optional<std::string> select;      // --select <ref>: highlight it (and --frame: look at it)
		bool                       frame{ false };
		std::optional<std::string> heightCut;   // --height lo,hi
		std::vector<std::string>   picks;       // --pick x,y (pixels): what a click there selects, in both modes
		bool                       noDisabled{ false };
		bool                       markers{ false };
		bool                       verbose{ false };
		bool                       noNeighbours{ false };  // --render-cell: exterior without the 8 cells around it
		std::optional<std::string> tab;  // --gui: the cell tab to show ("3d", "plan", "containers", "displays", "doors", "template")  // --render-cell: list what is drawn as a box and why
		std::vector<std::string>   errors;

		[[nodiscard]] bool Headless() const noexcept { return !errors.empty() || help || (nifInfo.has_value() || renderCell.has_value() || (!gui && (listMods || homes || cell.has_value()))); }
	};

	[[nodiscard]] Args ParseArgs(const std::vector<std::string>& a_args);
	[[nodiscard]] int  RunCli(const Args& a_args);
	[[nodiscard]] std::string Usage();
}
