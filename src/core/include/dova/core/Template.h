#pragma once

// Template discovery and per-cell layout resolution (DESIGN §6.2).
//
// Decisions:
// - A player layout for the cell always wins (Kind::Player); eligible templates are still ranked and
//   returned as `alternatives` so the UI can offer "re-apply / switch template".
// - A template is a candidate when its `cell` equals the requested cell. It is eligible when every
//   plugin in `requires` is loaded (case-insensitive). The cell's own plugin is implied by the cell
//   match, so it is not checked separately.
// - Ranking: priority (desc), then semantic `version` (desc; unparseable versions rank lowest), then
//   `source` path (case-insensitive asc) as a deterministic tie-break.
// - ApplyTemplate copies the template, sets basedOn = "<source>@<version>" and drops any learned data
//   carried by the template. When applied over an existing player layout, only the category bindings
//   (and master, name, requires) come from the template; the player's data is merged in, never dropped:
//     * containers in both: template categories; ignored = template OR player; explicit forms are
//       united; the player's label is used when the template has none; unknown keys merged;
//     * containers only in the player layout: kept with NO categories (ignored flag, label, forms and
//       unknown keys kept), so learned destinations to them stay valid; a previous master that the
//       template does not use is kept the same way;
//     * displays in both: the template's entry, with the player's ignored flag (OR) and the player's
//       kind/label/items/set where the template gives none; displays only in the player layout: kept;
//     * every learned entry whose container is in the merged layout (and is not the master);
//     * the player's keep block (replaces the template's), unparsed learned entries, unparsed
//       container entries and unknown top-level keys.
//   The plugin backs up the player file before applying (HomeRegistry::ApplyBestTemplate).

#include <cstddef>
#include <filesystem>
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
	// plugins that are not listed in `requires`.
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

	[[nodiscard]] LayoutResolution ResolveLayout(const Layout* a_playerLayout, std::span<const TemplateCandidate> a_templates, const FormRef& a_cell,
		std::span<const std::string> a_loadedPlugins);

	// Plugins from `requires` that are not in `a_loadedPlugins` (case-insensitive).
	[[nodiscard]] std::vector<std::string> MissingRequirements(const Layout& a_layout, std::span<const std::string> a_loadedPlugins);

	// Player layout created from a template. `a_existing` (optional) is the current player layout when re-applying.
	[[nodiscard]] Layout ApplyTemplate(const TemplateCandidate& a_template, const Layout* a_existing = nullptr);

	// Strips player-only data (learned, basedOn) so the layout can be shared as a template.
	[[nodiscard]] Layout ExportAsTemplate(const Layout& a_layout);

	// "<source>@<version>"
	[[nodiscard]] std::string MakeBasedOn(std::string_view a_source, std::string_view a_version);
}
