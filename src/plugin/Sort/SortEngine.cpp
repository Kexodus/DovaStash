#include "Sort/SortEngine.h"

#include "Game/ArmorIndex.h"
#include "Game/Eligibility.h"
#include "Game/FormUtil.h"
#include "Game/ItemAdapter.h"
#include "Game/ItemMover.h"
#include "Home/Store.h"
#include "Settings.h"
#include "Sort/DisplayFiller.h"

namespace dova::sort
{
	namespace
	{
		// Per-operation container availability (DESIGN §9 "templates bypass none of these checks"). In a
		// multi-cell home, containers in the home's other cells count when they can be reached from here
		// (home::CheckHomeContainer, DESIGN §6.7); the others are skipped like unbuilt ones.
		// Copyable (core stores it in a std::function); the cache is shared.
		class Availability
		{
		public:
			// `a_quiet`: log unavailable containers at debug level only (frequent read-only queries such
			// as CountByCategory would otherwise repeat the warning for every unbuilt Hearthfire wing).
			explicit Availability(const home::Home& a_home, bool a_quiet = false) :
				_home(&a_home),
				_quiet(a_quiet),
				_cache(std::make_shared<std::map<core::FormRef, bool>>())
			{}

			bool operator()(const core::FormRef& a_ref) const
			{
				if (const auto it = _cache->find(a_ref); it != _cache->end()) {
					return it->second;
				}
				auto*      ref = game::LookupRef(a_ref);
				core::Reach reach = core::Reach::Here;
				const auto verdict = ref ? home::CheckHomeContainer(ref, *_home, &reach) :
				                           game::Verdict::Reject(_home->MultiCell() ? "not in memory (in an area of the home that is not loaded and not persistent, or its plugin is missing)" :
				                                                                      "reference not found (plugin missing?)");
				if (!verdict.ok) {
					// Out of reach from this area of a multi-cell home is expected, not a problem, and so is furniture
					// not built yet (Hearthfire): debug. Use template reports those once (Controller CountUnavailable).
					if (_quiet || reach == core::Reach::Unreachable || (!ref && _home->MultiCell()) || verdict.notBuilt) {
						logger::debug("Container {} unavailable: {}", a_ref.ToString(), verdict.LogReason());
					} else {
						logger::warn("Container {} unavailable: {}", a_ref.ToString(), verdict.LogReason());
					}
				} else if (!verdict.warning.empty()) {
					logger::debug("{}", verdict.warning);
				}
				_cache->emplace(a_ref, verdict.ok);
				return verdict.ok;
			}

		private:
			const home::Home*                             _home;
			bool                                          _quiet{ false };
			std::shared_ptr<std::map<core::FormRef, bool>> _cache;
		};

		std::vector<core::ContainerContents> SnapshotManaged(const core::Layout& a_layout, const Availability& a_available, game::StackTable& a_stacks)
		{
			std::vector<core::ContainerContents> out;
			auto add = [&](const core::FormRef& a_ref) {
				if (std::ranges::any_of(out, [&](const core::ContainerContents& c) { return c.ref == a_ref; })) {
					return;
				}
				if (!a_available(a_ref)) {
					return;
				}
				auto* ref = game::LookupRef(a_ref);
				if (!ref) {
					return;
				}
				out.push_back(core::ContainerContents{ a_ref, game::SnapshotInventory(ref, a_stacks) });
			};
			if (a_layout.master) {
				add(*a_layout.master);
			}
			for (const auto& c : a_layout.containers) {
				// The sell chest is a target only: nothing is taken out of it but by selling (DESIGN §8.8), so it is
				// no source for Sort all, Retrieve, its counts or the displays.
				if (!c.ignored && !a_layout.IsSellChest(c.ref)) {
					add(c.ref);
				}
			}
			return out;
		}

		// Every stack in the snapshots, as display stock (all of it may go on display).
		void AddStock(std::vector<core::DisplayStock>& a_stocks, const std::vector<core::ContainerContents>& a_contents)
		{
			for (const auto& c : a_contents) {
				for (const auto& item : c.items) {
					a_stocks.push_back(core::DisplayStock{ core::ContainerRef(c.ref), item, item.count });
				}
			}
		}

		core::KeepRules CompileKeep(const core::Layout& a_layout)
		{
			core::Diagnostics diags;
			auto              keep = core::KeepRules::Compile(home::Store::Get().GlobalKeep(), a_layout.keep ? &*a_layout.keep : nullptr, diags);
			for (const auto& d : diags.Items()) {
				logger::debug("Keep rules: {}", d.ToString());
			}
			return keep;
		}

