#pragma once

// The editor's view of the player's setup: the MO2 instance and profile (or a plain Data folder), the
// mods with their Nexus ids and plugins, and the merged Data folder (DataFiles). Opens a plugin with its
// masters as an esp::LoadOrder for the home finder and the cell views.

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "data/DataFiles.h"
#include "esp/LoadOrder.h"
#include "mo2/Instance.h"

namespace dova::editor
{
	struct WorkspaceOptions
	{
		std::optional<std::filesystem::path> mo2;      // MO2 instance folder (ModOrganizer.ini); detected when empty
		std::optional<std::string>           profile;  // default: the instance's selected profile
		std::optional<std::filesystem::path> data;     // a Skyrim Data folder to use without MO2
		std::string                          language{ "english" };
	};

	class Workspace
	{
	public:
		bool Open(const WorkspaceOptions& a_options, std::string& a_error);

		[[nodiscard]] const std::optional<mo2::Instance>& Instance() const noexcept { return _instance; }
		[[nodiscard]] const std::string&                  Profile() const noexcept { return _profile; }
		[[nodiscard]] const std::vector<mo2::Mod>&        Mods() const noexcept { return _mods; }
		[[nodiscard]] const DataFiles&                    Files() const noexcept { return _files; }
		[[nodiscard]] bool                                UnderVfs() const noexcept { return _underVfs; }
		[[nodiscard]] const std::filesystem::path&        DataDir() const noexcept { return _dataDir; }
		[[nodiscard]] const std::string&                  Language() const noexcept { return _language; }

		// Plugins in the game's own Data folder (base game, Creation Club, unmanaged files).
		[[nodiscard]] const std::vector<std::string>& BasePlugins() const noexcept { return _basePlugins; }

		// Mods matching a Nexus mod id (all of them) or a name (exact first, else substring), enabled first.
		[[nodiscard]] std::vector<const mo2::Mod*> FindMods(std::string_view a_nameOrId) const;

		// The plugin and its masters (each once, masters first). nullptr when the plugin cannot be loaded.
		[[nodiscard]] std::unique_ptr<esp::LoadOrder> OpenPlugin(std::string_view a_plugin, std::vector<std::string>& a_errors) const;

		[[nodiscard]] std::string Describe() const;

	private:
		std::optional<mo2::Instance> _instance;
		std::string                  _profile;
		std::vector<mo2::Mod>        _mods;
		DataFiles                    _files;
		std::filesystem::path        _dataDir;
		std::vector<std::string>     _basePlugins;
		std::string                  _language;
		bool                         _underVfs{ false };
	};
}
