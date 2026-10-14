#pragma once

// Display entries and the template a layout was made from (DESIGN §8.4 "Displays in templates";
// core rules in dova/core/TemplateDisplays.h). Main thread (SKSE tasks) only: reads the templates.

namespace dova::home
{
	// Run on arrival in a home with a layout, after applying a template and after turning a display on:
	// - Writes what each used item plaque and display case is made for (its script's items) into its layout
	//   entry when the entry names none, or names only the copy of the template's list that ApplyTemplate made:
	//   a template never decides what such a display is made for. A player's own edit stays. Entries whose
	//   display is not loaded or not recognised are left as they are.
	// - Logs, once per session per display and at info, the items and sets the layout's template chose for its
	//   racks and mannequins that do not exist in this load order (skipped; the data is kept). Never for item
	//   plaques and cases.
	// - Logs, once per session per display and category and as a warning, the categories a used rack, mannequin or
	//   bookshelf can't take (core::UnfitDisplayCategories: "Soul gems can't go on a weapon rack"). They stay in the
	//   layout and never put anything on it.
	// True if the layout changed (the caller saves it). `a_log` false: nothing is logged (the panel's check on a
	// copy of the layout, HomeSnapshot::ownChanges).
	bool RecordDisplayItems(core::Layout& a_layout, bool a_log = true);
}
