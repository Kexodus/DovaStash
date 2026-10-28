#pragma once

// Per-cell home state (DESIGN §6.2): the player's current cell, its player layout (if any), and the
// template resolution. Only the current cell is tracked; moves reach the home's other cells only as
// Home/Areas.h allows (loaded cells, persistent references). Main thread only.
//
// Cell-change detection: the registry is refreshed lazily from PlayerCharacter::GetParentCell() every
// time anything asks for Current() (hotkey, events, menu actions), so it is always right even if an
// event was missed. BGSActorCellEvent / TESCellFullyLoadedEvent / kPostLoadGame only trigger an
// early refresh so the one-time template notification shows on entry.
//
// Multi-cell homes (DESIGN §6.7, Home/Areas.h): a Home is still loaded per cell the player is in (`cellId`,
// `here`), but in one of the extra cells a layout lists, it is that layout's home (`cell` is the main cell,
// `extra` is set): the same layout, saved to the main cell's file. Menus keep capturing `cellId`, so they
// stay valid while the player stays in that cell, as before.

#include "Home/Areas.h"
#include "Home/Store.h"

namespace dova::home
{
	struct Home
	{
		RE::FormID                  cellId{ 0 };  // the player's cell this Home was loaded for
		core::FormRef               cell;         // the home's main cell: the layout's identity and file
		core::FormRef               here;         // the player's cell (== cell unless `extra`)
		bool                        extra{ false };  // the player is in one of the home's extra cells
		std::vector<Area>           areas;  // every cell of the home in use: the main cell first (Areas.h)
		std::vector<core::FormRef>  areasExcluded;  // cells the layout lists that have a setup of their own
		std::vector<core::FormRef>  areasFrom;      // the layout's cells the areas were built from
		std::string                 name;
		std::optional<core::Layout> layout;  // player layout; nullopt = not set up yet
		// The name the HUD gives the home in every notice (setup, reset, areas): its setup's name (the template's,
		// "Tundra Homestead (with cellar)"), else the cell's.
		[[nodiscard]] const std::string& DisplayName() const noexcept { return layout && !layout->name.empty() ? layout->name : name; }
		core::LayoutResolution      resolution;
		bool                        layoutBroken{ false };  // a layout file exists but does not parse: never overwrite
		bool                        dirty{ false };
		// The latest copy of this home's layout outside Data (Store::LatestHomeBackup): what "Restore your last
		// setup" brings back when the layout file is gone. Checked on entry and after a reset or restore.
		std::optional<Store::HomeBackup> backup;
		// Activators that open a hidden container (game::FindFronts): activator -> container. Scanned once
		// the cell is attached.
		std::unordered_map<RE::FormID, RE::FormID> fronts;
		bool                                       frontsScanned{ false };
		// How much of BestTemplate() is there in the cell (DESIGN §9.1, the auto-enable guard): judged while the cell is
		// attached (HomeRegistry::BestTemplateFit); nullopt until then, and after the templates are resolved again.
		std::optional<core::TemplateFit> bestFit;

		[[nodiscard]] bool                           Enabled() const noexcept { return layout.has_value(); }
		[[nodiscard]] const core::TemplateCandidate* BestTemplate() const;
		// No setup and no template of its own, but a template of another home lists this cell in its `cells`
		// (core::LayoutResolution::partOf, DESIGN §6.7): that template, set up from its main cell. Else nullptr.
		[[nodiscard]] const core::TemplateCandidate* PartOfTemplate() const;
		// More than one cell in use: containers elsewhere are reached only as Areas.h allows.
		[[nodiscard]] bool MultiCell() const noexcept { return areas.size() > 1; }
		[[nodiscard]] const Area* AreaById(RE::FormID a_cell) const;
		// The area the player is in (nullptr before the areas are built).
		[[nodiscard]] const Area* HereArea() const { return AreaById(cellId); }
	};

