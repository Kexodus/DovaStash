// DovaStashEditorTests: the editor's file readers on fixtures made here (docs/EDITOR.md §5.2). No game data
// and no MO2 needed; ctest runs it (`ctest --preset editor`), or run the exe. Covered: a
// BSA (v105) read by path, its change on disk noticed, Assets re-indexing an archive that changed under it
// (the stale-archive path), loose files over archives, DDS textures (uncompressed, BC1, truncated) on a
// WARP device, and hand-built SE meshes (NIF): a textured BSTriShape (vertices, bounds, texture path), a
// collision-only model, a black card and a shape of material alpha 0 (left out: the model is invisible),
// state.json saved and read back with no --state, the game-data checks of the Checks panel
// (home::CheckTemplateRefs) on a hand-made cell, the floor heights are measured from (ui::detail::FloorOf), and the
// home finder on hand-made plugins (home::FindAllHomes: a cell a template names stays a home after an override).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>

#include "data/Assets.h"
#include "data/DataFiles.h"
#include "esp/Bsa.h"
#include "home/Drafts.h"
#include "home/RefChecks.h"
#include "render/Dds.h"
#include "render/Nif.h"
#include "ui/Common.h"
#include "ui/State.h"

namespace fs = std::filesystem;
using namespace dova::editor;
namespace core = dova::core;

namespace
{
	int g_failed = 0;
	int g_checks = 0;

	void Check(bool a_ok, std::string_view a_what)
	{
		++g_checks;
		if (!a_ok) {
			++g_failed;
			std::printf("FAIL: %.*s\n", static_cast<int>(a_what.size()), a_what.data());
		}
	}

	std::string Text(const std::optional<std::vector<std::byte>>& a_data)
	{
		return a_data ? std::string(reinterpret_cast<const char*>(a_data->data()), a_data->size()) : std::string("<none>");
	}

	template <class T>
	void Put(std::string& a_out, T a_value)
	{
		a_out.append(reinterpret_cast<const char*>(&a_value), sizeof(T));
	}

	// A v105 (SE) archive, uncompressed, folder and file names included: { "folder\\name", contents }.
	void WriteBsa(const fs::path& a_path, const std::vector<std::pair<std::string, std::string>>& a_files)
	{
		struct Folder
		{
			std::string                                       name;
			std::vector<std::pair<std::string, std::string>> files;  // name, contents
		};
		std::vector<Folder> folders;
		for (const auto& [path, contents] : a_files) {
			const auto slash = path.rfind('\\');
			const auto dir = path.substr(0, slash);
			auto       it = std::ranges::find(folders, dir, &Folder::name);
			if (it == folders.end()) {
				folders.push_back({ dir, {} });
				it = folders.end() - 1;
			}
			it->files.emplace_back(path.substr(slash + 1), contents);
		}
		std::uint32_t folderNames = 0, fileNames = 0;
		for (const auto& f : folders) {
			folderNames += static_cast<std::uint32_t>(f.name.size() + 1);
			for (const auto& file : f.files) {
				fileNames += static_cast<std::uint32_t>(file.first.size() + 1);
			}
		}
		const auto    fileCount = static_cast<std::uint32_t>(a_files.size());
		const auto    folderCount = static_cast<std::uint32_t>(folders.size());
		std::uint32_t dataStart = 36 + folderCount * 24;
		for (const auto& f : folders) {
			dataStart += static_cast<std::uint32_t>(1 + f.name.size() + 1 + f.files.size() * 16);
		}
		dataStart += fileNames;

		std::string out("BSA\0", 4);
		for (const std::uint32_t v : { 105u, 36u, 0x3u, folderCount, fileCount, folderNames, fileNames, 0u }) {
			Put(out, v);
		}
		for (const auto& f : folders) {
			Put<std::uint64_t>(out, 0);  // hash: the reader keys by name
			Put(out, static_cast<std::uint32_t>(f.files.size()));
			Put<std::uint32_t>(out, 0);
			Put<std::uint64_t>(out, 0);
		}
		std::uint32_t offset = dataStart;
		for (const auto& f : folders) {
			out.push_back(static_cast<char>(f.name.size() + 1));
			out += f.name;
			out.push_back('\0');
			for (const auto& file : f.files) {
				Put<std::uint64_t>(out, 0);
				Put(out, static_cast<std::uint32_t>(file.second.size()));
				Put(out, offset);
				offset += static_cast<std::uint32_t>(file.second.size());
			}
		}
		for (const auto& f : folders) {
			for (const auto& file : f.files) {
				out += file.first;
				out.push_back('\0');
			}
		}
		for (const auto& f : folders) {
			for (const auto& file : f.files) {
				out += file.second;
			}
		}
		std::ofstream(a_path, std::ios::binary | std::ios::trunc).write(out.data(), static_cast<std::streamsize>(out.size()));
	}

