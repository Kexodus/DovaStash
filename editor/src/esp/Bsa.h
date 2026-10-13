#pragma once

// Minimal reader for Skyrim BSA archives (v104 LE, v105 SE), ported from tools/esm_dump.py: single files
// by path (the Strings tables of localized plugins, meshes and textures for the 3D view). Thread-safe after
// Open: every Read opens its own stream. The file table is keyed by a 64-bit hash of the normalized path,
// so the hundreds of archives of a big load order stay small in memory.

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
		[[nodiscard]] bool                                  Contains(std::string_view a_key) const;
		[[nodiscard]] const std::filesystem::path&          Path() const noexcept { return _path; }
		[[nodiscard]] std::size_t                           FileCount() const noexcept { return _files.size(); }

		// Lower case, backslashes: the form every key is hashed in.
		[[nodiscard]] static std::string     NormalizeKey(std::string_view a_key);
		[[nodiscard]] static std::uint64_t   HashKey(std::string_view a_normalized) noexcept;

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
		std::unordered_map<std::uint64_t, Entry> _files;  // HashKey(NormalizeKey(path))
	};

	// A .STRINGS / .DLSTRINGS / .ILSTRINGS table: string id -> UTF-8 text.
	[[nodiscard]] std::unordered_map<std::uint32_t, std::string> ParseStringsFile(const std::vector<std::byte>& a_raw, bool a_lengthPrefixed);
}
