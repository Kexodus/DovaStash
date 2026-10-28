#pragma once

// Game event sinks (DESIGN §5). Every sink only captures plain values (form ids, handles) and enqueues
// work for the main thread; none of them touches inventories or layouts directly.
//
//   TESContainerChangedEvent  player -> container transfers -> Controller::OnItemStored (learning);
//                             container -> player transfers -> Controller::OnItemReturned (items a
//                             script hands back after a store);
//                             any player inventory change -> inventory journal (debug log)
//   MenuOpenCloseEvent        ContainerMenu open/close -> target tracking, Distribute on master close
//   BGSActorCellEvent         player entered a cell -> template notification (registered once the
//   TESCellFullyLoadedEvent   player exists); the cell-loaded event is a fallback for the same purpose
//   SKSE::CrosshairRefEvent   crosshair target, stored as a RefHandle value (never a raw pointer)

namespace dova::events
{
	// kDataLoaded: script events, UI events, crosshair.
	void RegisterSinks();
	// First kPostLoadGame / kNewGame: sinks that live on the PlayerCharacter.
	void RegisterPlayerSinks();

	// Crosshair target as a handle value (0 = none). Safe from any thread.
	[[nodiscard]] RE::RefHandle CrosshairTarget() noexcept;
	// AutoTest's "aim" step: CrosshairTarget reports this handle (0: nothing) until it is cleared (nullopt).
	void OverrideCrosshair(std::optional<RE::RefHandle> a_handle) noexcept;
	// True while AutoTest sets the crosshair.
	[[nodiscard]] bool CrosshairOverridden() noexcept;
}
