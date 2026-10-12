#include "mo2/Instance.h"

#include <algorithm>
#include <charconv>
#include <system_error>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <TlHelp32.h>

#include "Util.h"
#include "dova/core/FormRef.h"

namespace fs = std::filesystem;

namespace dova::editor::mo2
{
	namespace
	{
		// "%BASE_DIR%/mods" style settings; empty -> default.
		fs::path Directory(const IniFile& a_ini, std::string_view a_key, const fs::path& a_base, std::string_view a_default)
		{
			auto value = a_ini.Get("Settings", a_key);
			if (!value || value->empty()) {
				return a_base / a_default;
			}
			std::string v = *value;
			if (const auto p = v.find("%BASE_DIR%"); p != std::string::npos) {
				v.replace(p, 10, PathToUtf8(a_base));
			}
			return PathFromUtf8(v).lexically_normal();
		}

		bool IsPluginFile(const fs::path& a_path)
		{
			const auto ext = core::ToLower(PathToUtf8(a_path.extension()));
			return ext == ".esm" || ext == ".esp" || ext == ".esl";
		}

		std::optional<fs::path> ParentProcessImage()
		{
			const DWORD self = ::GetCurrentProcessId();
			DWORD       parent = 0;
			HANDLE      snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
			if (snap == INVALID_HANDLE_VALUE) {
				return std::nullopt;
			}
			PROCESSENTRY32W pe{};
			pe.dwSize = sizeof(pe);
			for (BOOL ok = ::Process32FirstW(snap, &pe); ok; ok = ::Process32NextW(snap, &pe)) {
				if (pe.th32ProcessID == self) {
					parent = pe.th32ParentProcessID;
					break;
				}
			}
			::CloseHandle(snap);
			if (parent == 0) {
				return std::nullopt;
			}
			HANDLE proc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parent);
			if (!proc) {
				return std::nullopt;
			}
			wchar_t buf[MAX_PATH * 4];
			DWORD   len = static_cast<DWORD>(std::size(buf));
			const bool ok = ::QueryFullProcessImageNameW(proc, 0, buf, &len) != 0;
			::CloseHandle(proc);
			if (!ok) {
				return std::nullopt;
			}
			return fs::path(std::wstring(buf, len));
		}

		std::optional<fs::path> RegistryCurrentInstance()
		{
			wchar_t buf[512];
			DWORD   size = sizeof(buf);
			if (::RegGetValueW(HKEY_CURRENT_USER, L"Software\\Mod Organizer Team\\Mod Organizer", L"CurrentInstance", RRF_RT_REG_SZ, nullptr, buf,
					&size) != ERROR_SUCCESS) {
				return std::nullopt;
			}
			wchar_t local[MAX_PATH];
			if (!::GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) {
				return std::nullopt;
			}
			return fs::path(local) / L"ModOrganizer" / buf;
		}

