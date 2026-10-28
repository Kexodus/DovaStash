#pragma once

// Layout / template model (DESIGN §6.3) and keep-rule configuration (§8.3).
//
// One struct serves both player layouts (Layouts/*.json) and templates (Templates/**.json); templates
// simply never carry `learned` / `basedOn` (a warning is emitted if they do).
//
// Serialization decisions:
// - Output is indented with tabs and kept compact so people can read it: a short list or an entry without
//   nested objects stays on one line (one line per container), and the fields of such lines are lined up
//   in columns (detail::Dump). Fixed key order: $schema, autoDraft, name, note, author, version, priority, requires,
//   conflicts, replaces, cell, cells, master, containers, displays, keep, learned, declined, hiddenTemplates, basedOn, then any unknown keys
//   (sorted). `cells` (more cells of a multi-cell home, HomeCells.h) and `declined` (turned-down category
//   suggestions, Learning.h) are only written when the layout has any.
//   name/author/version/priority are always written; optional parts only when set (autoDraft only when true).
// - `conflicts` (templates, DESIGN §6.2): plugins that rebuild the home; a template is not offered while any of them
//   is loaded (the negative of `requires`). Written only when the layout has any.
// - `replaces` (templates, DESIGN §6.7): the sources of retired templates this one takes over ("Templates/Vanilla/
//   LakeviewManorCellar.json"). An untouched setup made from one of them may be taken in when this template is applied
//   (IsUntouchedTemplateSetup, Template.h). Written only when the layout has any; ApplyTemplate drops it.
// - `autoDraft` (templates only, DESIGN §6.8): true marks a file written by the editor's bulk "Draft all homes"
//   that no person has saved since. Such a file may be replaced by the next bulk draft; any other template file
//   never is (CheckAutoDraftTarget, Template.h). Saving it as a person drops the mark. The game loads it as any
//   template; a player layout never carries it (ApplyTemplate and ExportAsTemplate clear it).
// - `note` (top level, containers, displays) is free text for people reading the file; Dova Stash never
//   uses it (the panel shows a container's note).
// - Unknown keys at the top level, in container entries, in keep, and in quota rules are preserved
//   verbatim (sorted) so newer files survive a round-trip through an older DLL.
// - Never lose user data: a container entry or learned pair with a malformed form reference is
//   reported as an error but kept verbatim (`unparsedContainers` / `unparsedLearned`) and written back.
//   A second container or display entry for the same ref is a warning: the first entry is the binding,
//   the duplicate is kept verbatim the same way (never used, so it cannot contradict the first). A
//   name/author/version/priority of the wrong type is kept verbatim in `extra` and written back as long
//   as the field itself is still at its default.
// - `learned` is written sorted by FormRef (plugin case-insensitive, then id) for stable diffs.
// - Per-item explicit bindings (DESIGN §7 step 1) live in each container entry as
//   `"forms": ["Plugin.esp|0x..."]`.
// - keep: both spellings from the spec are accepted for quota rules — "quotas" (§8.3) and "rules"
//   (§6.3). The key used on load is remembered and re-used on save. `inherit` defaults to TRUE when a
//   layout has a keep block but omits it: a layout adding one quota should not silently drop the global
//   gold/lockpick/torch keeps. Write `"inherit": false` to replace the globals. A quota rule's `restock`
//   (bool) is written after `quota`, only when true; a value of another type is a warning and kept verbatim.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "dova/core/Diagnostics.h"
#include "dova/core/DisplayKind.h"
#include "dova/core/FormRef.h"
#include "dova/core/HomeCells.h"

namespace dova::core
{
	inline constexpr std::string_view kLayoutSchema = "dovastash-layout-1";

	// One entry of keep.always / keep.never. Classification: a reserved token ("equipped", "favorited",
	// "quest"; case-insensitive) first, then anything containing '|' is a FormRef, otherwise a category
	// pattern. Token names are therefore reserved and cannot be used as category ids in keep lists.
	struct KeepSelector
	{
		enum class Kind : std::uint8_t
		{
			Invalid,
			Equipped,
			Favorited,
			Quest,
			Form,
			Category
		};

		Kind        kind{ Kind::Invalid };
		FormRef     form;     // Kind::Form
		std::string pattern;  // Kind::Category

		[[nodiscard]] static KeepSelector Parse(std::string_view a_text);
	};

	// "keep up to `quota` items matching all given selectors". At least one selector is required.
	// `restock` ("restock": true; optional, off when absent): Restock also tops the player up to `quota`
	// from the home's containers (DESIGN §8.7). Written only when on, so files without it read as before.
	struct QuotaRule
	{
		std::optional<std::string>              category;     // category pattern (wildcards allowed)
		std::optional<std::vector<std::string>> keywordsAny;  // keyword EditorIDs
		std::optional<std::vector<FormRef>>     forms;        // explicit base forms
		std::int32_t                            quota{ 0 };
		bool                                    restock{ false };
		nlohmann::json                          extra = nlohmann::json::object();  // unknown keys, preserved
	};

