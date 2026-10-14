// The 3D view (docs/EDITOR.md §4.5, EDITOR-UX.md §2.3): a Creation Kit style viewport of the open cell that fills
// the viewport; the floating toolbar and the hint line are drawn over it (App.cpp). Holding the right mouse
// button flies (the mouse looks, WASD moves, Q/E down/up), the wheel moves forward, the middle button pans,
// Alt + left drag orbits the selection; F frames the selection, Home shows everything (keys in App.cpp). Select
// mode makes the left button pick what a template can use (containers, displays, doors, furniture), sharing the
// selection with the plan and the tables. A double click on a door opens the cell behind it.
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include <imgui.h>

#include "Util.h"
#include "ui/App.h"

using namespace DirectX;

namespace dova::editor::ui
{
	namespace
	{
		std::string RefName(const home::PlacedRef& a_ref)
		{
			if (!a_ref.baseName.empty()) {
				return a_ref.baseName;
			}
			if (!a_ref.baseEditorId.empty()) {
				return a_ref.baseEditorId;
			}
			return a_ref.editorId.empty() ? std::string("(unnamed)") : a_ref.editorId;
		}
	}

	void EditorApp::DrawView3D(OpenCell& a_cell, ImVec2 a_size)
	{
		ImGuiIO& io = ImGui::GetIO();
		const ImVec2 size(std::max(a_size.x, 64.0f), std::max(a_size.y, 64.0f));
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("view3d", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();
		ImDrawList* dl = ImGui::GetWindowDrawList();

		if (!_scene) {
			w::CentredMessage(origin, ImVec2(origin.x + size.x, origin.y + size.y), "The 3D view needs Direct3D 11", "The plan (2) works without it.");
			return;
		}
		const float scale = io.DisplayFramebufferScale.x > 0.0f ? io.DisplayFramebufferScale.x : 1.0f;
		_target.Resize(_device, static_cast<unsigned>(size.x * scale), static_cast<unsigned>(size.y * scale));
		const float aspect = size.x / size.y;
		_viewAspect = aspect;
		if (_cameraPending) {
			_camera = _scene->OverviewCamera(aspect);
			_cameraPending = false;
		}
		if (_frameSelection && _selectedRef >= 0 && _scene->Progress().finished) {  // walls must be loaded to look past them
			_camera = _scene->FrameCamera(_camera, _selectedRef, aspect, _view);
			_frameSelection = false;
		}

		// ---- Camera input ----
		const float dt = std::min(io.DeltaTime, 0.1f);
		const bool  looking = active && ImGui::IsMouseDown(ImGuiMouseButton_Right);
		float       speed = _flySpeed * (io.KeyShift ? 4.0f : 1.0f) * (io.KeyAlt ? 0.25f : 1.0f);
		if (looking) {
			_camera.yaw -= io.MouseDelta.x * 0.005f;
			_camera.pitch = std::clamp(_camera.pitch - io.MouseDelta.y * 0.005f, -1.55f, 1.55f);
		}
		XMVECTOR pos = XMLoadFloat3(&_camera.position);
		if (looking) {
			XMVECTOR move = XMVectorZero();
			if (ImGui::IsKeyDown(ImGuiKey_W)) {
				move = XMVectorAdd(move, _camera.Forward());
			}
			if (ImGui::IsKeyDown(ImGuiKey_S)) {
				move = XMVectorSubtract(move, _camera.Forward());
			}
			if (ImGui::IsKeyDown(ImGuiKey_D)) {
				move = XMVectorAdd(move, _camera.Right());
			}
			if (ImGui::IsKeyDown(ImGuiKey_A)) {
				move = XMVectorSubtract(move, _camera.Right());
			}
			if (ImGui::IsKeyDown(ImGuiKey_E) || ImGui::IsKeyDown(ImGuiKey_Space)) {
				move = XMVectorAdd(move, XMVectorSet(0, 0, 1, 0));
			}
			if (ImGui::IsKeyDown(ImGuiKey_Q)) {
				move = XMVectorSubtract(move, XMVectorSet(0, 0, 1, 0));
			}
			if (!XMVector3Equal(move, XMVectorZero())) {
				pos = XMVectorAdd(pos, XMVectorScale(XMVector3Normalize(move), speed * dt));
			}
		}
		if (hovered && io.MouseWheel != 0.0f) {
			if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
				_flySpeed = std::clamp(_flySpeed * std::pow(1.25f, io.MouseWheel), 50.0f, 8000.0f);
			} else {
				pos = XMVectorAdd(pos, XMVectorScale(_camera.Forward(), io.MouseWheel * std::max(speed * 0.3f, 24.0f)));
			}
		}
		if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
			const float k = std::max(speed * 0.0025f, 0.2f);
			pos = XMVectorAdd(pos, XMVectorScale(_camera.Right(), -io.MouseDelta.x * k));
			pos = XMVectorAdd(pos, XMVectorScale(_camera.Up(), io.MouseDelta.y * k));
		}
		if (active && io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
			// Orbit around the selection (or a point ahead).
			XMVECTOR pivot = XMVectorAdd(pos, XMVectorScale(_camera.Forward(), 400.0f));
			if (_selectedRef >= 0) {
				if (const auto b = _scene->Bounds(_selectedRef)) {
					pivot = XMVectorScale(XMVectorAdd(XMLoadFloat3(&b->first), XMLoadFloat3(&b->second)), 0.5f);
				}
			}
			const float dist = XMVectorGetX(XMVector3Length(XMVectorSubtract(pivot, pos)));
			_camera.yaw -= io.MouseDelta.x * 0.006f;
			_camera.pitch = std::clamp(_camera.pitch - io.MouseDelta.y * 0.006f, -1.55f, 1.55f);
			pos = XMVectorSubtract(pivot, XMVectorScale(_camera.Forward(), dist));
		}
		XMStoreFloat3(&_camera.position, pos);

