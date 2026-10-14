#pragma once

// Writing a template the way the DLL's "Save as template" does (DESIGN §6.5, docs/EDITOR.md §4.4), one folder
// per home: `<base>/SKSE/Plugins/DovaStash/Templates/<Home>/<Author>.json` (names through core::TemplateFileStem;
// the folder is the home's name, so every template of a home sits together and the author is the file),
// `requires` refreshed, an existing file copied to a timestamped .bak first and the version raised above it,
// written with core::SaveLayoutFile. `<base>` is a mod folder (under MO2: the "Dova Stash Templates" mod)
// or a Data folder. Used by the window's Template tab and by `--cell <cell> --draft --save-to <folder>`.
// "Draft all homes" (the window's *Save N drafts...*, `--draft-all --save-to`) saves with SaveOptions::autoDraft:
// the file is marked `"autoDraft": true` and only replaces that home's own earlier auto-draft
// (core::CheckAutoDraftTarget, DESIGN §6.8). Any other save is a person's: the mark is dropped.

#include <filesystem>
#include <string>
#include <string_view>

#include "dova/core/Layout.h"

namespace dova::editor::home
{
	inline constexpr const char* kTemplatesMod = "Dova Stash Templates";

	// `a_home`: the home's name (the folder); empty: the template's own name.
	[[nodiscard]] std::filesystem::path TemplatePath(const std::filesystem::path& a_base, const core::Layout& a_template, std::string_view a_home = {});
	// `<base>/SKSE/Plugins/DovaStash/Templates`: what core::ScanTemplateFiles reads before a bulk save.
	[[nodiscard]] std::filesystem::path TemplatesDir(const std::filesystem::path& a_base);

	struct SaveOptions
	{
		bool modMeta{ false };     // create a minimal MO2 meta.ini in the base when it has none (a new "Dova Stash Templates" mod)
		bool autoDraft{ false };   // "Draft all homes": mark the file "autoDraft": true (implies onlyOverAutoDrafts)
		bool onlyOverAutoDrafts{ false };  // a bulk save: never replace a file that is not this home's auto-draft
	};

	struct SaveResult
	{
		bool                  ok{ false };
		bool                  replaced{ false };  // a file was there (now in `backup`)
		std::filesystem::path path;
		std::filesystem::path backup;
		std::string           version;  // the version written
		std::string           error;
		bool                  skipped{ false };  // onlyOverAutoDrafts: a file someone made is there (`error` says which)
	};

	// `a_template`'s version, requires and autoDraft mark are updated to what was written.
	SaveResult SaveTemplateFile(const std::filesystem::path& a_base, core::Layout& a_template, const SaveOptions& a_options, std::string_view a_home = {});
}