	// A 128-byte DDS header (magic included): uncompressed 32-bit BGRA, or a FourCC format.
	std::string DdsHeader(std::uint32_t a_width, std::uint32_t a_height, const char* a_fourCC)
	{
		std::string h("DDS ", 4);
		Put<std::uint32_t>(h, 124);
		Put<std::uint32_t>(h, 0x1 | 0x2 | 0x4 | 0x1000);  // caps, height, width, pixel format
		Put(h, a_height);
		Put(h, a_width);
		Put<std::uint32_t>(h, a_fourCC ? 0 : a_width * 4);
		Put<std::uint32_t>(h, 0);  // depth
		Put<std::uint32_t>(h, 1);  // mips
		for (int i = 0; i < 11; ++i) {
			Put<std::uint32_t>(h, 0);
		}
		Put<std::uint32_t>(h, 32);
		if (a_fourCC) {
			Put<std::uint32_t>(h, 0x4);
			h.append(a_fourCC, 4);
			for (int i = 0; i < 5; ++i) {
				Put<std::uint32_t>(h, 0);
			}
		} else {
			Put<std::uint32_t>(h, 0x40 | 0x1);  // RGB + alpha
			Put<std::uint32_t>(h, 0);
			for (const std::uint32_t m : { 32u, 0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u }) {
				Put(h, m);
			}
		}
		Put<std::uint32_t>(h, 0x1000);  // caps: texture
		for (int i = 0; i < 4; ++i) {
			Put<std::uint32_t>(h, 0);
		}
		return h;
	}

	std::span<const std::byte> Bytes(const std::string& a_s) { return { reinterpret_cast<const std::byte*>(a_s.data()), a_s.size() }; }

	void BsaTests(const fs::path& a_dir)
	{
		const auto path = a_dir / "test.bsa";
		WriteBsa(path, { { "textures\\a.dds", "hello" }, { "meshes\\clutter\\b.nif", "mesh!" } });
		const auto bsa = esp::Bsa::Open(path);
		Check(bsa != nullptr, "BSA opens");
		if (!bsa) {
			return;
		}
		Check(bsa->FileCount() == 2, "BSA lists both files");
		Check(bsa->Contains("Textures/A.DDS"), "BSA keys ignore case and slash");
		Check(Text(bsa->Read("textures\\a.dds")) == "hello", "BSA reads a file");
		Check(Text(bsa->Read("meshes\\clutter\\b.nif")) == "mesh!", "BSA reads a file in a nested folder");
		Check(!bsa->Read("textures\\missing.dds"), "BSA: a file it does not have");
		Check(!bsa->Changed(), "BSA unchanged after Open");
		WriteBsa(path, { { "textures\\a.dds", "changed!!" } });
		Check(bsa->Changed(), "BSA: a rewrite on disk is noticed");
		std::string error;
		Check(!bsa->Read("textures\\a.dds", &error) && error.find("changed on disk") != std::string::npos, "BSA: a stale index does not read the new bytes");
	}

