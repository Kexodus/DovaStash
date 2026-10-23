#pragma once

// "Save as template..." from the hotkey menus (ListUI and MessageBoxUI; the panel has its Template tab).
// Both go through Controller::ExportTemplate, so the file, backup, version and refresh rules are the
// same everywhere (DESIGN §6.5).

namespace dova::ui
{
	inline constexpr auto kSaveAsTemplate = "Save as template...";

	// Asks for the template's name with UIExtensions' text entry, pre-filled with the home's name (without
	// UIExtensions the home's name is used as it is), then saves. The author is the player character, the
	// version is automatic, and learned items are kept as fixed item bindings (a personal backup; the HUD
	// says "incl. N learned items"). When a template of that name exists, a Yes/No box names the file and
	// the version it becomes; the old file is backed up; No, or cancelling the name box, runs `a_onBack` (the
	// menu it came from). The HUD reports the result. Main thread.
	void SaveAsTemplate(RE::FormID a_cellId, std::function<void()> a_onBack = {});
}
