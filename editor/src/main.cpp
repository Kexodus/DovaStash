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
	dova::editor::ui::GuiOptions gui;
	gui.mod = parsed.mod;
	gui.cell = parsed.cell;
	gui.screenshot = parsed.screenshot;
	gui.tab = parsed.tab;
	gui.select = parsed.select;
	gui.drafts = parsed.drafts;
	gui.home = parsed.home ? parsed.home : parsed.only;
	gui.inspector = parsed.inspector;
	gui.palette = parsed.palette;
	gui.popover = parsed.popover;
	gui.theme = parsed.theme;
	gui.window = parsed.window;
	if (gui.screenshot && !gui.window) {
		gui.window = std::pair{ 1600, 900 };  // the guide's figures: the same size on every display
	}
	gui.colours = parsed.colours;
	gui.paint = parsed.paint;
	gui.multi = parsed.multi;
	return dova::editor::ui::RunGui(parsed.workspace, gui);
}
