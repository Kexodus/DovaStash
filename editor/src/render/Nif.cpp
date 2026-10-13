#include "render/Nif.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string_view>

namespace dova::editor::render
{
	namespace
	{
		bool g_matrixColumns = false;

		struct ParseError : std::runtime_error
		{
			using std::runtime_error::runtime_error;
		};

		class Reader
		{
		public:
			Reader(std::span<const std::byte> a_data, std::size_t a_begin, std::size_t a_end) :
				_data(a_data), _pos(a_begin), _end(std::min(a_end, a_data.size()))
			{}

			template <class T>
			T Get()
			{
				Need(sizeof(T));
				T v;
				std::memcpy(&v, _data.data() + _pos, sizeof(T));
				_pos += sizeof(T);
				return v;
			}
			std::uint8_t  U8() { return Get<std::uint8_t>(); }
			std::uint16_t U16() { return Get<std::uint16_t>(); }
			std::uint32_t U32() { return Get<std::uint32_t>(); }
			std::int32_t  I32() { return Get<std::int32_t>(); }
			float         F32() { return Get<float>(); }

			void Skip(std::size_t a_n)
			{
				Need(a_n);
				_pos += a_n;
			}
			// A count read from the file, checked against what is left (each item at least `a_itemSize` bytes).
			std::uint32_t Count(std::uint32_t a_n, std::size_t a_itemSize)
			{
				if (a_itemSize > 0 && static_cast<std::uint64_t>(a_n) * a_itemSize > Left()) {
					throw ParseError(std::format("count {} runs past the block", a_n));
				}
				return a_n;
			}
			std::string Sized()  // uint32 length + chars
			{
				const auto n = Count(U32(), 1);
				std::string s(reinterpret_cast<const char*>(_data.data() + _pos), n);
				_pos += n;
				while (!s.empty() && s.back() == '\0') {
					s.pop_back();
				}
				return s;
			}
			std::string Export()  // uint8 length (with NUL) + chars
			{
				const auto n = U8();
				Need(n);
				std::string s(reinterpret_cast<const char*>(_data.data() + _pos), n);
				_pos += n;
				while (!s.empty() && s.back() == '\0') {
					s.pop_back();
				}
				return s;
			}
			const std::byte* Here() const { return _data.data() + _pos; }
			std::size_t      Pos() const { return _pos; }
			std::size_t      End() const { return _end; }
			std::size_t      Left() const { return _end - _pos; }
			void             Seek(std::size_t a_pos)
			{
				if (a_pos > _end) {
					throw ParseError("seek past the block");
				}
				_pos = a_pos;
			}

		private:
			void Need(std::size_t a_n) const
			{
				if (_pos + a_n > _end) {
					throw ParseError("read past the end of the block");
				}
			}

			std::span<const std::byte> _data;
			std::size_t                _pos;
			std::size_t                _end;
		};

		float Half(std::uint16_t a_h)
		{
			const std::uint32_t sign = (a_h >> 15) & 1;
			std::int32_t        exp = (a_h >> 10) & 0x1F;
			std::uint32_t       mant = a_h & 0x3FF;
			std::uint32_t       bits;
			if (exp == 0) {
				if (mant == 0) {
					bits = sign << 31;
				} else {
					exp = 1;
					while ((mant & 0x400) == 0) {
						mant <<= 1;
						--exp;
					}
					mant &= 0x3FF;
					bits = (sign << 31) | (static_cast<std::uint32_t>(exp + 112) << 23) | (mant << 13);
				}
			} else if (exp == 31) {
				bits = (sign << 31) | 0x7F800000u | (mant << 13);
			} else {
				bits = (sign << 31) | (static_cast<std::uint32_t>(exp + 112) << 23) | (mant << 13);
			}
			float f;
			std::memcpy(&f, &bits, 4);
			return f;
		}

		struct Transform
		{
			float r[3][3]{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
			float t[3]{ 0, 0, 0 };
			float s{ 1.0f };

			// this * child
			[[nodiscard]] Transform operator*(const Transform& c) const
			{
				Transform o;
				for (int i = 0; i < 3; ++i) {
					for (int j = 0; j < 3; ++j) {
						o.r[i][j] = r[i][0] * c.r[0][j] + r[i][1] * c.r[1][j] + r[i][2] * c.r[2][j];
					}
				}
				for (int i = 0; i < 3; ++i) {
					o.t[i] = (r[i][0] * c.t[0] + r[i][1] * c.t[1] + r[i][2] * c.t[2]) * s + t[i];
				}
				o.s = s * c.s;
				return o;
			}
			void Point(const float* a_in, float* a_out) const
			{
				for (int i = 0; i < 3; ++i) {
					a_out[i] = (r[i][0] * a_in[0] + r[i][1] * a_in[1] + r[i][2] * a_in[2]) * s + t[i];
				}
			}
			void Vector(const float* a_in, float* a_out) const
			{
				for (int i = 0; i < 3; ++i) {
					a_out[i] = r[i][0] * a_in[0] + r[i][1] * a_in[1] + r[i][2] * a_in[2];
				}
				const float len = std::sqrt(a_out[0] * a_out[0] + a_out[1] * a_out[1] + a_out[2] * a_out[2]);
				if (len > 1e-6f) {
					for (int i = 0; i < 3; ++i) {
						a_out[i] /= len;
					}
				}
			}
		};