		// ---- Picking ----
		auto rayAt = [&](ImVec2 a_mouse) {
			const float nx = ((a_mouse.x - origin.x) / size.x) * 2.0f - 1.0f;
			const float ny = 1.0f - ((a_mouse.y - origin.y) / size.y) * 2.0f;
			const float t = std::tan(_camera.fovY * 0.5f);
			return XMVector3Normalize(XMVectorAdd(_camera.Forward(),
				XMVectorAdd(XMVectorScale(_camera.Right(), nx * t * aspect), XMVectorScale(_camera.Up(), ny * t))));
		};
		_view.selected = _selectedRef;
		_hoveredRef = -1;
		if (hovered && !looking && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
			const auto  progress = _scene->Progress();
			PickCache   key{ io.MousePos.x, io.MousePos.y, _camera.position.x, _camera.position.y, _camera.position.z, _camera.yaw, _camera.pitch, _selectMode,
                progress.modelsDone, -1 };
			const bool  same = key.mouseX == _pick.mouseX && key.mouseY == _pick.mouseY && key.camX == _pick.camX && key.camY == _pick.camY && key.camZ == _pick.camZ &&
			                  key.yaw == _pick.yaw && key.pitch == _pick.pitch && key.selectMode == _pick.selectMode && key.loaded == _pick.loaded;
			if (!same) {
				const auto hit = _scene->Pick(XMLoadFloat3(&_camera.position), rayAt(io.MousePos), _view, _selectMode);
				key.result = hit && hit->index >= 0 ? hit->index : -1;  // neighbour cells (negative indices) are shown, not selectable
				_pick = key;
			}
			_hoveredRef = _pick.result;
		}
		const ImVec2 clickDelta(io.MousePos.x - io.MouseClickedPos[ImGuiMouseButton_Left].x, io.MousePos.y - io.MouseClickedPos[ImGuiMouseButton_Left].y);
		const bool   clicked = hovered && !io.KeyAlt && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
		                     clickDelta.x * clickDelta.x + clickDelta.y * clickDelta.y <= io.MouseDragThreshold * io.MouseDragThreshold;
		if (clicked && (_hoveredRef >= 0 || !io.KeyCtrl)) {
			ClickRef(a_cell, _hoveredRef);
		}
		// Box selection: a left drag in select mode selects the containers and displays whose middle is inside (Ctrl adds).
		if (_selectMode && !_paint && active && !io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
			_boxing = true;
		} else if (_boxing && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			_boxing = false;
			const ImVec2     a = io.MouseClickedPos[ImGuiMouseButton_Left];
			const ImVec2     b = io.MousePos;
			const XMMATRIX   vp = XMMatrixMultiply(_camera.View(), _camera.Projection(aspect));
			std::vector<int> inside;
			for (std::size_t i = 0; i < a_cell.info.refs.size(); ++i) {
				const auto& r = a_cell.info.refs[i];
				if (!r.IsContainer() && !r.display) {
					continue;
				}
				if (_heightCut && (r.position[2] < _zLo || r.position[2] > _zHi)) {
					continue;
				}
				XMVECTOR center = XMVectorSet(r.position[0], r.position[1], r.position[2], 1.0f);
				if (const auto bounds = _scene->Bounds(static_cast<int>(i))) {
					center = XMVectorScale(XMVectorAdd(XMLoadFloat3(&bounds->first), XMLoadFloat3(&bounds->second)), 0.5f);
					center = XMVectorSetW(center, 1.0f);
				}
				const XMVECTOR clip = XMVector4Transform(center, vp);
				const float    w = XMVectorGetW(clip);
				if (w <= 0.0f) {
					continue;  // behind the camera
				}
				const float sx = origin.x + (XMVectorGetX(clip) / w * 0.5f + 0.5f) * size.x;
				const float sy = origin.y + (0.5f - XMVectorGetY(clip) / w * 0.5f) * size.y;
				if (sx >= std::min(a.x, b.x) && sx <= std::max(a.x, b.x) && sy >= std::min(a.y, b.y) && sy <= std::max(a.y, b.y)) {
					inside.push_back(static_cast<int>(i));
				}
			}
			BoxSelect(inside, io.KeyCtrl);
		}
		if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && _hoveredRef >= 0) {
			const auto& ref = a_cell.info.refs[static_cast<std::size_t>(_hoveredRef)];
			for (const auto& d : a_cell.info.doors) {
				if (d.door == ref.key && d.destinationCell && !_job.valid()) {
					OpenCellAsync(a_cell.lo, *d.destinationCell, true, true);
					break;
				}
			}
		}
		_view.selected = _selectedRef;
		_view.hovered = _hoveredRef;

		// ---- Draw ----
		_scene->Render(_context, _target, _camera, _view);
		dl->AddImage(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(_target.Srv())), origin, ImVec2(origin.x + size.x, origin.y + size.y));
		dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
		if (_boxing) {
			const ImVec2 a = io.MouseClickedPos[ImGuiMouseButton_Left];
			const ImVec2 b = io.MousePos;
			dl->AddRectFilled(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)), ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)), IM_COL32(255, 230, 120, 40));
			dl->AddRect(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)), ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)), IM_COL32(255, 230, 120, 200));
		}
		dl->PopClipRect();
		if (_hoveredRef >= 0 && hovered && !looking) {
			// The name near the cursor; the details are in the Selection panel.
			const auto& r = a_cell.info.refs[static_cast<std::size_t>(_hoveredRef)];
			if (ImGui::BeginTooltip()) {
				ImGui::TextUnformatted(RefName(r).c_str());
				if (r.display) {
					w::Muted(home::DisplayLabel(r).c_str());
				}
				for (const auto& d : a_cell.info.doors) {
					if (d.door == r.key) {
						w::Muted(("leads to " + d.destinationLabel + (d.destinationCell ? ", double-click opens it" : "")).c_str());
					}
				}
				ImGui::EndTooltip();
			}
		}
	}
}