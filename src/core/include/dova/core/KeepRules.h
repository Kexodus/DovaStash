#pragma once

// Keep rules (DESIGN §8.3): which player-inventory items Deposit leaves in the inventory, and which ones
// Restock brings back (§8.7).
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
//
// Restock (DESIGN §8.7): a quota rule with `restock` also tops the player up to its quota from storage.
// - Goal: the rule's quota minus what the player carries of it, counted exactly as Deposit counts against
//   the quota (stacks kept whole by `always` or the hard rule that match the rule's selectors, plus the
//   stacks the rule claims). So a Deposit right after a Restock leaves everything restocked on the player.
// - Owner: a stored stack belongs to the first quota rule that matches it (layers in order, rules in file
//   order). A `never` rule of the same or an earlier layer that matches it (and no `always` rule of that
//   layer does) means it is never restocked. `always` does not block: it only says what you carry stays.
//   A stack whose owning rule does not restock is not restocked by a later rule either. Quest, non-playable
//   items and items that come back when stored never belong to a rule.

#include <compare>
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

	// True when a quota rule in force restocks: the layout's (`a_layoutKeep`, may be null), and the global
	// ones when the layout has no keep block or inherits. The hotkey menus offer Restock only then.
	[[nodiscard]] bool RestockApplies(const KeepConfig& a_global, const KeepConfig* a_layoutKeep);

	class KeepRules
	{
	public:
		KeepRules() = default;

		// `a_layoutKeep` may be null (layout without a keep block).
		[[nodiscard]] static KeepRules Compile(const KeepConfig& a_global, const KeepConfig* a_layoutKeep, Diagnostics& a_diags);

		// Number of items to keep for each stack, parallel to `a_inventory` (0 .. count). When `a_why` is
		// given it receives, parallel too, the rule that kept items of each stack ("always equipped
		// (global)", "quota alchemy.potions 5 (layout)", "quest item"...), empty where nothing is kept.
		[[nodiscard]] std::vector<std::int32_t> ComputeKeepCounts(std::span<const ItemInfo> a_inventory, const CategorySet& a_categories,
			std::vector<std::string>* a_why = nullptr) const;

		// Restock (see the header comment). A rule is named by its layer and its index in that layer.
		struct RuleId
		{
			std::size_t layer{ 0 };
			std::size_t quota{ 0 };

			friend auto operator<=>(const RuleId&, const RuleId&) = default;
		};
		struct RestockGoal
		{
			RuleId       rule;
			std::int32_t quota{ 0 };  // the rule's N
			std::int32_t have{ 0 };   // what the player carries of it now
			std::string  label;       // "quota alchemy.potions 10 (layout)"

			[[nodiscard]] std::int32_t Missing() const noexcept { return quota > have ? quota - have : 0; }
		};

		// True when any quota rule restocks.
		[[nodiscard]] bool Restocks() const noexcept;
		// Every rule with `restock`, in evaluation order, with what `a_inventory` (the player's) holds of it.
		[[nodiscard]] std::vector<RestockGoal> RestockGoals(std::span<const ItemInfo> a_inventory, const CategorySet& a_categories) const;
		// The restocking rule a stored stack belongs to, if any.
		[[nodiscard]] std::optional<RuleId> RestockRuleFor(const ItemInfo& a_item, const CategorySet& a_categories) const;

		struct Quota
		{
			std::optional<std::string>              category;
			std::optional<std::vector<std::string>> keywordsAny;  // lower-cased
			std::optional<std::vector<FormRef>>     forms;
			std::int32_t                            quota{ 0 };
			bool                                    restock{ false };
		};

		struct Layer
		{
			std::vector<KeepSelector> always;
			std::vector<KeepSelector> never;
			std::vector<Quota>        quotas;
			std::string               name;  // "layout" or "global", for explanations
		};

		[[nodiscard]] std::span<const Layer> Layers() const noexcept { return _layers; }

	private:
		std::vector<Layer> _layers;  // evaluation order
	};
}
