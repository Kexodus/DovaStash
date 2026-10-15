#include "Sort/CraftingStorage.h"

#include "Controller.h"
#include "Game/FormUtil.h"
#include "Game/ItemAdapter.h"
#include "Game/ItemMover.h"
#include "Home/Areas.h"
#include "Home/HomeRegistry.h"
#include "Home/Store.h"
#include "Hooks/CraftingHooks.h"
#include "Settings.h"
#include "Sort/CraftingDetail.h"

#include <Windows.h>

#include <mutex>

namespace dova::sort::crafting
{
	namespace
	{
		namespace cc = core::crafting;

		std::mutex g_modeLock;
		ModeStatus g_mode;
		bool       g_modeLogged{ false };

		bool IsDll(std::string_view a_file)
		{
			return a_file.size() > 4 && _strnicmp(a_file.data() + a_file.size() - 4, ".dll", 4) == 0;
		}

		// Loaded plugins, and the known crafting mods' DLLs that are loaded (SKSE loads every DLL before kPostLoad).
		std::vector<std::string> LoadedFiles()
		{
			auto out = game::LoadedPlugins();
			for (const auto& m : cc::KnownCraftingStorageMods()) {
				if (IsDll(m.file) && GetModuleHandleA(std::string(m.file).c_str())) {
					out.emplace_back(m.file);
				}
			}
			return out;
		}
	}

	void ResolveMode(std::string_view a_why)
	{
		ModeStatus status;
		status.otherMods = cc::DetectOtherMods(LoadedFiles());
		status.hooksAvailable = hooks::crafting::Available();
		const auto setting = cc::ToSupplySetting(Settings::GetSingleton().GetSorting().craftingSupply);
		status.resolution = cc::ResolveMode(setting, status.hooksAvailable, status.otherMods);
		if (status.resolution.mode == cc::SupplyMode::Storage && !hooks::crafting::EnsureInstalled()) {
			status.resolution = cc::ResolveMode(setting, false, status.otherMods);
		}
		status.hooksInstalled = hooks::crafting::Installed();

		std::scoped_lock lock(g_modeLock);
		const bool changed = !g_modeLogged || g_mode.resolution.mode != status.resolution.mode || g_mode.resolution.why != status.resolution.why;
		if (changed) {
			logger::info("Materials at the workbench: {} ({}, {}){}", cc::SupplyModeName(status.resolution.mode),
				status.resolution.chosen ? "your setting" : "auto", a_why, status.resolution.why.empty() ? "" : std::format(": {}", status.resolution.why));
			g_modeLogged = true;
		}
		g_mode = std::move(status);
	}

	ModeStatus Mode()
	{
		std::scoped_lock lock(g_modeLock);
		return g_mode;
	}

	namespace storage
	{
		namespace
		{
			struct Material
			{
				core::FormRef        form;
				std::int32_t         count{ 0 };  // copies the containers can give
				std::vector<core::FormRef> containers;  // in the order they give
			};

			struct Session
			{
				std::uint64_t     id{ 0 };  // tells a consumption that ends after End (or in a new session) apart
				RE::FormID        station{ 0 };
				RE::FormID        cellId{ 0 };
				std::string       stationName;
				cc::SupplyRequest request;
				// A copy of the home, taken by Begin (a task): Consume runs on the crafting menu's thread, which never
				// touches the live Home (DESIGN §5, Threading).
				std::shared_ptr<const home::Home>                  home;
				std::unordered_map<RE::TESBoundObject*, Material> materials;

				// The alchemy list as last counted: the stored ingredients the player does not carry.
				std::vector<RE::TESBoundObject*> listExtra;
				bool                             viewing{ false };
				bool                             playable{ true };

				std::int32_t            crafts{ 0 };  // consumptions that needed the containers
				std::int32_t            used{ 0 };    // copies taken from the containers
				std::set<core::FormRef> usedFrom;
			};

			// The hooks run on whatever thread the engine calls them from (on AE the crafting menu's thread is not the
			// one SKSE tasks drain on, and neither is fixed), so the session is shared between threads:
			//   - g_lock guards g_session. It is held only to copy or update session data, never while calling
			//     into the game, so it cannot deadlock with the engine's own locks;
			//   - g_active / g_stationHandle / g_stationId mirror the session for the hooks' first checks (no lock);
			//   - g_consumeLock serializes consumptions (one craft at a time); the count hooks never take it;
			//   - t_busy (Busy) is per thread: Dova Stash's own counts and moves on one thread never see the hooked
			//     answers, while the menu on another thread still does.
			std::mutex                 g_lock;
			std::optional<Session>     g_session;
			std::uint64_t              g_nextId{ 1 };
			std::atomic<bool>          g_active{ false };
			std::atomic<std::uint32_t> g_stationHandle{ 0 };
			std::atomic<RE::FormID>    g_stationId{ 0 };
			std::mutex                 g_consumeLock;
			thread_local std::int32_t  t_busy{ 0 };