		// What Deposit would move off the player may go on display instead, and so may what is already stored.
		std::vector<core::DisplayStock> DepositStock(std::span<const core::ItemInfo> a_inventory, std::span<const core::StackDecision> a_why,
			const std::vector<core::ContainerContents>& a_stored)
		{
			std::vector<core::DisplayStock> stock;
			for (std::size_t i = 0; i < a_inventory.size() && i < a_why.size(); ++i) {
				if (a_why[i].outcome == core::StackDecision::Outcome::Move && a_why[i].moved > 0) {
					stock.push_back(core::DisplayStock{ core::ContainerRef::Player(), a_inventory[i], a_why[i].moved });
				}
			}
			AddStock(stock, a_stored);
			return stock;
		}

		std::string Plural(std::int32_t a_n, std::string_view a_word)
		{
			return std::format("{} {}{}", a_n, a_word, a_n == 1 ? "" : "s");
		}

		// Restock (DESIGN §8.7): what to take from `a_stored` so the player carries every restocking quota.
		// Stored copies the display plan takes are left out. Logs one line per rule and per move at debug.
		std::vector<core::Move> PlanRestock(const core::SortPlanner& a_planner, std::span<const core::ItemInfo> a_inventory,
			std::vector<core::ContainerContents> a_stored, const core::KeepRules& a_keep, const display::Prepared& a_displays)
		{
			std::map<std::uint32_t, std::int32_t> reserved;  // stored stack id -> copies going on display
			for (const auto& a : a_displays.plan) {
				if (a.stock < a_displays.stocks.size() && !a_displays.stocks[a.stock].from.IsPlayer()) {
					reserved[a_displays.stocks[a.stock].item.stackId] += a.count;
				}
			}
			for (auto& c : a_stored) {
				for (auto& item : c.items) {
					if (const auto it = reserved.find(item.stackId); it != reserved.end()) {
						item.count = std::max(0, item.count - it->second);
					}
				}
			}
			std::vector<core::RestockLine> lines;
			auto                           moves = a_planner.Restock(a_inventory, a_stored, a_keep, &lines);
			for (const auto& line : lines) {
				logger::debug("Restock: {}: carried {}, taking {}{}", line.label, line.have, line.taken,
					line.have + line.taken < line.quota ? " (storage ran short)" : "");
			}
			if (spdlog::should_log(spdlog::level::debug)) {
				for (const auto& m : moves) {
					const auto c = std::ranges::find_if(a_stored, [&](const core::ContainerContents& a_c) { return a_c.ref == m.from.Ref(); });
					if (c == a_stored.end()) {
						continue;
					}
					const auto item = std::ranges::find_if(c->items, [&](const core::ItemInfo& a_i) { return a_i.stackId == m.stackId; });
					if (item != c->items.end()) {
						logger::debug("Restock: {} [{}] x{} from {}", item->name, item->form.ToString(), m.count, game::Describe(game::LookupRef(c->ref)));
					}
				}
			}
			return moves;
		}

		// One line per stack: where it goes and why, or why it stays (debug; homeless stays in the
		// master at trace, they repeat on every Distribute), then a summary at info.
		void LogDecisions(Op a_op, std::span<const core::ItemInfo> a_items, std::span<const core::StackDecision> a_why)
		{
			using enum core::StackDecision::Outcome;
			std::map<core::StackDecision::Outcome, std::int32_t> stacks;
			for (std::size_t i = 0; i < a_items.size() && i < a_why.size(); ++i) {
				const auto& item = a_items[i];
				const auto& d = a_why[i];
				++stacks[d.outcome];
				const auto what = std::format("{} [{}] x{}", item.name.empty() ? "<nameless>" : item.name, item.form.ToString(), item.count);
				const auto kept = d.kept > 0 ? std::format("; {} kept by {}", d.kept, d.keepRule) : std::string{};
				if (d.outcome == Move && d.destination) {
					const auto& dest = *d.destination;
					const auto  container = game::Describe(game::LookupRef(dest.container));
					const auto  reason = dest.reason == core::Destination::Reason::Category ? std::format("category {}", dest.category) :
					                                                                          std::string(core::DestinationReasonName(dest.reason));
					logger::debug("{}: {} -> {} x{} ({}){}", OpName(a_op), what, container, d.moved, reason, kept);
				} else if (d.outcome == Kept) {
					logger::debug("{}: {} stays: kept by {}", OpName(a_op), what, d.keepRule);
				} else if (d.outcome == Homeless) {
					logger::trace("{}: {} stays: homeless", OpName(a_op), what);
				} else {
					logger::debug("{}: {} stays: {}{}", OpName(a_op), what, core::StackDecision::OutcomeName(d.outcome), kept);
				}
			}
			std::string summary;
			for (const auto& [outcome, n] : stacks) {
				summary += std::format("{}{} {}", summary.empty() ? "" : ", ", n, core::StackDecision::OutcomeName(outcome));
			}
			logger::info("{} plan: {} ({})", OpName(a_op), Plural(static_cast<std::int32_t>(a_items.size()), "stack"),
				summary.empty() ? "nothing" : summary);
		}
	}

