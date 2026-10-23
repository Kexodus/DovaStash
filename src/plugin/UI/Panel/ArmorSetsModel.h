#pragma once

// Data for the armor set picker of a mannequin on the Menu Framework "Displays" page (UI/Panel/ArmorSetPicker.h;
// DESIGN §10.1, §8.4 "Mannequins"): every armor set in the load order and how much of each the player has. Same
// threading contract as PanelModel: the snapshot is built on the MAIN thread (SKSE task) and only read on the
// render thread.

#include "Game/ArmorIndex.h"

#include <mutex>

namespace dova::ui::panel
{
	struct ArmorSetsSnapshot
	{
		std::uint64_t                           serial{ 0 };
		std::shared_ptr<const game::ArmorIndex> index;     // never changes once built
		std::vector<core::SetCoverage>          coverage;  // parallel to index->sets
		std::set<core::FormRef>                 have;      // pieces the player has (carried or stored in this home)
	};

	class ArmorSetsModel
	{
	public:
		[[nodiscard]] static ArmorSetsModel& Get();

		// Any thread.
		[[nodiscard]] std::shared_ptr<const ArmorSetsSnapshot> Snapshot() const;
		// Any thread. Coalesced; at most every few seconds unless `a_now`.
		void RequestRefresh(bool a_now = false);

	private:
		ArmorSetsModel() = default;

		void Rebuild();  // main thread

		mutable std::mutex                       _lock;
		std::shared_ptr<const ArmorSetsSnapshot> _snapshot;
		std::atomic<bool>                        _queued{ false };
		std::atomic<std::int64_t>                _lastMs{ 0 };
		std::uint64_t                            _serial{ 0 };  // main thread
	};
}