	void AssetsTests(const fs::path& a_dir)
	{
		const auto archive = a_dir / "assets.bsa";
		const auto loose = a_dir / "loose";
		WriteBsa(archive, { { "textures\\a.dds", "from the archive" }, { "textures\\b.dds", "archive b" } });
		fs::create_directories(loose / "textures");
		std::ofstream(loose / "textures" / "b.dds", std::ios::binary) << "loose b";
		Assets assets({ loose }, { archive });
		assets.OpenArchives();
		std::string source;
		Check(Text(assets.Read("textures\\a.dds", &source)) == "from the archive", "Assets reads from an archive");
		Check(source.find("assets.bsa") != std::string::npos, "Assets names the archive as the source");
		Check(Text(assets.Read("Textures/B.dds", &source)) == "loose b", "Assets: a loose file wins over the archive");
		Check(source.find("loose") != std::string::npos, "Assets names the loose file as the source");
		std::string error;
		Check(!assets.Read("textures\\nope.dds", nullptr, &error) && error.empty(), "Assets: not found leaves the error empty");

		// The stale-archive path: the archive is replaced (a mod update in MO2) after it was indexed.
		WriteBsa(archive, { { "textures\\a.dds", "the new archive's a" } });
		Check(Text(assets.Read("textures\\a.dds")) == "the new archive's a", "Assets indexes a changed archive again (Reindex)");
		fs::remove(loose / "textures" / "b.dds");
		error.clear();
		Check(!assets.Read("textures\\b.dds", nullptr, &error), "Assets: a loose file removed and gone from the new archive is not found");
	}

