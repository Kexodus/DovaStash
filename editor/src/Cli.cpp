#include "Cli.h"

#include <array>
#include <span>
#include <chrono>
#include <cstdio>
#include <algorithm>
#include <format>

#include "Util.h"
#include "dova/core/Layout.h"
#include <cstdlib>
#include <fstream>
#include <numbers>

#include <d3d11.h>
#include <wrl/client.h>

#include "data/Assets.h"
#include "home/Cell.h"
#include "home/DovaData.h"
#include "home/TemplateFile.h"
#include "render/Image.h"
#include "render/Nif.h"
#include "render/Scene.h"
#include "ui/Common.h"
#include "ui/Tables.h"
#include "ui/Themes.h"

namespace dova::editor
{
	namespace
	{
		void Out(const std::string& a_text) { std::fputs(a_text.c_str(), stdout); }
		void Err(const std::string& a_text) { std::fputs(a_text.c_str(), stderr); }

		using Clock = std::chrono::steady_clock;
		long long Ms(Clock::time_point a_start)
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - a_start).count();
		}

		// Plugins the command works on: --plugin, or the plugins of --mod.
		std::vector<std::string> SelectedPlugins(const Workspace& a_ws, const Args& a_args, std::string& a_error)
		{
			if (a_args.plugin) {
				return { *a_args.plugin };
			}
			if (!a_args.mod) {
				return {};
			}
			const auto mods = a_ws.FindMods(*a_args.mod);
			if (mods.empty()) {
				a_error = "no mod matches \"" + *a_args.mod + "\" (try --list-mods)";
				return {};
			}
			if (mods.size() > 1) {
				Err("note: " + std::to_string(mods.size()) + " mods match \"" + *a_args.mod + "\"; using \"" + mods.front()->name + "\"\n");
			}
			if (mods.front()->plugins.empty()) {
				a_error = "mod \"" + mods.front()->name + "\" has no plugins";
			}
			return mods.front()->plugins;
		}

		int ListMods(const Workspace& a_ws, const Args& a_args)
		{
			Out(a_ws.Describe() + "\n\n");
			if (a_args.filter.empty() || IContains("game data", a_args.filter)) {
				Out(std::format("Game Data folder: {} plugins\n", a_ws.BasePlugins().size()));
				for (const auto& p : a_ws.BasePlugins()) {
					Out("    " + p + "\n");
				}
				Out("\n");
			}
			Out(std::format("{:>5}  {:1}  {:>7}  {}\n", "prio", "", "nexus", "mod: plugins"));
			int shown = 0;
			for (const auto& m : a_ws.Mods()) {
				if (m.separator || m.unmanaged) {
					continue;
				}
				if (!a_args.all && (!m.enabled || m.plugins.empty())) {
					continue;
				}
				if (!a_args.filter.empty()) {
					const bool idMatch = m.nexusId && std::to_string(*m.nexusId) == a_args.filter;
					if (!idMatch && !IContains(m.name, a_args.filter)) {
						continue;
					}
				}
				std::string plugins;
				for (const auto& p : m.plugins) {
					plugins += (plugins.empty() ? "" : ", ") + p;
				}
				Out(std::format("{:>5}  {:1}  {:>7}  {}{}{}\n", m.priority, m.enabled ? "+" : "-", m.nexusId ? std::to_string(*m.nexusId) : "",
					m.name, plugins.empty() ? "" : ": ", plugins));
				++shown;
			}
			Out(std::format("\n{} mods shown{}\n", shown, a_args.all ? "" : " (enabled, with plugins; --all for every mod)"));
			return 0;
		}

		int Homes(const Workspace& a_ws, const Args& a_args)
		{
			std::string error;
			const auto  plugins = SelectedPlugins(a_ws, a_args, error);
			if (!error.empty() || plugins.empty()) {
				Err("error: " + (error.empty() ? std::string("--homes needs --mod <name or Nexus id> or --plugin <file>") : error) + "\n");
				return 2;
			}
			Out(a_ws.Describe() + "\n");
			for (const auto& plugin : plugins) {
				const auto               start = Clock::now();
				std::vector<std::string> errors;
				auto                     lo = a_ws.OpenPlugin(plugin, errors);
				for (const auto& e : errors) {
					Err("warning: " + e + "\n");
				}
				if (!lo) {
					continue;
				}
				const auto index = lo->PluginIndex(plugin);
				const auto homes = home::FindHomes(*lo, *index, a_args.exteriors);
				const auto likely = static_cast<std::size_t>(std::ranges::count_if(homes, [](const home::HomeCandidate& h) { return h.Likely(); }));
				Out(std::format("\n{} ({} plugins loaded with its masters, {} ms): {} likely homes, {} other cells with containers{}\n", plugin,
					lo->PluginCount(), Ms(start), likely, homes.size() - likely, a_args.all ? "" : " (--all lists them)"));
				for (const auto& h : homes) {
					if (!a_args.all && !h.Likely()) {
						continue;
					}
					std::string why;
					for (const auto& r : h.reasons) {
						why += (why.empty() ? "" : ", ") + r;
					}
					Out(std::format("  {} score {:>2}  {:<28}  {:<28}  {:<24}  containers {:>3} ({} respawn)  racks {:>3}  mannequins {:>2}  plaques {:>3}  refs {:>5}{}{}  [{}]\n",
						h.Likely() ? "*" : " ", h.score, lo->Format(h.cell), h.editorId, "\"" + h.name + "\"", h.containers, h.respawning, h.racks,
						h.mannequins, h.plaques, h.references, h.interior ? "" : "  exterior", h.overridesOther ? "  (edits a cell of another plugin)" : "",
						why));
				}
			}
			Out("\n* = likely a player home (LocTypePlayerHouse location, owned by the player or by its mod's own faction, or mannequins where nothing respawns)\n");
			return 0;
		}

		int Cell(const Workspace& a_ws, const Args& a_args)
		{
			std::string error;
			auto        plugins = SelectedPlugins(a_ws, a_args, error);
			if (!error.empty()) {
				Err("error: " + error + "\n");
				return 2;
			}
			if (plugins.empty()) {
				plugins = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" };
			}
			const auto               start = Clock::now();
			std::vector<std::string> errors;
			auto                     lo = a_ws.NewLoadOrder();
			for (const auto& p : plugins) {
				lo->AddWithMasters(p, errors);
			}
			for (const auto& e : errors) {
				Err("warning: " + e + "\n");
			}
			std::optional<esp::FormKey> key = lo->ParseFormRef(*a_args.cell);
			if (!key) {
				key = lo->FindCellByEditorId(*a_args.cell);
			}
			if (!key) {
				Err("error: cell \"" + *a_args.cell + "\" not found in " + std::to_string(lo->PluginCount()) + " loaded plugins\n");
				return 1;
			}
			const auto cell = home::LoadCell(*lo, *key);
			if (!cell) {
				Err("error: cell " + lo->Format(*key) + " not found\n");
				return 1;
			}
			if (a_args.draft) {
				core::Diagnostics diags;
				const auto        categories = home::LoadCategories(a_ws);
				const auto        rules = home::LoadDraftRules(a_ws);
				auto              layout = home::DraftTemplate(*lo, *cell, diags, categories.set ? &*categories.set : nullptr, &rules.rules);
				for (const auto& d : diags.Items()) {
					Err(d.ToString() + "\n");
				}
				if (a_args.saveTo) {
					// The window's Save template, into a folder of your choice (a mod folder or a Data folder).
					if (a_args.author) {
						layout.author = *a_args.author;
					}
					const auto r = home::SaveTemplateFile(PathFromUtf8(*a_args.saveTo), layout, {}, cell->name);
					if (!r.ok) {
						Err("error: " + r.error + "\n");
						return 1;
					}
					Out(std::format("saved version {} to {}{}\n", r.version, PathToUtf8(std::filesystem::path(r.path).make_preferred()), r.replaced ? " (old file kept as " + PathToUtf8(r.backup.filename()) + ")" : ""));
					return 0;
				}
				Out(core::SerializeLayout(layout));
				return 0;
			}

			Out(std::format("{} {} \"{}\" ({}; last edited by {}; {} ms)\n", lo->Format(cell->key), cell->editorId, cell->name,
				cell->interior ? "interior" : "exterior", cell->lastOverride, Ms(start)));
			std::string kws;
			for (const auto& k : cell->locationKeywords) {
				kws += (kws.empty() ? "" : ", ") + k;
			}
			Out(std::format("  location: {} [{}]\n  owner: {}{}\n  encounter zone: {}{}\n", cell->location ? cell->locationLabel : "none", kws,
				cell->owner ? cell->ownerLabel : "none", cell->ownedByPlayer ? " (the player)" : "", cell->encounterZone.empty() ? "none" : cell->encounterZone,
				cell->neverResets ? " (never resets)" : ""));

			int containers = 0, displays = 0;
			Out("\nContainers:\n");
			for (const auto& r : cell->refs) {
				if (!r.IsContainer()) {
					continue;
				}
				++containers;
				std::string flags;
				if (r.respawns) {
					flags += " RESPAWNS";
				} else if (r.respawnFlag) {
					flags += " respawn-flag";  // the cell never resets: harmless (DESIGN §9)
				}
				if (r.initiallyDisabled) {
					flags += " initially-disabled";
				}
				if (r.enableParent) {
					flags += r.enableOpposite ? " removed-by-enable-parent" : " enable-parent";
				}
				for (const auto& s : r.scripts) {
					flags += " script:" + s;
				}
				Out(std::format("  {}  {:<32}  {:<32}  at ({:.0f}, {:.0f}, {:.0f}){}\n", lo->Format(r.key), r.baseName, r.baseEditorId, r.position[0],
					r.position[1], r.position[2], flags));
			}
			Out("\nDisplays:\n");
			for (const auto& r : cell->refs) {
				if (!r.display) {
					continue;
				}
				++displays;
				Out(std::format("  {}  {:<20}  {:<32}  z {:.0f}\n", lo->Format(r.key), home::DisplayLabel(r), r.baseName.empty() ? r.baseEditorId : r.baseName,
					r.position[2]));
			}
			Out("\nFronts (activators that open a container):\n");
			for (const auto& r : cell->refs) {
				if (r.opens) {
					Out(std::format("  {}  {:<32} opens {}\n", lo->Format(r.key), r.baseName.empty() ? r.baseEditorId : r.baseName, lo->Format(*r.opens)));
				}
			}
			Out("\nDoors to other cells:\n");
			for (const auto& d : cell->doors) {
				Out(std::format("  {} -> {}  {}{}\n", lo->Format(d.door), lo->Format(d.destination), d.destinationLabel,
					d.destinationCell ? "  (" + lo->Format(*d.destinationCell) + ")" : ""));
			}
			Out(std::format("\n{} references, {} containers, {} displays, {} doors\n", cell->refs.size(), containers, displays, cell->doors.size()));
			return 0;
		}
	}

	namespace
	{
		using Microsoft::WRL::ComPtr;

		int NifInfo(const Workspace& a_ws, const Args& a_args)
		{
			render::SetNifMatrixColumns(a_args.nifColumns);
			Assets                                assets(a_ws.Files().Roots(), Assets::ArchiveList(a_ws));
			std::string                           path = Assets::NormalizeMesh(*a_args.nifInfo);
			std::string                           source;
			const auto                            start = Clock::now();
			std::optional<std::vector<std::byte>> data;
			std::error_code                       ec;
			if (const auto file = PathFromUtf8(*a_args.nifInfo); file.is_absolute() && std::filesystem::exists(file, ec)) {
				// A file on disk (a mesh of a disabled mod, a download).
				std::ifstream in(file, std::ios::binary | std::ios::ate);
				data.emplace(static_cast<std::size_t>(std::max<std::streamoff>(in.tellg(), 0)));
				in.seekg(0);
				in.read(reinterpret_cast<char*>(data->data()), static_cast<std::streamsize>(data->size()));
				path = *a_args.nifInfo;
				source = "disk";
			} else {
				data = assets.Read(path, &source);
			}
			if (!data) {
				Err(std::format("error: {} not found in {} loose roots and {} archives\n", path, assets.LooseRootCount(), assets.ArchiveCount()));
				return 1;
			}
			Out(std::format("{} from {} ({} bytes, {} ms)\n", path, source, data->size(), Ms(start)));
			Out(render::DescribeNif(*data));
			return 0;
		}

		std::vector<float> Numbers(std::string_view a_text)
		{
			std::vector<float> out;
			std::string        cur;
			for (const char c : std::string(a_text) + ",") {
				if (c == ',' || c == 'x' || c == 'X' || c == ' ') {
					if (!cur.empty()) {
						out.push_back(std::strtof(cur.c_str(), nullptr));
						cur.clear();
					}
				} else {
					cur.push_back(c);
				}
			}
			return out;
		}

		// --render-cell: the 3D view without a window (docs/EDITOR.md, M2). Loads the cell like --cell, draws it
		// off-screen and saves --screenshot (PNG, or BMP for .bmp).
		int RenderCell(const Workspace& a_ws, const Args& a_args)
		{
			if (!a_args.screenshot) {
				Err("error: --render-cell needs --screenshot <file.png>\n");
				return 2;
			}
			std::string error;
			auto        plugins = SelectedPlugins(a_ws, a_args, error);
			if (!error.empty()) {
				Err("error: " + error + "\n");
				return 2;
			}
			if (plugins.empty()) {
				plugins = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" };
			}
			const auto               start = Clock::now();
			std::vector<std::string> errors;
			auto                     lo = a_ws.NewLoadOrder();
			for (const auto& p : plugins) {
				lo->AddWithMasters(p, errors);
			}
			for (const auto& e : errors) {
				Err("warning: " + e + "\n");
			}
			std::optional<esp::FormKey> key = lo->ParseFormRef(*a_args.renderCell);
			if (!key) {
				key = lo->FindCellByEditorId(*a_args.renderCell);
			}
			const auto cell = key ? home::LoadCell(*lo, *key) : std::nullopt;
			if (!cell) {
				Err("error: cell \"" + *a_args.renderCell + "\" not found in " + std::to_string(lo->PluginCount()) + " loaded plugins\n");
				return 1;
			}
			const auto cellMs = Ms(start);

			ComPtr<ID3D11Device>        device;
			ComPtr<ID3D11DeviceContext> context;
			const D3D_FEATURE_LEVEL     levels[] = { D3D_FEATURE_LEVEL_11_0 };
			HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1, D3D11_SDK_VERSION, &device, nullptr, &context);
			bool    warp = false;
			if (FAILED(hr)) {
				hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1, D3D11_SDK_VERSION, &device, nullptr, &context);
				warp = true;
			}
			if (FAILED(hr)) {
				Err("error: no Direct3D 11 device\n");
				return 1;
			}

			render::SetNifMatrixColumns(a_args.nifColumns);
			auto assets = std::make_shared<Assets>(a_ws.Files().Roots(), Assets::ArchiveList(a_ws));
			const auto around = a_args.noNeighbours ? std::vector<home::CellInfo>{} : home::LoadNeighbourCells(*lo, *cell);
			auto       desc = render::BuildSceneDesc(*cell, around);
			render::Scene scene(device.Get(), assets, std::move(desc));
			const auto    loadStart = Clock::now();
			scene.Start();
			scene.Wait();
			const auto loadMs = Ms(loadStart);

			render::RenderTarget target;
			if (!target.Resize(device.Get(), a_args.width, a_args.height)) {
				Err("error: cannot create a render target\n");
				return 1;
			}
			const float aspect = static_cast<float>(a_args.width) / static_cast<float>(a_args.height);
			render::ViewOptions options;
			options.showDisabled = !a_args.noDisabled;
			options.showMarkers = a_args.markers;
			options.brightness = a_args.brightness > 0.0f ? a_args.brightness : 1.0f;
			if (a_args.heightCut) {
				const auto v = Numbers(*a_args.heightCut);
				if (v.size() >= 2) {
					options.heightCut = true;
					options.zLo = v[0];
					options.zHi = v[1];
				}
			}
			if (a_args.roofCut) {
				// The window's roof cut (interiors): per area, never above the highest container + 120.
				options.roofZ = ui::detail::RoofTop(*cell);
				options.roofGrid = render::RoofGrid::Build(ui::detail::RoofFloor(*cell), options.roofZ, ui::detail::kRoofAboveFloor);
			}
			render::Camera cam = scene.OverviewCamera(aspect, options.roofZ, options.showDisabled);
			if (a_args.select) {
				const auto ref = lo->ParseFormRef(*a_args.select);
				for (std::size_t i = 0; ref && i < cell->refs.size(); ++i) {
					if (cell->refs[i].key == *ref) {
						options.selected = static_cast<int>(i);
					}
				}
				if (options.selected < 0) {
					Err("warning: --select " + *a_args.select + " is not a reference of the cell\n");
				}
			}
			if (a_args.camera) {
				const auto v = Numbers(*a_args.camera);
				if (v.size() >= 5) {
					cam.position = { v[0], v[1], v[2] };
					cam.yaw = v[3] * std::numbers::pi_v<float> / 180.0f;
					cam.pitch = v[4] * std::numbers::pi_v<float> / 180.0f;
				} else {
					Err("warning: --camera wants x,y,z,yaw,pitch\n");
				}
			} else if (a_args.frame && options.selected >= 0) {
				cam = scene.FrameCamera(cam, options.selected, aspect, options);
			}
			const auto renderStart = Clock::now();
			scene.Render(context.Get(), target, cam, options);
			unsigned   w = 0, h = 0;
			const auto pixels = render::ReadTexture(device.Get(), context.Get(), target.Resolved(), w, h);
			const auto renderMs = Ms(renderStart);
			for (const auto& p : a_args.picks) {
				// The 3D view's click: a ray through the pixel (same math as ui/View3D.cpp).
				const auto v = Numbers(p);
				if (v.size() < 2) {
					continue;
				}
				const float nx = (v[0] / static_cast<float>(a_args.width)) * 2.0f - 1.0f;
				const float ny = 1.0f - (v[1] / static_cast<float>(a_args.height)) * 2.0f;
				const float t = std::tan(cam.fovY * 0.5f);
				const auto  dir = DirectX::XMVector3Normalize(DirectX::XMVectorAdd(cam.Forward(),
                    DirectX::XMVectorAdd(DirectX::XMVectorScale(cam.Right(), nx * t * aspect), DirectX::XMVectorScale(cam.Up(), ny * t))));
				for (const bool selectMode : { true, false }) {
					const auto hit = scene.Pick(DirectX::XMLoadFloat3(&cam.position), dir, options, selectMode);
					std::string what = "nothing";
					if (hit && hit->index >= 0) {
						const auto& r = cell->refs[static_cast<std::size_t>(hit->index)];
						what = std::format("{} {} {} \"{}\" at {:.0f} units{}", lo->Format(r.key), esp::SigToString(r.baseType), r.baseEditorId, r.baseName, hit->distance,
							r.model.empty() ? std::string{} : " (" + r.model + ")");
					} else if (hit) {
						what = "a neighbour cell's object";
					}
					Out(std::format("pick {} ({}): {}\n", p, selectMode ? "select mode" : "anything", what));
				}
			}
			if (a_args.verbose) {
				// Frame cost once everything is loaded: CPU submit, then wait for the GPU (a read-back forces it).
				const auto  benchStart = std::chrono::steady_clock::now();
				constexpr int frames = 20;
				for (int i = 0; i < frames; ++i) {
					scene.Render(context.Get(), target, cam, options);
				}
				const auto cpu = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - benchStart).count();
				unsigned   bw = 0, bh = 0;
				(void)render::ReadTexture(device.Get(), context.Get(), target.Resolved(), bw, bh);
				const auto total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - benchStart).count();
				Out(std::format("frame: {:.2f} ms CPU submit, {:.2f} ms with the GPU ({} draw calls)\n", cpu / frames, total / frames, scene.DrawCalls()));
			}
			if (pixels.empty() || !render::SaveImage(PathFromUtf8(*a_args.screenshot), w, h, pixels)) {
				Err("error: cannot save " + *a_args.screenshot + "\n");
				return 1;
			}

			const auto progress = scene.Progress();
			std::size_t shown = 0;
			for (const auto& r : scene.Desc().refs) {
				shown += r.model.empty() ? 0 : 1;
			}
			Out(std::format("{} {} \"{}\": {} references, {} drawn ({} with a mesh, {} boxes), {} terrain cells\n", lo->Format(cell->key), cell->editorId,
				cell->name, cell->refs.size(), scene.Desc().refs.size(), shown, scene.Desc().refs.size() - shown, scene.Desc().terrain.size()));
			if (!cell->interior && scene.Desc().terrain.empty()) {
				Out(std::format("no terrain: LAND {}, grid {}\n", cell->land ? "read" : (lo->CellLandRecord(cell->key) ? "unreadable" : "missing"),
					cell->grid ? std::format("{}, {}", (*cell->grid)[0], (*cell->grid)[1]) : std::string("missing")));
			}
			for (const auto& t : scene.Desc().terrain) {
				const auto [lo2, hi2] = std::ranges::minmax(t.heights);
				Out(std::format("terrain ({}, {}): heights {:.0f} .. {:.0f}, {} colors, {} normals, texture {}\n", t.gridX, t.gridY, lo2, hi2, t.colors.size(),
					t.normals.size(), t.texture));
			}
			Out(std::format("meshes: {} unique, {} missing, {} unreadable; textures: {} loaded, {} missing, {} unreadable; {} archives, {} loose roots{}\n",
				progress.modelsTotal, progress.missingModels, progress.unreadableModels,
				progress.texturesTotal - progress.missingTextures - progress.unreadableTextures, progress.missingTextures, progress.unreadableTextures,
				assets->ArchiveCount(), assets->LooseRootCount(), warp ? "; WARP software device" : ""));
			Out(std::format("camera {:.0f},{:.0f},{:.0f} yaw {:.1f} pitch {:.1f}; {} draw calls\n", cam.position.x, cam.position.y, cam.position.z,
				cam.yaw * 180.0f / std::numbers::pi_v<float>, cam.pitch * 180.0f / std::numbers::pi_v<float>, scene.DrawCalls()));
			Out(std::format("time: cell {} ms, meshes and textures {} ms, render {} ms -> {}\n", cellMs, loadMs, renderMs, *a_args.screenshot));
			if (a_args.verbose) {
				std::size_t checked = 0;
				const auto  off = scene.BoundsMismatches(4.0f, checked);
				Out(std::format("bounds check: {} of {} meshes differ from their OBND by more than 4 units\n", off.size(), checked));
				for (const auto& m : off) {
					Out("  " + m + "\n");
				}
				for (const auto& r : scene.Desc().refs) {
					const auto& p = cell->refs[static_cast<std::size_t>(r.index)];
					if (r.model.empty() || std::ranges::any_of(r.model, [](char c) { return static_cast<unsigned char>(c) >= 0x80 || c < 0x20; })) {
						Out(std::format("  box: {} {} {} \"{}\" model \"{}\"\n", lo->Format(p.key), esp::SigToString(p.baseType), p.baseEditorId, p.baseName, p.model));
					}
				}
			}
			const auto problems = scene.Problems();
			for (std::size_t i = 0; i < problems.size() && i < 40; ++i) {
				Out("  " + problems[i] + "\n");
			}
			if (problems.size() > 40) {
				Out(std::format("  ... {} more\n", problems.size() - 40));
			}
			return 0;
		}
	}

	namespace
	{
		// The window's names: ParseArgs accepts exactly these and Usage lists them, so the two cannot drift.
		constexpr std::array<std::string_view, 7>  kTabs{ "3d", "plan", "containers", "displays", "doors", "preview", "template" };
		constexpr std::array<std::string_view, 4>  kInspectors{ "selection", "template", "draft", "checks" };
		constexpr std::array<std::string_view, 13> kPopovers{ "menu", "setup", "switcher", "filter", "kind", "draftmenu", "layers", "colours",
			"camera", "paintcat", "templates", "savedrafts", "categories" };
		constexpr std::array<std::string_view, 3>  kColours{ "category", "fill", "none" };
		constexpr std::array<std::string_view, 12> kLayers{ "structure", "containers", "displays", "doors", "furniture", "people", "disabled",
			"effects", "other", "terrain", "roof", "missing" };

		template <std::size_t N>
		std::string JoinNames(const std::array<std::string_view, N>& a_names, std::string_view a_sep = ", ")
		{
			std::string out;
			for (const auto n : a_names) {
				out += (out.empty() ? "" : std::string(a_sep)) + std::string(n);
			}
			return out;
		}

		// The help text's lines wrapped at 104 columns, each continuation indented to the description column.
		std::string Wrapped(std::string_view a_first, const std::string& a_text)
		{
			constexpr std::size_t kWidth = 104;
			const std::string     indent(24, ' ');
			std::string           out(a_first);
			out.resize(std::max<std::size_t>(out.size(), 24), ' ');
			std::size_t col = out.size();
			std::size_t pos = 0;
			while (pos < a_text.size()) {
				auto end = a_text.find(' ', pos);
				end = end == std::string::npos ? a_text.size() : end;
				const auto word = std::string_view(a_text).substr(pos, end - pos);
				if (col > indent.size() && col + 1 + word.size() > kWidth) {
					out += "\n" + indent;
					col = indent.size();
				} else if (col > indent.size()) {
					out += ' ';
					++col;
				}
				out += word;
				col += word.size();
				pos = end + 1;
			}
			return out + "\n";
		}
	}

	std::string Usage()
	{
		return std::string("Dova Stash Editor (in development): make Dova Stash home templates outside the game.\n"
		       "\n"
		       "Without a command it opens the editor window. Run it as an MO2 executable to see your mods.\n"
		       "In cmd or PowerShell type DovaStashEditor (no .exe): DovaStashEditor.com, next to the .exe, runs it,\n"
		       "waits, and passes on its output and exit code.\n"
		       "\n"
		       "Workspace:\n"
		       "  --mo2 <folder>        MO2 instance folder (with ModOrganizer.ini); detected when launched from MO2\n"
		       "  --profile <name>      MO2 profile (default: the instance's selected profile)\n"
		       "  --data <folder>       Skyrim Data folder to use without MO2\n"
		       "  --lang <language>     Strings language (default: english)\n"
		       "\n"
		       "Commands (print to the console, no window):\n"
		       "  --list-mods [text]    mods with their Nexus ids and plugins (filter by name or id; --all for every mod)\n"
		       "  --homes               find player homes in --mod <name or Nexus id> or --plugin <file>\n"
		       "                        (--exteriors: exterior cells too; --all: every cell with containers)\n"
		       "  --cell <cell>         containers, displays and doors of a cell (EditorID or \"Plugin.esm|0x...\");\n"
		       "                        loads --mod/--plugin with its masters, else the base game\n"
		       "  --cell <cell> --draft print a draft template for the cell (dovastash-layout-1)\n"
		       "                        (--save-to <folder> [--author <name>]: save it as the window does, into\n"
		       "                        <folder>/SKSE/Plugins/DovaStash/Templates/<home>/<author>.json, with backup)\n"
		       "  --render-cell <cell> --screenshot <out.png>\n"
		       "                        draw the cell in 3D without a window (loads like --cell); options:\n"
		       "                        --size WxH, --camera x,y,z,yaw,pitch (degrees), --select <ref> [--frame],\n"
		       "                        --height lo,hi, --no-disabled, --markers, --light <factor>, --no-neighbours (exteriors),\n"
		       "                        --pick x,y (what a click there selects), --roof-cut (the 3D view's, interiors),\n"
		       "                        --verbose (boxes, bounds check, frame time)\n"
		       "  --nif-info <mesh>     how the 3D view reads a mesh (\"clutter\\chest01.nif\" or a file on disk)\n"
		       "  --draft-all           every player home of the active load order with its template status and a\n"
		       "                        draft (master, categories with confidence and reason, ignored containers);\n"
		       "                        --only <text>: homes whose name, EditorID or plugin contains it;\n"
		       "                        --compare: drafts against the existing templates (how many agree);\n"
		       "                        --save-to <folder> [--author <name>]: save the drafts of homes without a\n"
		       "                        template (--include-templated: all) as auto-drafts (\"autoDraft\": true;\n"
		       "                        backup, version); a template someone made there is never replaced\n"
		       "                        --base-game: only the game's own plugins (homes as shipped templates see them)\n"
		       "  --preview <cell>      where items go: run Dova Stash's planner over a sample (--sample haul, all or\n"
		       "                        a category pattern such as \"alchemy.*\") against the cell's template, or its\n"
		       "                        draft with --draft; loads the active load order\n"
		       "\n"
		       "Window (one workspace: homes rail, 3D view or plan, inspector, drawer):\n"
		       "  --gui                 open the window even with --mod/--cell (they preselect)\n"
		       "  --mod <name or id>    the rail shows that mod's homes; with --cell, the cell is searched in its plugins\n"
		       "  --cell <cell>         open that cell (a home of the rail opens with its draft)\n"
		       "  --home <text>         open the home whose cell EditorID or name is it, else the first whose name\n"
		       "                        contains it (again: those homes in turn,\n"
		       "                        each once the previous one's 3D view has loaded; --screenshot: the last)\n"
		       "  --drafts              start \"Draft all homes\"; the rail shows the homes without a template\n"
		       "                        (with --home: that home's Draft panel; --only <text> is the same as --home)\n"
		       "  --tab <name>          3d or plan (the view); containers, displays, doors or preview (the drawer);\n"
		       "                        template (the inspector's Template panel)\n") +
		       Wrapped("  --inspector <panel>", JoinNames(kInspectors)) +
		       "  --select <ref>        select a reference once the cell is open; --multi <ref> selects more\n"
		       "  --paint <category>    paint mode on with that category\n" +
		       Wrapped("  --colours <mode>", "colour the containers: " + JoinNames(kColours)) +
		       "  --palette             open the command palette (Ctrl+K)\n" +
		       Wrapped("  --popover <id>", "open a popover for a screenshot: " + JoinNames(kPopovers) +
		                                      " (kind: the drawer tab's Kind list; templates: the title's template list; savedrafts: the Save N "
		                                      "drafts dialog; categories: the selection's category picker)") +
		       "  --window WxH          the window's client size (e.g. 1280x720)\n"
		       "  --theme <name>        a colour theme for this run (Setup's Theme list, e.g. \"Windows10 Dark\")\n"
		       "  --text-size <n|auto>  the text size for this run, in percent (100 to 250) or auto; screenshots too\n" +
		       Wrapped("  --layers <names>", "turn layers on, or off with a leading -, comma separated: " + JoinNames(kLayers)) +
		       "  --sort <t>:<col>[:desc]  sort a drawer table (containers, displays, doors, preview) by a column\n"
		       "                        (its header or number); repeat it to add columns\n"
		       "  --filter <text>       the drawer tab's filter box (every word must match)\n"
		       "  --quick <filter>      turn a column filter of the drawer tab on (e.g. Ignored, Respawning, Used,\n"
		       "                        \"Receives nothing\"); repeatable\n"
		       "  --screenshot <file>   save the window (.png or .bmp) once everything is loaded, then exit; the\n"
		       "                        run starts from the default state and writes none (no state.json change;\n"
		       "                        it logs to %TEMP%\\DovaStashEditor\\editor-run.log, as console commands do);\n"
		       "                        exit 1 on an error (stderr and the log, no message box) and when the home\n"
		       "                        asked for cannot open (none matches, no instance found), 4 when it times\n"
		       "                        out; without an interactive desktop (session 0) it draws off-screen at\n"
		       "                        --window size (a window run without --screenshot exits 3 there)\n"
		       "  --timeout <s>         --screenshot's limit (default 180 s): then it saves what it has, logs what it\n"
		       "                        waited for and exits 4\n"
		       "  --state <file>        read and write this state file instead of state.json (also with --screenshot;\n"
		       "                        screenshots keep the text at 100 % unless --text-size is given)\n"
		       "  (no --mod/--cell/--home/--drafts: the start panel)\n"
		       "  --help\n";
	}

	Args ParseArgs(const std::vector<std::string>& a_args)
	{
		Args a;
		for (std::size_t i = 0; i < a_args.size(); ++i) {
			const auto& s = a_args[i];
			auto        value = [&]() -> std::optional<std::string> {
                if (i + 1 < a_args.size()) {
                    return a_args[++i];
                }
                a.errors.push_back(s + " needs a value");
                return std::nullopt;
			};
			if (s == "--help" || s == "-h" || s == "/?") {
				a.help = true;
			} else if (s == "--mo2") {
				if (auto v = value()) {
					a.workspace.mo2 = PathFromUtf8(*v);
				}
			} else if (s == "--profile") {
				a.workspace.profile = value();
			} else if (s == "--data") {
				if (auto v = value()) {
					a.workspace.data = PathFromUtf8(*v);
				}
			} else if (s == "--lang") {
				if (auto v = value()) {
					a.workspace.language = *v;
				}
			} else if (s == "--list-mods") {
				a.listMods = true;
				if (i + 1 < a_args.size() && !a_args[i + 1].starts_with("--")) {
					a.filter = a_args[++i];
				}
			} else if (s == "--all") {
				a.all = true;
			} else if (s == "--mod") {
				a.mod = value();
			} else if (s == "--plugin") {
				a.plugin = value();
			} else if (s == "--homes") {
				a.homes = true;
			} else if (s == "--exteriors") {
				a.exteriors = true;
			} else if (s == "--cell") {
				a.cell = value();
			} else if (s == "--draft") {
				a.draft = true;
				a.useDraft = true;
			} else if (s == "--draft-all") {
				a.draftAll = true;
			} else if (s == "--compare") {
				a.compare = true;
			} else if (s == "--only") {
				a.only = value();
			} else if (s == "--include-templated") {
				a.includeTemplated = true;
			} else if (s == "--preview") {
				a.preview = value();
			} else if (s == "--sample") {
				if (auto v = value()) {
					a.sample = *v;
				}
			} else if (s == "--drafts") {
				a.drafts = true;
			} else if (s == "--base-game") {
				a.baseGame = true;
			} else if (s == "--colours" || s == "--colors") {
				a.colours = value();
			} else if (s == "--paint") {
				a.paint = value();
			} else if (s == "--multi") {
				if (auto v = value()) {
					a.multi.push_back(*v);
				}
			} else if (s == "--gui") {
				a.gui = true;
			} else if (s == "--home") {
				if (auto v = value(); v && a.home) {
					a.moreHomes.push_back(*v);
				} else {
					a.home = v;
				}
			} else if (s == "--inspector") {
				a.inspector = value();
			} else if (s == "--palette") {
				a.palette = true;
			} else if (s == "--popover") {
				a.popover = value();
			} else if (s == "--layers") {
				a.layers = value();
			} else if (s == "--theme") {
				if (auto v = value()) {
					if (!ui::w::FindTheme(*v)) {
						a.errors.push_back("--theme: no theme named \"" + *v + "\" (Setup's Theme list)");
					}
					a.theme = std::move(v);
				}
			} else if (s == "--text-size") {
				if (auto v = value()) {
					if (core::IEquals(*v, "auto")) {
						a.textSize = 0.0f;
					} else if (const float pct = std::strtof(v->c_str(), nullptr); pct >= 100.0f && pct <= 250.0f) {
						a.textSize = pct / 100.0f;
					} else {
						a.errors.push_back("--text-size wants a percent from 100 to 250, or auto");
					}
				}
			} else if (s == "--sort") {
				if (auto v = value()) {
					std::string why;
					if (!ui::ParseSortArg(*v, &why)) {
						a.errors.push_back(why);
					}
					a.sort.push_back(std::move(*v));
				}
			} else if (s == "--filter") {
				a.tableFilter = value();
			} else if (s == "--quick") {
				if (auto v = value()) {
					a.quick.push_back(std::move(*v));
				}
			} else if (s == "--window") {
				if (auto v = value()) {
					const auto n = Numbers(*v);
					if (n.size() >= 2 && n[0] >= 320 && n[1] >= 240) {
						a.window = std::pair{ static_cast<int>(n[0]), static_cast<int>(n[1]) };
					} else {
						a.errors.push_back("--window wants WIDTHxHEIGHT (at least 320x240)");
					}
				}
			} else if (s == "--timeout") {
				if (auto v = value()) {
					if (const long n = std::strtol(v->c_str(), nullptr, 10); n >= 1 && n <= 86400) {
						a.timeout = static_cast<int>(n);
					} else {
						a.errors.push_back("--timeout wants seconds (1 to 86400)");
					}
				}
			} else if (s == "--nif-info") {
				a.nifInfo = value();
			} else if (s == "--render-cell") {
				a.renderCell = value();
			} else if (s == "--size") {
				if (auto v = value()) {
					const auto n = Numbers(*v);
					if (n.size() >= 2 && n[0] >= 16 && n[1] >= 16) {
						a.width = static_cast<unsigned>(n[0]);
						a.height = static_cast<unsigned>(n[1]);
					} else {
						a.errors.push_back("--size wants WIDTHxHEIGHT");
					}
				}
			} else if (s == "--camera") {
				a.camera = value();
			} else if (s == "--select") {
				a.select = value();
			} else if (s == "--frame") {
				a.frame = true;
			} else if (s == "--height") {
				a.heightCut = value();
			} else if (s == "--no-disabled") {
				a.noDisabled = true;
			} else if (s == "--roof-cut") {
				a.roofCut = true;
			} else if (s == "--tab") {
				a.tab = value();
			} else if (s == "--save-to") {
				a.saveTo = value();
			} else if (s == "--author") {
				a.author = value();
			} else if (s == "--pick") {
				if (auto v = value()) {
					a.picks.push_back(*v);
				}
			} else if (s == "--no-neighbours") {
				a.noNeighbours = true;
			} else if (s == "--verbose") {
				a.verbose = true;
			} else if (s == "--light") {
				if (auto v = value()) {
					a.brightness = std::strtof(v->c_str(), nullptr);
				}
			} else if (s == "--markers") {
				a.markers = true;
			} else if (s == "--nif-columns") {
				a.nifColumns = true;
			} else if (s == "--screenshot") {
				a.screenshot = value();
			} else if (s == "--state") {
				a.state = value();
			} else {
				a.errors.push_back("unknown option " + s);
			}
		}
		// The window's names: a typo is an error (exit 2), not a screenshot of something else.
		const auto oneOf = [&](const char* a_flag, const std::optional<std::string>& a_value, std::span<const std::string_view> a_names) {
			if (!a_value) {
				return;
			}
			std::string list;
			for (const auto n : a_names) {
				if (core::IEquals(*a_value, n)) {
					return;
				}
				list += (list.empty() ? "" : ", ") + std::string(n);
			}
			a.errors.push_back(std::format("unknown {} {} ({})", a_flag, *a_value, list));
		};
		oneOf("--tab", a.tab, kTabs);
		oneOf("--inspector", a.inspector, kInspectors);
		oneOf("--popover", a.popover, kPopovers);
		oneOf("--colours", a.colours, kColours);
		if (a.layers) {
			std::string_view rest = *a.layers;
			while (!rest.empty()) {
				const auto comma = rest.find(',');
				std::string      name(rest.substr(0, comma));
				rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
				if (name.starts_with('-') || name.starts_with('+')) {
					name.erase(0, 1);
				}
				oneOf("--layers", name, kLayers);
			}
		}
		// --quick names a column filter of the drawer tab --tab shows (the Containers tab without one).
		const auto table = a.tab ? ui::ParseTableName(*a.tab) : std::optional<ui::DrawerTable>{};
		for (const auto& q : a.quick) {
			const auto t = table.value_or(ui::DrawerTable::Containers);
			if (!ui::QuickFilterBit(t, q)) {
				std::string chips;
				for (const auto& f : ui::TableQuickFilters(t)) {
					chips += (chips.empty() ? "" : ", ") + std::string(f.label);
				}
				a.errors.push_back("--quick: the " + std::string(ui::TableName(t)) + " table has no filter \"" + q + "\"" +
				                   (chips.empty() ? std::string(" (it has none)") : " (" + chips + ")"));
			}
		}
		return a;
	}

	int RunCli(const Args& a_args)
	{
		if (!a_args.errors.empty()) {
			for (const auto& e : a_args.errors) {
				Err("error: " + e + "\n");
			}
			Err("\n" + Usage());
			return 2;
		}
		if (a_args.help) {
			Out(Usage());
			return 0;
		}
		Workspace   ws;
		std::string error;
		if (!ws.Open(a_args.workspace, error)) {
			Err("error: " + error + "\n");
			return 2;
		}
		if (a_args.nifInfo) {
			return NifInfo(ws, a_args);
		}
		if (a_args.renderCell) {
			return RenderCell(ws, a_args);
		}
		if (a_args.listMods) {
			return ListMods(ws, a_args);
		}
		if (a_args.homes) {
			return Homes(ws, a_args);
		}
		if (a_args.draftAll) {
			return DraftAllCommand(ws, a_args);
		}
		if (a_args.preview) {
			return PreviewCommand(ws, a_args);
		}
		if (a_args.cell) {
			return Cell(ws, a_args);
		}
		return 0;
	}
}
