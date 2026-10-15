#pragma once

// JSON data I/O (DESIGN §6): categories, templates, global keep defaults, player layouts.
//
//   Data/SKSE/Plugins/DovaStash/Categories/*.json   merged in file-name order
//   Data/SKSE/Plugins/DovaStash/Templates/**.json   community / shipped templates (read-only)
//   Data/SKSE/Plugins/DovaStash/SelfSorting/*.json  homes that sort themselves (DESIGN §9.1), file-name order
//   Data/SKSE/Plugins/DovaStash/Layouts/<cell>.json player layouts (written by us)
//   Data/SKSE/Plugins/DovaStash/Settings.json       optional: { "keep": { ...global keep defaults... } }
//
// Home backups (DESIGN §6.6), outside Data, which MO2 does not virtualise (under MO2 everything the game
// writes below Data lands in the overwrite folder, which a cleanup can empty):
//   Documents/My Games/<edition>/SKSE/DovaStash/Backups/<cell>.json                   the latest copy
//   Documents/My Games/<edition>/SKSE/DovaStash/Backups/<cell>.json.<stamp>.bak       history (newest 5)
// Every layout save also writes the latest copy, on a background thread (atomic, latest wins); the
// first write of a session per home (and the first after a reset) moves the previous copy to history
// first. A failed backup never fails the save (warned once in the log).
//
// Load() runs once at kDataLoaded (forms must exist for formList resolution). Main thread only.

namespace dova::home
{
	class Store
	{
	public:
		static constexpr auto kRoot = "Data/SKSE/Plugins/DovaStash"sv;

		[[nodiscard]] static Store& Get();

		void Load();
		// Scans Templates/** again (after an export), so a new template is offered without a restart.
		// Indices into Templates() (LayoutResolution) are stale afterwards: re-resolve the current home
		// (HomeRegistry::RefreshTemplates). Main thread.
		void ReloadTemplates();

		[[nodiscard]] const core::CategorySet&                 Categories() const noexcept { return _categories; }
		[[nodiscard]] std::span<const core::TemplateCandidate> Templates() const noexcept { return _templates; }
		[[nodiscard]] const core::KeepConfig&                  GlobalKeep() const noexcept { return _globalKeep; }
		// Homes that sort themselves (DESIGN §9.1): the entries whose plugin is loaded. Never changes after Load(),
		// so any thread may read it.
		[[nodiscard]] const core::SelfSortingHomes& SelfSorting() const noexcept { return _selfSorting; }
		// Writes the "keep" block of Settings.json (other keys in the file are kept) and makes it current.
		bool SaveGlobalKeep(core::KeepConfig a_keep);

		// Diagnostics from the last Load() (shown by the Menu Framework UI's Diagnostics page).
		[[nodiscard]] const core::Diagnostics& CategoryDiagnostics() const noexcept { return _categoryDiags; }
		[[nodiscard]] const core::Diagnostics& TemplateDiagnostics() const noexcept { return _templateDiags; }
		[[nodiscard]] const core::Diagnostics& SettingsDiagnostics() const noexcept { return _settingsDiags; }
		[[nodiscard]] const core::Diagnostics& SelfSortingDiagnostics() const noexcept { return _selfSortingDiags; }

		static constexpr std::size_t kMaxBackups = 5;  // timestamped backups kept per layout file

		[[nodiscard]] std::filesystem::path LayoutPath(const core::FormRef& a_cell) const;
		// `a_broken` is set when a layout file exists but cannot be parsed, or when its existence cannot be
		// determined (it must then never be overwritten). The returned layout's `cell` is always `a_cell`.
		[[nodiscard]] std::optional<core::Layout> LoadPlayerLayout(const core::FormRef& a_cell, bool& a_broken) const;
		// Always writes LayoutPath(a_cell), whatever `a_layout.cell` says (it is forced to `a_cell`).
		bool SavePlayerLayout(const core::FormRef& a_cell, const core::Layout& a_layout) const;
		// Copies the player layout to "<file>.<YYYYMMDD-HHMMSS>.bak" (the newest kMaxBackups are kept).
		// True if there is no file or the backup was written.
		bool BackupPlayerLayout(const core::FormRef& a_cell) const;
		// Renames the player layout to a timestamped backup (as above). True if nothing is left behind.
		bool BackupAndRemovePlayerLayout(const core::FormRef& a_cell) const;
		// Copies any data file (an exported template about to be replaced) to a timestamped backup next to
		// it, as above. True if there is no file or the backup was written.
		static bool BackupFile(const std::filesystem::path& a_path, std::string_view a_context);