	// A Skyrim SE NIF (20.2.0.7, BS version 100) built by hand: a BSFadeNode moved 100 units along x (the game
	// ignores the root's transform), holding one BSTriShape moved 100 units along x (a 128 x 64 quad at z 0..10:
	// 4 vertices, 2 triangles) with a BSLightingShaderProperty and its
	// BSShaderTextureSet; `a_alpha` < 1 adds an NiAlphaProperty (blending on). No shape: the node alone.
	std::string WriteNif(bool a_shape, const std::string& a_texture, float a_alpha = 1.0f)
	{
		auto sized = [](std::string& o, const std::string& s) {
			Put(o, static_cast<std::uint32_t>(s.size()));
			o += s;
		};
		auto objectNet = [](std::string& o) {
			Put(o, std::int32_t{ -1 });  // name
			Put(o, std::uint32_t{ 0 });  // extra data
			Put(o, std::int32_t{ -1 });  // controller
		};
		auto avObject = [&](std::string& o, float a_x) {
			objectNet(o);
			Put(o, std::uint32_t{ 0x0E });  // flags (not hidden)
			for (const float v : { a_x, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f }) {
				Put(o, v);  // translation, rotation, scale
			}
			Put(o, std::int32_t{ -1 });  // collision object
		};
		const bool               withAlpha = a_alpha < 1.0f;
		std::vector<std::string> types{ "BSFadeNode" };
		std::vector<std::string> blocks;
		std::string              node;
		avObject(node, 100.0f);
		Put(node, std::uint32_t{ a_shape ? 1u : 0u });
		if (a_shape) {
			Put(node, std::int32_t{ 1 });
		}
		Put(node, std::uint32_t{ 0 });  // effects
		blocks.push_back(node);
		if (a_shape) {
			types.insert(types.end(), { "BSTriShape", "BSLightingShaderProperty", "BSShaderTextureSet", "NiAlphaProperty" });
			std::string shape;
			avObject(shape, 100.0f);
			for (const float v : { 0.0f, 0.0f, 5.0f, 72.0f }) {
				Put(shape, v);  // bounding sphere
			}
			Put(shape, std::int32_t{ -1 });                   // skin
			Put(shape, std::int32_t{ 2 });                    // shader
			Put(shape, std::int32_t{ withAlpha ? 4 : -1 });   // alpha
			Put(shape, std::uint64_t{ 3 } | (std::uint64_t{ 1 } << 44));  // 12-byte vertices, positions only
			Put(shape, std::uint16_t{ 2 });                   // triangles
			Put(shape, std::uint16_t{ 4 });                   // vertices
			Put(shape, std::uint32_t{ 4 * 12 + 2 * 6 });      // data size
			for (const float v : { -64.0f, -32.0f, 0.0f, 64.0f, -32.0f, 0.0f, 64.0f, 32.0f, 10.0f, -64.0f, 32.0f, 10.0f }) {
				Put(shape, v);
			}
			for (const std::uint16_t i : std::initializer_list<std::uint16_t>{ 0, 1, 2, 0, 2, 3 }) {
				Put(shape, i);
			}
			Put(shape, std::uint32_t{ 0 });  // particle data
			blocks.push_back(shape);
			std::string shader;
			Put(shader, std::uint32_t{ 0 });  // shader type
			objectNet(shader);
			Put(shader, std::uint32_t{ 0x82400300 });
			Put(shader, std::uint32_t{ 0x8001 });
			for (const float v : { 0.0f, 0.0f, 1.0f, 1.0f }) {
				Put(shader, v);  // uv offset, scale
			}
			Put(shader, std::int32_t{ 3 });  // texture set
			for (const float v : { 0.0f, 0.0f, 0.0f, 1.0f }) {
				Put(shader, v);  // emissive
			}
			Put(shader, std::uint32_t{ 3 });  // clamp
			Put(shader, a_alpha);
			for (const float v : { 1.0f, 80.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.3f, 2.0f }) {
				Put(shader, v);  // refraction, glossiness, specular, strength, lighting effects
			}
			blocks.push_back(shader);
			std::string textures;
			Put(textures, std::uint32_t{ 1 });
			sized(textures, a_texture);
			blocks.push_back(textures);
			if (withAlpha) {
				std::string alpha;
				objectNet(alpha);
				Put(alpha, std::uint16_t{ 0x00ED });  // blending on
				Put(alpha, std::uint8_t{ 128 });
				blocks.push_back(alpha);
			}
		}
		std::string out = "Gamebryo File Format, Version 20.2.0.7\n";
		Put(out, std::uint32_t{ 0x14020007 });
		Put(out, std::uint8_t{ 1 });   // little endian
		Put(out, std::uint32_t{ 12 });  // user version
		Put(out, static_cast<std::uint32_t>(blocks.size()));
		Put(out, std::uint32_t{ 100 });  // BS version (SE)
		for (int i = 0; i < 3; ++i) {
			Put(out, std::uint8_t{ 1 });  // author, process script, export script: empty
			out += '\0';
		}
		Put(out, static_cast<std::uint16_t>(types.size()));
		for (const auto& t : types) {
			sized(out, t);
		}
		for (std::uint16_t i = 0; i < blocks.size(); ++i) {
			Put(out, i);  // block i has type i
		}
		for (const auto& b : blocks) {
			Put(out, static_cast<std::uint32_t>(b.size()));
		}
		Put(out, std::uint32_t{ 0 });  // strings
		Put(out, std::uint32_t{ 0 });  // max length
		Put(out, std::uint32_t{ 0 });  // groups
		for (const auto& b : blocks) {
			out += b;
		}
		return out;
	}