	std::string_view OpName(Op a_op) noexcept
	{
		switch (a_op) {
		case Op::Deposit:
			return "Deposit";
		case Op::Distribute:
			return "Distribute";
		case Op::SortAll:
			return "Sort all";
		case Op::Retrieve:
			return "Retrieve";
		case Op::Restock:
			return "Restock";
		default:
			return "?";
		}
	}

	Result Run(home::Home& a_home, Op a_op, std::string_view a_pattern)
	{
		Result result;
		if (!a_home.layout) {
			result.message = "Dova Stash: this home is not set up yet";
			return result;
		}
		auto* player = game::Player();
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!cell || cell->GetFormID() != a_home.cellId) {
			result.message = "Dova Stash: you are no longer in this home";
			result.failed = true;
			return result;
		}

		const auto& layout = *a_home.layout;
		const auto& categories = home::Store::Get().Categories();
		Availability available(a_home);
		core::SortPlanner planner(layout, categories, available);
		game::StackTable  stacks;
		std::vector<core::Move> moves;
		std::vector<core::Move> restock;  // Deposit: what Restock after Deposit takes (bRestockAfterDeposit)
		display::Prepared displays;
		const auto        sorting = Settings::GetSingleton().GetSorting();
		const bool        fill = sorting.fillDisplays;
		std::int32_t      stranded = 0;  // Deposit: items no reachable container takes (their area is not loaded)

		logger::info("{} in {} ({}){}", OpName(a_op), a_home.name, a_home.cell.ToString(),
			a_op == Op::Retrieve ? std::format(" pattern '{}'", a_pattern) : "");
		const auto started = std::chrono::steady_clock::now();
		struct Timer
		{
			std::chrono::steady_clock::time_point start;
			Op                                    op;
			~Timer()
			{
				const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				logger::debug("{} took {:.1f} ms", OpName(op), ms);
			}
		} timer{ started, a_op };

		switch (a_op) {
		case Op::Deposit:
			{
				const auto                       inventory = game::SnapshotInventory(player, stacks);
				const auto                       keep = CompileKeep(layout);
				std::vector<core::StackDecision> why;
				const bool                       explain = spdlog::should_log(spdlog::level::info);
				const bool                       restocks = sorting.restockAfterDeposit && keep.Restocks();
				moves = planner.Deposit(inventory, keep, &why);
				if (explain) {
					LogDecisions(a_op, inventory, why);
				}
				for (std::size_t i = 0; i < why.size() && i < inventory.size(); ++i) {
					if (why[i].outcome == core::StackDecision::Outcome::NoDestination) {
						stranded += inventory[i].count;
					}
				}
				std::vector<core::ContainerContents> stored;
				if (fill || restocks) {
					stored = SnapshotManaged(layout, available, stacks);
				}
				if (fill) {
					displays = display::Prepare(a_home, cell, DepositStock(inventory, why, stored));
					core::ReserveForDisplays(moves, displays.stocks, displays.plan);
				}
				// Planned from the same snapshots: what Deposit keeps is what Restock counts (a quota the player is
				// short of had nothing of it deposited), and what goes on display stays out of it.
				if (restocks) {
					restock = PlanRestock(planner, inventory, std::move(stored), keep, displays);
				}
			}
			break;
		case Op::Distribute:
			{
				if (!layout.master || !available(*layout.master)) {
					result.message = "Dova Stash: the master container is unavailable";
					result.failed = true;
					return result;
				}
				auto* master = game::LookupRef(*layout.master);
				const auto                       contents = game::SnapshotInventory(master, stacks);
				std::vector<core::StackDecision> why;
				const bool                       explain = spdlog::should_log(spdlog::level::info);
				moves = planner.Distribute(contents, explain ? &why : nullptr);
				if (explain) {
					LogDecisions(a_op, contents, why);
				}
			}
			break;
		case Op::SortAll:
			{
				const auto contents = SnapshotManaged(layout, available, stacks);
				moves = planner.SortAll(contents);
				if (fill) {
					std::vector<core::DisplayStock> stock;
					AddStock(stock, contents);
					displays = display::Prepare(a_home, cell, std::move(stock));
					core::ReserveForDisplays(moves, displays.stocks, displays.plan);
				}
			}
			break;
		case Op::Retrieve:
			moves = planner.Retrieve(SnapshotManaged(layout, available, stacks), a_pattern);
			break;
		case Op::Restock:
			{
				const auto keep = CompileKeep(layout);
				if (!keep.Restocks()) {
					result.message = "Dova Stash: no keep quota restocks";
					return result;
				}
				const auto inventory = game::SnapshotInventory(player, stacks);
				moves = PlanRestock(planner, inventory, SnapshotManaged(layout, available, stacks), keep, displays);
			}
			break;
		}

