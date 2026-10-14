#pragma once

// Living next to other home mods (DESIGN §9.1). Two decisions, both pure:
//
// Homes that sort themselves (SelfSorting/*.json): some home mods sort and display items with their own
// scripts (Clockwork, Legacy of the Dragonborn, Mirele Bismath...). Two systems moving the same items undo
// each other, so Dova Stash leaves those homes alone. The list is data, so a patch or a player can add one
// without a new DLL. Each entry applies while its `plugin` is loaded:
//   - no `cells` and no `containers`: every cell that comes from `plugin` itself is left alone;
//   - `cells`: those cells are left alone, whichever plugin they come from (a patch that turns a Creation Club
//     home into Legacy storage: the cell is the CC file's, the sorting is the patch's);
//   - `containers`: those containers are never used (a reference, or a base container: every reference of it),
//     the home itself is not left alone (Carpenter Storage Chests sorts only the Hearthfire building-supply
//     chests). With `cells` as well, both apply.
// Files load in file-name order (as Categories/); every entry of every file counts. A malformed entry is an
// error and is skipped; a malformed form reference in it is an error and only that reference is skipped.
//
// Template fit (the auto-enable guard): before a template is enabled on its own (bAutoEnableTemplates), the
// plugin looks at the references it names. A home another mod rebuilt has taken most of them out (disabled or
// deleted) or replaced them; enabling there would bind containers that are gone. Under kTemplateFitPercent of
// them there, the template is offered instead of enabled. Disabled through an enable parent counts as there
// (Hearthfire furniture not built yet, a house upgrade): that is the home's own build system, not another mod.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "dova/core/Diagnostics.h"
#include "dova/core/FormRef.h"
#include "dova/core/Layout.h"

namespace dova::core
{
	inline constexpr std::string_view kSelfSortingSchema = "dovastash-selfsorting-1";

	struct SelfSortingHome
	{
		std::string          plugin;      // the entry applies while this plugin is loaded
		std::string          name;        // the home or mod, for the log and messages (optional)
		std::string          note;        // for people reading the file (optional)
		std::vector<FormRef> cells;       // cells left alone, any plugin's (optional)
		std::vector<FormRef> containers;  // containers never used: references or base containers (optional)
		std::string          source;      // "SelfSorting/00_Default.json"

		// No `cells` and no `containers`: the plugin's own cells are left alone.
		[[nodiscard]] bool WholePlugin() const noexcept { return cells.empty() && containers.empty(); }
	};

	// One file: {"$schema": "dovastash-selfsorting-1", "homes": [...]}. Unusable documents (not JSON, wrong schema,
	// no "homes" array) are an error and yield nothing.
	[[nodiscard]] std::vector<SelfSortingHome> ParseSelfSorting(const nlohmann::json& a_doc, std::string_view a_source, Diagnostics& a_diags);
	[[nodiscard]] std::vector<SelfSortingHome> ParseSelfSortingText(std::string_view a_text, std::string_view a_source, Diagnostics& a_diags);
	// Every *.json directly in `a_dir`, in file-name order; `source` is "SelfSorting/<file>". A missing directory
	// is a warning and yields nothing.
	[[nodiscard]] std::vector<SelfSortingHome> LoadSelfSortingDirectory(const std::filesystem::path& a_dir, Diagnostics& a_diags);

	// The entries that apply in this game: those whose plugin is loaded.
	class SelfSortingHomes
	{
	public:
		SelfSortingHomes() = default;
		SelfSortingHomes(std::vector<SelfSortingHome> a_entries, std::span<const std::string> a_loadedPlugins);

		// The entry that leaves `a_cell` alone, or nullptr.
		[[nodiscard]] const SelfSortingHome* ForCell(const FormRef& a_cell) const;
		// The entry that keeps the container `a_ref` (base container `a_base`, optional) out of use, or nullptr.
		[[nodiscard]] const SelfSortingHome* ForContainer(const FormRef& a_ref, const FormRef* a_base = nullptr) const;

		[[nodiscard]] std::span<const SelfSortingHome> Entries() const noexcept { return _entries; }
		[[nodiscard]] bool                             Empty() const noexcept { return _entries.empty(); }

	private:
		std::vector<SelfSortingHome> _entries;
	};

	// --- Template fit ---------------------------------------------------------------------------------------------

	// What the plugin found for one reference a template names.
	enum class RefPresence : std::uint8_t
	{
		Present,   // there and enabled
		NotBuilt,  // disabled through an enable parent (Hearthfire furniture not built yet, an upgrade): counts as there
		Disabled,  // disabled on its own, or deleted: taken out by another mod
		Missing,   // not found, or not in the home's cells
		Unknown    // cannot be told now (in another cell that is not loaded): not counted
	};

	// Auto-enable needs at least this share (percent) of the counted references there.
	inline constexpr std::size_t kTemplateFitPercent = 60;

	struct TemplateFit
	{
		std::size_t checked{ 0 };  // counted: present + notBuilt + disabled + missing
		std::size_t present{ 0 };
		std::size_t notBuilt{ 0 };
		std::size_t disabled{ 0 };
		std::size_t missing{ 0 };
		std::size_t unknown{ 0 };
		bool        fits{ true };  // nothing counted, or at least kTemplateFitPercent of `checked` there
	};

	// The references a template names, each once: the master, the containers (ignored ones too: they are the home's
	// furniture all the same), then the displays.
	[[nodiscard]] std::vector<FormRef> TemplateFitRefs(const Layout& a_template);
	// Asks `a_probe` about every TemplateFitRefs entry and judges the share that is there.
	[[nodiscard]] TemplateFit JudgeTemplateFit(const Layout& a_template, const std::function<RefPresence(const FormRef&)>& a_probe);
}
