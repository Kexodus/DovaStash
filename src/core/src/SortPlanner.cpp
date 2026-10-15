#include "dova/core/SortPlanner.h"

#include <algorithm>

namespace dova::core
{
	namespace
	{
		// The reason CanMove refuses an item, if it does.
		std::optional<StackDecision::Outcome> Unmovable(const ItemInfo& a_item) noexcept
		{
			using enum StackDecision::Outcome;
			if (a_item.count <= 0) {
				return Empty;
			}
			if (a_item.questItem) {
				return Quest;
			}
			if (a_item.name.empty()) {
				return Nameless;  // before NonPlayable: the plugin also marks nameless items non-playable
			}
			if (!a_item.playable) {
				return NonPlayable;
			}
			if (a_item.returns) {
				return Returns;
			}
			return std::nullopt;
		}
	}

	std::string_view StackDecision::OutcomeName(Outcome a_outcome) noexcept
	{
		switch (a_outcome) {
		case Outcome::Move:
			return "move";
		case Outcome::Kept:
			return "kept";
		case Outcome::Empty:
			return "empty stack";
		case Outcome::Quest:
			return "quest item";
		case Outcome::NonPlayable:
			return "non-playable";
		case Outcome::Nameless:
			return "nameless";
		case Outcome::Returns:
			return "comes back when stored";
		case Outcome::NoDestination:
			return "no destination";
		case Outcome::Homeless:
			return "homeless";
		default:
			return "?";
		}
	}

	std::string_view DestinationReasonName(Destination::Reason a_reason) noexcept
	{
		switch (a_reason) {
		case Destination::Reason::Explicit:
			return "explicit";
		case Destination::Reason::Learned:
			return "learned";
		case Destination::Reason::Category:
			return "category";
		case Destination::Reason::Master:
			return "homeless";
		default:
			return "?";
		}
	}

	SortPlanner::SortPlanner(const Layout& a_layout, const CategorySet& a_categories, ContainerAvailability a_available) :
		_layout(a_layout),
		_categories(a_categories),
		_available(std::move(a_available))
	{}

	bool SortPlanner::CanMove(const ItemInfo& a_item) noexcept
	{
		// Nameless items are script tokens (mods track state with unnamed MISC items); they are
		// invisible in container menus, so moving one would make it vanish for the player.
		return !Unmovable(a_item).has_value();
	}

