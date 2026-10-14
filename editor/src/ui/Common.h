#pragma once

// Small helpers shared by the workspace's panels (App, Plan, Drawer, Inspector...): what layer a reference is
// on, its plan colour, its display name.

#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "esp/LoadOrder.h"
#include "home/Cell.h"

namespace dova::editor::ui::detail
{
	enum class Layer
	{
		Static,
		Container,
		Display,
		Door,
		Furniture,
		Other
	};

	inline Layer LayerOf(const home::PlacedRef& a_ref)
	{
		using esp::MakeSig;
		if (a_ref.display) {
			return Layer::Display;
		}
		if (a_ref.IsContainer()) {
			return Layer::Container;
		}
		switch (a_ref.baseType) {
		case MakeSig("DOOR"):
			return Layer::Door;
		case MakeSig("FURN"):
		case MakeSig("ACTI"):
			return Layer::Furniture;
		case MakeSig("STAT"):
		case MakeSig("MSTT"):
			return a_ref.hasBounds && (a_ref.bounds[3] - a_ref.bounds[0]) > 0 ? Layer::Static : Layer::Other;  // markers have no bounds
		default:
			return Layer::Other;
		}
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
		case Layer::Static:
			return IM_COL32(150, 150, 150, 70);
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
	inline std::string GameName(const home::PlacedRef& a_ref) { return a_ref.baseName.empty() ? a_ref.baseEditorId : a_ref.baseName; }

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
