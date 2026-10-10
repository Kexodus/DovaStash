#pragma once

// Keep rules (DESIGN §8.3): which player-inventory items Deposit leaves in the inventory.
//
// Semantics (decisions):
// - Hard rule: quest items and non-playable items are always kept, whatever the configuration.
// - Layers: the layout's keep block (if any) is consulted first; the global defaults are consulted
//   after it when the layout has no keep block or its `inherit` is true (the default). The first layer
//   with an applicable rule decides, so a layout can override a global keep with `never`.
// - Within a layer: `always` beats `never` (keeping is the safe side), `never` beats quotas, and the
//   first matching quota rule (in file order) claims the stack.
// - Category patterns (in always/never/quota.category) are tested against the item's *primary*
//   category (CategorySet::Classify). Items without a category only match "*".
// - Quota rules select stacks by every selector they have (category AND keywordsAny AND forms).
//   "Keep up to N in the inventory": stacks already kept by an `always` rule (or the hard rule) that
//   match the quota's selectors count against N first. The remaining allowance is filled from the
//   stacks claimed by the rule, ordered by unit value (desc), then count (desc), then form, then
//   stackId, then input order — i.e. the most valuable copies are kept. A partially kept stack keeps
//   part of its count and the rest is deposited.
// - Unknown tokens / malformed entries are ignored (reported once at compile time as warnings).

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "dova/core/Category.h"
#include "dova/core/Diagnostics.h"
#include "dova/core/FormRef.h"
#include "dova/core/Item.h"
#include "dova/core/Layout.h"

namespace dova::core
{
	inline const FormRef kGoldForm{ "Skyrim.esm", 0x0000000F };
	inline const FormRef kLockpickForm{ "Skyrim.esm", 0x0000000A };
	inline const FormRef kTorchForm{ "Skyrim.esm", 0x0001D4EC };

	// Global defaults: always = equipped, favorited, quest, gold, lockpick, torch.
	[[nodiscard]] KeepConfig DefaultKeepConfig();

	class KeepRules
	{
	public:
		KeepRules() = default;

		// `a_layoutKeep` may be null (layout without a keep block).
		[[nodiscard]] static KeepRules Compile(const KeepConfig& a_global, const KeepConfig* a_layoutKeep, Diagnostics& a_diags);

		// Number of items to keep for each stack, parallel to `a_inventory` (0 .. count).
		[[nodiscard]] std::vector<std::int32_t> ComputeKeepCounts(std::span<const ItemInfo> a_inventory, const CategorySet& a_categories) const;

		struct Quota
		{
			std::optional<std::string>              category;
			std::optional<std::vector<std::string>> keywordsAny;  // lower-cased
			std::optional<std::vector<FormRef>>     forms;
			std::int32_t                            quota{ 0 };
		};

		struct Layer
		{
			std::vector<KeepSelector> always;
			std::vector<KeepSelector> never;
			std::vector<Quota>        quotas;
		};

		[[nodiscard]] std::span<const Layer> Layers() const noexcept { return _layers; }

	private:
		std::vector<Layer> _layers;  // evaluation order
	};
}