		struct Header
		{
			std::string                headerString;
			std::uint32_t              version{ 0 };
			std::uint32_t              userVersion{ 0 };
			std::uint32_t              bsVersion{ 0 };
			std::uint32_t              numBlocks{ 0 };
			std::vector<std::string>   types;
			std::vector<std::uint16_t> typeIndex;
			std::vector<std::uint32_t> sizes;
			std::vector<std::size_t>   offsets;
			std::vector<std::string>   strings;
			std::size_t                end{ 0 };  // first byte after the last block

			[[nodiscard]] const std::string& TypeOf(std::size_t a_block) const
			{
				static const std::string none;
				if (a_block >= typeIndex.size() || typeIndex[a_block] >= types.size()) {
					return none;
				}
				return types[typeIndex[a_block]];
			}
			[[nodiscard]] std::string String(std::int32_t a_index) const
			{
				return a_index >= 0 && static_cast<std::size_t>(a_index) < strings.size() ? strings[static_cast<std::size_t>(a_index)] : std::string{};
			}
		};

		Header ReadHeader(std::span<const std::byte> a_data)
		{
			Header h;
			std::size_t nl = 0;
			while (nl < a_data.size() && nl < 128 && a_data[nl] != std::byte{ '\n' }) {
				++nl;
			}
			if (nl >= a_data.size() || nl >= 128) {
				throw ParseError("not a NIF file (no header line)");
			}
			h.headerString.assign(reinterpret_cast<const char*>(a_data.data()), nl);
			if (!h.headerString.starts_with("Gamebryo File Format") && !h.headerString.starts_with("NetImmerse File Format")) {
				throw ParseError("not a NIF file: " + h.headerString);
			}
			Reader r(a_data, nl + 1, a_data.size());
			h.version = r.U32();
			if (h.version != 0x14020007) {
				throw ParseError(std::format("NIF version 0x{:08X} is not Skyrim's 20.2.0.7", h.version));
			}
			r.U8();  // endian
			h.userVersion = r.U32();
			h.numBlocks = r.Count(r.U32(), 6);
			if (h.userVersion >= 10) {
				h.bsVersion = r.U32();
				r.Export();  // author
				if (h.bsVersion > 130) {
					r.U32();
				}
				if (h.bsVersion < 131) {
					r.Export();  // process script
				}
				r.Export();  // export script
				if (h.bsVersion >= 103) {
					r.Export();  // max file path
				}
			}
			const auto numTypes = r.Count(r.U16(), 4);
			h.types.reserve(numTypes);
			for (std::uint32_t i = 0; i < numTypes; ++i) {
				h.types.push_back(r.Sized());
			}
			h.typeIndex.resize(h.numBlocks);
			for (auto& t : h.typeIndex) {
				t = r.U16() & 0x7FFF;
			}
			h.sizes.resize(h.numBlocks);
			for (auto& s : h.sizes) {
				s = r.U32();
			}
			const auto numStrings = r.Count(r.U32(), 4);
			r.U32();  // max length
			h.strings.reserve(numStrings);
			for (std::uint32_t i = 0; i < numStrings; ++i) {
				h.strings.push_back(r.Sized());
			}
			const auto numGroups = r.Count(r.U32(), 4);
			r.Skip(static_cast<std::size_t>(numGroups) * 4);
			std::size_t pos = r.Pos();
			h.offsets.resize(h.numBlocks);
			for (std::uint32_t i = 0; i < h.numBlocks; ++i) {
				h.offsets[i] = pos;
				pos += h.sizes[i];
				if (pos > a_data.size()) {
					throw ParseError(std::format("block {} ({}) runs past the end of the file", i, h.TypeOf(i)));
				}
			}
			h.end = pos;
			return h;
		}

		bool IsNode(std::string_view a_type)
		{
			if (a_type == "BSMasterParticleSystem" || a_type == "NiBone") {
				return a_type == "NiBone";
			}
			return a_type.ends_with("Node") || a_type == "BSOrderedNode" || a_type == "BSTreeNode";
		}
		bool IsBSTriShape(std::string_view a_type)
		{
			return a_type == "BSTriShape" || a_type == "BSSubIndexTriShape" || a_type == "BSMeshLODTriShape" || a_type == "BSDynamicTriShape";
		}
		bool IsNiGeometry(std::string_view a_type)
		{
			return a_type == "NiTriShape" || a_type == "NiTriStrips" || a_type == "BSLODTriShape" || a_type == "BSSegmentedTriShape";
		}

