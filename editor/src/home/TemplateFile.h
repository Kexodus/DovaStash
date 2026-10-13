#pragma once

// Writing a template the way the DLL's "Save as template" does (DESIGN §6.5, docs/EDITOR.md §4.4):
// `<base>/SKSE/Plugins/DovaStash/Templates/<Author>/<Name>.json` (names through core::TemplateFileStem),
// `requires` refreshed, an existing file copied to a timestamped .bak first and the version raised above it,
// written with core::SaveLayoutFile. `<base>` is a mod folder (under MO2: the "Dova Stash Templates" mod)
// or a Data folder. Used by the window's Template tab and by `--cell <cell> --draft --save-to <folder>`.

#include <filesystem>
#include <string>

#include "dova/core/Layout.h"

namespace dova::editor::home
{
	inline constexpr const char* kTemplatesMod = "Dova Stash Templates";

	[[nodiscard]] std::filesystem::path TemplatePath(const std::filesystem::path& a_base, const core::Layout& a_template);

	struct SaveResult
	{
		bool                  ok{ false };
		bool                  replaced{ false };  // a file was there (now in `backup`)
		std::filesystem::path path;
		std::filesystem::path backup;
		std::string           version;  // the version written
		std::string           error;
	};

	// `a_template`'s version and requires are updated to what was written. `a_modMeta`: create a minimal MO2
	// meta.ini in `a_base` when it has none (a new "Dova Stash Templates" mod).
	SaveResult SaveTemplateFile(const std::filesystem::path& a_base, core::Layout& a_template, bool a_modMeta);
}
