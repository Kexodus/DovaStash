#pragma once

// The sell chest (DESIGN §8.7): pure logic. A container marked "sell" (Layout::IsSellChest) receives items like
// any other; what is in it is sold for gold only on a clear trigger: the player confirms "Sell now" (the
// default), or turned on timed selling ([Selling] iAutoSellHours) and the items have been in it that long. The
// plugin snapshots the chest, asks PlanSale which stacks go and for how much, removes them on the main thread
// and gives the player the gold. Core never touches the game.
//
// Rules (never sold, whatever the trigger; the stack stays in the chest):
// - quest items, non-playable or nameless items, items that come back when stored, worn or favorited copies;
// - gold itself; forms without a load-order identity (potions and poisons the player made: a buy-back could not
//   bring the same form back);
// - customized copies (tempered, enchanted by the player, renamed, poisoned), soul-trapped gems and copies with
//   extra data the plugin does not recognise (Copy::Special), stolen copies (Copy::Stolen): only plain copies are
//   sold, so the buy-back gives back exactly what went;
// - anything an `always` keep rule protects (KeepRules::AlwaysKeeps: by default gold, lockpicks and torches);
// - items worth nothing (value 0, or a price that rounds to 0).
//
// Price per item (UnitPrice), the vanilla barter formula without a merchant:
//   factor = fBarterMax - (fBarterMax - fBarterMin) * clamp(Speech, 0, 100) / 100      (3.3 at Speech 0, 2.0 at 100)
//   mult   = min(fBarterSellMax, perks * (1 + FortifyBarter / 100) / factor) * percent / 100      (fBarterSellMax: 1 when absent)
//   price  = floor(value * mult), at least 1 for an item worth something
// `perks` is the product of the player's Mod Sell Prices perk entries (Haggling: 1.1 to 1.3); `percent` is the
// player's setting (100 = the game's price). Values that are not finite or out of range fall back to the vanilla
// defaults, so a broken game setting can never make a sale pay more than the item's value.

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dova/core/Category.h"
#include "dova/core/FormRef.h"
#include "dova/core/Item.h"
#include "dova/core/KeepRules.h"

namespace dova::core::sell
{
	// What the game's barter menu would pay, as far as it can be known without a merchant.
	struct Barter
	{
		float        barterMin{ 2.0f };  // GMST fBarterMin: the price factor at Speech 100
		float        barterMax{ 3.3f };  // GMST fBarterMax: the price factor at Speech 0
		float        sellMax{ 1.0f };    // GMST fBarterSellMax when the game has it: the most a sale pays, as a fraction of the value
		float        speech{ 15.0f };    // the player's Speech skill
		float        perks{ 1.0f };      // the player's Mod Sell Prices perk entries, multiplied (1 = none)
		float        fortify{ 0.0f };    // Fortify Barter, in percent (the SpeechcraftModifier actor value)
		std::int32_t percent{ 100 };     // [Selling] iSellPricePercent
	};

	// The price factor for the player's Speech (2.0 - 3.3 with vanilla settings).
	[[nodiscard]] double PriceFactor(const Barter& a_barter) noexcept;
	// The fraction of an item's value a sale pays (percent included).
	[[nodiscard]] double SellMultiplier(const Barter& a_barter) noexcept;
	// What one item of value `a_value` sells for (0 for an item worth nothing).
	[[nodiscard]] std::int32_t UnitPrice(std::int32_t a_value, const Barter& a_barter) noexcept;

	// A copy's extra data, as the plugin sees it.
	enum class Copy : std::uint8_t
	{
		Plain,   // no extra data besides a count or a unique id
		Stolen,  // carries an owner
		Special  // anything else: tempered, enchanted by the player, renamed, poisoned, a trapped soul, unknown data
	};

	struct Stack
	{
		ItemInfo item;  // `value` is the value of one item (the plugin fills it with the game's own value)
		Copy     copy{ Copy::Plain };
		float    perks{ 1.0f };  // the Mod Sell Prices perk entries for this item (perk conditions can name the item); times Barter::perks
	};

