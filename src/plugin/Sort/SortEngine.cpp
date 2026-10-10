#include "Sort/SortEngine.h"

#include "Game/Eligibility.h"
#include "Game/FormUtil.h"
#include "Game/ItemAdapter.h"
#include "Game/ItemMover.h"
#include "Home/Store.h"

namespace dova::sort
{
	namespace
	{
		// Per-operation container availability (DESIGN §9 "templates bypass none of these checks").
		// Copyable (core stores it in a std::function); the cache is shared.
		class Availability
		{
		public:
			// `a_quiet`: log unavailable containers at debug level only (frequent read-only queries such
			// as CountByCategory would otherwise repeat the warning for every unbuilt Hearthfire wing).
			explicit Availability(RE::TESObjectCELL* a_cell, bool a_quiet = false) :
				_cell(a_cell),
				_quiet(a_quiet),
				_cache(std::make_shared<std::map<core::FormRef, bool>>())
			{}

			bool operator()(const core::FormRef& a_ref) const
			{
				if (const auto it = _cache->find(a_ref); it != _cache->end()) {
					return it->second;
				}
				auto*      ref = game::LookupRef(a_ref);
				const auto verdict = ref ? game::CheckContainer(ref, _cell) : game::Verdict::Reject("reference not found (plugin missing?)");
				if (!verdict.ok) {
					if (_quiet) {
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
			RE::TESObjectCELL*                            _cell;
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

		std::string Plural(std::int32_t a_n, std::string_view a_word)
		{
			return std::format("{} {}{}", a_n, a_word, a_n == 1 ? "" : "s");
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
		Availability available(cell);
		core::SortPlanner planner(layout, categories, available);
		game::StackTable  stacks;
		std::vector<core::Move> moves;

		logger::info("{} in {} ({}){}", OpName(a_op), a_home.name, a_home.cell.ToString(),
			a_op == Op::Retrieve ? std::format(" pattern '{}'", a_pattern) : "");

		switch (a_op) {
		case Op::Deposit:
			{
				const auto        inventory = game::SnapshotInventory(player, stacks);
				core::Diagnostics diags;
				const auto        keep = core::KeepRules::Compile(home::Store::Get().GlobalKeep(), layout.keep ? &*layout.keep : nullptr, diags);
				for (const auto& d : diags.Items()) {
					logger::debug("Keep rules: {}", d.ToString());
				}
				moves = planner.Deposit(inventory, keep);
			}
			break;
		case Op::Distribute:
			{
				if (!layout.master || !available(*layout.master)) {
					result.message = "Dova Stash: the master container is unavailable";
					return result;
				}
				auto* master = game::LookupRef(*layout.master);
				const auto contents = game::SnapshotInventory(master, stacks);
				moves = planner.Distribute(contents);
			}
			break;
		case Op::SortAll:
			moves = planner.SortAll(SnapshotManaged(layout, available, stacks));
			break;
		case Op::Retrieve:
			moves = planner.Retrieve(SnapshotManaged(layout, available, stacks), a_pattern);
			break;
		}

		result.ran = true;
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
		Availability     available(cell, true);
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
