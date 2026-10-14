#include "Sort/SellChest.h"

#include "Controller.h"
#include "Game/FormUtil.h"
#include "Game/ItemAdapter.h"
#include "Game/ItemMover.h"
#include "Home/Areas.h"
#include "Home/Store.h"
#include "Settings.h"

#include <dova/core/Sell.h>

#include <limits>
#include <mutex>

namespace dova::sort::selling
{
	namespace
	{
		namespace cs = core::sell;
		using Control = RE::BSEventNotifyControl;

		constexpr RE::FormID  kGoldID = 0x0000000F;
		constexpr std::size_t kLogSize = 20;    // sales kept in the session's log
		constexpr std::size_t kDescribe = 4;    // item names in a one-line summary

		// One stack that went: what a buy-back puts back.
		struct Sold
		{
			RE::FormID   chest{ 0 };
			RE::FormID   object{ 0 };
			std::string  name;
			std::int32_t count{ 0 };
			std::int32_t unitPrice{ 0 };
		};

		struct LastSale
		{
			RE::FormID        cellId{ 0 };
			std::vector<Sold> sold;
			std::int64_t      gold{ 0 };
			std::size_t       logIndex{ 0 };  // Sale::boughtBack is set on this record (counted from the oldest)
		};

		std::mutex                              g_lock;   // the log and the buy-back: read by the panel model, reset on load
		std::vector<Sale>                       g_log;    // oldest first
		std::size_t                             g_logged{ 0 };  // sales logged this session (g_log keeps the last kLogSize)
		std::optional<LastSale>                 g_last;
		std::map<RE::FormID, cs::Aging>         g_aging;  // timed selling, per chest

		void Enqueue(std::function<void()> a_task)
		{
			if (const auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask(std::move(a_task));
			}
		}

		std::string Plural(std::int64_t a_n, std::string_view a_word)
		{
			return std::format("{} {}{}", a_n, a_word, a_n == 1 ? "" : "s");
		}

		std::string GameTime()
		{
			const auto* calendar = RE::Calendar::GetSingleton();
			if (!calendar) {
				return {};
			}
			const auto hour = static_cast<int>(calendar->GetHour());
			return std::format("{} {}, {:02}:{:02}", static_cast<int>(calendar->GetDay()), calendar->GetMonthName(), hour, calendar->GetMinutes());
		}

		// A copy's extra data (core::sell::Copy): plain with nothing but a count or a unique id.
		cs::Copy CopyOf(RE::ExtraDataList* a_extra)
		{
			if (!a_extra) {
				return cs::Copy::Plain;
			}
			auto copy = cs::Copy::Plain;
			for (const auto& data : *a_extra) {
				switch (data.GetType()) {
				case RE::ExtraDataType::kCount:
				case RE::ExtraDataType::kUniqueID:
					break;
				case RE::ExtraDataType::kOwnership:
					copy = cs::Copy::Stolen;
					break;
				default:
					return cs::Copy::Special;
				}
			}
			return copy;
		}

		// The game's own value of one item (base value plus its enchantment), as the barter menu shows it.
		std::int32_t UnitValue(RE::TESBoundObject* a_object)
		{
			RE::InventoryEntryData entry(a_object, 1);
			const auto             value = entry.GetValue();
			return value >= 0 ? value : a_object->GetGoldValue();
		}

		// The player's Mod Sell Prices perk entries for this item (Haggling...), multiplied. The entry point's
		// parameters are read from the game (item, then the merchant: the player stands in for one, so perks that
		// need a merchant of the other sex never apply).
		float PerkMultiplier(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_object)
		{
			using EP = RE::BGSEntryPoint::ENTRY_POINT;
			const auto* entry = RE::BGSEntryPoint::GetEntryPoint(EP::kModSellPrices);
			const auto  count = entry ? entry->parameters.count : 0u;
			float       mult = 1.0f;
			if (count == 1) {
				RE::BGSEntryPoint::HandleEntryPoint(EP::kModSellPrices, a_player, static_cast<RE::TESForm*>(a_object), &mult);
			} else if (count == 2) {
				RE::BGSEntryPoint::HandleEntryPoint(EP::kModSellPrices, a_player, static_cast<RE::TESForm*>(a_object), static_cast<RE::TESObjectREFR*>(a_player), &mult);
			} else {
				static bool logged = false;
				if (!std::exchange(logged, true)) {
					logger::warn("Sell chest: the Mod Sell Prices perk entry has {} parameters; perks are left out of sale prices", count);
				}
			}
			return std::isfinite(mult) && mult > 0.0f ? mult : 1.0f;
		}