	enum class Verdict : std::uint8_t
	{
		Sell,
		Empty,        // count <= 0
		Quest,        // quest item
		NonPlayable,  // non-playable or nameless
		Returns,      // comes back when stored
		Protected,    // worn or favorited
		Gold,         // gold itself
		Dynamic,      // no load-order identity (made by the player)
		Customized,   // Copy::Special or ItemInfo::customized
		Stolen,       // Copy::Stolen
		Kept,         // an `always` keep rule protects it
		Worthless     // sells for nothing
	};
	// Plain words for logs and the panel ("quest item", "stolen", "kept by a keep rule"...).
	[[nodiscard]] std::string_view VerdictName(Verdict a_verdict) noexcept;

	// `a_why` receives the keep rule for Verdict::Kept.
	[[nodiscard]] Verdict Judge(const Stack& a_stack, const KeepRules& a_keep, const CategorySet& a_categories, const Barter& a_barter,
		std::string* a_why = nullptr);

	using Counts = std::map<FormRef, std::int32_t>;  // copies per base form

	// One stack sold.
	struct Line
	{
		std::size_t   index{ 0 };  // into the planned stacks
		std::uint32_t stackId{ 0 };
		FormRef       form;
		std::string   name;
		std::int32_t  count{ 0 };
		std::int32_t  unitPrice{ 0 };

		[[nodiscard]] std::int64_t Total() const noexcept { return static_cast<std::int64_t>(count) * unitPrice; }
	};

	struct Plan
	{
		std::vector<Line>               lines;     // input order
		std::vector<Verdict>            verdicts;  // parallel to the input
		std::vector<std::string>        why;       // parallel to the input: the keep rule for Verdict::Kept
		std::map<Verdict, std::int32_t> stays;     // copies not sold, per reason (Sell itself only for what a limit held back)
		std::int32_t                    items{ 0 };
		std::int64_t                    gold{ 0 };

		[[nodiscard]] bool Empty() const noexcept { return lines.empty(); }
		// Copies sold per form.
		[[nodiscard]] Counts Sold() const;
	};

	// What to sell from the chest's `a_stacks`. `a_limit` (timed selling): at most that many copies of each form,
	// taken from the stacks in input order; forms it does not list are not sold.
	[[nodiscard]] Plan PlanSale(std::span<const Stack> a_stacks, const KeepRules& a_keep, const CategorySet& a_categories, const Barter& a_barter,
		const Counts* a_limit = nullptr);

	// "Iron Dagger x5, Tankard x3 +4 more": the lines that bring the most gold first, at most `a_max` names.
	[[nodiscard]] std::string Describe(const Plan& a_plan, std::size_t a_max);

	// --- timed selling ([Selling] iAutoSellHours) -------------------------------------------------------------
	// Only what has been in the chest for the whole period is sold. Without a co-save the plugin cannot know when
	// an item arrived, so it keeps, per chest, what it saw the first time (`counts`, sellable copies per form) and
	// when (`since`, in game days). Each later look keeps the smaller count per form (an item taken out is gone
	// from it; what arrived since is not in it). Once the period has passed since `since`, the copies still in both
	// are sold and the rest starts its own period. So an item is never sold sooner than the period after Dova Stash
	// first saw it (after loading a game, the period starts again). Kept in memory only.
	struct Aging
	{
		double since{ 0.0 };  // game days (Calendar::GetDaysPassed)
		Counts counts;
	};

	struct AgingStep
	{
		bool   due{ false };  // the period has passed: sell `sell`
		Counts sell;          // copies per form that have been in the chest for the whole period
		Aging  next;          // what to keep for the next look (after selling `sell`)
	};

	// `a_now`: the chest's sellable copies per form (Plan::Sold of an unlimited plan). `a_hours` > 0.
	[[nodiscard]] AgingStep StepAging(const std::optional<Aging>& a_previous, const Counts& a_now, double a_nowDays, double a_hours);
}
