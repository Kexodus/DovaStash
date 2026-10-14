#pragma once

// ImGui side of the Menu Framework panel. PanelRender.cpp, the other pages (UI/Panel/ArmorSetsPage.cpp,
// UI/Panel/DisplaysPage.cpp) and UI/MenuFramework.cpp are the only translation units that include the vendored
// SKSEMenuFramework.h (it drags in <windows.h>). The pages share UI/Panel/Widgets.h; the wording and layout
// conventions are at the top of UI/Panel/Strings.h.
//
// Render callbacks run on the RENDER thread: they read PanelModel snapshots only and turn every button
// press into a PanelModel::Post() closure that runs on the main thread.

namespace dova::ui::panel
{
	// Main thread, kDataLoaded or later (the framework's function lookups are cached on first use).
	// Registers the stash window (AddWindow) and the "Dova Stash" section pages. False if the framework
	// is not loaded or refused the window; idempotent.
	[[nodiscard]] bool Register();

	// Shows the stash window (any thread; the framework reads an atomic flag).
	void OpenWindow();
}
