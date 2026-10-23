#include "data/Assets.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <ShlObj.h>
#include <Windows.h>

#include "Log.h"
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

		enum class Listed
		{
			Yes,
			Absent,  // no such folder
			Failed   // could not be listed now (not remembered: asked again next time)
		};

		Listed ListInto(const fs::path& a_dir, std::unordered_set<std::string>& a_files, std::unordered_set<std::string>& a_dirs)
		{
			WIN32_FIND_DATAW fd{};
			const auto       pattern = (a_dir / L"*").native();
			HANDLE           h = ::FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
			if (h == INVALID_HANDLE_VALUE) {
				const DWORD err = ::GetLastError();
				if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND || err == ERROR_DIRECTORY) {
					return Listed::Absent;
				}
				Log("loose files: cannot list {} (Win32 error {})", PathToUtf8(a_dir), err);
				return Listed::Failed;
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
			const DWORD err = ::GetLastError();
			::FindClose(h);
			if (err != ERROR_NO_MORE_FILES) {
				Log("loose files: listing {} stopped (Win32 error {})", PathToUtf8(a_dir), err);
				return Listed::Failed;
			}
			return Listed::Yes;
		}

		// A whole loose file; `a_error` says why not, `a_gone` that it is not there any more.
		std::optional<std::vector<std::byte>> ReadWhole(const fs::path& a_path, std::string& a_error, bool& a_gone)
		{
			const HANDLE file = ::CreateFileW(a_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
				OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
			if (file == INVALID_HANDLE_VALUE) {
				const DWORD err = ::GetLastError();
				a_gone = err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
				a_error = std::format("{}: cannot open (Win32 error {})", PathToUtf8(a_path), err);
				return std::nullopt;
			}
			LARGE_INTEGER size{};
			std::optional<std::vector<std::byte>> out;
			if (!::GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > (1ll << 31)) {
				a_error = std::format("{}: no size (Win32 error {})", PathToUtf8(a_path), ::GetLastError());
			} else {
				std::vector<std::byte> data(static_cast<std::size_t>(size.QuadPart));
				std::size_t            got = 0;
				while (got < data.size()) {
					DWORD n = 0;
					if (!::ReadFile(file, data.data() + got, static_cast<DWORD>(data.size() - got), &n, nullptr) || n == 0) {
						break;
					}
					got += n;
				}
				if (got == data.size()) {
					out = std::move(data);
				} else {
					a_error = std::format("{}: short read ({} of {} bytes, Win32 error {})", PathToUtf8(a_path), got, data.size(), ::GetLastError());
				}
			}
			::CloseHandle(file);
			return out;
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
		std::vector<std::shared_ptr<const esp::Bsa>> opened(_archivePaths.size());
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
		std::size_t ok = 0;
		for (std::size_t i = 0; i < opened.size(); ++i) {
			if (opened[i]) {
				++ok;
			} else {
				Log("archive not opened: {}", PathToUtf8(_archivePaths[i]));
			}
		}
		Log("archives: {} of {} opened; {} loose roots", ok, opened.size(), _roots.size());
		{
			std::unique_lock write(_archivesMutex);
			_archives = std::move(opened);
		}
		_archivesOpen = true;
	}

	Assets::DirEntries Assets::ResolveDir(const std::string& a_dir)
	{
		if (const auto it = _dirs.find(a_dir); it != _dirs.end()) {
			return it->second;
		}
		DirEntries entries;
		bool       complete = true;  // every root answered: the listing is kept
		auto       list = [&](std::uint32_t a_root, const fs::path& a_path) {
            auto l = std::make_shared<Listing>();
            switch (ListInto(a_path, l->files, l->dirs)) {
            case Listed::Yes:
                entries.emplace_back(a_root, std::move(l));
                break;
            case Listed::Failed:
                complete = false;
                break;
            default:
                break;
            }
		};
		if (a_dir.empty()) {
			for (std::uint32_t r = 0; r < _roots.size(); ++r) {
				list(r, _roots[r]);
			}
		} else {
			const auto        slash = a_dir.rfind('\\');
			const std::string parent = slash == std::string::npos ? std::string{} : a_dir.substr(0, slash);
			const std::string leaf = slash == std::string::npos ? a_dir : a_dir.substr(slash + 1);
			const auto        parents = ResolveDir(parent);
			if (!_dirs.contains(parent)) {
				complete = false;  // the parent's listing is partial, so is this one
			}
			for (const auto& [root, listing] : parents) {
				if (listing->dirs.contains(leaf)) {
					list(root, _roots[root] / PathFromUtf8(a_dir));
				}
			}
		}
		if (complete) {
			_dirs.insert_or_assign(a_dir, entries);
		}
		return entries;
	}

	std::optional<fs::path> Assets::FindLoose(const std::string& a_normalized)
	{
		const auto        slash = a_normalized.rfind('\\');
		const std::string dir = slash == std::string::npos ? std::string{} : a_normalized.substr(0, slash);
		const std::string name = slash == std::string::npos ? a_normalized : a_normalized.substr(slash + 1);
		std::scoped_lock  lock(_mutex);
		const auto        entries = ResolveDir(dir);
		for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
			if (it->second->files.contains(name)) {
				return _roots[it->first] / PathFromUtf8(a_normalized);
			}
		}
		return std::nullopt;
	}

	void Assets::ForgetDir(const std::string& a_normalized)
	{
		const auto       slash = a_normalized.rfind('\\');
		const auto       dir = slash == std::string::npos ? std::string{} : a_normalized.substr(0, slash);
		std::scoped_lock lock(_mutex);
		_dirs.erase(dir);
	}

	std::vector<std::pair<std::size_t, std::shared_ptr<const esp::Bsa>>> Assets::ArchivesWith(const std::string& a_key)
	{
		OpenArchives();
		std::vector<std::pair<std::size_t, std::shared_ptr<const esp::Bsa>>> out;
		std::shared_lock                                                      lock(_archivesMutex);
		for (std::size_t i = _archives.size(); i-- > 0;) {
			if (_archives[i] && _archives[i]->Contains(a_key)) {
				out.emplace_back(i, _archives[i]);
			}
		}
		return out;
	}

	std::shared_ptr<const esp::Bsa> Assets::Reindex(std::size_t a_index, const std::shared_ptr<const esp::Bsa>& a_stale)
	{
		std::unique_lock lock(_archivesMutex);
		if (_archives[a_index] != a_stale) {
			return _archives[a_index];  // another thread was first
		}
		std::shared_ptr<const esp::Bsa> fresh;
		try {
			fresh = esp::Bsa::Open(_archivePaths[a_index]);
		} catch (...) {
			fresh.reset();
		}
		Log("archive changed on disk, indexed again: {} ({})", PathToUtf8(_archivePaths[a_index]), fresh ? "ok" : "gone or unreadable");
		_archives[a_index] = fresh;
		return fresh;
	}

	void Assets::NoteFailure(std::string_view a_what)
	{
		const int n = ++_failuresLogged;
		if (n <= 100) {
			Log("read failed: {}", a_what);
		} else if (n == 101) {
			Log("read failed: more failures are not logged");
		}
	}

	bool Assets::Exists(std::string_view a_path)
	{
		const auto key = Normalize(a_path);
		return FindLoose(key) || !ArchivesWith(key).empty();
	}

	std::optional<std::vector<std::byte>> Assets::Read(std::string_view a_path, std::string* a_source, std::string* a_error)
	{
		const auto  key = Normalize(a_path);
		std::string why;  // the first reason a copy that exists could not be read
		auto        failed = [&](std::string a_what) {
            NoteFailure(a_what);
            if (why.empty()) {
                why = std::move(a_what);
            }
		};
		// Loose files win. A listed file that cannot be opened: its folder changed (a mod removed or replaced),
		// so the folder is listed again once and another root may have it.
		for (int attempt = 0; attempt < 2; ++attempt) {
			const auto loose = FindLoose(key);
			if (!loose) {
				break;
			}
			std::string error;
			bool        gone = false;
			if (auto data = ReadWhole(*loose, error, gone)) {
				if (a_source) {
					*a_source = PathToUtf8(*loose);
				}
				return data;
			}
			if (gone) {
				NoteFailure(error);  // removed since its folder was listed: not a copy that exists
			} else {
				failed(std::move(error));
			}
			ForgetDir(key);
		}
		for (auto& [index, bsa] : ArchivesWith(key)) {
			std::string error;
			if (auto data = bsa->Read(key, &error)) {
				if (a_source) {
					*a_source = PathToUtf8(bsa->Path().filename());
				}
				return data;
			}
			if (bsa->Changed()) {
				// Replaced on disk since it was indexed (a mod updated): the old offsets read the wrong bytes.
				const auto fresh = Reindex(index, bsa);
				if (!fresh || !fresh->Contains(key)) {
					continue;
				}
				if (auto data = fresh->Read(key, &error)) {
					if (a_source) {
						*a_source = PathToUtf8(fresh->Path().filename());
					}
					return data;
				}
			}
			failed(std::move(error));
		}
		if (a_error) {
			*a_error = std::move(why);
		}
		return std::nullopt;
	}
}