		result.ran = true;
		if (moves.empty() && restock.empty() && !displays.plan.empty()) {
			logger::info("{}: no moves", OpName(a_op));
			display::Fill(a_home.cellId, displays, stacks);
			return result;  // Fill tells the player what went on display
		}
		if (moves.empty() && restock.empty()) {
			switch (a_op) {
			case Op::Deposit:
				if (stranded > 0) {
					// Everything waits for an area that is not loaded (DESIGN §6.7): say so, not "nothing to deposit".
					const bool away = layout.master && !available(*layout.master);
					result.message = std::format("Dova Stash: nothing deposited ({} left: {})", Plural(stranded, "item"), away ? "master not here" : "no container here takes them");
				} else {
					result.message = "Dova Stash: nothing to deposit";
				}
				break;
			case Op::Distribute:
				result.message = {};  // closing the master with nothing to sort: stay quiet
				break;
			case Op::SortAll:
				result.message = "Dova Stash: everything is already in place";
				break;
			case Op::Retrieve:
				result.message = "Dova Stash: nothing to retrieve";
				break;
			case Op::Restock:
				result.message = "Dova Stash: nothing to restock";
				break;
			}
			logger::info("{}: no moves", OpName(a_op));
			return result;
		}

		const game::EndpointResolver resolve = [&](const core::ContainerRef& a_end) -> RE::TESObjectREFR* {
			if (a_end.IsPlayer()) {
				return player;
			}
			return available(a_end.Ref()) ? game::LookupRef(a_end.Ref()) : nullptr;
		};
		const auto report = moves.empty() ? game::MoveReport{} : game::ItemMover::Execute(moves, stacks, resolve);
		// Restock after Deposit: other stacks (stored ones; the deposit's are the player's), moved after it.
		const auto restocked = restock.empty() ? game::MoveReport{} : game::ItemMover::Execute(restock, stacks, resolve);
		if (!restock.empty()) {
			logger::info("Restock after Deposit: {} in {}", Plural(restocked.items, "item"), Plural(restocked.stacks, "stack"));
		}
		result.items = report.items + restocked.items;
		display::Fill(a_home.cellId, displays, stacks);

