#include "Home/HomeRegistry.h"

#include "Game/Eligibility.h"
#include "Game/FormUtil.h"
#include "Game/ItemAdapter.h"
#include "Home/Store.h"
#include "Home/TemplateDisplays.h"
#include "Log.h"

#include <list>

namespace dova::home
{
	namespace
	{
		// Whole-home templates (DESIGN Â§6.7): a cell the home's layout lists whose own setup is only what a template
		// made by itself (auto-enable on the first visit: Lakeview Manor Cellar; core::AbsorbableCells) joins the
		// home. Its file is backed up and removed, the Reset path (.bak next to it; the copy outside Data moves to
		// history with the next write). Anything of the player's in it, or a file that cannot be read, keeps it a
		// home of its own. Only the setup changes: no item is moved. The number of cells taken in.
		std::size_t TakeInUntouchedCells(const Home& a_home)
		{
			if (!a_home.layout || a_home.layout->cells.empty()) {
				return 0;
			}
			auto&                   store = Store::Get();
			std::list<core::Layout> loaded;  // stable addresses for the lookup's pointers
			const auto              lookup = [&](const core::FormRef& a_cell) -> const core::Layout* {
				if (a_cell == a_home.cell || !store.HasPlayerLayoutFile(a_cell)) {
					return nullptr;
				}
				bool broken = false;
				auto layout = store.LoadPlayerLayout(a_cell, broken);
				if (!layout || broken) {
					return nullptr;
				}
				return &loaded.emplace_back(std::move(*layout));
			};
			// The cell's setup was made with its displays' script items recorded on arrival: count those as the
			// template's (a display that is not loaded from here is not recorded, so its cell stays its own).
			const auto record = [](core::Layout& a_copy) { (void)RecordDisplayItems(a_copy, false); };
			std::size_t taken = 0;
			for (const auto& cell : core::AbsorbableCells(*a_home.layout, lookup, store.Templates(), record)) {
				const auto name = ListedCellName(a_home, cell);
				if (!store.BackupAndRemovePlayerLayout(cell)) {
					logger::warn("{}: could not back up the setup of {} ({}); it stays a home of its own", a_home.name, name, cell.ToString());
					continue;
				}
				store.KeepHomeBackupHistoryNext(cell);
				logger::info("{}: took in {} (untouched template setup)", a_home.name, name);
				++taken;
			}
			return taken;
		}

		// An extra cell of a multi-cell home: nobody else's (CheckExtraCell), and the home's main cell still
		// eligible when it is in memory (interior cells always are).
		game::Verdict CheckExtraArea(const Home& a_home, RE::TESObjectCELL* a_cell)
		{
			auto verdict = CheckExtraCell(a_cell);
			if (!verdict.ok) {
				return verdict;
			}
			auto* form = game::LookupForm(a_home.cell);
			if (auto* main = form ? form->As<RE::TESObjectCELL>() : nullptr) {
				if (const auto mainVerdict = game::CheckCell(main); !mainVerdict.ok) {
					return game::Verdict::Reject(std::format("{}: {}", a_home.name, mainVerdict.reason));
				}
			}
			return verdict;
		}

		// Rebuilds Home::areas when the layout's cells differ from the ones they were built from. True if rebuilt.
		bool RebuildAreasIfChanged(Home& a_home)
		{
			const std::span<const core::HomeCell> cells = a_home.layout ? std::span<const core::HomeCell>(a_home.layout->cells) :
			                                                              std::span<const core::HomeCell>{};
			if (core::HomeCellRefs(a_home.cell, cells) == a_home.areasFrom) {
				return false;
			}
			BuildAreas(a_home);
			LogAreas(a_home);
			return true;
		}

		std::string MainCellName(const Home& a_home)
		{
			auto* form = game::LookupForm(a_home.cell);
			if (auto* main = form ? form->As<RE::TESObjectCELL>() : nullptr) {
				return game::CellName(main);
			}
			return a_home.layout && !a_home.layout->name.empty() ? a_home.layout->name : a_home.cell.ToString();
		}
	}

