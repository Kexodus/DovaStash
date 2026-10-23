#include "esp/Bsa.h"

#include <algorithm>
#include <format>
#include <fstream>

#include <lz4frame.h>
#include <zlib.h>

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include "Util.h"
#include "dova/core/FormRef.h"
#include "esp/Plugin.h"

namespace dova::editor::esp
{
	namespace
	{
		template <class T>
		bool Get(std::ifstream& a_in, T& a_out)
		{
			return static_cast<bool>(a_in.read(reinterpret_cast<char*>(&a_out), sizeof(T)));
		}

		// Size and last write time of a file (false when it cannot be read).
		bool Identity(const std::filesystem::path& a_path, std::uint64_t& a_size, std::uint64_t& a_time)
		{
			WIN32_FILE_ATTRIBUTE_DATA d{};
			if (!::GetFileAttributesExW(a_path.c_str(), GetFileExInfoStandard, &d)) {
				return false;
			}
			a_size = (static_cast<std::uint64_t>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
			a_time = (static_cast<std::uint64_t>(d.ftLastWriteTime.dwHighDateTime) << 32) | d.ftLastWriteTime.dwLowDateTime;
			return true;
		}

		// No archived file is anywhere near this large: a bigger "original size" means the bytes are not the entry's.
		constexpr std::uint32_t kMaxEntrySize = 512u << 20;
	}

	std::string Bsa::NormalizeKey(std::string_view a_key)
	{
		std::string k = core::ToLower(a_key);
		std::ranges::replace(k, '/', '\\');
		return k;
	}

	namespace
	{
		std::uint64_t Key(std::string_view a_key) { return Bsa::HashKey(Bsa::NormalizeKey(a_key)); }
	}

	std::uint64_t Bsa::HashKey(std::string_view a_normalized) noexcept
	{
		std::uint64_t h = 0xcbf29ce484222325ull;  // FNV-1a
		for (const char c : a_normalized) {
			h ^= static_cast<unsigned char>(c);
			h *= 0x100000001b3ull;
		}
		return h;
	}

	bool Bsa::Contains(std::string_view a_key) const { return _files.contains(Key(a_key)); }

	std::unique_ptr<Bsa> Bsa::Open(const std::filesystem::path& a_path)
	{
		std::ifstream in(a_path, std::ios::binary);
		if (!in) {
			return nullptr;
		}
		char          magic[4]{};
		std::uint32_t ver = 0, folderOffset = 0, archiveFlags = 0, folderCount = 0, fileCount = 0, folderNamesLen = 0, fileNamesLen = 0;
		in.read(magic, 4);
		if (!in || std::string_view(magic, 4) != std::string_view("BSA\0", 4)) {
			return nullptr;
		}
		Get(in, ver);
		Get(in, folderOffset);
		Get(in, archiveFlags);
		Get(in, folderCount);
		Get(in, fileCount);
		Get(in, folderNamesLen);
		Get(in, fileNamesLen);
		if (!in || (ver != 103 && ver != 104 && ver != 105)) {
			return nullptr;
		}
		auto bsa = std::make_unique<Bsa>();
		bsa->_path = a_path;
		if (!Identity(a_path, bsa->_fileSize, bsa->_writeTime)) {
			return nullptr;
		}
		bsa->_version = ver;
		const bool compressedDefault = (archiveFlags & 0x4) != 0;
		bsa->_embedNames = (archiveFlags & 0x100) != 0 && ver != 103;
		const std::streamoff folderRecordSize = ver == 105 ? 24 : 16;

		in.seekg(folderOffset);
		std::vector<std::uint32_t> counts(folderCount);
		for (auto& c : counts) {
			char rec[24];
			in.read(rec, folderRecordSize);
			std::memcpy(&c, rec + 8, 4);
		}
		struct Pending
		{
			std::string   folder;
			std::uint32_t size;
			std::uint32_t offset;
		};
		std::vector<Pending> entries;
		entries.reserve(fileCount);
		for (const auto count : counts) {
			unsigned char len = 0;
			Get(in, len);
			std::string folder(len, '\0');
			in.read(folder.data(), len);
			while (!folder.empty() && folder.back() == '\0') {
				folder.pop_back();
			}
			for (std::uint32_t i = 0; i < count; ++i) {
				std::uint64_t hash = 0;
				std::uint32_t size = 0, offset = 0;
				Get(in, hash);
				Get(in, size);
				Get(in, offset);
				entries.push_back({ folder, size, offset });
			}
		}
		std::string names(fileNamesLen, '\0');
		in.read(names.data(), fileNamesLen);
		if (!in) {
			return nullptr;
		}
		std::size_t p = 0;
		for (const auto& e : entries) {
			const auto end = names.find('\0', p);
			if (end == std::string::npos) {
				break;
			}
			const auto name = std::string_view(names).substr(p, end - p);
			p = end + 1;
			bool comp = compressedDefault;
			if (e.size & 0x40000000) {
				comp = !comp;
			}
			bsa->_files[Key(e.folder + "\\" + std::string(name))] = Entry{ e.offset, e.size & 0x3FFFFFFF, comp };
		}
		return bsa;
	}

	bool Bsa::Changed() const
	{
		std::uint64_t size = 0, time = 0;
		return !Identity(_path, size, time) || size != _fileSize || time != _writeTime;
	}

