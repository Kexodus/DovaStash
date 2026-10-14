#include "render/Thumbnail.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dova::editor::render
{
	using namespace DirectX;

	Thumbnails::Thumbnails(ID3D11Device* a_device, ID3D11DeviceContext* a_context, unsigned a_size) :
		_device(a_device), _context(a_context), _size(a_size)
	{}

	void Thumbnails::Clear()
	{
		_cache.clear();
		_scene = nullptr;
		_sceneRefs = 0;
	}

	ImTextureID Thumbnails::For(Scene& a_scene, int a_refIndex)
	{
		if (&a_scene != _scene || a_scene.Desc().refs.size() != _sceneRefs) {
			Clear();
			_scene = &a_scene;
			_sceneRefs = a_scene.Desc().refs.size();
		}
		if (const auto it = _cache.find(a_refIndex); it != _cache.end()) {
			return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(it->second.Srv()));
		}
		if (!a_scene.Progress().finished || !_device || !_context) {
			return ImTextureID{};
		}
		const SceneRef* ref = nullptr;
		for (const auto& r : a_scene.Desc().refs) {
			if (r.index == a_refIndex) {
				ref = &r;
				break;
			}
		}
		const auto bounds = ref ? a_scene.Bounds(a_refIndex) : std::nullopt;
		if (!bounds) {
			return ImTextureID{};
		}
		RenderTarget target;
		if (!target.Resize(_device.Get(), _size, _size)) {
			return ImTextureID{};
		}
		// Three-quarter view: from the object's front (local -Y, turned by its Z rotation), 35 degrees to the
		// side and 25 degrees above, far enough for the bounding sphere to fill the picture.
		const XMFLOAT3 lo = bounds->first, hi = bounds->second;
		const XMFLOAT3 centre((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f);
		const float    radius = std::max(8.0f, 0.5f * std::sqrt((hi.x - lo.x) * (hi.x - lo.x) + (hi.y - lo.y) * (hi.y - lo.y) + (hi.z - lo.z) * (hi.z - lo.z)));
		constexpr float pi = std::numbers::pi_v<float>;
		Camera          cam;
		cam.fovY = 0.55f;
		const float rz = ref->rotation[2];
		const float frontYaw = std::atan2(-std::cos(rz), -std::sin(rz));  // direction the object faces
		const float camYaw = frontYaw + pi + 35.0f * pi / 180.0f;     // looking back at it, turned to the side
		cam.pitch = -25.0f * pi / 180.0f;
		cam.yaw = camYaw;
		const float dist = radius / std::sin(cam.fovY * 0.5f) * 0.92f;
		const XMVECTOR fwd = cam.Forward();
		XMStoreFloat3(&cam.position, XMVectorSubtract(XMLoadFloat3(&centre), XMVectorScale(fwd, dist)));
		cam.nearZ = std::max(1.0f, dist - radius * 1.5f);
		cam.farZ = dist + radius * 2.0f;

		ViewOptions options;
		options.only = a_refIndex;
		options.showTerrain = false;
		options.showDisabled = true;
		options.background = _background;
		a_scene.Render(_context.Get(), target, cam, options);
		const auto srv = target.Srv();
		_cache.emplace(a_refIndex, std::move(target));
		return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(srv));
	}
}
