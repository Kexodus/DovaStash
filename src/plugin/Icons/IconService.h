#pragma once

// Icons borrowed from the player's own UI (DESIGN §1 principle 6, §10.3). Dova Stash ships no art:
// every icon is a frame label in an SWF the player already has (SkyUI's icon SWFs, or whatever their
// Inventory Interface Information Injector (I4) rule files point at).
//
// The resolution logic is engine-independent (core/Icons.h); this service feeds it the player's setup:
// - I4 rule files, discovered the way I4 discovers them: for every active plugin in load order,
//   SKSE/Plugins/InventoryInjector/<plugin name>.json (loose files or archives). Only when I4 itself
//   (SKSE/Plugins/InventoryInjector.dll) is installed; otherwise items get SkyUI's default icons.
// - SkyUI's Interface/skyui/config.txt [Appearance]: icons.item.source, icons.category.source,
//   icons.item.noColor (MCM overrides of those are not read).
//
// Main thread only. Load() runs once at kDataLoaded,
// after home::Store::Load() (category icons come from the store).

namespace dova::icons
{
	// Parses every I4 rule file in I4's own load order and reads SkyUI's icon settings. Logs the rule
	// count and diagnostics. No-op (SkyUI defaults only) when I4 is not installed.
	void Load();

	// True when at least one I4 rule was loaded.
	[[nodiscard]] bool HasI4Rules() noexcept;

	// The icon the player's inventory shows for this item: SkyUI's default, then I4 rules.
	[[nodiscard]] core::IconRef ForItem(const core::ItemInfo& a_item);

	// The category's own "icon" (falling back to the nearest ancestor category that has one), with
	// SkyUI's default SWFs replaced by the player's configured ones. Empty when none. Accepts a binding
	// pattern too ("weapons.*" -> "weapons"; "*" -> empty).
	[[nodiscard]] core::IconRef ForCategory(std::string_view a_categoryId);

	// Icon for a container, stable for as long as its layout entry does not change (DESIGN §10.3):
	// master -> SkyUI's "All" category icon; bound -> the first bound pattern's category icon ("*" ->
	// "All"); learned-only -> the icon of the category most of its learned items belong to; ignored or
	// unmanaged -> empty. The container's current contents never matter.
	[[nodiscard]] core::IconRef ForContainer(const core::Layout& a_layout, RE::TESObjectREFR* a_container);
}
