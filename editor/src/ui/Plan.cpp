// The floor plan (docs/EDITOR.md §2 step 5, EDITOR-UX.md §2.3): the open cell seen from above, footprints from
// the base records' bounds turned by each reference's rotation, layers and the height cut from the Layers
// popover. Select mode: a click selects (containers first, then displays, doors, furniture, people, structure),
// a drag boxes containers and displays (Ctrl adds); the right or middle button pans, the wheel zooms.

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

#include <imgui.h>

#include "Util.h"
#include "ui/App.h"
#include "ui/Common.h"

namespace dova::editor::ui
{
	using namespace detail;

	namespace
	{
		// Which footprint wins under the cursor: what a template uses first, volumes last.
		int PickPriority(Layer a_layer)
		{
			switch (a_layer) {
			case Layer::Container:
				return 7;
			case Layer::Display:
				return 6;
			case Layer::Door:
				return 5;
			case Layer::Furniture:
				return 4;
			case Layer::Actor:
				return 3;
			case Layer::Structure:
				return 2;
			case Layer::Effect:
				return 1;
			default:
				return 0;
			}
		}

		// The footprint's longest side in units (0: no bounds).
		float FootprintSize(const home::PlacedRef& a_ref)
		{
			if (!a_ref.hasBounds) {
				return 0.0f;
			}
			const float w = static_cast<float>(a_ref.bounds[3] - a_ref.bounds[0]);
			const float h = static_cast<float>(a_ref.bounds[4] - a_ref.bounds[1]);
			return std::max(w, h) * std::abs(a_ref.scale);
		}
	}

	bool EditorApp::PlanShows(Layer a_layer) const
	{
		switch (a_layer) {
		case Layer::Structure:
			return _view.showStructure;
		case Layer::Container:
			return _view.showContainers;
		case Layer::Display:
			return _view.showDisplays;
		case Layer::Door:
			return _view.showDoors;
		case Layer::Furniture:
			return _view.showFurniture;
		case Layer::Actor:
			return _view.showActors;
		case Layer::Effect:
			return _planEffects;
		default:
			return _showMarkers;
		}
	}

	unsigned EditorApp::PlanLayers() const
	{
		unsigned mask = _view.showDisabled ? 1u << 8 : 0u;
		for (unsigned l = 0; l <= static_cast<unsigned>(Layer::Other); ++l) {
			if (PlanShows(static_cast<Layer>(l))) {
				mask |= 1u << l;
			}
		}
		return mask;
	}

