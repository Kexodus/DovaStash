#pragma once

// Which categories a display can take (DESIGN §8.4 "Categories on displays"). One rule for every picker (the
// plugin's hotkey menu and panel, the editor's Inspector), the editor's Checks, the plugin's log and the tests.
//
// Decisions:
// - Decided by what a category can match (its `match`, as written), not by its id, so user and OCF categories
//   work too. A category can go on a display when it can match at least one item the display holds:
//     rack (any weapon, large display case): weapons; crossbows only where the trigger has a crossbow node
//     rack (shield): shields          rack (daggers only): daggers
//     rack (no dagger or bow): weapons but daggers, bows and crossbows
//     mannequin: armor it wears (no shields, rings, necklaces)      bookshelf: books
//     item plaque, display case: nothing (they are made for their own items)
// - What a category can match: its `formTypes` (absent: any type), narrowed by the predicates only some types
//   have (`weaponTypes`: weapons; `armorTypes`: armor; `bookTeaches`: books; `alchemy`, `soulGem`, `ammo`:
//   none a display holds). Weapons by `weaponTypes` (and `keywordsAny`/`keywordsAll` made of WeapType*
//   keywords). Armor: shields are heavy or light armor; a keyword with "shield" in its name marks a shield,
//   and the slot keywords of the game and OCF (ArmorHelmet, ClothingRing, OCF_HandTypeRing...) mark pieces
//   that are no shield; rings and necklaces are no mannequin's. A predicate core cannot see through
//   (`forms`, `plugins`, other keywords) does not narrow: when in doubt, it fits.
// - A grouping category (no `match`) matches nothing itself. A pattern ("weapons.*", "*") fits when one of the
//   categories it matches fits; a pattern that matches no known category has no verdict (nullopt: the
//   unknown-category lint is about it).
// - Nothing is ever deleted: a category that can't go on a display stays in the layout or template. It never
//   puts anything on that display, as the display only ever takes what it holds (RackTakes, MannequinTakes,
//   BookshelfTakes in Display.h), so it is ignored; a display whose every category is such takes nothing.
//   ValidateDisplayCategories warns about it (the editor's Checks), the plugin logs it once per session.

#include <compare>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dova/core/Category.h"
#include "dova/core/Diagnostics.h"
#include "dova/core/DisplayKind.h"
#include "dova/core/Layout.h"

namespace dova::core
{
	// What a display holds: its kind and, for a rack, its type.
	struct DisplayFit
	{
		DisplayKind             kind{ DisplayKind::Rack };
		std::optional<RackType> rack;  // Rack: its RackType; nullopt: unknown (anything some rack takes)
		bool                    crossbows{ true };  // Rack: its trigger has a crossbow node (the Unofficial Patch's); true when unknown

		friend auto operator<=>(const DisplayFit&, const DisplayFit&) = default;
	};

	// Whether a category with this `match` can go on the display (see above). A grouping category: false.
	[[nodiscard]] bool CategoryFitsDisplay(const CategoryMatch& a_match, const DisplayFit& a_fit);
	// Whether a pattern can go on the display: one of the categories it matches fits. nullopt: it matches no
	// known category (other than groups).
	[[nodiscard]] std::optional<bool> PatternFitsDisplay(const CategorySet& a_categories, std::string_view a_pattern, const DisplayFit& a_fit);
	// A display's category picker, as patterns, in the set's order: per top-level group "<root>.*", then each
	// category below it ("<id>.*" for a grouping one), only those that fit. Empty for item plaques and cases.
	[[nodiscard]] std::vector<std::string> DisplayCategoryChoices(const CategorySet& a_categories, const DisplayFit& a_fit);
	// The patterns of `a_patterns` that can't go on the display (known categories only), in order.
	[[nodiscard]] std::vector<std::string> UnfitDisplayCategories(const CategorySet& a_categories, std::span<const std::string> a_patterns,
		const DisplayFit& a_fit);

	// "weapon rack", "shield rack", "dagger rack", "mannequin", "bookshelf", "item plaque"...
	[[nodiscard]] std::string DisplayFitNoun(const DisplayFit& a_fit);
	// "Soul gems can't go on a weapon rack" (the pattern's category name).
	[[nodiscard]] std::string UnfitCategoryMessage(const CategorySet& a_categories, std::string_view a_pattern, const DisplayFit& a_fit);

	// The display an entry is about, as its scripts make it; nullopt: unknown (the entry's `kind` is used then,
	// with the rack type unknown).
	using DisplayFitOf = std::function<std::optional<DisplayFit>(const DisplayBinding& a_entry)>;
	// Warns, per display entry that is not ignored, about each category it can't take ("display <ref> (Weapon
	// Plaque): Soul gems can't go on a weapon rack"; "...; nothing goes on it" when none fits). `a_fitOf` may be
	// empty. Item plaques and cases: only when the entry has no `kind` (ValidateDisplays covers the others).
	void ValidateDisplayCategories(const Layout& a_layout, const CategorySet& a_categories, const DisplayFitOf& a_fitOf, std::string_view a_source,
		Diagnostics& a_diags);
}
