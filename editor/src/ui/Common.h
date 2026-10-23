#pragma once

// Small helpers shared by the workspace's panels (App, Plan, Drawer, Inspector...): what layer a reference is
// on, its plan colour, its display name.

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

#include "dova/core/Layout.h"
#include "esp/LoadOrder.h"
#include "home/Cell.h"
#include "ui/Widgets.h"

namespace dova::editor::ui::detail
{
	// The Layers popover's groups, one rule for the plan and the 3D view (home::LayerOf).
	using Layer = home::RefLayer;
	using home::LayerOf;

	// What heights are measured from (the tables' Height (m), the Draft panel's "· 1.3 m"), per cell: the lowest
	// container there from the start that has another within 400 units above it (a floor, not one container a mod
	// parked alone), leaving out any more than 2000 units from the median, where a mod parks one it removed (an
	// overhaul's vanilla cupboard far below the rooms). `a_z`: those containers' heights.
	inline constexpr float kUnitsPerMetre = 70.0f;  // Skyrim: about 70 units to the metre (one in 1.43 cm)
	struct Floor
	{
		static constexpr float kOutOfHome = 2000.0f;
		static constexpr float kNeighbour = 400.0f;
		float                  base{ 0.0f };
		float                  median{ 0.0f };
		[[nodiscard]] bool     Outside(float a_z) const { return std::abs(a_z - median) > kOutOfHome; }
		[[nodiscard]] float    Metres(float a_z) const { return (a_z - base) / kUnitsPerMetre; }
	};
	inline Floor FloorOf(std::vector<float> a_z)
	{
		if (a_z.empty()) {
			return {};
		}
		std::ranges::sort(a_z);
		Floor f;
		f.median = a_z[a_z.size() / 2];
		f.base = f.median;
		bool found = false;
		for (std::size_t i = 0; i < a_z.size() && !found; ++i) {
			if (!f.Outside(a_z[i]) && i + 1 < a_z.size() && a_z[i + 1] - a_z[i] <= Floor::kNeighbour) {
				f.base = a_z[i];
				found = true;
			}
		}
		for (std::size_t i = 0; i < a_z.size() && !found; ++i) {
			if (!f.Outside(a_z[i])) {  // no two together: the lowest in the home
				f.base = a_z[i];
				found = true;
			}
		}
		return f;
	}
	// A cell's floor from its containers (not displays), initially disabled ones only when there is nothing else.
	inline Floor FloorOf(const home::CellInfo& a_info)
	{
		std::vector<float> z, all;
		for (const auto& r : a_info.refs) {
			if (r.IsContainer() && !r.display) {
				all.push_back(r.position[2]);
				if (!r.initiallyDisabled) {
					z.push_back(r.position[2]);
				}
			}
		}
		return FloorOf(z.empty() ? std::move(all) : std::move(z));
	}

	// The 3D view's roof cut for an interior (render::RoofGrid; Layers > Roof turns it off): RoofTop is the highest
	// container there from the start + 120 (initially disabled ones may be stored above the rooms; with none enabled,
	// any), 1e9 for none; RoofFloor gives where things stand per area: containers, furniture and doors there from the
	// start, none far out of the home. Each area is cut kRoofAboveFloor over its own floor, so a cave below the
	// built rooms opens from above too. One rule for the window and --render-cell --roof-cut.
	inline constexpr float kRoofAboveFloor = 2.0f * kUnitsPerMetre;
	inline float RoofTop(const home::CellInfo& a_info)
	{
		float top = -1e9f, topAny = -1e9f;
		for (const auto& r : a_info.refs) {
			if (r.IsContainer() && !r.display) {
				topAny = (std::max)(topAny, r.position[2]);
				if (!r.initiallyDisabled) {
					top = (std::max)(top, r.position[2]);
				}
			}
		}
		if (top <= -1e9f) {
			top = topAny;
		}
		return a_info.interior && top > -1e9f ? top + 120.0f : 1e9f;
	}
	inline std::vector<std::array<float, 3>> RoofFloor(const home::CellInfo& a_info)
	{
		const auto                        floor = FloorOf(a_info);
		std::vector<std::array<float, 3>> out;
		for (const auto& r : a_info.refs) {
			if (r.initiallyDisabled || floor.Outside(r.position[2])) {
				continue;
			}
			if ((r.IsContainer() && !r.display) || r.baseType == esp::MakeSig("FURN") || LayerOf(r) == Layer::Door) {
				out.push_back(r.position);
			}
		}
		return out;
	}

	// No instance given or found: what the start card and Setup say (not an error; the console's has the flags).
	inline constexpr const char* kPickSetup = "Pick your MO2 instance (the folder with ModOrganizer.ini) or your Skyrim Data folder, or start the editor from MO2.";

	// One name for a container or display everywhere (the Draft panel, Where items go, the tables, the inspector):
	// the template's label (a landmark: "Barrel (by the grindstone)") first, else the game's name.
	inline std::string RefLabel(const core::Layout& a_layout, const core::FormRef& a_ref, std::string a_gameName)
	{
		if (const auto* b = a_layout.FindContainer(a_ref); b && !b->label.empty()) {
			return b->label;
		}
		if (const auto* d = a_layout.FindDisplay(a_ref); d && !d->label.empty()) {
			return d->label;
		}
		return a_gameName;
	}