	core::TemplateFit CheckTemplateFit(const core::Layout& a_template, RE::TESObjectCELL* a_cell)
	{
		std::vector<RE::TESObjectCELL*> templateCells;  // the other cells it names that are in memory
		for (const auto& c : a_template.cells) {
			auto* form = game::LookupForm(c.cell);
			if (auto* cell = form ? form->As<RE::TESObjectCELL>() : nullptr) {
				templateCells.push_back(cell);
			}
		}
		const auto storage = [&](const core::FormRef& a_ref) { return a_template.IsMaster(a_ref) || a_template.FindContainer(a_ref); };
		const auto probe = [&](const core::FormRef& a_ref) {
			using enum core::RefPresence;
			auto* ref = game::LookupRef(a_ref);
			if (!ref) {
				// Every reference of an attached cell is in memory; one of another cell may just not be loaded.
				return a_template.cells.empty() ? Missing : Unknown;
			}
			if (ref->IsDeleted() || ref->IsMarkedForDeletion()) {
				return Disabled;
			}
			auto* cell = ref->GetParentCell();
			if (cell != a_cell) {
				if (!cell || (std::ranges::find(templateCells, cell) != templateCells.end() && !cell->IsAttached())) {
					return Unknown;
				}
				if (std::ranges::find(templateCells, cell) == templateCells.end()) {
					return Missing;  // moved to a place the template does not name
				}
			}
			if (storage(a_ref) && !game::ContainerBehind(ref)) {
				return Missing;  // replaced by something that is no container
			}
			if (!game::IsEffectivelyDisabled(ref)) {
				return Present;
			}
			return game::IsUndeletedRef(ref) ? Disabled : NotBuilt;
		};
		return core::JudgeTemplateFit(a_template, probe);
	}

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

		if (_forgetVisits.exchange(false)) {
			_cameFrom.reset();
		}
		const bool moved = !_home || _home->cellId != cell->GetFormID();
		if (moved || _stale) {
			if (moved) {
				// The home the player just left, if it is set up: this cell can be offered to it (OfferArea).
				_cameFrom = _home && _home->layout && !_home->layoutBroken ? std::optional<Visit>(Visit{ _home->cell, _home->DisplayName() }) : std::nullopt;
			}
			_stale = false;
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
		// An extra cell of a multi-cell home (DESIGN §6.7) only needs to be nobody else's, and the home itself
		// still eligible: the layout lists it on purpose (an exterior cell included).
		const auto verdict = _home->extra ? CheckExtraArea(*_home, cell) : game::CheckCell(cell);
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
		if (display->layout) {
			std::map<core::FormRef, std::map<std::string, std::int32_t>> counts;
			for (const auto& [item, container] : display->layout->learned) {
				if (const auto& category = LearnedCategory(item); !category.empty()) {
					++counts[container][category];
				}
			}
			for (const auto& [container, perCategory] : counts) {
				auto& sorted = display->learnedCategories[container];
				sorted.assign(perCategory.begin(), perCategory.end());  // id order
				std::ranges::stable_sort(sorted, [](const auto& a_lhs, const auto& a_rhs) { return a_lhs.second > a_rhs.second; });
			}
		}
		_display.store(std::move(display));
	}