	void EditorApp::FitPlan(const OpenCell& a_cell, ImVec2 a_size)
	{
		// The bounds of what is shown, without the references far from the rest (a marker or a prop left thousands
		// of units away would shrink the home to a dot): only the largest group of references 1024 units apart or
		// closer (the 3D overview's rule, LargestGroup), then 2 % trimmed on each side. Effects and the "Other"
		// volumes never count (a fog bank or a trigger can be larger than the home), nor a footprint larger than
		// 40 % of the home (an exterior backdrop, a room-sized volume).
		_planFitLayers = PlanLayers();
		std::vector<const home::PlacedRef*> fit;
		for (const auto& r : a_cell.info.refs) {
			const auto l = LayerOf(r);
			if (l != Layer::Effect && l != Layer::Other && PlanShows(l) && (!r.initiallyDisabled || _view.showDisabled)) {
				fit.push_back(&r);
			}
		}
		if (fit.empty()) {
			// Only effects or markers shown: fit to those.
			for (const auto& r : a_cell.info.refs) {
				if (PlanShows(LayerOf(r)) && (!r.initiallyDisabled || _view.showDisabled)) {
					fit.push_back(&r);
				}
			}
		}
		if (fit.size() > 1) {
			std::vector<std::array<float, 3>> points;
			std::vector<bool>                 containers;
			for (const auto* r : fit) {
				points.push_back(r->position);
				containers.push_back(LayerOf(*r) == Layer::Container);
			}
			std::vector<const home::PlacedRef*> kept;
			for (const auto i : LargestGroup(points, containers)) {
				kept.push_back(fit[i]);
			}
			fit = std::move(kept);
		}
		float minX = -1000.0f, minY = -1000.0f, maxX = 1000.0f, maxY = 1000.0f;
		auto  trimmedRange = [&] {
			std::vector<float> xs, ys;
			for (const auto* r : fit) {
				xs.push_back(r->position[0]);
				ys.push_back(r->position[1]);
			}
			const std::size_t trim = xs.size() >= 20 ? xs.size() / 50 : 0;
			auto range = [trim](std::vector<float>& a_v, float& a_lo, float& a_hi) {
				std::ranges::sort(a_v);
				a_lo = a_v[trim];
				a_hi = a_v[a_v.size() - 1 - trim];
			};
			range(xs, minX, maxX);
			range(ys, minY, maxY);
		};
		if (!fit.empty()) {
			trimmedRange();
			const float extent = std::max({ maxX - minX, maxY - minY, 200.0f });
			std::erase_if(fit, [&](const home::PlacedRef* a_ref) { return FootprintSize(*a_ref) > 0.4f * extent; });
			if (!fit.empty()) {
				trimmedRange();
			}
			// Containers, displays and doors the trim cut off but that lie near the rest stay in view.
			const float mx = std::max(maxX - minX, 200.0f) * 0.5f, my = std::max(maxY - minY, 200.0f) * 0.5f;
			const float x0 = minX - mx, x1 = maxX + mx, y0 = minY - my, y1 = maxY + my;
			for (const auto& r : a_cell.info.refs) {
				const auto l = LayerOf(r);
				if ((l == Layer::Container || l == Layer::Display || l == Layer::Door) && PlanShows(l) && (!r.initiallyDisabled || _view.showDisabled) &&
					r.position[0] >= x0 && r.position[0] <= x1 && r.position[1] >= y0 && r.position[1] <= y1) {
					minX = std::min(minX, r.position[0]);
					maxX = std::max(maxX, r.position[0]);
					minY = std::min(minY, r.position[1]);
					maxY = std::max(maxY, r.position[1]);
				}
			}
		}
		_centerX = (minX + maxX) * 0.5f;
		_centerY = (minY + maxY) * 0.5f;
		const float w = std::max(maxX - minX, 200.0f);
		const float h = std::max(maxY - minY, 200.0f);
		// Room for the floating toolbar at the top and the hint line at the bottom.
		const float top = w::Th().toolbarH + 2.0f * w::Th().pad, bottom = 2.0f * w::Th().rowH;
		_zoom = std::min(a_size.x * 0.88f / w, std::max(a_size.y - top - bottom, 100.0f) * 0.88f / h);
		_panX = 0.0f;
		_panY = (top - bottom) * 0.5f;
		_fitSize = a_size;
		_planMoved = false;
	}

	void EditorApp::CentrePlanOn(const OpenCell& a_cell, int a_index)
	{
		if (a_index < 0 || static_cast<std::size_t>(a_index) >= a_cell.info.refs.size()) {
			return;
		}
		const auto& r = a_cell.info.refs[static_cast<std::size_t>(a_index)];
		_panX = -(r.position[0] - _centerX) * _zoom;
		_panY = (r.position[1] - _centerY) * _zoom;
		_planMoved = true;
	}