		constexpr std::uint32_t kSF1VertexAlpha = 1u << 3;
		constexpr std::uint32_t kSF2DoubleSided = 1u << 4;
		constexpr std::uint32_t kSF2VertexColors = 1u << 5;

		struct AVObject
		{
			std::string   name;
			std::uint32_t flags{ 0 };
			Transform     local;
		};

		class Parser
		{
		public:
			Parser(std::span<const std::byte> a_data, Header a_header) :
				_data(a_data), _h(std::move(a_header)), _visiting(_h.numBlocks, false)
			{}

			NifModel Run()
			{
				if (_h.numBlocks > 0) {
					Visit(0, Transform{}, 0);
				}
				_model.ComputeBounds();
				return std::move(_model);
			}

			std::vector<std::string> log;  // per block notes (DescribeNif)
			bool                     verbose{ false };

		private:
			Reader Block(std::size_t a_index) const { return Reader(_data, _h.offsets[a_index], _h.offsets[a_index] + _h.sizes[a_index]); }

			void Warn(std::string a_text)
			{
				if (_model.warnings.size() < 50) {
					_model.warnings.push_back(std::move(a_text));
				}
			}

			void ReadObjectNET(Reader& r, std::string* a_name = nullptr)
			{
				const auto name = r.I32();
				if (a_name) {
					*a_name = _h.String(name);
				}
				const auto extras = r.Count(r.U32(), 4);
				r.Skip(static_cast<std::size_t>(extras) * 4);
				r.I32();  // controller
			}

			AVObject ReadAVObject(Reader& r)
			{
				AVObject o;
				ReadObjectNET(r, &o.name);
				o.flags = _h.bsVersion > 26 ? r.U32() : r.U16();
				for (float& t : o.local.t) {
					t = r.F32();
				}
				float m[9];
				for (float& v : m) {
					v = r.F32();
				}
				for (int i = 0; i < 3; ++i) {
					for (int j = 0; j < 3; ++j) {
						o.local.r[i][j] = g_matrixColumns ? m[j * 3 + i] : m[i * 3 + j];
					}
				}
				o.local.s = r.F32();
				if (!std::isfinite(o.local.s) || o.local.s == 0.0f) {
					o.local.s = 1.0f;
				}
				if (_h.bsVersion <= 34) {
					const auto props = r.Count(r.U32(), 4);
					r.Skip(static_cast<std::size_t>(props) * 4);
				}
				r.I32();  // collision object
				return o;
			}

			static bool EditorOnly(const std::string& a_name)
			{
				return a_name.starts_with("EditorMarker") || a_name.starts_with("editormarker") || a_name == "EditorLandPlane";
			}

			void Visit(std::int32_t a_block, const Transform& a_parent, int a_depth)
			{
				if (a_block < 0 || static_cast<std::uint32_t>(a_block) >= _h.numBlocks || a_depth > 64 || _visiting[static_cast<std::size_t>(a_block)]) {
					return;
				}
				const auto  index = static_cast<std::size_t>(a_block);
				const auto& type = _h.TypeOf(index);
				_visiting[index] = true;
				try {
					if (IsNode(type)) {
						VisitNode(index, type, a_parent, a_depth);
					} else if (IsBSTriShape(type)) {
						ReadBSTriShape(index, type, a_parent);
					} else if (IsNiGeometry(type)) {
						ReadNiGeometry(index, type, a_parent);
					} else if (verbose) {
						log.push_back(std::format("block {} {}: not drawn", index, type));
					}
				} catch (const ParseError& e) {
					Warn(std::format("block {} {}: {}", index, type, e.what()));
				}
				_visiting[index] = false;
			}

			void VisitNode(std::size_t a_index, const std::string& a_type, const Transform& a_parent, int a_depth)
			{
				auto       r = Block(a_index);
				const auto av = ReadAVObject(r);
				const auto count = r.Count(r.U32(), 4);
				std::vector<std::int32_t> children(count);
				for (auto& c : children) {
					c = r.I32();
				}
				if (_h.bsVersion < 130) {
					const auto effects = r.Count(r.U32(), 4);
					r.Skip(static_cast<std::size_t>(effects) * 4);
				}
				if ((av.flags & 1) != 0 || EditorOnly(av.name)) {
					if (verbose) {
						log.push_back(std::format("block {} {} \"{}\": hidden, skipped", a_index, a_type, av.name));
					}
					return;
				}
				const Transform world = a_parent * av.local;
				if (verbose) {
					log.push_back(std::format("block {} {} \"{}\": {} children, t ({:.1f} {:.1f} {:.1f}) s {:.2f} r [{:.2f} {:.2f} {:.2f} | {:.2f} {:.2f} {:.2f} | {:.2f} {:.2f} {:.2f}]",
						a_index, a_type, av.name, count, av.local.t[0], av.local.t[1], av.local.t[2], av.local.s, av.local.r[0][0], av.local.r[0][1],
						av.local.r[0][2], av.local.r[1][0], av.local.r[1][1], av.local.r[1][2], av.local.r[2][0], av.local.r[2][1], av.local.r[2][2]));
				}
				if (a_type == "NiSwitchNode" || a_type == "NiLODNode") {
					// Switch: the active child (LOD: the closest level, child 0).
					std::uint32_t active = 0;
					if (a_type == "NiSwitchNode" && r.Left() >= 6) {
						r.U16();
						active = r.U32();
					}
					if (active < children.size()) {
						Visit(children[active], world, a_depth + 1);
					}
					return;
				}
				for (const auto c : children) {
					Visit(c, world, a_depth + 1);
				}
			}

