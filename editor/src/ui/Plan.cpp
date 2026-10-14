// The floor plan (docs/EDITOR.md §2 step 5, EDITOR-UX.md §2.3): the open cell seen from above, footprints from
// the base records' bounds turned by each reference's rotation, layers and the height cut from the Layers
// popover. Select mode: a click selects (containers first, then displays, doors, furniture, structure), a drag
// boxes containers and displays (Ctrl adds); the right or middle button pans, the wheel zooms.

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include "ui/App.h"
#include "ui/Common.h"

namespace dova::editor::ui
{
	using namespace detail;

	void EditorApp::FitPlan(const OpenCell& a_cell, ImVec2 a_size)
	{
		float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
		for (const auto& r : a_cell.info.refs) {
			if (LayerOf(r) == Layer::Other) {
				continue;
			}
			minX = std::min(minX, r.position[0]);
			maxX = std::max(maxX, r.position[0]);
			minY = std::min(minY, r.position[1]);
			maxY = std::max(maxY, r.position[1]);
		}
		if (minX > maxX) {
			minX = minY = -1000.0f;
			maxX = maxY = 1000.0f;
		}
		_centerX = (minX + maxX) * 0.5f;
		_centerY = (minY + maxY) * 0.5f;
		const float w = std::max(maxX - minX, 200.0f);
		const float h = std::max(maxY - minY, 200.0f);
		// Room for the floating toolbar at the top and the hint line at the bottom.
		_zoom = std::min(a_size.x * 0.85f / w, std::max(a_size.y - 90.0f, 100.0f) * 0.85f / h);
		_panX = 0.0f;
		_panY = 10.0f;
	}

	void EditorApp::CentrePlanOn(const OpenCell& a_cell, int a_index)
	{
		if (a_index < 0 || static_cast<std::size_t>(a_index) >= a_cell.info.refs.size()) {
			return;
		}
		const auto& r = a_cell.info.refs[static_cast<std::size_t>(a_index)];
		_panX = -(r.position[0] - _centerX) * _zoom;
		_panY = (r.position[1] - _centerY) * _zoom;
	}

	void EditorApp::DrawPlan(OpenCell& a_cell, ImVec2 a_size)
	{
		if (_fitPending) {
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
		}
		if (hovered && io.MouseWheel != 0.0f) {
			const float dx = (io.MousePos.x - center.x - _panX) / _zoom;
			const float dy = -(io.MousePos.y - center.y - _panY) / _zoom;
			_zoom = std::clamp(_zoom * std::pow(1.2f, io.MouseWheel), 0.005f, 50.0f);
			_panX = io.MousePos.x - center.x - dx * _zoom;
			_panY = io.MousePos.y - center.y + dy * _zoom;
		}
		auto toScreen = [&](float a_x, float a_y) {
			return ImVec2(center.x + (a_x - _centerX) * _zoom + _panX, center.y - (a_y - _centerY) * _zoom + _panY);
		};

		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(origin, ImVec2(origin.x + canvasSize.x, origin.y + canvasSize.y), w::Th().bg0);
		dl->PushClipRect(origin, ImVec2(origin.x + canvasSize.x, origin.y + canvasSize.y), true);

		struct Hit
		{
			int    index;
			ImVec2 pts[4];
			bool   poly;
			ImVec2 at;
			int    priority;
			float  area;
		};
		std::vector<Hit> hits;
		_hoveredRef = -1;

		// Draw in layer order: structure below, what matters on top.
		const Layer order[] = { Layer::Static, Layer::Other, Layer::Furniture, Layer::Door, Layer::Container, Layer::Display };
		for (const auto layer : order) {
			const bool show = (layer == Layer::Static && _view.showStructure) || (layer == Layer::Container && _view.showContainers) ||
			                  (layer == Layer::Display && _view.showDisplays) || (layer == Layer::Door && _view.showDoors) ||
			                  (layer == Layer::Furniture && _view.showFurniture) || (layer == Layer::Other && _showMarkers);
			if (!show) {
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
				Hit          hit{ static_cast<int>(i), {}, false, at, static_cast<int>(layer), 0.0f };
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
					if (layer == Layer::Static || layer == Layer::Furniture || layer == Layer::Other) {
						dl->AddPolyline(hit.pts, 4, col, ImDrawFlags_Closed, 1.0f);
					} else {
						dl->AddConvexPolyFilled(hit.pts, 4, (col & 0x00FFFFFF) | 0x90000000);
						dl->AddPolyline(hit.pts, 4, col, ImDrawFlags_Closed, 1.5f);
					}
				} else {
					dl->AddCircleFilled(at, layer == Layer::Other ? 2.0f : 4.0f, col);
				}
				if (layer != Layer::Static && layer != Layer::Other) {
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
				const auto layer = static_cast<Layer>(h.priority);
				if ((layer == Layer::Container || layer == Layer::Display) && h.at.x >= std::min(a.x, b.x) && h.at.x <= std::max(a.x, b.x) &&
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
		// Scale bar (512 units, a little over 7 m) above the hint line, north at the top right.
		const auto&  t = w::Th();
		const float  bar = 512.0f * _zoom;
		const ImVec2 b0(origin.x + t.pad, origin.y + canvasSize.y - t.rowH - 10.0f * t.scale);
		dl->AddLine(b0, ImVec2(b0.x + bar, b0.y), t.muted, 2.0f);
		ImGui::PushFont(t.smallFont);
		dl->AddText(ImVec2(b0.x, b0.y - ImGui::GetFontSize() - 2.0f), t.muted, "512 units");
		dl->AddText(ImVec2(origin.x + canvasSize.x - ImGui::GetFontSize() * 1.5f, origin.y + t.pad), t.muted, "N");
		ImGui::PopFont();
		dl->PopClipRect();
	}
}
