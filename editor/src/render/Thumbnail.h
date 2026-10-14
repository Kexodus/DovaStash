#pragma once

// A reference's own mesh as a small picture (docs/EDITOR-UX.md §3.4: the top of the Selection panel).
// Each thumbnail is drawn once, by Scene::Render with ViewOptions::only, into a 96 x 96 target from a fixed
// three-quarter camera that frames the object's bounds, then cached per reference. UI thread only.

#include <cstdint>
#include <unordered_map>

#include <imgui.h>

#include "home/Cell.h"  // Scene.h's BuildSceneDesc default argument needs CellInfo complete
#include "render/Scene.h"

namespace dova::editor::render
{
	class Thumbnails
	{
	public:
		Thumbnails(ID3D11Device* a_device, ID3D11DeviceContext* a_context, unsigned a_size = 96);

		// The thumbnail of `a_refIndex` (SceneRef::index), 0 while the scene is still loading or when the
		// reference is not in it. A different scene clears the cache.
		[[nodiscard]] ImTextureID For(Scene& a_scene, int a_refIndex);
		void                      Clear();

		// Background (RGBA8); the editor passes its bg2 so the picture sits in the panel.
		void SetBackground(std::uint32_t a_rgba) { _background = a_rgba; }

	private:
		ComPtr<ID3D11Device>                 _device;
		ComPtr<ID3D11DeviceContext>          _context;
		unsigned                             _size;
		std::uint32_t                        _background{ 0xFF282222 };
		const Scene*                         _scene{ nullptr };
		std::size_t                          _sceneRefs{ 0 };
		std::unordered_map<int, RenderTarget> _cache;
	};
}