	void NifTests()
	{
		std::string error;
		const auto  quad = render::ParseNif(Bytes(WriteNif(true, "textures\\test\\wood.dds")), error);
		Check(quad.has_value(), std::format("NIF: a BSTriShape mesh parses ({})", error));
		if (quad) {
			Check(quad->shapes.size() == 1 && quad->warnings.empty() && quad->hiddenShapes == 0, "NIF: one shape, no warnings");
			if (!quad->shapes.empty()) {
				const auto& s = quad->shapes.front();
				Check(s.positions.size() == 12 && s.indices.size() == 6, "NIF: 4 vertices, 2 triangles");
				Check(s.diffuse == "textures\\test\\wood.dds", "NIF: the diffuse texture path");
			}
			Check(quad->boundsMin == std::array<float, 3>{ 36.0f, -32.0f, 0.0f } && quad->boundsMax == std::array<float, 3>{ 164.0f, 32.0f, 10.0f },
				std::format("NIF: bounds with the shape's translation, not the root's ({} {} {} .. {} {} {})", quad->boundsMin[0], quad->boundsMin[1], quad->boundsMin[2],
					quad->boundsMax[0], quad->boundsMax[1], quad->boundsMax[2]));
		}
		error.clear();
		const auto collision = render::ParseNif(Bytes(WriteNif(false, "")), error);
		Check(collision && collision->Empty() && collision->warnings.empty(), "NIF: a collision-only model is empty without warnings (invisible)");
		error.clear();
		const auto black = render::ParseNif(Bytes(WriteNif(true, "textures\\Black.dds")), error);
		Check(black && black->Empty() && black->warnings.empty() && black->hiddenShapes == 1, "NIF: a black card is left out (invisible)");
		error.clear();
		const auto clear = render::ParseNif(Bytes(WriteNif(true, "textures\\test\\wood.dds", 0.0f)), error);
		Check(clear && clear->Empty() && clear->hiddenShapes == 1, "NIF: a shape of material alpha 0, blending on, is left out");
		error.clear();
		Check(!render::ParseNif(Bytes(std::string("not a nif\n")), error) && !error.empty(), "NIF: not a NIF file");
	}

	void DdsTests()
	{
		ID3D11Device*          device = nullptr;
		ID3D11DeviceContext*   context = nullptr;
		const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
		if (FAILED(::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1, D3D11_SDK_VERSION, &device, nullptr, &context))) {
			Check(false, "a WARP Direct3D 11 device for the DDS tests");
			return;
		}
		render::DdsInfo info;
		std::string     error;

		auto rgba = DdsHeader(4, 4, nullptr) + std::string(4 * 4 * 4, '\x7F');
		auto* srv = render::LoadDds(device, Bytes(rgba), 1024, info, error);
		Check(srv != nullptr && info.width == 4 && info.height == 4 && info.mips == 1, std::format("DDS: 32-bit BGRA 4x4 ({})", error));
		if (srv) {
			srv->Release();
		}

		error.clear();
		auto bc1 = DdsHeader(8, 8, "DXT1") + std::string(4 * 8, '\x11');
		srv = render::LoadDds(device, Bytes(bc1), 1024, info, error);
		Check(srv != nullptr && info.format == DXGI_FORMAT_BC1_UNORM && !info.hasAlpha, std::format("DDS: BC1 8x8 ({})", error));
		if (srv) {
			srv->Release();
		}

		error.clear();
		const auto truncated = DdsHeader(64, 64, "DXT5") + std::string(16, '\0');
		srv = render::LoadDds(device, Bytes(truncated), 1024, info, error);
		Check(srv == nullptr && !error.empty(), "DDS: truncated pixel data is refused with a reason");
		if (srv) {
			srv->Release();
		}

		error.clear();
		srv = render::LoadDds(device, Bytes(std::string("not a dds")), 1024, info, error);
		Check(srv == nullptr && !error.empty(), "DDS: not a DDS file");
		context->Release();
		device->Release();
	}

	// state.json in normal use (no --state: `path` empty, the file from DOVASTASH_EDITOR_STATE): saved, read
	// back, saved again over the old file, and no .tmp left over.
	void StateTests(const fs::path& a_dir)
	{
		const auto file = a_dir / L"state-roundtrip.json";
		_wputenv_s(L"DOVASTASH_EDITOR_STATE", file.c_str());
		for (int pass = 0; pass < 2; ++pass) {
			ui::EditorState s;
			s.author = std::format("Author {}", pass);
			s.mo2 = "E:/MO2 instance";
			s.Advance("Skyrim.esm|0x000165A8", "Breezehome", ui::HomeStage::Reviewed);
			s.Save();
		}
		Check(fs::exists(file), "state: state.json written with no --state");
		Check(!fs::exists(fs::path(file.native() + L".tmp")), "state: no .tmp left over");
		ui::EditorState r;
		r.Load();
		Check(r.author == "Author 1", "state: author read back (second save replaced the first)");
		Check(r.mo2 && *r.mo2 == "E:/MO2 instance", "state: MO2 instance read back");
		Check(r.StageOf("Skyrim.esm|0x000165A8") == ui::HomeStage::Reviewed, "state: home stage read back");
	}
}

