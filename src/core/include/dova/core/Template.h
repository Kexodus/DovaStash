#pragma once

// Template discovery and per-cell layout resolution (DESIGN §6.2).
//
// Decisions:
// - A player layout for the cell always wins (Kind::Player); eligible templates are still ranked and
//   returned as `alternatives` so the UI can offer "re-apply / switch template".
// - A template is a candidate when its `cell` equals the requested cell. It is eligible when every
//   plugin in `requires` is loaded (case-insensitive). The cell's own plugin is implied by the cell
//   match, so it is not checked separately.
// - Ranking (CompareTemplateRank): templates a person made before auto-drafts (`autoDraft`, DESIGN §6.8: the
//   editor's "Draft all homes" output nobody saved since), whatever their priority or version; then priority
//   (desc), then the player's own exports (`source` under kUserTemplatePrefix) before the others, then semantic
//   `version` (desc; unparseable versions rank lowest), then `source` path (case-insensitive asc) as a
//   deterministic tie-break. An auto-draft is bulk-generated, so its priority never lets it beat hand-made
//   work: it is chosen only when the home has no eligible template someone made. The player's own saved setup
//   is what they want back in their game, as their layout always wins over templates.
// - Auto-drafts (DESIGN §6.8): the bulk draft writes an auto-draft only where CheckAutoDraftTarget allows it: no
//   file at the target, or that home's own earlier auto-draft there. A file someone made (no `autoDraft`, or a
//   file that cannot be read: in doubt it is a person's) is never replaced, and a home that already has one in
//   the destination is left alone.
// - ExportAsTemplate (player layout -> template) carries everything a template can hold: master, the
//   home's extra cells, containers (categories, explicit forms, label, note, ignored, unknown keys), displays (kind, label,
//   note, categories, items, only, set, ignored), keep rules, the top-level note and unknown keys, and entries
//   that could not be parsed (kept verbatim, as in the layout). It is never an auto-draft. It deliberately drops `learned` (the
//   player's own item-by-item habits: personal, and often hundreds of entries), `declined` (the player's
//   answers to category suggestions, Learning.h) and `basedOn`, and lists
//   every plugin the result references in `requires` (ReferencedPlugins), so it passes ValidateTemplate
//   and is only offered where its references can resolve. Display items and sets are soft references
//   (TemplateDisplays.h): they never make a template depend on their plugins. With ExportOptions::learnedAsForms the learned
//   destinations are kept instead, as explicit item bindings (`forms`) of the container each points at:
//   the items then go to the same containers wherever the template is used (a personal backup). Skipped:
//   items learned for the master or for an ignored container (the planner never uses those), and items
//   some container already binds explicitly (that binding wins over learned, so nothing changes). A
//   container the layout does not list gets a plain entry. Their plugins join `requires` like any form.
// - ApplyTemplate copies the template, sets basedOn = "<source>@<version>" and drops any learned data
//   carried by the template, and its `autoDraft` mark (a player layout is the player's). When applied over an existing player layout, only the category bindings
//   (and master, name, requires) come from the template; the player's data is merged in, never dropped:
//     * containers in both: template categories; ignored = template OR player; explicit forms are
//       united; the player's label is used when the template has none; unknown keys merged;
//     * containers only in the player layout: kept with NO categories (ignored flag, label, forms and
//       unknown keys kept), so learned destinations to them stay valid; a previous master that the
//       template does not use is kept the same way;
//     * displays in both: the template's entry, with the player's ignored flag (OR) and the player's
//       kind/label/note/categories/items (with their `only`)/set where the template gives none; displays
//       only in the player layout: kept. (An item plaque's or case's items from a template are replaced by
//       what its script is made for, in the plugin: TemplateDisplays.h.)
//     * every learned entry whose container is in the merged layout (and is not the master), and the
//       declined suggestions of those containers;
//     * the home's extra `cells` (HomeCells.h): the template's, then the player's (MergeHomeCells);
//     * the player's keep block (replaces the template's), unparsed learned entries, unparsed
//       container entries and unknown top-level keys.
//   The plugin backs up the player file before applying (HomeRegistry::ApplyBestTemplate).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dova/core/Category.h"
#include "dova/core/Diagnostics.h"
#include "dova/core/Layout.h"

