#pragma once

// A small stand-in for MO2's virtual Data folder (docs/EDITOR.md §4.3): roots are added lowest priority
// first (the game's Data folder, then each enabled mod, then overwrite), and a later root wins a file
// conflict, as in MO2. The same under MO2's VFS: its merged Data folder is shared by every MO2 process and
// changes when another one launches a program, so the mod folders are read directly.
//
// M1 indexes what plugin reading needs: the files in each root and its Strings folder. M2 adds meshes and
// textures (loose files, then archives in plugin order).

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dova::editor
{
	class DataFiles
	{
	public:
		void AddRoot(const std::filesystem::path& a_dir);

		// `a_relative` like "Skyrim.esm" or "Strings/Skyrim_English.STRINGS" (either slash, any case).
		[[nodiscard]] std::optional<std::filesystem::path> Find(std::string_view a_relative) const;

		// Plugin files (.esm/.esp/.esl) at the top of the merged folder, by name.
		[[nodiscard]] std::vector<std::string> Plugins() const;

		[[nodiscard]] const std::vector<std::filesystem::path>& Roots() const noexcept { return _roots; }

	private:
		std::vector<std::filesystem::path>                     _roots;
		std::unordered_map<std::string, std::filesystem::path> _files;  // lower-case "strings\\x" -> winning path
	};
}
