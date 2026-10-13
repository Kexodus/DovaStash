#pragma once

// Every item record of a load order as Dova Stash's core sees an inventory stack (core::ItemInfo): the same
// fields the DLL's ItemAdapter fills from the game (type, name, keywords, weapon and armor types, book
// teaches, alchemy kind, soul gem state, ammo kind, enchanted, playable), read from the plugins instead. Used
// by the "where items go" preview (core::PreviewWhereItemsGo) and to classify loose items for draft
// suggestions.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "dova/core/Item.h"
#include "esp/LoadOrder.h"

namespace dova::editor::esp
{
	// One item record: nullopt for records that are no inventory item (a light that cannot be carried).
	[[nodiscard]] std::optional<core::ItemInfo> ReadItem(LoadOrder& a_lo, const FormKey& a_key, const RecordRef& a_rec);

	struct ItemCatalog
	{
		std::vector<core::ItemInfo> items;  // playable, named items, sorted by type then name
		FormKeyMap<std::size_t>     index;  // form -> items index (every readable item, playable or not, is looked up through ReadItem)
		std::size_t                 skipped{ 0 };  // non-playable or nameless records left out
	};

	// Reads every WEAP, ARMO, AMMO, ALCH, INGR, BOOK, SCRL, SLGM, MISC, KEYM and carryable LIGH record.
	[[nodiscard]] ItemCatalog BuildItemCatalog(LoadOrder& a_lo);
}
