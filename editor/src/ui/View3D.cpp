// The 3D view tab (docs/EDITOR.md §4.5, M2): a Creation Kit style viewport of the open cell. Holding the
// right mouse button flies (the mouse looks, WASD moves, Q/E down/up), the wheel moves forward, the middle button pans, Alt + left drag orbits the
// selection; F frames the selection, Home shows everything. Select mode (S) makes the left button pick what
// a template can use (containers, displays, doors, furniture), sharing the selection with the plan and the
// tables. A double click on a door opens the cell behind it.

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

	void EditorApp::DrawView3D(OpenCell& a_cell)
	{
		ImGuiIO& io = ImGui::GetIO();

		// Toolbar, first row: select mode, camera, speed, loading state.
		ImGui::Checkbox("Select mode (S)", &_selectMode);
		ImGui::SetItemTooltip("On: the left button picks what a template can use (containers, displays, doors, furniture); a wall in front "
		                      "still hides what is behind it. Off: it picks anything, walls and clutter included.");
		ImGui::SameLine();
		if (ImGui::Button("Overview (Home)")) {
			_cameraPending = true;
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(_selectedRef < 0);
		if (ImGui::Button("Frame (F)")) {
			_frameSelection = true;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
		ImGui::SliderFloat("Speed", &_flySpeed, 50.0f, 8000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
		ImGui::SetItemTooltip("Flying speed in game units per second (Shift: 4x, Alt: 1/4). The wheel with the right button held changes it.");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0f);
		ImGui::SliderFloat("Light", &_view.brightness, 0.4f, 2.5f, "%.1f");
		ImGui::SetItemTooltip("How bright the editor's light is (the game's own lights are not used).");
		if (_scene) {
			const auto p = _scene->Progress();
			ImGui::SameLine();
			if (!p.finished) {
				if (p.archivesTotal == 0 || p.archivesDone < p.archivesTotal) {
					ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Opening archives %zu/%zu...", p.archivesDone, p.archivesTotal);
				} else {
					ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Loading meshes %zu/%zu, textures %zu/%zu...", p.modelsDone, p.modelsTotal, p.texturesDone,
						p.texturesTotal);
				}
			} else {
				ImGui::TextDisabled("%zu meshes, %zu textures, %zu draws, %.0f fps", p.modelsTotal, p.texturesTotal, _scene->DrawCalls(), io.Framerate);
			}
			if (p.missingModels + p.missingTextures > 0) {
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.35f, 1.0f), "(%zu meshes, %zu textures missing)", p.missingModels, p.missingTextures);
				if (ImGui::BeginItemTooltip()) {
					ImGui::TextUnformatted("Missing meshes show as boxes, missing textures as a gray checker:");
					const auto problems = _scene->Problems();
					for (std::size_t i = 0; i < problems.size() && i < 30; ++i) {
						ImGui::BulletText("%s", problems[i].c_str());
					}
					if (problems.size() > 30) {
						ImGui::TextDisabled("... %zu more", problems.size() - 30);
					}
					ImGui::EndTooltip();
				}
			}
		}

		// Second row: what is shown.
		ImGui::Checkbox("Structure", &_view.showStructure);
		ImGui::SameLine();
		ImGui::Checkbox("Containers", &_view.showContainers);
		ImGui::SameLine();
		ImGui::Checkbox("Displays", &_view.showDisplays);
		ImGui::SameLine();
		ImGui::Checkbox("Doors", &_view.showDoors);
		ImGui::SameLine();
		ImGui::Checkbox("Furniture", &_view.showFurniture);
		ImGui::SameLine();
		ImGui::Checkbox("People", &_view.showActors);
		ImGui::SameLine();
		ImGui::Checkbox("Disabled", &_view.showDisabled);
		ImGui::SetItemTooltip("Initially disabled references: house upgrades, things a quest adds later.");
		ImGui::SameLine();
		ImGui::Checkbox("Effects", &_view.showEffects);
		ImGui::SetItemTooltip("Glows, flames, fog and light rays.");
		ImGui::SameLine();
		ImGui::Checkbox("Outlines", &_view.showMarkers);
		ImGui::SetItemTooltip("A box around everything select mode can pick, in its plan color.");
		if (!a_cell.info.interior) {
			ImGui::SameLine();
			ImGui::Checkbox("Terrain", &_view.showTerrain);
			ImGui::SameLine();
			ImGui::TextDisabled("(+%zu cells around)", a_cell.context.size());
		}
		ImGui::SameLine();
		ImGui::Checkbox("Height", &_heightCut3d);
		ImGui::SetItemTooltip("Only references between these heights (shared with the floor plan): one floor of a tower.");
		if (_heightCut3d) {
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
			ImGui::DragFloatRange2("##height3d", &_zLo, &_zHi, 4.0f, std::floor(_zMin) - 1.0f, std::ceil(_zMax) + 1.0f, "%.0f", "%.0f",
				ImGuiSliderFlags_AlwaysClamp);
		}
		_view.heightCut = _heightCut3d;
		_view.zLo = _zLo;
		_view.zHi = _zHi;

		const float  detailsWidth = ImGui::GetFontSize() * 19.0f;
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const ImVec2 size(std::max(avail.x - detailsWidth - ImGui::GetStyle().ItemSpacing.x, 64.0f), std::max(avail.y, 64.0f));
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("view3d", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();
		ImDrawList* dl = ImGui::GetWindowDrawList();

		if (!_scene) {
			dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(28, 30, 34, 255));
			dl->AddText(ImVec2(origin.x + 12, origin.y + 12), IM_COL32(200, 200, 200, 255), "The 3D view needs Direct3D 11.");
			ImGui::SameLine();
			DrawDetails(a_cell, detailsWidth, size.y);
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
		const bool  keys = (hovered || looking) && !io.WantTextInput;
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
		} else if (keys && !io.KeyCtrl) {
			if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
				_selectMode = !_selectMode;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_F, false) && _selectedRef >= 0) {
				_frameSelection = true;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
				_cameraPending = true;
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
		if (clicked) {
			Select(_hoveredRef);
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
		const char* help = _selectMode ? "Hold right mouse: look + WASD/QE fly   Wheel: forward   Middle: pan   Alt+Left: orbit   F: frame   Home: overview   S: select mode (on)"
		                               : "Hold right mouse: look + WASD/QE fly   Wheel: forward   Middle: pan   Alt+Left: orbit   F: frame   Home: overview   S: select mode (off)";
		dl->AddText(ImVec2(origin.x + 8.0f, origin.y + size.y - ImGui::GetFontSize() - 6.0f), IM_COL32(220, 220, 220, 170), help);
		dl->PopClipRect();
		if (_hoveredRef >= 0 && hovered && !looking) {
			const auto& r = a_cell.info.refs[static_cast<std::size_t>(_hoveredRef)];
			std::string extra;
			if (r.display) {
				extra = "\n" + home::DisplayLabel(r);
			}
			for (const auto& d : a_cell.info.doors) {
				if (d.door == r.key) {
					extra += "\nleads to " + d.destinationLabel + (d.destinationCell ? " (double-click to open)" : "");
				}
			}
			ImGui::SetTooltip("%s\n%s  %s%s", RefName(r).c_str(), a_cell.refText[static_cast<std::size_t>(_hoveredRef)].c_str(), r.baseEditorId.c_str(), extra.c_str());
		}

		ImGui::SameLine();
		DrawDetails(a_cell, detailsWidth, size.y);
	}
}
