// The icons (docs/EDITOR-UX.md §3.3): drawn with draw-list strokes on a 16-unit grid, 1.5 px at 100 %,
// pixel-snapped so they stay crisp. No icon font, no bitmaps.

#include <algorithm>
#include <cmath>
#include <numbers>

#include <imgui.h>

#include "ui/ThemeDetail.h"
#include "ui/Widgets.h"

namespace dova::editor::ui::w
{
	namespace
	{
		constexpr float kPi = std::numbers::pi_v<float>;

		// Draws on a 16 x 16 grid at `min` with unit `u` (size / 16).
		struct Pen
		{
			ImDrawList* dl;
			ImVec2      min;
			float       u;
			float       th;
			ImU32       col;

			[[nodiscard]] ImVec2 P(float a_x, float a_y) const
			{
				return detail::Snap(ImVec2(min.x + a_x * u, min.y + a_y * u), th);
			}
			[[nodiscard]] ImVec2 Raw(float a_x, float a_y) const  // unsnapped (curves, diagonals' far ends)
			{
				return ImVec2(min.x + a_x * u, min.y + a_y * u);
			}

			// An open or closed polyline through grid points.
			void Poly(std::initializer_list<ImVec2> a_pts, bool a_closed = false) const
			{
				for (const ImVec2& p : a_pts) {
					dl->PathLineTo(P(p.x, p.y));
				}
				dl->PathStroke(col, a_closed ? ImDrawFlags_Closed : ImDrawFlags_None, th);
			}
			void Line(float a_x0, float a_y0, float a_x1, float a_y1) const { Poly({ { a_x0, a_y0 }, { a_x1, a_y1 } }); }
			void Circle(float a_x, float a_y, float a_r) const
			{
				const ImVec2 c = P(a_x, a_y);
				dl->PathArcTo(c, a_r * u, 0.0f, 2.0f * kPi, std::max(16, static_cast<int>(a_r * u * 2.5f)));
				dl->PathStroke(col, ImDrawFlags_Closed, th);
			}
			void Dot(float a_x, float a_y, float a_r) const { dl->AddCircleFilled(Raw(a_x, a_y), std::max(a_r * u, th * 0.6f), col, 12); }
			void Arc(float a_x, float a_y, float a_r, float a_a0, float a_a1) const
			{
				dl->PathArcTo(Raw(a_x, a_y), a_r * u, a_a0, a_a1, std::max(10, static_cast<int>(a_r * u * 2.0f)));
				dl->PathStroke(col, ImDrawFlags_None, th);
			}
			// Rounded rectangle outline.
			void Rect(float a_x0, float a_y0, float a_x1, float a_y1, float a_r = 0.0f) const
			{
				dl->PathRect(P(a_x0, a_y0), P(a_x1, a_y1), a_r * u);
				dl->PathStroke(col, ImDrawFlags_Closed, th);
			}
			// An arrow head at grid point (x, y) pointing along (dx, dy), arms `len` units long.
			void Head(float a_x, float a_y, float a_dx, float a_dy, float a_len) const
			{
				const float n = std::sqrt(a_dx * a_dx + a_dy * a_dy);
				const float dx = a_dx / n, dy = a_dy / n;
				const float c = 0.7071f;  // 45 degree arms
				const ImVec2 tip = Raw(a_x, a_y);
				const ImVec2 l(tip.x + (-dx * c + dy * c) * a_len * u, tip.y + (-dy * c - dx * c) * a_len * u);
				const ImVec2 r(tip.x + (-dx * c - dy * c) * a_len * u, tip.y + (-dy * c + dx * c) * a_len * u);
				dl->PathLineTo(l);
				dl->PathLineTo(tip);
				dl->PathLineTo(r);
				dl->PathStroke(col, ImDrawFlags_None, th);
			}
		};

