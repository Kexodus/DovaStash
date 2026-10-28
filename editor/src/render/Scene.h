#pragma once

// The 3D view of a cell (docs/EDITOR.md §4.5, M2): every placed reference drawn with its base object's
// mesh, textured and lit, exterior terrain from LAND, a fly camera, and picking for select mode.
//
// Loading is asynchronous: Scene::Start reads meshes and textures on worker threads (Direct3D 11 devices
// create resources from any thread) and the view fills in as models arrive; Progress() tells how far it
// got. A missing or unreadable mesh is drawn as a box of the object's bounds, a missing or unreadable
// texture as a gray checker. "Missing" is a file no loose folder and no archive has; "unreadable" one that
// was found but could not be read or used (a broken file, Direct3D refusing it). Rendering and picking run
// on the UI thread.

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <DirectXMath.h>
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#include <d3d11.h>
#include <wrl/client.h>

namespace dova::editor
{
	class Assets;
	namespace home
	{
		struct CellInfo;
	}
}

namespace dova::editor::render
{
	using Microsoft::WRL::ComPtr;

	enum class RefKind : std::uint8_t
	{
		Structure,  // statics: walls, floors, clutter that cannot be bound
		Container,
		Display,
		Door,
		Furniture,  // furniture, crafting stations, activators
		Actor,      // NPCs (mannequins): drawn as a box
		Effect,     // fog, mist, glows, light rays (Effects)
		Other
	};

	// One placed reference as the scene needs it (built from home::PlacedRef by BuildSceneRefs).
	struct SceneRef
	{
		int                         index{ -1 };  // index into CellInfo::refs: the selection id shared with the plan (< -1: a neighbour cell's)
		std::string                 model;        // "meshes\\...nif", empty: box
		std::array<float, 3>        position{};
		std::array<float, 3>        rotation{};  // radians as stored
		float                       scale{ 1.0f };
		std::array<float, 6>        bounds{};  // OBND (local, unscaled); all zero: none
		RefKind                     kind{ RefKind::Structure };
		std::uint32_t               color{ 0xFFFFFFFF };  // box / highlight color (RGBA8)
		bool                        initiallyDisabled{ false };
		bool                        selectable{ false };  // a role can be given to it (select mode picks it)
		bool                        ghost{ false };       // invisible in game (a marker mesh): a see-through box
	};

	struct SceneTerrain
	{
		int                               gridX{ 0 }, gridY{ 0 };
		std::array<float, 33 * 33>        heights{};
		std::vector<std::array<float, 3>> normals;
		std::vector<std::uint32_t>        colors;
		std::string                       texture;  // "textures\\...dds" (first quadrant's base texture)
		bool                              context{ false };  // a neighbour cell's
	};

	struct SceneDesc
	{
		std::vector<SceneRef>     refs;
		std::vector<SceneTerrain> terrain;
		bool                      interior{ true };
	};

	// Turns an opened cell into scene references: markers and objects without a mesh are left out (NPCs stay
	// as boxes), `a_selectable` decides what select mode picks.
	// `a_context`: cells drawn around it (exterior neighbours); their references cannot be selected and get
	// negative indices.
	[[nodiscard]] SceneDesc BuildSceneDesc(const home::CellInfo& a_cell, const std::vector<home::CellInfo>& a_context = {});

	struct Camera
	{
		DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
		float             yaw{ 0.0f };    // radians, 0 = +X (east), counter-clockwise seen from above
		float             pitch{ 0.0f };  // radians, + looks up
		float             fovY{ 1.0f };   // radians
		float             nearZ{ 4.0f };
		float             farZ{ 200000.0f };

		[[nodiscard]] DirectX::XMVECTOR Forward() const;
		[[nodiscard]] DirectX::XMVECTOR Right() const;
		[[nodiscard]] DirectX::XMVECTOR Up() const;
		[[nodiscard]] DirectX::XMMATRIX View() const;
		[[nodiscard]] DirectX::XMMATRIX Projection(float a_aspect) const;  // reversed Z
		void                            LookAt(const DirectX::XMFLOAT3& a_target);
	};

	// An off-screen colour + depth target (4x MSAA, resolved into a texture ImGui can show).
	class RenderTarget
	{
	public:
		bool                                    Resize(ID3D11Device* a_device, unsigned a_width, unsigned a_height);
		[[nodiscard]] unsigned                  Width() const noexcept { return _w; }
		[[nodiscard]] unsigned                  Height() const noexcept { return _h; }
		[[nodiscard]] ID3D11ShaderResourceView* Srv() const noexcept { return _srv.Get(); }
		[[nodiscard]] ID3D11Texture2D*          Resolved() const noexcept { return _resolved.Get(); }

