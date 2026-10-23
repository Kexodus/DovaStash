#pragma once

// The editor's log: %LOCALAPPDATA%\DovaStashEditor\editor.log (next to state.json; started fresh each run, the
// previous run kept as editor.old.log). Console commands and --screenshot runs write
// %TEMP%\DovaStashEditor\editor-run.log instead (UseRunLog), so they never replace the author's last
// session; with DOVASTASH_EDITOR_STATE set, editor.log next to that file. State changes only (a workspace opened,
// archives opened, a cell's meshes and textures loaded, a read that failed and why), never per frame. Thread-safe.

#include <filesystem>
#include <format>
#include <string>
#include <string_view>

namespace dova::editor
{
	void                                LogLine(std::string_view a_text);
	void                                UseRunLog();  // before the first line: console commands and screenshot runs
	[[nodiscard]] std::filesystem::path LogFile();

	template <class... A>
	void Log(std::format_string<A...> a_fmt, A&&... a_args)
	{
		LogLine(std::format(a_fmt, std::forward<A>(a_args)...));
	}
}
