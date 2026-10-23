#pragma once

// Naming an area of a multi-cell home (DESIGN §6.7), or a container, shared by every UI surface that adds or
// renames one (the hotkey's area prompt, the UIExtensions lists, the message boxes). Main thread.

namespace dova::ui
{
	// Adds the player's cell to the home it was offered to (Controller::AddArea), asking for the area's name
	// first with the UIExtensions text box (pre-filled with the cell's own name; a name equal to it is not
	// stored). Cancelling, or an empty name, adds the area without a name: the add is never lost. Without
	// UIExtensions the area is added at once.
	void AddAreaNamed(RE::FormID a_cellId);

	// Renames an extra area of the home (Controller::RenameArea) with the UIExtensions text box, pre-filled with
	// its current name. Cancelled or empty: nothing changes. False when the text box cannot open (no UIExtensions,
	// another menu is up); the caller says so.
	bool RenameAreaNamed(RE::FormID a_cellId, const core::FormRef& a_area, std::string a_current);

	// Renames a container (Controller::SetLabel) the same way: the text box pre-filled with its name; cancelled
	// or empty changes nothing ("Restore original name" is its own entry). False when the text box cannot open.
	bool RenameContainerNamed(RE::FormID a_cellId, const core::FormRef& a_container);
	// The container has a name the player gave it ("Restore original name" applies).
	[[nodiscard]] bool HasCustomName(RE::TESObjectREFR* a_ref);
}
