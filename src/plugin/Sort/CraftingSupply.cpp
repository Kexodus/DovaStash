#include "Sort/CraftingSupply.h"

#include "Controller.h"
#include "Game/Eligibility.h"
#include "Game/FormUtil.h"
#include "Game/ItemAdapter.h"
#include "Game/ItemMover.h"
#include "Game/RecipeIndex.h"
#include "Home/HomeRegistry.h"
#include "Settings.h"
#include "Sort/CraftingDetail.h"
#include "Sort/CraftingStorage.h"

#include <dova/core/Crafting.h>
#include <dova/core/CraftingStorage.h>

namespace dova::sort::crafting
{
	namespace detail
	{
		namespace
		{
			namespace cc = core::crafting;

			// A stack's identity within its form (core::crafting::Stack::key): "" for the plain remainder; the extra
			// data that may travel (a soul, a stolen item's owner; the count and a unique id carry no meaning here);
			// nullopt for anything else (tempered, enchanted, renamed, poisoned, worn, favorited, quest alias...).
			std::optional<std::string> KeyOf(RE::ExtraDataList* a_extra)
			{
				std::string key;
				if (!a_extra) {
					return key;
				}
				for (const auto& data : *a_extra) {
					switch (data.GetType()) {
					case RE::ExtraDataType::kCount:
					case RE::ExtraDataType::kUniqueID:
						break;
					case RE::ExtraDataType::kOwnership:
						{
							const auto* owner = static_cast<const RE::ExtraOwnership&>(data).owner;
							key += std::format("owner={:08X};", owner ? owner->GetFormID() : 0);
						}
						break;
					case RE::ExtraDataType::kSoul:
						key += std::format("soul={};", static_cast<int>(static_cast<const RE::ExtraSoul&>(data).soul.get()));
						break;
					default:
						return std::nullopt;
					}
				}
				return key;
			}
		}

		std::vector<cc::Stack> Stacks(RE::TESObjectREFR* a_ref, game::StackTable& a_table)
		{
			std::vector<cc::Stack> out;
			for (auto& item : game::SnapshotInventory(a_ref, a_table)) {
				std::optional<std::string> key;  // unknown stack: never moves
				if (const auto* stack = a_table.Find(item.stackId)) {
					key = KeyOf(stack->extra);
				}
				out.push_back(cc::Stack{ std::move(item), std::move(key) });
			}
			return out;
		}

		std::vector<cc::Stored> SnapshotStored(const home::Home& a_home, game::StackTable& a_table, const std::vector<core::FormRef>* a_only)
		{
			std::vector<cc::Stored> out;
			const auto&             layout = *a_home.layout;
			auto add = [&](const core::FormRef& a_ref) {
				if (a_only && std::ranges::find(*a_only, a_ref) == a_only->end()) {
					return;
				}
				if (std::ranges::any_of(out, [&](const cc::Stored& s) { return s.container == a_ref; })) {
					return;
				}
				auto* ref = game::LookupRef(a_ref);
				if (!ref || !home::CheckHomeContainer(ref, a_home).ok) {
					return;
				}
				out.push_back(cc::Stored{ a_ref, Stacks(ref, a_table) });
			};
			if (layout.master) {
				add(*layout.master);
			}
			for (const auto& c : layout.containers) {
				if (!c.ignored) {
					add(c.ref);
				}
			}
			return out;
		}

		game::EndpointResolver Resolver(RE::TESObjectREFR* a_player, const home::Home& a_home)
		{
			return [a_player, place = &a_home](const core::ContainerRef& a_end) -> RE::TESObjectREFR* {
				if (a_end.IsPlayer()) {
					return a_player;
				}
				auto* ref = game::LookupRef(a_end.Ref());
				return ref && home::CheckHomeContainer(ref, *place).ok ? ref : nullptr;
			};
		}

		std::string FormName(const core::FormRef& a_form)
		{
			return game::Describe(game::LookupForm(a_form));
		}

		std::string Plural(std::int32_t a_n, std::string_view a_word)
		{
			return std::format("{} {}{}", a_n, a_word, a_n == 1 ? "" : "s");
		}

		cc::SupplyRequest RequestFor(const RE::TESFurniture* a_furniture)
		{
			using Bench = RE::TESFurniture::WorkBenchData::BenchType;
			cc::SupplyRequest request;
			switch (a_furniture->workBenchData.benchType.get()) {
			case Bench::kAlchemy:
			case Bench::kAlchemyExperiment:
				request.station = cc::Station::Alchemy;
				break;
			case Bench::kEnchanting:
			case Bench::kEnchantingExperiment:
				request.station = cc::Station::Enchanting;
				break;
			default:
				request.station = cc::Station::Recipes;
				break;
			}
			const auto keywords = game::KeywordNames(a_furniture);
			request.materials = game::Recipes().MaterialsFor(keywords);
			request.gear = Settings::GetSingleton().GetSorting().craftingSupplyGear;
			return request;
		}
	}

