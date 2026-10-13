#pragma once

// Dova Stash's own data as the player's setup has it (docs/EDITOR.md §3.4): the category files
// (Categories/*.json, merged as the DLL merges them), every template in the merged Data folder (shipped ones,
// other mods' packs, the player's own under Templates/User and the "Dova Stash Templates" mod the editor saves
// into), and the draft rules (DraftRules.json next to the categories, else core's defaults). Shared by the
// window and the headless commands.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "dova/core/Category.h"
#include "dova/core/DraftSuggest.h"
#include "dova/core/Layout.h"

namespace dova::editor
{
	class Workspace;
}

namespace dova::editor::home
{
	struct CategoryData
	{
		std::vector<core::CategoryDef>   defs;
		std::optional<core::CategorySet> set;
		std::string                      source;  // the folder they came from
		std::vector<std::string>         problems;
	};

	// Data/SKSE/Plugins/DovaStash/Categories/*.json of the merged Data folder (a later root wins a file name);
	// without Dova Stash in the profile: next to the editor (its mod folder), else the mod named "Dova Stash",
	// else any mod that has the default set.
	[[nodiscard]] CategoryData LoadCategories(const Workspace& a_ws);

	struct KnownTemplate
	{
		std::string           source;  // "Templates/Vanilla/Breezehome.json"
		std::filesystem::path path;
		std::string           origin;  // the mod (folder name) it comes from, "Data" for the game folder
		bool                  user{ false };  // the player's own: Templates/User or the "Dova Stash Templates" mod
		core::Layout          layout;
	};

	struct TemplateIndex
	{
		std::vector<KnownTemplate> templates;
		std::vector<std::string>   problems;

		// Templates whose `cell` is the cell, or that list it in `cells`.
		[[nodiscard]] std::vector<const KnownTemplate*> ForCell(const core::FormRef& a_cell) const;
	};

	// Every Templates/**.json of the merged Data folder, plus the "Dova Stash Templates" mod even when MO2 has
	// not enabled it yet (the editor just saved into it).
	[[nodiscard]] TemplateIndex LoadTemplateIndex(const Workspace& a_ws);

	struct RulesData
	{
		core::DraftRules         rules;
		std::string              source;  // "built in" or the file
		std::vector<std::string> problems;
	};

	// SKSE/Plugins/DovaStash/DraftRules.json of the merged Data folder when there is one, else the defaults.
	[[nodiscard]] RulesData LoadDraftRules(const Workspace& a_ws);

	// Where the editor saves: the "Dova Stash Templates" mod under MO2, else the Data folder.
	[[nodiscard]] std::filesystem::path SaveBase(const Workspace& a_ws);
}
