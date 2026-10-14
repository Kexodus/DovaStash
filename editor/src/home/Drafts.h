#pragma once

// Bulk drafts (docs/EDITOR.md §3.4): every player home of the active load order, grouped by the doors between
// them (a main hall and its cellar are one home), and a reviewable draft template for each, made by
// core::SuggestDraft from the facts of the plugins: what each container is called, the crafting stations and
// displays around it, the items placed on it. The suggestions keep their confidence and reason so the author
// can accept or reject each one; the layout follows those choices.

#include <atomic>
#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dova/core/Category.h"
#include "esp/Items.h"
#include "dova/core/DraftSuggest.h"
#include "dova/core/Layout.h"
#include "home/Cell.h"

namespace dova::editor::home
{
	struct HomeEntry
	{
		struct Extra
		{
			esp::FormKey cell;
			std::string  cellRef;
			std::string  name;
			std::string  editorId;
			bool         likely{ false };  // a home by its own evidence (else: only reached through the home's door)
		};

		struct Door
		{
			esp::FormKey door;
			std::string  leadsTo;  // the other side's cell (EditorID) or worldspace
			bool         exterior{ false };
		};

		esp::FormKey       cell;  // the main cell
		std::string        cellRef;
		std::string        name;  // the cell's name, else its EditorID
		std::string        editorId;
		std::string        plugin;      // the plugin that defines the main cell
		std::string        lastPlugin;  // the plugin with the winning cell record
		HomeCandidate      candidate;   // the main cell's evidence
		std::vector<Extra> extra;       // more cells of the same home (doors lead there)
		std::vector<Door>  doors;       // load doors of all its cells, resolved once for every home
		int                containers{ 0 };  // over all its cells
		int                displays{ 0 };
	};

	using ScanProgress = std::function<void(std::size_t a_done, std::size_t a_total, const std::string& a_stage)>;

	// Every likely player home among the interior cells of the loaded plugins (each cell once, as the load order
	// has it), with the interior cells their doors lead to joined when those are homes too, or storage that
	// nothing respawns in and nobody else owns. Sorted by plugin load order, then name.
	[[nodiscard]] std::vector<HomeEntry> FindAllHomes(esp::LoadOrder& a_lo, const ScanProgress& a_progress = {}, const std::atomic<bool>* a_cancel = nullptr);

	// Item names of the load order (lower-case) -> their category, for containers named after an item. Names
	// that mean different categories, and clutter, are left out.
	using ItemNameIndex = std::unordered_map<std::string, std::string>;
	[[nodiscard]] ItemNameIndex BuildItemNameIndex(const esp::ItemCatalog& a_catalog, const core::CategorySet& a_categories);

	struct DraftContext
	{
		const core::CategorySet* categories{ nullptr };  // classifies loose items; unknown patterns are dropped
		const core::DraftRules*  rules{ nullptr };       // nullptr: core's defaults
		const ItemNameIndex*     itemNames{ nullptr };   // optional
	};

	// core::DraftOptions for a draft of the home whose main cell is `a_mainCell`.
	[[nodiscard]] core::DraftOptions MakeDraftOptions(const std::string& a_mainCell, const DraftContext& a_context);

	// The facts core::SuggestDraft works on, one object per container, display, load door, loose item and piece
	// of furniture of the cells. `a_where` (optional) receives (cell index, ref index) per object.
	[[nodiscard]] std::vector<core::DraftObject> BuildDraftObjects(esp::LoadOrder& a_lo, std::span<const CellInfo> a_cells, const core::CategorySet* a_categories,
		std::vector<std::pair<std::size_t, std::size_t>>* a_where = nullptr);

	struct HomeDraft
	{
		std::vector<CellInfo>                            cells;  // main first
		std::vector<core::DraftObject>                   objects;
		std::vector<std::pair<std::size_t, std::size_t>> where;  // object -> (cell, ref)
		core::DraftPlan                                  plan;
		core::Layout                                     layout;
		std::vector<std::string>                         diagnostics;
	};

	[[nodiscard]] HomeDraft DraftHome(esp::LoadOrder& a_lo, const HomeEntry& a_home, const DraftContext& a_context);

	// The home's cells as form references: the main cell, then the others (core::CheckAutoDraftTarget's home).
	[[nodiscard]] std::vector<core::FormRef> HomeCellRefs(const esp::LoadOrder& a_lo, const HomeEntry& a_home);

	// The template the plan describes: name, cell, cells, master, every container (accepted categories, ignored
	// with notes), every display (plaques and cases with the items their script is made for; shield and dagger
	// racks with their category), a note, `requires`.
	[[nodiscard]] core::Layout LayoutFromPlan(esp::LoadOrder& a_lo, std::span<const CellInfo> a_cells, std::span<const core::DraftObject> a_objects,
		const core::DraftPlan& a_plan);

	// Accepts or rejects one suggestion: the plan's flag and the container's categories in the layout.
	void SetSuggestion(HomeDraft& a_draft, std::size_t a_container, std::size_t a_suggestion, bool a_accepted);

	// How far a draft agrees with a template written by a person (docs/EDITOR.md §3.4, accuracy):
	// - per container the template gives categories: the draft's are the same, overlap, differ or are missing;
	// - per category of the set: does it end up in the same container (the planner's rule: the most specific
	//   pattern, the first container on a tie, else the master)?
	struct TemplateComparison
	{
		int                      containers{ 0 };  // template containers with categories
		int                      exact{ 0 }, overlap{ 0 }, different{ 0 }, missing{ 0 };
		int                      categories{ 0 }, sameDestination{ 0 };
		bool                     sameMaster{ false };
		int                      ignoredInTemplate{ 0 }, ignoredAgree{ 0 };
		std::vector<std::string> details;  // one line per template container with categories
	};
	[[nodiscard]] TemplateComparison CompareToTemplate(const core::Layout& a_draft, const core::Layout& a_template, const core::CategorySet& a_categories);

	// The container a category goes to under a layout (most specific pattern, first on a tie), else the master.
	[[nodiscard]] std::optional<core::FormRef> CategoryDestination(const core::Layout& a_layout, std::string_view a_category);

	// `requires` refreshed, then core::ValidateTemplate and (with categories) ValidateLayoutCategories.
	[[nodiscard]] std::vector<std::string> ValidateDraft(core::Layout& a_layout, const core::CategorySet* a_categories);
}
