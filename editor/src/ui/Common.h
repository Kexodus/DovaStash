#pragma once

// Small helpers shared by the workspace's panels (App, Plan, Drawer, Inspector...): what layer a reference is
// on, its plan colour, its display name.

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

#include "esp/LoadOrder.h"
#include "home/Cell.h"
#include "ui/Widgets.h"

namespace dova::editor::ui::detail
{
	// The Layers popover's groups, one rule for the plan and the 3D view (home::LayerOf).
	using Layer = home::RefLayer;
	using home::LayerOf;

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