	const std::string& HomeRegistry::LearnedCategory(const core::FormRef& a_item)
	{
		if (const auto it = _learnedCategory.find(a_item); it != _learnedCategory.end()) {
			return it->second;
		}
		std::string    category;
		auto*          form = game::LookupForm(a_item);
		auto*          object = form ? form->As<RE::TESBoundObject>() : nullptr;
		core::ItemInfo info;
		if (object && game::FillBaseInfo(info, object)) {
			if (auto classified = Store::Get().Categories().Classify(info)) {
				category = std::move(*classified);
			}
		}
		return _learnedCategory.emplace(a_item, std::move(category)).first->second;
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
		home.here = a_cellRef;
		// Multi-cell homes (DESIGN §6.7): a cell another home's layout lists is part of that home.
		const auto membership = Store::Get().MembershipOf(a_cellRef);
		home.extra = membership.kind == core::CellMembership::Kind::Extra;
		home.cell = home.extra ? membership.home : a_cellRef;
		home.layout = Store::Get().LoadPlayerLayout(home.cell, home.layoutBroken);
		if (home.extra && !home.layout && !home.layoutBroken) {
			// The layout that listed this cell is gone (deleted by hand): the cell is its own again.
			logger::info("Cell {}: the setup of {} that listed it is gone", home.here.ToString(), home.cell.ToString());
			home.extra = false;
			home.cell = home.here;
			home.layout = Store::Get().LoadPlayerLayout(home.cell, home.layoutBroken);
		}
		home.name = home.extra ? MainCellName(home) : game::CellName(a_cell);
		BuildAreas(home);
		home.backup = Store::Get().LatestHomeBackup(home.cell);
		Resolve(home);
		_home = std::move(home);
		Publish();

		const auto& h = *_home;
		if (h.extra) {
			const auto* area = h.HereArea();
			logger::info("Cell {} ({}): part of {} ({})", area ? area->name : game::CellName(a_cell), h.here.ToString(), h.name, h.cell.ToString());
		}
		LogAreas(h);
		logger::info("Cell {} ({}): {}", h.name, h.cell.ToString(),
			h.layout                                                        ? "player layout"s :
			h.layoutBroken                                                  ? "player layout file is broken (left untouched)"s :
			h.resolution.kind == core::LayoutResolution::Kind::Template ? "template available"s :
			                                                                  "no layout"s);
		if (!h.layout && h.backup) {
			logger::info("  a backup of its layout can be restored: {} ({})", Store::PathText(h.backup->path), h.backup->date);
		}
	}