			// Shader and alpha properties shared by both shape families.
			void ReadProperties(NifShape& a_shape, std::int32_t a_shader, std::int32_t a_alpha, bool& a_drawable)
			{
				a_drawable = true;
				if (a_shader >= 0 && static_cast<std::uint32_t>(a_shader) < _h.numBlocks) {
					const auto  si = static_cast<std::size_t>(a_shader);
					const auto& st = _h.TypeOf(si);
					try {
						auto r = Block(si);
						if (st == "BSLightingShaderProperty") {
							if (_h.bsVersion <= 130) {
								r.U32();  // shader type
							}
							ReadObjectNET(r);
							std::uint32_t f1 = 0, f2 = 0;
							if (_h.bsVersion < 130) {
								f1 = r.U32();
								f2 = r.U32();
							} else {
								throw ParseError("Fallout 4 style shader flags");
							}
							a_shape.uvOffset = { r.F32(), r.F32() };
							a_shape.uvScale = { r.F32(), r.F32() };
							const auto textureSet = r.I32();
							r.Skip(16);  // emissive color + multiple
							r.U32();     // texture clamp mode
							a_shape.alpha = std::clamp(r.F32(), 0.0f, 1.0f);
							a_shape.doubleSided = (f2 & kSF2DoubleSided) != 0;
							a_shape.vertexColors = (f2 & kSF2VertexColors) != 0;
							a_shape.vertexAlpha = (f1 & kSF1VertexAlpha) != 0;
							if (textureSet >= 0 && static_cast<std::uint32_t>(textureSet) < _h.numBlocks &&
								_h.TypeOf(static_cast<std::size_t>(textureSet)) == "BSShaderTextureSet") {
								auto       tr = Block(static_cast<std::size_t>(textureSet));
								const auto n = tr.Count(tr.U32(), 4);
								if (n > 0) {
									a_shape.diffuse = tr.Sized();
								}
							}
						} else if (st == "BSEffectShaderProperty") {
							ReadObjectNET(r);
							std::uint32_t f2 = 0;
							if (_h.bsVersion < 130) {
								r.U32();
								f2 = r.U32();
							} else {
								throw ParseError("Fallout 4 style shader flags");
							}
							a_shape.uvOffset = { r.F32(), r.F32() };
							a_shape.uvScale = { r.F32(), r.F32() };
							a_shape.diffuse = r.Sized();
							r.Skip(4);   // clamp, lighting influence, env map LOD, unused
							r.Skip(16);  // falloff
							a_shape.tint = { r.F32(), r.F32(), r.F32(), r.F32() };
							const float mult = r.F32();
							for (int i = 0; i < 3; ++i) {
								a_shape.tint[static_cast<std::size_t>(i)] = std::clamp(a_shape.tint[static_cast<std::size_t>(i)] * std::max(mult, 0.0f), 0.0f, 1.0f);
							}
							a_shape.effect = true;
							a_shape.doubleSided = true;
							a_shape.vertexColors = (f2 & kSF2VertexColors) != 0;
						} else if (st == "BSWaterShaderProperty" || st == "BSSkyShaderProperty" || st == "BSDistantTreeShaderProperty") {
							a_drawable = false;
						}
					} catch (const ParseError& e) {
						Warn(std::format("shader block {} {}: {}", si, st, e.what()));
					}
				}
				if (a_alpha >= 0 && static_cast<std::uint32_t>(a_alpha) < _h.numBlocks && _h.TypeOf(static_cast<std::size_t>(a_alpha)) == "NiAlphaProperty") {
					try {
						auto r = Block(static_cast<std::size_t>(a_alpha));
						ReadObjectNET(r);
						a_shape.alphaFlags = r.U16();
						a_shape.alphaThreshold = r.U8();
						a_shape.alphaBlend = (a_shape.alphaFlags & 0x1) != 0;
						a_shape.alphaTest = (a_shape.alphaFlags & 0x200) != 0;
					} catch (const ParseError& e) {
						Warn(std::format("alpha block {}: {}", a_alpha, e.what()));
					}
				}
			}