	std::optional<std::vector<std::byte>> Bsa::Read(std::string_view a_key, std::string* a_error) const
	{
		const auto it = _files.find(Key(a_key));
		if (it == _files.end()) {
			return std::nullopt;
		}
		auto fail = [&](std::string a_why) -> std::optional<std::vector<std::byte>> {
			if (a_error) {
				*a_error = PathToUtf8(_path.filename()) + ": " + std::move(a_why);
			}
			return std::nullopt;
		};
		const auto& e = it->second;
		if (static_cast<std::uint64_t>(e.offset) + e.size > _fileSize) {
			return fail("the entry lies past the end of the file");
		}
		// Win32 with an explicit offset: a real error code, and no C runtime stream (their number is limited).
		const HANDLE file = ::CreateFileW(_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			return fail(std::format("cannot open the archive (Win32 error {})", ::GetLastError()));
		}
		// The same file as indexed? (Replaced by a mod update, the old offsets would read other bytes.)
		BY_HANDLE_FILE_INFORMATION info{};
		if (!::GetFileInformationByHandle(file, &info) ||
			((static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow) != _fileSize ||
			((static_cast<std::uint64_t>(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime) != _writeTime) {
			::CloseHandle(file);
			return fail("the archive changed on disk since it was indexed");
		}
		std::vector<std::byte> blob(e.size);
		std::size_t            got = 0;
		DWORD                  error = 0;
		while (got < blob.size()) {
			OVERLAPPED at{};
			const auto pos = static_cast<std::uint64_t>(e.offset) + got;
			at.Offset = static_cast<DWORD>(pos & 0xFFFFFFFFu);
			at.OffsetHigh = static_cast<DWORD>(pos >> 32);
			DWORD n = 0;
			if (!::ReadFile(file, blob.data() + got, static_cast<DWORD>(blob.size() - got), &n, &at)) {
				error = ::GetLastError();
				break;
			}
			if (n == 0) {
				break;
			}
			got += n;
		}
		::CloseHandle(file);
		if (got < blob.size()) {
			return fail(error ? std::format("read error (Win32 error {})", error) : std::format("short read ({} of {} bytes at {})", got, e.size, e.offset));
		}
		std::size_t p = 0;
		if (_embedNames && !blob.empty()) {
			p = 1 + static_cast<std::size_t>(blob[0]);
		}
		if (!e.compressed) {
			return std::vector<std::byte>(blob.begin() + static_cast<std::ptrdiff_t>(std::min(p, blob.size())), blob.end());
		}
		if (p + 4 > blob.size()) {
			return fail("truncated entry");
		}
		const auto original = esp::Read<std::uint32_t>(blob.data() + p);
		if (original > kMaxEntrySize) {
			return fail(std::format("bad entry header (original size {})", original));
		}
		std::vector<std::byte> out(original);
		const auto*            src = blob.data() + p + 4;
		const std::size_t      srcLen = blob.size() - p - 4;
		if (_version == 105) {
			LZ4F_dctx* ctx = nullptr;
			if (LZ4F_isError(LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION))) {
				return fail("no LZ4 context");
			}
			std::size_t outLen = out.size(), inLen = srcLen;
			const auto  rc = LZ4F_decompress(ctx, out.data(), &outLen, src, &inLen, nullptr);
			LZ4F_freeDecompressionContext(ctx);
			if (LZ4F_isError(rc)) {
				return fail("LZ4 data is corrupt");
			}
			out.resize(outLen);
		} else {
			uLongf outLen = static_cast<uLongf>(out.size());
			if (::uncompress(reinterpret_cast<Bytef*>(out.data()), &outLen, reinterpret_cast<const Bytef*>(src), static_cast<uLong>(srcLen)) != Z_OK) {
				return fail("zlib data is corrupt");
			}
			out.resize(outLen);
		}
		return out;
	}

	std::unordered_map<std::uint32_t, std::string> ParseStringsFile(const std::vector<std::byte>& a_raw, bool a_lengthPrefixed)
	{
		std::unordered_map<std::uint32_t, std::string> out;
		const std::span<const std::byte>               raw(a_raw);
		const auto                                     count = esp::Read<std::uint32_t>(raw, 0);
		const std::size_t                              base = 8 + static_cast<std::size_t>(count) * 8;
		if (base > raw.size()) {
			return out;
		}
		out.reserve(count);
		for (std::uint32_t k = 0; k < count; ++k) {
			const auto        sid = esp::Read<std::uint32_t>(raw, 8 + k * 8);
			const std::size_t p = base + esp::Read<std::uint32_t>(raw, 8 + k * 8 + 4);
			if (p >= raw.size()) {
				continue;
			}
			const char* chars = reinterpret_cast<const char*>(raw.data());
			std::string_view s;
			if (a_lengthPrefixed) {
				const auto len = esp::Read<std::uint32_t>(raw, p);
				if (p + 4 + len > raw.size()) {
					continue;
				}
				s = std::string_view(chars + p + 4, len);
			} else {
				std::size_t e = p;
				while (e < raw.size() && chars[e] != '\0') {
					++e;
				}
				s = std::string_view(chars + p, e - p);
			}
			while (!s.empty() && s.back() == '\0') {
				s.remove_suffix(1);
			}
			out.emplace(sid, BytesToUtf8(s));
		}
		return out;
	}
}