		std::optional<fs::path> FindIniAbove(fs::path a_dir)
		{
			std::error_code ec;
			for (int depth = 0; depth < 8 && !a_dir.empty(); ++depth) {
				if (fs::exists(a_dir / L"ModOrganizer.ini", ec)) {
					return a_dir;
				}
				const auto parent = a_dir.parent_path();
				if (parent == a_dir) {
					break;
				}
				a_dir = parent;
			}
			return std::nullopt;
		}
	}

	std::optional<Instance> Instance::Load(const fs::path& a_root, std::string& a_error)
	{
		const auto ini = IniFile::Load(a_root / L"ModOrganizer.ini");
		if (!ini) {
			a_error = "no ModOrganizer.ini in " + PathToUtf8(a_root);
			return std::nullopt;
		}
		Instance inst;
		inst.root = a_root;
		inst.gameName = ini->Get("General", "gameName").value_or("");
		const auto gamePath = ini->Get("General", "gamePath");
		if (!gamePath || gamePath->empty()) {
			a_error = "ModOrganizer.ini has no gamePath";
			return std::nullopt;
		}
		inst.gamePath = PathFromUtf8(*gamePath).lexically_normal();
		inst.dataDir = inst.gamePath / L"Data";
		const fs::path base = Directory(*ini, "base_directory", a_root, "");
		inst.modsDir = Directory(*ini, "mod_directory", base, "mods");
		inst.profilesDir = Directory(*ini, "profiles_directory", base, "profiles");
		inst.overwriteDir = Directory(*ini, "overwrite_directory", base, "overwrite");
		inst.selectedProfile = ini->Get("General", "selected_profile").value_or("Default");

		std::error_code ec;
		for (const auto& e : fs::directory_iterator(inst.profilesDir, ec)) {
			if (e.is_directory(ec) && fs::exists(e.path() / L"modlist.txt", ec)) {
				inst.profiles.push_back(PathToUtf8(e.path().filename()));
			}
		}
		std::ranges::sort(inst.profiles, [](const auto& a, const auto& b) { return core::ICompare(a, b) < 0; });
		return inst;
	}

	std::vector<Mod> Instance::LoadMods(const std::string& a_profile, std::string& a_error) const
	{
		std::vector<Mod> mods;
		const auto       text = ReadTextFile(profilesDir / PathFromUtf8(a_profile) / L"modlist.txt");
		if (!text) {
			a_error = "cannot read modlist.txt of profile " + a_profile;
			return mods;
		}
		// modlist.txt lists the highest priority first.
		auto lines = SplitLines(*text);
		std::ranges::reverse(lines);
		std::error_code ec;
		for (const auto& raw : lines) {
			const auto line = Trim(raw);
			if (line.size() < 2 || line.front() == '#') {
				continue;
			}
			Mod m;
			m.name = std::string(line.substr(1));
			m.enabled = line.front() == '+' || line.front() == '*';
			m.unmanaged = line.front() == '*';
			m.separator = IEndsWith(m.name, "_separator");
			m.priority = static_cast<int>(mods.size());
			if (!m.unmanaged && !m.separator) {
				m.dir = modsDir / PathFromUtf8(m.name);
				if (const auto meta = IniFile::Load(m.dir / L"meta.ini")) {
					if (const auto id = meta->Get("General", "modid")) {
						int v = 0;
						if (std::from_chars(id->data(), id->data() + id->size(), v).ec == std::errc{} && v > 0) {
							m.nexusId = v;
						}
					}
					m.version = meta->Get("General", "version").value_or("");
					m.installationFile = meta->Get("General", "installationFile").value_or("");
				}
				for (const auto& e : fs::directory_iterator(m.dir, ec)) {
					if (!e.is_regular_file(ec)) {
						continue;
					}
					if (IsPluginFile(e.path())) {
						m.plugins.push_back(PathToUtf8(e.path().filename()));
					} else if (core::IEquals(PathToUtf8(e.path().extension()), ".bsa")) {
						m.archives.push_back(PathToUtf8(e.path().filename()));
					}
				}
				std::ranges::sort(m.plugins, [](const auto& a, const auto& b) { return core::ICompare(a, b) < 0; });
			}
			mods.push_back(std::move(m));
		}
		return mods;
	}

	std::vector<std::string> Instance::LoadActivePlugins(const std::string& a_profile) const
	{
		std::vector<std::string> enabled;
		const auto               dir = profilesDir / PathFromUtf8(a_profile);
		if (const auto text = ReadTextFile(dir / L"plugins.txt")) {
			for (const auto& raw : SplitLines(*text)) {
				const auto line = Trim(raw);
				if (line.size() > 1 && line.front() == '*') {
					enabled.emplace_back(line.substr(1));
				}
			}
		}
		const auto order = ReadTextFile(dir / L"loadorder.txt");
		if (!order) {
			return enabled;
		}
		std::vector<std::string> out;
		for (const auto& raw : SplitLines(*order)) {
			const auto line = Trim(raw);
			if (line.empty() || line.front() == '#') {
				continue;
			}
			const bool on = std::ranges::any_of(enabled, [&](const auto& p) { return core::IEquals(p, line); });
			// Base masters are always active and are not listed in plugins.txt.
			const bool base = core::IEquals(line, "Skyrim.esm") || core::IEquals(line, "Update.esm") || core::IEquals(line, "Dawnguard.esm") ||
			                  core::IEquals(line, "HearthFires.esm") || core::IEquals(line, "Dragonborn.esm");
			if (on || base) {
				out.emplace_back(line);
			}
		}
		return out;
	}

	bool RunningUnderMo2Vfs()
	{
		return ::GetModuleHandleW(L"usvfs_x64.dll") != nullptr;
	}

	std::optional<fs::path> DetectInstance()
	{
		std::error_code ec;
		if (const auto parent = ParentProcessImage(); parent && core::IEquals(PathToUtf8(parent->filename()), "ModOrganizer.exe")) {
			const auto dir = parent->parent_path();
			if (fs::exists(dir / L"portable.txt", ec) && fs::exists(dir / L"ModOrganizer.ini", ec)) {
				return dir;
			}
			if (const auto reg = RegistryCurrentInstance(); reg && fs::exists(*reg / L"ModOrganizer.ini", ec)) {
				return reg;
			}
		}
		wchar_t exe[MAX_PATH * 4];
		const DWORD len = ::GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
		if (len > 0) {
			if (auto found = FindIniAbove(fs::path(std::wstring(exe, len)).parent_path())) {
				return found;
			}
		}
		return FindIniAbove(fs::current_path(ec));
	}
}
