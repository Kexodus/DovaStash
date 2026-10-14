#include "dova/core/Sell.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace dova::core::sell
{
	namespace
	{
		const Barter kVanilla{};

		double Finite(float a_value, float a_fallback) noexcept
		{
			return std::isfinite(a_value) ? static_cast<double>(a_value) : static_cast<double>(a_fallback);
		}

		// The barter for one stack: its own perk entries on top.
		Barter For(const Barter& a_barter, const Stack& a_stack) noexcept
		{
			auto barter = a_barter;
			if (std::isfinite(a_stack.perks) && a_stack.perks > 0.0f) {
				barter.perks = static_cast<float>(Finite(barter.perks, 1.0f) * a_stack.perks);
			}
			return barter;
		}
	}

	double PriceFactor(const Barter& a_barter) noexcept
	{
		double low = Finite(a_barter.barterMin, kVanilla.barterMin);
		double high = Finite(a_barter.barterMax, kVanilla.barterMax);
		if (low <= 0.0 || high <= 0.0) {
			low = kVanilla.barterMin;
			high = kVanilla.barterMax;
		}
		const double speech = std::clamp(Finite(a_barter.speech, kVanilla.speech), 0.0, 100.0);
		return high - (high - low) * speech / 100.0;
	}

	double SellMultiplier(const Barter& a_barter) noexcept
	{
		const double factor = PriceFactor(a_barter);
		if (factor <= 0.0) {
			return 0.0;
		}
		const double perks = std::max(0.0, Finite(a_barter.perks, 1.0f));
		const double fortify = std::max(-100.0, Finite(a_barter.fortify, 0.0f));
		double       cap = Finite(a_barter.sellMax, kVanilla.sellMax);
		if (cap <= 0.0 || cap > 1.0) {
			cap = kVanilla.sellMax;  // never more than the item's value from the game's side
		}
		const double game = std::min(cap, perks * (1.0 + fortify / 100.0) / factor);
		return std::max(0.0, game) * std::max(0, a_barter.percent) / 100.0;
	}

	std::int32_t UnitPrice(std::int32_t a_value, const Barter& a_barter) noexcept
	{
		if (a_value <= 0) {
			return 0;
		}
		// Rounded to 6 decimals: the inputs are floats (a Haggling 1.3f is 1.2999999...), and 100 * 0.65 must not
		// floor to 64. The epsilon does the same for the product.
		const double mult = std::round(SellMultiplier(a_barter) * 1e6) / 1e6;
		if (mult <= 0.0) {
			return 0;
		}
		const double price = std::floor(static_cast<double>(a_value) * mult + 1e-6);
		if (price >= static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
			return std::numeric_limits<std::int32_t>::max();
		}
		return std::max<std::int32_t>(1, static_cast<std::int32_t>(price));
	}

	std::string_view VerdictName(Verdict a_verdict) noexcept
	{
		switch (a_verdict) {
		case Verdict::Sell:
			return "sold";
		case Verdict::Empty:
			return "empty";
		case Verdict::Quest:
			return "quest item";
		case Verdict::NonPlayable:
			return "non-playable";
		case Verdict::Returns:
			return "comes back when stored";
		case Verdict::Protected:
			return "worn or favorited";
		case Verdict::Gold:
			return "gold";
		case Verdict::Dynamic:
			return "made by you";
		case Verdict::Customized:
			return "customized";
		case Verdict::Stolen:
			return "stolen";
		case Verdict::Kept:
			return "kept by a keep rule";
		case Verdict::Worthless:
			return "worth nothing";
		default:
			return "?";
		}
	}

	Verdict Judge(const Stack& a_stack, const KeepRules& a_keep, const CategorySet& a_categories, const Barter& a_barter, std::string* a_why)
	{
		const auto& item = a_stack.item;
		if (item.count <= 0) {
			return Verdict::Empty;
		}
		if (item.questItem) {
			return Verdict::Quest;
		}
		if (!item.playable || item.name.empty()) {
			return Verdict::NonPlayable;
		}
		if (item.returns) {
			return Verdict::Returns;
		}
		if (item.equipped || item.favorited) {
			return Verdict::Protected;
		}
		if (item.form == kGoldForm) {
			return Verdict::Gold;
		}
		if (!item.form.IsValid()) {
			return Verdict::Dynamic;
		}
		if (a_stack.copy == Copy::Special || item.customized) {
			return Verdict::Customized;
		}
		if (a_stack.copy == Copy::Stolen) {
			return Verdict::Stolen;
		}
		if (a_keep.AlwaysKeeps(item, a_categories, a_why)) {
			return Verdict::Kept;
		}
		if (UnitPrice(item.value, For(a_barter, a_stack)) <= 0) {
			return Verdict::Worthless;
		}
		return Verdict::Sell;
	}

	Counts Plan::Sold() const
	{
		Counts out;
		for (const auto& line : lines) {
			out[line.form] += line.count;
		}
		return out;
	}

	Plan PlanSale(std::span<const Stack> a_stacks, const KeepRules& a_keep, const CategorySet& a_categories, const Barter& a_barter, const Counts* a_limit)
	{
		Plan   plan;
		Counts left;
		if (a_limit) {
			left = *a_limit;
		}
		plan.verdicts.reserve(a_stacks.size());
		plan.why.resize(a_stacks.size());
		for (std::size_t i = 0; i < a_stacks.size(); ++i) {
			const auto& stack = a_stacks[i];
			const auto  verdict = Judge(stack, a_keep, a_categories, a_barter, &plan.why[i]);
			plan.verdicts.push_back(verdict);
			const auto count = std::max(0, stack.item.count);
			if (verdict != Verdict::Sell) {
				if (count > 0) {
					plan.stays[verdict] += count;
				}
				continue;
			}
			auto sell = count;
			if (a_limit) {
				auto& allowance = left[stack.item.form];
				sell = std::clamp(allowance, 0, count);
				allowance -= sell;
				if (count > sell) {
					plan.stays[Verdict::Sell] += count - sell;  // not long enough in the chest
				}
			}
			if (sell <= 0) {
				continue;
			}
			Line line;
			line.index = i;
			line.stackId = stack.item.stackId;
			line.form = stack.item.form;
			line.name = stack.item.name;
			line.count = sell;
			line.unitPrice = UnitPrice(stack.item.value, For(a_barter, stack));
			plan.items += sell;
			plan.gold += line.Total();
			plan.lines.push_back(std::move(line));
		}
		return plan;
	}

	std::string Describe(const Plan& a_plan, std::size_t a_max)
	{
		// Merge stacks of one item, then the most gold first (name to break ties, so the text is stable).
		struct Entry
		{
			std::string  name;
			std::int32_t count{ 0 };
			std::int64_t gold{ 0 };
		};
		std::vector<Entry> entries;
		std::map<FormRef, std::size_t> byForm;
		for (const auto& line : a_plan.lines) {
			const auto [it, added] = byForm.emplace(line.form, entries.size());
			if (added) {
				entries.push_back(Entry{ line.name, 0, 0 });
			}
			entries[it->second].count += line.count;
			entries[it->second].gold += line.Total();
		}
		std::ranges::stable_sort(entries, [](const Entry& a_lhs, const Entry& a_rhs) {
			return a_lhs.gold != a_rhs.gold ? a_lhs.gold > a_rhs.gold : a_lhs.name < a_rhs.name;
		});
		std::string out;
		const auto  shown = std::min(entries.size(), a_max);
		for (std::size_t i = 0; i < shown; ++i) {
			out += (i == 0 ? "" : ", ") + (entries[i].count == 1 ? entries[i].name : std::format("{} x{}", entries[i].name, entries[i].count));
		}
		if (entries.size() > shown) {
			out += std::format(" +{} more", entries.size() - shown);
		}
		return out;
	}

	AgingStep StepAging(const std::optional<Aging>& a_previous, const Counts& a_now, double a_nowDays, double a_hours)
	{
		AgingStep step;
		const auto fresh = [&]() {
			step.next.since = a_nowDays;
			for (const auto& [form, n] : a_now) {
				if (n > 0) {
					step.next.counts[form] = n;
				}
			}
		};
		// No earlier look, timed selling off, or the clock went back (a game was loaded meanwhile): start now.
		if (!a_previous || !(a_hours > 0.0) || !std::isfinite(a_nowDays) || a_nowDays < a_previous->since) {
			fresh();
			return step;
		}
		// What has been there since the earlier look: the smaller count per form.
		Counts still;
		for (const auto& [form, n] : a_previous->counts) {
			const auto it = a_now.find(form);
			const auto both = it != a_now.end() ? std::min(n, it->second) : 0;
			if (both > 0) {
				still[form] = both;
			}
		}
		if (a_nowDays - a_previous->since + 1e-9 < a_hours / 24.0) {
			step.next.since = a_previous->since;
			step.next.counts = std::move(still);
			return step;
		}
		step.due = !still.empty();
		step.next.since = a_nowDays;
		for (const auto& [form, n] : a_now) {
			const auto it = still.find(form);
			const auto rest = n - (it != still.end() ? it->second : 0);
			if (rest > 0) {
				step.next.counts[form] = rest;
			}
		}
		step.sell = std::move(still);
		return step;
	}
}