			// One BSVertexDataSSE / BSVertexData record.
			void ReadVertex(const std::byte* a_v, std::uint64_t a_desc, std::uint32_t a_size, NifShape& a_out, bool a_wantColors)
			{
				const auto attrs = static_cast<std::uint32_t>(a_desc >> 44);
				const bool full = _h.bsVersion == 100 || (attrs & 0x400) != 0;
				auto       get = [&](std::uint32_t off, auto& v) {
                    if (off + sizeof(v) <= a_size) {
                        std::memcpy(&v, a_v + off, sizeof(v));
                    }
				};
				float p[3]{};
				if (attrs & 0x1) {
					if (full) {
						get(0, p);
					} else {
						std::uint16_t h[3]{};
						get(0, h);
						for (int i = 0; i < 3; ++i) {
							p[i] = Half(h[i]);
						}
					}
				}
				a_out.positions.insert(a_out.positions.end(), p, p + 3);
				if (attrs & 0x2) {
					std::uint16_t uv[2]{};
					get(static_cast<std::uint32_t>((a_desc >> 8) & 0xF) * 4, uv);
					a_out.uvs.push_back(Half(uv[0]));
					a_out.uvs.push_back(Half(uv[1]));
				}
				if (attrs & 0x8) {
					std::uint8_t n[3]{};
					get(static_cast<std::uint32_t>((a_desc >> 16) & 0xF) * 4, n);
					for (int i = 0; i < 3; ++i) {
						a_out.normals.push_back(static_cast<float>(n[i]) / 255.0f * 2.0f - 1.0f);
					}
				}
				if ((attrs & 0x20) && a_wantColors) {
					std::uint32_t c = 0xFFFFFFFF;
					get(static_cast<std::uint32_t>((a_desc >> 24) & 0xF) * 4, c);
					a_out.colors.push_back(c);
				}
			}

			void Finish(NifShape&& a_shape, const Transform& a_world)
			{
				const auto nv = a_shape.positions.size() / 3;
				if (nv == 0 || a_shape.indices.empty()) {
					return;
				}
				// Drop triangles pointing outside the vertices, and degenerate ones.
				std::vector<std::uint32_t> clean;
				clean.reserve(a_shape.indices.size());
				for (std::size_t t = 0; t + 2 < a_shape.indices.size(); t += 3) {
					const auto a = a_shape.indices[t], b = a_shape.indices[t + 1], c = a_shape.indices[t + 2];
					if (a < nv && b < nv && c < nv && a != b && b != c && a != c) {
						clean.insert(clean.end(), { a, b, c });
					}
				}
				a_shape.indices = std::move(clean);
				if (a_shape.indices.empty()) {
					return;
				}
				if (a_shape.normals.size() != nv * 3) {
					a_shape.normals.clear();
				}
				if (a_shape.uvs.size() != nv * 2) {
					a_shape.uvs.clear();
				}
				if (a_shape.colors.size() != nv) {
					a_shape.colors.clear();
				}
				for (std::size_t i = 0; i < nv; ++i) {
					float p[3];
					a_world.Point(&a_shape.positions[i * 3], p);
					std::copy(p, p + 3, &a_shape.positions[i * 3]);
					if (!a_shape.normals.empty()) {
						a_world.Vector(&a_shape.normals[i * 3], p);
						std::copy(p, p + 3, &a_shape.normals[i * 3]);
					}
				}
				if (verbose) {
					log.push_back(std::format("  shape \"{}\": {} vertices, {} triangles, texture \"{}\"{}{}{}{}{}", a_shape.name, nv, a_shape.indices.size() / 3,
						a_shape.diffuse, a_shape.alphaBlend ? ", blend" : "", a_shape.alphaTest ? std::format(", alpha test {}", a_shape.alphaThreshold) : "",
						a_shape.doubleSided ? ", two-sided" : "", a_shape.effect ? ", effect" : "", a_shape.vertexColors && !a_shape.colors.empty() ? ", vertex colors" : ""));
				}
				_model.shapes.push_back(std::move(a_shape));
			}

