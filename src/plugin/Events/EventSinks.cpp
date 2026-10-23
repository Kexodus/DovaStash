#include "Events/EventSinks.h"

#include "Controller.h"
#include "Game/FormUtil.h"
#include "Game/ItemMover.h"
#include "Home/Areas.h"

namespace dova::events
{
	namespace
	{
		using Control = RE::BSEventNotifyControl;

		std::atomic<RE::RefHandle> g_crosshair{ 0 };
		std::atomic<bool>          g_crosshairOverridden{ false };
		std::atomic<RE::RefHandle> g_crosshairOverride{ 0 };

		void Enqueue(std::function<void()> a_task)
		{
			if (const auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask(std::move(a_task));
			}
		}

		// "Chest (ref) [Plugin|0x...]", or what a 0 end means: the item came from / went to no container.
		std::string JournalEnd(RE::FormID a_container, RE::RefHandle a_worldRef, bool a_incoming)
		{
			if (a_container != 0) {
				return game::Describe(RE::TESForm::LookupByID(a_container));
			}
			if (a_worldRef != 0) {
				return std::format("the world ({})", game::Describe(game::RefFromHandle(a_worldRef).get()));
			}
			return a_incoming ? "nowhere (created: script or console)" : "nowhere (removed: script, consumed or deleted)";
		}

		// Inventory journal (debug): every change to the player's inventory that Dova Stash did not make
		// itself (those are logged as "Moved ..."). Shows items that scripts add, swap or remove, and items
		// that come back after a Deposit.
		void Journal(const RE::TESContainerChangedEvent& a_event)
		{
			const bool incoming = a_event.newContainer == game::kPlayerRefID;
			if ((!incoming && a_event.oldContainer != game::kPlayerRefID) || a_event.oldContainer == a_event.newContainer ||
				!spdlog::should_log(spdlog::level::debug) || game::ItemMover::IsSuppressing()) {
				return;
			}
			const auto other = incoming ? a_event.oldContainer : a_event.newContainer;
			const auto base = a_event.baseObj;
			const auto count = a_event.itemCount;
			const auto worldRef = a_event.reference.native_handle();
			Enqueue([incoming, other, base, count, worldRef]() {
				logger::debug("Inventory: {}{} {} {} {}", incoming ? "+" : "-", count, game::Describe(RE::TESForm::LookupByID(base)),
					incoming ? "from" : "to", JournalEnd(other, worldRef, incoming));
			});
		}

		class ContainerChangedSink final : public RE::BSTEventSink<RE::TESContainerChangedEvent>
		{
		public:
			Control ProcessEvent(const RE::TESContainerChangedEvent* a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
			{
				if (a_event) {
					Journal(*a_event);
				}
				// Container -> player: maybe a script handing back what Dova Stash just stored there (not gated on
				// IsSuppressing: the script can answer within the grace period; our own retrieves never match).
				// Taken by hand through QuickLoot IE's loot menu (no ContainerMenu opens for it) is not a script
				// handing it back: decided here, while the menu is still open.
				if (a_event && a_event->newContainer == game::kPlayerRefID && a_event->oldContainer != 0 &&
					a_event->oldContainer != game::kPlayerRefID && a_event->itemCount > 0) {
					const auto container = a_event->oldContainer;
					const auto base = a_event->baseObj;
					auto*      ui = RE::UI::GetSingleton();
					const bool byHand = ui && ui->IsMenuOpen("LootMenu");
					Enqueue([container, base, byHand]() { Controller::Get().OnItemReturned(container, base, byHand); });
				}
				if (!a_event || a_event->oldContainer != game::kPlayerRefID || a_event->newContainer == 0 ||
					a_event->newContainer == game::kPlayerRefID || a_event->itemCount <= 0) {
					return Control::kContinue;
				}
				// Our own moves must never be learned (DESIGN §8.1).
				if (game::ItemMover::IsSuppressing()) {
					return Control::kContinue;
				}
				const auto container = a_event->newContainer;
				const auto base = a_event->baseObj;
				Enqueue([container, base]() { Controller::Get().OnItemStored(container, base); });
				return Control::kContinue;
			}
		};

