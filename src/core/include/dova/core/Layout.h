#pragma once

// Layout / template model (DESIGN §6.3) and keep-rule configuration (§8.3).
//
// One struct serves both player layouts (Layouts/*.json) and templates (Templates/**.json); templates
// simply never carry `learned` / `basedOn` (a warning is emitted if they do).
//
// Serialization decisions:
// - Output is indented with tabs and kept compact so people can read it: a short list or an entry without
//   nested objects stays on one line (one line per container), and the fields of such lines are lined up
//   in columns (detail::Dump). Fixed key order: $schema, name, note, author, version, priority, requires,
//   cell, master, containers, displays, keep, learned, basedOn, then any unknown keys (sorted).
//   name/author/version/priority are always written; optional parts only when set.
// - `note` (top level, containers, displays) is free text for people reading the file; Dova Stash never
//   uses it (the panel shows a container's note).
// - Unknown keys at the top level, in container entries, in keep, and in quota rules are preserved
//   verbatim (sorted) so newer files survive a round-trip through an older DLL.
// - Never lose user data: a container entry or learned pair with a malformed form reference is
//   reported as an error but kept verbatim (`unparsedContainers` / `unparsedLearned`) and written back.
// - `learned` is written sorted by FormRef (plugin case-insensitive, then id) for stable diffs.
// - Per-item explicit bindings (DESIGN §7 step 1) live in each container entry as
//   `"forms": ["Plugin.esp|0x..."]`.
// - keep: both spellings from the spec are accepted for quota rules — "quotas" (§8.3) and "rules"
//   (§6.3). The key used on load is remembered and re-used on save. `inherit` defaults to TRUE when a
//   layout has a keep block but omits it: a layout adding one quota should not silently drop the global
//   gold/lockpick/torch keeps. Write `"inherit": false` to replace the globals.

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
	struct QuotaRule
	{
		std::optional<std::string>              category;     // category pattern (wildcards allowed)
		std::optional<std::vector<std::string>> keywordsAny;  // keyword EditorIDs
		std::optional<std::vector<FormRef>>     forms;        // explicit base forms
		std::int32_t                            quota{ 0 };
		nlohmann::json                          extra = nlohmann::json::object();  // unknown keys, preserved
	};

	struct KeepConfig
	{
		std::optional<bool>      inherit;  // nullopt = not written; treated as true (see header comment)
		std::vector<std::string> always;   // tokens ("equipped", "favorited", "quest"), FormRefs or category patterns
		std::vector<std::string> never;    // same vocabulary
		std::vector<QuotaRule>   quotas;
		nlohmann::json           unparsedQuotas = nlohmann::json::array();  // invalid rules, written back verbatim
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
		nlohmann::json           extra = nlohmann::json::object();
	};

	// A display the layout uses (DESIGN §8.4): only listed displays that are not ignored are filled. The
	// player turns one on with the hotkey (an entry is added) and off again (it stays, ignored); templates
	// list theirs. `categories` limits what a rack or mannequin takes, like a container's. Dova Stash
	// recognises vanilla racks, mannequins, item plaques and scripted display cases by their scripts; an
	// entry can change their items, or name a display it cannot recognise (with `kind`).
	struct DisplayBinding
	{
		FormRef                    ref;  // rack activator, plaque, mannequin actor or case container (REFR/ACHR)
		std::optional<DisplayKind> kind;
		std::string                label;
		std::string                note;
		std::vector<std::string>   categories;  // Rack/Mannequin: patterns it takes (empty: anything that fits)
		std::vector<FormRef>       items;  // Item/Case: what it is made for. Rack/Mannequin: preferred items
		std::vector<std::string>   unparsedItems;  // malformed "items" entries, written back verbatim
		std::optional<FormRef>     set;  // Mannequin: any piece of the armor set it wears (ArmorSets.h); nullopt: it picks a set
		bool                       ignored{ false };  // never fill it
		nlohmann::json             extra = nlohmann::json::object();
	};

	struct Layout
	{
		std::string                   name;
		std::string                   note;  // what the file is, for people reading it
		std::string                   author;
		std::string                   version;
		std::int32_t                  priority{ 0 };
		std::vector<std::string>      requiredPlugins;  // JSON "requires": plugin file names
		std::optional<FormRef>        cell;
		std::optional<FormRef>        master;
		std::vector<ContainerBinding> containers;
		std::vector<DisplayBinding>   displays;
		std::optional<KeepConfig>     keep;
		std::map<FormRef, FormRef>    learned;  // base form -> container ref
		std::optional<std::string>    basedOn;  // "Templates/Vanilla/Breezehome.json@1.0.0"

		nlohmann::json extra = nlohmann::json::object();               // unknown top-level keys
		nlohmann::json unparsedContainers = nlohmann::json::array();   // entries we could not parse
		nlohmann::json unparsedDisplays = nlohmann::json::array();     // display entries we could not parse
		nlohmann::json unparsedLearned = nlohmann::json::object();     // pairs we could not parse

		[[nodiscard]] const ContainerBinding* FindContainer(const FormRef& a_ref) const;
		[[nodiscard]] ContainerBinding*       FindContainer(const FormRef& a_ref);
		[[nodiscard]] const DisplayBinding*   FindDisplay(const FormRef& a_ref) const;
		[[nodiscard]] DisplayBinding*         FindDisplay(const FormRef& a_ref);
		[[nodiscard]] bool                    IsMaster(const FormRef& a_ref) const { return master && *master == a_ref; }
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

	[[nodiscard]] std::optional<Layout> LoadLayoutFile(const std::filesystem::path& a_path, std::string_view a_source, Diagnostics& a_diags);
	// Atomic write (temp file + rename). Creates parent directories.
	[[nodiscard]] bool SaveLayoutFile(const std::filesystem::path& a_path, const Layout& a_layout);

	// "Skyrim.esm_000165A7.json" — the player layout file name for a cell (DESIGN §6).
	[[nodiscard]] std::string LayoutFileName(const FormRef& a_cell);
}
