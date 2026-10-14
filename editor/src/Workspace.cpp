#include "Workspace.h"

#include <algorithm>
#include <charconv>

#include "Util.h"
#include "data/Assets.h"
#include "dova/core/FormRef.h"

namespace fs = std::filesystem;

namespace dova::editor
{
	bool Workspace::Open(const WorkspaceOptions& a_options, std::string& a_error)
	{
		_language = a_options.language;
		_underVfs = mo2::RunningUnderMo2Vfs();

		std::optional<fs::path> root = a_options.mo2;
		if (!root && !a_options.data) {
			root = mo2::DetectInstance();
		}
		if (root) {
			_instance = mo2::Instance::Load(*root, a_error);
			if (!_instance) {
				return false;
			}
			_profile = a_options.profile.value_or(_instance->selectedProfile);
			_mods = _instance->LoadMods(_profile, a_error);
			if (_mods.empty() && !a_error.empty()) {
				return false;
			}
			_dataDir = a_options.data.value_or(_instance->dataDir);
		} else if (a_options.data) {
			_dataDir = *a_options.data;
		} else {
			a_error = "no MO2 instance found: pass --mo2 \"<instance folder>\" (the folder with ModOrganizer.ini) or --data \"<Skyrim Data folder>\"";
			return false;
		}

		// Lowest priority first: the game's Data folder, then the enabled mods, then overwrite. Under MO2's
		// VFS the Data folder already shows all of them.
		_files.AddRoot(_dataDir);
		if (_instance && !_underVfs) {
			for (const auto& m : _mods) {
				if (m.enabled && !m.separator && !m.unmanaged) {
					_files.AddRoot(m.dir);
				}
			}
			_files.AddRoot(_instance->overwriteDir);
		}

		DataFiles game;
		game.AddRoot(_dataDir);
		for (const auto& p : game.Plugins()) {
			const bool fromMod = std::ranges::any_of(_mods, [&](const mo2::Mod& m) {
				return m.enabled && std::ranges::any_of(m.plugins, [&](const std::string& q) { return core::IEquals(p, q); });
			});
			if (!fromMod) {
				_basePlugins.push_back(p);
			}
		}
		return true;
	}

	std::vector<const mo2::Mod*> Workspace::FindMods(std::string_view a_nameOrId) const
	{
		std::vector<const mo2::Mod*> out;
		int                          id = 0;
		const auto                   text = Trim(a_nameOrId);
		const bool numeric = !text.empty() && std::from_chars(text.data(), text.data() + text.size(), id).ptr == text.data() + text.size();
		for (const auto& m : _mods) {
			if (m.separator) {
				continue;
			}
			if (numeric ? (m.nexusId && *m.nexusId == id) : core::IEquals(m.name, text)) {
				out.push_back(&m);
			}
		}
		if (out.empty() && !numeric) {
			for (const auto& m : _mods) {
				if (!m.separator && IContains(m.name, text)) {
					out.push_back(&m);
				}
			}
		}
		std::ranges::stable_sort(out, [](const mo2::Mod* a, const mo2::Mod* b) { return a->enabled > b->enabled; });
		return out;
	}

	std::shared_ptr<esp::ScriptLibrary> Workspace::Scripts() const
	{
		std::scoped_lock lock(_scriptsMutex);
		if (!_scripts) {
			auto assets = std::make_shared<Assets>(_files.Roots(), Assets::ArchiveList(*this));
			_scripts = std::make_shared<esp::ScriptLibrary>([assets](std::string_view a_path) { return assets->Read(a_path); });
		}
		return _scripts;
	}

	std::unique_ptr<esp::LoadOrder> Workspace::NewLoadOrder() const
	{
		auto lo = std::make_unique<esp::LoadOrder>(_files, _language);
		lo->SetScripts(Scripts());
		return lo;
	}

	std::unique_ptr<esp::LoadOrder> Workspace::OpenPlugin(std::string_view a_plugin, std::vector<std::string>& a_errors) const
	{
		auto lo = NewLoadOrder();
		if (!lo->AddWithMasters(a_plugin, a_errors)) {
			return nullptr;
		}
		return lo;
	}

	std::vector<std::string> Workspace::ActivePlugins() const
	{
		std::vector<std::string> out{ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" };
		const auto add = [&](const std::string& a_plugin) {
			if (std::ranges::none_of(out, [&](const std::string& p) { return core::IEquals(p, a_plugin); })) {
				out.push_back(a_plugin);
			}
		};
		if (_instance) {
			for (const auto& p : _instance->LoadActivePlugins(_profile)) {
				add(p);
			}
		} else {
			for (const auto& p : _files.Plugins()) {
				add(p);
			}
		}
		return out;
	}

	std::unique_ptr<esp::LoadOrder> Workspace::OpenLoadOrder(std::vector<std::string>& a_errors, const std::function<void(std::size_t, std::size_t)>& a_progress) const
	{
		auto       lo = NewLoadOrder();
		const auto plugins = ActivePlugins();
		for (std::size_t i = 0; i < plugins.size(); ++i) {
			if (a_progress) {
				a_progress(i, plugins.size());
			}
			lo->AddWithMasters(plugins[i], a_errors);
		}
		if (a_progress) {
			a_progress(plugins.size(), plugins.size());
		}
		return lo;
	}

	std::string Workspace::Describe() const
	{
		std::string s;
		if (_instance) {
			const auto enabled = std::ranges::count_if(_mods, [](const mo2::Mod& m) { return m.enabled && !m.separator && !m.unmanaged; });
			const auto total = std::ranges::count_if(_mods, [](const mo2::Mod& m) { return !m.separator && !m.unmanaged; });
			s = "MO2 instance " + PathToUtf8(_instance->root) + ", profile \"" + _profile + "\": " + std::to_string(enabled) + " of " +
			    std::to_string(total) + " mods enabled";
		} else {
			s = "No MO2 instance";
		}
		s += "; Data: " + PathToUtf8(_dataDir);
		s += _underVfs ? " (MO2 virtual file system)" : (_instance ? " + mod folders read directly" : "");
		return s;
	}
}
