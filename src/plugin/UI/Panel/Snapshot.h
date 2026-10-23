#pragma once

#include "Settings.h"

// Plain-data snapshots rendered by the Menu Framework panel (DESIGN §10.1).
//
// Threading contract: snapshots are built on the MAIN thread (PanelModel, from HomeRegistry/Store and
// game forms), published as shared_ptr<const ...> under a mutex, and only READ on the render thread.
// Nothing in here points into game memory or into HomeRegistry/Store: every field is a copy.

namespace dova::ui::panel
{
	enum class Tab : std::uint8_t
	{
		None,  // keep the current tab
		Actions,
		Containers,
		Learned,
		Keep,
		Template
	};

	// Category hierarchy derived from the dotted ids (synthetic parents for missing ids).
	struct CategoryNode
	{
		std::string              id;
		std::string              name;
		std::vector<std::size_t> children;  // indices into CategoryTree::nodes, definition order
	};

	// An item the player carries, for the keep rules' Item... picker.
	struct InventoryEntry
	{
		std::string name;
		std::string refText;  // "Skyrim.esm|0x0001C4E6": what the keep rule stores
	};

	struct CategoryTree
	{
		std::vector<CategoryNode> nodes;
		std::vector<std::size_t>  roots;
	};

	struct ContainerRow
	{
		core::FormRef            ref;
		std::string              refText;   // "Skyrim.esm|0x000AAAAA"
		std::string              label;     // binding label, else the game name
		std::string              gameName;  // reference display name ("" if not loaded)
		std::string              note;      // the binding's note (why a template ignores it...)
		std::vector<std::string> patterns;  // bound category patterns
		std::string              patternsText;  // display names, comma separated
		std::size_t              explicitForms{ 0 };
		std::size_t              learnedCount{ 0 };
		std::map<std::string, std::int32_t> learnedByCategory;  // learned items per primary category id
		std::string              learnedText;  // their category names, most items first ("Books, Potions")
		// Category suggestion (DESIGN §8.6): the category a few learned items share that this container does
		// not bind yet ("" = none), its display name and how many learned items it covers.
		std::string              suggestionCategory;
		std::string              suggestionName;
		std::int32_t             suggestionCount{ 0 };
		std::vector<std::pair<std::string, std::string>> declined;  // suggestions turned down: (id, display name)
		bool                     master{ false };
		bool                     managed{ false };  // has a binding entry (or is the master)
		bool                     ignored{ false };
		bool                     sell{ false };  // the sell chest (Layout::IsSellChest, DESIGN §8.8)
		bool                     available{ false };
		std::string              reason;   // why it is unavailable
		std::string              warning;  // accepted with a warning (e.g. respawn flag in a never-reset cell)
		// Multi-cell homes (DESIGN §6.7): the area it is in when that is not the player's ("" otherwise), and
		// whether it is reached from here anyway (persistent, or its cell is loaded).
		std::string              area;
		bool                     remote{ false };
	};

	// One cell of a multi-cell home (Home::areas).
	struct AreaRow
	{
		core::FormRef cell;
		std::string   cellText;
		std::string   name;
		bool          main{ false };
		bool          here{ false };  // the player is in it
	};

	struct LearnedRow
	{
		core::FormRef item;
		std::string   itemText;
		std::string   itemName;
		std::string   containerText;
		std::string   containerLabel;
		bool          containerManaged{ false };
		// Why the entry does nothing right now ("" = it works): the container is no longer used (`unused`: not
		// in the setup, ignored, or the master; what "Forget those" removes), or it fails the safety checks
		// (not loaded, not built yet...). Out of reach from this area of a multi-cell home is not stale.
		std::string   stale;
		bool          unused{ false };
	};

	struct TemplateOption
	{
		std::string  source;
		std::string  name;
		std::string  author;
		std::string  version;
		std::int32_t priority{ 0 };
		bool         best{ false };
		bool         applied{ false };    // the layout's basedOn points at this template
		bool         autoDraft{ false };  // "autoDraft": true (DESIGN §6.8): offered only when no template someone made fits
		bool         hidden{ false };     // removed from this home (Layout::hiddenTemplates): listed last, with Add back
	};

	// A copy of this home's setup kept outside Data (Store::HomeBackups, DESIGN §6.6).
	struct BackupOption
	{
		std::filesystem::path path;
		std::string           date;  // "2026-10-12 14:03"
		bool                  history{ false };
		bool                  same{ false };  // says the same as the current setup: restoring it would change nothing (not offered)
	};

	// The sell chest (DESIGN §8.8): what Sell now would do, the buy-back, and the session's sales.
	struct SaleRow
	{
		std::string                                          time;
		std::string                                          home;
		std::string                                          chests;
		bool                                                 timed{ false };
		bool                                                 boughtBack{ false };
		std::int32_t                                         items{ 0 };
		std::int64_t                                         gold{ 0 };
		std::vector<std::tuple<std::string, std::int32_t, std::int64_t>> lines;  // name, count, gold
	};

	struct SellInfo
	{
		bool         chest{ false };  // the home has a sell chest
		bool         reachable{ false };  // one can be used from here
		std::int32_t items{ 0 };  // Sell now: what goes
		std::int64_t gold{ 0 };
		std::string  what;
		std::string  stays;
		std::optional<std::pair<std::int32_t, std::int64_t>> buyBack;  // items, gold
		std::string  buyBackWhat;
		std::vector<SaleRow> log;  // newest first
	};

	struct HomeSnapshot
	{
		std::uint64_t serial{ 0 };