namespace
{
	// The Checks panel's game-data checks (esm_dump check's words) on a cell made here.
	void RefCheckTests()
	{
		const auto ref = [](std::uint32_t a_id) { return core::FormRef{ "Skyrim.esm", a_id }; };
		std::vector<home::PlacedRef> refs(7);
		for (auto& r : refs) {
			r.baseType = esp::MakeSig("CONT");
			r.baseEditorId = "Chest";
		}
		refs[0].initiallyDisabled = true;  // the master
		refs[1].respawns = refs[1].respawnFlag = true;
		refs[2].respawnFlag = true;  // a cell that never resets
		refs[3].respawns = refs[3].respawnFlag = true;  // ignored: fine
		refs[4].baseType = esp::MakeSig("STAT");
		refs[5].enableParent = esp::FormKey{};
		refs[5].enableOpposite = true;
		refs[6].display = core::DisplayKind::Rack;
		home::RefIndex index;
		for (std::uint32_t i = 0; i < refs.size(); ++i) {
			index.emplace(ref(0x100 + i), &refs[i]);
		}
		core::Layout l;
		l.cell = ref(0x50);
		l.master = ref(0x100);
		for (std::uint32_t i = 1; i <= 5; ++i) {
			core::ContainerBinding c;
			c.ref = ref(0x100 + i);
			c.ignored = i == 3;
			l.containers.push_back(c);
		}
		core::ContainerBinding missing, other;
		missing.ref = ref(0x200);
		other.ref = core::FormRef{ "NotLoaded.esp", 0x801 };
		l.containers.push_back(missing);
		l.containers.push_back(other);
		core::DisplayBinding rack, gone;
		rack.ref = ref(0x106);
		gone.ref = ref(0x300);
		l.displays = { rack, gone };
		const auto loaded = [](std::string_view a_plugin) { return core::IEquals(a_plugin, "Skyrim.esm"); };
		core::Diagnostics diags;
		home::CheckTemplateRefs(l, { &index, true, false, loaded }, "template", diags);
		std::vector<std::string> lines;
		for (const auto& d : diags.Items()) {
			lines.push_back(d.ToString());
		}
		const auto has = [&](std::string_view a_text) { return std::ranges::any_of(lines, [&](const std::string& s) { return s == a_text; }); };
		Check(has("warning: template: master Skyrim.esm|0x00000100 starts disabled (the game enables it later, e.g. when built)"), "checks: the master starts disabled");
		Check(has("error: template: containers[0]: Skyrim.esm|0x00000101 (Chest) respawns: never bind it"), "checks: a respawning container is an error");
		Check(has("warning: template: containers[1]: Skyrim.esm|0x00000102 (Chest) respawns; accepted because its cell never resets"),
			"checks: the respawn flag in a cell that never resets is a warning");
		Check(!std::ranges::any_of(lines, [](const std::string& s) { return s.contains("containers[2]"); }), "checks: an ignored respawning container is fine");
		Check(has("error: template: containers[3]: Skyrim.esm|0x00000104 is not a container (base is not a CONT)"), "checks: not a container");
		Check(has("warning: template: containers[4]: Skyrim.esm|0x00000105 disappears when its enable parent is enabled (house upgrade?)"),
			"checks: removed by an upgrade");
		Check(has("error: template: containers[5]: Skyrim.esm|0x00000200 is not a reference in cell Skyrim.esm|0x00000050"), "checks: not in the cell");
		Check(!std::ranges::any_of(lines, [](const std::string& s) { return s.contains("containers[6]"); }), "checks: a plugin not loaded is skipped");
		Check(has("error: template: displays[1]: Skyrim.esm|0x00000300 is not a reference in cell Skyrim.esm|0x00000050"), "checks: a display not in the cell");
		Check(lines.size() == 7, std::format("checks: 7 lines ({})", lines.size()));
		core::Diagnostics partial;
		home::CheckTemplateRefs(l, { &index, false, true, loaded }, "template", partial);
		Check(std::ranges::none_of(partial.Items(), [](const core::Diagnostic& d) { return d.message.contains("is not a reference"); }),
			"checks: a cell of the home not read reports nothing missing");
	}