	private:
		friend class Scene;
		unsigned                       _w{ 0 }, _h{ 0 };
		ComPtr<ID3D11Texture2D>        _color, _resolved, _depth;
		ComPtr<ID3D11RenderTargetView> _rtv;
		ComPtr<ID3D11DepthStencilView> _dsv;
		ComPtr<ID3D11ShaderResourceView> _srv;
	};

	struct LoadProgress
	{
		std::size_t modelsDone{ 0 }, modelsTotal{ 0 };
		std::size_t texturesDone{ 0 }, texturesTotal{ 0 };
		std::size_t archivesDone{ 0 }, archivesTotal{ 0 };
		std::size_t missingModels{ 0 }, missingTextures{ 0 };        // in no loose folder and no archive
		std::size_t unreadableModels{ 0 }, unreadableTextures{ 0 };  // found, but broken or refused by Direct3D
		bool        finished{ false };
		std::string stage;
	};

	// The roof cut per area of an interior (ViewOptions::roofGrid): squares over the home, each cut at its own floor
	// + about 3 m, so a cave below the built rooms opens from above too, not only the highest rooms.
	struct RoofGrid
	{
		static constexpr int kMaxSquares = 4096;  // the shader's table
		float                x0{ 0.0f }, y0{ 0.0f }, size{ 512.0f };  // the south-west corner, a square's side
		int                  nx{ 0 }, ny{ 0 };
		std::vector<float>   cuts;  // nx * ny, row by row from the south

		[[nodiscard]] bool  Empty() const noexcept { return nx <= 0 || ny <= 0 || cuts.size() != static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny); }
		[[nodiscard]] float At(float a_x, float a_y) const;  // outside: the nearest edge square

