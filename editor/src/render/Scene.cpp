#include "render/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <numbers>

#include <d3dcompiler.h>

#include "Util.h"
#include "data/Assets.h"
#include "home/Cell.h"
#include "render/Dds.h"
#include "render/Nif.h"

using namespace DirectX;

namespace dova::editor::render
{
	namespace
	{
		constexpr unsigned kMaxTextureSize = 1024;
		constexpr bool     kFrontCounterClockwise = true;

		// ---- Shaders ---------------------------------------------------------------------------------------
		constexpr const char* kShader = R"(
cbuffer Frame : register(b0)
{
	row_major float4x4 viewProj;
	float4 cameraPos;
	float4 lightDir;
	float4 ambient;
	float4 lightColor;
};
cbuffer Draw : register(b1)
{
	row_major float4x4 world;
	float4 tint;
	float4 highlight;
	float4 uvTransform;
	float4 params;  // x: alpha test threshold (< 0: off), y: vertex colors (1 rgb, 2 rgba), z: unlit, w: textured
};
Texture2D    tex  : register(t0);
SamplerState samp : register(s0);

struct VSIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; float4 col : COLOR0; };
struct PSIn { float4 pos : SV_Position; float3 wpos : TEXCOORD1; float3 nrm : NORMAL; float2 uv : TEXCOORD0; float4 col : COLOR0; };

PSIn VS(VSIn i)
{
	PSIn o;
	float4 w = mul(float4(i.pos, 1.0), world);
	o.pos = mul(w, viewProj);
	o.wpos = w.xyz;
	o.nrm = mul(float4(i.nrm, 0.0), world).xyz;
	o.uv = i.uv * uvTransform.zw + uvTransform.xy;
	o.col = i.col;
	return o;
}

float4 PS(PSIn i, bool front : SV_IsFrontFace) : SV_Target
{
	float4 c = params.w > 0.5 ? tex.Sample(samp, i.uv) : float4(1, 1, 1, 1);
	if (params.y > 0.5) {
		c.rgb *= i.col.rgb;
		if (params.y > 1.5) {
			c.a *= i.col.a;
		}
	}
	c *= tint;
	if (params.x >= 0.0 && c.a < params.x) {
		discard;
	}
	if (params.z > 0.5) {
		return float4(c.rgb + highlight.rgb, c.a);
	}
	float3 n = normalize(i.nrm);
	if (!front) {
		n = -n;
	}
	float3 v = normalize(cameraPos.xyz - i.wpos);
	float  key = saturate(dot(n, lightDir.xyz));
	float  head = saturate(dot(n, v));
	float3 lit = c.rgb * (ambient.rgb + lightColor.rgb * key + head * 0.35);
	return float4(lit + highlight.rgb, c.a);
}

struct LineIn { float3 pos : POSITION; float4 col : COLOR0; };
struct LineOut { float4 pos : SV_Position; float4 col : COLOR0; };
LineOut LineVS(LineIn i)
{
	LineOut o;
	o.pos = mul(float4(i.pos, 1.0), viewProj);
	o.col = i.col;
	return o;
}
float4 LinePS(LineOut i) : SV_Target { return i.col; }
)";

		struct FrameCB
		{
			XMFLOAT4X4 viewProj;
			XMFLOAT4   cameraPos;
			XMFLOAT4   lightDir;
			XMFLOAT4   ambient;
			XMFLOAT4   lightColor;
		};
		struct DrawCB
		{
			XMFLOAT4X4 world;
			XMFLOAT4   tint;
			XMFLOAT4   highlight;
			XMFLOAT4   uv;
			XMFLOAT4   params;
		};

		struct Vertex
		{
			float         pos[3];
			float         nrm[3];
			float         uv[2];
			std::uint32_t color;
		};
		struct LineVertex
		{
			float         pos[3];
			std::uint32_t color;
		};

		XMFLOAT4 ColorF(std::uint32_t a_rgba)
		{
			return { static_cast<float>(a_rgba & 0xFF) / 255.0f, static_cast<float>((a_rgba >> 8) & 0xFF) / 255.0f,
				static_cast<float>((a_rgba >> 16) & 0xFF) / 255.0f, static_cast<float>((a_rgba >> 24) & 0xFF) / 255.0f };
		}

		constexpr std::uint32_t Rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255)
		{
			return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8) | (static_cast<std::uint32_t>(b) << 16) |
			       (static_cast<std::uint32_t>(a) << 24);
		}

		D3D11_BLEND BlendOf(unsigned a_nif)
		{
			switch (a_nif) {
			case 0:
				return D3D11_BLEND_ONE;
			case 1:
				return D3D11_BLEND_ZERO;
			case 2:
				return D3D11_BLEND_SRC_COLOR;
			case 3:
				return D3D11_BLEND_INV_SRC_COLOR;
			case 4:
				return D3D11_BLEND_DEST_COLOR;
			case 5:
				return D3D11_BLEND_INV_DEST_COLOR;
			case 6:
				return D3D11_BLEND_SRC_ALPHA;
			case 7:
				return D3D11_BLEND_INV_SRC_ALPHA;
			case 8:
				return D3D11_BLEND_DEST_ALPHA;
			case 9:
				return D3D11_BLEND_INV_DEST_ALPHA;
			case 10:
				return D3D11_BLEND_SRC_ALPHA_SAT;
			default:
				return D3D11_BLEND_ONE;
			}
		}

		// Skyrim's reference rotation (CommonLib NiMatrix3::SetEulerAnglesXYZ), applied to column vectors:
		// v' = M v. Returned transposed for DirectXMath's row vectors.
		XMMATRIX RefRotation(float x, float y, float z)
		{
			const float sx = std::sin(x), cx = std::cos(x);
			const float sy = std::sin(y), cy = std::cos(y);
			const float sz = std::sin(z), cz = std::cos(z);
			const float m[3][3] = {
				{ cy * cz, cy * sz, -sy },
				{ sx * sy * cz - cx * sz, sx * sy * sz + cx * cz, sx * cy },
				{ cx * sy * cz + sx * sz, cx * sy * sz - sx * cz, cx * cy },
			};
			// Row-vector matrix R = M^T: R[i][j] = M[j][i].
			return XMMATRIX(m[0][0], m[1][0], m[2][0], 0.0f, m[0][1], m[1][1], m[2][1], 0.0f, m[0][2], m[1][2], m[2][2], 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
		}

		bool RayBox(XMVECTOR o, XMVECTOR d, const XMFLOAT3& bmin, const XMFLOAT3& bmax, float& tmin)
		{
			XMFLOAT3 of, df;
			XMStoreFloat3(&of, o);
			XMStoreFloat3(&df, d);
			float       t0 = 0.0f, t1 = FLT_MAX;
			const float oo[3] = { of.x, of.y, of.z }, dd[3] = { df.x, df.y, df.z };
			const float lo[3] = { bmin.x, bmin.y, bmin.z }, hi[3] = { bmax.x, bmax.y, bmax.z };
			for (int k = 0; k < 3; ++k) {
				if (std::abs(dd[k]) < 1e-9f) {
					if (oo[k] < lo[k] || oo[k] > hi[k]) {
						return false;
					}
					continue;
				}
				float a = (lo[k] - oo[k]) / dd[k];
				float b = (hi[k] - oo[k]) / dd[k];
				if (a > b) {
					std::swap(a, b);
				}
				t0 = std::max(t0, a);
				t1 = std::min(t1, b);
				if (t0 > t1) {
					return false;
				}
			}
			tmin = t0;
			return true;
		}

		bool RayTriangle(const XMFLOAT3& o, const XMFLOAT3& d, const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT3& c, float& t)
		{
			const float e1[3] = { b.x - a.x, b.y - a.y, b.z - a.z };
			const float e2[3] = { c.x - a.x, c.y - a.y, c.z - a.z };
			const float p[3] = { d.y * e2[2] - d.z * e2[1], d.z * e2[0] - d.x * e2[2], d.x * e2[1] - d.y * e2[0] };
			const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
			if (std::abs(det) < 1e-12f) {
				return false;
			}
			const float inv = 1.0f / det;
			const float s[3] = { o.x - a.x, o.y - a.y, o.z - a.z };
			const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
			if (u < 0.0f || u > 1.0f) {
				return false;
			}
			const float q[3] = { s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0] };
			const float v = (d.x * q[0] + d.y * q[1] + d.z * q[2]) * inv;
			if (v < 0.0f || u + v > 1.0f) {
				return false;
			}
			t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
			return t > 0.0f;
		}

		void ComputeNormals(const std::vector<float>& a_pos, const std::vector<std::uint32_t>& a_idx, std::vector<float>& a_out)
		{
			a_out.assign(a_pos.size(), 0.0f);
			for (std::size_t t = 0; t + 2 < a_idx.size(); t += 3) {
				const float* p0 = &a_pos[a_idx[t] * 3];
				const float* p1 = &a_pos[a_idx[t + 1] * 3];
				const float* p2 = &a_pos[a_idx[t + 2] * 3];
				const float  e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
				const float  e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
				const float  n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
				for (int k = 0; k < 3; ++k) {
					for (int j = 0; j < 3; ++j) {
						a_out[a_idx[t + static_cast<std::size_t>(k)] * 3 + static_cast<std::size_t>(j)] += n[j];
					}
				}
			}
			for (std::size_t i = 0; i + 2 < a_out.size(); i += 3) {
				const float len = std::sqrt(a_out[i] * a_out[i] + a_out[i + 1] * a_out[i + 1] + a_out[i + 2] * a_out[i + 2]);
				if (len > 1e-9f) {
					a_out[i] /= len;
					a_out[i + 1] /= len;
					a_out[i + 2] /= len;
				} else {
					a_out[i + 2] = 1.0f;
				}
			}
		}

		std::uint32_t KindColor(RefKind a_kind, const home::PlacedRef& a_ref)
		{
			switch (a_kind) {
			case RefKind::Container:
				return a_ref.respawns ? Rgba(230, 70, 60) : Rgba(90, 200, 110);
			case RefKind::Display:
				switch (*a_ref.display) {
				case core::DisplayKind::Rack:
					return Rgba(80, 190, 230);
				case core::DisplayKind::Mannequin:
					return Rgba(220, 110, 220);
				default:
					return Rgba(240, 170, 60);
				}
			case RefKind::Door:
				return Rgba(240, 220, 80);
			case RefKind::Furniture:
				return Rgba(140, 150, 220);
			case RefKind::Actor:
				return Rgba(220, 110, 220);
			default:
				return Rgba(160, 160, 160);
			}
		}
	}

	// ---- Scene description ------------------------------------------------------------------------------

	namespace
	{
		void AddCell(SceneDesc& desc, const home::CellInfo& a_cell, bool a_context, int& a_contextIndex)
		{
		if (!a_context && a_cell.refs.empty() && !a_cell.land) {
			return;
		}
		using esp::MakeSig;
		for (std::size_t i = 0; i < a_cell.refs.size(); ++i) {
			const auto& r = a_cell.refs[i];
			SceneRef    s;
			s.index = a_context ? a_contextIndex-- : static_cast<int>(i);
			if (r.display) {
				s.kind = RefKind::Display;
			} else if (r.IsContainer()) {
				s.kind = RefKind::Container;
			} else {
				switch (r.baseType) {
				case MakeSig("DOOR"):
					s.kind = RefKind::Door;
					break;
				case MakeSig("FURN"):
				case MakeSig("ACTI"):
					s.kind = RefKind::Furniture;
					break;
				case MakeSig("NPC_"):
					s.kind = RefKind::Actor;
					break;
				case 0:
					s.kind = RefKind::Other;
					break;
				default:
					s.kind = RefKind::Structure;
					break;
				}
			}
			s.model = r.model.empty() ? std::string{} : Assets::NormalizeMesh(r.model);
			const bool marker = IContains(s.model, "marker") || IContains(r.baseEditorId, "marker");
			// Kept even on a marker mesh (drawn as a box): what a template can use, doors, people.
			const bool important = s.kind == RefKind::Container || s.kind == RefKind::Display || s.kind == RefKind::Door || s.kind == RefKind::Actor;
			if (marker && !important) {
				continue;
			}
			if (marker) {
				s.model.clear();  // a door marker or a container on a marker mesh: a box shows where it is
				s.ghost = true;
			}
			if (s.model.empty() && !important) {
				continue;  // sound markers, trigger volumes, lights without a mesh
			}
			if (s.model.empty()) {
				s.ghost = true;  // no mesh at all (an activator volume): see-through like a marker
			}
			s.position = r.position;
			s.rotation = r.rotation;
			s.scale = r.scale > 0.0f ? r.scale : 1.0f;
			if (r.hasBounds) {
				for (std::size_t k = 0; k < 6; ++k) {
					s.bounds[k] = static_cast<float>(r.bounds[k]);
				}
			}
			if (s.kind == RefKind::Actor || (s.kind == RefKind::Display && r.display == core::DisplayKind::Mannequin)) {
				// NPC bounds are often zero: a person-sized box. Mannequins show their body mesh (home::LoadCell
				// finds it through the NPC's skin), other people stay boxes.
				if (s.bounds[3] - s.bounds[0] < 1.0f) {
					s.bounds = { -22.0f, -16.0f, 0.0f, 22.0f, 16.0f, 128.0f };
				}
				if (s.kind == RefKind::Actor) {
					s.model.clear();
				}
				s.ghost = false;  // solid
			}
			s.color = KindColor(s.kind, r);
			s.initiallyDisabled = r.initiallyDisabled;
			s.selectable = !a_context && (s.kind == RefKind::Container || s.kind == RefKind::Display || s.kind == RefKind::Door || s.kind == RefKind::Furniture);
			desc.refs.push_back(std::move(s));
		}
		if (a_cell.land && a_cell.grid) {
			SceneTerrain t;
			t.context = a_context;
			t.gridX = (*a_cell.grid)[0];
			t.gridY = (*a_cell.grid)[1];
			t.heights = a_cell.land->heights;
			t.normals = a_cell.land->normals;
			t.colors = a_cell.land->colors;
			std::string tex;
			for (const auto& q : a_cell.land->textures) {
				if (!q.empty()) {
					tex = q;
					break;
				}
			}
			t.texture = Assets::NormalizeTexture(tex.empty() ? std::string("landscape\\dirt02.dds") : tex);
			desc.terrain.push_back(std::move(t));
		}
		}
	}

	SceneDesc BuildSceneDesc(const home::CellInfo& a_cell, const std::vector<home::CellInfo>& a_context)
	{
		SceneDesc desc;
		desc.interior = a_cell.interior;
		int contextIndex = -2;
		AddCell(desc, a_cell, false, contextIndex);
		for (const auto& c : a_context) {
			AddCell(desc, c, true, contextIndex);
		}
		return desc;
	}

	// ---- Camera -----------------------------------------------------------------------------------------

	XMVECTOR Camera::Forward() const
	{
		return XMVectorSet(std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch), 0.0f);
	}
	XMVECTOR Camera::Right() const { return XMVectorSet(std::sin(yaw), -std::cos(yaw), 0.0f, 0.0f); }
	XMVECTOR Camera::Up() const { return XMVector3Cross(Right(), Forward()); }
	XMMATRIX Camera::View() const { return XMMatrixLookToRH(XMLoadFloat3(&position), Forward(), XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)); }
	XMMATRIX Camera::Projection(float a_aspect) const { return XMMatrixPerspectiveFovRH(fovY, std::max(a_aspect, 0.01f), farZ, nearZ); }
	void     Camera::LookAt(const XMFLOAT3& a_target)
	{
		const float dx = a_target.x - position.x, dy = a_target.y - position.y, dz = a_target.z - position.z;
		yaw = std::atan2(dy, dx);
		pitch = std::atan2(dz, std::sqrt(dx * dx + dy * dy));
	}

	// ---- Render target ----------------------------------------------------------------------------------

	bool RenderTarget::Resize(ID3D11Device* a_device, unsigned a_width, unsigned a_height)
	{
		a_width = std::max(a_width, 8u);
		a_height = std::max(a_height, 8u);
		if (a_width == _w && a_height == _h && _rtv) {
			return true;
		}
		*this = RenderTarget{};
		UINT samples = 4, quality = 0;
		if (FAILED(a_device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, samples, &quality)) || quality == 0) {
			samples = 1;
		}
		D3D11_TEXTURE2D_DESC d{};
		d.Width = a_width;
		d.Height = a_height;
		d.MipLevels = 1;
		d.ArraySize = 1;
		d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		d.SampleDesc.Count = 1;
		d.Usage = D3D11_USAGE_DEFAULT;
		d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		if (FAILED(a_device->CreateTexture2D(&d, nullptr, &_resolved))) {
			return false;
		}
		a_device->CreateShaderResourceView(_resolved.Get(), nullptr, &_srv);
		if (samples > 1) {
			d.SampleDesc.Count = samples;
			d.BindFlags = D3D11_BIND_RENDER_TARGET;
			if (FAILED(a_device->CreateTexture2D(&d, nullptr, &_color))) {
				return false;
			}
			a_device->CreateRenderTargetView(_color.Get(), nullptr, &_rtv);
		} else {
			a_device->CreateRenderTargetView(_resolved.Get(), nullptr, &_rtv);
		}
		d.Format = DXGI_FORMAT_D32_FLOAT;
		d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		if (FAILED(a_device->CreateTexture2D(&d, nullptr, &_depth))) {
			return false;
		}
		a_device->CreateDepthStencilView(_depth.Get(), nullptr, &_dsv);
		_w = a_width;
		_h = a_height;
		return _rtv && _dsv && _srv;
	}

	// ---- Scene internals ----------------------------------------------------------------------------------

	struct Scene::Texture
	{
		std::atomic<int>                 state{ 0 };  // 0 loading, 1 ready, 2 failed
		ComPtr<ID3D11ShaderResourceView> srv;
	};

	struct Scene::Model
	{
		struct Sub
		{
			std::uint32_t            indexStart{ 0 }, indexCount{ 0 };
			std::int32_t             baseVertex{ 0 };
			std::shared_ptr<Texture> texture;  // null: untextured
			bool                     textured{ false };
			float                    alphaRef{ -1.0f };
			bool                     blend{ false };
			unsigned                 src{ 6 }, dst{ 7 };
			bool                     twoSided{ false };
			bool                     effect{ false };
			float                    vertexColors{ 0.0f };
			XMFLOAT4                 tint{ 1, 1, 1, 1 };
			XMFLOAT4                 uv{ 0, 0, 1, 1 };
		};

		std::string                path;
		std::atomic<int>           state{ 0 };  // 0 pending, 1 ready, 2 failed (drawn as a box)
		std::atomic<bool>          invisible{ false };  // read fine but nothing to draw (activator triggers)
		ComPtr<ID3D11Buffer>       vb, ib;
		std::vector<Sub>           subs;
		std::vector<XMFLOAT3>      cpuPos;
		std::vector<std::uint32_t> cpuIdx;
		XMFLOAT3                   bmin{ -0.5f, -0.5f, -0.5f }, bmax{ 0.5f, 0.5f, 0.5f };
	};

	struct Scene::Instance
	{
		std::size_t model{ 0 };  // 0: box
		XMFLOAT4X4  world{};
		XMFLOAT4X4  inverse{};
		XMFLOAT4X4  boxWorld{};  // unit box -> OBND box (or a 32-unit cube)
		XMFLOAT4X4  boxInverse{};
	};

	struct Scene::Shared
	{
		ComPtr<ID3D11VertexShader>    vs, lineVs;
		ComPtr<ID3D11PixelShader>     ps, linePs;
		ComPtr<ID3D11InputLayout>     layout, lineLayout;
		ComPtr<ID3D11Buffer>          frameCb, drawCb, lineVb;
		std::size_t                   lineCapacity{ 0 };
		ComPtr<ID3D11RasterizerState> cullBack, cullNone, wire;
		ComPtr<ID3D11DepthStencilState> depthWrite, depthRead, depthOff;
		ComPtr<ID3D11BlendState>      opaque;
		std::unordered_map<unsigned, ComPtr<ID3D11BlendState>> blends;
		ComPtr<ID3D11SamplerState>    sampler;
		ComPtr<ID3D11ShaderResourceView> white, checker;
		bool                          ok{ false };

		ID3D11BlendState* Blend(ID3D11Device* a_device, unsigned a_src, unsigned a_dst)
		{
			const unsigned key = a_src * 16 + a_dst;
			auto&          b = blends[key];
			if (!b) {
				D3D11_BLEND_DESC d{};
				d.RenderTarget[0].BlendEnable = TRUE;
				d.RenderTarget[0].SrcBlend = BlendOf(a_src);
				d.RenderTarget[0].DestBlend = BlendOf(a_dst);
				d.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
				d.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
				d.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
				d.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
				d.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
				a_device->CreateBlendState(&d, &b);
			}
			return b.Get();
		}
	};

	namespace
	{
		ComPtr<ID3D11ShaderResourceView> SolidTexture(ID3D11Device* a_device, unsigned a_size, std::uint32_t a_a, std::uint32_t a_b)
		{
			std::vector<std::uint32_t> px(static_cast<std::size_t>(a_size) * a_size);
			for (unsigned y = 0; y < a_size; ++y) {
				for (unsigned x = 0; x < a_size; ++x) {
					px[static_cast<std::size_t>(y) * a_size + x] = ((x / 4 + y / 4) % 2) ? a_b : a_a;
				}
			}
			D3D11_TEXTURE2D_DESC d{};
			d.Width = d.Height = a_size;
			d.MipLevels = 1;
			d.ArraySize = 1;
			d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			d.SampleDesc.Count = 1;
			d.Usage = D3D11_USAGE_IMMUTABLE;
			d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA init{ px.data(), a_size * 4, 0 };
			ComPtr<ID3D11Texture2D> tex;
			ComPtr<ID3D11ShaderResourceView> srv;
			if (SUCCEEDED(a_device->CreateTexture2D(&d, &init, &tex))) {
				a_device->CreateShaderResourceView(tex.Get(), nullptr, &srv);
			}
			return srv;
		}

		ComPtr<ID3DBlob> Compile(const char* a_entry, const char* a_target, std::string& a_error)
		{
			ComPtr<ID3DBlob> code, errors;
			if (FAILED(D3DCompile(kShader, std::strlen(kShader), "DovaStashEditor3D", nullptr, nullptr, a_entry, a_target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
					&code, &errors))) {
				a_error = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "shader compile failed";
				return nullptr;
			}
			return code;
		}
	}

	Scene::Scene(ID3D11Device* a_device, std::shared_ptr<Assets> a_assets, SceneDesc a_desc) :
		_device(a_device),
		_assets(std::move(a_assets)),
		_desc(std::move(a_desc)),
		_shared(std::make_unique<Shared>())
	{
		// Model 0: the unit box (placeholders, NPCs).
		auto box = std::make_unique<Model>();
		box->path = "(box)";
		{
			static const float faces[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
			std::vector<Vertex>        verts;
			std::vector<std::uint32_t> idx;
			for (const auto& n : faces) {
				// Two axes spanning the face.
				const float u[3] = { n[1] != 0 || n[2] != 0 ? 1.0f : 0.0f, n[0] != 0 ? 1.0f : 0.0f, 0.0f };
				float       v[3] = { n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0] };
				const auto  base = static_cast<std::uint32_t>(verts.size());
				for (int k = 0; k < 4; ++k) {
					const float su = (k == 1 || k == 2) ? 0.5f : -0.5f;
					const float sv = (k >= 2) ? 0.5f : -0.5f;
					Vertex      vx{};
					for (int j = 0; j < 3; ++j) {
						vx.pos[j] = n[j] * 0.5f + u[j] * su + v[j] * sv;
						vx.nrm[j] = n[j];
					}
					vx.uv[0] = su + 0.5f;
					vx.uv[1] = sv + 0.5f;
					vx.color = 0xFFFFFFFF;
					verts.push_back(vx);
					box->cpuPos.push_back({ vx.pos[0], vx.pos[1], vx.pos[2] });
				}
				for (const std::uint32_t t : { 0u, 1u, 2u, 0u, 2u, 3u, 0u, 2u, 1u, 0u, 3u, 2u }) {  // both windings: never culled
					idx.push_back(base + t);
				}
			}
			box->cpuIdx = idx;
			D3D11_BUFFER_DESC      bd{ static_cast<UINT>(verts.size() * sizeof(Vertex)), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
			D3D11_SUBRESOURCE_DATA init{ verts.data(), 0, 0 };
			_device->CreateBuffer(&bd, &init, &box->vb);
			bd = { static_cast<UINT>(idx.size() * 4), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
			init.pSysMem = idx.data();
			_device->CreateBuffer(&bd, &init, &box->ib);
			Model::Sub sub;
			sub.indexCount = static_cast<std::uint32_t>(idx.size());
			box->subs.push_back(sub);
			box->state = 1;
		}
		_models.push_back(std::move(box));

		if (!_desc.terrain.empty()) {
			_terrain = std::make_unique<Model>();
		}
		_instances.resize(_desc.refs.size());
		for (std::size_t i = 0; i < _desc.refs.size(); ++i) {
			const auto& r = _desc.refs[i];
			auto&       inst = _instances[i];
			if (!r.model.empty()) {
				auto [it, inserted] = _modelIndex.try_emplace(r.model, _models.size());
				if (inserted) {
					auto m = std::make_unique<Model>();
					m->path = r.model;
					_models.push_back(std::move(m));
				}
				inst.model = it->second;
			}
			const XMMATRIX world = XMMatrixScaling(r.scale, r.scale, r.scale) * RefRotation(r.rotation[0], r.rotation[1], r.rotation[2]) *
			                       XMMatrixTranslation(r.position[0], r.position[1], r.position[2]);
			XMStoreFloat4x4(&inst.world, world);
			XMStoreFloat4x4(&inst.inverse, XMMatrixInverse(nullptr, world));
			float ext[3], ctr[3];
			for (int k = 0; k < 3; ++k) {
				ext[k] = r.bounds[static_cast<std::size_t>(k) + 3] - r.bounds[static_cast<std::size_t>(k)];
				ctr[k] = (r.bounds[static_cast<std::size_t>(k) + 3] + r.bounds[static_cast<std::size_t>(k)]) * 0.5f;
			}
			if (ext[0] < 1.0f && ext[1] < 1.0f && ext[2] < 1.0f) {
				ext[0] = ext[1] = ext[2] = 32.0f;
				ctr[0] = ctr[1] = 0.0f;
				ctr[2] = 16.0f;
			}
			for (float& e : ext) {
				e = std::max(e, 2.0f);
			}
			const XMMATRIX boxWorld = XMMatrixScaling(ext[0], ext[1], ext[2]) * XMMatrixTranslation(ctr[0], ctr[1], ctr[2]) * world;
			XMStoreFloat4x4(&inst.boxWorld, boxWorld);
			XMStoreFloat4x4(&inst.boxInverse, XMMatrixInverse(nullptr, boxWorld));
		}
	}

	Scene::~Scene()
	{
		for (auto& w : _workers) {
			w.request_stop();
		}
		_workers.clear();  // joins
	}

	void Scene::Start()
	{
		if (!_workers.empty()) {
			return;
		}
		const auto models = _models.size() - 1;
		const auto threads = std::clamp<std::size_t>(std::thread::hardware_concurrency(), 2, 8);
		const auto count = std::max<std::size_t>(1, std::min(threads, models));
		_workersLeft = static_cast<int>(count);
		for (std::size_t t = 0; t < count; ++t) {
			_workers.emplace_back([this](std::stop_token a_stop) { LoaderThread(a_stop); });
		}
	}

	void Scene::Wait()
	{
		for (auto& w : _workers) {
			if (w.joinable()) {
				w.join();
			}
		}
	}

	LoadProgress Scene::Progress() const
	{
		LoadProgress p;
		p.modelsDone = _modelsDone;
		p.modelsTotal = _models.size() - 1;
		p.texturesDone = _texturesDone;
		p.texturesTotal = _texturesTotal;
		p.archivesDone = _archivesDone;
		p.archivesTotal = _archivesTotal;
		p.missingModels = _missingModels;
		p.missingTextures = _missingTextures;
		p.finished = _finished;
		switch (_stage.load()) {
		case 0:
			p.stage = "Opening archives";
			break;
		case 1:
			p.stage = "Loading meshes and textures";
			break;
		default:
			p.stage = "Done";
			break;
		}
		return p;
	}

	std::vector<std::string> Scene::Problems() const
	{
		std::scoped_lock lock(_mutex);
		return _problems;
	}

	void Scene::LoaderThread(std::stop_token a_stop)
	{
		try {
			_assets->OpenArchives([this](std::size_t a_done, std::size_t a_total) {
				_archivesDone = a_done;
				_archivesTotal = a_total;
			});
			_stage = std::max(_stage.load(), 1);
			BuildTerrain();
			for (std::size_t slot = _nextModel++; slot < _models.size() && !a_stop.stop_requested(); slot = _nextModel++) {
				LoadModel(slot);
				++_modelsDone;
			}
		} catch (const std::exception& e) {
			std::scoped_lock lock(_mutex);
			_problems.push_back(std::string("loader: ") + e.what());
		}
		if (--_workersLeft == 0) {
			_stage = 2;
			_finished = true;
		}
	}

	std::shared_ptr<Scene::Texture> Scene::GetTexture(const std::string& a_path)
	{
		std::shared_ptr<Texture> tex;
		{
			std::scoped_lock lock(_mutex);
			auto [it, inserted] = _textures.try_emplace(a_path);
			if (!inserted) {
				return it->second;
			}
			it->second = std::make_shared<Texture>();
			tex = it->second;
			++_texturesTotal;
		}
		std::string error;
		if (const auto data = _assets->Read(a_path)) {
			DdsInfo info;
			tex->srv.Attach(LoadDds(_device.Get(), *data, kMaxTextureSize, info, error));
		} else {
			error = "not found";
		}
		if (tex->srv) {
			tex->state = 1;
		} else {
			tex->state = 2;
			++_missingTextures;
			std::scoped_lock lock(_mutex);
			if (_problems.size() < 200) {
				_problems.push_back(std::format("texture {}: {}", a_path, error));
			}
		}
		++_texturesDone;
		return tex;
	}

	void Scene::LoadModel(std::size_t a_slot)
	{
		auto&       m = *_models[a_slot];
		std::string error;
		const auto  data = _assets->Read(m.path);
		std::optional<NifModel> nif;
		if (!data) {
			error = "not found";
		} else {
			nif = ParseNif(*data, error);
			if (nif && nif->Empty()) {
				if (nif->warnings.empty()) {
					m.invisible = true;  // collision-only activators (rack and plaque triggers): a see-through box, not a problem
					m.state = 2;
					return;
				}
				error = "no drawable shapes (" + nif->warnings.front() + ")";
				nif.reset();
			}
		}
		if (!nif) {
			m.state = 2;
			++_missingModels;
			std::scoped_lock lock(_mutex);
			if (_problems.size() < 200) {
				_problems.push_back(std::format("mesh {}: {}", m.path, error));
			}
			return;
		}
		if (!nif->warnings.empty()) {
			std::scoped_lock lock(_mutex);
			if (_problems.size() < 200) {
				_problems.push_back(std::format("mesh {}: {}", m.path, nif->warnings.front()));
			}
		}
		std::vector<Vertex>        verts;
		std::vector<std::uint32_t> idx;
		for (auto& s : nif->shapes) {
			if (s.normals.empty()) {
				ComputeNormals(s.positions, s.indices, s.normals);
			}
			Model::Sub sub;
			sub.indexStart = static_cast<std::uint32_t>(idx.size());
			sub.indexCount = static_cast<std::uint32_t>(s.indices.size());
			sub.baseVertex = static_cast<std::int32_t>(verts.size());
			if (!s.diffuse.empty()) {
				sub.texture = GetTexture(Assets::NormalizeTexture(s.diffuse));
				sub.textured = true;
			}
			sub.alphaRef = s.alphaTest ? static_cast<float>(s.alphaThreshold) / 255.0f : -1.0f;
			sub.effect = s.effect;
			sub.blend = s.effect || (s.alphaBlend && !s.alphaTest);
			if (s.alphaFlags & 1) {
				sub.src = (s.alphaFlags >> 1) & 0xF;
				sub.dst = (s.alphaFlags >> 5) & 0xF;
			} else if (s.effect) {
				sub.src = 6;  // src alpha, one: additive glow
				sub.dst = 0;
			}
			if (s.effect && !s.alphaTest) {
				sub.alphaRef = 0.004f;
			}
			sub.twoSided = s.doubleSided;
			sub.vertexColors = (s.vertexColors && !s.colors.empty()) ? (s.vertexAlpha ? 2.0f : 1.0f) : 0.0f;
			// Effects (fog, glows, light rays) at half strength: the editor has no scene lighting to balance them.
			sub.tint = { s.tint[0], s.tint[1], s.tint[2], s.tint[3] * s.alpha * (s.effect ? 0.5f : 1.0f) };
			sub.uv = { s.uvOffset[0], s.uvOffset[1], s.uvScale[0], s.uvScale[1] };
			const auto nv = s.positions.size() / 3;
			const auto base = static_cast<std::uint32_t>(m.cpuPos.size());
			for (std::size_t v = 0; v < nv; ++v) {
				Vertex vx{};
				std::memcpy(vx.pos, &s.positions[v * 3], 12);
				std::memcpy(vx.nrm, &s.normals[v * 3], 12);
				if (!s.uvs.empty()) {
					vx.uv[0] = s.uvs[v * 2];
					vx.uv[1] = s.uvs[v * 2 + 1];
				}
				vx.color = s.colors.empty() ? 0xFFFFFFFF : s.colors[v];
				verts.push_back(vx);
				m.cpuPos.push_back({ vx.pos[0], vx.pos[1], vx.pos[2] });
			}
			for (const auto i : s.indices) {
				idx.push_back(i);
				if (!s.effect) {
					m.cpuIdx.push_back(base + i);
				}
			}
			m.subs.push_back(std::move(sub));
		}
		m.bmin = { nif->boundsMin[0], nif->boundsMin[1], nif->boundsMin[2] };
		m.bmax = { nif->boundsMax[0], nif->boundsMax[1], nif->boundsMax[2] };
		D3D11_BUFFER_DESC      bd{ static_cast<UINT>(verts.size() * sizeof(Vertex)), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
		D3D11_SUBRESOURCE_DATA init{ verts.data(), 0, 0 };
		HRESULT                hr = _device->CreateBuffer(&bd, &init, &m.vb);
		if (SUCCEEDED(hr)) {
			bd = { static_cast<UINT>(idx.size() * 4), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
			init.pSysMem = idx.data();
			hr = _device->CreateBuffer(&bd, &init, &m.ib);
		}
		m.state = SUCCEEDED(hr) ? 1 : 2;
	}

	void Scene::BuildTerrain()
	{
		int expected = 0;
		if (!_terrain || !_terrain->state.compare_exchange_strong(expected, 3)) {
			return;  // no terrain, or another thread builds it
		}
		std::vector<Vertex>        verts;
		std::vector<std::uint32_t> idx;
		auto&                      m = *_terrain;
		for (const auto& t : _desc.terrain) {
			Model::Sub sub;
			sub.indexStart = static_cast<std::uint32_t>(idx.size());
			sub.baseVertex = static_cast<std::int32_t>(verts.size());
			sub.texture = GetTexture(t.texture);
			sub.textured = true;
			sub.vertexColors = t.colors.empty() ? 0.0f : 1.0f;
			sub.uv = { 0, 0, 1, 1 };
			const auto base = static_cast<std::uint32_t>(m.cpuPos.size());
			for (std::size_t y = 0; y < 33; ++y) {
				for (std::size_t x = 0; x < 33; ++x) {
					Vertex vx{};
					vx.pos[0] = static_cast<float>(t.gridX) * 4096.0f + static_cast<float>(x) * 128.0f;
					vx.pos[1] = static_cast<float>(t.gridY) * 4096.0f + static_cast<float>(y) * 128.0f;
					vx.pos[2] = t.heights[y * 33 + x];
					if (!t.normals.empty()) {
						std::memcpy(vx.nrm, t.normals[y * 33 + x].data(), 12);
					} else {
						vx.nrm[2] = 1.0f;
					}
					vx.uv[0] = static_cast<float>(x) / 4.0f;  // a texture repeat every 512 units
					vx.uv[1] = static_cast<float>(32 - y) / 4.0f;
					vx.color = t.colors.empty() ? 0xFFFFFFFF : t.colors[y * 33 + x];
					verts.push_back(vx);
					m.cpuPos.push_back({ vx.pos[0], vx.pos[1], vx.pos[2] });
				}
			}
			for (std::uint32_t y = 0; y < 32; ++y) {
				for (std::uint32_t x = 0; x < 32; ++x) {
					const std::uint32_t a = y * 33 + x, b = a + 1, c = a + 33, d = c + 1;
					for (const auto i : { a, b, d, a, d, c }) {
						idx.push_back(i);
						m.cpuIdx.push_back(base + i);
					}
				}
			}
			sub.indexCount = static_cast<std::uint32_t>(idx.size()) - sub.indexStart;
			m.subs.push_back(std::move(sub));
		}
		m.bmin = { FLT_MAX, FLT_MAX, FLT_MAX };
		m.bmax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
		for (const auto& p : m.cpuPos) {
			m.bmin = { std::min(m.bmin.x, p.x), std::min(m.bmin.y, p.y), std::min(m.bmin.z, p.z) };
			m.bmax = { std::max(m.bmax.x, p.x), std::max(m.bmax.y, p.y), std::max(m.bmax.z, p.z) };
		}
		D3D11_BUFFER_DESC      bd{ static_cast<UINT>(verts.size() * sizeof(Vertex)), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
		D3D11_SUBRESOURCE_DATA init{ verts.data(), 0, 0 };
		_device->CreateBuffer(&bd, &init, &m.vb);
		bd = { static_cast<UINT>(idx.size() * 4), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
		init.pSysMem = idx.data();
		_device->CreateBuffer(&bd, &init, &m.ib);
		m.state = (m.vb && m.ib) ? 1 : 2;
		std::scoped_lock lock(_mutex);
		_problems.push_back(std::format("debug terrain: {} vertices, {} indices, state {}, bounds z {:.0f}..{:.0f}", verts.size(), idx.size(), m.state.load(), m.bmin.z, m.bmax.z));
	}

	void Scene::EnsurePipeline()
	{
		auto& s = *_shared;
		if (s.ok || s.vs) {
			return;
		}
		std::string error;
		const auto  vs = Compile("VS", "vs_5_0", error);
		const auto  ps = Compile("PS", "ps_5_0", error);
		const auto  lvs = Compile("LineVS", "vs_5_0", error);
		const auto  lps = Compile("LinePS", "ps_5_0", error);
		if (!vs || !ps || !lvs || !lps) {
			std::scoped_lock lock(_mutex);
			_problems.push_back("shaders: " + error);
			return;
		}
		_device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &s.vs);
		_device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &s.ps);
		_device->CreateVertexShader(lvs->GetBufferPointer(), lvs->GetBufferSize(), nullptr, &s.lineVs);
		_device->CreatePixelShader(lps->GetBufferPointer(), lps->GetBufferSize(), nullptr, &s.linePs);
		const D3D11_INPUT_ELEMENT_DESC layout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		_device->CreateInputLayout(layout, 4, vs->GetBufferPointer(), vs->GetBufferSize(), &s.layout);
		const D3D11_INPUT_ELEMENT_DESC lineLayout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		_device->CreateInputLayout(lineLayout, 2, lvs->GetBufferPointer(), lvs->GetBufferSize(), &s.lineLayout);

		D3D11_BUFFER_DESC cb{ sizeof(FrameCB), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
		_device->CreateBuffer(&cb, nullptr, &s.frameCb);
		cb.ByteWidth = sizeof(DrawCB);
		_device->CreateBuffer(&cb, nullptr, &s.drawCb);

		D3D11_RASTERIZER_DESC rd{};
		rd.FillMode = D3D11_FILL_SOLID;
		rd.CullMode = D3D11_CULL_BACK;
		rd.FrontCounterClockwise = kFrontCounterClockwise;
		rd.DepthClipEnable = TRUE;
		rd.MultisampleEnable = TRUE;
		rd.AntialiasedLineEnable = TRUE;
		_device->CreateRasterizerState(&rd, &s.cullBack);
		rd.CullMode = D3D11_CULL_NONE;
		_device->CreateRasterizerState(&rd, &s.cullNone);
		rd.FillMode = D3D11_FILL_WIREFRAME;
		_device->CreateRasterizerState(&rd, &s.wire);

		D3D11_DEPTH_STENCIL_DESC dd{};
		dd.DepthEnable = TRUE;
		dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		dd.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;  // reversed Z
		_device->CreateDepthStencilState(&dd, &s.depthWrite);
		dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		_device->CreateDepthStencilState(&dd, &s.depthRead);
		dd.DepthEnable = FALSE;
		_device->CreateDepthStencilState(&dd, &s.depthOff);

		D3D11_BLEND_DESC bd{};
		bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		_device->CreateBlendState(&bd, &s.opaque);

		D3D11_SAMPLER_DESC sd{};
		sd.Filter = D3D11_FILTER_ANISOTROPIC;
		sd.MaxAnisotropy = 8;
		sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		sd.MaxLOD = D3D11_FLOAT32_MAX;
		_device->CreateSamplerState(&sd, &s.sampler);

		s.white = SolidTexture(_device.Get(), 4, 0xFFFFFFFF, 0xFFFFFFFF);
		s.checker = SolidTexture(_device.Get(), 16, Rgba(150, 150, 150), Rgba(110, 110, 110));
		s.ok = s.vs && s.ps && s.layout && s.frameCb && s.drawCb;
	}

	namespace
	{
		bool KindShown(RefKind a_kind, const ViewOptions& o)
		{
			switch (a_kind) {
			case RefKind::Structure:
				return o.showStructure;
			case RefKind::Container:
				return o.showContainers;
			case RefKind::Display:
				return o.showDisplays;
			case RefKind::Door:
				return o.showDoors;
			case RefKind::Furniture:
				return o.showFurniture;
			case RefKind::Actor:
				return o.showActors;
			default:
				return o.showOther;
			}
		}

		bool RefShown(const SceneRef& a_ref, const ViewOptions& o)
		{
			if (!KindShown(a_ref.kind, o) || (!o.showDisabled && a_ref.initiallyDisabled)) {
				return false;
			}
			return !o.heightCut || (a_ref.position[2] >= o.zLo && a_ref.position[2] <= o.zHi);
		}

		// Things invisible in game (marker meshes, collision-only activators) get a see-through box only when
		// a template can use them (hidden chests, rack and plaque triggers); everything else stays invisible.
		bool GhostShown(const SceneRef& a_ref)
		{
			return a_ref.selectable && (a_ref.kind == RefKind::Container || a_ref.kind == RefKind::Display || a_ref.kind == RefKind::Door);
		}

		// The 8 corners of a local box under a world matrix.
		std::array<XMFLOAT3, 8> Corners(const XMFLOAT3& a_min, const XMFLOAT3& a_max, const XMFLOAT4X4& a_world)
		{
			std::array<XMFLOAT3, 8> out{};
			const XMMATRIX          w = XMLoadFloat4x4(&a_world);
			for (int k = 0; k < 8; ++k) {
				const XMVECTOR p = XMVectorSet((k & 1) ? a_max.x : a_min.x, (k & 2) ? a_max.y : a_min.y, (k & 4) ? a_max.z : a_min.z, 1.0f);
				XMStoreFloat3(&out[static_cast<std::size_t>(k)], XMVector3TransformCoord(p, w));
			}
			return out;
		}
	}

	void Scene::Render(ID3D11DeviceContext* a_ctx, RenderTarget& a_target, const Camera& a_camera, const ViewOptions& a_options)
	{
		EnsurePipeline();
		auto& s = *_shared;
		const float clear[4] = { 0.11f, 0.12f, 0.14f, 1.0f };
		a_ctx->ClearRenderTargetView(a_target._rtv.Get(), clear);
		a_ctx->ClearDepthStencilView(a_target._dsv.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
		_drawCalls = 0;
		if (!s.ok) {
			return;
		}
		const float aspect = static_cast<float>(a_target.Width()) / static_cast<float>(a_target.Height());
		const XMMATRIX viewProj = a_camera.View() * a_camera.Projection(aspect);

		FrameCB frame{};
		XMStoreFloat4x4(&frame.viewProj, viewProj);
		frame.cameraPos = { a_camera.position.x, a_camera.position.y, a_camera.position.z, 1.0f };
		const XMVECTOR light = XMVector3Normalize(XMVectorSet(0.35f, 0.25f, 1.0f, 0.0f));
		XMStoreFloat4(&frame.lightDir, light);
		const float b = std::clamp(a_options.brightness, 0.2f, 3.0f);
		frame.ambient = { 0.42f * b, 0.42f * b, 0.45f * b, 1.0f };
		frame.lightColor = { 0.55f * b, 0.52f * b, 0.48f * b, 1.0f };
		D3D11_MAPPED_SUBRESOURCE map{};
		if (SUCCEEDED(a_ctx->Map(s.frameCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
			std::memcpy(map.pData, &frame, sizeof(frame));
			a_ctx->Unmap(s.frameCb.Get(), 0);
		}

		D3D11_VIEWPORT vp{ 0.0f, 0.0f, static_cast<float>(a_target.Width()), static_cast<float>(a_target.Height()), 0.0f, 1.0f };
		a_ctx->RSSetViewports(1, &vp);
		ID3D11RenderTargetView* rtv = a_target._rtv.Get();
		a_ctx->OMSetRenderTargets(1, &rtv, a_target._dsv.Get());
		a_ctx->IASetInputLayout(s.layout.Get());
		a_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		a_ctx->VSSetShader(s.vs.Get(), nullptr, 0);
		a_ctx->PSSetShader(s.ps.Get(), nullptr, 0);
		ID3D11Buffer* cbs[2] = { s.frameCb.Get(), s.drawCb.Get() };
		a_ctx->VSSetConstantBuffers(0, 2, cbs);
		a_ctx->PSSetConstantBuffers(0, 2, cbs);
		ID3D11SamplerState* samp = s.sampler.Get();
		a_ctx->PSSetSamplers(0, 1, &samp);

		// Frustum side planes (row-vector clip = v * M).
		XMFLOAT4X4 m;
		XMStoreFloat4x4(&m, viewProj);
		XMVECTOR planes[4] = {
			XMVectorSet(m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41),
			XMVectorSet(m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41),
			XMVectorSet(m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42),
			XMVectorSet(m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42),
		};
		for (auto& p : planes) {
			p = XMPlaneNormalize(p);
		}
		const float pixelScale = static_cast<float>(a_target.Height()) / (2.0f * std::tan(a_camera.fovY * 0.5f));
		const XMVECTOR camPos = XMLoadFloat3(&a_camera.position);

		ID3D11Buffer* boundVb = nullptr;
		auto draw = [&](const Model& a_model, const Model::Sub& a_sub, const XMFLOAT4X4& a_world, XMFLOAT4 a_tint, XMFLOAT4 a_highlight, bool a_box) {
			DrawCB d{};
			d.world = a_world;
			d.tint = a_box ? a_tint : XMFLOAT4{ a_sub.tint.x * a_tint.x, a_sub.tint.y * a_tint.y, a_sub.tint.z * a_tint.z, a_sub.tint.w * a_tint.w };
			d.highlight = a_highlight;
			d.uv = a_sub.uv;
			ID3D11ShaderResourceView* srv = s.white.Get();
			bool                      textured = false;
			if (a_sub.textured && a_sub.texture) {
				const int st = a_sub.texture->state.load();
				srv = st == 1 ? a_sub.texture->srv.Get() : s.checker.Get();
				textured = true;
			}
			d.params = { a_sub.alphaRef, a_sub.vertexColors, a_sub.effect ? 1.0f : 0.0f, textured ? 1.0f : 0.0f };
			D3D11_MAPPED_SUBRESOURCE mm{};
			if (FAILED(a_ctx->Map(s.drawCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mm))) {
				return;
			}
			std::memcpy(mm.pData, &d, sizeof(d));
			a_ctx->Unmap(s.drawCb.Get(), 0);
			a_ctx->PSSetShaderResources(0, 1, &srv);
			if (boundVb != a_model.vb.Get()) {
				const UINT stride = sizeof(Vertex), offset = 0;
				ID3D11Buffer* vb = a_model.vb.Get();
				a_ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
				a_ctx->IASetIndexBuffer(a_model.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
				boundVb = vb;
			}
			a_ctx->DrawIndexed(a_sub.indexCount, a_sub.indexStart, a_sub.baseVertex);
			++_drawCalls;
		};

		struct Deferred
		{
			const Model*      model;
			const Model::Sub* sub;
			std::size_t       instance;
			float             distance;
		};
		std::vector<Deferred>    blended;
		std::vector<std::size_t> ghosts;  // see-through boxes: things invisible in game
		const XMFLOAT4        noHighlight{ 0, 0, 0, 0 };
		const XMFLOAT4        white{ 1, 1, 1, 1 };
		auto tintOf = [&](int a_index) -> std::uint32_t {
			return a_index >= 0 && static_cast<std::size_t>(a_index) < a_options.tint.size() ? a_options.tint[static_cast<std::size_t>(a_index)] : 0u;
		};
		auto isMulti = [&](int a_index) { return a_index >= 0 && std::ranges::find(a_options.multi, a_index) != a_options.multi.end(); };
		// Selection first, then hover, then the overlay colour (added light, so the texture stays readable).
		auto highlightOf = [&](int a_index) -> XMFLOAT4 {
			if (a_index == a_options.selected || isMulti(a_index)) {
				return { 0.30f, 0.22f, 0.02f, 0.0f };
			}
			if (a_index == a_options.hovered) {
				return { 0.12f, 0.12f, 0.14f, 0.0f };
			}
			if (const auto t = tintOf(a_index)) {
				const auto c = ColorF(t);
				return { c.x * 0.45f, c.y * 0.45f, c.z * 0.45f, 0.0f };
			}
			return noHighlight;
		};

		a_ctx->OMSetBlendState(s.opaque.Get(), nullptr, 0xFFFFFFFF);
		a_ctx->OMSetDepthStencilState(s.depthWrite.Get(), 0);

		// Terrain
		if (a_options.showTerrain && _terrain && _terrain->state == 1) {
			XMFLOAT4X4 id;
			XMStoreFloat4x4(&id, XMMatrixIdentity());
			a_ctx->RSSetState(a_options.wireframe ? s.wire.Get() : s.cullNone.Get());
			for (const auto& sub : _terrain->subs) {
				draw(*_terrain, sub, id, white, noHighlight, false);
			}
		}

		for (std::size_t i = 0; i < _instances.size(); ++i) {
			const auto& ref = _desc.refs[i];
			if (!RefShown(ref, a_options)) {
				continue;
			}
			const auto& inst = _instances[i];
			const auto& model = *_models[inst.model];
			const int   state = model.state.load(std::memory_order_acquire);
			const bool  box = inst.model == 0 || state == 2;
			if (!box && state != 1) {
				continue;  // still loading
			}
			// Bounding sphere for culling.
			const XMFLOAT3& bmin = box ? _models[0]->bmin : model.bmin;
			const XMFLOAT3& bmax = box ? _models[0]->bmax : model.bmax;
			const XMFLOAT4X4& world = box ? inst.boxWorld : inst.world;
			const XMVECTOR  center = XMVector3TransformCoord(XMVectorSet((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f, (bmin.z + bmax.z) * 0.5f, 1.0f),
				 XMLoadFloat4x4(&world));
			const XMVECTOR  edge = XMVector3TransformCoord(XMLoadFloat3(&bmax), XMLoadFloat4x4(&world));
			const float     radius = XMVectorGetX(XMVector3Length(XMVectorSubtract(edge, center))) + 1.0f;
			bool            outside = false;
			for (const auto& p : planes) {
				if (XMVectorGetX(XMPlaneDotCoord(p, center)) < -radius) {
					outside = true;
					break;
				}
			}
			if (outside) {
				continue;
			}
			const float dist = XMVectorGetX(XMVector3Length(XMVectorSubtract(center, camPos)));
			if (dist > radius && radius * pixelScale / dist < 0.75f && ref.index != a_options.selected) {
				continue;  // smaller than a pixel
			}
			const XMFLOAT4 highlight = highlightOf(ref.index);
			if (box) {
				if (ref.ghost || model.invisible) {
					if (GhostShown(ref)) {
						ghosts.push_back(i);
					}
					continue;
				}
				a_ctx->RSSetState(a_options.wireframe ? s.wire.Get() : s.cullBack.Get());
				const auto c = ColorF(ref.color);
				draw(*_models[0], _models[0]->subs[0], inst.boxWorld, { c.x, c.y, c.z, 1.0f }, highlight, true);
				continue;
			}
			for (const auto& sub : model.subs) {
				if (sub.effect && !a_options.showEffects) {
					continue;
				}
				if (sub.blend) {
					blended.push_back({ &model, &sub, i, dist });
					continue;
				}
				a_ctx->RSSetState(a_options.wireframe ? s.wire.Get() : (sub.twoSided ? s.cullNone.Get() : s.cullBack.Get()));
				draw(model, sub, inst.world, white, highlight, false);
			}
		}

		// Blended shapes, far to near, without writing depth.
		std::ranges::sort(blended, [](const Deferred& a, const Deferred& b) { return a.distance > b.distance; });
		a_ctx->OMSetDepthStencilState(s.depthRead.Get(), 0);
		for (const auto& d : blended) {
			const auto& ref = _desc.refs[d.instance];
			a_ctx->OMSetBlendState(s.Blend(_device.Get(), d.sub->src, d.sub->dst), nullptr, 0xFFFFFFFF);
			a_ctx->RSSetState(a_options.wireframe ? s.wire.Get() : (d.sub->twoSided ? s.cullNone.Get() : s.cullBack.Get()));
			draw(*d.model, *d.sub, _instances[d.instance].world, white, highlightOf(ref.index), false);
		}
		a_ctx->OMSetBlendState(s.Blend(_device.Get(), 6, 7), nullptr, 0xFFFFFFFF);
		a_ctx->RSSetState(s.cullBack.Get());
		for (const auto i : ghosts) {
			const auto& ref = _desc.refs[i];
			const auto  t = tintOf(ref.index);
			const auto  c = ColorF(t ? t : ref.color);
			XMFLOAT4    highlight = highlightOf(ref.index);
			// A faint fill for small things (a hidden chest, a rack trigger); trigger volumes the size of a room
			// only get their outline.
			const float extent = std::max({ ref.bounds[3] - ref.bounds[0], ref.bounds[4] - ref.bounds[1], ref.bounds[5] - ref.bounds[2] }) * ref.scale;
			const bool  big = extent > 300.0f;
			if (!big || ref.index == a_options.selected || ref.index == a_options.hovered) {
				draw(*_models[0], _models[0]->subs[0], _instances[i].boxWorld, { c.x, c.y, c.z, big ? 0.10f : 0.22f }, highlight, true);
			}
		}
		a_ctx->OMSetBlendState(s.opaque.Get(), nullptr, 0xFFFFFFFF);

		// Lines: selection and hover boxes, markers.
		std::vector<LineVertex> lines;     // drawn behind walls too: selection, hover
		std::vector<LineVertex> occluded;  // hidden by what stands in front: markers, see-through boxes
		auto addBox = [&](std::size_t a_i, std::uint32_t a_color, std::vector<LineVertex>& a_out) {
			const auto& inst = _instances[a_i];
			const auto& model = *_models[inst.model];
			const bool  box = inst.model == 0 || model.state.load() != 1;
			const auto  c = box ? Corners(_models[0]->bmin, _models[0]->bmax, inst.boxWorld) : Corners(model.bmin, model.bmax, inst.world);
			static constexpr int edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
			for (const auto& e : edges) {
				for (const int k : e) {
					const auto& p = c[static_cast<std::size_t>(k)];
					a_out.push_back({ { p.x, p.y, p.z }, a_color });
				}
			}
		};
		for (std::size_t i = 0; i < _instances.size(); ++i) {
			const auto& ref = _desc.refs[i];
			if (!RefShown(ref, a_options)) {
				continue;
			}
			if (ref.index == a_options.selected || isMulti(ref.index)) {
				addBox(i, Rgba(255, 230, 120), lines);
			} else if (ref.index == a_options.hovered) {
				addBox(i, Rgba(220, 220, 220, 200), lines);
			} else if (const auto t = tintOf(ref.index)) {
				addBox(i, (t & 0x00FFFFFF) | 0xE0000000, lines);
			} else if ((a_options.showMarkers && ref.selectable) || ((ref.ghost || _models[_instances[i].model]->invisible) && GhostShown(ref))) {
				addBox(i, (ref.color & 0x00FFFFFF) | 0xC0000000, occluded);
			}
		}
		const std::size_t occludedCount = occluded.size();
		occluded.insert(occluded.end(), lines.begin(), lines.end());
		lines = std::move(occluded);
		if (!lines.empty() && s.lineVs) {
			if (lines.size() > s.lineCapacity) {
				s.lineVb.Reset();
				s.lineCapacity = std::max<std::size_t>(lines.size(), 1024);
				D3D11_BUFFER_DESC bd{ static_cast<UINT>(s.lineCapacity * sizeof(LineVertex)), D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
				_device->CreateBuffer(&bd, nullptr, &s.lineVb);
			}
			D3D11_MAPPED_SUBRESOURCE lm{};
			if (s.lineVb && SUCCEEDED(a_ctx->Map(s.lineVb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &lm))) {
				std::memcpy(lm.pData, lines.data(), lines.size() * sizeof(LineVertex));
				a_ctx->Unmap(s.lineVb.Get(), 0);
				const UINT    stride = sizeof(LineVertex), offset = 0;
				ID3D11Buffer* vb = s.lineVb.Get();
				a_ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
				a_ctx->IASetInputLayout(s.lineLayout.Get());
				a_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
				a_ctx->VSSetShader(s.lineVs.Get(), nullptr, 0);
				a_ctx->PSSetShader(s.linePs.Get(), nullptr, 0);
				a_ctx->RSSetState(s.cullNone.Get());
				a_ctx->OMSetBlendState(s.Blend(_device.Get(), 6, 7), nullptr, 0xFFFFFFFF);
				a_ctx->OMSetDepthStencilState(s.depthRead.Get(), 0);
				if (occludedCount > 0) {
					a_ctx->Draw(static_cast<UINT>(occludedCount), 0);
				}
				a_ctx->OMSetDepthStencilState(s.depthOff.Get(), 0);
				if (lines.size() > occludedCount) {
					a_ctx->Draw(static_cast<UINT>(lines.size() - occludedCount), static_cast<UINT>(occludedCount));
				}
				a_ctx->OMSetBlendState(s.opaque.Get(), nullptr, 0xFFFFFFFF);
			}
		}

		if (a_target._color) {
			a_ctx->ResolveSubresource(a_target._resolved.Get(), 0, a_target._color.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
		}
		ID3D11ShaderResourceView* nullSrv = nullptr;
		a_ctx->PSSetShaderResources(0, 1, &nullSrv);
		a_ctx->OMSetRenderTargets(0, nullptr, nullptr);
	}

	std::optional<PickResult> Scene::Pick(FXMVECTOR a_origin, FXMVECTOR a_dir, const ViewOptions& a_options, bool a_selectableOnly, int a_ignore) const
	{
		struct Hit
		{
			std::size_t instance;
			float       t;
		};
		std::vector<Hit> hits;
		for (std::size_t i = 0; i < _instances.size(); ++i) {
			const auto& ref = _desc.refs[i];
			if (!RefShown(ref, a_options) || ref.index == a_ignore) {
				continue;
			}
			const auto& inst = _instances[i];
			const auto& model = *_models[inst.model];
			const int   state = model.state.load(std::memory_order_acquire);
			const bool  box = inst.model == 0 || state == 2;
			if (!box && state != 1) {
				continue;
			}
			if (box && (ref.ghost || model.invisible) && (a_ignore >= 0 || !GhostShown(ref))) {
				continue;  // see-through boxes do not block the view; invisible things cannot be clicked
			}
			const Model&    m = box ? *_models[0] : model;
			const XMMATRIX  inv = XMLoadFloat4x4(box ? &inst.boxInverse : &inst.inverse);
			const XMVECTOR  o = XMVector3TransformCoord(a_origin, inv);
			const XMVECTOR  d = XMVector3TransformNormal(a_dir, inv);
			float           tBox = 0.0f;
			if (!RayBox(o, d, m.bmin, m.bmax, tBox)) {
				continue;
			}
			XMFLOAT3 of, df;
			XMStoreFloat3(&of, o);
			XMStoreFloat3(&df, d);
			float best = FLT_MAX;
			for (std::size_t t = 0; t + 2 < m.cpuIdx.size(); t += 3) {
				float tt;
				if (RayTriangle(of, df, m.cpuPos[m.cpuIdx[t]], m.cpuPos[m.cpuIdx[t + 1]], m.cpuPos[m.cpuIdx[t + 2]], tt) && tt < best) {
					best = tt;
				}
			}
			if (best < FLT_MAX) {
				hits.push_back({ i, best });
			}
		}
		if (_terrain && _terrain->state == 1 && a_options.showTerrain) {
			XMFLOAT3 of, df;
			XMStoreFloat3(&of, a_origin);
			XMStoreFloat3(&df, a_dir);
			float tBox = 0.0f;
			if (RayBox(a_origin, a_dir, _terrain->bmin, _terrain->bmax, tBox)) {
				float best = FLT_MAX;
				for (std::size_t t = 0; t + 2 < _terrain->cpuIdx.size(); t += 3) {
					float tt;
					if (RayTriangle(of, df, _terrain->cpuPos[_terrain->cpuIdx[t]], _terrain->cpuPos[_terrain->cpuIdx[t + 1]],
							_terrain->cpuPos[_terrain->cpuIdx[t + 2]], tt) && tt < best) {
						best = tt;
					}
				}
				if (best < FLT_MAX) {
					hits.push_back({ SIZE_MAX, best });
				}
			}
		}
		if (hits.empty()) {
			return std::nullopt;
		}
		std::ranges::sort(hits, [](const Hit& a, const Hit& b) { return a.t < b.t; });
		const Hit* chosen = nullptr;
		if (!a_selectableOnly) {
			for (const auto& h : hits) {
				if (h.instance != SIZE_MAX) {
					chosen = &h;
					break;
				}
			}
		} else {
			// What select mode can pick, unless something stands well in front of it (a wall). Clutter lying
			// on a chest does not hide the chest.
			const float front = hits.front().t;
			for (const auto& h : hits) {
				if (h.t > front + 40.0f) {
					break;
				}
				if (h.instance != SIZE_MAX && _desc.refs[h.instance].selectable) {
					chosen = &h;
					break;
				}
			}
		}
		if (!chosen) {
			return std::nullopt;
		}
		PickResult r;
		r.index = _desc.refs[chosen->instance].index;
		r.distance = chosen->t;
		XMStoreFloat3(&r.point, XMVectorAdd(a_origin, XMVectorScale(a_dir, chosen->t)));
		return r;
	}

	std::optional<std::pair<XMFLOAT3, XMFLOAT3>> Scene::Bounds(int a_index) const
	{
		for (std::size_t i = 0; i < _instances.size(); ++i) {
			if (_desc.refs[i].index != a_index) {
				continue;
			}
			const auto& inst = _instances[i];
			const auto& model = *_models[inst.model];
			const bool  box = inst.model == 0 || model.state.load() != 1;
			const auto  c = box ? Corners(_models[0]->bmin, _models[0]->bmax, inst.boxWorld) : Corners(model.bmin, model.bmax, inst.world);
			XMFLOAT3    lo{ FLT_MAX, FLT_MAX, FLT_MAX }, hi{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
			for (const auto& p : c) {
				lo = { std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z) };
				hi = { std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z) };
			}
			return std::pair{ lo, hi };
		}
		return std::nullopt;
	}

	std::vector<std::string> Scene::BoundsMismatches(float a_tolerance, std::size_t& a_checked) const
	{
		std::vector<std::string>        out;
		std::unordered_map<std::size_t, bool> seen;
		a_checked = 0;
		for (std::size_t i = 0; i < _instances.size(); ++i) {
			const auto& r = _desc.refs[i];
			const auto  mi = _instances[i].model;
			if (mi == 0 || _models[mi]->state.load() != 1 || seen.contains(mi)) {
				continue;
			}
			const float* b = r.bounds.data();
			if (b[3] - b[0] < 1.0f && b[4] - b[1] < 1.0f) {
				continue;
			}
			seen[mi] = true;
			++a_checked;
			const auto& m = *_models[mi];
			const float d = std::max({ std::abs(m.bmin.x - b[0]), std::abs(m.bmin.y - b[1]), std::abs(m.bmin.z - b[2]), std::abs(m.bmax.x - b[3]),
				std::abs(m.bmax.y - b[4]), std::abs(m.bmax.z - b[5]) });
			if (d > a_tolerance) {
				out.push_back(std::format("{}: mesh ({:.0f} {:.0f} {:.0f})..({:.0f} {:.0f} {:.0f}) vs OBND ({:.0f} {:.0f} {:.0f})..({:.0f} {:.0f} {:.0f})", m.path, m.bmin.x,
					m.bmin.y, m.bmin.z, m.bmax.x, m.bmax.y, m.bmax.z, b[0], b[1], b[2], b[3], b[4], b[5]));
			}
		}
		return out;
	}

	std::pair<XMFLOAT3, XMFLOAT3> Scene::SceneBounds() const
	{
		XMFLOAT3 lo{ FLT_MAX, FLT_MAX, FLT_MAX }, hi{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
		bool     any = false;
		// Robust bounds: the 2nd to 98th percentile on each axis (a stray reference far below the cell, a
		// forgotten marker, must not shrink the home to a dot), padded by a tenth of the span.
		std::array<std::vector<float>, 3> axes;
		for (const auto& r : _desc.refs) {
			if (r.kind == RefKind::Other || r.index < 0) {
				continue;  // neighbour cells are not part of the overview
			}
			for (std::size_t k = 0; k < 3; ++k) {
				axes[k].push_back(r.position[k]);
			}
		}
		if (!axes[0].empty()) {
			float l[3], h[3];
			for (std::size_t k = 0; k < 3; ++k) {
				auto& v = axes[k];
				std::ranges::sort(v);
				const auto  n = v.size();
				const float a = v[static_cast<std::size_t>(static_cast<double>(n - 1) * 0.02)];
				const float b = v[static_cast<std::size_t>(static_cast<double>(n - 1) * 0.98 + 0.5)];
				const float pad = (b - a) * 0.1f;
				l[k] = a - pad;
				h[k] = b + pad;
			}
			lo = { l[0], l[1], l[2] };
			hi = { h[0], h[1], h[2] };
			any = true;
		}
		for (const auto& t : _desc.terrain) {
			if (t.context) {
				continue;
			}
			const float x0 = static_cast<float>(t.gridX) * 4096.0f, y0 = static_cast<float>(t.gridY) * 4096.0f;
			lo = { std::min(lo.x, x0), std::min(lo.y, y0), lo.z };
			hi = { std::max(hi.x, x0 + 4096.0f), std::max(hi.y, y0 + 4096.0f), hi.z };
			for (const float h : t.heights) {
				lo.z = std::min(lo.z, h);
				hi.z = std::max(hi.z, h);
			}
			any = true;
		}
		if (!any) {
			return { { -512, -512, -512 }, { 512, 512, 512 } };
		}
		return { lo, hi };
	}

	Camera Scene::OverviewCamera(float a_aspect) const
	{
		auto [lo, hi] = SceneBounds();
		const XMFLOAT3 c{ (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f };
		const float    radius = std::max(256.0f, 0.5f * std::sqrt((hi.x - lo.x) * (hi.x - lo.x) + (hi.y - lo.y) * (hi.y - lo.y) + (hi.z - lo.z) * (hi.z - lo.z)));
		Camera         cam;
		const float    fov = std::min(cam.fovY, 2.0f * std::atan(std::tan(cam.fovY * 0.5f) * a_aspect));
		const float    dist = radius / std::sin(fov * 0.5f);
		cam.yaw = std::numbers::pi_v<float> * 0.25f;  // from the south-west, looking north-east
		cam.pitch = -std::numbers::pi_v<float> * 0.28f;
		const XMVECTOR f = cam.Forward();
		XMStoreFloat3(&cam.position, XMVectorSubtract(XMLoadFloat3(&c), XMVectorScale(f, dist)));
		return cam;
	}

	Camera Scene::FrameCamera(const Camera& a_from, int a_index, float a_aspect, const ViewOptions& a_options) const
	{
		const auto b = Bounds(a_index);
		if (!b) {
			return a_from;
		}
		const auto& [lo, hi] = *b;
		const XMFLOAT3 c{ (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f };
		const float    radius = std::max(24.0f, 0.5f * std::sqrt((hi.x - lo.x) * (hi.x - lo.x) + (hi.y - lo.y) * (hi.y - lo.y) + (hi.z - lo.z) * (hi.z - lo.z)));
		Camera         cam = a_from;
		const float    fov = std::min(cam.fovY, 2.0f * std::atan(std::tan(cam.fovY * 0.5f) * a_aspect));
		const float    want = std::max(radius / std::sin(fov * 0.5f) * 1.2f, 90.0f);
		const XMVECTOR center = XMLoadFloat3(&c);

		// Candidate directions: the current one first, then around the object, level and from above.
		struct Candidate
		{
			float yaw, pitch;
		};
		std::vector<Candidate> candidates{ { a_from.yaw, std::clamp(a_from.pitch, -0.9f, 0.2f) } };
		for (const float pitch : { -0.30f, -0.65f, -0.05f }) {
			for (int k = 0; k < 8; ++k) {
				candidates.push_back({ a_from.yaw + static_cast<float>(k) * std::numbers::pi_v<float> / 4.0f, pitch });
			}
		}
		float  bestRoom = -1.0f;
		Camera best = cam;
		for (const auto& cand : candidates) {
			Camera   test = cam;
			test.yaw = cand.yaw;
			test.pitch = cand.pitch;
			const XMVECTOR back = XMVectorNegate(test.Forward());
			// How far can the camera go back from the object before something is in the way?
			float room = want;
			if (const auto hit = Pick(center, back, a_options, false, a_index)) {
				room = std::min(room, hit->distance - 12.0f);
			}
			if (room >= want - 1.0f) {
				XMStoreFloat3(&test.position, XMVectorAdd(center, XMVectorScale(back, want)));
				return test;
			}
			if (room > bestRoom) {
				bestRoom = room;
				best = test;
				XMStoreFloat3(&best.position, XMVectorAdd(center, XMVectorScale(back, std::max(room, radius + 8.0f))));
			}
		}
		return best;
	}
}
