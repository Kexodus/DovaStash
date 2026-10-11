#pragma once

// Activate prompt decoration (DESIGN §10.3): "Chest" becomes "Chest (Weapons, Armor)" for managed
// containers in an enabled home. A vtable hook on TESObjectCONT::GetActivateText (vfunc 0x4C) edits
// only the displayed string. Nothing is written to the reference or the save, so uninstalling leaves
// no trace. Drawn by the vanilla HUD: the player's HUD skin and fonts apply, VR included.

namespace dova::hooks
{
	// kDataLoaded. Always installs; [Integrations] bActivatePrompt is read on every call (live toggle).
	void InstallActivatePrompt();
}