	namespace
	{
		namespace cc = core::crafting;
		using Control = RE::BSEventNotifyControl;
		using detail::FormName;
		using detail::Plural;

		constexpr std::string_view kMenuTrigger = "crafting menu";

		struct Session
		{
			RE::FormID  cellId{ 0 };
			RE::FormID  station{ 0 };
			std::string stationName;
			cc::Ledger  ledger;
		};

		// Session state: touched by SKSE tasks and by Reset (kPreLoadGame, main thread).
		std::mutex             g_lock;
		std::optional<Session> g_session;

		// Copies that reached the player while a station is in use, from anywhere but a pull. Written by the
		// container-changed sink (any thread).
		std::mutex                       g_craftedLock;
		std::map<RE::FormID, std::int32_t> g_crafted;  // base form -> copies
		std::set<RE::FormID>             g_sources;    // containers the pull took from
		std::atomic<bool>                g_open{ false };
		std::atomic<bool>                g_pulling{ false };

		void Enqueue(std::function<void()> a_task)
		{
			if (const auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask(std::move(a_task));
			}
		}

		void StopCounting()
		{
			std::scoped_lock lock(g_craftedLock);
			g_open = false;
			g_crafted.clear();
			g_sources.clear();
		}

		void FinishLocked(std::string_view a_why);

		// Brings the station's materials (Move mode).
		void PullLocked(RE::TESObjectREFR* a_station, const home::Home& a_home, const cc::SupplyRequest& a_request, std::string_view a_trigger)
		{
			auto*                         player = game::Player();
			const storage::Busy           busy;  // our own moves: the crafting hooks stay out of them
			game::StackTable              table;
			const auto                    storedBefore = detail::SnapshotStored(a_home, table);
			const auto                    playerBefore = cc::CountStacks(detail::Stacks(player, table));
			const auto                    pulls = cc::PlanPull(a_request, storedBefore);
			logger::debug("Crafting supply: {} ({}, {} materials in its recipes): {} stacks to bring", game::Describe(a_station), a_trigger,
				a_request.materials.size(), pulls.size());
			if (pulls.empty()) {
				return;
			}

			std::vector<core::Move> moves;
			{
				std::scoped_lock lock(g_craftedLock);
				g_crafted.clear();
				g_sources.clear();
				for (const auto& p : pulls) {
					moves.push_back(p.move);
					if (auto* ref = game::LookupRef(p.move.from.Ref())) {
						g_sources.insert(ref->GetFormID());
					}
				}
				g_open = true;
			}
			g_pulling = true;
			const auto report = game::ItemMover::Execute(moves, table, detail::Resolver(player, a_home));
			g_pulling = false;

			game::StackTable afterTable;
			const auto       storedAfter = detail::SnapshotStored(a_home, afterTable);
			auto             ledger = cc::MakeLedger(pulls, storedBefore, storedAfter, playerBefore);
			if (ledger.Empty()) {
				StopCounting();
				logger::info("Crafting supply: nothing could be brought to {} ({} skipped)", game::Describe(a_station), report.skipped);
				return;
			}
			for (const auto& p : ledger.pulled) {
				logger::debug("Crafting supply: brought {} x{} from {}{}", FormName(p.form), p.count, game::Describe(game::LookupRef(p.from)),
					p.key.empty() ? "" : std::format(" ({})", p.key));
			}
			const auto total = ledger.Total();
			logger::info("Crafting supply: brought {} ({} kinds) to {} in {}", Plural(total, "item"), ledger.Keys().size(),
				game::Describe(a_station), a_home.name);
			g_session = Session{ a_home.cellId, a_station->GetFormID(), game::RefName(a_station), std::move(ledger) };

			RE::SendUIMessage::SendInventoryUpdateMessage(player, nullptr);  // in case the crafting menu is already up
			Controller::Get().Notify(std::format("Dova Stash: {} at hand", Plural(total, "material")));
		}

