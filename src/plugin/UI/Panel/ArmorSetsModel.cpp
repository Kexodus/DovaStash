#include "UI/Panel/ArmorSetsModel.h"

#include "Game/Displays.h"
#include "Game/FormUtil.h"
#include "UI/Panel/Strings.h"

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

	void ArmorSetsModel::Post(std::function<void()> a_action)
	{
		SKSE::GetTaskInterface()->AddTask([this, action = std::move(a_action)]() {
			if (action) {
				action();
			}
			Rebuild();
		});
	}

	void ArmorSetsModel::Rebuild()
	{
		_lastMs = NowMs();
		auto snap = std::make_shared<ArmorSetsSnapshot>();
		snap->serial = ++_serial;
		snap->index = game::GetArmorIndex();

		auto* home = home::HomeRegistry::Get().Current();
		if (home) {
			snap->inHome = true;
			snap->enabled = home->Enabled();
			snap->cellId = home->cellId;
			snap->homeName = home->name;
			snap->have = game::HaveArmor(*snap->index, *home, nullptr);

			auto* player = game::Player();
			auto* cell = player ? player->GetParentCell() : nullptr;
			if (cell && cell->GetFormID() != _scannedCell) {
				_scannedCell = cell->GetFormID();
				_mannequins.clear();
				for (const auto& d : game::FindDisplays(cell)) {
					if (d.kind == core::DisplayKind::Mannequin) {
						_mannequins.push_back(d.ref);
					}
				}
			}
			for (const auto id : _mannequins) {
				auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
				auto  formRef = game::ToFormRef(ref);
				if (!formRef) {
					continue;
				}
				MannequinRow row;
				row.ref = *formRef;
				row.name = game::RefName(ref);
				if (const auto* binding = home->layout ? home->layout->FindDisplay(row.ref) : nullptr) {
					row.used = !binding->ignored;
					row.set = binding->set;
					if (!binding->label.empty()) {
						row.name = binding->label;
					}
				}
				row.setName = row.set ? game::SetName(*snap->index, *row.set) : std::string(text::kAuto);
				snap->mannequins.push_back(std::move(row));
			}
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
