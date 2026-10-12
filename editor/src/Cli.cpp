#include "Cli.h"

#include <chrono>
#include <cstdio>
#include <algorithm>
#include <format>

#include "Util.h"
#include "dova/core/Layout.h"
#include "home/Cell.h"

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
