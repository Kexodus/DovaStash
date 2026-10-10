#pragma once

// Game event sinks (DESIGN §5). Every sink only captures plain values (form ids, handles) and enqueues
// work for the main thread; none of them touches inventories or layouts directly.
//
//   TESContainerChangedEvent  player -> container transfers -> Controller::OnItemStored (learning)
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
}
