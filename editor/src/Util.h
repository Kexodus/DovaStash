#pragma once

// Small helpers shared by the editor: UTF-8 <-> wide strings and paths, text files, a minimal INI
// reader for MO2's Qt-written files. Windows only (the editor is a Win32 program).

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dova::editor
{
	[[nodiscard]] std::wstring          Widen(std::string_view a_utf8);
	[[nodiscard]] std::string           Narrow(std::wstring_view a_wide);
	[[nodiscard]] std::string           PathToUtf8(const std::filesystem::path& a_path);
	[[nodiscard]] std::filesystem::path PathFromUtf8(std::string_view a_utf8);

	// Text from a plugin or archive: kept when it is valid UTF-8, otherwise read as Windows-1252
	// (what the Creation Kit writes for English plugins) and converted to UTF-8.
	[[nodiscard]] std::string BytesToUtf8(std::string_view a_bytes);

	[[nodiscard]] std::string_view Trim(std::string_view a_text);
	[[nodiscard]] bool             IContains(std::string_view a_haystack, std::string_view a_needle);
	[[nodiscard]] bool             IEndsWith(std::string_view a_text, std::string_view a_suffix);

	// Whole file as bytes (a UTF-8 BOM is removed). nullopt when it cannot be read.
	[[nodiscard]] std::optional<std::string> ReadTextFile(const std::filesystem::path& a_path);
	[[nodiscard]] std::vector<std::string>   SplitLines(std::string_view a_text);

	// MO2 writes its INI files with Qt's QSettings: values may be wrapped in @ByteArray(...), backslashes
	// are doubled and strings may be quoted. Section and key names compare case-insensitively.
	class IniFile
	{
	public:
		[[nodiscard]] static std::optional<IniFile> Load(const std::filesystem::path& a_path);

		[[nodiscard]] std::optional<std::string> Get(std::string_view a_section, std::string_view a_key) const;

	private:
		std::map<std::string, std::map<std::string, std::string>> _sections;  // lower-case names
	};

	[[nodiscard]] std::string QtUnescape(std::string_view a_value);

	// The largest group of points chained `a_gap` units apart or closer (the one with the most `a_weighted` points,
	// then the most points): the home without a prefab or marker a mod parked far away. Indices into `a_points`, in
	// order. One rule for the 3D overview (render::Scene::SceneBounds) and the plan's Home fit.
	[[nodiscard]] std::vector<std::size_t> LargestGroup(const std::vector<std::array<float, 3>>& a_points, const std::vector<bool>& a_weighted, float a_gap = 1024.0f);
}
