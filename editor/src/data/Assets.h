#pragma once

// Meshes and textures as the game sees them (docs/EDITOR.md §4.3, M2): loose files in the merged Data
// folder win over archives; archives are searched highest priority first (the game's own list from
// Skyrim.ini, then each plugin's "<name>.bsa" / "<name> - Textures.bsa" in load order).
//
// Loose files are found without walking the whole Data folder (an MO2 setup can hold millions of
// textures over thousands of mods): a directory is listed only in the roots that have its parent, and
// only when a file in it is asked for. Every function is thread-safe.
//
// The files can change while the editor runs (MO2 installs, removes or replaces a mod): a listing that
// failed is not kept, a listed file that cannot be read has its folder listed again, and an archive whose
// file changed on disk is indexed again. So "not found" means: in no loose folder and no archive.

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "esp/Bsa.h"

namespace dova::editor
{
	class Workspace;

	class Assets
	{
	public:
		// `a_roots`: loose-file roots, lowest priority first (DataFiles::Roots()).
		// `a_archives`: archive files, lowest priority first.
		Assets(std::vector<std::filesystem::path> a_roots, std::vector<std::filesystem::path> a_archives);

		// The archives of a workspace in the game's order (see the header comment).
		[[nodiscard]] static std::vector<std::filesystem::path> ArchiveList(const Workspace& a_ws);

		// Opens every archive (parallel). `a_progress(done, total)` is called from worker threads.
		void OpenArchives(const std::function<void(std::size_t, std::size_t)>& a_progress = {});

		// `a_path` like "meshes\\clutter\\chest01.nif" (either slash, any case). `a_source` (optional)
		// receives where it came from: the loose file's path or "<archive>.bsa". nullopt: `a_error` (optional)
		// stays empty when no loose folder or archive has the file, else says why the one found could not be read.
		[[nodiscard]] std::optional<std::vector<std::byte>> Read(std::string_view a_path, std::string* a_source = nullptr,
			std::string* a_error = nullptr);
		[[nodiscard]] bool                                  Exists(std::string_view a_path);

		[[nodiscard]] std::size_t ArchiveCount() const noexcept { return _archivePaths.size(); }
		[[nodiscard]] std::size_t LooseRootCount() const noexcept { return _roots.size(); }

		// "textures\\x.dds" from a texture set entry or "meshes\\x.nif" from a MODL (adds the folder when
		// missing, strips a leading "data\\", lower case, backslashes).
		[[nodiscard]] static std::string NormalizeMesh(std::string_view a_path);
		[[nodiscard]] static std::string NormalizeTexture(std::string_view a_path);

	private:
		struct Listing
		{
			std::unordered_set<std::string> files;  // lower-case names
			std::unordered_set<std::string> dirs;
		};
		using DirEntries = std::vector<std::pair<std::uint32_t, std::shared_ptr<Listing>>>;  // root index (ascending) -> listing

		DirEntries                           ResolveDir(const std::string& a_dir);  // _mutex held
		std::optional<std::filesystem::path> FindLoose(const std::string& a_normalized);
		void                                 ForgetDir(const std::string& a_normalized);  // the folder of this file is listed again
		// The archives that have `a_key`, highest priority first (index into _archives, archive).
		std::vector<std::pair<std::size_t, std::shared_ptr<const esp::Bsa>>> ArchivesWith(const std::string& a_key);
		// Indexes archive `a_index` again when its file is not the one indexed (nullptr: gone or unreadable now).
		std::shared_ptr<const esp::Bsa> Reindex(std::size_t a_index, const std::shared_ptr<const esp::Bsa>& a_stale);
		void                                 NoteFailure(std::string_view a_what);

		std::vector<std::filesystem::path>            _roots;
		std::vector<std::filesystem::path>            _archivePaths;
		std::vector<std::shared_ptr<const esp::Bsa>>  _archives;  // parallel to _archivePaths (null: unreadable)
		std::shared_mutex                             _archivesMutex;  // _archives' entries (Reindex replaces one)
		std::atomic<bool>                             _archivesOpen{ false };
		std::atomic<int>                              _failuresLogged{ 0 };
		std::mutex                                    _openMutex;
		std::mutex                                    _mutex;
		std::unordered_map<std::string, DirEntries>   _dirs;  // lower-case relative dir ("" = root)
	};
}