		void Gear(const Pen& a_p)
		{
			const ImVec2 c = a_p.Raw(8, 8);
			constexpr int teeth = 8;
			const float   ro = 6.6f * a_p.u, ri = 4.9f * a_p.u;
			for (int i = 0; i < teeth; ++i) {
				const float a = static_cast<float>(i) * 2.0f * kPi / teeth;
				const float w = 0.22f;  // half the tooth's angle at the tip
				const float b = 0.36f;  // half the tooth's angle at the root
				const float pts[4][2] = { { a - b, ri }, { a - w, ro }, { a + w, ro }, { a + b, ri } };
				for (const auto& [ang, r] : pts) {
					a_p.dl->PathLineTo(ImVec2(c.x + std::cos(ang) * r, c.y + std::sin(ang) * r));
				}
			}
			a_p.dl->PathStroke(a_p.col, ImDrawFlags_Closed, a_p.th);
			a_p.Circle(8, 8, 2.1f);
		}
	}

	void DrawIcon(ImDrawList* a_list, Icon a_icon, ImVec2 a_min, float a_size, ImU32 a_colour)
	{
		const Theme& t = Th();
		// Strokes keep their optical weight: 1.5 px at 16 px and 100 %, growing gently with the icon.
		const float rel = a_size / (16.0f * t.scale);
		const float th = std::max(1.0f, 1.5f * t.scale * std::clamp(rel, 0.75f, 1.5f));
		const Pen   p{ a_list, detail::Floor(a_min), a_size / 16.0f, th, a_colour };

		switch (a_icon) {
		case Icon::Menu:
			p.Line(2.5f, 4, 13.5f, 4);
			p.Line(2.5f, 8, 13.5f, 8);
			p.Line(2.5f, 12, 13.5f, 12);
			break;
		case Icon::Search:
			p.Circle(7, 7, 4.6f);
			p.Poly({ { 10.4f, 10.4f }, { 14, 14 } });
			break;
		case Icon::Filter:
			p.Poly({ { 2, 3 }, { 14, 3 }, { 9.5f, 8.5f }, { 9.5f, 13.5f }, { 6.5f, 12 }, { 6.5f, 8.5f } }, true);
			break;
		case Icon::Layers:
			p.Poly({ { 8, 2 }, { 14, 5 }, { 8, 8 }, { 2, 5 } }, true);
			p.Poly({ { 2, 8 }, { 8, 11 }, { 14, 8 } });
			p.Poly({ { 2, 11 }, { 8, 14 }, { 14, 11 } });
			break;
		case Icon::Palette:
			p.dl->PathArcTo(p.Raw(8, 8), 6.2f * p.u, 0.15f * kPi, 1.85f * kPi + 0.15f * kPi, 28);
			p.dl->PathLineTo(p.Raw(11.2f, 10.2f));
			p.dl->PathStroke(p.col, ImDrawFlags_Closed, p.th);
			p.Dot(5.2f, 7.6f, 1.05f);
			p.Dot(7.4f, 4.8f, 1.05f);
			p.Dot(10.7f, 5.4f, 1.05f);
			p.Dot(6.4f, 11.0f, 1.05f);
			break;
		case Icon::Camera:
			p.Rect(1.5f, 5, 14.5f, 13.5f, 1.5f);
			p.Poly({ { 5, 5 }, { 6.5f, 2.5f }, { 9.5f, 2.5f }, { 11, 5 } });
			p.Circle(8, 9.2f, 2.6f);
			break;
		case Icon::Settings:
			Gear(p);
			break;
		case Icon::ChevronLeft:
			p.Poly({ { 10, 3.5f }, { 5.5f, 8 }, { 10, 12.5f } });
			break;
		case Icon::ChevronRight:
			p.Poly({ { 6, 3.5f }, { 10.5f, 8 }, { 6, 12.5f } });
			break;
		case Icon::ChevronDown:
			p.Poly({ { 3.5f, 6 }, { 8, 10.5f }, { 12.5f, 6 } });
			break;
		case Icon::ChevronUp:
			p.Poly({ { 3.5f, 10 }, { 8, 5.5f }, { 12.5f, 10 } });
			break;
		case Icon::Check:
			p.Poly({ { 2.5f, 8.5f }, { 6.2f, 12 }, { 13.5f, 4.5f } });
			break;
		case Icon::Warning:
			p.Poly({ { 8, 2 }, { 14.5f, 13.5f }, { 1.5f, 13.5f } }, true);
			p.Line(8, 6.2f, 8, 9.6f);
			p.Dot(8, 11.6f, 0.85f);
			break;
		case Icon::Error:
			p.Circle(8, 8, 6.2f);
			p.Line(5.6f, 5.6f, 10.4f, 10.4f);
			p.Line(5.6f, 10.4f, 10.4f, 5.6f);
			break;
		case Icon::Info:
			p.Circle(8, 8, 6.2f);
			p.Line(8, 7.2f, 8, 11.4f);
			p.Dot(8, 4.9f, 0.85f);
			break;
		case Icon::Undo:
		case Icon::Redo:
		{
			const bool  undo = a_icon == Icon::Undo;
			const auto  X = [&](float a_x) { return undo ? a_x : 16.0f - a_x; };
			p.dl->PathLineTo(p.P(X(3), 6.5f));
			p.dl->PathLineTo(p.P(X(9.5f), 6.5f));
			if (undo) {
				p.dl->PathArcTo(p.Raw(9.5f, 10), 3.5f * p.u, -kPi / 2, kPi / 2, 14);
			} else {
				p.dl->PathArcTo(p.Raw(6.5f, 10), 3.5f * p.u, -kPi / 2, -3.0f * kPi / 2, 14);
			}
			p.dl->PathLineTo(p.P(X(5.5f), 13.5f));
			p.dl->PathStroke(p.col, ImDrawFlags_None, p.th);
			p.Head(X(3), 6.5f, undo ? -1.0f : 1.0f, 0.0f, 3.6f);
			break;
		}
		case Icon::Save:
			p.Poly({ { 2.5f, 2.5f }, { 11, 2.5f }, { 13.5f, 5 }, { 13.5f, 13.5f }, { 2.5f, 13.5f } }, true);
			p.Poly({ { 5, 2.5f }, { 5, 6 }, { 10.5f, 6 }, { 10.5f, 2.5f } });
			p.Poly({ { 5, 13.5f }, { 5, 9.5f }, { 11, 9.5f }, { 11, 13.5f } });
			break;
		case Icon::Close:
			p.Line(4, 4, 12, 12);
			p.Line(4, 12, 12, 4);
			break;
		case Icon::Pin:
			p.dl->PathArcTo(p.Raw(8, 6.5f), 4.6f * p.u, kPi * 0.82f, kPi * 2.18f, 20);
			p.dl->PathLineTo(p.Raw(8, 14.5f));
			p.dl->PathStroke(p.col, ImDrawFlags_Closed, p.th);
			p.Circle(8, 6.5f, 1.6f);
			break;
		case Icon::Copy:
			p.Poly({ { 2.5f, 10.5f }, { 2.5f, 2.5f }, { 10.5f, 2.5f } });
			p.Rect(5.5f, 5.5f, 13.5f, 13.5f, 1.0f);
			break;
		case Icon::Home:
			p.Poly({ { 1.5f, 8 }, { 8, 2 }, { 14.5f, 8 } });
			p.Poly({ { 3.5f, 6.5f }, { 3.5f, 14 }, { 12.5f, 14 }, { 12.5f, 6.5f } });
			p.Poly({ { 6.5f, 14 }, { 6.5f, 10 }, { 9.5f, 10 }, { 9.5f, 14 } });
			break;
		case Icon::Cube:
			p.Poly({ { 8, 1.5f }, { 14, 4.8f }, { 14, 11.2f }, { 8, 14.5f }, { 2, 11.2f }, { 2, 4.8f } }, true);
			p.Poly({ { 2, 4.8f }, { 8, 8.1f }, { 14, 4.8f } });
			p.Line(8, 8.1f, 8, 14.5f);
			break;
		case Icon::Plan:
			p.Rect(2, 2, 14, 14);
			p.Line(9, 2, 9, 6.5f);
			p.Line(9, 9.5f, 9, 14);
			p.Line(2, 8.5f, 6, 8.5f);
			break;
		case Icon::Draft:
			// A quill: the vane rounded at the top right, the shaft from it down to the nib at the bottom left, one barb.
			p.dl->PathLineTo(p.Raw(4.0f, 12.0f));
			p.dl->PathLineTo(p.Raw(4.0f, 7.6f));
			p.dl->PathLineTo(p.Raw(8.4f, 3.2f));
			p.dl->PathBezierQuadraticCurveTo(p.Raw(13.8f, 2.2f), p.Raw(12.8f, 7.6f), 8);
			p.dl->PathLineTo(p.Raw(8.4f, 12.0f));
			p.dl->PathStroke(p.col, ImDrawFlags_Closed, p.th);
			p.Poly({ { 10.6f, 5.4f }, { 1.8f, 14.2f } });
			p.Line(10.4f, 10.0f, 6.0f, 10.0f);
			break;
		case Icon::Plus:
			p.Line(8, 3, 8, 13);
			p.Line(3, 8, 13, 8);
			break;
		case Icon::Minus:
			p.Line(3, 8, 13, 8);
			break;
		case Icon::Emblem:
		{
			// The logo (presskit/logo): a clipped-corner frame around a house with crossed gable beams and a
			// three-bay body. Thinner strokes keep it legible at 16 px.
			Pen e = p;
			e.th = std::max(1.0f, p.th * 0.75f);
			e.Poly({ { 4, 0.75f }, { 12, 0.75f }, { 15.25f, 4 }, { 15.25f, 12 }, { 12, 15.25f }, { 4, 15.25f }, { 0.75f, 12 }, { 0.75f, 4 } }, true);
			e.Line(3.6f, 8.2f, 9.8f, 3.0f);
			e.Line(12.4f, 8.2f, 6.2f, 3.0f);
			e.Poly({ { 4.6f, 8.2f }, { 4.6f, 12.4f }, { 11.4f, 12.4f }, { 11.4f, 8.2f } }, true);
			if (a_size >= 20.0f * t.scale) {
				e.Line(6.9f, 8.2f, 6.9f, 12.4f);
				e.Line(9.1f, 8.2f, 9.1f, 12.4f);
			} else {
				e.Line(8, 8.2f, 8, 12.4f);
			}
			break;
		}
		case Icon::Door:
			p.Poly({ { 4, 14.5f }, { 4, 1.5f }, { 12, 1.5f }, { 12, 14.5f } });
			p.Line(2, 14.5f, 14, 14.5f);
			p.Dot(9.8f, 8.6f, 0.9f);
			break;
		case Icon::Container:
			p.Poly({ { 2, 7 }, { 2, 4.5f }, { 4, 2.5f }, { 12, 2.5f }, { 14, 4.5f }, { 14, 7 } });
			p.Rect(2, 7, 14, 13.5f);
			p.Rect(6.8f, 5.8f, 9.2f, 9.2f);
			break;
		case Icon::Display:
			p.Rect(2.5f, 2, 13.5f, 10.5f, 0.5f);
			p.Poly({ { 8, 4 }, { 10.2f, 6.25f }, { 8, 8.5f }, { 5.8f, 6.25f } }, true);
			p.Line(8, 10.5f, 8, 13.5f);
			p.Line(4.5f, 13.5f, 11.5f, 13.5f);
			break;
		case Icon::Mannequin:
			p.Circle(8, 3, 1.8f);
			p.Poly({ { 4, 6.5f }, { 12, 6.5f }, { 10.5f, 11 }, { 5.5f, 11 } }, true);
			p.Line(8, 11, 8, 14.5f);
			p.Line(5, 14.5f, 11, 14.5f);
			break;
		case Icon::Rack:
			// Two crossed swords (what a weapon rack holds).
			for (const float m : { 0.0f, 1.0f }) {
				const auto X = [m](float a_x) { return m > 0.5f ? 16.0f - a_x : a_x; };
				p.dl->PathLineTo(p.Raw(X(5.6f), 10.4f));
				p.dl->PathLineTo(p.Raw(X(13.5f), 2.5f));
				p.dl->PathStroke(p.col, ImDrawFlags_None, p.th);
				p.dl->PathLineTo(p.Raw(X(3.6f), 8.4f));
				p.dl->PathLineTo(p.Raw(X(7.6f), 12.4f));
				p.dl->PathStroke(p.col, ImDrawFlags_None, p.th);
				p.dl->PathLineTo(p.Raw(X(5.6f), 10.4f));
				p.dl->PathLineTo(p.Raw(X(2.6f), 13.4f));
				p.dl->PathStroke(p.col, ImDrawFlags_None, p.th);
			}
			break;
		case Icon::Book:
			p.Poly({ { 8, 4.5f }, { 5, 3 }, { 1.5f, 3 }, { 1.5f, 12.5f }, { 5, 12.5f }, { 8, 14 } });
			p.Poly({ { 8, 4.5f }, { 11, 3 }, { 14.5f, 3 }, { 14.5f, 12.5f }, { 11, 12.5f }, { 8, 14 } });
			p.Line(8, 4.5f, 8, 14);
			break;
		case Icon::Sort:
			p.Line(5, 13.5f, 5, 2.5f);
			p.Head(5, 2.5f, 0, -1, 3.2f);
			p.Line(11, 2.5f, 11, 13.5f);
			p.Head(11, 13.5f, 0, 1, 3.2f);
			break;
		case Icon::Expand:
			p.Poly({ { 4, 6 }, { 8, 2 }, { 12, 6 } });
			p.Poly({ { 4, 10 }, { 8, 14 }, { 12, 10 } });
			break;
		case Icon::Collapse:
			p.Poly({ { 4, 2 }, { 8, 6 }, { 12, 2 } });
			p.Poly({ { 4, 14 }, { 8, 10 }, { 12, 14 } });
			break;
		case Icon::ExternalLink:
			p.Poly({ { 7, 3 }, { 2.5f, 3 }, { 2.5f, 13.5f }, { 13, 13.5f }, { 13, 9 } });
			p.Line(7.5f, 8.5f, 13.5f, 2.5f);
			p.Poly({ { 9, 2.5f }, { 13.5f, 2.5f }, { 13.5f, 7 } });
			break;
		case Icon::Folder:
			p.Poly({ { 1.5f, 3 }, { 6, 3 }, { 7.5f, 5 }, { 14.5f, 5 }, { 14.5f, 13.5f }, { 1.5f, 13.5f } }, true);
			break;
		case Icon::Refresh:
		{
			const float a0 = -kPi * 0.30f, a1 = kPi * 1.35f;
			p.Arc(8, 8, 5.6f, a0, a1);
			// head at the start, pointing against the sweep (screen y is down: decreasing angle)
			p.Head(8 + std::cos(a0) * 5.6f, 8 + std::sin(a0) * 5.6f, std::sin(a0), -std::cos(a0), 3.2f);
			break;
		}
		case Icon::Brush:
			p.Poly({ { 14, 2 }, { 7.5f, 9.5f } });
			p.Poly({ { 7.2f, 8.2f }, { 8.8f, 9.8f } });
			p.dl->PathLineTo(p.Raw(6.4f, 9.6f));
			p.dl->PathBezierQuadraticCurveTo(p.Raw(3.6f, 9.8f), p.Raw(3.4f, 13.6f), 6);
			p.dl->PathLineTo(p.Raw(2.0f, 14.0f));
			p.dl->PathBezierQuadraticCurveTo(p.Raw(6.4f, 14.6f), p.Raw(6.4f, 9.6f), 6);
			p.dl->PathStroke(p.col, ImDrawFlags_None, p.th);
			break;
		case Icon::Cursor:
			p.Poly({ { 3.5f, 2 }, { 3.5f, 13 }, { 6.4f, 10.4f }, { 8.4f, 14.4f }, { 10.2f, 13.6f }, { 8.3f, 9.6f }, { 12, 9.4f } }, true);
			break;
		case Icon::Trash:
			p.Line(2, 4, 14, 4);
			p.Poly({ { 6, 4 }, { 6, 2 }, { 10, 2 }, { 10, 4 } });
			p.Poly({ { 3.5f, 4 }, { 4.5f, 14 }, { 11.5f, 14 }, { 12.5f, 4 } });
			p.Line(6.8f, 7, 6.8f, 11);
			p.Line(9.2f, 7, 9.2f, 11);
			break;
		case Icon::Eye:
			p.dl->PathLineTo(p.Raw(1.5f, 8));
			p.dl->PathBezierQuadraticCurveTo(p.Raw(8, 0.8f), p.Raw(14.5f, 8), 14);
			p.dl->PathBezierQuadraticCurveTo(p.Raw(8, 15.2f), p.Raw(1.5f, 8), 14);
			p.dl->PathStroke(p.col, ImDrawFlags_Closed, p.th);
			p.Circle(8, 8, 2.2f);
			break;
		case Icon::More:
			p.Dot(3.5f, 8, 1.15f);
			p.Dot(8, 8, 1.15f);
			p.Dot(12.5f, 8, 1.15f);
			break;
		default:
			break;
		}
	}
}