		const auto containers = static_cast<std::int32_t>(a_op == Op::Retrieve ? report.sources.size() : report.targets.size());
		switch (a_op) {
		case Op::Deposit:
			if (moves.empty()) {
				result.message = std::format("Dova Stash: restocked {}", Plural(restocked.items, "item"));
			} else {
				result.message = std::format("Dova Stash: deposited {} into {}", Plural(report.items, "item"), Plural(containers, "container"));
				if (restocked.items > 0) {
					result.message += std::format(", restocked {}", restocked.items);
				}
			}
			if (stranded > 0) {
				// The master and their containers are in an area that is not loaded (DESIGN §6.7): they wait for it.
				const bool away = layout.master && !available(*layout.master);
				result.message += std::format(" ({} left: {})", Plural(stranded, "item"), away ? "master not here" : "no container here takes them");
			}
			break;
		case Op::Distribute:
			result.message = std::format("Dova Stash: sorted {} into {}", Plural(report.items, "item"), Plural(containers, "container"));
			break;
		case Op::SortAll:
			result.message = std::format("Dova Stash: re-sorted {} into {}", Plural(report.items, "item"), Plural(containers, "container"));
			break;
		case Op::Retrieve:
			result.message = std::format("Dova Stash: retrieved {} from {}", Plural(report.items, "item"), Plural(containers, "container"));
			break;
		case Op::Restock:
			result.message = std::format("Dova Stash: restocked {}", Plural(report.items, "item"));
			break;
		}
		if (const auto skipped = report.skipped + report.errors + restocked.skipped + restocked.errors; skipped > 0) {
			result.message += std::format(" ({} skipped, see DovaStash.log)", skipped);
		}
		return result;
	}

	bool Restocks(const home::Home& a_home)
	{
		return a_home.layout && core::RestockApplies(home::Store::Get().GlobalKeep(), a_home.layout->keep ? &*a_home.layout->keep : nullptr);
	}

	Result FillDisplay(home::Home& a_home, const core::FormRef& a_display)
	{
		Result result;
		auto*  player = game::Player();
		auto*  cell = player ? player->GetParentCell() : nullptr;
		auto*  ref = game::LookupRef(a_display);
		if (!a_home.layout || !ref || !cell || cell->GetFormID() != a_home.cellId) {
			return result;
		}
		const auto name = game::RefName(ref);
		if (!Settings::GetSingleton().GetSorting().fillDisplays) {
			result.message = "Dova Stash: putting gear on displays is off in the settings";
			result.failed = true;
			return result;
		}
		if (display::Busy()) {
			result.message = "Dova Stash: still putting things on display; try again in a moment";
			result.failed = true;
			return result;
		}
		if (const auto found = game::DisplayAt(ref)) {
			if (!game::DisplayAvailable(*found)) {
				result.message = std::format("Dova Stash: {} isn't built yet", name);
				result.failed = true;
				return result;
			}
			if (game::DisplayFullFor(*found, a_home.layout->FindDisplay(a_display))) {
				result.message = std::format("Dova Stash: {} is full", name);
				return result;
			}
		}

		const auto&       layout = *a_home.layout;
		Availability      available(a_home);
		core::SortPlanner planner(layout, home::Store::Get().Categories(), available);
		game::StackTable  stacks;
		const auto        inventory = game::SnapshotInventory(player, stacks);
		std::vector<core::StackDecision> why;
		(void)planner.Deposit(inventory, CompileKeep(layout), &why);
		const auto displays = display::Prepare(a_home, cell, DepositStock(inventory, why, SnapshotManaged(layout, available, stacks)), ref->GetFormID());
		if (displays.plan.empty()) {
			result.message = std::format("Dova Stash: nothing you have or store fits {}", name);
			return result;
		}
		result.ran = true;
		display::Fill(a_home.cellId, displays, stacks);  // tells the player what went up
		return result;
	}

	std::map<std::string, std::int32_t> LearnedByCategory(const core::Layout& a_layout, const core::FormRef& a_container)
	{
		std::map<std::string, std::int32_t> out;
		const auto&                         categories = home::Store::Get().Categories();
		for (const auto& [item, container] : a_layout.learned) {
			if (!(container == a_container)) {
				continue;
			}
			auto*          form = game::LookupForm(item);
			auto*          object = form ? form->As<RE::TESBoundObject>() : nullptr;
			core::ItemInfo info;
			if (!object || !game::FillBaseInfo(info, object)) {
				continue;
			}
			if (const auto category = categories.Classify(info)) {
				++out[*category];
			}
		}
		return out;
	}

	std::map<std::string, std::int32_t> CountByCategory(home::Home& a_home)
	{
		std::map<std::string, std::int32_t> counts;
		if (!a_home.layout) {
			return counts;
		}
		auto* player = game::Player();
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!cell || cell->GetFormID() != a_home.cellId) {
			return counts;
		}
		const auto&      categories = home::Store::Get().Categories();
		Availability     available(a_home, true);
		game::StackTable stacks;
		for (const auto& contents : SnapshotManaged(*a_home.layout, available, stacks)) {
			for (const auto& item : contents.items) {
				if (!core::SortPlanner::CanMove(item)) {
					continue;
				}
				counts[categories.Classify(item).value_or(std::string{})] += item.count;
			}
		}
		return counts;
	}
}
