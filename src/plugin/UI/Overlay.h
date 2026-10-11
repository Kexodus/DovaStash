#pragma once

// Floating container icons (DESIGN §10.3). While the player is in an enabled home, each nearby managed
// container gets its icon drawn above it on the HUD.
//
// No art of ours: the icon is the player's own (core::IconRef from icons::ForContainer: I4 rules and
// icon packs when installed, else SkyUI's icon SWF). We create empty movie clips on the vanilla HUD
// movie, loadMovie() the icon SWF into them and gotoAndStop() the label: the same thing SkyUI does for
// inventory icons. Skin-agnostic: nothing depends on how the HUD is skinned.
//
// Driven from a vtable hook on HUDMenu::AdvanceMovie (main thread, once per frame). Not on VR.

namespace dova::ui::overlay
{
	// kDataLoaded. No-op on VR. [Overlay] settings are read every frame (live changes).
	void Install();
}
