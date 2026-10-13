#pragma once

// Displays in templates (DESIGN §8.4 "Displays in templates"): one documented shape for a display entry in
// layouts and templates, its lint, and the rules for what a template may pre-define.
//
// Decisions:
// - A display entry has the same fields everywhere: ref, kind, label, categories, items, only, set, ignored,
//   note (written in that order; see Layout.h). Every field but `ref` is optional, and each applies to
//   some kinds only: `categories` and `only` to racks, mannequins and bookshelves, `set` to mannequins, `items` to all.
// - Racks, mannequins and bookshelves: `items` are chosen by the file (preferred items; with `only`, the only ones). An
//   item from a plugin that is not loaded, or a form that is not in its plugin, is a soft reference: it is
//   skipped and the rest of the template works. A mannequin's `set` is soft the same way (the mannequin
//   then waits for it, as with a set chosen in game, ArmorSets.h), and so does an `only` display none of
//   whose items exist (Display.h). A template therefore never needs the plugins of its display items or
//   sets in `requires` (ReferencedPlugins leaves them out, ValidateTemplate does not ask for them); listing
//   one there makes the whole template depend on it, as for any plugin.
// - Item plaques and display cases: what they are made for comes from their own script (dragon priest
//   masks, claws, Elder Scrolls...). A template never decides it: for a display whose script Dova Stash
//   reads, the template's `items` are not used; the plugin replaces the copy ApplyTemplate made with the
//   script's items (ItemsCopiedFromTemplate). A player's own edit in their layout keeps working (it is
//   not the template's copy). Only for a display declared with `kind` whose script Dova Stash cannot read
//   are the entry's items its definition. None of this is ever reported as missing.
// - Missing template items are reported once per session per display, at info, by the plugin
//   (MissingTemplateItems + MissingItemsMessage). Only what the template chose and the layout still names
//   counts: the player's own picks (the set menu) and script items never do.
// - Unresolvable references are never removed from the data (layouts or templates). The one list that is
//   replaced is a layout's copy of a template's items for an item plaque or case; the template keeps it.
// - Existing files are untouched by all of this: every field is optional, `only` is new (an older DLL keeps
//   it as an unknown key) and written only when true.

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dova/core/Diagnostics.h"
#include "dova/core/DisplayKind.h"
#include "dova/core/FormRef.h"
#include "dova/core/Layout.h"
#include "dova/core/Template.h"

namespace dova::core
{
	// Whether an entry's `items` are chosen by the file (racks, mannequins, bookshelves) rather than defined by the display's
	// own script (item plaques, cases). nullopt (kind unknown) counts as chosen.
	[[nodiscard]] bool ItemsChosenByFile(std::optional<DisplayKind> a_kind) noexcept;

	// Lint for display entries (warnings only, nothing is changed): fields that do nothing for the entry's
	// `kind` (set on a rack, categories or only on an item plaque or case), `only` without items or next to a
	// set, and a display that is also listed as the master or a container that is not ignored (it is then
	// used for storage).
	// ValidateTemplate runs it; it suits player layouts too.
	void ValidateDisplays(const Layout& a_layout, std::string_view a_source, Diagnostics& a_diags);

	// The template the layout was made from: the candidate whose source its `basedOn` names
	// ("<source>@<version>", any version). Null when there is none.
	[[nodiscard]] const TemplateCandidate* BasedOnTemplate(const Layout& a_layout, std::span<const TemplateCandidate> a_templates);

	// True when `a_entry`'s items are the copy ApplyTemplate made of the template's entry `a_templateEntry`
	// (same items, same unparsed items, and the template names some). For an item plaque or case this copy is
	// replaced with what its script is made for.
	[[nodiscard]] bool ItemsCopiedFromTemplate(const DisplayBinding& a_entry, const DisplayBinding* a_templateEntry);

	struct MissingDisplayItems
	{
		FormRef                display;
		std::string            label;  // the entry's label, else the template's, else the ref
		std::vector<FormRef>   items;  // chosen items that do not exist, in the entry's order
		std::optional<FormRef> set;    // the chosen set, when it does not exist
		bool                   waits{ false };  // nothing it may take exists: an "only" display none of whose items
		                                        // exist, or a mannequin whose set does not; it stays empty until they do
	};

	// The display's kind as its script makes it; nullopt when the script is not recognised or the display is not
	// loaded (the entry's `kind` is used then).
	using DisplayKindOf = std::function<std::optional<DisplayKind>(const FormRef& a_display)>;
	// Whether a form exists in this load order (its plugin is loaded and has it).
	using FormExists = std::function<bool(const FormRef& a_form)>;

	// What the template `a_template` chose for the displays of `a_layout` (the layout made from it, or the
	// template itself) that does not exist: rack and mannequin `items`, a mannequin's `set`. Only entries the
	// layout uses (not ignored) and the template lists, and only references the layout entry still names.
	// Skipped: item plaques and cases (their script decides), and displays whose kind is unknown yet (not
	// loaded, no `kind`). In layout order.
	[[nodiscard]] std::vector<MissingDisplayItems> MissingTemplateItems(const Layout& a_layout, const Layout& a_template, const DisplayKindOf& a_kindOf,
		const FormExists& a_exists);

	// One log line: "Template Breezehome: Bow rack: 2 items don't exist in this load order (Mod.esp|0x00000D62,
	// Mod.esp|0x00000D63); skipped". `a_template` is the template's name.
	[[nodiscard]] std::string MissingItemsMessage(std::string_view a_template, const MissingDisplayItems& a_missing);
}
