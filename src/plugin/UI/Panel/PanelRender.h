#pragma once

// ImGui side of the Menu Framework panel. PanelRender.cpp is (with UI/MenuFramework.cpp and
// UI/Panel/ArmorSetsPage.cpp) the only translation unit that includes the vendored SKSEMenuFramework.h
// (it drags in <windows.h>).
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