		// --- multi-cell homes (DESIGN §6.7; Home/Areas.cpp) ----------------------------------------------
		// A player layout file exists for the cell (parsed or not). One file check.
		[[nodiscard]] bool HasPlayerLayoutFile(const core::FormRef& a_cell) const;
		// Which home the cell belongs to: its own (a layout of its own, or no layout lists it), or the home
		// whose layout lists it in `cells` (core::ResolveCellMembership). From an index of every player
		// layout's cells: built by Load(), kept current by every layout load, save and reset.
		[[nodiscard]] core::CellMembership MembershipOf(const core::FormRef& a_cell) const;

		// --- home backups outside Data (see the header comment) ----------------------------------------
		struct HomeBackup
		{
			std::filesystem::path path;
			std::string           date;               // local time of its last write: "2026-10-12 14:03"
			std::int64_t          time{ 0 };          // the same, as a time_t
			bool                  history{ false };   // a timestamped history copy (false: the latest copy)
		};
		// Documents/My Games/<edition>/SKSE/DovaStash/Backups (the log's folder; VR and GOG included), or
		// nullopt when Documents cannot be found.
		[[nodiscard]] static const std::optional<std::filesystem::path>& BackupDir();
		// The in-game test harness (AutoTest/AutoTest.h) keeps a test run's copies apart from the player's: called
		// at plugin load, before anything asks for BackupDir(). Nothing else calls it.
		static void RedirectBackups(std::filesystem::path a_dir);
		// The latest copy of this home's layout, if there is one. Main thread (one file check).
		[[nodiscard]] std::optional<HomeBackup> LatestHomeBackup(const core::FormRef& a_cell) const;
		// The latest copy first, then the history copies, newest first. Main thread (lists the folder).
		[[nodiscard]] std::vector<HomeBackup> HomeBackups(const core::FormRef& a_cell) const;
		// Loads a backup with the layout loader (so a broken one is never used: nullopt, logged). The
		// returned layout's `cell` is `a_cell`.
		[[nodiscard]] std::optional<core::Layout> LoadHomeBackup(const core::FormRef& a_cell, const std::filesystem::path& a_path) const;
		// The copy at `a_path` says the same as `a_layout` (core::SameSetup: formatting does not count), so
		// restoring it would change nothing. False when it cannot be read or parsed (quiet: debug only). One file
		// read; for the panel's refresh, never per frame.
		[[nodiscard]] bool HomeBackupMatches(const std::filesystem::path& a_path, const core::Layout& a_layout) const;
		// The next backup write for this home first moves the latest copy to history (after a reset, so the
		// setup that was reset survives a new one).
		void KeepHomeBackupHistoryNext(const core::FormRef& a_cell) const;

		// UTF-8 text of a path for logs and the UI (never throws, unlike path::string() with characters the
		// code page lacks: Documents can have any name).
		[[nodiscard]] static std::string PathText(const std::filesystem::path& a_path);

		static void LogDiagnostics(const core::Diagnostics& a_diags, std::string_view a_context);

	private:
		Store() = default;

		void LoadCategories();
		void LoadTemplates();
		void LoadGlobalKeep();
		void LoadSelfSorting();
		// Queues the latest copy of a saved layout (SavePlayerLayout).
		void BackupHome(const core::FormRef& a_cell, const core::Layout& a_layout) const;
		// Load(): copies every layout file that has no copy outside Data yet (setups made before this
		// feature, or while the backup folder was gone). Never replaces a copy.
		void BackupExistingLayouts() const;
		// Load(): reads every Layouts/*.json once for its `cells` (Home/Areas.cpp).
		void LoadHomeCellIndex();
		// Records the cells a player layout lists (nullptr: the cell has no layout any more).
		void IndexHomeCells(const core::FormRef& a_cell, const core::Layout* a_layout) const;

		mutable std::vector<core::HomeCellsEntry> _homeCells;  // player layouts that list extra cells (main thread)
		core::CategorySet                    _categories;
		std::vector<core::TemplateCandidate> _templates;
		core::KeepConfig                     _globalKeep;
		core::Diagnostics                    _categoryDiags;
		core::Diagnostics                    _templateDiags;
		core::Diagnostics                    _settingsDiags;
		core::SelfSortingHomes               _selfSorting;
		core::Diagnostics                    _selfSortingDiags;
	};
}
