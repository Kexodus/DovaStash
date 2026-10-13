#include "data/Assets.h"

#include <algorithm>
#include <fstream>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <ShlObj.h>
#include <Windows.h>

#include "Util.h"
#include "Workspace.h"
#include "dova/core/FormRef.h"

namespace fs = std::filesystem;

namespace dova::editor
{
	namespace
	{
		std::string Normalize(std::string_view a_path)
		{
			std::string s = core::ToLower(Trim(a_path));
			std::ranges::replace(s, '/', '\\');
			// Collapse doubled separators and strip leading ones / "data\".
			std::string out;
			out.reserve(s.size());
			for (const char c : s) {
				if (c == '\\' && (out.empty() || out.back() == '\\')) {
					continue;
				}
				out.push_back(c);
			}
			if (out.starts_with(".\\")) {
				out.erase(0, 2);
			}
			if (out.starts_with("data\\")) {
				out.erase(0, 5);
			}
			return out;
		}

		std::string WithFolder(std::string a_path, std::string_view a_folder)
		{
			// "...\\data\\textures\\x.dds" (absolute paths from the author's machine) -> "textures\\x.dds"
			// "dlc01\\build\\pc\\data\\textures\\x.dds" (build paths left in texture sets) -> "textures\\x.dds"
			const std::string folder = std::string(a_folder) + "\\";
			if (a_path.starts_with(folder)) {
				return a_path;
			}
			if (const auto at = a_path.rfind("data\\" + folder); at != std::string::npos && (at == 0 || a_path[at - 1] == '\\')) {
				return a_path.substr(at + 5);
			}
			if (const auto at = a_path.find("\\" + folder); at != std::string::npos && a_path.find(':') != std::string::npos) {
				return a_path.substr(at + 1);
			}
			return folder + a_path;
		}

		std::vector<std::string> SplitList(std::string_view a_text)
		{
			std::vector<std::string> out;
			std::size_t              p = 0;
			while (p <= a_text.size()) {
				const auto e = a_text.find(',', p);
				const auto item = Trim(a_text.substr(p, e == std::string_view::npos ? std::string_view::npos : e - p));
				if (!item.empty()) {
					out.emplace_back(item);
				}
				if (e == std::string_view::npos) {
					break;
				}
				p = e + 1;
			}
			return out;
		}

		std::optional<fs::path> DocumentsIni()
		{
			PWSTR docs = nullptr;
			if (FAILED(::SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs))) {
				return std::nullopt;
			}
			fs::path p = fs::path(docs) / L"My Games" / L"Skyrim Special Edition" / L"Skyrim.ini";
			::CoTaskMemFree(docs);
			return p;
		}