			void ReadBSTriShape(std::size_t a_index, const std::string& a_type, const Transform& a_parent)
			{
				auto       r = Block(a_index);
				const auto av = ReadAVObject(r);
				r.Skip(16);  // bounding sphere
				if (_h.bsVersion >= 155) {
					r.Skip(24);
				}
				const auto skin = r.I32();
				const auto shader = r.I32();
				const auto alpha = r.I32();
				const auto desc = r.Get<std::uint64_t>();
				const std::uint32_t numTris = _h.bsVersion >= 130 ? r.U32() : r.U16();
				const std::uint32_t numVerts = r.U16();
				const std::uint32_t dataSize = r.U32();
				if ((av.flags & 1) != 0 || EditorOnly(av.name)) {
					if (verbose) {
						log.push_back(std::format("block {} {} \"{}\": hidden, skipped", a_index, a_type, av.name));
					}
					return;
				}
				NifShape shape;
				shape.name = av.name;
				bool drawable = true;
				ReadProperties(shape, shader, alpha, drawable);
				if (!drawable) {
					return;
				}
				const auto vsize = static_cast<std::uint32_t>(desc & 0xF) * 4;
				if (dataSize > 0) {
					// Data size counts the vertices and the triangles.
					if (vsize == 0 || static_cast<std::uint64_t>(vsize) * numVerts + static_cast<std::uint64_t>(numTris) * 6 != dataSize) {
						throw ParseError(std::format("data size {} does not match {} vertices of {} bytes and {} triangles", dataSize, numVerts, vsize, numTris));
					}
					const std::byte* base = r.Here();
					r.Skip(static_cast<std::size_t>(vsize) * numVerts);
					shape.positions.reserve(numVerts * 3);
					for (std::uint32_t v = 0; v < numVerts; ++v) {
						ReadVertex(base + static_cast<std::size_t>(v) * vsize, desc, vsize, shape, true);
					}
					r.Count(numTris, 6);
					shape.indices.reserve(numTris * 3);
					for (std::uint32_t t = 0; t < numTris * 3; ++t) {
						shape.indices.push_back(r.U16());
					}
					if (_h.bsVersion == 100 && r.Left() >= 4) {
						const auto particle = r.U32();
						if (particle > 0) {
							r.Skip(std::min<std::size_t>(r.Left(), static_cast<std::size_t>(numVerts) * 12 + static_cast<std::size_t>(numTris) * 6));
						}
					}
					if (a_type == "BSMeshLODTriShape" && r.Left() >= 12) {
						const auto lod0 = r.U32();
						if (lod0 > 0 && lod0 < numTris) {
							shape.indices.resize(static_cast<std::size_t>(lod0) * 3);
						}
					} else if (a_type == "BSDynamicTriShape" && r.Left() >= 4) {
						const auto dyn = r.U32();
						if (dyn / 16 == numVerts && r.Left() >= dyn) {
							for (std::uint32_t v = 0; v < numVerts; ++v) {
								shape.positions[v * 3 + 0] = r.F32();
								shape.positions[v * 3 + 1] = r.F32();
								shape.positions[v * 3 + 2] = r.F32();
								r.F32();
							}
						}
					}
				} else if (skin >= 0) {
					shape.skinned = true;
					ReadSkinPartition(skin, shape);
				}
				Finish(std::move(shape), a_parent * av.local);
			}

			// SE skinned shapes keep their vertices in the skin partition (BSTriShape data size 0).
			void ReadSkinPartition(std::int32_t a_skin, NifShape& a_shape)
			{
				if (static_cast<std::uint32_t>(a_skin) >= _h.numBlocks) {
					return;
				}
				const auto& st = _h.TypeOf(static_cast<std::size_t>(a_skin));
				if (st != "NiSkinInstance" && st != "BSDismemberSkinInstance") {
					return;
				}
				auto sr = Block(static_cast<std::size_t>(a_skin));
				sr.I32();  // data
				const auto part = sr.I32();
				if (part < 0 || static_cast<std::uint32_t>(part) >= _h.numBlocks || _h.TypeOf(static_cast<std::size_t>(part)) != "NiSkinPartition") {
					return;
				}
				auto       r = Block(static_cast<std::size_t>(part));
				const auto numParts = r.Count(r.U32(), 10);
				if (_h.bsVersion != 100) {
					return;  // LE partitions hold no vertex data: the NiTriShapeData has it
				}
				const auto dataSize = r.U32();
				const auto vsize = r.U32();
				const auto desc = r.Get<std::uint64_t>();
				if (vsize == 0 || dataSize % vsize != 0) {
					throw ParseError("skin partition vertex size");
				}
				const std::byte* base = r.Here();
				r.Skip(dataSize);
				const auto nv = dataSize / vsize;
				for (std::uint32_t v = 0; v < nv; ++v) {
					ReadVertex(base + static_cast<std::size_t>(v) * vsize, desc, vsize, a_shape, true);
				}
				for (std::uint32_t p = 0; p < numParts; ++p) {
					const auto pv = r.U16();
					const auto pt = r.U16();
					const auto pb = r.U16();
					const auto ps = r.U16();
					const auto pw = r.U16();
					r.Skip(static_cast<std::size_t>(pb) * 2);
					std::vector<std::uint16_t> map;
					if (r.U8()) {
						map.resize(pv);
						for (auto& m : map) {
							m = r.U16();
						}
					}
					if (r.U8()) {
						r.Skip(static_cast<std::size_t>(pv) * pw * 4);
					}
					std::vector<std::uint16_t> strips(ps);
					for (auto& s : strips) {
						s = r.U16();
					}
					std::vector<std::uint32_t> local;
					if (r.U8()) {
						if (ps == 0) {
							for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(pt) * 3; ++i) {
								local.push_back(r.U16());
							}
						} else {
							for (const auto len : strips) {
								std::vector<std::uint16_t> pts(len);
								for (auto& q : pts) {
									q = r.U16();
								}
								for (std::size_t k = 2; k < pts.size(); ++k) {
									if (k % 2 == 0) {
										local.insert(local.end(), { pts[k - 2], pts[k - 1], pts[k] });
									} else {
										local.insert(local.end(), { pts[k - 1], pts[k - 2], pts[k] });
									}
								}
							}
						}
					}
					if (r.U8()) {
						r.Skip(static_cast<std::size_t>(pv) * pw);
					}
					r.U8();  // LOD level
					r.U8();  // global VB
					r.Get<std::uint64_t>();
					std::vector<std::uint32_t> copy;
					for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(pt) * 3; ++i) {
						copy.push_back(r.U16());
					}
					if (!copy.empty()) {
						a_shape.indices.insert(a_shape.indices.end(), copy.begin(), copy.end());
					} else {
						for (const auto l : local) {
							a_shape.indices.push_back(l < map.size() ? map[l] : l);
						}
					}
				}
			}

