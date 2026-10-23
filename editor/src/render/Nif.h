#pragma once

// A small reader for Skyrim NIF meshes (NIF 20.2.0.7: LE user version 12 / BS version 83, SE BS version
// 100), enough for the 3D view (docs/EDITOR.md §4.5): the node tree with its transforms, the triangle
// shapes (BSTriShape and its SE subclasses, NiTriShape / NiTriStrips with their data blocks, skinned SE
// shapes through NiSkinPartition), and per shape the diffuse texture, alpha and two-sidedness. Shapes that draw
// nothing (material alpha 0 with alpha on) or only a black card (textures\black.dds: Lux's shadow planes, the
// darkness behind doorways) are left out; a model of only those is invisible, as a collision-only one.
//
// Block sizes in the header let every block be skipped without knowing it, so unknown blocks never stop
// a file. Everything is bounds-checked; a malformed file gives an error, never a crash. Pure C++: no game,
// no Direct3D.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dova::editor::render
{
	struct NifShape
	{
		std::string                name;
		std::vector<float>         positions;  // xyz, model space (node transforms applied)
		std::vector<float>         normals;    // xyz, model space; empty: compute from the triangles
		std::vector<float>         uvs;        // uv; empty: none
		std::vector<std::uint32_t> colors;     // RGBA8 (r in the low byte); empty: none
		std::vector<std::uint32_t> indices;    // triangle list

		std::string diffuse;  // texture path as written in the file ("textures\\...dds"); empty: none
		bool        alphaBlend{ false };
		bool        alphaTest{ false };
		std::uint8_t alphaThreshold{ 128 };
		std::uint16_t alphaFlags{ 0 };
		bool        doubleSided{ false };
		bool        vertexColors{ false };  // the shader multiplies by the vertex colors
		bool        vertexAlpha{ false };
		bool        effect{ false };  // BSEffectShaderProperty: glow, flame, light rays (drawn additive)
		bool        skinned{ false };
		float       alpha{ 1.0f };    // material alpha
		std::array<float, 4> tint{ 1.0f, 1.0f, 1.0f, 1.0f };  // effect base color
		std::array<float, 2> uvOffset{ 0.0f, 0.0f };
		std::array<float, 2> uvScale{ 1.0f, 1.0f };
	};

	struct NifModel
	{
		std::vector<NifShape> shapes;
		std::array<float, 3>  boundsMin{ 0.0f, 0.0f, 0.0f };
		std::array<float, 3>  boundsMax{ 0.0f, 0.0f, 0.0f };
		std::vector<std::string> warnings;  // blocks or shapes left out, and why
		std::size_t              hiddenShapes{ 0 };  // shapes that draw nothing, or only a black card (light blockers): left out

		[[nodiscard]] bool Empty() const noexcept { return shapes.empty(); }
		void               ComputeBounds();
	};

	// Parses a whole NIF file. nullopt (and `a_error`) when the header or the block table is unusable.
	[[nodiscard]] std::optional<NifModel> ParseNif(std::span<const std::byte> a_data, std::string& a_error);

	// A readable dump of the header, the blocks (type, size, what the parser made of it) and the shapes:
	// the editor's --nif-info command, for checking the reader against NifSkope.
	[[nodiscard]] std::string DescribeNif(std::span<const std::byte> a_data);

	// Matrices in NIF files are read as rows (Gamebryo's in-memory NiMatrix3 order). Set to true to read
	// them as columns instead (only for experiments; see the --nif-info output).
	void SetNifMatrixColumns(bool a_columns);
}