namespace dova::core
{
	// Source prefix of the player's own exports (Data/SKSE/Plugins/DovaStash/Templates/User/).
	inline constexpr std::string_view kUserTemplatePrefix = "Templates/User/";

	// `a_source` is under kUserTemplatePrefix (case-insensitive, either slash).
	[[nodiscard]] bool IsUserTemplate(std::string_view a_source);

	struct TemplateCandidate
	{
		std::string source;  // "Templates/Vanilla/Breezehome.json" (forward slashes)
		Layout      layout;
	};

	// Recursively loads every *.json below `a_dir`. Files that fail to parse are reported and skipped.
	// `source` = a_sourcePrefix + path relative to a_dir. A missing directory yields an empty list
	// (no diagnostic: having no templates is normal). Results are sorted by source.
	[[nodiscard]] std::vector<TemplateCandidate> LoadTemplateDirectory(const std::filesystem::path& a_dir, Diagnostics& a_diags,
		std::string_view a_sourcePrefix = "Templates/");

	// Template-only lint (warnings only): player-only fields present, no master, form references to
	// plugins that are not listed in `requires` (display items and sets are soft and not checked), and
	// the display entry lint (ValidateDisplays, TemplateDisplays.h).
	void ValidateTemplate(const Layout& a_template, std::string_view a_source, Diagnostics& a_diags);

	// Warns about container patterns / keep patterns that match no known category (likely typos).
	void ValidateLayoutCategories(const Layout& a_layout, const CategorySet& a_categories, std::string_view a_source, Diagnostics& a_diags);

	struct LayoutResolution
	{
		enum class Kind : std::uint8_t
		{
			None,      // no player layout and no eligible template
			Player,    // use the player layout
			Template   // offer/apply templates[*chosen]
		};

		struct Rejected
		{
			std::size_t              index{ 0 };       // into the candidate span
			std::vector<std::string> missingPlugins;  // `requires` entries not loaded
		};

		Kind                       kind{ Kind::None };
		std::optional<std::size_t> chosen;        // best eligible template (set even for Kind::Player when one exists)
		std::vector<std::size_t>   alternatives;  // the other eligible templates, best first
		std::vector<Rejected>      rejected;      // templates for this cell whose requirements are not met
	};

	// The order the templates of one home are offered in (ResolveLayout, DESIGN §6.2; see the ranking above):
	// negative when `a_lhs` comes first, positive when `a_rhs` does, 0 when only the source path tells them apart.
	// `a_lhsUser` / `a_rhsUser`: the player's own saved template (IsUserTemplate). The editor sorts its template
	// list with this too, so both show the order the game offers.
	[[nodiscard]] int CompareTemplateRank(const Layout& a_lhs, bool a_lhsUser, const Layout& a_rhs, bool a_rhsUser);

	[[nodiscard]] LayoutResolution ResolveLayout(const Layout* a_playerLayout, std::span<const TemplateCandidate> a_templates, const FormRef& a_cell,
		std::span<const std::string> a_loadedPlugins);

	// Plugins from `requires` that are not in `a_loadedPlugins` (case-insensitive).
	[[nodiscard]] std::vector<std::string> MissingRequirements(const Layout& a_layout, std::span<const std::string> a_loadedPlugins);

	// Player layout created from a template. `a_existing` (optional) is the current player layout when re-applying.
	[[nodiscard]] Layout ApplyTemplate(const TemplateCandidate& a_template, const Layout* a_existing = nullptr);

	// The template a player layout's `basedOn` names, exactly ("<source>@<version>", MakeBasedOn), or nullptr: no
	// `basedOn`, or that template is gone or now has another version.
	[[nodiscard]] const TemplateCandidate* FindBasedOn(const Layout& a_layout, std::span<const TemplateCandidate> a_templates);

