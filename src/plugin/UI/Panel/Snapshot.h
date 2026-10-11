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
		std::vector<std::string> patterns;  // bound category patterns
		std::string              patternsText;  // display names, comma separated
		std::size_t              explicitForms{ 0 };
		std::size_t              learnedCount{ 0 };
		bool                     master{ false };
		bool                     managed{ false };  // has a binding entry (or is the master)
		bool                     ignored{ false };
		bool                     available{ false };
		std::string              reason;   // why it is unavailable
		std::string              warning;  // accepted with a warning (e.g. respawn flag in a never-reset cell)
	};

	struct LearnedRow
	{
		core::FormRef item;
		std::string   itemText;
		std::string   itemName;
		std::string   containerText;
		std::string   containerLabel;
		bool          containerManaged{ false };
	};

	struct TemplateOption
	{
		std::string  source;
		std::string  name;
		std::string  author;
		std::string  version;
		std::int32_t priority{ 0 };
		bool         best{ false };
		bool         applied{ false };  // the layout's basedOn points at this template
	};

	struct HomeSnapshot
	{
		std::uint64_t serial{ 0 };

		bool        inHome{ false };
		std::string notHomeReason;

		std::uint32_t cellId{ 0 };
		std::string   cellText;
		std::string   homeName;

		bool                       enabled{ false };  // has a player layout
		bool                       layoutBroken{ false };
		std::string                layoutName;
		std::string                layoutAuthor;
		std::string                layoutVersion;
		std::optional<std::string> basedOn;

		std::vector<ContainerRow>  containers;  // master first, then bindings, then the focus container if unmanaged
		std::optional<std::size_t> masterRow;
		std::vector<LearnedRow>    learned;
		std::vector<TemplateOption> templates;  // eligible templates for this cell, best first
		std::vector<std::string>   layoutWarnings;  // ValidateLayoutCategories on the current layout

		std::optional<core::KeepConfig> keep;  // the layout's keep block
		std::string                     keepJson;  // serialized keep block ("" = none): change detection for the editor draft
		core::KeepConfig                globalKeep;

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
	};

	struct TemplateInfo
	{
		std::string              source;
		std::string              name;
		std::string              author;
		std::string              version;
		std::int32_t             priority{ 0 };
		std::string              cellText;
		std::string              cellName;
		std::vector<std::string> required;  // JSON "requires"
		std::vector<std::string> missing;
		std::vector<std::string> diagnostics;
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

		std::string version;
		std::string runtime;
		float       frameworkVersion{ 0.0f };

		// What is installed (decides which settings can do anything).
		bool        isVR{ false };
		bool        quickLootReady{ false };
		bool        uiExtensions{ false };
		std::size_t i4Rules{ 0 };
	};

	struct ExportState
	{
		std::uint64_t            serial{ 0 };
		bool                     pending{ false };
		bool                     ok{ false };
		bool                     exists{ false };  // waiting for an overwrite confirmation
		std::string              path;
		std::string              message;
		std::vector<std::string> warnings;
	};
}
