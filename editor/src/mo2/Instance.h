#pragma once

// Reads a Mod Organizer 2 instance (docs/EDITOR.md §4.2): ModOrganizer.ini (game path, selected profile,
// custom directories), the profiles, and a profile's modlist.txt with each mod's meta.ini (Nexus mod id)
// and the plugins and archives in the mod's root. Read-only.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dova::editor::mo2
{
	struct Mod
	{
		std::string              name;  // folder name under mods/ (UTF-8), as in modlist.txt
		std::filesystem::path    dir;
		bool                     enabled{ false };
		bool                     separator{ false };  // "<name>_separator": a list heading, not a mod
		bool                     unmanaged{ false };  // "*" lines: DLC / Creation Club files in the game folder
		int                      priority{ 0 };       // 0 = lowest (installed first); later wins file conflicts
		std::optional<int>       nexusId;             // meta.ini [General] modid (0/absent: not from Nexus)
		std::string              version;
		std::string              installationFile;
		std::vector<std::string> plugins;   // .esm/.esp/.esl in the mod's root
		std::vector<std::string> archives;  // .bsa in the mod's root
	};

	struct Instance
	{
		std::filesystem::path    root;  // folder holding ModOrganizer.ini
		std::string              gameName;
		std::filesystem::path    gamePath;
		std::filesystem::path    dataDir;  // gamePath/Data
		std::filesystem::path    modsDir;
		std::filesystem::path    profilesDir;
		std::filesystem::path    overwriteDir;
		std::string              selectedProfile;
		std::vector<std::string> profiles;

		// Reads `<root>/ModOrganizer.ini`. Returns nullopt and sets `a_error` when it is missing or unusable.
		[[nodiscard]] static std::optional<Instance> Load(const std::filesystem::path& a_root, std::string& a_error);

		// Mods of a profile in modlist.txt order reversed, i.e. priority order (lowest first), separators included.
		[[nodiscard]] std::vector<Mod> LoadMods(const std::string& a_profile, std::string& a_error) const;

		// Enabled plugins of the profile in load order (plugins.txt "*" lines; loadorder.txt order when present).
		[[nodiscard]] std::vector<std::string> LoadActivePlugins(const std::string& a_profile) const;
	};

	// True when this process runs inside MO2's virtual file system (usvfs is loaded): the game's Data folder
	// shows the mods of the profile the last MO2 process launched a program with (shared by every MO2 process
	// of the instance, so it can change under this one), and writes below it go to MO2's overwrite folder (or
	// the mod chosen in the executable's "Create files in mod" setting). The editor reads the mod folders
	// itself either way (Workspace::Open).
	[[nodiscard]] bool RunningUnderMo2Vfs();

	// Finds the MO2 instance this program belongs to, in order: the parent process when it is
	// ModOrganizer.exe (portable instance next to it, else the instance named in the registry's
	// CurrentInstance), then the folders above the executable and the current directory (e.g. the editor
	// placed in `<instance>/Mod Tools/`). nullopt when none is found; pass --mo2 then.
	[[nodiscard]] std::optional<std::filesystem::path> DetectInstance();
}
