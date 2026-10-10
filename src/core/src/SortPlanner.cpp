#include "dova/core/SortPlanner.h"

#include <algorithm>

namespace dova::core
{
	SortPlanner::SortPlanner(const Layout& a_layout, const CategorySet& a_categories, ContainerAvailability a_available) :
		_layout(a_layout),
		_categories(a_categories),
		_available(std::move(a_available))
	{}

	bool SortPlanner::CanMove(const ItemInfo& a_item) noexcept
	{
		// Nameless items are script tokens (mods track state with unnamed MISC items); they are
		// invisible in container menus, so moving one would make it vanish for the player.
		return a_item.count > 0 && !a_item.questItem && a_item.playable && !a_item.name.empty();
	}

	bool SortPlanner::IsSortSource(const FormRef& a_container) const
	{
		if (_layout.IsMaster(a_container)) {
			return true;
		}
		const auto* c = _layout.FindContainer(a_container);
		return c && !c->ignored && (!c->categories.empty() || !c->forms.empty());
	}

	bool SortPlanner::IsAvailable(const FormRef& a_container) const
	{
		return !_available || _available(a_container);
	}

	bool SortPlanner::IsManaged(const FormRef& a_container) const
	{
		if (_layout.IsMaster(a_container)) {
			return true;
		}
		const auto* c = _layout.FindContainer(a_container);
		return c && !c->ignored;
	}

	std::optional<Destination> SortPlanner::ResolveDestination(const ItemInfo& a_item) const
	{
		// 1. Explicit per-item binding.
		for (const auto& c : _layout.containers) {
			if (!c.ignored && std::ranges::find(c.forms, a_item.form) != c.forms.end() && IsAvailable(c.ref)) {
				return Destination{ c.ref, Destination::Reason::Explicit, {} };
			}
		}

		// 2. Learned destination.
		if (const auto it = _layout.learned.find(a_item.form); it != _layout.learned.end()) {
			const auto* c = _layout.FindContainer(it->second);
			if (c && !c->ignored && !_layout.IsMaster(c->ref) && IsAvailable(c->ref)) {
				return Destination{ c->ref, Destination::Reason::Learned, {} };
			}
		}

		// 3. Categories, best first; most specific binding pattern wins.
		for (const auto& category : _categories.MatchAll(a_item)) {
			const ContainerBinding* best = nullptr;
			int                     bestScore = -1;
			for (const auto& c : _layout.containers) {
				if (c.ignored) {
					continue;
				}
				for (const auto& pattern : c.categories) {
					if (!CategoryPatternMatches(pattern, category)) {
						continue;
					}
					const int score = CategoryPatternSpecificity(pattern);
					if (score > bestScore && IsAvailable(c.ref)) {
						best = &c;
						bestScore = score;
					}
				}
			}
			if (best) {
				return Destination{ best->ref, Destination::Reason::Category, category };
			}
		}

		// 4. Master.
		if (_layout.master && IsAvailable(*_layout.master)) {
			return Destination{ *_layout.master, Destination::Reason::Master, {} };
		}
		return std::nullopt;
	}

	std::vector<Move> SortPlanner::Deposit(std::span<const ItemInfo> a_inventory, const KeepRules& a_keep) const
	{
		std::vector<Move> moves;
		const auto        keep = a_keep.ComputeKeepCounts(a_inventory, _categories);
		for (std::size_t i = 0; i < a_inventory.size(); ++i) {
			const auto& item = a_inventory[i];
			if (!CanMove(item)) {
				continue;
			}
			const auto count = item.count - keep[i];
			if (count <= 0) {
				continue;
			}
			if (const auto dest = ResolveDestination(item)) {
				moves.push_back(Move{ item.stackId, item.form, count, ContainerRef::Player(), ContainerRef(dest->container) });
			}
		}
		return moves;
	}

