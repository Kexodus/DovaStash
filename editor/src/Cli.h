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
		std::optional<std::string> screenshot;  // --gui: save the window as a .bmp once loading is done, then exit
		std::vector<std::string>   errors;

		[[nodiscard]] bool Headless() const noexcept { return !errors.empty() || help || (!gui && (listMods || homes || cell.has_value())); }
	};

	[[nodiscard]] Args ParseArgs(const std::vector<std::string>& a_args);
	[[nodiscard]] int  RunCli(const Args& a_args);
	[[nodiscard]] std::string Usage();
}