	void HomeRegistry::Resolve(Home& a_home)
	{
		a_home.bestFit.reset();
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
				std::string conflicting;
				for (const auto& p : rejected.conflictingPlugins) {
					conflicting += conflicting.empty() ? p : ", " + p;
				}
				if (!missing.empty()) {
					logger::info("  template {} not offered: missing {}", templates[rejected.index].source, missing);
				}
				if (!conflicting.empty()) {
					logger::info("  template {} not offered: {} rebuilds this home (conflicts)", templates[rejected.index].source, conflicting);
				}
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
			// The home's cells changed (a template, a restored setup, an area added or removed): the areas follow,
			// and if the player's cell is no longer one of them, the next Current() loads it on its own.
			if (RebuildAreasIfChanged(a_home) && a_home.extra && !a_home.HereArea()) {
				_stale = true;
			}
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

	const core::TemplateFit* HomeRegistry::BestTemplateFit(Home& a_home)
	{
		if (a_home.bestFit) {
			return &*a_home.bestFit;
		}
		const auto* best = a_home.BestTemplate();
		auto*       player = game::Player();
		auto*       cell = player ? player->GetParentCell() : nullptr;
		if (!best || a_home.extra || !cell || cell->GetFormID() != a_home.cellId || !cell->IsAttached()) {
			return nullptr;
		}
		const auto& fit = a_home.bestFit.emplace(CheckTemplateFit(best->layout, cell));
		if (log::FirstTime(std::format("template-fit:{}:{}", a_home.cell.ToString(), best->source))) {
			const auto counts = std::format("{} of {} references there ({} present, {} not built yet, {} disabled or deleted, {} missing; {} not checked)",
				fit.present + fit.notBuilt, fit.checked, fit.present, fit.notBuilt, fit.disabled, fit.missing, fit.unknown);
			if (fit.fits) {
				logger::debug("{}: template {} fits: {}", a_home.name, best->source, counts);
			} else {
				logger::info("{}: template {} may not fit this home (another mod may have rebuilt it): {}; offered, not enabled automatically",
					a_home.name, best->source, counts);
			}
		}
		return &fit;
	}

	bool HomeRegistry::ApplyTemplate(Home& a_home, const core::TemplateCandidate& a_template)
	{
		if (a_home.layoutBroken) {
			logger::error("Not applying template {} to {}: the existing layout file could not be parsed", a_template.source, a_home.name);
			return false;
		}
		// What is there now is kept: unsaved changes are written, then the file is copied to a .bak, and the copy
		// outside Data (DESIGN §6.6) goes to its history with the next write, so Saved setups can bring it back.
		if (a_home.layout && a_home.dirty) {
			Save(a_home);
		}
		if (!Store::Get().BackupPlayerLayout(a_home.cell)) {
			logger::error("Not applying template {} to {}: the current layout could not be backed up", a_template.source, a_home.name);
			return false;
		}
		if (a_home.layout) {
			Store::Get().KeepHomeBackupHistoryNext(a_home.cell);
		}
		const auto source = a_template.source;  // a_template may live in the store; copy what we log
		const auto version = a_template.layout.version;
		const auto before = !a_home.layout ? "no setup"s : a_home.layout->basedOn ? *a_home.layout->basedOn : "a custom setup"s;
		auto       layout = core::ApplyTemplate(a_template, a_home.layout ? &*a_home.layout : nullptr);
		layout.cell = a_home.cell;
		a_home.layout = std::move(layout);
		Resolve(a_home);
		logger::info("Applied template {} v{} to {} (player data merged)", source, version, a_home.name);
		logger::debug("{}: template switched from {} to {}", a_home.name, before, core::MakeBasedOn(source, version));
		if (TakeInUntouchedCells(a_home) > 0) {
			BuildAreas(a_home);  // the cells taken in are areas now, even when the layout lists the same cells as before
			LogAreas(a_home);
		}
		return Save(a_home);
	}

	bool HomeRegistry::SetTemplateHidden(Home& a_home, std::string_view a_source, bool a_hidden)
	{
		if (!a_home.layout || a_home.layoutBroken) {
			return false;
		}
		if (!(a_hidden ? core::HideTemplate(*a_home.layout, a_source) : core::UnhideTemplate(*a_home.layout, a_source))) {
			return false;
		}
		Resolve(a_home);
		logger::info("{}: template {} {}", a_home.name, a_source, a_hidden ? "removed from this home" : "added back");
		return Save(a_home);
	}

	void HomeRegistry::RefreshTemplates()
	{
		if (_home) {
			Resolve(*_home);
			logger::debug("Cell {}: templates resolved again ({} eligible)", _home->name,
				_home->resolution.alternatives.size() + (_home->resolution.chosen ? 1u : 0u));
		}
	}

	bool HomeRegistry::Reset(Home& a_home)
	{
		if (!Store::Get().BackupAndRemovePlayerLayout(a_home.cell)) {
			return false;
		}
		a_home.layout.reset();
		a_home.layoutBroken = false;
		a_home.dirty = false;
		// The copy outside Data stays (Restore your last setup); a new setup saved later moves it to history first.
		Store::Get().KeepHomeBackupHistoryNext(a_home.cell);
		a_home.backup = Store::Get().LatestHomeBackup(a_home.cell);
		RebuildAreasIfChanged(a_home);  // only the main cell is left
		if (a_home.extra) {
			_stale = true;  // the player's cell belonged to the home through that layout: on its own from now on
		}
		Resolve(a_home);
		Publish();
		logger::info("Reset home {}", a_home.name);
		return true;
	}

	HomeRegistry::RestoreResult HomeRegistry::RestoreBackup(Home& a_home, const std::filesystem::path& a_backup)
	{
		auto restored = Store::Get().LoadHomeBackup(a_home.cell, a_backup);
		if (!restored) {
			return RestoreResult::Broken;
		}
		// What is there now is kept: unsaved changes are written, then the file is copied to a .bak.
		if (a_home.layout && a_home.dirty && !a_home.layoutBroken) {
			Save(a_home);
		}
		if (!Store::Get().BackupPlayerLayout(a_home.cell)) {
			logger::error("Not restoring {}: the current layout file could not be backed up", a_home.name);
			return RestoreResult::Failed;
		}
		const auto before = a_home.layout.has_value() ? "replacing its layout"s : a_home.layoutBroken ? "replacing its broken layout file"s : "it had no layout"s;
		a_home.layout = std::move(restored);
		a_home.layoutBroken = false;
		a_home.dirty = true;  // until saved
		Resolve(a_home);
		logger::info("Restored {} from {} ({})", a_home.name, Store::PathText(a_backup), before);
		const bool saved = Save(a_home);
		a_home.backup = Store::Get().LatestHomeBackup(a_home.cell);
		return saved ? RestoreResult::Restored : RestoreResult::Failed;
	}

	// --- multi-cell homes (DESIGN §6.7) ------------------------------------------------------------------------

	std::optional<HomeRegistry::AreaOffer> HomeRegistry::OfferArea()
	{
		auto* player = game::Player();
		auto* cell = player ? player->GetParentCell() : nullptr;
		(void)Current();  // loads the player's cell (and updates where they came from)
		if (!cell || !_cameFrom || !_home || _home->cellId != cell->GetFormID()) {
			return std::nullopt;
		}
		// A setup of its own (or a broken file) wins, and a cell already part of a home stays where it is.
		if (_home->layout || _home->layoutBroken || _home->extra || _cameFrom->home == _home->cell) {
			return std::nullopt;
		}
		if (!CheckExtraCell(cell).ok) {
			return std::nullopt;
		}
		return AreaOffer{ _cameFrom->home, _cameFrom->name, _home->here, _home->cellId };
	}

	bool HomeRegistry::AddArea(const AreaOffer& a_offer, std::string_view a_label)
	{
		const auto offer = OfferArea();
		if (!offer || offer->home != a_offer.home || offer->cellId != a_offer.cellId) {
			logger::info("Add area: the offer to add {} to {} is no longer valid", a_offer.cell.ToString(), a_offer.homeName);
			return false;
		}
		bool broken = false;
		auto layout = Store::Get().LoadPlayerLayout(offer->home, broken);
		if (!layout || broken) {
			logger::warn("Add area: {} has no readable setup; {} not added", offer->homeName, offer->cell.ToString());
			return false;
		}
		if (std::ranges::find(layout->cells, offer->cell, &core::HomeCell::cell) == layout->cells.end()) {
			layout->cells.push_back(core::HomeCell{ offer->cell });
		}
		core::SetHomeCellLabel(layout->cells, offer->cell, a_label);  // the entry is there now
		layout->cell = offer->home;
		if (!Store::Get().SavePlayerLayout(offer->home, *layout)) {
			return false;
		}
		const auto& label = std::ranges::find(layout->cells, offer->cell, &core::HomeCell::cell)->label;
		logger::info("Add area: {} ({}{}) is now part of {} ({})", _home ? _home->name : ""s, offer->cell.ToString(),
			label.empty() ? ""s : std::format(", named \"{}\"", label), offer->homeName, offer->home.ToString());
		_stale = true;  // the next Current() loads this cell as part of that home
		return true;
	}

	bool HomeRegistry::RenameArea(Home& a_home, const core::FormRef& a_cell, std::string_view a_label)
	{
		if (!a_home.layout || a_home.layoutBroken || a_cell == a_home.cell) {
			return false;
		}
		if (!core::SetHomeCellLabel(a_home.layout->cells, a_cell, a_label)) {
			return false;
		}
		const auto& label = std::ranges::find(a_home.layout->cells, a_cell, &core::HomeCell::cell)->label;
		logger::info("Rename area: {} of {} is {}", a_cell.ToString(), a_home.name, label.empty() ? "named after its cell again"s : std::format("now \"{}\"", label));
		const bool saved = Save(a_home);
		BuildAreas(a_home);  // the same cells, new names (Save only rebuilds when the cells change)
		Publish();
		return saved;
	}

	bool HomeRegistry::RemoveArea(Home& a_home, const core::FormRef& a_cell)
	{
		if (!a_home.layout || a_home.layoutBroken || a_cell == a_home.cell) {
			return false;
		}
		const auto removed = std::erase_if(a_home.layout->cells, [&](const core::HomeCell& c) { return c.cell == a_cell; });
		if (removed == 0) {
			return false;
		}
		logger::info("Remove area: {} is no longer part of {}", a_cell.ToString(), a_home.name);
		return Save(a_home);  // rebuilds the areas; the player's cell, if it was that one, is on its own again
	}
}
