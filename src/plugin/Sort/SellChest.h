#pragma once

// The sell chest (DESIGN §8.8), plugin side. core::sell decides which stacks go and for how much; this snapshots
// the sell chests, removes what is sold on the main thread, gives the player the gold, keeps the session's sale log
// and the last sale for a buy-back, and runs timed selling. Main thread only, except Reset (messaging thread) and
// the log copy (any thread).

#include "Home/HomeRegistry.h"

namespace dova::sort::selling
{
	// What a sale would do right now.
	struct Quote
	{
		std::int32_t               items{ 0 };
		std::int64_t               gold{ 0 };
		std::string                what;   // "Iron Dagger x5, Tankard x3 +4 more"
		std::string                stays;  // "3 stay: 2 stolen, 1 quest item" ("" when nothing stays)
		std::vector<core::FormRef> chests;  // the sell chests it covers (reachable from here)

		[[nodiscard]] bool Empty() const noexcept { return items <= 0; }
	};

	// `a_chest`: that sell chest only; nullopt: every sell chest of the home that can be reached from here.
	[[nodiscard]] Quote QuoteSale(const home::Home& a_home, const std::optional<core::FormRef>& a_chest);

	struct Result
	{
		bool        done{ false };
		std::string message;  // one HUD line
		bool        failed{ false };  // could not be done (the panel shows `message` red)
	};

	// Sells what QuoteSale says now, if that is still what the player confirmed (`a_items`, `a_gold`): a chest that
	// changed since is not sold (the HUD asks to try again).
	[[nodiscard]] Result Sell(const home::Home& a_home, const std::optional<core::FormRef>& a_chest, std::int32_t a_items, std::int64_t a_gold);

	// The last sale of this session, while it can be bought back here: the same items for the same gold.
	struct Offer
	{
		std::int32_t items{ 0 };
		std::int64_t gold{ 0 };
		std::string  what;
	};
	[[nodiscard]] std::optional<Offer> BuyBackOffer(const home::Home& a_home);
	[[nodiscard]] Result               BuyBack(const home::Home& a_home);

	// The session's sales, newest first (for the panel).
	struct SaleLine
	{
		std::string  name;
		std::int32_t count{ 0 };
		std::int64_t gold{ 0 };
	};
	struct Sale
	{
		std::string           time;  // game time, "Day 12, 14:05"
		std::string           home;
		std::string           chests;  // their labels
		bool                  timed{ false };
		bool                  boughtBack{ false };
		std::int32_t          items{ 0 };
		std::int64_t          gold{ 0 };
		std::vector<SaleLine> lines;
	};
	[[nodiscard]] std::vector<Sale> SaleLog();

	// Timed selling ([Selling] iAutoSellHours > 0): looks at the current home's sell chests and sells what has been
	// in them for the whole period (core::sell::StepAging). Called on entering a home, when a container closes and
	// after sleeping or waiting.
	void CheckTimed(std::string_view a_trigger);

	// Sleep / wait end: CheckTimed.
	void RegisterSinks();
	// Loading a game or starting a new one: the sale log, the buy-back and what timed selling saw are forgotten.
	void Reset(std::string_view a_why);
}
