#include "Hooks/CraftingHooks.h"

#include "Sort/CraftingStorage.h"

#include <MinHook.h>

namespace dova::hooks::crafting
{
	namespace
	{
		namespace storage = sort::crafting::storage;

		using ItemCount_t = std::int32_t (*)(RE::InventoryChanges*, RE::TESBoundObject*, void*);
		using ListCount_t = std::int32_t (*)(RE::TESObjectREFR*, bool, bool);
		using ListAt_t = RE::InventoryEntryData* (*)(RE::TESObjectREFR*, std::int32_t, bool);
		// TESObjectREFR::RemoveItem (vtable 0x56) returns an ObjectRefHandle by value: MSVC passes the result
		// pointer after `this` and returns it.
		using RemoveItem_t = RE::ObjectRefHandle* (*)(RE::TESObjectREFR*, RE::ObjectRefHandle*, RE::TESBoundObject*, std::int32_t, RE::ITEM_REMOVE_REASON,
			RE::ExtraDataList*, RE::TESObjectREFR*, const RE::NiPoint3*, const RE::NiPoint3*);

		ItemCount_t  g_itemCount{ nullptr };
		ListCount_t  g_listCount{ nullptr };
		ListAt_t     g_listAt{ nullptr };
		RemoveItem_t g_removeItem{ nullptr };

		bool g_tried{ false };
		bool g_installed{ false };

		bool IsPlayer(const RE::TESObjectREFR* a_ref) noexcept
		{
			return a_ref && a_ref == RE::PlayerCharacter::GetSingleton();
		}

		// InventoryChanges' count of one object (SE 15869 / AE 16109).
		std::int32_t ItemCount(RE::InventoryChanges* a_changes, RE::TESBoundObject* a_object, void* a_filter)
		{
			const auto count = g_itemCount(a_changes, a_object, a_filter);
			if (!a_changes || !IsPlayer(a_changes->owner)) {
				return count;
			}
			return count + Added(a_object);
		}

		// TESObjectREFR::GetInventoryItemCount (SE 19274 / AE 19700).
		std::int32_t ListCount(RE::TESObjectREFR* a_ref, bool a_viewing, bool a_playable)
		{
			const auto count = g_listCount(a_ref, a_viewing, a_playable);
			if (!IsPlayer(a_ref) || !storage::ListsHooked()) {
				return count;
			}
			return storage::ListCount(count, a_viewing, a_playable);
		}

		// TESObjectREFR::GetInventoryItemAt (SE 19273 / AE 19699). The caller owns what it gets.
		RE::InventoryEntryData* ListAt(RE::TESObjectREFR* a_ref, std::int32_t a_index, bool a_viewing)
		{
			if (!IsPlayer(a_ref) || !storage::ListsHooked() || a_index < 0) {
				return g_listAt(a_ref, a_index, a_viewing);
			}
			// The player's own entries come first. Counted now, with the arguments the list was counted with,
			// so an index is never mapped with a stale count.
			const auto [viewing, playable] = storage::PlayerListArgs();
			std::int32_t own = 0;
			{
				const storage::Busy busy;
				own = g_listCount(a_ref, viewing, playable);
			}
			if (a_index < own) {
				return storage::ListMerge(g_listAt(a_ref, a_index, a_viewing));
			}
			return storage::ListExtra(a_index - own);
		}

		RE::ObjectRefHandle* RemoveItem(RE::TESObjectREFR* a_this, RE::ObjectRefHandle* a_result, RE::TESBoundObject* a_item, std::int32_t a_count,
			RE::ITEM_REMOVE_REASON a_reason, RE::ExtraDataList* a_extra, RE::TESObjectREFR* a_moveTo, const RE::NiPoint3* a_dropLoc,
			const RE::NiPoint3* a_rotate)
		{
			// Crafting consumes with kRemove, no target and no drop point. A specific extra list is always one of
			// the player's own (the hooks never hand out a container's), so the game handles that alone.
			const bool crafting = a_reason == RE::ITEM_REMOVE_REASON::kRemove && a_item && a_count > 0 && !a_extra && !a_moveTo && !a_dropLoc &&
			                      IsPlayer(a_this) && storage::Hooked();
			if (crafting) {
				if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME)) {
					return storage::Consume(a_item, a_count, [&]() {
						return g_removeItem(a_this, a_result, a_item, a_count, a_reason, a_extra, a_moveTo, a_dropLoc, a_rotate);
					});
				}
			}
			return g_removeItem(a_this, a_result, a_item, a_count, a_reason, a_extra, a_moveTo, a_dropLoc, a_rotate);
		}

		template <class T>
		bool Detour(std::uintptr_t a_address, T a_hook, T& a_original, std::string_view a_name)
		{
			const auto status = MH_CreateHook(reinterpret_cast<void*>(a_address), reinterpret_cast<void*>(a_hook), reinterpret_cast<void**>(&a_original));
			if (status != MH_OK) {
				logger::warn("Crafting hooks: {} at {:X} could not be hooked ({})", a_name, a_address, MH_StatusToString(status));
				return false;
			}
			return true;
		}
	}

	bool Available() noexcept
	{
		return !REL::Module::IsVR();
	}

	bool Installed() noexcept
	{
		return g_installed;
	}

	std::int32_t Added(const RE::TESBoundObject* a_object) noexcept
	{
		return a_object && storage::Hooked() ? storage::Stored(a_object) : 0;
	}

	bool EnsureInstalled()
	{
		if (g_tried) {
			return g_installed;
		}
		g_tried = true;
		if (!Available()) {
			// The SE IDs are not all in the VR Address Library, and a missing ID ends the game: never resolved here.
			logger::info("Crafting hooks: not on VR (crafting from storage falls back to bringing the materials)");
			return false;
		}
		try {
			const auto itemCount = REL::Relocation<std::uintptr_t>{ RELOCATION_ID(15869, 16109) }.address();
			const auto listCount = REL::Relocation<std::uintptr_t>{ RELOCATION_ID(19274, 19700) }.address();
			const auto listAt = REL::Relocation<std::uintptr_t>{ RELOCATION_ID(19273, 19699) }.address();
			if (!itemCount || !listCount || !listAt) {
				logger::warn("Crafting hooks: an Address Library ID did not resolve; crafting from storage is off");
				return false;
			}
			if (const auto status = MH_Initialize(); status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
				logger::warn("Crafting hooks: MinHook could not start ({})", MH_StatusToString(status));
				return false;
			}
			const bool created = Detour(itemCount, &ItemCount, g_itemCount, "InventoryChanges item count") &&
			                     Detour(listCount, &ListCount, g_listCount, "GetInventoryItemCount") &&
			                     Detour(listAt, &ListAt, g_listAt, "GetInventoryItemAt");
			if (!created || MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
				MH_DisableHook(MH_ALL_HOOKS);
				MH_Uninitialize();  // removes every hook created above
				g_itemCount = nullptr;
				g_listCount = nullptr;
				g_listAt = nullptr;
				logger::warn("Crafting hooks: not installed; crafting from storage is off");
				return false;
			}
			REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_PlayerCharacter[0] };
			g_removeItem = reinterpret_cast<RemoveItem_t>(vtable.write_vfunc(0x56, &RemoveItem));
			g_installed = true;
			logger::info("Crafting hooks: installed (item count {:X}, inventory list {:X}/{:X}, PlayerCharacter::RemoveItem)", itemCount, listCount, listAt);
		} catch (const std::exception& e) {
			logger::warn("Crafting hooks: not installed ({})", e.what());
			return false;
		}
		return g_installed;
	}
}
