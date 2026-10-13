#pragma once

// What the editor knows about a cell (docs/EDITOR.md §3): its header facts (owner, location keywords,
// encounter zone: the same facts Dova Stash's eligibility checks use), every placed reference with its
// position, rotation and object bounds (the floor plan), the containers and displays Dova Stash can use
// (DESIGN §8.4 detection, as tools/esm_dump.py does it), and the doors leading to other cells.
// Also the home finder (scores the cells of a plugin) and a draft template built with dova::core.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "dova/core/Category.h"
#include "dova/core/DisplayKind.h"
#include "dova/core/DraftSuggest.h"
#include "dova/core/Layout.h"
#include "esp/LoadOrder.h"

namespace dova::editor::home
{
	struct PlacedRef
	{
		esp::FormKey key;
		esp::Sig     recordType{ 0 };  // REFR / ACHR
		esp::FormKey base;
		esp::Sig     baseType{ 0 };    // 0: base not found in the loaded plugins
		std::string  editorId;
		std::string  baseEditorId;
		std::string  baseName;
		std::string  model;  // base record's model (MODL; armor: its world model), as written: "clutter\chest01.nif"

		std::array<float, 3>        position{};
		std::array<float, 3>        rotation{};  // radians, as stored
		float                       scale{ 1.0f };
		std::array<std::int16_t, 6> bounds{};    // base OBND: x1 y1 z1 x2 y2 z2 (unscaled, local)
		bool                        hasBounds{ false };

		bool persistent{ false };
		bool initiallyDisabled{ false };
		bool deleted{ false };
		bool respawns{ false };  // CONT with the respawn flag: never bind it

		std::optional<esp::FormKey> enableParent;
		bool                        enableOpposite{ false };
		std::optional<esp::FormKey> teleportDoor;  // XTEL: the door this door leads to
		std::optional<std::array<float, 3>> teleportPosition;  // XTEL: where the player arrives on the other side
		std::optional<esp::FormKey> opens;         // ACTI front: the container it opens (XLKR without keyword)
		std::vector<std::string>    scripts;       // base + reference scripts
		std::vector<std::string>    keywords;      // base record's keyword EditorIDs (furniture, containers, items, activators)
		bool                        ownedByOther{ false };  // XOWN on the reference: an NPC or a faction other than the player's

		std::optional<core::DisplayKind> display;
		std::optional<core::RackType>    rackType;
		std::vector<esp::FormKey>        displayItems;  // item plaques / cases: what the script is made for

		[[nodiscard]] bool IsContainer() const noexcept;
	};

	struct DoorLink
	{
		esp::FormKey                door;
		esp::FormKey                destination;  // the door on the other side
		std::optional<esp::FormKey> destinationCell;
		std::string                 destinationLabel;  // "WhiterunBreezehome", "Tamriel (5, -3)"...
		bool                        destinationInterior{ false };
	};

	// Exterior terrain (LAND): 33 x 33 points 128 units apart over the cell's 4096-unit square, south-west first.
	struct CellLand
	{
		std::array<float, 33 * 33>          heights{};  // absolute Z
		std::vector<std::array<float, 3>>   normals;    // 33 * 33 or empty
		std::vector<std::uint32_t>          colors;     // 33 * 33 RGBA8 or empty
		std::array<std::string, 4>          textures;   // base texture per quadrant (SW, SE, NW, NE): diffuse path, empty: default
	};

	struct CellInfo
	{
		esp::FormKey                key;
		std::string                 editorId;
		std::string                 name;
		std::string                 lastOverride;  // plugin with the winning cell record
		bool                        interior{ false };
		std::optional<esp::FormKey> world;
		std::optional<std::array<std::int32_t, 2>> grid;  // exterior XCLC

		std::optional<esp::FormKey> owner;
		std::string                 ownerLabel;
		bool                        ownedByPlayer{ false };
		std::optional<esp::FormKey> location;
		std::string                 locationLabel;
		std::vector<std::string>    locationKeywords;
		std::string                 encounterZone;
		bool                        neverResets{ false };

		std::optional<CellLand> land;  // exterior cells with a LAND record

		std::vector<PlacedRef> refs;   // deleted references left out
		std::vector<DoorLink>  doors;  // filled when LoadCellOptions::resolveDoors
	};

	struct LoadCellOptions
	{
		bool resolveDoors{ true };  // find the cells behind teleport doors (walks every loaded plugin's cells)
	};

	[[nodiscard]] std::optional<CellInfo> LoadCell(esp::LoadOrder& a_lo, const esp::FormKey& a_cell, const LoadCellOptions& a_options = {});

	// The exterior cells around an exterior cell (`a_radius` 1: the 8 neighbours), loaded without door
	// links: the surroundings the 3D view draws around a home's exterior.
	[[nodiscard]] std::vector<CellInfo> LoadNeighbourCells(esp::LoadOrder& a_lo, const CellInfo& a_cell, int a_radius = 1);

	// A cell of a plugin that may be a player home, with the evidence.
	struct HomeCandidate
	{
		esp::FormKey             cell;
		std::string              editorId;
		std::string              name;
		bool                     interior{ false };
		bool                     overridesOther{ false };  // the plugin edits a cell defined elsewhere
		int                      references{ 0 };
		int                      containers{ 0 };
		int                      respawning{ 0 };
		int                      racks{ 0 };
		int                      mannequins{ 0 };
		int                      plaques{ 0 };  // item plaques + cases
		bool                     playerHouseKeyword{ false };
		bool                     ownedByPlayer{ false };
		bool                     neverResets{ false };
		int                      score{ 0 };
		std::vector<std::string> reasons;

		// A home signal is required: dungeons with a never-resetting zone, containers and racks score too.
		// Mannequins in a cell where nothing respawns catch homes owned by a faction (Severin Manor).
		[[nodiscard]] bool Likely() const noexcept { return playerHouseKeyword || ownedByPlayer || (mannequins > 0 && respawning == 0); }
	};

	// Every interior cell of the plugin (exteriors too with `a_exteriors`) that has containers or home
	// evidence, best first. Heuristics as in docs/EDITOR.md §3.2.
	[[nodiscard]] std::vector<HomeCandidate> FindHomes(esp::LoadOrder& a_lo, std::uint32_t a_plugin, bool a_exteriors);
	// The evidence and score for one loaded cell (FindHomes' rules).
	[[nodiscard]] HomeCandidate ScoreHome(const CellInfo& a_cell, bool a_overridesOther);

	// A starting template for the cell (home/Drafts.cpp): every container listed with its game name as label,
	// respawning, scripted, owned and upgrade-removed containers ignored with a note, the master and categories
	// suggested by core::SuggestDraft (names, stations, loose items; `a_categories` classifies the loose items and
	// drops patterns it does not know), the displays listed, `requires` filled. Validation goes through
	// core::ValidateTemplate (diagnostics in `a_diags`).
	[[nodiscard]] core::Layout DraftTemplate(esp::LoadOrder& a_lo, const CellInfo& a_cell, core::Diagnostics& a_diags,
		const core::CategorySet* a_categories = nullptr, const core::DraftRules* a_rules = nullptr);

	[[nodiscard]] std::string DisplayLabel(const PlacedRef& a_ref);  // "rack (Any)", "mannequin", "item (3)"
}