	// Read-only copy of the current home for code that runs outside the task queue (the HUD/UI thread:
	// overlay, activate prompt, QuickLoot handlers). On AE the SKSE task queue drains on worker threads,
	// not on the UI thread, so those readers must never touch Home/Layout directly. Published by the
	// registry after every change; readers take a shared_ptr and keep it for the duration of their call.
	struct DisplayHome
	{
		RE::FormID                  cellId{ 0 };
		std::string                 name;
		std::optional<core::Layout> layout;
		std::vector<RE::FormID>     areaCells;  // every loaded cell of the home (Home::areas), the player's included
		std::unordered_map<RE::FormID, RE::FormID> fronts;    // activator -> container it opens
		std::unordered_map<RE::FormID, RE::FormID> frontOf;   // container -> one activator that opens it
		// Per container: the primary categories of the items learned for it with their counts, most items
		// first (ties in id order; items in no category are left out). Worked out when published, so
		// display code never looks up or classifies forms.
		std::map<core::FormRef, std::vector<std::pair<std::string, std::int32_t>>> learnedCategories;

		// `a_cell` is one of the home's cells (the player's, or another area of a multi-cell home).
		[[nodiscard]] bool InHome(RE::FormID a_cell) const
		{
			return a_cell == cellId || std::ranges::find(areaCells, a_cell) != areaCells.end();
		}

		// The activator that stands for `a_container` on screen, or `a_container` itself.
		[[nodiscard]] RE::FormID Anchor(RE::FormID a_container) const
		{
			const auto it = frontOf.find(a_container);
			return it != frontOf.end() ? it->second : a_container;
		}
	};

	// Looks up every reference `a_template` names (core::TemplateFitRefs) in the player's cell, which must be attached
	// and be the template's cell: deleted, or taken out by xEdit's "undelete and disable" (enable parent the player,
	// opposite; or moved below -29000) is Disabled; disabled any other way (through an enable parent, or initially
	// disabled: Hearthfire furniture not built yet) is NotBuilt; a container entry that is no container any more,
	// not found, or found in a cell the template does not name is Missing; in another of its cells that is not
	// attached (or not placed yet) is Unknown. Main thread.
	[[nodiscard]] core::TemplateFit CheckTemplateFit(const core::Layout& a_template, RE::TESObjectCELL* a_cell);

	// Concurrency: the live Home/Layout below is read and written only from SKSE tasks (Controller,
	// ListUI callbacks, sort operations). That relies on the AE task queue running one task at a time;
	// the logs show no overlap, but it is an assumption, not a guarantee. Code on the UI thread uses
	// Display() only.
	class HomeRegistry
	{
	public:
		[[nodiscard]] static HomeRegistry& Get();

		// The eligible home for the player's current cell, or nullptr (with a UI reason).
		[[nodiscard]] Home* Current(std::string* a_reason = nullptr);
		// Same, but only if it is still the cell `a_cellId` (menu callbacks arrive frames later).
		[[nodiscard]] Home* CurrentIf(RE::FormID a_cellId);
		// Any thread: the published copy of the home loaded for the player's current cell, or nullptr
		// (another cell, or nothing loaded yet). Never causes a load. For per-frame display code.
		[[nodiscard]] std::shared_ptr<const DisplayHome> Display() const;

		// True the first time it is called for a cell this session (template notification gating).
		bool MarkNotified(RE::FormID a_cellId) { return _notified.insert(a_cellId).second; }

		// Persistence. MarkDirty() defers the write (learned destinations), Save() writes now.
		void MarkDirty(Home& a_home);
		bool Save(Home& a_home);
		void Flush();  // writes the current home if dirty

