#pragma once

// Learning helpers beyond the single Learn (SortPlanner.h): category suggestions after a few learns,
// and pruning learned entries a binding has made redundant. DESIGN §8.6.
//
// Suggestion rule: when `threshold` or more items learned for one container share a primary category
// that the container does not bind yet, the plugin asks once ("Send all Books to End Table?"). Core
// decides *what* to suggest from plain data; the plugin classifies the learned items (it needs the
// game's forms) and asks the question.
// - The container must be a non-ignored, non-master container of the layout.
// - Not suggested: a category one of the container's own patterns already covers; one listed in
//   `Layout::declined` for that container (the player said No); one another non-ignored container
//   binds by its exact id (adding it here too would only compete on list order). A wider pattern
//   elsewhere (`books.*` on the bookshelf) does not block it: the exact binding wins by specificity, so
//   the suggestion is a clean exception ("skill books go to the end table").
// - The category with the most learned items is suggested first.

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dova/core/Category.h"
#include "dova/core/FormRef.h"
#include "dova/core/Item.h"
#include "dova/core/Layout.h"

namespace dova::core
{
	struct CategorySuggestion
	{
		FormRef      container;
		std::string  category;  // exact category id to bind
		std::int32_t count{ 0 };  // learned items of that category pointing at the container
	};

	// Primary categories of the items learned for one container with their counts, most items first
	// (what DisplayHome::learnedCategories holds). Items in no category are left out.
	using LearnedCategories = std::vector<std::pair<std::string, std::int32_t>>;

	// (category -> count) to the sorted vector above: most items first, ties in id order.
	[[nodiscard]] LearnedCategories SortLearnedCategories(const std::map<std::string, std::int32_t>& a_counts);

	// The suggestion for `a_container`, if any (see the rule above). `a_threshold` <= 0 never suggests.
	[[nodiscard]] std::optional<CategorySuggestion> SuggestCategory(const Layout& a_layout, const FormRef& a_container,
		const LearnedCategories& a_learned, std::int32_t a_threshold);

	// True when the player turned this suggestion down for this container.
	[[nodiscard]] bool IsDeclined(const Layout& a_layout, const FormRef& a_container, std::string_view a_category);
	// Remembers a No. Returns false when it was already remembered.
	bool DeclineSuggestion(Layout& a_layout, const FormRef& a_container, std::string_view a_category);
	// Forgets a No (the panel's "Ask again"), or every No for the container when `a_category` is empty.
	// Returns how many were forgotten.
	std::size_t ClearDeclined(Layout& a_layout, const FormRef& a_container, std::string_view a_category = {});

	// Item data for a learned base form, or nullopt when it cannot be looked up (plugin missing).
	using ItemLookup = std::function<std::optional<ItemInfo>(const FormRef&)>;

	// A Yes: binds the exact category to the container (unless one of its patterns covers it already)
	// and prunes the learned entries this makes redundant (below). Also forgets a No for the pair.
	// Returns the number of learned entries pruned; nullopt when the container is the master, ignored
	// or not in the layout (nothing changed).
	std::optional<std::size_t> AcceptSuggestion(Layout& a_layout, const FormRef& a_container, std::string_view a_category,
		const CategorySet& a_categories, const ItemLookup& a_lookup);

	// Drops every learned entry pointing at `a_container` whose item would reach that container anyway
	// (its explicit or category destination without the entry): the same rule the smart Learn applies
	// to one item. Items the lookup cannot resolve are kept (never lose data over a missing plugin).
	std::size_t PruneRedundantLearned(Layout& a_layout, const FormRef& a_container, const CategorySet& a_categories, const ItemLookup& a_lookup);
}
