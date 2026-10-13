#pragma once

// The Dova Stash Editor (tools/editor), for the About page: the build deploys it to
// <mod folder>/Tools/DovaStashEditor.exe, next to SKSE/Plugins/DovaStash.dll. Any thread (file system only).

namespace dova::ui::panel
{
	// Where the editor is, found from this DLL's own path (<dll>/../../../Tools/DovaStashEditor.exe; under MO2
	// the path the game sees). Empty when it is not installed. Looked up once per session.
	[[nodiscard]] const std::string& EditorInstallPath();
}
