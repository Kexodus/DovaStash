#pragma once

// Data for the Menu Framework "Armor sets" page (DESIGN §10.1, §8.4 "Mannequins"). Same threading
// contract as PanelModel: the snapshot is built on the MAIN thread (SKSE task) and only read on the
// render thread; every action is a closure posted to the main thread, followed by a rebuild.

#include "Game/ArmorIndex.h"

#include <mutex>

namespace dova::ui::panel
{
	struct MannequinRow
	{
		core::FormRef                ref;
		std::string                  name;     // "Mannequin"
		std::string                  setName;  // "Ebony", "Auto"
		std::optional<core::FormRef> set;      // the layout entry's `set`
		bool                         used{ false };  // the layout fills it
	};

	struct ArmorSetsSnapshot
	{
		std::uint64_t                           serial{ 0 };
		std::shared_ptr<const game::ArmorIndex> index;     // never changes once built
		std::vector<core::SetCoverage>          coverage;  // parallel to index->sets
		std::set<core::FormRef>                 have;      // pieces the player has (carried or stored in this home)

		bool                      inHome{ false };
		bool                      enabled{ false };  // the home has a player layout
		std::uint32_t             cellId{ 0 };
		std::string               homeName;
		std::vector<MannequinRow> mannequins;  // in this home's cell
	};

	class ArmorSetsModel
	{
	public:
		[[nodiscard]] static ArmorSetsModel& Get();

		// Any thread.
		[[nodiscard]] std::shared_ptr<const ArmorSetsSnapshot> Snapshot() const;
		// Any thread. Coalesced; at most every few seconds unless `a_now`.
		void RequestRefresh(bool a_now = false);
		// Runs `a_action` on the main thread, then rebuilds the snapshot.
		void Post(std::function<void()> a_action);

	private:
		ArmorSetsModel() = default;

		void Rebuild();  // main thread

		mutable std::mutex                       _lock;
		std::shared_ptr<const ArmorSetsSnapshot> _snapshot;
		std::atomic<bool>                        _queued{ false };
		std::atomic<std::int64_t>                _lastMs{ 0 };
		std::uint64_t                            _serial{ 0 };  // main thread

		// main thread: the mannequins of the last scanned cell (scanning runs the VM's script lookups)
		RE::FormID              _scannedCell{ 0 };
		std::vector<RE::FormID> _mannequins;
	};
}
