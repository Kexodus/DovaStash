#pragma once

// JSON data I/O (DESIGN §6): categories, templates, global keep defaults, player layouts.
//
//   Data/SKSE/Plugins/DovaStash/Categories/*.json   merged in file-name order
//   Data/SKSE/Plugins/DovaStash/Templates/**.json   community / shipped templates (read-only)
//   Data/SKSE/Plugins/DovaStash/Layouts/<cell>.json player layouts (written by us)
//   Data/SKSE/Plugins/DovaStash/Settings.json       optional: { "keep": { ...global keep defaults... } }
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

		[[nodiscard]] const core::CategorySet&                 Categories() const noexcept { return _categories; }
		[[nodiscard]] std::span<const core::TemplateCandidate> Templates() const noexcept { return _templates; }
		[[nodiscard]] const core::KeepConfig&                  GlobalKeep() const noexcept { return _globalKeep; }

		// Diagnostics from the last Load() (shown by the Menu Framework UI's Diagnostics page).
		[[nodiscard]] const core::Diagnostics& CategoryDiagnostics() const noexcept { return _categoryDiags; }
		[[nodiscard]] const core::Diagnostics& TemplateDiagnostics() const noexcept { return _templateDiags; }
		[[nodiscard]] const core::Diagnostics& SettingsDiagnostics() const noexcept { return _settingsDiags; }

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

		static void LogDiagnostics(const core::Diagnostics& a_diags, std::string_view a_context);

	private:
		Store() = default;

		void LoadCategories();
		void LoadTemplates();
		void LoadGlobalKeep();

		core::CategorySet                    _categories;
		std::vector<core::TemplateCandidate> _templates;
		core::KeepConfig                     _globalKeep;
		core::Diagnostics                    _categoryDiags;
		core::Diagnostics                    _templateDiags;
		core::Diagnostics                    _settingsDiags;
	};
}