		// The barter settings and the player's Speech; perks per item (PerkMultiplier).
		cs::Barter GameBarter()
		{
			cs::Barter barter;
			if (auto* settings = RE::GameSettingCollection::GetSingleton()) {
				const auto read = [&](const char* a_name, float& a_out) {
					if (auto* setting = settings->GetSetting(a_name)) {
						a_out = setting->GetFloat();
					}
				};
				read("fBarterMin", barter.barterMin);
				read("fBarterMax", barter.barterMax);
				read("fBarterSellMax", barter.sellMax);
			}
			static bool logged = false;
			if (!std::exchange(logged, true)) {
				logger::info("Sell chest: barter settings fBarterMin {} fBarterMax {} fBarterSellMax {}", barter.barterMin, barter.barterMax, barter.sellMax);
			}
			if (auto* player = game::Player()) {
				if (auto* values = player->AsActorValueOwner()) {
					barter.speech = values->GetActorValue(RE::ActorValue::kSpeech);
					barter.fortify = values->GetActorValue(RE::ActorValue::kSpeechcraftModifier);
				}
			}
			barter.percent = Settings::GetSingleton().GetSelling().pricePercent;
			return barter;
		}

		core::KeepRules CompileKeep(const core::Layout& a_layout)
		{
			core::Diagnostics diags;
			return core::KeepRules::Compile(home::Store::Get().GlobalKeep(), a_layout.keep ? &*a_layout.keep : nullptr, diags);
		}

		// One sell chest, looked at now.
		struct Chest
		{
			core::FormRef          ref;
			RE::TESObjectREFR*     object{ nullptr };
			std::string            label;
			game::StackTable       table;
			std::vector<cs::Stack> stacks;
			cs::Plan               plan;
		};

		std::string Label(const core::Layout& a_layout, const core::FormRef& a_ref, RE::TESObjectREFR* a_object)
		{
			const auto* binding = a_layout.FindContainer(a_ref);
			return binding && !binding->label.empty() ? binding->label : game::RefName(a_object);
		}

		// The home's sell chests that can be used from here (all of them, or `a_only`), planned. Their stack ids
		// stay valid within the calling task.
		std::vector<Chest> LookAt(const home::Home& a_home, const std::optional<core::FormRef>& a_only, const cs::Barter& a_barter)
		{
			std::vector<Chest> out;
			if (!a_home.layout || a_home.layoutBroken) {
				return out;
			}
			const auto& layout = *a_home.layout;
			auto*       player = game::Player();
			if (!player) {
				return out;
			}
			const auto& categories = home::Store::Get().Categories();
			const auto  keep = CompileKeep(layout);
			for (const auto& c : layout.containers) {
				if (!layout.IsSellChest(c.ref) || (a_only && !(*a_only == c.ref))) {
					continue;
				}
				auto* ref = game::LookupRef(c.ref);
				if (!ref) {
					continue;
				}
				if (const auto verdict = home::CheckHomeContainer(ref, a_home); !verdict.ok) {
					logger::debug("Sell chest {}: not usable from here ({})", game::Describe(ref), verdict.reason);
					continue;
				}
				Chest chest;
				chest.ref = c.ref;
				chest.object = ref;
				chest.label = Label(layout, c.ref, ref);
				for (auto& item : game::SnapshotInventory(ref, chest.table)) {
					cs::Stack stack{ std::move(item), cs::Copy::Special };
					if (const auto* s = chest.table.Find(stack.item.stackId); s && s->object) {
						stack.copy = CopyOf(s->extra);
						stack.item.value = UnitValue(s->object);
						stack.perks = PerkMultiplier(player, s->object);
					}
					chest.stacks.push_back(std::move(stack));
				}
				chest.plan = cs::PlanSale(chest.stacks, keep, categories, a_barter);
				out.push_back(std::move(chest));
			}
			return out;
		}

		std::string StaysText(const std::map<cs::Verdict, std::int32_t>& a_stays)
		{
			std::int32_t total = 0;
			std::string  why;
			for (const auto& [verdict, n] : a_stays) {
				if (verdict == cs::Verdict::Empty || verdict == cs::Verdict::Sell) {
					continue;
				}
				total += n;
				why += std::format("{}{} {}", why.empty() ? "" : ", ", n, cs::VerdictName(verdict));
			}
			return total > 0 ? std::format("{} stay: {}", total, why) : std::string{};
		}

