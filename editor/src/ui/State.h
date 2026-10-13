#pragma once

// What the editor remembers between runs (docs/EDITOR.md §3.6): the MO2 instance and profile last opened, the
// author name, and each home's to-do stage (drafted -> reviewed -> validated -> saved) with where it was saved.
// One JSON file, %LOCALAPPDATA%\DovaStashEditor\state.json, written atomically after each change. Homes are keyed
// by their main cell's form reference ("Skyrim.esm|0x000165A8"), which does not depend on the load order.

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace dova::editor::ui
{
	enum class HomeStage : int
	{
		None = 0,
		Drafted = 1,    // a draft was made and looked at
		Reviewed = 2,   // the author accepted, rejected or edited something
		Validated = 3,  // reviewed, and the checks find no problem
		Saved = 4       // written to a template file
	};

	[[nodiscard]] const char* HomeStageName(HomeStage a_stage);

	struct HomeRecord
	{
		std::string name;
		HomeStage   stage{ HomeStage::None };
		std::string savedPath;
		std::string updated;  // "2026-10-12 14:03"
	};

	class EditorState
	{
	public:
		void Load();
		void Save() const;

		std::optional<std::string> mo2;
		std::optional<std::string> profile;
		std::string                author;
		std::map<std::string, HomeRecord> homes;  // main cell form ref -> record

		// Raises a home's stage (never lowers it, except `a_force`) and saves.
		void Advance(const std::string& a_cell, const std::string& a_name, HomeStage a_stage, const std::string& a_savedPath = {}, bool a_force = false);
		[[nodiscard]] HomeStage StageOf(const std::string& a_cell) const;

		[[nodiscard]] static std::filesystem::path File();
	};
}