		class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			Control ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_event || a_event->menuName != RE::ContainerMenu::MENU_NAME) {
					return Control::kContinue;
				}
				if (a_event->opening) {
					// The target handle is set before the menu opens; resolve it now, while it is valid.
					RE::FormID target = 0;
					if (const auto ref = game::RefFromHandle(RE::ContainerMenu::GetTargetRefHandle())) {
						target = ref->GetFormID();
					}
					Enqueue([target]() { Controller::Get().OnContainerMenuOpened(target); });
				} else {
					Enqueue([]() { Controller::Get().OnContainerMenuClosed(); });
				}
				return Control::kContinue;
			}
		};

		class ActorCellSink final : public RE::BSTEventSink<RE::BGSActorCellEvent>
		{
		public:
			Control ProcessEvent(const RE::BGSActorCellEvent* a_event, RE::BSTEventSource<RE::BGSActorCellEvent>*) override
			{
				// CellFlag is a plain value (kEnter = 0, kLeave = 1), not a bit mask.
				if (a_event && a_event->flags.get() == RE::BGSActorCellEvent::CellFlag::kEnter) {
					Enqueue([]() { Controller::Get().OnCellMaybeChanged(); });
				}
				return Control::kContinue;
			}
		};

		class CellLoadedSink final : public RE::BSTEventSink<RE::TESCellFullyLoadedEvent>
		{
		public:
			Control ProcessEvent(const RE::TESCellFullyLoadedEvent* a_event, RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override
			{
				if (a_event && a_event->cell && a_event->cell->IsInteriorCell()) {
					Enqueue([]() { Controller::Get().OnCellMaybeChanged(); });
				} else if (a_event && a_event->cell) {
					// An exterior cell loaded (walked near): its grid square is remembered so that the persistent
					// references of a home's exterior area can be matched to it from indoors later (DESIGN §6.7).
					home::RememberCell(a_event->cell);
				}
				return Control::kContinue;
			}
		};

		class CrosshairSink final : public RE::BSTEventSink<SKSE::CrosshairRefEvent>
		{
		public:
			Control ProcessEvent(const SKSE::CrosshairRefEvent* a_event, RE::BSTEventSource<SKSE::CrosshairRefEvent>*) override
			{
				RE::RefHandle handle = 0;
				if (a_event && a_event->crosshairRef) {
					handle = a_event->crosshairRef->GetHandle().native_handle();
				}
				g_crosshair = handle;
				return Control::kContinue;
			}
		};

		ContainerChangedSink g_containerSink;
		MenuSink             g_menuSink;
		ActorCellSink        g_actorCellSink;
		CellLoadedSink       g_cellLoadedSink;
		CrosshairSink        g_crosshairSink;
	}

	void RegisterSinks()
	{
		if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink<RE::TESContainerChangedEvent>(&g_containerSink);
			holder->AddEventSink<RE::TESCellFullyLoadedEvent>(&g_cellLoadedSink);
			logger::info("Events: container-changed and cell-loaded sinks registered");
		} else {
			logger::error("Events: ScriptEventSourceHolder unavailable");
		}

		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(&g_menuSink);
			logger::info("Events: menu sink registered");
		} else {
			logger::error("Events: UI unavailable");
		}

		if (auto* crosshair = SKSE::GetCrosshairRefEventSource()) {
			crosshair->AddEventSink(&g_crosshairSink);
			logger::info("Events: crosshair sink registered");
		} else {
			logger::warn("Events: no crosshair event source (VR?); hotkey works without a target only");
		}
	}

	void RegisterPlayerSinks()
	{
		static bool registered = false;
		if (registered) {
			return;
		}
		if (auto* player = game::Player()) {
			if (auto* source = player->AsBGSActorCellEventSource()) {
				source->AddEventSink(&g_actorCellSink);
				registered = true;
				logger::info("Events: player cell sink registered");
			}
		}
	}

	RE::RefHandle CrosshairTarget() noexcept
	{
		return g_crosshairOverridden.load() ? g_crosshairOverride.load() : g_crosshair.load();
	}

	void OverrideCrosshair(std::optional<RE::RefHandle> a_handle) noexcept
	{
		g_crosshairOverride = a_handle.value_or(0);
		g_crosshairOverridden = a_handle.has_value();
	}
}
