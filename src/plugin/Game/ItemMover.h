#pragma once

// Executes core::Moves (DESIGN §8.2). Main thread only.
//
// Every move is one TESObjectREFR::RemoveItem(object, count, reason, extraList, target) call, so the
// stack's ExtraDataList (tempering, enchantment, custom name, stolen ownership, soul) travels with it.
// Before each move the stack is re-validated against the live inventory (same ExtraDataList pointer
// still present with at least `count` items, not worn/favorited/quest); anything doubtful is skipped.
// After each move the source/target counts are compared and any discrepancy is logged as an error.
//
// Remove reasons (mirroring what ContainerMenu uses): container -> player = kRemove ("take"),
// anything -> container = kStoreInContainer ("store").

#include "Game/ItemAdapter.h"

namespace dova::game
{
	struct MoveReport
	{
		std::int32_t                items{ 0 };    // total item count moved
		std::int32_t                stacks{ 0 };   // RemoveItem calls that succeeded
		std::int32_t                skipped{ 0 };  // stacks not moved (stale, protected, unresolvable)
		std::int32_t                errors{ 0 };   // count mismatches after a move (see log)
		std::set<RE::FormID>        targets;       // distinct target refs
		std::set<RE::FormID>        sources;       // distinct source refs
	};

	// Resolves a move endpoint to a live reference (player or container).
	using EndpointResolver = std::function<RE::TESObjectREFR*(const core::ContainerRef&)>;

	class ItemMover
	{
	public:
		[[nodiscard]] static MoveReport Execute(std::span<const core::Move> a_moves, const StackTable& a_stacks, const EndpointResolver& a_resolve);

		// True while we are moving items and for a short grace period afterwards, so that our own
		// TESContainerChangedEvents are not learned (events may be dispatched after RemoveItem returns).
		[[nodiscard]] static bool IsSuppressing() noexcept;

	private:
		static void BeginSuppress() noexcept;
		static void EndSuppress() noexcept;
	};
}