		// `a_forceMove`: Craft from storage gave up at this station (FallBackToMove).
		void BeginLocked(RE::TESObjectREFR* a_station, std::string_view a_trigger, bool a_forceMove = false)
		{
			if (!a_station) {
				return;
			}
			if (g_session) {
				if (g_session->station == a_station->GetFormID()) {
					return;  // already brought (the furniture event, then the menu)
				}
				FinishLocked("another station");
			}
			if (storage::Active()) {
				if (storage::StationId() == a_station->GetFormID() && !a_forceMove) {
					return;  // already crafting from storage here (the furniture event, then the menu)
				}
				storage::End(a_forceMove ? "falling back to bringing the materials" : "another station");
			}
			auto* base = a_station->GetBaseObject();
			const auto* furniture = base ? base->As<RE::TESFurniture>() : nullptr;
			if (!furniture) {
				return;
			}
			const bool workbench = furniture->workBenchData.benchType.get() != RE::TESFurniture::WorkBenchData::BenchType::kNone;
			const auto status = Mode();
			if (status.resolution.mode == cc::SupplyMode::Off) {
				if (workbench) {
					logger::debug("Crafting supply: off{}", status.resolution.why.empty() ? "" : std::format(" ({})", status.resolution.why));
				}
				return;
			}
			auto* home = home::HomeRegistry::Get().Current();
			auto* player = game::Player();
			auto* cell = player ? player->GetParentCell() : nullptr;
			if (!home || !home->layout || home->layoutBroken || !cell || cell->GetFormID() != home->cellId) {
				if (workbench) {
					logger::debug("Crafting supply: {} is not in a home that is set up", game::Describe(a_station));
				}
				return;
			}
			const auto request = detail::RequestFor(furniture);
			if (request.Empty()) {
				if (workbench) {
					logger::debug("Crafting supply: no recipe uses {}'s keywords", game::Describe(a_station));
				}
				return;
			}

			auto mode = cc::ModeFor(status.resolution.mode, request.station);
			if (a_forceMove) {
				mode = cc::SupplyMode::Move;
			}
			if (mode == cc::SupplyMode::Storage) {
				if (a_trigger == kMenuTrigger) {
					// The menu built its lists before Dova Stash saw the station: they would not show the stored
					// materials, so this visit brings them instead.
					logger::info("Crafting from storage: {} was already open when the station was noticed; bringing the materials instead",
						game::Describe(a_station));
				} else if (storage::Begin(a_station, *home, request)) {
					return;
				} else {
					logger::info("Crafting from storage: could not start at {}; bringing the materials instead", game::Describe(a_station));
				}
			}
			PullLocked(a_station, *home, request, a_trigger);
		}

		void FinishLocked(std::string_view a_why)
		{
			if (storage::Active()) {
				storage::End(a_why);
			}
			if (!g_session) {
				return;
			}
			auto session = std::move(*g_session);
			g_session.reset();
			std::map<RE::FormID, std::int32_t> crafted;
			{
				std::scoped_lock lock(g_craftedLock);
				g_open = false;
				crafted.swap(g_crafted);
				g_sources.clear();
			}

			auto* player = game::Player();
			auto* cell = player ? player->GetParentCell() : nullptr;
			auto* place = cell && cell->GetFormID() == session.cellId ? home::HomeRegistry::Get().CurrentIf(session.cellId) : nullptr;
			if (!place || !place->layout) {
				logger::info("Crafting supply: left the home before getting up from {}; the {} brought stay with you",
					session.stationName, Plural(session.ledger.Total(), "item"));
				return;
			}

			std::map<core::FormRef, std::int32_t> craftedRefs;
			for (const auto& [id, n] : crafted) {
				if (auto ref = game::ToFormRef(RE::TESForm::LookupByID(id))) {
					craftedRefs[*ref] += n;
				}
				logger::debug("Crafting supply: +{} {} while at {} (crafted; stays with you)", n, game::Describe(RE::TESForm::LookupByID(id)),
					session.stationName);
			}

			const storage::Busy busy;
			game::StackTable    table;
			const auto          playerStacks = detail::Stacks(player, table);
			const auto          plan = cc::PlanReturn(session.ledger, cc::CountStacks(playerStacks), craftedRefs);
			for (const auto& line : plan.lines) {
				logger::debug("Crafting supply: {}{}: had {}, brought {}, crafted {}, now {} -> {} back", FormName(line.stack.form),
					line.stack.key.empty() ? "" : std::format(" ({})", line.stack.key), line.before, line.pulled, line.crafted, line.now, line.back);
			}
			const auto moves = cc::ReturnMoves(plan.back, playerStacks);
			game::MoveReport report;
			if (!moves.empty()) {
				report = game::ItemMover::Execute(moves, table, detail::Resolver(player, *place));
				RE::SendUIMessage::SendInventoryUpdateMessage(player, nullptr);
			}
			logger::info("Crafting supply ({}): put {} of the {} brought to {} back", a_why, report.items, plan.pulled, session.stationName);
			if (report.items > 0) {
				Controller::Get().Notify(std::format("Dova Stash: put {} back", report.items));
			}
		}

