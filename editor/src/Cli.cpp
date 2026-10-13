#include "Cli.h"

#include <chrono>
#include <cstdio>
#include <algorithm>
#include <format>

#include "Util.h"
#include "dova/core/Layout.h"
#include <cstdlib>
#include <numbers>

#include <d3d11.h>
#include <wrl/client.h>

#include "data/Assets.h"
#include "home/Cell.h"
#include "render/Image.h"
#include "render/Nif.h"
#include "render/Scene.h"

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
			Out("\n* = likely a player home (LocTypePlayerHouse location, owned by the player, or mannequins where nothing respawns)\n");
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
			auto                     lo = std::make_unique<esp::LoadOrder>(a_ws.Files(), a_ws.Language());
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
				const auto        layout = home::DraftTemplate(*lo, *cell, diags);
				for (const auto& d : diags.Items()) {
					Err(d.ToString() + "\n");
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
			Assets      assets(a_ws.Files().Roots(), Assets::ArchiveList(a_ws));
			const auto  path = Assets::NormalizeMesh(*a_args.nifInfo);
			std::string source;
			const auto  start = Clock::now();
			const auto  data = assets.Read(path, &source);
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
			auto                     lo = std::make_unique<esp::LoadOrder>(a_ws.Files(), a_ws.Language());
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

			auto assets = std::make_shared<Assets>(a_ws.Files().Roots(), Assets::ArchiveList(a_ws));
			auto desc = render::BuildSceneDesc(*cell);
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
			if (a_args.heightCut) {
				const auto v = Numbers(*a_args.heightCut);
				if (v.size() >= 2) {
					options.heightCut = true;
					options.zLo = v[0];
					options.zHi = v[1];
				}
			}
			render::Camera cam = scene.OverviewCamera(aspect);
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
			Out(std::format("meshes: {} unique, {} missing or unreadable; textures: {} loaded, {} missing; {} archives, {} loose roots{}\n",
				progress.modelsTotal, progress.missingModels, progress.texturesTotal - progress.missingTextures, progress.missingTextures,
				assets->ArchiveCount(), assets->LooseRootCount(), warp ? "; WARP software device" : ""));
			Out(std::format("camera {:.0f},{:.0f},{:.0f} yaw {:.1f} pitch {:.1f}; {} draw calls\n", cam.position.x, cam.position.y, cam.position.z,
				cam.yaw * 180.0f / std::numbers::pi_v<float>, cam.pitch * 180.0f / std::numbers::pi_v<float>, scene.DrawCalls()));
			Out(std::format("time: cell {} ms, meshes and textures {} ms, render {} ms -> {}\n", cellMs, loadMs, renderMs, *a_args.screenshot));
			if (a_args.verbose) {
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

	std::string Usage()
	{
		return "Dova Stash Editor (in development): make Dova Stash home templates outside the game.\n"
		       "\n"
		       "Without a command it opens the editor window. Run it as an MO2 executable to see your mods.\n"
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
			} else if (s == "--gui") {
				a.gui = true;
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
			} else if (s == "--tab") {
				a.tab = value();
			} else if (s == "--verbose") {
				a.verbose = true;
			} else if (s == "--markers") {
				a.markers = true;
			} else if (s == "--nif-columns") {
				a.nifColumns = true;
			} else if (s == "--screenshot") {
				a.screenshot = value();
			} else {
				a.errors.push_back("unknown option " + s);
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
		if (a_args.cell) {
			return Cell(ws, a_args);
		}
		return 0;
	}
}
