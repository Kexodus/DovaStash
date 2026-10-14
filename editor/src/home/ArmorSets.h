#pragma once

// The armor sets of a load order (DESIGN §8.4 "Mannequins"), built as the DLL's game::ArmorIndex builds them
// in game, from the plugins instead: every playable ARMO a mannequin can wear (core::MannequinTakes), enchanted
// copies (an ARMO with a template, TNAM) left out, each piece's `family` the first ingredient of its tempering
// recipe (COBJ at CraftingSmithingArmorTable), grouped by core::BuildArmorSets. The mannequin set picker of
// the role editor; a layout names a set by any of its pieces (`set`).

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "dova/core/ArmorSets.h"
#include "esp/Items.h"
#include "esp/LoadOrder.h"

namespace dova::editor::home
{
	struct ArmorSetIndex
	{
		std::vector<core::ArmorPiece>                  pieces;
		std::vector<core::ArmorSet>                    sets;   // sorted by name
		std::unordered_map<core::FormRef, std::size_t> setOf;  // piece -> index into sets

		// The set a layout's `set` names (any of its pieces).
		[[nodiscard]] std::optional<std::size_t> SetOf(const core::FormRef& a_piece) const;
	};

	[[nodiscard]] ArmorSetIndex BuildArmorSetIndex(esp::LoadOrder& a_lo, const esp::ItemCatalog& a_catalog);
}
