#pragma once

// Minimal reader for Skyrim BSA archives (v104 LE, v105 SE), ported from tools/esm_dump.py: enough to
// pull single files (the Strings tables of localized plugins). M2 reads meshes and textures through the
// same interface, or switches to rsm-bsa (docs/EDITOR.md §5.4).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dova::editor::esp
{
	class Bsa
	{
	public:
		[[nodiscard]] static std::unique_ptr<Bsa> Open(const std::filesystem::path& a_path);

		// `a_key` like "strings\\skyrim_english.strings" (either slash, any case).
		[[nodiscard]] std::optional<std::vector<std::byte>> Read(std::string_view a_key) const;

	private:
		struct Entry
		{
			std::uint32_t offset{ 0 };
			std::uint32_t size{ 0 };
			bool          compressed{ false };
		};

		std::filesystem::path                  _path;
		std::uint32_t                          _version{ 0 };
		bool                                   _embedNames{ false };
		std::unordered_map<std::string, Entry> _files;
	};

	// A .STRINGS / .DLSTRINGS / .ILSTRINGS table: string id -> UTF-8 text.
	[[nodiscard]] std::unordered_map<std::uint32_t, std::string> ParseStringsFile(const std::vector<std::byte>& a_raw, bool a_lengthPrefixed);
}
