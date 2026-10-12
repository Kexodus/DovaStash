#include "Home/HomeRegistry.h"

#include "Game/Eligibility.h"
#include "Game/FormUtil.h"
#include "Home/Store.h"

namespace dova::home
{
	const core::TemplateCandidate* Home::BestTemplate() const
	{
		if (!resolution.chosen) {
			return nullptr;
		}
		const auto templates = Store::Get().Templates();
		return *resolution.chosen < templates.size() ? &templates[*resolution.chosen] : nullptr;
	}

	HomeRegistry& HomeRegistry::Get()
	{
		static HomeRegistry registry;
		return registry;
	}

	Home* HomeRegistry::Current(std::string* a_reason)
	{
		auto* player = game::Player();
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!cell) {
			if (a_reason) {
				*a_reason = "no current cell";
			}
			return nullptr;
		}

		if (!_home || _home->cellId != cell->GetFormID()) {
			Flush();
			_home.reset();
			Publish();
			auto cellRef = game::ToFormRef(cell);
			if (!cellRef) {
				if (a_reason) {
					*a_reason = "temporary cell";
				}
				return nullptr;
			}
			Load(cell, std::move(*cellRef));
		}

		if (!_home->frontsScanned && cell->IsAttached()) {
			_home->fronts = game::FindFronts(cell);
			_home->frontsScanned = true;
			if (!_home->fronts.empty()) {
				logger::debug("Cell {}: {} activator(s) open a hidden container", _home->name, _home->fronts.size());
				for (const auto& [front, container] : _home->fronts) {
					logger::trace("  {} -> {}", game::Describe(RE::TESForm::LookupByID(front)), game::Describe(RE::TESForm::LookupByID(container)));
				}
			}
			Publish();
		}

		// Eligibility can change while we stand here (house purchased, faction joined), so check every time.
		const auto verdict = game::CheckCell(cell);
		if (!verdict.ok) {
			if (a_reason) {
				*a_reason = verdict.reason;
			}
			return nullptr;
		}
		return &*_home;
	}

	Home* HomeRegistry::CurrentIf(RE::FormID a_cellId)
	{
		auto* home = Current();
		return home && home->cellId == a_cellId ? home : nullptr;
	}

	std::shared_ptr<const DisplayHome> HomeRegistry::Display() const
	{
		auto        display = _display.load();
		const auto* player = game::Player();
		const auto* cell = player ? player->GetParentCell() : nullptr;
		if (!display || !cell || display->cellId != cell->GetFormID()) {
			return nullptr;
		}
		return display;
	}

	void HomeRegistry::Publish()
	{
		if (!_home) {
			_display.store(nullptr);
			return;
		}
		auto display = std::make_shared<DisplayHome>();
		display->cellId = _home->cellId;
		display->name = _home->name;
		display->layout = _home->layout;
		display->fronts = _home->fronts;
		for (const auto& [front, container] : _home->fronts) {
			auto& anchor = display->frontOf[container];
			if (!anchor || front < anchor) {
				anchor = front;  // several fronts for one container: any stable one
			}
		}
		_display.store(std::move(display));
	}

	void HomeRegistry::MarkDirty(Home& a_home)
	{
		a_home.dirty = true;
		Publish();
	}

	void HomeRegistry::Load(RE::TESObjectCELL* a_cell, core::FormRef a_cellRef)
	{
		Home home;
		home.cellId = a_cell->GetFormID();
		home.cell = std::move(a_cellRef);
		home.name = game::CellName(a_cell);
		home.layout = Store::Get().LoadPlayerLayout(home.cell, home.layoutBroken);
		Resolve(home);
		_home = std::move(home);
		Publish();

		const auto& h = *_home;
		logger::info("Cell {} ({}): {}", h.name, h.cell.ToString(),
			h.layout                                                        ? "player layout"s :
			h.layoutBroken                                                  ? "player layout file is broken (left untouched)"s :
			h.resolution.kind == core::LayoutResolution::Kind::Template ? "template available"s :
			                                                                  "no layout"s);
	}

	void HomeRegistry::Resolve(Home& a_home)
	{
		const auto templates = Store::Get().Templates();
		a_home.resolution = core::ResolveLayout(a_home.layout ? &*a_home.layout : nullptr, templates, a_home.cell, game::LoadedPlugins());
		if (const auto* best = a_home.BestTemplate()) {
			logger::info("  template: {} v{} by {} ({}), {} alternative(s)", best->layout.name, best->layout.version, best->layout.author,
				best->source, a_home.resolution.alternatives.size());
		}
		for (const auto& rejected : a_home.resolution.rejected) {
			if (rejected.index < templates.size()) {
				std::string missing;
				for (const auto& p : rejected.missingPlugins) {
					missing += missing.empty() ? p : ", " + p;
				}
				logger::info("  template {} not offered: missing {}", templates[rejected.index].source, missing);
			}
		}
	}

	bool HomeRegistry::Save(Home& a_home)
	{
		if (!a_home.layout) {
			return false;
		}
		if (a_home.layoutBroken) {
			logger::error("Not saving {}: the existing layout file could not be parsed. Fix or delete it first.", a_home.cell.ToString());
			return false;
		}
		// The cell being edited decides the file, never what the layout itself claims.
		a_home.layout->cell = a_home.cell;
		const bool ok = Store::Get().SavePlayerLayout(a_home.cell, *a_home.layout);
		if (ok) {
			a_home.dirty = false;
		}
		Publish();  // in-memory edits are what the display shows, saved or not
		return ok;
	}

	void HomeRegistry::Flush()
	{
		if (_home && _home->dirty) {
			Save(*_home);
		}
	}

	core::Layout& HomeRegistry::EnsureLayout(Home& a_home)
	{
		if (!a_home.layout) {
			core::Layout layout;
			layout.name = a_home.name;
			layout.author = "Player";
			layout.version = "1.0.0";
			layout.cell = a_home.cell;
			a_home.layout = std::move(layout);
			Resolve(a_home);
		}
		return *a_home.layout;
	}

	bool HomeRegistry::ApplyBestTemplate(Home& a_home)
	{
		const auto* best = a_home.BestTemplate();
		if (!best) {
			return false;
		}
		return ApplyTemplate(a_home, *best);
	}

	bool HomeRegistry::ApplyTemplate(Home& a_home, const core::TemplateCandidate& a_template)
	{
		if (a_home.layoutBroken) {
			logger::error("Not applying template {} to {}: the existing layout file could not be parsed", a_template.source, a_home.name);
			return false;
		}
		if (!Store::Get().BackupPlayerLayout(a_home.cell)) {
			logger::error("Not applying template {} to {}: the current layout could not be backed up", a_template.source, a_home.name);
			return false;
		}
		const auto source = a_template.source;  // a_template may live in the store; copy what we log
		const auto version = a_template.layout.version;
		auto       layout = core::ApplyTemplate(a_template, a_home.layout ? &*a_home.layout : nullptr);
		layout.cell = a_home.cell;
		a_home.layout = std::move(layout);
		Resolve(a_home);
		logger::info("Applied template {} v{} to {} (player data merged)", source, version, a_home.name);
		return Save(a_home);
	}

	bool HomeRegistry::Reset(Home& a_home)
	{
		if (!Store::Get().BackupAndRemovePlayerLayout(a_home.cell)) {
			return false;
		}
		a_home.layout.reset();
		a_home.layoutBroken = false;
		a_home.dirty = false;
		Resolve(a_home);
		Publish();
		logger::info("Reset home {}", a_home.name);
		return true;
	}
}