	bool SortPlanner::IsSortSource(const FormRef& a_container) const
	{
		if (_layout.IsMaster(a_container)) {
			return true;
		}
		const auto* c = _layout.FindContainer(a_container);
		// The sell chest is a target only: what the player put there by hand stays until it is sold (DESIGN §8.8).
		return c && !c->ignored && !_layout.IsSellChest(a_container) && (!c->categories.empty() || !c->forms.empty());
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
		// 1. Explicit per-item binding: the item itself, then the item it is made from (its family root).
		const auto explicitFor = [&](const FormRef& a_form) -> const ContainerBinding* {
			for (const auto& c : _layout.containers) {
				if (!c.ignored && std::ranges::find(c.forms, a_form) != c.forms.end() && IsAvailable(c.ref)) {
					return &c;
				}
			}
			return nullptr;
		};
		if (const auto* c = explicitFor(a_item.form)) {
			return Destination{ c->ref, Destination::Reason::Explicit, {} };
		}
		if (a_item.family) {
			if (const auto* c = explicitFor(*a_item.family)) {
				return Destination{ c->ref, Destination::Reason::Explicit, {} };
			}
		}

		// 2. Learned destination: the item itself, then its family root.
		const auto learnedFor = [&](const FormRef& a_form) -> const ContainerBinding* {
			const auto it = _layout.learned.find(a_form);
			if (it == _layout.learned.end()) {
				return nullptr;
			}
			const auto* c = _layout.FindContainer(it->second);
			return c && !c->ignored && !_layout.IsMaster(c->ref) && IsAvailable(c->ref) ? c : nullptr;
		};
		if (const auto* c = learnedFor(a_item.form)) {
			return Destination{ c->ref, Destination::Reason::Learned, {} };
		}
		if (a_item.family) {
			if (const auto* c = learnedFor(*a_item.family)) {
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
					// Equal patterns: list order, except that a sell chest wins over a storage container (the
					// junk category it binds on creation would otherwise never reach it in a template home, §8.8).
					const bool wins = score > bestScore ||
					                  (score == bestScore && best && _layout.IsSellChest(c.ref) && !_layout.IsSellChest(best->ref));
					if (wins && IsAvailable(c.ref)) {
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

	std::vector<Move> SortPlanner::Deposit(std::span<const ItemInfo> a_inventory, const KeepRules& a_keep,
		std::vector<StackDecision>* a_explain) const
	{
		std::vector<Move>          moves;
		std::vector<std::string>   why;
		const auto                 keep = a_keep.ComputeKeepCounts(a_inventory, _categories, a_explain ? &why : nullptr);
		std::vector<StackDecision> decisions(a_explain ? a_inventory.size() : 0);
		for (std::size_t i = 0; i < a_inventory.size(); ++i) {
			const auto&    item = a_inventory[i];
			StackDecision  scratch;
			StackDecision& d = a_explain ? decisions[i] : scratch;
			if (const auto refused = Unmovable(item)) {
				d.outcome = *refused;
				continue;
			}
			d.kept = keep[i];
			if (a_explain) {
				d.keepRule = std::move(why[i]);
			}
			const auto count = item.count - keep[i];
			if (count <= 0) {
				d.outcome = StackDecision::Outcome::Kept;
				continue;
			}
			d.destination = ResolveDestination(item);
			if (!d.destination) {
				d.outcome = StackDecision::Outcome::NoDestination;
				continue;
			}
			d.outcome = StackDecision::Outcome::Move;
			d.moved = count;
			moves.push_back(Move{ item.stackId, item.form, count, ContainerRef::Player(), ContainerRef(d.destination->container) });
		}
		if (a_explain) {
			*a_explain = std::move(decisions);
		}
		return moves;
	}

	std::vector<Move> SortPlanner::Distribute(std::span<const ItemInfo> a_masterContents, std::vector<StackDecision>* a_explain) const
	{
		std::vector<Move>          moves;
		std::vector<StackDecision> decisions(a_explain ? a_masterContents.size() : 0);
		const bool                 masterOk = _layout.master && IsAvailable(*_layout.master);
		for (std::size_t i = 0; i < a_masterContents.size(); ++i) {
			const auto&    item = a_masterContents[i];
			StackDecision  scratch;
			StackDecision& d = a_explain ? decisions[i] : scratch;
			if (!masterOk) {
				d.outcome = StackDecision::Outcome::NoDestination;
				continue;
			}
			if (const auto refused = Unmovable(item)) {
				d.outcome = *refused;
				continue;
			}
			d.destination = ResolveDestination(item);
			if (!d.destination) {
				d.outcome = StackDecision::Outcome::NoDestination;
			} else if (d.destination->reason == Destination::Reason::Master || d.destination->container == *_layout.master) {
				d.outcome = StackDecision::Outcome::Homeless;
			} else {
				d.outcome = StackDecision::Outcome::Move;
				d.moved = item.count;
				moves.push_back(Move{ item.stackId, item.form, item.count, ContainerRef(*_layout.master), ContainerRef(d.destination->container) });
			}
		}
		if (a_explain) {
			*a_explain = std::move(decisions);
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
			if (!IsManaged(contents.ref) || _layout.IsSellChest(contents.ref) || !IsAvailable(contents.ref)) {
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

	std::vector<Move> SortPlanner::Restock(std::span<const ItemInfo> a_inventory, std::span<const ContainerContents> a_containers,
		const KeepRules& a_keep, std::vector<RestockLine>* a_explain) const
	{
		std::vector<Move> moves;
		if (a_explain) {
			a_explain->clear();
		}
		const auto goals = a_keep.RestockGoals(a_inventory, _categories);
		if (goals.empty()) {
			return moves;
		}

		// Every stored stack that belongs to a restocking rule.
		struct Candidate
		{
			std::size_t      container{ 0 };  // index into a_containers
			const ItemInfo*  item{ nullptr };
			bool             home{ false };   // it sits in its own destination
			KeepRules::RuleId rule;
		};
		std::vector<Candidate> candidates;
		for (std::size_t c = 0; c < a_containers.size(); ++c) {
			const auto& contents = a_containers[c];
			if (!IsManaged(contents.ref) || _layout.IsSellChest(contents.ref) || !IsAvailable(contents.ref)) {  // never from the sell chest (DESIGN §8.8)
				continue;
			}
			for (const auto& item : contents.items) {
				if (!CanMove(item)) {
					continue;
				}
				if (const auto rule = a_keep.RestockRuleFor(item, _categories)) {
					const auto dest = ResolveDestination(item);
					candidates.push_back(Candidate{ c, &item, dest && dest->container == contents.ref, *rule });
				}
			}
		}
		std::ranges::stable_sort(candidates, [](const Candidate& a_lhs, const Candidate& a_rhs) {
			const auto& l = *a_lhs.item;
			const auto& r = *a_rhs.item;
			if (l.value != r.value) {
				return l.value > r.value;
			}
			if (l.customized != r.customized) {
				return !l.customized;
			}
			if (a_lhs.home != a_rhs.home) {
				return a_lhs.home;
			}
			if (a_lhs.container != a_rhs.container) {
				return a_lhs.container < a_rhs.container;
			}
			if (l.form != r.form) {
				return l.form < r.form;
			}
			if (l.count != r.count) {
				return l.count > r.count;
			}
			return l.stackId < r.stackId;
		});

		for (const auto& goal : goals) {
			std::int32_t missing = goal.Missing();
			std::int32_t taken = 0;
			for (const auto& cand : candidates) {
				if (missing <= 0) {
					break;
				}
				if (cand.rule != goal.rule) {
					continue;
				}
				const auto n = std::min(missing, cand.item->count);
				moves.push_back(Move{ cand.item->stackId, cand.item->form, n, ContainerRef(a_containers[cand.container].ref), ContainerRef::Player() });
				missing -= n;
				taken += n;
			}
			if (a_explain) {
				a_explain->push_back(RestockLine{ goal.label, goal.quota, goal.have, taken });
			}
		}
		return moves;
	}

	LearnResult Learn(Layout& a_layout, const FormRef& a_form, const FormRef& a_container, LearnUndo* a_undo)
	{
		if (a_layout.IsMaster(a_container)) {
			return LearnResult::RejectedMaster;
		}
		bool added = false;
		if (const auto* c = a_layout.FindContainer(a_container)) {
			if (c->ignored) {
				return LearnResult::RejectedIgnored;
			}
		} else {
			ContainerBinding binding;
			binding.ref = a_container;
			a_layout.containers.push_back(std::move(binding));
			added = true;
		}
		if (a_undo) {
			a_undo->key = a_form;
			a_undo->container = a_container;
			a_undo->containerAdded = added;
			a_undo->previous.reset();
		}

		const auto it = a_layout.learned.find(a_form);
		if (it == a_layout.learned.end()) {
			a_layout.learned.emplace(a_form, a_container);
			return LearnResult::Recorded;
		}
		if (a_undo) {
			a_undo->previous = it->second;
		}
		if (it->second == a_container) {
			return LearnResult::Unchanged;
		}
		it->second = a_container;
		return LearnResult::Updated;
	}

	LearnResult Learn(Layout& a_layout, const ItemInfo& a_item, const FormRef& a_container, const CategorySet& a_categories, LearnUndo* a_undo)
	{
		if (a_item.questItem || !a_item.playable || a_item.name.empty() || a_item.returns) {
			return LearnResult::RejectedItem;
		}
		if (a_layout.IsMaster(a_container)) {
			return LearnResult::RejectedMaster;
		}
		if (const auto* c = a_layout.FindContainer(a_container); c && c->ignored) {
			return LearnResult::RejectedIgnored;
		}

		// A variant is learned under its root item (the family), so every variant follows.
		const FormRef& key = a_item.family ? *a_item.family : a_item.form;

		// What would the destination be without a learned entry for this item (its own, or its family's)?
		std::optional<FormRef> previous;
		if (const auto it = a_layout.learned.find(key); it != a_layout.learned.end()) {
			previous = it->second;
			a_layout.learned.erase(it);
		}
		std::optional<std::pair<FormRef, FormRef>> variantWas;
		if (a_item.family) {
			if (const auto it = a_layout.learned.find(a_item.form); it != a_layout.learned.end()) {
				variantWas = *it;
				a_layout.learned.erase(it);  // older data for the variant itself: the family's entry takes over
			}
		}
		if (a_undo) {
			*a_undo = LearnUndo{ .key = key, .previous = previous, .variantWas = variantWas, .container = a_container };
		}
		const auto natural = SortPlanner(a_layout, a_categories).ResolveDestination(a_item);
		if (natural && natural->container == a_container &&
			(natural->reason == Destination::Reason::Explicit || natural->reason == Destination::Reason::Category)) {
			return previous || variantWas ? LearnResult::Removed : LearnResult::Unchanged;
		}
		if (previous) {
			a_layout.learned.emplace(key, *previous);  // restore, then let the basic overload decide
		}
		LearnUndo basic;
		const auto result = Learn(a_layout, key, a_container, &basic);
		if (a_undo) {
			a_undo->containerAdded = basic.containerAdded;
		}
		return result;
	}

	void UndoLearn(Layout& a_layout, const LearnUndo& a_undo)
	{
		if (a_undo.previous) {
			a_layout.learned[a_undo.key] = *a_undo.previous;
		} else if (const auto it = a_layout.learned.find(a_undo.key); it != a_layout.learned.end() && it->second == a_undo.container) {
			a_layout.learned.erase(it);
		}
		if (a_undo.variantWas) {
			a_layout.learned.try_emplace(a_undo.variantWas->first, a_undo.variantWas->second);
		}
		if (!a_undo.containerAdded) {
			return;
		}
		const auto* c = a_layout.FindContainer(a_undo.container);
		const bool  stillUsed = std::ranges::any_of(a_layout.learned, [&](const auto& a_pair) { return a_pair.second == a_undo.container; });
		if (c && !c->ignored && c->categories.empty() && c->forms.empty() && c->unparsedForms.empty() && !stillUsed) {
			std::erase_if(a_layout.containers, [&](const ContainerBinding& b) { return b.ref == a_undo.container; });
		}
	}
}