		Quote Summarize(const std::vector<Chest>& a_chests)
		{
			Quote                            quote;
			cs::Plan                         all;
			std::map<cs::Verdict, std::int32_t> stays;
			for (const auto& chest : a_chests) {
				quote.chests.push_back(chest.ref);
				quote.items += chest.plan.items;
				quote.gold += chest.plan.gold;
				for (const auto& line : chest.plan.lines) {
					all.lines.push_back(line);
				}
				for (const auto& [verdict, n] : chest.plan.stays) {
					stays[verdict] += n;
				}
			}
			quote.what = cs::Describe(all, kDescribe);
			quote.stays = StaysText(stays);
			return quote;
		}

		// Removes what `a_plan` sells from `a_chest`, returns what really went. Each stack is checked against the
		// live inventory first (the same extra-data list, enough copies, not protected); anything doubtful stays.
		std::vector<Sold> Remove(Chest& a_chest, const cs::Plan& a_plan)
		{
			std::vector<Sold> sold;
			for (const auto& line : a_plan.lines) {
				const auto* stack = a_chest.table.Find(line.stackId);
				if (!stack || !stack->object) {
					continue;
				}
				const auto live = game::FindLiveStack(a_chest.object, stack->object, stack->extra);
				if (!live.found || live.isProtected || live.count < line.count) {
					logger::info("Sell chest: {} x{} left alone (it changed in {})", line.name, line.count, a_chest.label);
					continue;
				}
				const auto before = game::LiveCount(a_chest.object, stack->object);
				a_chest.object->RemoveItem(stack->object, line.count, RE::ITEM_REMOVE_REASON::kRemove, stack->extra, nullptr);
				const auto after = game::LiveCount(a_chest.object, stack->object);
				const auto gone = std::clamp(before - after, 0, line.count);
				if (gone != line.count) {
					logger::error("Sell chest: {} x{} from {}: {} went", game::Describe(stack->object), line.count, a_chest.label, gone);
				}
				if (gone > 0) {
					sold.push_back(Sold{ a_chest.object->GetFormID(), stack->object->GetFormID(), line.name, gone, line.unitPrice });
				}
			}
			return sold;
		}

		// Sells `a_plans` (one per chest, same order as `a_chests`): removes the items, pays the player, logs the sale
		// (every stack at info: this destroys items), and remembers it for a buy-back.
		Result Execute(const home::Home& a_home, std::vector<Chest>& a_chests, const std::vector<cs::Plan>& a_plans, bool a_timed)
		{
			Result result;
			auto*  player = game::Player();
			auto*  gold = RE::TESForm::LookupByID<RE::TESBoundObject>(kGoldID);
			if (!player || !gold) {
				result.message = "Dova Stash: can't sell now";
				return result;
			}
			std::vector<Sold> sold;
			std::string       labels;
			{
				game::ItemMover::SuppressScope quiet;  // our own changes: never learned, never "came back"
				for (std::size_t i = 0; i < a_chests.size() && i < a_plans.size(); ++i) {
					if (a_plans[i].Empty()) {
						continue;
					}
					auto went = Remove(a_chests[i], a_plans[i]);
					if (!went.empty()) {
						labels += (labels.empty() ? "" : ", ") + a_chests[i].label;
						RE::SendUIMessage::SendInventoryUpdateMessage(a_chests[i].object, nullptr);
					}
					sold.insert(sold.end(), std::make_move_iterator(went.begin()), std::make_move_iterator(went.end()));
				}
				std::int64_t total = 0;
				std::int32_t items = 0;
				for (const auto& s : sold) {
					total += static_cast<std::int64_t>(s.count) * s.unitPrice;
					items += s.count;
				}
				if (sold.empty()) {
					result.message = "Dova Stash: nothing was sold (see log)";
					return result;
				}
				total = std::min<std::int64_t>(total, std::numeric_limits<std::int32_t>::max());
				if (total > 0) {
					player->AddObjectToContainer(gold, nullptr, static_cast<std::int32_t>(total), nullptr);
				}
				RE::SendUIMessage::SendInventoryUpdateMessage(player, nullptr);

				Sale sale;
				sale.time = GameTime();
				sale.home = a_home.name;
				sale.chests = labels;
				sale.timed = a_timed;
				sale.items = items;
				sale.gold = total;
				for (const auto& s : sold) {
					const auto lineGold = static_cast<std::int64_t>(s.count) * s.unitPrice;
					logger::info("Sell chest: sold {} x{} for {} gold ({} each) from {}", game::Describe(RE::TESForm::LookupByID(s.object)), s.count,
						lineGold, s.unitPrice, game::Describe(RE::TESForm::LookupByID(s.chest)));
					auto it = std::ranges::find(sale.lines, s.name, &SaleLine::name);
					if (it == sale.lines.end()) {
						sale.lines.push_back(SaleLine{ s.name, 0, 0 });
						it = std::prev(sale.lines.end());
					}
					it->count += s.count;
					it->gold += lineGold;
				}
				logger::info("Sell chest ({}): sold {} for {} gold in {}", a_timed ? "timed" : "sell now", Plural(items, "item"), total, a_home.name);

				std::scoped_lock lock(g_lock);
				g_log.push_back(std::move(sale));
				++g_logged;
				if (g_log.size() > kLogSize) {
					g_log.erase(g_log.begin());
				}
				g_last = LastSale{ a_home.cellId, std::move(sold), total, g_logged };
				result.done = true;
				result.message = std::format("Dova Stash: sold {} for {} gold", Plural(items, "item"), total);
			}
			return result;
		}