			void ReadNiGeometry(std::size_t a_index, const std::string& a_type, const Transform& a_parent)
			{
				auto       r = Block(a_index);
				const auto av = ReadAVObject(r);
				const auto data = r.I32();
				r.I32();  // skin instance
				std::int32_t shader = -1, alpha = -1;
				{
					const auto numMat = r.Count(r.U32(), 8);
					r.Skip(static_cast<std::size_t>(numMat) * 8 + 4);  // names, extra data, active material
					r.U8();  // material needs update
					if (_h.bsVersion > 34) {
						shader = r.I32();
						alpha = r.I32();
					}
				}
				if (a_type == "NiTriShape" || a_type == "NiTriStrips") {
					if (r.Left() != 0 && _h.sizes[a_index] >= 8) {
						// Another material layout: the two property links end these blocks.
						Reader tail = Block(a_index);
						tail.Seek(tail.End() - 8);
						shader = tail.I32();
						alpha = tail.I32();
					}
				}
				if ((av.flags & 1) != 0 || EditorOnly(av.name)) {
					return;
				}
				NifShape shape;
				shape.name = av.name;
				bool drawable = true;
				ReadProperties(shape, shader, alpha, drawable);
				if (!drawable || data < 0 || static_cast<std::uint32_t>(data) >= _h.numBlocks) {
					return;
				}
				const auto& dt = _h.TypeOf(static_cast<std::size_t>(data));
				if (dt != "NiTriShapeData" && dt != "NiTriStripsData") {
					return;
				}
				// The Material CRC field: try with it, then without; keep the reading that ends at the block end.
				std::optional<NifShape> best;
				for (const bool crc : { true, false }) {
					NifShape s = shape;
					try {
						if (ReadGeometryData(static_cast<std::size_t>(data), dt == "NiTriStripsData", crc, s)) {
							best = std::move(s);
							break;
						}
						if (!best) {
							best = std::move(s);
						}
					} catch (const ParseError&) {
					}
				}
				if (best) {
					Finish(std::move(*best), a_parent * av.local);
				} else {
					Warn(std::format("block {} {}: geometry data unreadable", data, dt));
				}
			}