		bool ListInto(const fs::path& a_dir, std::unordered_set<std::string>& a_files, std::unordered_set<std::string>& a_dirs)
		{
			WIN32_FIND_DATAW fd{};
			const auto       pattern = (a_dir / L"*").native();
			HANDLE           h = ::FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
			if (h == INVALID_HANDLE_VALUE) {
				return false;
			}
			do {
				const std::wstring_view name(fd.cFileName);
				if (name == L"." || name == L"..") {
					continue;
				}
				auto lower = core::ToLower(Narrow(name));
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
					a_dirs.insert(std::move(lower));
				} else {
					a_files.insert(std::move(lower));
				}
			} while (::FindNextFileW(h, &fd));
			::FindClose(h);
			return true;
		}
	}

	Assets::Assets(std::vector<fs::path> a_roots, std::vector<fs::path> a_archives) :
		_roots(std::move(a_roots)),
		_archivePaths(std::move(a_archives))
	{}

	std::string Assets::NormalizeMesh(std::string_view a_path) { return WithFolder(Normalize(a_path), "meshes"); }
	std::string Assets::NormalizeTexture(std::string_view a_path) { return WithFolder(Normalize(a_path), "textures"); }

	std::vector<fs::path> Assets::ArchiveList(const Workspace& a_ws)
	{
		std::vector<std::string> names;
		auto                     add = [&](std::string_view a_name) {
            if (std::ranges::none_of(names, [&](const std::string& n) { return core::IEquals(n, a_name); })) {
                names.emplace_back(a_name);
            }
		};

		// 1. The game's own archives (Skyrim.ini [Archive]): the profile's INI when it has its own, else Documents.
		std::vector<fs::path> inis;
		if (a_ws.Instance()) {
			inis.push_back(a_ws.Instance()->profilesDir / PathFromUtf8(a_ws.Profile()) / L"skyrim.ini");
		}
		if (auto d = DocumentsIni()) {
			inis.push_back(*d);
		}
		bool fromIni = false;
		for (const auto& ini : inis) {
			if (const auto file = IniFile::Load(ini)) {
				const auto l1 = file->Get("Archive", "sResourceArchiveList");
				const auto l2 = file->Get("Archive", "sResourceArchiveList2");
				if (l1 || l2) {
					for (const auto& n : SplitList(l1.value_or(""))) {
						add(n);
					}
					for (const auto& n : SplitList(l2.value_or(""))) {
						add(n);
					}
					fromIni = true;
					break;
				}
			}
		}
		if (!fromIni) {
			for (const char* n : { "Skyrim - Misc.bsa", "Skyrim - Shaders.bsa", "Skyrim - Interface.bsa", "Skyrim - Animations.bsa", "Skyrim - Meshes0.bsa",
					 "Skyrim - Meshes1.bsa", "Skyrim - Sounds.bsa", "Skyrim - Voices_en0.bsa", "Skyrim - Textures0.bsa", "Skyrim - Textures1.bsa",
					 "Skyrim - Textures2.bsa", "Skyrim - Textures3.bsa", "Skyrim - Textures4.bsa", "Skyrim - Textures5.bsa", "Skyrim - Textures6.bsa",
					 "Skyrim - Textures7.bsa", "Skyrim - Textures8.bsa", "Skyrim - Patch.bsa" }) {
				add(n);
			}
		}

		// 2. Plugin archives in load order: the active plugins of the profile, then every other plugin in the
		// merged Data folder (a disabled home mod opened on purpose still shows its meshes).
		std::vector<std::string> plugins = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" };
		if (a_ws.Instance()) {
			for (auto& p : a_ws.Instance()->LoadActivePlugins(a_ws.Profile())) {
				plugins.push_back(std::move(p));
			}
		}
		for (auto& p : a_ws.Files().Plugins()) {
			plugins.push_back(std::move(p));
		}
		for (const auto& p : plugins) {
			const auto stem = PathToUtf8(fs::path(Widen(p)).stem());
			add(stem + ".bsa");
			add(stem + " - Textures.bsa");
		}

		std::vector<fs::path> out;
		for (const auto& n : names) {
			if (const auto path = a_ws.Files().Find(n)) {
				if (std::ranges::find(out, *path) == out.end()) {
					out.push_back(*path);
				}
			}
		}
		return out;
	}

	void Assets::OpenArchives(const std::function<void(std::size_t, std::size_t)>& a_progress)
	{
		std::scoped_lock lock(_openMutex);
		if (_archivesOpen) {
			return;
		}
		std::vector<std::unique_ptr<esp::Bsa>> opened(_archivePaths.size());
		std::atomic<std::size_t>               next{ 0 }, done{ 0 };
		const auto                             threads = std::clamp<unsigned>(std::thread::hardware_concurrency(), 2u, 8u);
		{
			std::vector<std::jthread> pool;
			for (unsigned t = 0; t < threads; ++t) {
				pool.emplace_back([&]() {
					for (std::size_t i = next++; i < opened.size(); i = next++) {
						try {
							opened[i] = esp::Bsa::Open(_archivePaths[i]);
						} catch (...) {
							opened[i].reset();
						}
						const auto d = ++done;
						if (a_progress) {
							a_progress(d, opened.size());
						}
					}
				});
			}
		}
		_archives = std::move(opened);
		_archivesOpen = true;
	}

	const Assets::DirEntries& Assets::ResolveDir(const std::string& a_dir)
	{
		if (const auto it = _dirs.find(a_dir); it != _dirs.end()) {
			return it->second;
		}
		DirEntries entries;
		if (a_dir.empty()) {
			for (std::uint32_t r = 0; r < _roots.size(); ++r) {
				auto l = std::make_shared<Listing>();
				if (ListInto(_roots[r], l->files, l->dirs)) {
					entries.emplace_back(r, std::move(l));
				}
			}
		} else {
			const auto        slash = a_dir.rfind('\\');
			const std::string parent = slash == std::string::npos ? std::string{} : a_dir.substr(0, slash);
			const std::string leaf = slash == std::string::npos ? a_dir : a_dir.substr(slash + 1);
			const DirEntries  parents = ResolveDir(parent);  // copy: the map may rehash below
			for (const auto& [root, listing] : parents) {
				if (!listing->dirs.contains(leaf)) {
					continue;
				}
				auto l = std::make_shared<Listing>();
				if (ListInto(_roots[root] / PathFromUtf8(a_dir), l->files, l->dirs)) {
					entries.emplace_back(root, std::move(l));
				}
			}
		}
		return _dirs.emplace(a_dir, std::move(entries)).first->second;
	}

	std::optional<fs::path> Assets::FindLoose(const std::string& a_normalized)
	{
		const auto        slash = a_normalized.rfind('\\');
		const std::string dir = slash == std::string::npos ? std::string{} : a_normalized.substr(0, slash);
		const std::string name = slash == std::string::npos ? a_normalized : a_normalized.substr(slash + 1);
		std::scoped_lock  lock(_mutex);
		const auto&       entries = ResolveDir(dir);
		for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
			if (it->second->files.contains(name)) {
				return _roots[it->first] / PathFromUtf8(a_normalized);
			}
		}
		return std::nullopt;
	}

	bool Assets::Exists(std::string_view a_path)
	{
		const auto key = Normalize(a_path);
		if (FindLoose(key)) {
			return true;
		}
		OpenArchives();
		return std::ranges::any_of(_archives, [&](const auto& a) { return a && a->Contains(key); });
	}

	std::optional<std::vector<std::byte>> Assets::Read(std::string_view a_path, std::string* a_source)
	{
		const auto key = Normalize(a_path);
		if (const auto loose = FindLoose(key)) {
			std::ifstream in(*loose, std::ios::binary | std::ios::ate);
			if (in) {
				const auto             size = static_cast<std::size_t>(std::max<std::streamoff>(in.tellg(), 0));
				std::vector<std::byte> data(size);
				in.seekg(0);
				if (in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size))) {
					if (a_source) {
						*a_source = PathToUtf8(*loose);
					}
					return data;
				}
			}
		}
		OpenArchives();
		for (auto it = _archives.rbegin(); it != _archives.rend(); ++it) {
			if (*it && (*it)->Contains(key)) {
				if (auto data = (*it)->Read(key)) {
					if (a_source) {
						*a_source = PathToUtf8((*it)->Path().filename());
					}
					return data;
				}
			}
		}
		return std::nullopt;
	}
}