		void Begin(RE::FormID a_station, std::string_view a_trigger, bool a_forceMove = false)
		{
			std::scoped_lock lock(g_lock);
			auto*            form = RE::TESForm::LookupByID(a_station);
			BeginLocked(form ? form->AsReference() : nullptr, a_trigger, a_forceMove);
		}

		void Finish(std::string_view a_why)
		{
			std::scoped_lock lock(g_lock);
			FinishLocked(a_why);
		}

		void OnMenuOpened()
		{
			{
				std::scoped_lock lock(g_lock);
				if (g_session || storage::Active()) {
					return;
				}
			}
			auto* player = game::Player();
			if (!player) {
				return;
			}
			const auto station = player->GetOccupiedFurniture().get();
			if (station) {
				Begin(station->GetFormID(), kMenuTrigger);
			}
		}

		void OnStationLeft()
		{
			if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME)) {
				return;  // returned when the menu closes
			}
			Finish("got up");
		}

		class FurnitureSink final : public RE::BSTEventSink<RE::TESFurnitureEvent>
		{
		public:
			Control ProcessEvent(const RE::TESFurnitureEvent* a_event, RE::BSTEventSource<RE::TESFurnitureEvent>*) override
			{
				if (!a_event || !a_event->actor || a_event->actor->GetFormID() != game::kPlayerRefID) {
					return Control::kContinue;
				}
				if (a_event->type.get() == RE::TESFurnitureEvent::FurnitureEventType::kEnter) {
					const auto station = a_event->targetFurniture ? a_event->targetFurniture->GetFormID() : 0;
					if (station) {
						Enqueue([station]() { Begin(station, "sat down"); });
					}
				} else {
					Enqueue([]() { OnStationLeft(); });
				}
				return Control::kContinue;
			}
		};

		class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			Control ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_event || a_event->menuName != RE::CraftingMenu::MENU_NAME) {
					return Control::kContinue;
				}
				if (a_event->opening) {
					Enqueue([]() { OnMenuOpened(); });
				} else {
					Enqueue([]() { Finish("crafting menu closed"); });
				}
				return Control::kContinue;
			}
		};

		// Copies reaching the player while a station is in use, other than the pull's own: crafted (the game
		// adds them from no container) or handed over by a script. They are never sent back.
		class ContainerSink final : public RE::BSTEventSink<RE::TESContainerChangedEvent>
		{
		public:
			Control ProcessEvent(const RE::TESContainerChangedEvent* a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
			{
				if (!g_open || g_pulling || !a_event || a_event->newContainer != game::kPlayerRefID || a_event->itemCount <= 0 ||
					a_event->oldContainer == game::kPlayerRefID) {
					return Control::kContinue;
				}
				std::scoped_lock lock(g_craftedLock);
				if (g_open && !g_sources.contains(a_event->oldContainer)) {
					g_crafted[a_event->baseObj] += a_event->itemCount;
				}
				return Control::kContinue;
			}
		};

		FurnitureSink g_furnitureSink;
		MenuSink      g_menuSink;
		ContainerSink g_containerSink;
	}

	void detail::FallBackToMove(RE::FormID a_station, std::string a_why)
	{
		Enqueue([a_station, why = std::move(a_why)]() {
			std::scoped_lock lock(g_lock);
			if (!storage::Active() || storage::StationId() != a_station) {
				return;
			}
			logger::info("Crafting from storage: {}; bringing the materials for the rest of this visit", why);
			auto* form = RE::TESForm::LookupByID(a_station);
			BeginLocked(form ? form->AsReference() : nullptr, "fallback", true);
		});
	}

	void RegisterSinks()
	{
		if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink<RE::TESFurnitureEvent>(&g_furnitureSink);
			holder->AddEventSink<RE::TESContainerChangedEvent>(&g_containerSink);
		}
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(&g_menuSink);
		}
		logger::info("Events: crafting station sinks registered");
	}

	void Reset(std::string_view a_why)
	{
		std::scoped_lock lock(g_lock);
		if (storage::Active()) {
			storage::End(a_why);
		}
		if (g_session) {
			logger::info("Crafting supply: {}; the {} brought to {} stay with you", a_why, Plural(g_session->ledger.Total(), "item"),
				g_session->stationName);
			g_session.reset();
		}
		StopCounting();
	}
}