		// Layout edits that create the player layout.
		core::Layout& EnsureLayout(Home& a_home);  // new empty player layout for this cell if none
		bool          ApplyBestTemplate(Home& a_home);
		// How much of the best template is there in the home as it is (CheckTemplateFit): judged once per load, while
		// the player is in the home's main cell and it is attached; the counts are logged once per session. nullptr
		// when there is no template, or it cannot be judged yet.
		const core::TemplateFit* BestTemplateFit(Home& a_home);
		// Writes unsaved changes, backs up the player layout file (timestamped .bak; the copy outside Data moves to
		// its history with this save, so Saved setups can bring the old setup back), then merges `a_template` over
		// the current layout (core::ApplyTemplate keeps the player's learned/keep/ignored/forms data) and saves.
		// Only the setup changes: no item is moved. Refused (false, nothing changed) when the layout file is
		// broken or the backup fails.
		bool ApplyTemplate(Home& a_home, const core::TemplateCandidate& a_template);
		// From a cell another home's template lists (Home::PartOfTemplate, DESIGN §6.7): `a_whole` can be set up from
		// here, as its main cell (the template's `cell`) has no setup file (its own setup is never replaced from here).
		[[nodiscard]] bool CanSetUpMainCell(const core::TemplateCandidate& a_whole) const;
		// Applies `a_whole` to its main cell as ApplyTemplate does there, and saves it; this cell then loads as an area
		// of that home. Refused (false, nothing changed) when CanSetUpMainCell is not met. No item is moved.
		bool ApplyTemplateToMainCell(const core::TemplateCandidate& a_whole);
		// Removes a template from this home (`a_hidden`) or adds it back: core::HideTemplate / UnhideTemplate on the
		// setup's hiddenTemplates (DESIGN §6.2), then the home's templates are resolved again and the setup is saved.
		// Only that list changes (and basedOn, when the removed template is the one in use); the file stays on disk and
		// no item is moved. False (nothing changed) without a setup, with a broken one, or when there is nothing to do.
		bool SetTemplateHidden(Home& a_home, std::string_view a_source, bool a_hidden);
		bool          Reset(Home& a_home);
		enum class RestoreResult : std::uint8_t
		{
			Restored,
			Broken,  // the backup does not load: nothing changed
			Failed   // the current layout file could not be backed up (nothing changed), or saving failed
		};
		// Brings a home backup (Store::HomeBackups) back as the player layout: loaded with the layout loader
		// first (a broken one is never used), the current layout file (if any, broken or not) is backed up
		// (timestamped .bak; unsaved changes are written first), then the backup becomes the layout and is
		// saved. Also allowed when the layout file is broken.
		RestoreResult RestoreBackup(Home& a_home, const std::filesystem::path& a_backup);
		// After Store::ReloadTemplates: resolves the loaded home again, so its template choices (the
		// indices in Home::resolution) point into the new template list.
		void RefreshTemplates();

		// --- multi-cell homes (DESIGN §6.7) ----------------------------------------------------------------
		// The player's cell can be added to the home they were in just before (walked out of its door, or
		// across into the next exterior cell): that home is enabled, this cell has no setup of its own, belongs
		// to no home, and is not owned by someone else. nullopt otherwise.
		struct AreaOffer
		{
			core::FormRef home;  // the home's main cell
			std::string   homeName;
			core::FormRef cell;  // the player's cell
			RE::FormID    cellId{ 0 };
		};
		[[nodiscard]] std::optional<AreaOffer> OfferArea();
		// Adds the offered cell to that home's layout (`cells`) and saves it. The player's cell then belongs to
		// that home (the next Current() loads it so). `a_label` (optional, trimmed) is the name the player gave
		// the area ("Grounds"); empty, the cell's own name shows. False (nothing changed) if the offer is no
		// longer valid.
		bool AddArea(const AreaOffer& a_offer, std::string_view a_label = {});
		// Names an extra cell of the home (its `label`; empty clears it) and saves. False if the layout does not
		// list it (the main cell is named by the home).
		bool RenameArea(Home& a_home, const core::FormRef& a_cell, std::string_view a_label);
		// Removes an extra cell from the home's layout and saves it. Its containers stay in the layout (they are
		// simply out of reach). False if the layout does not list it.
		bool RemoveArea(Home& a_home, const core::FormRef& a_cell);
		// Loading a game or starting a new one: the home the player came from is forgotten. Any thread.
		void ForgetVisits() noexcept { _forgetVisits = true; }

	private:
		HomeRegistry() = default;

		void Load(RE::TESObjectCELL* a_cell, core::FormRef a_cellRef);
		// Republishes the display copy of _home (or clears it). Called after every change.
		void Publish();
		void Resolve(Home& a_home);
		// Primary category id of a learned base form ("" when it is not loaded or in no category). Cached:
		// base forms and the category set do not change during a session.
		const std::string& LearnedCategory(const core::FormRef& a_item);

		std::optional<Home>                             _home;
		std::unordered_map<core::FormRef, std::string>  _learnedCategory;
		std::atomic<std::shared_ptr<const DisplayHome>> _display;
		std::unordered_set<RE::FormID> _notified;  // cells that showed the template notification this session
		// Multi-cell homes: the enabled home of the cell the player was in before the current one (OfferArea),
		// and whether the next Current() must load the home again (its cells changed).
		struct Visit
		{
			core::FormRef home;
			std::string   name;
		};
		std::optional<Visit> _cameFrom;
		bool                 _stale{ false };
		std::atomic<bool>    _forgetVisits{ false };
	};
}
