// Dova Stash Editor: a standalone template editor (docs/EDITOR.md). With a command (--list-mods, --homes,
// --cell) it prints to the console; without one it opens the window.
//
// Built for the Windows subsystem so that launching it from MO2 does not flash a console. Commands write
// to the inherited handles (pipes, files) or attach to the parent's console.

#include <cstdio>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "Cli.h"
#include "Util.h"
#include "ui/App.h"

namespace
{
	void ConnectConsole()
	{
		const auto usable = [](DWORD a_which) {
			const HANDLE h = ::GetStdHandle(a_which);
			return h != nullptr && h != INVALID_HANDLE_VALUE && ::GetFileType(h) != FILE_TYPE_UNKNOWN;
		};
		if (usable(STD_OUTPUT_HANDLE)) {
			return;  // redirected to a pipe or file: the C runtime already writes there
		}
		if (::AttachConsole(ATTACH_PARENT_PROCESS) || ::AllocConsole()) {
			FILE* f = nullptr;
			freopen_s(&f, "CONOUT$", "w", stdout);
			freopen_s(&f, "CONOUT$", "w", stderr);
			std::fputs("\n", stdout);
		}
		::SetConsoleOutputCP(CP_UTF8);
	}
}

int wmain(int argc, wchar_t** argv)
{
	std::vector<std::string> args;
	for (int i = 1; i < argc; ++i) {
		args.push_back(dova::editor::Narrow(argv[i]));
	}
	const auto parsed = dova::editor::ParseArgs(args);
	if (parsed.Headless()) {
		ConnectConsole();
		const int rc = dova::editor::RunCli(parsed);
		std::fflush(stdout);
		std::fflush(stderr);
		return rc;
	}
	return dova::editor::ui::RunGui(parsed.workspace, parsed.mod, parsed.cell, parsed.screenshot);
}
