#pragma once

// Every constructible-object recipe (COBJ) of the load order as a core::crafting::RecipeIndex (DESIGN §8.5).
// Built on first use, once per session (the load order cannot change after kDataLoaded); the counts are
// logged once at debug. Main thread only.

#include <dova/core/Crafting.h>

namespace dova::game
{
	[[nodiscard]] const core::crafting::RecipeIndex& Recipes();

	// A keyword as the recipe index names it: its EditorID, or its FormRef text when it has none.
	[[nodiscard]] std::string KeywordName(const RE::BGSKeyword* a_keyword);

	// The names (KeywordName) of every keyword on `a_form`.
	[[nodiscard]] std::vector<std::string> KeywordNames(const RE::BGSKeywordForm* a_form);
}
