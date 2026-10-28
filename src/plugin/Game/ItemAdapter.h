#pragma once

// Game inventory -> core::ItemInfo (the core <-> plugin boundary, Item.h).
//
// One ItemInfo per stack: every ExtraDataList of an InventoryEntryData (with its ExtraCount) is its own
// stack, and the plain remainder (total - sum of extra-list counts) is one more. Each stack gets an
// opaque stackId that maps back to (owner ref, TESBoundObject*, ExtraDataList*) in a StackTable, so the
// ItemMover can move exactly that stack and preserve its extra data.
//
// A StackTable is only meaningful within the main-thread task that built it: ExtraDataList pointers
// are re-validated against the live inventory before every move (see ItemMover).

namespace dova::game
{
	struct StackRef
	{
		RE::FormID          owner{ 0 };  // reference holding the stack (0x14 = player)
		RE::TESBoundObject* object{ nullptr };
		RE::ExtraDataList*  extra{ nullptr };  // nullptr = plain remainder
		std::int32_t        count{ 0 };
	};

	class StackTable
	{
	public:
		std::uint32_t                 Add(const StackRef& a_stack);
		[[nodiscard]] const StackRef* Find(std::uint32_t a_stackId) const;
		[[nodiscard]] std::size_t     Size() const noexcept { return _stacks.size(); }

	private:
		std::uint32_t                               _next{ 1 };
		std::unordered_map<std::uint32_t, StackRef> _stacks;
	};

	// Snapshot of `a_ref`'s inventory (player or container). Items of unknown type (ItemType::Other) are
	// left out entirely. Dynamic (0xFF) base forms get an invalid FormRef: they can be sorted by type/
	// keywords but are never learned or written to JSON.
	[[nodiscard]] std::vector<core::ItemInfo> SnapshotInventory(RE::TESObjectREFR* a_ref, StackTable& a_table);

	// Base-form part of an ItemInfo (type, kinds, keywords, name, weight, value, base enchantment,
	// playable). Objects with an empty base name (script tokens) are marked non-playable so they are
	// never moved. Returns false for objects that are not storable item types.
	bool FillBaseInfo(core::ItemInfo& a_info, RE::TESBoundObject* a_object);

	// Live state of one stack in `a_ref`'s inventory, or found=false if it no longer exists.
	struct LiveStack
	{
		bool         found{ false };
		std::int32_t count{ 0 };
		bool         isProtected{ false };  // worn (also the plain copies of an equipped ammo), favorited or quest alias (never moved)
	};
	[[nodiscard]] LiveStack FindLiveStack(RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_object, RE::ExtraDataList* a_extra);

	// Total count of `a_object` in `a_ref` (inventory changes + base container), as GetInventory sees it.
	[[nodiscard]] std::int32_t LiveCount(RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_object);

	[[nodiscard]] bool IsProtectedExtraList(RE::ExtraDataList* a_extra);

	// Every extra-data stack (tempered, enchanted, renamed, worn...) of `a_object` in `a_ref` that still
	// holds at least one item.
	[[nodiscard]] std::vector<RE::ExtraDataList*> ExtraStacks(RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_object);

	// An extra list that makes its copies different from plain ones (tempered, enchanted, renamed, poisoned,
	// charged, a filled soul gem, worn, favorited, a quest alias, scripted...). A list that only carries a
	// count, a unique id, an owner, a leveled-list marker or an original reference is how the engine keeps
	// plain items next to a container's base-record stock: its copies are plain for every rule of ours.
	[[nodiscard]] bool IsDistinctExtraList(RE::ExtraDataList* a_extra);

	// ExtraStacks narrowed to the distinct ones: the copies a plain move must never take (ItemMover).
	[[nodiscard]] std::vector<RE::ExtraDataList*> SpecialStacks(RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_object);
}