			std::string Names(const std::set<core::FormRef>& a_refs)
			{
				std::string out;
				for (const auto& r : a_refs) {
					out += (out.empty() ? "" : ", ") + game::RefName(game::LookupRef(r));
				}
				return out;
			}

			std::string Names(const std::vector<core::FormRef>& a_refs)
			{
				return Names(std::set<core::FormRef>(a_refs.begin(), a_refs.end()));
			}

			// Copies the containers in `a_stored` can give of `a_form`.
			std::int32_t Available(const cc::SupplyRequest& a_request, std::span<const cc::Stored> a_stored, const core::FormRef& a_form)
			{
				const auto supply = cc::MakeSupply(a_request, a_stored);
				const auto it = supply.find(a_form);
				return it != supply.end() ? it->second.count : 0;
			}

			// The hooks stop answering at once (the session itself lasts until End).
			void Deactivate()
			{
				g_active = false;
				g_stationHandle = 0;
			}

			// The craft used fewer copies than were brought for it: those go back where they came from.
			std::int32_t PutBack(std::string_view a_stationName, const home::Home& a_home, const std::vector<cc::Pull>& a_takes, std::int32_t a_unused)
			{
				auto*                 player = game::Player();
				std::vector<cc::Back> back;
				for (auto it = a_takes.rbegin(); it != a_takes.rend() && a_unused > 0; ++it) {
					const auto n = std::min(a_unused, it->move.count);
					back.push_back(cc::Back{ it->move.form, it->key, it->move.from.Ref(), n });
					a_unused -= n;
				}
				game::StackTable table;
				const auto       moves = cc::ReturnMoves(back, detail::Stacks(player, table));
				if (moves.empty()) {
					return 0;
				}
				const auto report = game::ItemMover::Execute(moves, table, detail::Resolver(player, a_home));
				logger::info("Crafting from storage: the craft at {} used fewer than it asked for; put {} back", a_stationName, report.items);
				return report.items;
			}

			// The crafting menu was open before the session began, so it built its recipe list without the stored
			// materials: the list is built again (what the menu itself does after every craft). UI task, where the
			// menu runs.
			void RefreshOpenMenu(RE::FormID a_station)
			{
				const auto* tasks = SKSE::GetTaskInterface();
				if (!tasks) {
					return;
				}
				tasks->AddUITask([a_station]() {
					if (!g_active.load() || g_stationId.load() != a_station) {
						return;
					}
					auto* ui = RE::UI::GetSingleton();
					const auto menu = ui ? ui->GetMenu<RE::CraftingMenu>() : RE::GPtr<RE::CraftingMenu>{};
					auto*      sub = menu ? menu->GetCraftingSubMenu() : nullptr;
					if (auto* recipes = sub ? skyrim_cast<RE::CraftingSubMenus::ConstructibleObjectMenu*>(sub) : nullptr) {
						recipes->UpdateConstructibleList();
						logger::debug("Crafting from storage: the crafting menu was open before the session began; its recipe list was built again");
					}
				});
			}
		}

		Busy::Busy() noexcept
		{
			++t_busy;
		}

		Busy::~Busy()
		{
			--t_busy;
		}