		bool        inHome{ false };
		std::string notHomeReason;
		// What the player carries (named, from a plugin), sorted by name: the keep rules' Item... picker. Also
		// outside a home (the Settings page's default keep rules).
		std::vector<InventoryEntry> inventory;
		// The player's cell can join the home they just came from (HomeRegistry::OfferArea): its name, and the
		// cell id Controller::AddArea takes. Also set on the setup page of a cell with no setup.
		std::optional<std::string> addAreaTo;
		std::uint32_t              addAreaCell{ 0 };
		std::string                addAreaName;  // the cell's own name: what the area is called unless the player names it

		std::uint32_t cellId{ 0 };
		std::string   cellText;
		std::string   homeName;
		// Multi-cell homes: every cell in use (main first; more than one entry only for such a home), the area
		// the player is in when it is not the main one, and listed cells that have a setup of their own.
		std::vector<AreaRow>     areas;
		std::string              hereArea;
		std::vector<std::string> areasExcluded;

		bool                       enabled{ false };  // has a player layout
		bool                       layoutBroken{ false };
		std::string                layoutName;
		std::string                layoutAuthor;
		std::string                layoutVersion;
		std::optional<std::string> basedOn;
		// Applying a template would replace changes the player made (core::HasOwnChanges: a custom setup, or one that
		// differs from the template it is based on): switching asks first. Worked out on each rebuild, never per frame.
		bool ownChanges{ false };

		std::vector<ContainerRow>  containers;  // master first, then bindings, then the focus container if unmanaged
		std::optional<std::size_t> masterRow;
		std::vector<LearnedRow>    learned;
		std::size_t                learnedUnused{ 0 };  // rows with `unused`
		std::size_t                learnedStale{ 0 };   // rows with a `stale` reason (unused ones included)
		std::vector<TemplateOption> templates;  // eligible templates for this cell, best first (core::ResolveLayout's order), then the removed ones
		std::vector<std::string>   layoutWarnings;  // ValidateLayoutCategories on the current layout
		// Saved setups outside Data: the latest copy first, then history, newest first. Listed on a full
		// refresh (and when the home changes); carried over otherwise.
		std::vector<BackupOption> backups;

		SellInfo sell;  // worked out on every rebuild (one look into the sell chests)

		std::optional<core::KeepConfig> keep;  // the layout's keep block
		std::string                     keepJson;  // serialized keep block ("" = none): change detection for the editor draft
		core::KeepConfig                globalKeep;
		// Names of the items the keep rules (this home's and the defaults) name by form ref, resolved on each rebuild:
		// "Skyrim.esm|0x00012EB7" -> "Iron Sword"; "" when the form is not loaded.
		std::map<std::string, std::string> keepNames;

		// Retrieve counts (managed, available containers). Only refreshed on open / after actions.
		bool                                countsValid{ false };
		std::int32_t                        totalCount{ 0 };
		std::vector<std::int32_t>           nodeCounts;  // parallel to categories->nodes (pattern "<id>.*")
		std::shared_ptr<const CategoryTree> categories;

		// Focus request (hotkey on a container / at the master). The render thread selects focusRow and
		// switches to focusTab when focusSerial changes.
		std::uint64_t              focusSerial{ 0 };
		std::optional<std::size_t> focusRow;
		Tab                        focusTab{ Tab::None };

		// The container the player looks at (the crosshair target, or the chest a front or bookshelf stands for):
		// marked "(looking at it)" in the Containers table and scrolled into view. An unmanaged one is added as a row.
		std::optional<std::size_t> aimedRow;
	};

	struct TemplateInfo
	{
		std::string              source;
		std::string              name;
		std::string              author;
		std::string              version;
		std::int32_t             priority{ 0 };
		bool                     autoDraft{ false };
		std::string              cellText;
		std::string              cellName;
		std::vector<std::string> required;  // JSON "requires"
		std::vector<std::string> missing;
		std::vector<std::string> conflicting;  // JSON "conflicts" that are loaded: not offered
		std::vector<std::string> diagnostics;
		bool                     homeLoaded{ true };  // its cell is in the load order (false: the home's mod is not loaded)

		// The home is installed: its cell is there and every required plugin is loaded (the Templates page's filter).
		[[nodiscard]] bool Installed() const { return homeLoaded && missing.empty(); }
	};

	struct GlobalSnapshot
	{
		// Settings (live; edited on the settings page, applied and saved by Controller::ApplySettings)
		Settings::Values settings;
		std::string      hotkeyName;
		core::KeepConfig globalKeep;      // Settings.json keep defaults (editable on the Settings page)
		std::string      globalKeepJson;  // its JSON, to notice changes

		std::vector<TemplateInfo> templates;
		std::size_t               categoryCount{ 0 };

		std::vector<core::Diagnostic> categoryDiagnostics;
		std::vector<core::Diagnostic> templateDiagnostics;
		std::vector<core::Diagnostic> settingsDiagnostics;
		std::vector<core::Diagnostic> selfSortingDiagnostics;

		std::string version;
		std::string runtime;
		float       frameworkVersion{ 0.0f };

		// What is installed (decides which settings can do anything).
		bool        isVR{ false };
		bool        quickLootReady{ false };
		bool        uiExtensions{ false };

		// Materials at the workbench as resolved now (sort::crafting::Mode): what Auto means here, and why.
		std::string craftingMode;  // core::crafting::SupplyModeName
		std::string craftingWhy;   // "" when nothing needs saying
		bool        craftingFromStorage{ false };  // the hooks are available on this runtime
	};

	struct ExportState
	{
		std::uint64_t            serial{ 0 };
		std::uint32_t            cell{ 0 };  // the home it was saved from: another home's result is not shown
		bool                     pending{ false };
		bool                     ok{ false };
		bool                     exists{ false };  // waiting for an overwrite confirmation
		std::string              name;             // the name it was saved (or asked about) under
		std::string              path;
		std::string              message;
		std::vector<std::string> warnings;
	};
}