	// The plan's colours per layer (the 3D view's outlines use the same).
	inline ImU32 ColorOf(const home::PlacedRef& a_ref, Layer a_layer)
	{
		switch (a_layer) {
		case Layer::Container:
			return a_ref.respawns ? IM_COL32(230, 70, 60, 255) : IM_COL32(90, 200, 110, 255);
		case Layer::Display:
			switch (*a_ref.display) {
			case core::DisplayKind::Rack:
				return IM_COL32(80, 190, 230, 255);
			case core::DisplayKind::Mannequin:
				return IM_COL32(220, 110, 220, 255);
			default:
				return IM_COL32(240, 170, 60, 255);
			}
		case Layer::Door:
			return IM_COL32(240, 220, 80, 255);
		case Layer::Furniture:
			return IM_COL32(140, 150, 220, 200);
		case Layer::Actor:
			return IM_COL32(220, 110, 220, 200);
		case Layer::Structure:
			return IM_COL32(150, 150, 150, 70);
		case Layer::Effect:
			return IM_COL32(110, 170, 210, 80);
		default:
			return IM_COL32(120, 120, 120, 120);
		}
	}

	inline std::string RefTitle(const home::PlacedRef& a_ref)
	{
		if (!a_ref.baseName.empty()) {
			return a_ref.baseName;
		}
		if (!a_ref.baseEditorId.empty()) {
			return a_ref.baseEditorId;
		}
		return a_ref.editorId.empty() ? std::string("(unnamed)") : a_ref.editorId;
	}

	// The game's name for a container or display (the label a new binding gets).
	// The icon of what a reference is: a door, a chest, a rack, a mannequin, a bookshelf, a plaque or case.
	inline w::Icon IconOf(const home::PlacedRef& a_ref)
	{
		if (a_ref.display) {
			switch (*a_ref.display) {
			case core::DisplayKind::Rack:
				return w::Icon::Rack;
			case core::DisplayKind::Mannequin:
				return w::Icon::Mannequin;
			case core::DisplayKind::Bookshelf:
				return w::Icon::Book;
			default:
				return w::Icon::Display;
			}
		}
		if (a_ref.teleportDoor) {
			return w::Icon::Door;
		}
		return a_ref.IsContainer() ? w::Icon::Container : w::Icon::Cube;
	}

	inline std::string GameName(const home::PlacedRef& a_ref) { return a_ref.baseName.empty() ? a_ref.baseEditorId : a_ref.baseName; }

	// What kind of object a container is (the Containers table's Kind): chest, sack, barrel, satchel, bookshelf...
	// From the mesh's file name first (what it looks like), then the base EditorID, then the game's name; "Other".
	inline std::string ContainerKind(const home::PlacedRef& a_ref)
	{
		static constexpr std::pair<const char*, const char*> words[] = { { "bookshel", "Bookshelf" }, { "bookcase", "Bookshelf" }, { "book shelf", "Bookshelf" },
			{ "satchel", "Satchel" }, { "knapsack", "Backpack" }, { "backpack", "Backpack" }, { "strongbox", "Strongbox" }, { "strong box", "Strongbox" },
			{ "barrel", "Barrel" }, { "sack", "Sack" }, { "basket", "Basket" }, { "coffin", "Coffin" }, { "sarcophag", "Coffin" }, { "crate", "Crate" },
			{ "wardrobe", "Wardrobe" }, { "armoire", "Wardrobe" }, { "dresser", "Dresser" }, { "drawer", "Dresser" }, { "cupboard", "Cupboard" },
			{ "cabinet", "Cupboard" }, { "endtable", "End table" }, { "end table", "End table" }, { "nightstand", "End table" }, { "displaycase", "Display case" },
			{ "display case", "Display case" }, { "chest", "Chest" }, { "pouch", "Pouch" }, { "jar", "Jar" }, { "urn", "Urn" } };
		auto lower = [](std::string_view a_text) {
			std::string s(a_text);
			for (auto& c : s) {
				c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
			}
			return s;
		};
		const auto slash = a_ref.model.find_last_of("\\/");
		const std::string sources[] = { lower(slash == std::string::npos ? a_ref.model : a_ref.model.substr(slash + 1)), lower(a_ref.baseEditorId),
			lower(a_ref.baseName) };
		for (const auto& s : sources) {
			for (const auto& [word, kind] : words) {
				const auto at = s.find(word);
				// "urn" only as a word's start (not "furniture", "burnt").
				if (at != std::string::npos && (std::string_view(word) != "urn" || at == 0 || !(s[at - 1] >= 'a' && s[at - 1] <= 'z'))) {
					return kind;
				}
			}
		}
		if (a_ref.display) {
			std::string s(core::DisplayKindName(*a_ref.display));
			s[0] = static_cast<char>(s[0] - 'a' + 'A');
			return s;
		}
		return "Other";
	}

	// Windows file dialogs (App.cpp). nullopt: cancelled.
	[[nodiscard]] std::optional<std::string> PickFolder();
	[[nodiscard]] std::optional<std::string> PickJsonFile();
	[[nodiscard]] std::optional<std::string> PickSaveJsonFile(const std::string& a_defaultName);

	inline std::string Join(const std::vector<std::string>& a_items, const char* a_sep = ", ")
	{
		std::string out;
		for (const auto& i : a_items) {
			if (!out.empty()) {
				out += a_sep;
			}
			out += i;
		}
		return out;
	}
}