	struct KeepConfig
	{
		std::optional<bool>      inherit;  // nullopt = not written; treated as true (see header comment)
		std::vector<std::string> always;   // tokens ("equipped", "favorited", "quest"), FormRefs or category patterns
		std::vector<std::string> never;    // same vocabulary
		std::vector<QuotaRule>   quotas;
		nlohmann::json           unparsedQuotas = nlohmann::json::array();  // invalid rules, written back verbatim
		// Parallel to unparsedQuotas: how many parsed rules came before each one, so it is written back in its place
		// (rules are first-match). Missing entries: written last.
		std::vector<std::size_t> unparsedQuotaSlots;
		bool                     quotasKeyIsRules{ false };  // loaded from "rules" instead of "quotas"
		nlohmann::json           extra = nlohmann::json::object();

		[[nodiscard]] bool Inherits() const noexcept { return inherit.value_or(true); }
	};

	struct ContainerBinding
	{
		FormRef                  ref;  // container reference (REFR)
		std::string              label;
		std::string              note;        // for people reading the file (why it is ignored...)
		std::vector<std::string> categories;  // patterns
		std::vector<FormRef>     forms;       // explicit per-item bindings
		std::vector<std::string> unparsedForms;  // malformed "forms" entries, written back verbatim
		bool                     ignored{ false };
		// "sell": true (DESIGN §8.8): the sell chest. Items reach it like any container (categories, learned,
		// fixed items); what is in it is sold for gold only when the player confirms "Sell now" (or turned on
		// timed selling). Nothing else is ever taken out of it. Inert on the master or an ignored container.
		bool                     sell{ false };
		nlohmann::json           extra = nlohmann::json::object();
	};

	// A display the layout uses (DESIGN §8.4): only listed displays that are not ignored are filled. The
	// player turns one on with the hotkey (an entry is added) and off again (it stays, ignored); templates
	// list theirs. `categories` limits what a rack, mannequin or bookshelf takes, like a container's. Dova Stash
	// recognises vanilla racks, mannequins, item plaques, scripted display cases and bookshelves by their scripts; an
	// entry can change their items, or name a display it cannot recognise (with `kind`).
	// One shape in layouts and templates (TemplateDisplays.h has the rules for templates): the items of a rack,
	// mannequin or bookshelf are chosen by the file (soft: one that does not exist in the load order is skipped); the
	// items of an item plaque or a case are what its own script is made for (a template never decides them).
	struct DisplayBinding
	{
		FormRef                    ref;  // rack activator, plaque, mannequin actor, case or bookshelf container (REFR/ACHR)
		std::optional<DisplayKind> kind;
		std::string                label;
		std::string                note;
		std::vector<std::string>   categories;  // Rack/Mannequin/Bookshelf: patterns it takes (empty: anything that fits)
		std::vector<FormRef>       items;  // Item/Case: what it is made for. Rack/Mannequin/Bookshelf: preferred items
		std::vector<std::string>   unparsedItems;  // malformed "items" entries, written back verbatim
		bool                       only{ false };  // Rack/Mannequin/Bookshelf with items: takes nothing but them ("only": true; optional)
		std::optional<FormRef>     set;  // Mannequin: any piece of the armor set it wears (ArmorSets.h); nullopt: it picks a set
		bool                       ignored{ false };  // never fill it
		nlohmann::json             extra = nlohmann::json::object();
	};

	struct Layout
	{
		bool                          autoDraft{ false };  // "autoDraft": true: written by "Draft all homes", no person saved it since
		std::string                   name;
		std::string                   note;  // what the file is, for people reading it
		std::string                   author;
		std::string                   version;
		std::int32_t                  priority{ 0 };
		std::vector<std::string>      requiredPlugins;  // JSON "requires": plugin file names
		std::vector<std::string>      conflictPlugins;  // JSON "conflicts": not offered while any of these is loaded
		std::vector<std::string>      replaces;  // JSON "replaces" (templates): retired template sources this one takes over
		std::optional<FormRef>        cell;
		std::vector<HomeCell>         cells;  // more cells of the same home (DESIGN §6.7); optional
		std::optional<FormRef>        master;
		std::vector<ContainerBinding> containers;
		std::vector<DisplayBinding>   displays;
		std::optional<KeepConfig>     keep;
		std::map<FormRef, FormRef>    learned;  // base form -> container ref
		// Category suggestions the player turned down (Learning.h): container ref -> category ids. "Send all
		// Books to End Table?" is never asked again for a pair listed here. Player layouts only; optional.
		std::map<FormRef, std::vector<std::string>> declined;
		// Templates the player removed from this home (DESIGN §6.2): their sources ("Templates/Vanilla/Breezehome.json"),
		// never offered, chosen or applied here until added back (HideTemplate / UnhideTemplate, Template.h). Player
		// layouts only; written only when there is one. A value of the wrong shape is kept verbatim in `extra`.
		std::vector<std::string>      hiddenTemplates;
		std::optional<std::string>    basedOn;  // "Templates/Vanilla/Breezehome.json@1.0.0"

