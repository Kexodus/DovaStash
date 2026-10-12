#pragma once

// Sort planner (DESIGN §7, §8.1): pure functions that turn inventories + a layout into a list of moves.
// Core never moves anything; the plugin executes the Moves on the main thread.
//
// Destination precedence for an item (first that applies):
//   1. explicit per-item binding: a non-ignored container whose "forms" lists the item's base form
//   2. learned destination for the base form, if it is a non-ignored, non-master container of the layout
//   3. categories: the item's matching categories are tried best-first (CategorySet::MatchAll); for the
//      first category that any non-ignored container binds, the container with the most specific
//      matching pattern wins (exact > deeper wildcard > shallower wildcard > "*"), ties go to the
//      container listed first
//   4. the master container ("homeless")
// Candidates rejected by the availability callback (unloaded, disabled, failed safety checks) are
// skipped; if nothing is available the item has no destination and is not moved.
//
// Never moved by any operation: quest items, non-playable items, nameless items (script tokens that
// would be invisible in a container), stacks with count <= 0.
//
// Operations:
// - Deposit:    player -> destination, leaving what the keep rules keep (partial stacks allowed).
// - Distribute: master -> destinations; homeless items stay in the master.
// - SortAll:    every sort source (available master, or available non-ignored container with at
//               least one category binding or explicit "forms" entry) -> its destination. Homeless
//               items found outside the master go to the master. Containers without bindings (e.g.
//               ones added by Learn: keepsake chests) are targets only and are never emptied. Ignored
//               containers and containers not in the layout are neither sources nor targets.
// - Retrieve:   items in managed containers whose primary category matches the pattern -> player.
//               "*" retrieves everything, including homeless items.
// Moves never have from == to. Output order follows input order (containers, then stacks).

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dova/core/Category.h"
#include "dova/core/FormRef.h"
#include "dova/core/Item.h"
#include "dova/core/KeepRules.h"
#include "dova/core/Layout.h"

namespace dova::core
{
	// A container endpoint: a container reference, or the player's inventory.
	class ContainerRef
	{
	public:
		ContainerRef() = default;  // the player
		ContainerRef(FormRef a_ref) :
			_ref(std::move(a_ref))
		{}

		[[nodiscard]] static ContainerRef Player() { return {}; }

		[[nodiscard]] bool           IsPlayer() const noexcept { return !_ref.has_value(); }
		[[nodiscard]] const FormRef& Ref() const { return _ref.value(); }  // throws for the player
		[[nodiscard]] std::string    ToString() const { return _ref ? _ref->ToString() : std::string("<player>"); }

		friend bool operator==(const ContainerRef& a_lhs, const ContainerRef& a_rhs) = default;

	private:
		std::optional<FormRef> _ref;
	};

	struct Move
	{
		std::uint32_t stackId{ 0 };
		FormRef       form;
		std::int32_t  count{ 0 };
		ContainerRef  from;
		ContainerRef  to;

		friend bool operator==(const Move& a_lhs, const Move& a_rhs) = default;
	};

	struct ContainerContents
	{
		FormRef               ref;
		std::vector<ItemInfo> items;
	};

	struct Destination
	{
		enum class Reason : std::uint8_t
		{
			Explicit,
			Learned,
			Category,
			Master
		};

		FormRef     container;
		Reason      reason{ Reason::Master };
		std::string category;  // the category that decided it (Reason::Category only)
	};

	// Why an operation moves a stack, or leaves it where it is (for logs). Parallel to the operation's input.
	struct StackDecision
	{
		enum class Outcome : std::uint8_t
		{
			Move,           // `moved` items go to `destination`; `kept` items stay (Deposit keep rules)
			Kept,           // Deposit: keep rules keep the whole stack (`keepRule` says which)
			Empty,          // count <= 0
			Quest,          // quest item: never moved
			NonPlayable,    // non-playable: never moved
			Nameless,       // nameless (script token): never moved
			Returns,        // a script sent it back the last time it was stored: never moved
			NoDestination,  // no available container takes it, not even the master
			Homeless        // Distribute: its destination is the master, so it stays there
		};

		Outcome                    outcome{ Outcome::Empty };
		std::int32_t               moved{ 0 };
		std::int32_t               kept{ 0 };
		std::string                keepRule;  // Deposit: the keep rule behind `kept`
		std::optional<Destination> destination;

		[[nodiscard]] static std::string_view OutcomeName(Outcome a_outcome) noexcept;
	};

	[[nodiscard]] std::string_view DestinationReasonName(Destination::Reason a_reason) noexcept;

	// Returns false for containers that cannot be used right now. Empty = everything is available.
	using ContainerAvailability = std::function<bool(const FormRef&)>;

	// Holds references to the layout and categories: both must outlive the planner.
	class SortPlanner
	{
	public:
		SortPlanner(const Layout& a_layout, const CategorySet& a_categories, ContainerAvailability a_available = {});

		[[nodiscard]] std::optional<Destination> ResolveDestination(const ItemInfo& a_item) const;

		// `a_explain`, when given, receives one StackDecision per input stack.
		[[nodiscard]] std::vector<Move> Deposit(std::span<const ItemInfo> a_inventory, const KeepRules& a_keep,
			std::vector<StackDecision>* a_explain = nullptr) const;
		[[nodiscard]] std::vector<Move> Distribute(std::span<const ItemInfo> a_masterContents,
			std::vector<StackDecision>* a_explain = nullptr) const;
		[[nodiscard]] std::vector<Move> SortAll(std::span<const ContainerContents> a_containers) const;
		[[nodiscard]] std::vector<Move> Retrieve(std::span<const ContainerContents> a_containers, std::string_view a_pattern) const;

		// Master, or a non-ignored container of the layout (availability not considered).
		[[nodiscard]] bool IsManaged(const FormRef& a_container) const;
		[[nodiscard]] bool IsAvailable(const FormRef& a_container) const;

		// Master, or a non-ignored container with category bindings or explicit forms (SortAll sources).
		[[nodiscard]] bool IsSortSource(const FormRef& a_container) const;

		// Quest items, non-playable items, nameless items and empty stacks are never moved.
		[[nodiscard]] static bool CanMove(const ItemInfo& a_item) noexcept;

	private:
		const Layout&         _layout;
		const CategorySet&    _categories;
		ContainerAvailability _available;
	};

	enum class LearnResult : std::uint8_t
	{
		Recorded,         // new learned entry
		Updated,          // entry now points to a different container
		Unchanged,        // already learned there, or (smart overload) already the natural destination
		Removed,          // smart overload: a stale entry was removed because the container is the natural destination
		RejectedMaster,   // never learn the master
		RejectedIgnored,  // never learn ignored containers
		RejectedItem      // smart overload: quest / non-playable / nameless items are not learned
	};

	// Records `a_form` -> `a_container` in layout.learned. A container not yet in the layout is added
	// as a managed container without categories (the plugin must have checked it is safe to use).
	LearnResult Learn(Layout& a_layout, const FormRef& a_form, const FormRef& a_container);

	// Like Learn, but does not record anything when the container already is the item's explicit or
	// category destination (and removes a stale learned entry in that case). This keeps learned data
	// minimal so later category re-binding still takes effect for those items.
	LearnResult Learn(Layout& a_layout, const ItemInfo& a_item, const FormRef& a_container, const CategorySet& a_categories);
}
