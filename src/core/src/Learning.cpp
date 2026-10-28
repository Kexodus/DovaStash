#include "dova/core/Learning.h"

#include <algorithm>

#include "dova/core/SortPlanner.h"

namespace dova::core
{
	namespace
	{
		// One of the container's own patterns routes the category to it.
		bool Covers(const ContainerBinding& a_binding, std::string_view a_category)
		{
			return std::ranges::any_of(a_binding.categories, [&](const std::string& p) { return CategoryPatternMatches(p, a_category); });
		}

		// Another non-ignored container binds this exact id.
		bool BoundExactlyElsewhere(const Layout& a_layout, const FormRef& a_container, std::string_view a_category)
		{
			for (const auto& c : a_layout.containers) {
				if (c.ignored || c.ref == a_container) {
					continue;
				}
				if (std::ranges::any_of(c.categories, [&](const std::string& p) { return IEquals(p, a_category); })) {
					return true;
				}
			}
			return false;
		}
	}

	LearnedCategories SortLearnedCategories(const std::map<std::string, std::int32_t>& a_counts)
	{
		LearnedCategories sorted(a_counts.begin(), a_counts.end());  // id order
		std::ranges::stable_sort(sorted, [](const auto& a_lhs, const auto& a_rhs) { return a_lhs.second > a_rhs.second; });
		return sorted;
	}

	std::optional<CategorySuggestion> SuggestCategory(const Layout& a_layout, const FormRef& a_container, const LearnedCategories& a_learned, std::int32_t a_threshold)
	{
		if (a_threshold <= 0 || a_layout.IsAnyMaster(a_container)) {
			return std::nullopt;
		}
		const auto* binding = a_layout.FindContainer(a_container);
		if (!binding || binding->ignored || binding->sell) {
			return std::nullopt;  // never "send all Books to the sell chest?": a Yes there would sell them (DESIGN §8.8)
		}
		for (const auto& [category, count] : a_learned) {
			if (count < a_threshold) {
				break;  // sorted: nothing further reaches the threshold
			}
			if (category.empty() || Covers(*binding, category) || IsDeclined(a_layout, a_container, category) ||
				BoundExactlyElsewhere(a_layout, a_container, category)) {
				continue;
			}
			return CategorySuggestion{ a_container, category, count };
		}
		return std::nullopt;
	}

	bool IsDeclined(const Layout& a_layout, const FormRef& a_container, std::string_view a_category)
	{
		const auto it = a_layout.declined.find(a_container);
		return it != a_layout.declined.end() &&
		       std::ranges::any_of(it->second, [&](const std::string& id) { return IEquals(id, a_category); });
	}

	bool DeclineSuggestion(Layout& a_layout, const FormRef& a_container, std::string_view a_category)
	{
		if (a_category.empty() || IsDeclined(a_layout, a_container, a_category)) {
			return false;
		}
		a_layout.declined[a_container].emplace_back(a_category);
		return true;
	}

	std::size_t ClearDeclined(Layout& a_layout, const FormRef& a_container, std::string_view a_category)
	{
		const auto it = a_layout.declined.find(a_container);
		if (it == a_layout.declined.end()) {
			return 0;
		}
		std::size_t removed = 0;
		if (a_category.empty()) {
			removed = it->second.size();
			it->second.clear();
		} else {
			removed = std::erase_if(it->second, [&](const std::string& id) { return IEquals(id, a_category); });
		}
		if (it->second.empty()) {
			a_layout.declined.erase(it);
		}
		return removed;
	}

	std::size_t PruneRedundantLearned(Layout& a_layout, const FormRef& a_container, const CategorySet& a_categories, const ItemLookup& a_lookup)
	{
		std::vector<FormRef> keys;
		for (const auto& [form, container] : a_layout.learned) {
			if (container == a_container) {
				keys.push_back(form);
			}
		}
		std::size_t pruned = 0;
		for (const auto& key : keys) {
			const auto it = a_layout.learned.find(key);
			if (it == a_layout.learned.end()) {
				continue;
			}
			auto item = a_lookup ? a_lookup(key) : std::nullopt;
			if (!item) {
				continue;  // cannot be classified now: kept
			}
			item->form = key;  // the entry's key is what the planner would look up
			item->family.reset();
			const auto saved = *it;
			a_layout.learned.erase(it);
			const auto natural = SortPlanner(a_layout, a_categories).ResolveDestination(*item);
			if (natural && natural->container == a_container &&
				(natural->reason == Destination::Reason::Explicit || natural->reason == Destination::Reason::Category)) {
				++pruned;
			} else {
				a_layout.learned.insert(saved);
			}
		}
		return pruned;
	}

	std::optional<std::size_t> AcceptSuggestion(Layout& a_layout, const FormRef& a_container, std::string_view a_category, const CategorySet& a_categories, const ItemLookup& a_lookup)
	{
		if (a_category.empty() || a_layout.IsAnyMaster(a_container)) {
			return std::nullopt;
		}
		auto* binding = a_layout.FindContainer(a_container);
		if (!binding || binding->ignored) {
			return std::nullopt;
		}
		if (!Covers(*binding, a_category)) {
			binding->categories.emplace_back(a_category);
		}
		ClearDeclined(a_layout, a_container, a_category);
		return PruneRedundantLearned(a_layout, a_container, a_categories, a_lookup);
	}
}
