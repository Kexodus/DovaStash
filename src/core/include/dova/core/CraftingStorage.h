#pragma once

// Materials at the workbench, "Craft from storage" (DESIGN §8.5): pure logic. While a crafting menu is open
// at a station of a home that is set up, the menu counts the home's stored materials as if the player carried
// them, and crafting takes what the player lacks from the containers. Nothing moves when the menu opens or
// closes. The plugin hooks the menu's inventory queries and the player's RemoveItem (Hooks/CraftingHooks);
// this header decides:
// - which mode a session uses (ResolveMode, ModeFor): Off, Move (bring and put back, Crafting.h), or Storage;
// - which other crafting-from-storage mods make the default Off (KnownCraftingStorageMods, DetectOtherMods);
// - what the containers can supply, per material, and in which order (MakeSupply): the same stacks Move
//   mode would bring (Crafting.h Judge: tempered, enchanted, renamed, favorited, worn and quest copies never),
//   the container the item is bound to first (where Sort all would put it), then the others in layout order;
// - which stacks cover a shortfall (PlanTake): partial stacks, plain copies before keyed ones (stolen, souls).

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dova/core/Crafting.h"
#include "dova/core/FormRef.h"
#include "dova/core/Item.h"

namespace dova::core::crafting
{
	// How a station session supplies materials.
	enum class SupplyMode : std::uint8_t
	{
		Off,     // nothing: the player's own inventory only
		Move,    // bring the materials when sitting down, put the rest back when getting up (Crafting.h)
		Storage  // the menu counts the containers; crafting takes the shortfall from them
	};
	[[nodiscard]] std::string_view SupplyModeName(SupplyMode a_mode) noexcept;

	// [Sorting] iCraftingSupply. Auto is the default: Storage when the hooks are in, Move when they are not,
	// Off when another crafting-from-storage mod is loaded. Any other value is the player's own choice.
	enum class SupplySetting : std::int32_t
	{
		Auto = 0,
		Off = 1,
		Move = 2,
		Storage = 3
	};
	[[nodiscard]] SupplySetting ToSupplySetting(std::int32_t a_value) noexcept;  // out of range: Auto

	struct ModeResolution
	{
		SupplyMode  mode{ SupplyMode::Off };
		bool        chosen{ false };  // the player's explicit setting decided it
		std::string why;              // for the log and the settings page; "" when nothing needs saying
	};

	// `a_hooksReady`: the Storage hooks are installed on this runtime. `a_otherMods`: display names of the
	// crafting-from-storage mods that are loaded (DetectOtherMods).
	[[nodiscard]] ModeResolution ResolveMode(SupplySetting a_setting, bool a_hooksReady, std::span<const std::string> a_otherMods);

	// The mode one station uses. Storage covers recipe stations (forge, smelter, tanning rack, cooking pot,
	// sharpening wheel, armor table, modded benches) and alchemy labs. Enchanters use Move: the enchanting
	// menu reads a soul gem's soul from the copy's own extra data and hands that exact copy back when it
	// consumes it, so the gems come to the player instead (a handful of items, no lag).
	[[nodiscard]] SupplyMode ModeFor(SupplyMode a_mode, Station a_station) noexcept;

	// Another mod that lets crafting use stored items. Either an SKSE DLL or a plugin file.
	struct KnownMod
	{
		std::string_view file;  // "CraftingPullFromContainer.dll", "LinkedCraftingStorage.esp"
		std::string_view name;  // display name, for the log and the settings page
	};
	[[nodiscard]] std::span<const KnownMod> KnownCraftingStorageMods() noexcept;

	// Display names of the known mods whose file is among `a_loadedFiles` (DLL file names and plugin names
	// together; case-insensitive), in KnownCraftingStorageMods order, each once.
	[[nodiscard]] std::vector<std::string> DetectOtherMods(std::span<const std::string> a_loadedFiles);

	// --- supply ------------------------------------------------------------------------------------------

	// One material's supply in the home: how many copies the containers can give and where they are, in the
	// order they are taken.
	struct FormSupply
	{
		std::int32_t         count{ 0 };
		std::vector<FormRef> containers;  // each once; the bound container first, then layout order
	};
	using Supply = std::map<FormRef, FormSupply>;

	// The container `a_item` is bound to (where sorting puts it), if any.
	using BoundContainer = std::function<std::optional<FormRef>(const ItemInfo& a_item)>;

	// What `a_stored` (master and containers, in layout order) can supply for `a_request`: every stack Judge
	// would bring (Verdict::Bring), counted per form.
	[[nodiscard]] Supply MakeSupply(const SupplyRequest& a_request, std::span<const Stored> a_stored, const BoundContainer& a_bound = {});

	// The container stacks that cover `a_need` copies of `a_form`, taken from `a_stored` (fresh) in `a_order`
	// (FormSupply::containers; containers missing from it come last, in `a_stored` order): within a container
	// the plain copies first, then keyed copies (stolen, filled souls), partial stacks allowed. Only stacks
	// Judge brings. The moves go container -> player; their counts add up to at most `a_need`.
	[[nodiscard]] std::vector<Pull> PlanTake(const SupplyRequest& a_request, std::span<const Stored> a_stored, const FormRef& a_form,
		std::span<const FormRef> a_order, std::int32_t a_need);

	// What the player lacks: `a_need` copies requested, `a_playerHas` copies carried.
	[[nodiscard]] constexpr std::int32_t Shortfall(std::int32_t a_need, std::int32_t a_playerHas) noexcept
	{
		return a_need > a_playerHas ? a_need - (a_playerHas > 0 ? a_playerHas : 0) : 0;
	}

	// After a craft took copies that were moved in for it: how many of them to put back (the craft used fewer
	// than it asked for, or none). `a_before` and `a_after`: the player's copies around the craft; `a_moved`:
	// copies brought from the containers for it; `a_need`: what the craft asked for. Never more than `a_moved`.
	[[nodiscard]] constexpr std::int32_t Unused(std::int32_t a_before, std::int32_t a_moved, std::int32_t a_need, std::int32_t a_after) noexcept
	{
		const auto expected = a_before + a_moved - a_need;
		const auto extra = a_after - (expected > 0 ? expected : 0);
		return extra <= 0 ? 0 : extra < a_moved ? extra : a_moved;
	}
}