		nlohmann::json extra = nlohmann::json::object();               // unknown top-level keys
		nlohmann::json unparsedContainers = nlohmann::json::array();   // entries we could not parse
		nlohmann::json unparsedDisplays = nlohmann::json::array();     // display entries we could not parse
		nlohmann::json unparsedLearned = nlohmann::json::object();     // pairs we could not parse
		nlohmann::json unparsedDeclined = nlohmann::json::object();    // "declined" entries we could not parse
		nlohmann::json unparsedCells = nlohmann::json::array();        // "cells" entries we could not parse

		[[nodiscard]] const ContainerBinding* FindContainer(const FormRef& a_ref) const;
		[[nodiscard]] ContainerBinding*       FindContainer(const FormRef& a_ref);
		[[nodiscard]] const DisplayBinding*   FindDisplay(const FormRef& a_ref) const;
		[[nodiscard]] DisplayBinding*         FindDisplay(const FormRef& a_ref);
		[[nodiscard]] bool                    IsMaster(const FormRef& a_ref) const { return master && *master == a_ref; }
		// The master or an area master (`cells[].master`, DESIGN §6.7): an inbox, never learned into.
		[[nodiscard]] bool IsAnyMaster(const FormRef& a_ref) const
		{
			return IsMaster(a_ref) || std::ranges::any_of(cells, [&](const HomeCell& c) { return c.master && *c.master == a_ref; });
		}
		// A container marked "sell" that is neither ignored nor the master or an area master (DESIGN §8.8).
		[[nodiscard]] bool IsSellChest(const FormRef& a_ref) const;
		[[nodiscard]] bool HasSellChest() const;
	};

	// --- Keep config JSON (also used by the plugin for global defaults in Settings.json) -----------

	[[nodiscard]] std::optional<KeepConfig> ParseKeepConfig(const nlohmann::json& a_value, std::string_view a_source, Diagnostics& a_diags);
	[[nodiscard]] nlohmann::ordered_json     KeepConfigToJson(const KeepConfig& a_keep);

	// --- Layout JSON -----------------------------------------------------------------------------

	// Returns nullopt only when the document is unusable (not JSON, not an object, wrong $schema, or
	// "containers" / "learned" of the wrong type: those could not be written back without replacing the
	// player's data, so the plugin treats the file as broken and never overwrites it).
	// Individual bad entries are reported and skipped/preserved; check `a_diags.HasErrors()`.
	[[nodiscard]] std::optional<Layout> ParseLayout(const nlohmann::json& a_doc, std::string_view a_source, Diagnostics& a_diags);
	[[nodiscard]] std::optional<Layout> ParseLayoutText(std::string_view a_text, std::string_view a_source, Diagnostics& a_diags);

	[[nodiscard]] nlohmann::ordered_json LayoutToJson(const Layout& a_layout);
	[[nodiscard]] std::string            SerializeLayout(const Layout& a_layout);  // tab-indented, trailing newline
	// The two layouts say the same (what they serialize to is equal; formatting and `cell` do not count). Used to
	// hide a "Restore last setup" that would change nothing (DESIGN §6.6).
	[[nodiscard]] bool SameSetup(const Layout& a_lhs, const Layout& a_rhs);

	[[nodiscard]] std::optional<Layout> LoadLayoutFile(const std::filesystem::path& a_path, std::string_view a_source, Diagnostics& a_diags);
	// Atomic write (temp file + rename, retried 3 times about 50 ms apart: a sync client such as OneDrive may hold
	// the target for a moment). Creates parent directories. `a_inPlaceFallback`: when the rename still fails, write
	// the target in place instead (for copies outside Data, the home backups; a setup file under Data stays atomic).
	[[nodiscard]] bool SaveLayoutFile(const std::filesystem::path& a_path, const Layout& a_layout, bool a_inPlaceFallback = false);

	// "Skyrim.esm_000165A7.json" — the player layout file name for a cell (DESIGN §6).
	[[nodiscard]] std::string LayoutFileName(const FormRef& a_cell);
}