		// Sleep and wait end: timed selling (the player may have slept in the home for the whole period).
		class SleepSink final : public RE::BSTEventSink<RE::TESSleepStopEvent>
		{
		public:
			Control ProcessEvent(const RE::TESSleepStopEvent*, RE::BSTEventSource<RE::TESSleepStopEvent>*) override
			{
				Enqueue([]() { CheckTimed("slept"); });
				return Control::kContinue;
			}
		};

		class WaitSink final : public RE::BSTEventSink<RE::TESWaitStopEvent>
		{
		public:
			Control ProcessEvent(const RE::TESWaitStopEvent*, RE::BSTEventSource<RE::TESWaitStopEvent>*) override
			{
				Enqueue([]() { CheckTimed("waited"); });
				return Control::kContinue;
			}
		};

		SleepSink g_sleepSink;
		WaitSink  g_waitSink;
	}

	Quote QuoteSale(const home::Home& a_home, const std::optional<core::FormRef>& a_chest)
	{
		return Summarize(LookAt(a_home, a_chest, GameBarter()));
	}

	Result Sell(const home::Home& a_home, const std::optional<core::FormRef>& a_chest, std::int32_t a_items, std::int64_t a_gold)
	{
		auto       chests = LookAt(a_home, a_chest, GameBarter());
		const auto quote = Summarize(chests);
		if (quote.Empty()) {
			return Result{ false, "Dova Stash: nothing to sell" };
		}
		if (quote.items != a_items || quote.gold != a_gold) {
			// Never sell something other than what the player said yes to.
			logger::info("Sell chest: not sold, the chest changed since the question ({} items for {} gold, now {} for {})", a_items, a_gold, quote.items,
				quote.gold);
			return Result{ false, "Dova Stash: the sell chest changed; sell again" };
		}
		std::vector<cs::Plan> plans;
		for (const auto& chest : chests) {
			plans.push_back(chest.plan);
		}
		return Execute(a_home, chests, plans, false);
	}

	std::optional<Offer> BuyBackOffer(const home::Home& a_home)
	{
		std::scoped_lock lock(g_lock);
		if (!g_last || g_last->cellId != a_home.cellId) {
			return std::nullopt;
		}
		Offer      offer;
		cs::Plan   plan;
		for (const auto& s : g_last->sold) {
			offer.items += s.count;
			cs::Line line;
			line.form = core::FormRef{ "", s.object };  // only told apart, never written
			line.name = s.name;
			line.count = s.count;
			line.unitPrice = s.unitPrice;
			plan.lines.push_back(std::move(line));
		}
		offer.gold = g_last->gold;
		offer.what = cs::Describe(plan, kDescribe);
		return offer;
	}

