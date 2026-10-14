#pragma once

// Helpers shared by Theme.cpp, Icons.cpp and Widgets.cpp (not part of the widget contract).

#include <cmath>
#include <cstdint>

#include <imgui.h>

namespace dova::editor::ui::w::detail
{
	inline ImU32 Rgb(std::uint32_t a_hex, int a_alpha = 255)
	{
		return IM_COL32((a_hex >> 16) & 0xFF, (a_hex >> 8) & 0xFF, a_hex & 0xFF, a_alpha);
	}

	inline ImVec4 V4(ImU32 a_col)
	{
		return ImGui::ColorConvertU32ToFloat4(a_col);
	}

	// `a` over `b` by `t` (0: b, 1: a), alpha included.
	inline ImU32 Mix(ImU32 a_a, ImU32 a_b, float a_t)
	{
		const ImVec4 a = V4(a_a), b = V4(a_b);
		return ImGui::ColorConvertFloat4ToU32(ImVec4(b.x + (a.x - b.x) * a_t, b.y + (a.y - b.y) * a_t, b.z + (a.z - b.z) * a_t, b.w + (a.w - b.w) * a_t));
	}

	// Pixel-snaps a stroke centre: odd stroke widths sit on half pixels, even ones on whole pixels.
	inline float Snap(float a_v, float a_stroke)
	{
		const bool odd = (static_cast<int>(std::floor(a_stroke)) & 1) != 0;
		return odd ? std::floor(a_v) + 0.5f : std::round(a_v);
	}

	inline ImVec2 Snap(ImVec2 a_p, float a_stroke)
	{
		return ImVec2(Snap(a_p.x, a_stroke), Snap(a_p.y, a_stroke));
	}

	inline ImVec2 Floor(ImVec2 a_p)
	{
		return ImVec2(std::floor(a_p.x), std::floor(a_p.y));
	}
}
