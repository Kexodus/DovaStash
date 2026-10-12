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
					// Out of reach from this area of a multi-cell home is expected, not a problem: debug.
					if (_quiet || reach == core::Reach::Unreachable || (!ref && _home->MultiCell())) {
						logger::debug("Container {} unavailable: {}", a_ref.ToString(), verdict.reason);
					} else {
						logger::warn("Container {} unavailable: {}", a_ref.ToString(), verdict.reason);
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
				if (!c.ignored) {
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
		std::vector<core::DisplayStock> DepositStock(const core::Layout& a_layout, std::span<const core::ItemInfo> a_inventory,
			std::span<const core::StackDecision> a_why, const Availability& a_available, game::StackTable& a_stacks)
		{
			std::vector<core::DisplayStock> stock;
			for (std::size_t i = 0; i < a_inventory.size() && i < a_why.size(); ++i) {
				if (a_why[i].outcome == core::StackDecision::Outcome::Move && a_why[i].moved > 0) {
					stock.push_back(core::DisplayStock{ core::ContainerRef::Player(), a_inventory[i], a_why[i].moved });
				}
			}
			AddStock(stock, SnapshotManaged(a_layout, a_available, a_stacks));
			return stock;
		}

		std::string Plural(std::int32_t a_n, std::string_view a_word)
		{
			return std::format("{} {}{}", a_n, a_word, a_n == 1 ? "" : "s");
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
			return result;
		}

		const auto& layout = *a_home.layout;
		const auto& categories = home::Store::Get().Categories();
		Availability available(a_home);
		core::SortPlanner planner(layout, categories, available);
		game::StackTable  stacks;
		std::vector<core::Move> moves;
		display::Prepared displays;
		const bool        fill = Settings::GetSingleton().GetSorting().fillDisplays;

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
				moves = planner.Deposit(inventory, keep, explain || fill ? &why : nullptr);
				if (explain) {
					LogDecisions(a_op, inventory, why);
				}
				if (fill) {
					displays = display::Prepare(a_home, cell, DepositStock(layout, inventory, why, available, stacks));
					core::ReserveForDisplays(moves, displays.stocks, displays.plan);
				}
			}
			break;
		case Op::Distribute:
			{
				if (!layout.master || !available(*layout.master)) {
					result.message = "Dova Stash: the master container is unavailable";
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
		}

		result.ran = true;
		if (moves.empty() && !displays.plan.empty()) {
			logger::info("{}: no moves", OpName(a_op));
			display::Fill(a_home.cellId, displays, stacks);
			return result;  // Fill tells the player what went on display
		}
		if (moves.empty()) {
			switch (a_op) {
			case Op::Deposit:
				result.message = "Dova Stash: nothing to deposit";
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
		const auto report = game::ItemMover::Execute(moves, stacks, resolve);
		result.items = report.items;
		display::Fill(a_home.cellId, displays, stacks);

		const auto containers = static_cast<std::int32_t>(a_op == Op::Retrieve ? report.sources.size() : report.targets.size());
		switch (a_op) {
		case Op::Deposit:
			result.message = std::format("Dova Stash: deposited {} into {}", Plural(report.items, "item"), Plural(containers, "container"));
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
		}
		if (report.skipped > 0 || report.errors > 0) {
			result.message += std::format(" ({} skipped, see log)", report.skipped + report.errors);
		}
		return result;
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
			return result;
		}
		if (display::Busy()) {
			result.message = "Dova Stash: still putting things on display; try again in a moment";
			return result;
		}
		if (const auto found = game::DisplayAt(ref)) {
			if (!game::DisplayAvailable(*found)) {
				result.message = std::format("Dova Stash: {} isn't built yet", name);
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
		const auto displays = display::Prepare(a_home, cell, DepositStock(layout, inventory, why, available, stacks), ref->GetFormID());
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
