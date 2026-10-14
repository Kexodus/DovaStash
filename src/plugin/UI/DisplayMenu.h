#pragma once

// The display menu (DESIGN §8.4): what the hotkey offers at a rack, mannequin, plaque, display case or bookshelf.
// Shared by the list (ListUI) and message-box (IStashUI default) versions.

#include "UI/IStashUI.h"

namespace dova::ui
{
	struct DisplayOption
	{
		std::string           text;
		std::function<void()> action;
		core::IconRef         icon;  // lists only (ListUI); message boxes are text
	};

	// "Use this rack" when it is not used. When it is: "Categories... (Bows)" for racks, mannequins and
	// bookshelves (`a_openCategories` shows the picker; its icon is the first chosen category's, else the
	// weapons/armor/books one), "Armor set... (Auto)" for mannequins (UI/ArmorSetMenu.h), "Fill now" while it is
	// empty (a mannequin with a chosen set: while it can take more of it), "Stop using this rack".
	[[nodiscard]] std::vector<DisplayOption> DisplayMenuOptions(const DisplayMenu& a_menu, std::function<void()> a_openCategories);

	// What a display's category picker offers, as patterns: the loaded categories it can take
	// (core::DisplayCategoryChoices: a rack for any weapon "weapons.*" and each weapon category, a shield rack the
	// shields, a dagger rack the daggers; mannequins the armor they wear; bookshelves the books). Empty for plaques
	// and cases (made for their own items).
	[[nodiscard]] std::vector<std::string> DisplayCategoryChoices(const core::DisplayFit& a_fit);
	// The display's patterns in the current layout (empty if it has none or is not listed).
	[[nodiscard]] std::vector<std::string> DisplayCategories(RE::FormID a_cellId, const core::FormRef& a_display);
	// One picker line: "[x] Bows" (chosen), "[+] Bows" (covered by a wider choice), "[ ] Bows".
	[[nodiscard]] std::string DisplayCategoryEntry(const std::vector<std::string>& a_patterns, const std::string& a_choice);
}
