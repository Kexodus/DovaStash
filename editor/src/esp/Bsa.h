#pragma once

// Minimal reader for Skyrim BSA archives (v104 LE, v105 SE), ported from tools/esm_dump.py: single files
// by path (the Strings tables of localized plugins, meshes and textures for the 3D view). Thread-safe after
// Open: every Read opens the file for itself (no handle is kept, so MO2 can still replace or remove a mod
// while the editor runs). The file table is keyed by a 64-bit hash of the normalized path, so the hundreds
// of archives of a big load order stay small in memory. The file's size and time are kept from Open:
// Changed() tells when the file on disk is no longer the one indexed (the caller then opens it again).

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
		// nullopt when the archive does not have it, or (with `a_error` set) when it could not be read.
		[[nodiscard]] std::optional<std::vector<std::byte>> Read(std::string_view a_key, std::string* a_error = nullptr) const;
		// The file at Path() is not the one Open indexed any more (another size or time, or gone).
		[[nodiscard]] bool                                  Changed() const;
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
		std::uint64_t                          _fileSize{ 0 };
		std::uint64_t                          _writeTime{ 0 };
		std::uint32_t                          _version{ 0 };
		bool                                   _embedNames{ false };
		std::unordered_map<std::uint64_t, Entry> _files;  // HashKey(NormalizeKey(path))
	};

	// A .STRINGS / .DLSTRINGS / .ILSTRINGS table: string id -> UTF-8 text.
	[[nodiscard]] std::unordered_map<std::uint32_t, std::string> ParseStringsFile(const std::vector<std::byte>& a_raw, bool a_lengthPrefixed);
}
