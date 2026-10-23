#include "UI/Panel/ArmorSetsModel.h"

#include "Home/HomeRegistry.h"

#include <mutex>

namespace dova::ui::panel
{
	namespace
	{
		constexpr std::int64_t kRefreshMs = 3000;

		std::int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}
	}

	ArmorSetsModel& ArmorSetsModel::Get()
	{
		static ArmorSetsModel model;
		return model;
	}

	std::shared_ptr<const ArmorSetsSnapshot> ArmorSetsModel::Snapshot() const
	{
		std::scoped_lock lock(_lock);
		return _snapshot;
	}

	void ArmorSetsModel::RequestRefresh(bool a_now)
	{
		if (!a_now && Snapshot() && NowMs() - _lastMs.load() < kRefreshMs) {
			return;
		}
		if (_queued.exchange(true)) {
			return;
		}
		SKSE::GetTaskInterface()->AddTask([this]() {
			_queued = false;
			Rebuild();
		});
	}

	void ArmorSetsModel::Rebuild()
	{
		_lastMs = NowMs();
		auto snap = std::make_shared<ArmorSetsSnapshot>();
		snap->serial = ++_serial;
		snap->index = game::GetArmorIndex();

		if (const auto* home = home::HomeRegistry::Get().Current()) {
			snap->have = game::HaveArmor(*snap->index, *home, nullptr);
		} else {
			home::Home none;  // not in a home: what the player carries
			snap->have = game::HaveArmor(*snap->index, none, nullptr);
		}
		snap->coverage.reserve(snap->index->sets.size());
		for (const auto& set : snap->index->sets) {
			snap->coverage.push_back(core::CoverageOf(set, snap->index->pieces, snap->have));
		}

		std::scoped_lock lock(_lock);
		_snapshot = std::move(snap);
	}
}
