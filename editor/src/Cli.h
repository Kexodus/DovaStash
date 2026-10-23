#pragma once

#include <optional>
#include <string>
#include <utility>
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
		std::optional<std::string> saveTo;  // --draft: save the draft into this folder (as the window saves)
		std::optional<std::string> author;
		bool                       gui{ false };  // open the window even with --mod/--cell (they preselect)
		std::optional<std::string> screenshot;  // --gui: save the window once loading is done, then exit; --render-cell: the image
		std::optional<std::string> state;       // --gui --state <file>: the remembered state (with --screenshot: none unless given)
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
		bool                       roofCut{ false };  // --render-cell --roof-cut: the window's roof cut (interiors)
		bool                       markers{ false };
		float                      brightness{ 1.0f };  // --light <factor>
		bool                       verbose{ false };
		bool                       noNeighbours{ false };  // --render-cell: exterior without the 8 cells around it
		std::optional<std::string> tab;  // --gui: 3d|plan (the view), containers|displays|doors|preview (the drawer), template (the inspector)
		bool                       draftAll{ false };   // --draft-all: every home of the load order, a draft each
		bool                       compare{ false };    // --draft-all --compare: drafts against the existing templates
		std::optional<std::string> only;                // --draft-all --only <text>: homes whose name, EditorID or plugin contains it
		bool                       includeTemplated{ false };  // --draft-all --save-to: also homes that have a template elsewhere (never over one in the folder)
		std::optional<std::string> preview;             // --preview <cell>: where items go
		std::string                sample{ "haul" };    // --sample haul | all | <category pattern>
		bool                       useDraft{ false };   // --preview --draft: the draft, not the existing template
		bool                       drafts{ false };     // --gui --drafts: start "Draft all homes", the rail on homes without a template
		bool                       baseGame{ false };   // --draft-all/--preview --base-game: only the game's own plugins
		std::optional<std::string> colours;             // --gui --colours category|fill
		std::optional<std::string> paint;               // --gui --paint <category>
		std::vector<std::string>   multi;               // --gui --multi <ref> (repeatable)
		std::optional<std::string> home;                // --gui --home <text>: open the first home whose name contains it
		std::vector<std::string>   moreHomes;           // --home again: open those homes in turn, each once the previous has loaded (tests)
		std::optional<std::string> inspector;           // --gui --inspector selection|template|draft|checks
		bool                       palette{ false };    // --gui --palette: the command palette open
		std::optional<std::string> popover;             // --gui --popover <id>: a popover open (screenshots of the guide)
		std::optional<std::pair<int, int>> window;      // --gui --window WxH: the window's client size
		std::optional<std::string> theme;               // --gui --theme <name>: a colour theme for this run
		std::optional<float>       textSize;            // --gui --text-size <percent|auto>: the text size for this run (0: Auto)
		std::optional<std::string> layers;              // --gui --layers <names>: Layers popover toggles ("effects,-structure")
		std::vector<std::string>   sort;                // --gui --sort table:column[:desc] (repeatable: more columns)
		std::optional<std::string> tableFilter;         // --gui --filter <text>: the drawer tab's filter box
		std::vector<std::string>   quick;               // --gui --quick <chip> (repeatable): the drawer tab's quick filters
		std::vector<std::string>   errors;

		[[nodiscard]] bool Headless() const noexcept
		{
			return !errors.empty() || help ||
			       (nifInfo.has_value() || renderCell.has_value() || (!gui && (listMods || homes || cell.has_value() || draftAll || preview.has_value())));
		}
	};

	[[nodiscard]] Args ParseArgs(const std::vector<std::string>& a_args);
	[[nodiscard]] int  RunCli(const Args& a_args);
	[[nodiscard]] std::string Usage();

	// CliDrafts.cpp
	[[nodiscard]] int DraftAllCommand(const Workspace& a_ws, const Args& a_args);
	[[nodiscard]] int PreviewCommand(const Workspace& a_ws, const Args& a_args);
}
