#include "Events/EventSinks.h"

#include "Controller.h"
#include "Game/FormUtil.h"
#include "Game/ItemMover.h"

namespace dova::events
{
	namespace
	{
		using Control = RE::BSEventNotifyControl;

		std::atomic<RE::RefHandle> g_crosshair{ 0 };

		void Enqueue(std::function<void()> a_task)
		{
			if (const auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask(std::move(a_task));
			}
		}

		class ContainerChangedSink final : public RE::BSTEventSink<RE::TESContainerChangedEvent>
		{
		public:
			Control ProcessEvent(const RE::TESContainerChangedEvent* a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
			{
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
		return g_crosshair.load();
	}
}