		bool Begin(RE::TESObjectREFR* a_station, const home::Home& a_home, const core::crafting::SupplyRequest& a_request)
		{
			if (!a_station || !a_home.layout || !hooks::crafting::Installed()) {
				return false;
			}
			const Busy busy;

			game::StackTable        table;
			const auto              stored = detail::SnapshotStored(a_home, table);
			const core::SortPlanner planner(*a_home.layout, home::Store::Get().Categories());
			const auto              supply = cc::MakeSupply(a_request, stored, [&](const core::ItemInfo& a_item) -> std::optional<core::FormRef> {
                const auto dest = planner.ResolveDestination(a_item);
                return dest ? std::optional(dest->container) : std::nullopt;
            });

			Session session;
			session.station = a_station->GetFormID();
			session.cellId = a_home.cellId;
			session.stationName = game::RefName(a_station);
			session.request = a_request;
			session.home = std::make_shared<const home::Home>(a_home);
			std::int32_t            total = 0;
			std::set<core::FormRef> containers;
			for (const auto& [form, f] : supply) {
				auto* form0 = game::LookupForm(form);
				auto* object = form0 ? form0->As<RE::TESBoundObject>() : nullptr;
				if (!object || f.count <= 0) {
					continue;
				}
				session.materials.emplace(object, Material{ form, f.count, f.containers });
				total += f.count;
				containers.insert(f.containers.begin(), f.containers.end());
				logger::debug("Crafting from storage: {} x{} in {}", detail::FormName(form), f.count, Names(f.containers));
			}
			logger::info("Crafting from storage at {} in {}: {} ({} kinds) in {}", game::Describe(a_station), a_home.name,
				detail::Plural(total, "material"), session.materials.size(), detail::Plural(static_cast<std::int32_t>(containers.size()), "container"));

			const auto stationId = session.station;
			{
				std::scoped_lock lock(g_lock);
				session.id = g_nextId++;
				g_session = std::move(session);
				g_stationId = stationId;
				g_stationHandle = a_station->GetHandle().native_handle();
				g_active = true;
			}
			if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME)) {
				RefreshOpenMenu(stationId);
			}
			if (total > 0) {
				Controller::Get().Notify(std::format("Dova Stash: {} at hand", detail::Plural(total, "material")));
			}
			return true;
		}

		bool Active() noexcept
		{
			std::scoped_lock lock(g_lock);
			return g_session.has_value();
		}

		RE::FormID StationId() noexcept
		{
			std::scoped_lock lock(g_lock);
			return g_session ? g_session->station : 0;
		}

		void End(std::string_view a_why)
		{
			std::optional<Session> ended;
			{
				std::scoped_lock lock(g_lock);
				Deactivate();
				g_stationId = 0;
				ended.swap(g_session);
			}
			if (!ended) {
				return;
			}
			const auto& s = *ended;
			logger::info("Crafting from storage ({}): {} taken from storage at {}{}", a_why, detail::Plural(s.used, "material"), s.stationName,
				s.usedFrom.empty() ? "" : std::format(" ({} crafts; {})", s.crafts, Names(s.usedFrom)));
		}

		bool Hooked() noexcept
		{
			if (t_busy > 0 || !g_active.load(std::memory_order_acquire)) {
				return false;
			}
			const auto* player = RE::PlayerCharacter::GetSingleton();
			return player && player->GetOccupiedFurniture().native_handle() == g_stationHandle.load();
		}

		bool ListsHooked() noexcept
		{
			if (!Hooked()) {
				return false;
			}
			std::scoped_lock lock(g_lock);
			return g_session && g_session->request.station == cc::Station::Alchemy;
		}

		std::int32_t Stored(const RE::TESBoundObject* a_object) noexcept
		{
			if (!a_object) {
				return 0;
			}
			std::scoped_lock lock(g_lock);
			if (!g_session) {
				return 0;
			}
			const auto it = g_session->materials.find(const_cast<RE::TESBoundObject*>(a_object));
			return it != g_session->materials.end() ? it->second.count : 0;
		}

		std::int32_t ListCount(std::int32_t a_playerEntries, bool a_viewing, bool a_playable)
		{
			std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> stored;
			{
				std::scoped_lock lock(g_lock);
				if (!g_session) {
					return a_playerEntries;
				}
				for (const auto& [object, m] : g_session->materials) {
					stored.emplace_back(object, m.count);
				}
			}
			std::vector<RE::TESBoundObject*> extra;
			{
				const Busy busy;
				auto*      player = game::Player();
				const auto carried = player ? player->GetInventoryCounts() : RE::TESObjectREFR::InventoryCountMap{};
				for (const auto& [object, count] : stored) {
					const auto it = carried.find(object);
					if (count > 0 && (it == carried.end() || it->second <= 0)) {
						extra.push_back(object);
					}
				}
			}
			std::ranges::sort(extra, {}, [](const RE::TESBoundObject* a_object) { return a_object->GetFormID(); });
			const auto       n = static_cast<std::int32_t>(extra.size());
			std::scoped_lock lock(g_lock);
			if (!g_session) {
				return a_playerEntries;
			}
			g_session->viewing = a_viewing;
			g_session->playable = a_playable;
			g_session->listExtra = std::move(extra);
			return a_playerEntries + n;
		}

		std::pair<bool, bool> PlayerListArgs() noexcept
		{
			std::scoped_lock lock(g_lock);
			return g_session ? std::pair{ g_session->viewing, g_session->playable } : std::pair{ false, true };
		}

		RE::InventoryEntryData* ListExtra(std::int32_t a_index)
		{
			RE::TESBoundObject* object = nullptr;
			std::int32_t        count = 0;
			{
				std::scoped_lock lock(g_lock);
				if (!g_session || a_index < 0 || a_index >= static_cast<std::int32_t>(g_session->listExtra.size())) {
					return nullptr;
				}
				object = g_session->listExtra[static_cast<std::size_t>(a_index)];
				const auto it = g_session->materials.find(object);
				count = it != g_session->materials.end() ? it->second.count : 0;
			}
			if (!object || count <= 0) {
				return nullptr;
			}
			return new RE::InventoryEntryData(object, count);  // game heap; the caller deletes it
		}

		RE::InventoryEntryData* ListMerge(RE::InventoryEntryData* a_entry)
		{
			if (!a_entry || !a_entry->object) {
				return a_entry;
			}
			const auto stored = Stored(a_entry->object);
			if (stored <= 0) {
				return a_entry;
			}
			// A worn, favorited or quest copy keeps its own entry: the menu decides about it as without Dova Stash.
			if (a_entry->extraLists) {
				for (auto* x : *a_entry->extraLists) {
					if (game::IsProtectedExtraList(x)) {
						return a_entry;
					}
				}
			}
			std::int32_t carried = 0;
			{
				const Busy busy;
				carried = game::LiveCount(game::Player(), a_entry->object);
			}
			auto* merged = new RE::InventoryEntryData(a_entry->object, carried + stored);  // plain: no extra list
			delete a_entry;  // the caller's copy (GetInventoryItemAt hands out copies the caller deletes)
			return merged;
		}

		RE::ObjectRefHandle* Consume(RE::TESBoundObject* a_item, std::int32_t a_count, const RemoveFn& a_remove)
		{
			// One craft at a time. The count hooks never take this lock, so a menu asking for counts meanwhile (on
			// another thread) is not held up; g_lock is taken only to copy the session's data and to update it.
			std::scoped_lock consume(g_consumeLock);

			std::uint64_t                     sessionId = 0;
			RE::FormID                        station = 0;
			RE::FormID                        cellId = 0;
			std::string                       stationName;
			cc::SupplyRequest                 request;
			std::shared_ptr<const home::Home> home;
			Material                          m;
			{
				std::scoped_lock lock(g_lock);
				if (!g_session) {
					return a_remove();
				}
				const auto it = g_session->materials.find(a_item);
				if (it == g_session->materials.end() || it->second.count <= 0) {
					return a_remove();
				}
				sessionId = g_session->id;
				station = g_session->station;
				cellId = g_session->cellId;
				stationName = g_session->stationName;
				request = g_session->request;
				home = g_session->home;
				m = it->second;
			}

			const Busy busy;
			auto*      player = game::Player();
			const auto before = game::LiveCount(player, a_item);
			const auto need = cc::Shortfall(a_count, before);
			if (need == 0) {
				return a_remove();
			}
			const auto* cell = player ? player->GetParentCell() : nullptr;
			if (!home || !home->layout || !cell || cell->GetFormID() != cellId) {
				Deactivate();
				detail::FallBackToMove(station, "the home is not loaded any more");
				return a_remove();
			}

			// Fresh: what is in the containers right now, not what the cache says. The containers are checked against
			// the session's copy of the home (the same checks as on the task thread, never the live Home).
			game::StackTable table;
			const auto       stored = detail::SnapshotStored(*home, table, &m.containers);
			const auto       available = Available(request, stored, m.form);
			const auto       takes = cc::PlanTake(request, stored, m.form, m.containers, need);
			std::vector<core::Move> moves;
			for (const auto& t : takes) {
				moves.push_back(t.move);
			}
			if (!moves.empty()) {
				(void)game::ItemMover::Execute(moves, table, detail::Resolver(player, *home));
			}
			const auto brought = std::max(0, game::LiveCount(player, a_item) - before);

			auto* result = a_remove();

			const auto after = game::LiveCount(player, a_item);
			const auto unused = cc::Unused(before, brought, a_count, after);
			const auto back = unused > 0 ? PutBack(stationName, *home, takes, unused) : 0;
			const auto left = std::max(0, available - brought + back);
			{
				std::scoped_lock lock(g_lock);
				if (g_session && g_session->id == sessionId) {
					auto& s = *g_session;
					if (const auto it = s.materials.find(a_item); it != s.materials.end()) {
						it->second.count = left;
					}
					++s.crafts;
					s.used += brought - back;
					for (const auto& t : takes) {
						s.usedFrom.insert(t.move.from.Ref());
					}
				}
			}
			logger::debug("Crafting from storage: {} x{}: carried {}, brought {} from {}, {} left in storage", game::Describe(a_item), a_count, before, brought,
				Names(m.containers), left);

			if (brought < need) {
				// The containers gave less than they promised (a script took from them meanwhile, or a stack failed
				// the ItemMover's checks): the craft took what the player had. From now on: Move.
				Deactivate();
				detail::FallBackToMove(station, std::format("{} of the {} {} needed could be brought", brought, need, game::Describe(a_item)));
			}
			return result;
		}
	}
}