	void EditorApp::DrawPlan(OpenCell& a_cell, ImVec2 a_size)
	{
		// Fitted when the cell opens and on Home, and again while the viewport is still settling (the window, the
		// rail and the drawer take their sizes in the first frames) until the author pans or zooms.
		// A layer turned on or off re-fits too, until then.
		if (_fitPending || (!_planMoved && (std::abs(a_size.x - _fitSize.x) > 0.5f || std::abs(a_size.y - _fitSize.y) > 0.5f || PlanLayers() != _planFitLayers))) {
			FitPlan(a_cell, a_size);
			_fitPending = false;
		}
		const ImVec2 canvasSize(std::max(a_size.x, 100.0f), std::max(a_size.y, 100.0f));
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("plan", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		const bool   hovered = ImGui::IsItemHovered();
		ImGuiIO&     io = ImGui::GetIO();
		const ImVec2 center(origin.x + canvasSize.x * 0.5f, origin.y + canvasSize.y * 0.5f);

		// Pan / zoom
		if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) ||
										 (!_selectMode && !_paint && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)))) {
			_panX += io.MouseDelta.x;
			_panY += io.MouseDelta.y;
			_planMoved = _planMoved || io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;
		}
		if (hovered && io.MouseWheel != 0.0f) {
			const float dx = (io.MousePos.x - center.x - _panX) / _zoom;
			const float dy = -(io.MousePos.y - center.y - _panY) / _zoom;
			_zoom = std::clamp(_zoom * std::pow(1.2f, io.MouseWheel), 0.005f, 50.0f);
			_panX = io.MousePos.x - center.x - dx * _zoom;
			_panY = io.MousePos.y - center.y + dy * _zoom;
			_planMoved = true;
		}
		auto toScreen = [&](float a_x, float a_y) {
			return ImVec2(center.x + (a_x - _centerX) * _zoom + _panX, center.y - (a_y - _centerY) * _zoom + _panY);
		};

		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(origin, ImVec2(origin.x + canvasSize.x, origin.y + canvasSize.y), w::Th().viewport);
		dl->PushClipRect(origin, ImVec2(origin.x + canvasSize.x, origin.y + canvasSize.y), true);

		struct Hit
		{
			int    index;
			ImVec2 pts[4];
			bool   poly;
			ImVec2 at;
			Layer  layer;
			int    priority;
			float  area;
		};
		std::vector<Hit> hits;
		_hoveredRef = -1;

		// Draw in layer order: structure and volumes below, what matters on top. Hidden layers are neither drawn nor
		// hit (hover, click and box selection only see what is drawn).
		const Layer order[] = { Layer::Structure, Layer::Other, Layer::Effect, Layer::Furniture, Layer::Actor, Layer::Door, Layer::Container, Layer::Display };
		for (const auto layer : order) {
			if (!PlanShows(layer)) {
				continue;
			}
			for (std::size_t i = 0; i < a_cell.info.refs.size(); ++i) {
				const auto& r = a_cell.info.refs[i];
				if (LayerOf(r) != layer || (_heightCut && (r.position[2] < _zLo || r.position[2] > _zHi))) {
					continue;
				}
				if (r.initiallyDisabled && !_view.showDisabled) {
					continue;
				}
				ImU32 col = ColorOf(r, layer);
				if (_colours != Colours::None && (layer == Layer::Container || layer == Layer::Display)) {
					// Colour by category or fill; what has no colour fades so the coloured ones stand out.
					const auto t = i < _view.tint.size() ? _view.tint[i] : 0u;
					col = t ? t : (col & 0x00FFFFFF) | 0x50000000;
				}
				const ImVec2 at = toScreen(r.position[0], r.position[1]);
				Hit          hit{ static_cast<int>(i), {}, false, at, layer, PickPriority(layer), 0.0f };
				const float  w = static_cast<float>(r.bounds[3] - r.bounds[0]) * r.scale;
				const float  h = static_cast<float>(r.bounds[4] - r.bounds[1]) * r.scale;
				if (r.hasBounds && w > 0.0f && h > 0.0f) {
					// Footprint: the base's bounds turned by the reference's Z rotation (clockwise seen from above).
					const float c = std::cos(r.rotation[2]);
					const float s = std::sin(r.rotation[2]);
					const float xs[4] = { r.bounds[0] * r.scale, r.bounds[3] * r.scale, r.bounds[3] * r.scale, r.bounds[0] * r.scale };
					const float ys[4] = { r.bounds[1] * r.scale, r.bounds[1] * r.scale, r.bounds[4] * r.scale, r.bounds[4] * r.scale };
					for (int k = 0; k < 4; ++k) {
						hit.pts[k] = toScreen(r.position[0] + xs[k] * c + ys[k] * s, r.position[1] - xs[k] * s + ys[k] * c);
					}
					hit.poly = true;
					hit.area = w * h;
					if (layer == Layer::Structure || layer == Layer::Furniture || layer == Layer::Effect || layer == Layer::Other) {
						dl->AddPolyline(hit.pts, 4, col, ImDrawFlags_Closed, 1.0f);
					} else {
						dl->AddConvexPolyFilled(hit.pts, 4, (col & 0x00FFFFFF) | 0x90000000);
						dl->AddPolyline(hit.pts, 4, col, ImDrawFlags_Closed, 1.5f);
					}
				} else {
					dl->AddCircleFilled(at, layer == Layer::Structure || layer == Layer::Effect || layer == Layer::Other ? 2.0f : 4.0f, col);
				}
				if (layer != Layer::Structure && layer != Layer::Effect && layer != Layer::Other) {
					dl->AddCircleFilled(at, 2.5f, col);
				}
				hits.push_back(hit);
			}
		}

		// Hover / select: the most important layer under the cursor, then the smallest footprint.
		const float clickDx = io.MousePos.x - io.MouseClickedPos[ImGuiMouseButton_Left].x;
		const float clickDy = io.MousePos.y - io.MouseClickedPos[ImGuiMouseButton_Left].y;
		const bool  clicked = ImGui::IsMouseReleased(ImGuiMouseButton_Left) && clickDx * clickDx + clickDy * clickDy <= io.MouseDragThreshold * io.MouseDragThreshold;
		if (hovered) {
			const ImVec2 m = io.MousePos;
			const Hit*   best = nullptr;
			for (const auto& h : hits) {
				bool inside = false;
				if (h.poly) {
					float sign = 0.0f;
					inside = true;
					for (int k = 0; k < 4 && inside; ++k) {
						const ImVec2& a = h.pts[k];
						const ImVec2& b = h.pts[(k + 1) % 4];
						const float   cross = (b.x - a.x) * (m.y - a.y) - (b.y - a.y) * (m.x - a.x);
						if (cross != 0.0f) {
							if (sign == 0.0f) {
								sign = cross;
							} else if ((cross > 0.0f) != (sign > 0.0f)) {
								inside = false;
							}
						}
					}
				}
				if (!inside) {
					const float dx = m.x - h.at.x, dy = m.y - h.at.y;
					inside = dx * dx + dy * dy <= 36.0f;
				}
				if (!inside) {
					continue;
				}
				if (!best || h.priority > best->priority || (h.priority == best->priority && h.area < best->area)) {
					best = &h;
				}
			}
			if (best) {
				_hoveredRef = best->index;
				const auto& r = a_cell.info.refs[static_cast<std::size_t>(best->index)];
				if (best->poly) {
					dl->AddPolyline(best->pts, 4, IM_COL32(255, 255, 255, 200), ImDrawFlags_Closed, 2.0f);
				}
				// The name near the cursor; the details are in the Selection panel.
				if (ImGui::BeginTooltip()) {
					ImGui::TextUnformatted(RefTitle(r).c_str());
					if (r.display) {
						w::Muted(home::DisplayLabel(r).c_str());
					}
					ImGui::EndTooltip();
				}
				if ((_selectMode || _paint) && clicked) {
					ClickRef(a_cell, best->index);
				}
			} else if (_selectMode && clicked && !io.KeyCtrl) {
				Select(-1);
			}
		}
		// Box selection: a left drag in select mode selects the containers and displays inside (Ctrl adds).
		if (_selectMode && !_paint && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !io.KeyAlt) {
			const ImVec2 a = io.MouseClickedPos[ImGuiMouseButton_Left];
			const ImVec2 b = io.MousePos;
			dl->AddRectFilled(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)), ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)), IM_COL32(255, 230, 120, 40));
			dl->AddRect(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)), ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)), IM_COL32(255, 230, 120, 200));
			_boxing = true;
		} else if (_boxing && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			_boxing = false;
			const ImVec2     a = io.MouseClickedPos[ImGuiMouseButton_Left];
			const ImVec2     b = io.MousePos;
			std::vector<int> inside;
			for (const auto& h : hits) {
				if ((h.layer == Layer::Container || h.layer == Layer::Display) && h.at.x >= std::min(a.x, b.x) && h.at.x <= std::max(a.x, b.x) &&
					h.at.y >= std::min(a.y, b.y) && h.at.y <= std::max(a.y, b.y)) {
					inside.push_back(h.index);
				}
			}
			BoxSelect(inside, io.KeyCtrl);
		}
		for (const auto& h : hits) {
			if (h.index != _selectedRef && IsSelected(h.index)) {
				if (h.poly) {
					dl->AddPolyline(h.pts, 4, IM_COL32(255, 230, 120, 255), ImDrawFlags_Closed, 2.5f);
				}
				dl->AddCircle(h.at, 7.0f, IM_COL32(255, 230, 120, 255), 0, 2.0f);
			}
		}
		if (_selectedRef >= 0) {
			for (const auto& h : hits) {
				if (h.index == _selectedRef) {
					if (h.poly) {
						dl->AddPolyline(h.pts, 4, w::Th().select, ImDrawFlags_Closed, 3.0f);
					}
					dl->AddCircle(h.at, 9.0f, w::Th().select, 0, 2.0f);
				}
			}
		}
		// Scale bar in metres above the hint line (1, 2, 5, 10 or 20 m, the longest that stays under 160 px), north at
		// the top right.
		const auto& t = w::Th();
		int         metres = 1;
		for (const int m : { 2, 5, 10, 20 }) {
			if (static_cast<float>(m) * kUnitsPerMetre * _zoom <= 160.0f * t.scale) {
				metres = m;
			}
		}
		const float  bar = static_cast<float>(metres) * kUnitsPerMetre * _zoom;
		const ImVec2 b0(origin.x + t.pad, origin.y + canvasSize.y - t.rowH - 10.0f * t.scale - t.pad);
		dl->AddLine(b0, ImVec2(b0.x + bar, b0.y), t.viewportText, 2.0f);
		ImGui::PushFont(t.smallFont);
		dl->AddText(ImVec2(b0.x, b0.y - ImGui::GetFontSize() - 2.0f), t.viewportText, std::format("{} m", metres).c_str());
		dl->AddText(ImVec2(origin.x + canvasSize.x - ImGui::GetFontSize() * 1.5f, origin.y + t.pad), t.viewportText, "N");
		ImGui::PopFont();
		dl->PopClipRect();
	}
}