	Result BuyBack(const home::Home& a_home)
	{
		std::optional<LastSale> last;
		{
			std::scoped_lock lock(g_lock);
			if (!g_last || g_last->cellId != a_home.cellId) {
				return Result{ false, "Dova Stash: nothing to buy back here" };
			}
			last = g_last;
		}
		auto* player = game::Player();
		auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(kGoldID);
		if (!player || !gold) {
			return Result{ false, "Dova Stash: can't buy back now" };
		}
		// Every chest must be usable from here and every item still exist: all or nothing.
		for (const auto& s : last->sold) {
			auto* chest = RE::TESForm::LookupByID<RE::TESObjectREFR>(s.chest);
			if (!chest || !home::CheckHomeContainer(chest, a_home).ok) {
				return Result{ false, "Dova Stash: the sell chest can't be reached from here" };
			}
			if (!RE::TESForm::LookupByID<RE::TESBoundObject>(s.object)) {
				return Result{ false, "Dova Stash: can't buy back (an item no longer exists)" };
			}
		}
		const auto have = game::LiveCount(player, gold);
		if (have < last->gold) {
			return Result{ false, std::format("Dova Stash: buying back takes {} gold", last->gold) };
		}
		std::int32_t items = 0;
		{
			game::ItemMover::SuppressScope quiet;
			if (last->gold > 0) {
				player->RemoveItem(gold, static_cast<std::int32_t>(last->gold), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}
			for (const auto& s : last->sold) {
				auto* chest = RE::TESForm::LookupByID<RE::TESObjectREFR>(s.chest);
				auto* object = RE::TESForm::LookupByID<RE::TESBoundObject>(s.object);
				chest->AddObjectToContainer(object, nullptr, s.count, nullptr);
				items += s.count;
				logger::info("Sell chest: bought back {} x{} into {}", game::Describe(object), s.count, game::Describe(chest));
				RE::SendUIMessage::SendInventoryUpdateMessage(chest, nullptr);
			}
			RE::SendUIMessage::SendInventoryUpdateMessage(player, nullptr);
		}
		logger::info("Sell chest: bought back {} for {} gold in {}", Plural(items, "item"), last->gold, a_home.name);
		{
			std::scoped_lock lock(g_lock);
			// Mark the log entry (if it is still in the log) and forget the offer: one buy-back per sale.
			const auto first = g_logged - g_log.size() + 1;  // sale number of g_log.front()
			if (g_last && g_last->logIndex >= first && g_last->logIndex - first < g_log.size()) {
				g_log[g_last->logIndex - first].boughtBack = true;
			}
			g_last.reset();
		}
		// What timed selling saw of these items starts again: they are back as of now.
		{
			std::scoped_lock lock(g_lock);
			g_aging.clear();
		}
		return Result{ true, std::format("Dova Stash: bought back {} for {} gold", Plural(items, "item"), last->gold) };
	}

	std::vector<Sale> SaleLog()
	{
		std::scoped_lock lock(g_lock);
		return { g_log.rbegin(), g_log.rend() };
	}

	void CheckTimed(std::string_view a_trigger)
	{
		const auto hours = Settings::GetSingleton().GetSelling().autoSellHours;
		if (hours <= 0) {
			std::scoped_lock lock(g_lock);
			g_aging.clear();
			return;
		}
		auto* home = home::HomeRegistry::Get().Current();
		if (!home || !home->layout || home->layoutBroken || !home->layout->HasSellChest()) {
			return;
		}
		auto* ui = RE::UI::GetSingleton();
		if (ui && ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME)) {
			return;  // never while a container shows its items (DESIGN §8.2); checked again when it closes
		}
		const auto* calendar = RE::Calendar::GetSingleton();
		if (!calendar) {
			return;
		}
		const double          days = calendar->GetDaysPassed();
		const auto            barter = GameBarter();
		auto                  chests = LookAt(*home, std::nullopt, barter);
		std::vector<cs::Plan> plans(chests.size());
		bool                  due = false;
		const auto            keep = CompileKeep(*home->layout);
		for (std::size_t i = 0; i < chests.size(); ++i) {
			auto&                    chest = chests[i];
			const auto               id = chest.object->GetFormID();
			std::optional<cs::Aging> previous;
			{
				std::scoped_lock lock(g_lock);
				if (const auto it = g_aging.find(id); it != g_aging.end()) {
					previous = it->second;
				}
			}
			const auto step = cs::StepAging(previous, chest.plan.Sold(), days, static_cast<double>(hours));
			if (step.due) {
				plans[i] = cs::PlanSale(chest.stacks, keep, home::Store::Get().Categories(), barter, &step.sell);
				due = due || !plans[i].Empty();
			}
			if (!previous || step.due) {
				logger::debug("Sell chest {}: timed selling ({}): {} kinds of item seen since day {:.2f}{}", chest.label, a_trigger, step.next.counts.size(),
					step.next.since, step.due ? std::format(", {} items due", plans[i].items) : ""s);
			}
			std::scoped_lock lock(g_lock);
			g_aging[id] = step.next;
		}
		if (!due) {
			return;
		}
		const auto result = Execute(*home, chests, plans, true);
		if (!result.message.empty()) {
			Controller::Get().Notify(result.message);
		}
	}

	void RegisterSinks()
	{
		if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink<RE::TESSleepStopEvent>(&g_sleepSink);
			holder->AddEventSink<RE::TESWaitStopEvent>(&g_waitSink);
		}
		logger::info("Events: sell chest sinks registered");
	}

	void Reset(std::string_view a_why)
	{
		std::scoped_lock lock(g_lock);
		if (!g_log.empty() || g_last) {
			logger::debug("Sell chest: {}; the sale log and the buy-back are forgotten", a_why);
		}
		g_log.clear();
		g_logged = 0;
		g_last.reset();
		g_aging.clear();
	}
}
