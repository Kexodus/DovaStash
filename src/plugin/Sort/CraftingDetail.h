#pragma once

// Shared by the two ways of supplying a workbench (DESIGN §8.5): Move (Sort/CraftingSupply.cpp) and Craft from
// storage (Sort/CraftingStorage.cpp). SKSE tasks, and Craft from storage's consumption on the crafting menu's
// thread with the session's own copy of the home (never the live Home there).

#include "Game/ItemAdapter.h"
#include "Game/ItemMover.h"

#include <dova/core/Crafting.h>

namespace dova::home
{
	struct Home;
}

namespace dova::sort::crafting::detail
{
	// A reference's stacks with their identity within the form (core::crafting::Stack::key).
	[[nodiscard]] std::vector<core::crafting::Stack> Stacks(RE::TESObjectREFR* a_ref, game::StackTable& a_table);

	// The home's master and non-ignored containers that pass the safety checks right now (DESIGN §9), in layout
	// order (master first). In a multi-cell home also those of its other cells that can be reached from here.
	// `a_only`, when given: just these containers (still checked), in layout order.
	[[nodiscard]] std::vector<core::crafting::Stored> SnapshotStored(const home::Home& a_home, game::StackTable& a_table,
		const std::vector<core::FormRef>* a_only = nullptr);

	// The player, or a home container that passes the safety checks. Only used within the call that built it.
	[[nodiscard]] game::EndpointResolver Resolver(RE::TESObjectREFR* a_player, const home::Home& a_home);

	// What the station's furniture asks for, or an empty request.
	[[nodiscard]] core::crafting::SupplyRequest RequestFor(const RE::TESFurniture* a_furniture);

	[[nodiscard]] std::string FormName(const core::FormRef& a_form);
	[[nodiscard]] std::string Plural(std::int32_t a_n, std::string_view a_word);

	// Craft from storage could not keep its promise: from the next task on, the station is supplied by Move for
	// the rest of this visit (the storage session ends, the materials are brought). Any thread.
	void FallBackToMove(RE::FormID a_station, std::string a_why);
}