	// Heights: the lowest containers that are together, not a lone one a mod parked lower, nor one far out of the home.
	void FloorTests()
	{
		const auto f = ui::detail::FloorOf(std::vector<float>{ 5000.0f, 100.0f, 120.0f, 300.0f, -500.0f, 400.0f, -3000.0f });
		Check(f.base == 100.0f, std::format("floor: the lowest pair, not a lone container ({})", f.base));
		Check(f.Outside(-3000.0f) && !f.Outside(-500.0f), "floor: far from the median is out of the home");
		Check(std::abs(f.Metres(240.0f) - 2.0f) < 1e-4f, "floor: 70 units to the metre");
	}

	// Plugin records for the home finder's fixture (uncompressed, not localized).
	std::string Sub(const char* a_sig, const std::string& a_data)
	{
		std::string out(a_sig, 4);
		Put(out, static_cast<std::uint16_t>(a_data.size()));
		return out + a_data;
	}
	std::string Record(const char* a_sig, std::uint32_t a_id, const std::string& a_subs, std::uint32_t a_flags = 0)
	{
		std::string out(a_sig, 4);
		Put(out, static_cast<std::uint32_t>(a_subs.size()));
		Put(out, a_flags);
		Put(out, a_id);
		Put(out, std::uint32_t{ 0 });
		Put(out, std::uint16_t{ 44 });
		Put(out, std::uint16_t{ 0 });
		return out + a_subs;
	}
	std::string Group(std::uint32_t a_label, std::int32_t a_type, const std::string& a_body)
	{
		std::string out("GRUP", 4);
		Put(out, static_cast<std::uint32_t>(24 + a_body.size()));
		Put(out, a_label);
		Put(out, a_type);
		Put(out, std::uint32_t{ 0 });
		Put(out, std::uint32_t{ 0 });
		return out + a_body;
	}
	std::uint32_t Label(const char* a_sig)
	{
		std::uint32_t v = 0;
		std::memcpy(&v, a_sig, 4);
		return v;
	}
	std::string Z(std::string_view a_text) { return std::string(a_text) + '\0'; }