	std::vector<Move> SortPlanner::Distribute(std::span<const ItemInfo> a_masterContents) const
	{
		std::vector<Move> moves;
		if (!_layout.master || !IsAvailable(*_layout.master)) {
			return moves;
		}
		const auto& master = *_layout.master;
		for (const auto& item : a_masterContents) {
			if (!CanMove(item)) {
				continue;
			}
			const auto dest = ResolveDestination(item);
			if (dest && dest->reason != Destination::Reason::Master && dest->container != master) {
				moves.push_back(Move{ item.stackId, item.form, item.count, ContainerRef(master), ContainerRef(dest->container) });
			}
		}
		return moves;
	}

	std::vector<Move> SortPlanner::SortAll(std::span<const ContainerContents> a_containers) const
	{
		std::vector<Move> moves;
		for (const auto& contents : a_containers) {
			if (!IsSortSource(contents.ref) || !IsAvailable(contents.ref)) {
				continue;
			}
			for (const auto& item : contents.items) {
				if (!CanMove(item)) {
					continue;
				}
				const auto dest = ResolveDestination(item);
				if (dest && dest->container != contents.ref) {
					moves.push_back(Move{ item.stackId, item.form, item.count, ContainerRef(contents.ref), ContainerRef(dest->container) });
				}
			}
		}
		return moves;
	}

	std::vector<Move> SortPlanner::Retrieve(std::span<const ContainerContents> a_containers, std::string_view a_pattern) const
	{
		std::vector<Move> moves;
		if (!IsValidCategoryPattern(a_pattern)) {
			return moves;
		}
		for (const auto& contents : a_containers) {
			if (!IsManaged(contents.ref) || !IsAvailable(contents.ref)) {
				continue;
			}
			for (const auto& item : contents.items) {
				if (!CanMove(item)) {
					continue;
				}
				const auto primary = _categories.Classify(item);
				const bool match = primary ? CategoryPatternMatches(a_pattern, *primary) : a_pattern == "*";
				if (match) {
					moves.push_back(Move{ item.stackId, item.form, item.count, ContainerRef(contents.ref), ContainerRef::Player() });
				}
			}
		}
		return moves;
	}

	LearnResult Learn(Layout& a_layout, const FormRef& a_form, const FormRef& a_container)
	{
		if (a_layout.IsMaster(a_container)) {
			return LearnResult::RejectedMaster;
		}
		if (const auto* c = a_layout.FindContainer(a_container)) {
			if (c->ignored) {
				return LearnResult::RejectedIgnored;
			}
		} else {
			ContainerBinding binding;
			binding.ref = a_container;
			a_layout.containers.push_back(std::move(binding));
		}

		const auto it = a_layout.learned.find(a_form);
		if (it == a_layout.learned.end()) {
			a_layout.learned.emplace(a_form, a_container);
			return LearnResult::Recorded;
		}
		if (it->second == a_container) {
			return LearnResult::Unchanged;
		}
		it->second = a_container;
		return LearnResult::Updated;
	}

	LearnResult Learn(Layout& a_layout, const ItemInfo& a_item, const FormRef& a_container, const CategorySet& a_categories)
	{
		if (a_item.questItem || !a_item.playable || a_item.name.empty()) {
			return LearnResult::RejectedItem;
		}
		if (a_layout.IsMaster(a_container)) {
			return LearnResult::RejectedMaster;
		}
		if (const auto* c = a_layout.FindContainer(a_container); c && c->ignored) {
			return LearnResult::RejectedIgnored;
		}

		// What would the destination be without a learned entry for this form?
		std::optional<FormRef> previous;
		if (const auto it = a_layout.learned.find(a_item.form); it != a_layout.learned.end()) {
			previous = it->second;
			a_layout.learned.erase(it);
		}
		const auto natural = SortPlanner(a_layout, a_categories).ResolveDestination(a_item);
		if (natural && natural->container == a_container &&
			(natural->reason == Destination::Reason::Explicit || natural->reason == Destination::Reason::Category)) {
			return previous ? LearnResult::Removed : LearnResult::Unchanged;
		}
		if (previous) {
			a_layout.learned.emplace(a_item.form, *previous);  // restore, then let the basic overload decide
		}
		return Learn(a_layout, a_item.form, a_container);
	}
}
