#pragma once

// Every armor set in the load order (DESIGN §8.4 "Mannequins"), for the mannequin set picker (hotkey
// menu and the Menu Framework "Armor sets" page) and for mannequins with a chosen set.
//
// - Built once, lazily, on the main thread, the first time a chosen set or a picker needs it: every
//   playable ARMO a mannequin can wear (no shields, no jewelry other than circlets) from
//   TESDataHandler, grouped by core::BuildArmorSets. Enchanted copies (ARMO with a template, "Ebony
//   Armor of Health") are left out and count as the armor they are made from. Each piece's `family` is
//   the first ingredient of its tempering recipe (COBJ at CraftingSmithingArmorTable), which ties a
//   mod's pieces that share neither a material keyword nor a name.
// - The built index never changes, so it is shared read-only with the render thread (Peek).
// - Layouts name a set by one of its pieces (`set`, §6.3); SetOf finds the set of any piece.

#include "Game/Displays.h"
#include "Home/HomeRegistry.h"

namespace dova::game
{
	struct ArmorIndex
	{
		std::vector<core::ArmorPiece>                  pieces;
		std::vector<core::ArmorSet>                    sets;   // sorted by name
		std::unordered_map<core::FormRef, std::size_t> setOf;  // piece -> index into sets
	};

	// Main thread: the index, built on the first call (logs its size once, at debug).
	[[nodiscard]] std::shared_ptr<const ArmorIndex> GetArmorIndex();
	// Any thread: the index if it is built, else null.
	[[nodiscard]] std::shared_ptr<const ArmorIndex> PeekArmorIndex();

	// Main thread. The set of an armor form: its own, or (an enchanted copy) that of the armor it is made
	// from. nullopt for anything else.
	[[nodiscard]] std::optional<std::size_t> SetOf(const ArmorIndex& a_index, const core::FormRef& a_form);
	// The indexed form for `a_form` (itself, or the armor an enchanted copy is made from).
	[[nodiscard]] std::optional<core::FormRef> IndexedForm(const ArmorIndex& a_index, const core::FormRef& a_form);

	// Main thread. Armor the player has for the set picker: carried (not worn, favorited or a quest item),
	// stored in the home's containers, and worn by `a_mannequin` (if given); as indexed forms.
	[[nodiscard]] std::set<core::FormRef> HaveArmor(const ArmorIndex& a_index, const home::Home& a_home, const core::FormRef* a_mannequin);

	// Main thread. For a mannequin whose layout entry has a `set`: the set's pieces (with any enchanted
	// copies of them among `a_stocks` and `a_holds`), the slots taken by what it wears, `chosen`.
	void ApplyChosenSet(core::DisplaySpot& a_spot, const core::DisplayBinding& a_binding, std::span<const core::DisplayStock> a_stocks,
		const std::vector<RE::TESBoundObject*>& a_holds);

	// game::DisplayFull, except that a mannequin with a chosen set is full only once it can take no more
	// of it (core::SetMannequinOpen). Main thread.
	[[nodiscard]] bool DisplayFullFor(const FoundDisplay& a_display, const core::DisplayBinding* a_binding);

	// The name of the set `a_set` names ("Ebony"); the piece's id when it is not loaded.
	[[nodiscard]] std::string SetName(const ArmorIndex& a_index, const core::FormRef& a_set);
}
