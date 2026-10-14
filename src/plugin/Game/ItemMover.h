#pragma once

// Executes core::Moves (DESIGN §8.2). Main thread only.
//
// Every move is one TESObjectREFR::RemoveItem(object, count, reason, extraList, target) call, so the
// stack's ExtraDataList (tempering, enchantment, custom name, stolen ownership, soul) travels with it.
// Before each move the stack is re-validated against the live inventory (same ExtraDataList pointer
// still present with at least `count` items, not worn/favorited/quest); anything doubtful is skipped.
// After each move the source/target counts are compared and any discrepancy is logged as an error.
//
// Plain copies (no extra list) of an object whose tempered / enchanted / renamed / worn copy stays in the
// source move with RemoveItem and no extra list. The engine takes plain copies first, as the container
// menu does; every extra-data stack of the source is checked afterwards, and if one left anyway it is put
// back and such moves stop for the session.
//
// Items that come back: when an object Dova Stash stored in a container returns from that container to
// the player within a few seconds (a script hands it back, e.g. a token that opens a prompt), it is never
// moved again this session (ItemInfo::returns).
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

		// Marks moves made outside Execute (putting items on displays) as Dova Stash's own while it lives.
		struct SuppressScope
		{
			SuppressScope() noexcept { BeginSuppress(); }
			~SuppressScope() { EndSuppress(); }
			SuppressScope(const SuppressScope&) = delete;
			SuppressScope& operator=(const SuppressScope&) = delete;
		};

		// `a_object` went from `a_container` to the player, not by Dova Stash. True when Dova Stash stored it
		// there moments ago: from now on it stays where it is (Returns). Any thread.
		static bool NoteReturned(RE::FormID a_object, RE::FormID a_container);
		[[nodiscard]] static bool Returns(RE::FormID a_object);
		// A move made outside Execute (the display fill taking an item back out of a container): forgets the
		// store it undoes, so the container-changed event it causes is not taken for a script handing the
		// item back (NoteReturned). Call before the RemoveItem. Any thread.
		static void NoteTaken(RE::FormID a_object, RE::FormID a_from);

	private:
		static void BeginSuppress() noexcept;
		static void EndSuppress() noexcept;
	};
}
