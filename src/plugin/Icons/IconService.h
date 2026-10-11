#pragma once

// Icons borrowed from the player's own UI (DESIGN §1 principle 6, §10.3). Dova Stash ships no art:
// every icon is a frame label in an SWF the player already has. Container icons come from the
// category definitions ("icon" in Categories/*.json), resolved against the SWFs the player's SkyUI is
// configured with (Interface/skyui/config.txt [Appearance] icons.item.source / icons.category.source).
//
// Any thread after Load() (kDataLoaded, after home::Store::Load()): nothing here changes afterwards.

namespace dova::icons
{
	// Reads SkyUI's icon settings (which SWFs the player's icons live in). Logs them.
	void Load();

	// The category's own "icon" (falling back to the nearest ancestor category that has one), with
	// SkyUI's default SWFs replaced by the player's configured ones. Empty when none. Accepts a binding
	// pattern too ("weapons.*" -> "weapons"; "*" -> empty).
	[[nodiscard]] core::IconRef ForCategory(std::string_view a_categoryId);

	// ForCategory, plus "*" -> SkyUI's "All" category icon. For showing every pattern a container is
	// bound to.
	[[nodiscard]] core::IconRef ForPattern(std::string_view a_pattern);

	// Icon for a container, stable for as long as its layout entry does not change (DESIGN §10.3):
	// master -> SkyUI's "All" category icon; bound -> the first bound pattern's category icon ("*" ->
	// "All"); learned-only -> the icon of the category most of its learned items belong to; ignored or
	// unmanaged -> empty. The container's current contents never matter.
	[[nodiscard]] core::IconRef ForContainer(const core::Layout& a_layout, RE::TESObjectREFR* a_container);
}
