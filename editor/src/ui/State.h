#pragma once

// What the editor remembers between runs (docs/EDITOR.md §3.6): the MO2 instance and profile last opened, the
// author name, the save target, the panels' layout, and each home's to-do stage (drafted -> reviewed -> validated -> saved) with where it was saved.
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

	// The workspace's panels (docs/EDITOR-UX.md §2): open or collapsed, and their size in pixels.
	struct UiLayout
	{
		bool  railOpen{ true };
		float railWidth{ 0.0f };  // 0: the default
		bool  inspectorOpen{ true };
		float inspectorWidth{ 0.0f };
		bool  drawerOpen{ false };
		float drawerHeight{ 0.0f };  // 0: a third of the window
		int   view{ 0 };             // 0 3D, 1 plan

		friend bool operator==(const UiLayout&, const UiLayout&) = default;
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
		UiLayout                   ui;
		std::string                saveTarget;  // the mod templates are saved into; empty: "Dova Stash Templates"

		// Raises a home's stage (never lowers it, except `a_force`) and saves.
		void Advance(const std::string& a_cell, const std::string& a_name, HomeStage a_stage, const std::string& a_savedPath = {}, bool a_force = false);
		[[nodiscard]] HomeStage StageOf(const std::string& a_cell) const;

		[[nodiscard]] static std::filesystem::path File();
	};
}