	// The home finder (home::FindAllHomes): a cell whose last override drops its location, name and interior flag
	// (USCCCP's Myrwatch) has no home evidence left; a template that names it keeps it a home, under the first name.
	void HomeFinderTests(const fs::path& a_dir)
	{
		const auto root = a_dir / L"homes";
		std::error_code ec;
		fs::create_directories(root, ec);
		std::string pos;
		for (int i = 0; i < 6; ++i) {
			Put(pos, 0.0f);
		}
		std::string ref;
		Put(ref, std::uint32_t{ 0xD03 });
		const auto cellChildren = Group(0xD01, 6, Group(0xD01, 9, Record("REFR", 0xD02, Sub("NAME", ref) + Sub("DATA", pos))));
		const auto master = Record("TES4", 0, Sub("HEDR", std::string(12, '\0')), 1) +
		                    Group(Label("CONT"), 0, Record("CONT", 0xD03, Sub("EDID", Z("TestChest")) + Sub("FULL", Z("Chest")))) +
		                    Group(Label("CELL"), 0, Group(0, 2, Group(0, 3, Record("CELL", 0xD01, Sub("EDID", Z("TestHomeCell")) + Sub("FULL", Z("Test Home")) + Sub("DATA", std::string("\x01\x00", 2))) + cellChildren)));
		const auto patch = Record("TES4", 0, Sub("HEDR", std::string(12, '\0')) + Sub("MAST", Z("Master.esm")) + Sub("DATA", std::string(8, '\0'))) +
		                   Group(Label("CELL"), 0, Group(0, 2, Group(0, 3, Record("CELL", 0xD01, Sub("EDID", Z("TestHomeCell"))))));
		std::ofstream(root / L"Master.esm", std::ios::binary).write(master.data(), static_cast<std::streamsize>(master.size()));
		std::ofstream(root / L"Patch.esp", std::ios::binary).write(patch.data(), static_cast<std::streamsize>(patch.size()));

		DataFiles files;
		files.AddRoot(root);
		esp::LoadOrder           lo(files, "english");
		std::vector<std::string> errors;
		Check(lo.AddWithMasters("Patch.esp", errors) && errors.empty(), "homes: the fixture plugins load");
		const auto cell = lo.ParseFormRef("Master.esm|0x00000D01");
		Check(cell.has_value(), "homes: the cell's form ref");
		if (!cell) {
			return;
		}
		const auto info = home::LoadCell(lo, *cell, home::LoadCellOptions{ .resolveDoors = false });
		Check(info && info->interior, "homes: a cell of the CELL group is inside, whatever its last override's DATA says");
		Check(info && info->name == "Test Home", std::format("homes: the name from the first plugin that has one ({})", info ? info->name : "-"));
		Check(home::FindAllHomes(lo).empty(), "homes: no home evidence left, no template: not a home");

		home::TemplateIndex index;
		home::KnownTemplate t;
		t.source = "Templates/Test/TestHome.json";
		t.layout.cell = core::FormRef{ "Master.esm", 0xD01 };
		index.templates.push_back(std::move(t));
		const auto cells = home::TemplateCellsOf(lo, index);
		const auto homes = home::FindAllHomes(lo, {}, nullptr, &cells);
		Check(homes.size() == 1 && homes.front().cell == *cell, std::format("homes: a cell a template names stays a home ({})", homes.size()));
		Check(!homes.empty() && homes.front().name == "Test Home", "homes: listed under its first name");
	}

	// --compare: a scripted bookshelf the template ignores and lists under displays, the draft only as a display, agrees.
	void CompareTests()
	{
		const core::FormRef shelf{ "Skyrim.esm", 0x500 }, chest{ "Skyrim.esm", 0x501 };
		core::Layout        tmpl, draft;
		core::ContainerBinding ignoredShelf, ignoredChest;
		ignoredShelf.ref = shelf;
		ignoredShelf.ignored = true;
		ignoredChest.ref = chest;
		ignoredChest.ignored = true;
		tmpl.containers = { ignoredShelf, ignoredChest };
		core::DisplayBinding display;
		display.ref = shelf;
		tmpl.displays = { display };
		draft.displays = { display };
		const auto r = home::CompareToTemplate(draft, tmpl, core::CategorySet{});
		Check(r.ignoredInTemplate == 2, std::format("compare: two ignored in the template ({})", r.ignoredInTemplate));
		Check(r.ignoredAgree == 1, std::format("compare: the bookshelf listed only as a display agrees, the unlisted chest does not ({})", r.ignoredAgree));
	}
}

int main()
{
	wchar_t temp[MAX_PATH]{};
	::GetTempPathW(MAX_PATH, temp);
	// %TEMP%/DovaStashEditorTests: the fixtures, and the editor's log (not the author's editor.log, which goes
	// next to DOVASTASH_EDITOR_STATE's file).
	const auto base = fs::path(temp) / L"DovaStashEditorTests";
	const auto dir = base / std::format(L"fixtures-{}", ::GetCurrentProcessId());
	std::error_code ec;
	fs::create_directories(dir, ec);
	StateTests(dir);
	_wputenv_s(L"DOVASTASH_EDITOR_STATE", (base / L"state.json").c_str());
	BsaTests(dir);
	AssetsTests(dir);
	DdsTests();
	NifTests();
	RefCheckTests();
	FloorTests();
	CompareTests();
	HomeFinderTests(dir);
	fs::remove_all(dir, ec);
	std::printf("%d of %d checks passed\n", g_checks - g_failed, g_checks);
	return g_failed == 0 ? 0 : 1;
}