			// Returns true when the block was read to its exact end.
			bool ReadGeometryData(std::size_t a_index, bool a_strips, bool a_crc, NifShape& a_shape)
			{
				auto r = Block(a_index);
				r.I32();  // group id
				const std::uint32_t nv = r.U16();
				r.U8();  // keep flags
				r.U8();  // compress flags
				if (r.U8()) {
					r.Count(nv, 12);
					a_shape.positions.resize(static_cast<std::size_t>(nv) * 3);
					for (auto& p : a_shape.positions) {
						p = r.F32();
					}
				}
				const auto flags = r.U16();
				if (a_crc) {
					r.U32();
				}
				const auto hasNormals = r.U8();
				if (hasNormals > 1) {
					throw ParseError("bad normals flag");
				}
				if (hasNormals) {
					r.Count(nv, 12);
					a_shape.normals.resize(static_cast<std::size_t>(nv) * 3);
					for (auto& n : a_shape.normals) {
						n = r.F32();
					}
					if (flags & 0x1000) {
						r.Skip(static_cast<std::size_t>(nv) * 24);
					}
				}
				r.Skip(16);  // center, radius
				const auto hasColors = r.U8();
				if (hasColors > 1) {
					throw ParseError("bad colors flag");
				}
				if (hasColors) {
					r.Count(nv, 16);
					for (std::uint32_t i = 0; i < nv; ++i) {
						float c[4];
						for (float& x : c) {
							x = std::clamp(r.F32(), 0.0f, 1.0f);
						}
						a_shape.colors.push_back(static_cast<std::uint32_t>(c[0] * 255.0f + 0.5f) | (static_cast<std::uint32_t>(c[1] * 255.0f + 0.5f) << 8) |
												 (static_cast<std::uint32_t>(c[2] * 255.0f + 0.5f) << 16) | (static_cast<std::uint32_t>(c[3] * 255.0f + 0.5f) << 24));
					}
				}
				const std::uint32_t uvSets = _h.bsVersion > 0 ? (flags & 1u) : (flags & 63u);
				for (std::uint32_t s = 0; s < uvSets; ++s) {
					r.Count(nv, 8);
					for (std::uint32_t i = 0; i < nv; ++i) {
						const float u = r.F32(), v = r.F32();
						if (s == 0) {
							a_shape.uvs.push_back(u);
							a_shape.uvs.push_back(v);
						}
					}
				}
				r.U16();  // consistency
				r.I32();  // additional data
				const std::uint32_t numTris = r.U16();
				if (!a_strips) {
					r.U32();  // triangle points
					if (r.U8()) {
						r.Count(numTris, 6);
						for (std::uint32_t i = 0; i < numTris * 3; ++i) {
							a_shape.indices.push_back(r.U16());
						}
					}
					const auto groups = r.U16();
					for (std::uint32_t g = 0; g < groups; ++g) {
						const auto n = r.U16();
						r.Skip(static_cast<std::size_t>(n) * 2);
					}
				} else {
					const auto                 numStrips = r.U16();
					std::vector<std::uint16_t> lengths(numStrips);
					for (auto& l : lengths) {
						l = r.U16();
					}
					if (r.U8()) {
						for (const auto len : lengths) {
							std::vector<std::uint16_t> pts(len);
							for (auto& q : pts) {
								q = r.U16();
							}
							for (std::size_t k = 2; k < pts.size(); ++k) {
								if (k % 2 == 0) {
									a_shape.indices.insert(a_shape.indices.end(), { pts[k - 2], pts[k - 1], pts[k] });
								} else {
									a_shape.indices.insert(a_shape.indices.end(), { pts[k - 1], pts[k - 2], pts[k] });
								}
							}
						}
					}
				}
				return r.Left() == 0;
			}

			std::span<const std::byte> _data;
			Header                     _h;
			std::vector<bool>          _visiting;
			NifModel                   _model;
		};
	}

	void NifModel::ComputeBounds()
	{
		bool any = false;
		for (const auto& s : shapes) {
			for (std::size_t i = 0; i + 2 < s.positions.size(); i += 3) {
				for (std::size_t k = 0; k < 3; ++k) {
					const float v = s.positions[i + k];
					boundsMin[k] = any ? std::min(boundsMin[k], v) : v;
					boundsMax[k] = any ? std::max(boundsMax[k], v) : v;
				}
				any = true;
			}
		}
	}

	void SetNifMatrixColumns(bool a_columns) { g_matrixColumns = a_columns; }

	std::optional<NifModel> ParseNif(std::span<const std::byte> a_data, std::string& a_error)
	{
		try {
			Parser p(a_data, ReadHeader(a_data));
			return p.Run();
		} catch (const ParseError& e) {
			a_error = e.what();
		} catch (const std::exception& e) {
			a_error = e.what();
		}
		return std::nullopt;
	}

	std::string DescribeNif(std::span<const std::byte> a_data)
	{
		std::string out;
		try {
			auto h = ReadHeader(a_data);
			out += std::format("{}\nversion 0x{:08X}, user version {}, BS version {}, {} blocks, {} strings, blocks end at {} of {} bytes\n", h.headerString,
				h.version, h.userVersion, h.bsVersion, h.numBlocks, h.strings.size(), h.end, a_data.size());
			for (std::uint32_t i = 0; i < h.numBlocks; ++i) {
				out += std::format("  [{}] {} ({} bytes)\n", i, h.TypeOf(i), h.sizes[i]);
			}
			Parser p(a_data, std::move(h));
			p.verbose = true;
			const auto model = p.Run();
			out += "Parse:\n";
			for (const auto& l : p.log) {
				out += "  " + l + "\n";
			}
			for (const auto& w : model.warnings) {
				out += "  warning: " + w + "\n";
			}
			out += std::format("{} shapes; bounds ({:.1f} {:.1f} {:.1f}) .. ({:.1f} {:.1f} {:.1f})\n", model.shapes.size(), model.boundsMin[0], model.boundsMin[1],
				model.boundsMin[2], model.boundsMax[0], model.boundsMax[1], model.boundsMax[2]);
		} catch (const std::exception& e) {
			out += std::string("error: ") + e.what() + "\n";
		}
		return out;
	}
}