	// The player layout has changes of their own that applying a template replaces (DESIGN §6.2, switching
	// templates): it is a custom setup (no `basedOn`), `a_basedOn` is not the template it names (nullptr: gone, or
	// another version now; in doubt it has changes), or applying `a_basedOn` over it again would change it
	// (SameSetup). What ApplyTemplate always carries over (learned items, keep rules, ignored containers, explicit
	// item bindings, containers only the player added) does not count. `a_afterApply` (optional) runs on the
	// re-applied copy before the comparison: whatever the caller does after applying a template (the plugin
	// records what item plaques and display cases are made for).
	[[nodiscard]] bool HasOwnChanges(const Layout& a_layout, const TemplateCandidate* a_basedOn,
		const std::function<void(Layout&)>& a_afterApply = {});

	// Every plugin the layout's form references point into: cell, cells, master, containers and their explicit
	// forms, displays (not their items or set: soft references, TemplateDisplays.h), and FormRefs in keep
	// (always/never entries, quota forms). Learned pairs are not included. First use first; unique
	// case-insensitively.
	[[nodiscard]] std::vector<std::string> ReferencedPlugins(const Layout& a_layout);

	struct ExportOptions
	{
		// Keep the learned destinations as explicit item bindings (`forms`) of their containers.
		bool learnedAsForms{ false };
	};

	// Player layout -> shareable template (see the header comment): drops learned (or, with
	// `learnedAsForms`, turns it into `forms`) and basedOn, keeps everything else, and adds every plugin
	// from ReferencedPlugins to `requires` (entries already there are kept, in their order, ahead of the
	// added ones). Name, author and version are the layout's; the caller sets them. `a_learnedKept`
	// (optional) receives how many learned items became item bindings.
	[[nodiscard]] Layout ExportAsTemplate(const Layout& a_layout, const ExportOptions& a_options = {}, std::size_t* a_learnedKept = nullptr);

	// File stem for an exported template named `a_name`: ASCII letters, digits, space, '-', '_' and '.'
	// are kept, every other byte becomes '_'; leading/trailing spaces and dots are removed, at most 64
	// characters, Windows device names get a trailing '_', and an empty result is "Template".
	[[nodiscard]] std::string TemplateFileStem(std::string_view a_name);

	// "<source>@<version>"
	[[nodiscard]] std::string MakeBasedOn(std::string_view a_source, std::string_view a_version);

	// --- Auto-drafts (DESIGN §6.8): "Draft all homes" never replaces a template someone made ---------------------

	// A template file as the bulk draft sees it before writing.
	struct TemplateFileInfo
	{
		std::filesystem::path path;
		bool                  parsed{ false };     // read and parsed as a layout (one that is not counts as a person's)
		bool                  autoDraft{ false };  // "autoDraft": true
		std::vector<FormRef>  cells;               // its `cell`, then the cells of `cells`
	};

	// nullopt: there is no file at `a_path`.
	[[nodiscard]] std::optional<TemplateFileInfo> ReadTemplateFileInfo(const std::filesystem::path& a_path);
	// Every *.json below `a_dir`, recursively (a missing directory: none), sorted by path.
	[[nodiscard]] std::vector<TemplateFileInfo> ScanTemplateFiles(const std::filesystem::path& a_dir);

	enum class AutoDraftVerdict : std::uint8_t
	{
		New,        // nothing in the way: write it
		Refresh,    // the file at the target is this home's own earlier auto-draft: replace it
		Person,     // a template someone made is at the target, or is in the destination for one of the home's cells
		OtherHome   // the file at the target is another home's auto-draft (two homes with the same name)
	};

	struct AutoDraftCheck
	{
		AutoDraftVerdict      verdict{ AutoDraftVerdict::New };
		std::filesystem::path blocker;  // Person / OtherHome: the file in the way
	};

	// May an auto-draft of the home whose cells are `a_cells` be written to `a_target`? `a_files`: the template files
	// already in the destination (ScanTemplateFiles of its Templates folder). The file at the target is read again
	// from disk (a stale list never lets a write through): it must be missing, or an auto-draft for one of
	// `a_cells`. Every other file of `a_files` that is not an auto-draft must be for none of `a_cells`; files that
	// cannot be parsed only count at the target (in doubt: a person's).
	[[nodiscard]] AutoDraftCheck CheckAutoDraftTarget(std::span<const TemplateFileInfo> a_files, const std::filesystem::path& a_target,
		std::span<const FormRef> a_cells);
}
