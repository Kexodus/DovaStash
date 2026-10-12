#pragma once

// A mannequin's armor set (DESIGN §8.4 "Mannequins"): "Armor set... (Auto)" in the display menu of a
// used mannequin. The picker lists "Auto" and the sets the player has (carried, stored in the home, or
// on this mannequin), most complete first: "[x] Ebony (4/4)". Choosing one writes the layout entry's
// `set` (Auto removes it). Shared by the list (ListUI) and message-box (IStashUI default) versions.
// Main thread.

#include "UI/IStashUI.h"

namespace dova::ui
{
	struct ArmorSetChoice
	{
		std::string                  text;  // "[x] Ebony (4/4)"
		std::optional<core::FormRef> set;   // nullopt: Auto
	};

	struct ArmorSetMenu
	{
		RE::FormID                  cellId{ 0 };
		core::FormRef               ref;   // the mannequin
		std::string                 name;  // "Mannequin"
		std::vector<ArmorSetChoice> choices;
	};

	// "Ebony" or "Auto": the set the mannequin's layout entry names (for "Armor set... (Ebony)").
	[[nodiscard]] std::string ArmorSetSummary(RE::FormID a_cellId, const core::FormRef& a_mannequin);

	// Builds the picker for the mannequin and opens it in the world UI.
	void OpenArmorSets(RE::FormID a_cellId, const core::FormRef& a_mannequin);

	// Stores the choice (nullopt: Auto) in the mannequin's layout entry, adding the entry (used) if it has
	// none, saves the layout and tells the player. Also used by the Menu Framework "Armor sets" page.
	void ChooseArmorSet(RE::FormID a_cellId, const core::FormRef& a_mannequin, std::optional<core::FormRef> a_set);
}