		// 512-unit squares over `a_floor` (where things stand: x, y and the height they stand at), each cut `a_above`
		// units over its lowest point; a square without one takes its neighbours' lowest cut (three squares out), else
		// `a_top`. Never above `a_top` (ui::detail::RoofTop): a cave below the built rooms opens, the rooms as before.
		[[nodiscard]] static RoofGrid Build(const std::vector<std::array<float, 3>>& a_floor, float a_top, float a_above);
	};

	struct ViewOptions
	{
		int   selected{ -1 };  // SceneRef::index
		int   hovered{ -1 };
		bool  showStructure{ true }, showContainers{ true }, showDisplays{ true }, showDoors{ true }, showFurniture{ true }, showActors{ true },
			showOther{ true };
		bool  showDisabled{ true };   // initially disabled references (upgrades, quest items)
		bool  showTerrain{ true };
		bool  showEffects{ true };    // glows, flames, fog and light rays (effect shaders and home::RefLayer::Effect references)
		bool  showMissing{ false };   // structure whose mesh is missing or unreadable: a see-through box (containers and displays stay solid)
		bool  showMarkers{ false };   // boxes around every selectable object
		bool  heightCut{ false };     // only references between zLo and zHi
		float zLo{ -1e9f }, zHi{ 1e9f };
		float roofZ{ 1e9f };          // the roof cut: nothing above this height is drawn or picked but containers and displays (an interior's 3D view)
		RoofGrid roofGrid;            // with roofZ below 1e9: the cut per area instead (empty: roofZ everywhere)
		bool  wireframe{ false };
		float brightness{ 1.0f };  // scales the editor's light (Skyrim's textures are made for darker, HDR lighting)
		// Colour overlay per reference (indexed by SceneRef::index; RGBA8, 0 = none): category colours, the preview's
		// fill. Tinted and outlined, the outline drawn through walls.
		std::vector<std::uint32_t> tint;
		std::vector<int>           multi;  // more selected references (multi-select): outlined like the selection
		int                        only{ -1 };        // >= 0: draw just this reference (SceneRef::index), for thumbnails
		std::uint32_t              background{ 0 };  // clear colour (RGBA8, 0 = the view's own)
	};

	struct PickResult
	{
		int   index{ -1 };  // SceneRef::index
		float distance{ 0.0f };
		DirectX::XMFLOAT3 point{};
	};

	class Scene
	{
	public:
		Scene(ID3D11Device* a_device, std::shared_ptr<Assets> a_assets, SceneDesc a_desc);
		~Scene();
		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;

		void Start();                         // starts the loader threads
		void Wait();                          // blocks until loading is done (headless rendering)
		[[nodiscard]] LoadProgress Progress() const;
		[[nodiscard]] std::vector<std::string> Problems() const;  // missing meshes / textures, parse warnings (first ones)

		// Draws into `a_target` (UI thread).
		void Render(ID3D11DeviceContext* a_context, RenderTarget& a_target, const Camera& a_camera, const ViewOptions& a_options);

		// The nearest reference under a ray. `a_selectableOnly`: what select mode may pick; something drawn in
		// front of it (a wall) still blocks it.
		[[nodiscard]] std::optional<PickResult> Pick(DirectX::FXMVECTOR a_origin, DirectX::FXMVECTOR a_dir, const ViewOptions& a_options, bool a_selectableOnly,
			int a_ignore = -1) const;

		// World-space bounds of a reference (its mesh, or its OBND box) / of everything.
		[[nodiscard]] std::optional<std::pair<DirectX::XMFLOAT3, DirectX::XMFLOAT3>> Bounds(int a_index) const;
		// `a_withDisabled` false: initially disabled references never count (they are left out anyway when they are
		// fewer than three in four).
		[[nodiscard]] std::pair<DirectX::XMFLOAT3, DirectX::XMFLOAT3>                SceneBounds(bool a_withDisabled = true) const;
		// The references the overview frames: SceneBounds' cluster (the largest group, with its rule for initially disabled ones).
		[[nodiscard]] std::vector<const SceneRef*> OverviewRefs(bool a_withDisabled = true) const;

		[[nodiscard]] const SceneDesc& Desc() const noexcept { return _desc; }

		// Meshes whose bounds differ from their base object's OBND by more than `a_tolerance` units (the
		// Creation Kit computes OBND from the mesh, so this checks the mesh reader): "path: mesh .. vs OBND ..".
		[[nodiscard]] std::vector<std::string> BoundsMismatches(float a_tolerance, std::size_t& a_checked) const;
		[[nodiscard]] std::size_t      DrawCalls() const noexcept { return _drawCalls; }

		// A camera looking at the whole scene from above at an angle (interiors: through the culled ceilings).
		[[nodiscard]] Camera OverviewCamera(float a_aspect, float a_roofZ = 1e9f, bool a_withDisabled = true) const;  // framed below the roof cut
		// A camera looking at one reference from where nothing stands in between (tries several directions,
		// starting with the current one; moves closer when every side is walled in).
		[[nodiscard]] Camera FrameCamera(const Camera& a_from, int a_index, float a_aspect, const ViewOptions& a_options) const;

	private:
		struct Texture;
		struct Model;
		struct Instance;
		struct Shared;

		void LoaderThread(std::stop_token a_stop);
		void LoadModel(std::size_t a_slot);
		void Problem(std::string a_text);
		void LogLoaded();
		// `a_usedBy`: the mesh asking and where it comes from ("meshes\x.nif from <mod>"), for the problem list.
		std::shared_ptr<Texture> GetTexture(const std::string& a_path, const std::string& a_usedBy = {});
		void BuildTerrain();
		void EnsurePipeline();

		ComPtr<ID3D11Device>      _device;
		std::shared_ptr<Assets>   _assets;
		SceneDesc                 _desc;
		std::unique_ptr<Shared>   _shared;  // pipeline objects (shaders, states)
		std::vector<std::unique_ptr<Model>> _models;  // one per unique mesh path; [0] is the unit box
		std::unordered_map<std::string, std::size_t> _modelIndex;
		std::vector<Instance>     _instances;  // parallel to _desc.refs
		std::unique_ptr<Model>    _terrain;

		mutable std::mutex                                        _mutex;  // _textures, _problems, model readiness
		std::unordered_map<std::string, std::shared_ptr<Texture>> _textures;
		std::vector<std::string>                                  _problems;

		std::atomic<std::size_t> _nextModel{ 1 };
		std::atomic<std::size_t> _modelsDone{ 0 }, _texturesDone{ 0 }, _texturesTotal{ 0 }, _archivesDone{ 0 }, _archivesTotal{ 0 };
		std::atomic<std::size_t> _missingModels{ 0 }, _missingTextures{ 0 }, _unreadableModels{ 0 }, _unreadableTextures{ 0 };
		std::atomic<int>         _workersLeft{ 0 };
		std::atomic<bool>        _finished{ false };
		std::atomic<int>         _stage{ 0 };  // 0 archives, 1 models, 2 done
		std::vector<std::jthread> _workers;
		std::size_t              _drawCalls{ 0 };
	};
}
