#include "esp/Plugin.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <zlib.h>

#include "Util.h"
#include "dova/core/FormRef.h"

namespace dova::editor::esp
{
	std::string SigToString(Sig a_sig)
	{
		std::string s(4, ' ');
		for (int i = 0; i < 4; ++i) {
			const auto c = static_cast<char>((a_sig >> (8 * i)) & 0xFF);
			s[static_cast<std::size_t>(i)] = (c >= 32 && c < 127) ? c : '?';
		}
		return s;
	}

	std::string ZString(std::span<const std::byte> a_data)
	{
		const auto* p = reinterpret_cast<const char*>(a_data.data());
		std::size_t n = 0;
		while (n < a_data.size() && p[n] != '\0') {
			++n;
		}
		return BytesToUtf8(std::string_view(p, n));
	}

	void ParseSubrecords(std::span<const std::byte> a_data, std::vector<std::pair<Sig, std::span<const std::byte>>>& a_out)
	{
		std::size_t                  p = 0;
		std::optional<std::uint32_t> big;
		while (p + 6 <= a_data.size()) {
			const Sig     t = Read<Sig>(a_data.data() + p);
			std::uint32_t sz = Read<std::uint16_t>(a_data.data() + p + 4);
			p += 6;
			if (t == MakeSig("XXXX")) {
				big = Read<std::uint32_t>(a_data, p);
				p += sz;
				continue;
			}
			if (big) {
				sz = *big;
				big.reset();
			}
			if (p + sz > a_data.size()) {
				return;
			}
			a_out.emplace_back(t, a_data.subspan(p, sz));
			p += sz;
		}
	}

	std::optional<std::span<const std::byte>> Subrecords::Get(Sig a_sig) const
	{
		for (const auto& [t, d] : _items) {
			if (t == a_sig) {
				return d;
			}
		}
		return std::nullopt;
	}

	std::vector<std::span<const std::byte>> Subrecords::GetAll(Sig a_sig) const
	{
		std::vector<std::span<const std::byte>> out;
		for (const auto& [t, d] : _items) {
			if (t == a_sig) {
				out.push_back(d);
			}
		}
		return out;
	}

	std::unique_ptr<Plugin> Plugin::Open(const std::filesystem::path& a_path, std::string a_name, std::string& a_error)
	{
		std::unique_ptr<Plugin> pl(new Plugin());
		pl->_path = a_path;
		pl->_name = std::move(a_name);
		HANDLE file = ::CreateFileW(a_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			a_error = "cannot open " + PathToUtf8(a_path);
			return nullptr;
		}
		pl->_file = file;
		LARGE_INTEGER size{};
		::GetFileSizeEx(file, &size);
		pl->_size = static_cast<std::size_t>(size.QuadPart);
		if (pl->_size < 24) {
			a_error = PathToUtf8(a_path) + " is too small to be a plugin";
			return nullptr;
		}
		pl->_mapping = ::CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
		if (!pl->_mapping) {
			a_error = "cannot map " + PathToUtf8(a_path);
			return nullptr;
		}
		pl->_data = static_cast<const std::byte*>(::MapViewOfFile(pl->_mapping, FILE_MAP_READ, 0, 0, 0));
		if (!pl->_data) {
			a_error = "cannot map " + PathToUtf8(a_path);
			return nullptr;
		}
		if (Read<Sig>(pl->_data) != MakeSig("TES4")) {
			a_error = PathToUtf8(a_path) + " is not a TES4 plugin";
			return nullptr;
		}
		const auto hsize = Read<std::uint32_t>(pl->_data + 4);
		pl->_headerFlags = Read<std::uint32_t>(pl->_data + 8);
		if (24 + static_cast<std::size_t>(hsize) > pl->_size) {
			a_error = PathToUtf8(a_path) + ": broken header";
			return nullptr;
		}
		std::vector<std::pair<Sig, std::span<const std::byte>>> subs;
		ParseSubrecords(std::span(pl->_data + 24, hsize), subs);
		for (const auto& [t, d] : subs) {
			if (t == MakeSig("MAST")) {
				pl->_masters.push_back(ZString(d));
			}
		}
		pl->_localized = (pl->_headerFlags & 0x80) != 0;
		pl->_light = (pl->_headerFlags & 0x200) != 0 || IEndsWith(PathToUtf8(a_path.filename()), ".esl");

		std::size_t pos = 24 + hsize;
		while (pos + 24 <= pl->_size) {
			if (Read<Sig>(pl->_data + pos) != kGRUP) {
				a_error = PathToUtf8(a_path) + ": expected a top-level group";
				return nullptr;
			}
			const auto gsize = Read<std::uint32_t>(pl->_data + pos + 4);
			if (gsize < 24 || pos + gsize > pl->_size) {
				break;
			}
			const auto label = Read<std::uint32_t>(pl->_data + pos + 8);
			pl->_top.emplace(label, GroupHeader{ pos, gsize, label, 0 });
			pos += gsize;
		}
		return pl;
	}

	Plugin::~Plugin()
	{
		if (_data) {
			::UnmapViewOfFile(_data);
		}
		if (_mapping) {
			::CloseHandle(_mapping);
		}
		if (_file) {
			::CloseHandle(_file);
		}
	}

	std::optional<GroupHeader> Plugin::TopGroup(Sig a_type) const
	{
		const auto it = _top.find(a_type);
		if (it == _top.end()) {
			return std::nullopt;
		}
		return it->second;
	}

	Subrecords Plugin::ReadSubrecords(std::size_t a_recordOffset) const
	{
		Subrecords out;
		if (a_recordOffset + 24 > _size) {
			return out;
		}
		const auto size = Read<std::uint32_t>(_data + a_recordOffset + 4);
		const auto flags = Read<std::uint32_t>(_data + a_recordOffset + 8);
		if (a_recordOffset + 24 + size > _size) {
			return out;
		}
		std::span<const std::byte> data(_data + a_recordOffset + 24, size);
		if (flags & kRecCompressed) {
			if (size < 4) {
				return out;
			}
			uLongf outLen = Read<std::uint32_t>(data.data());
			out._inflated.resize(outLen);
			if (::uncompress(reinterpret_cast<Bytef*>(out._inflated.data()), &outLen, reinterpret_cast<const Bytef*>(data.data() + 4),
					static_cast<uLong>(size - 4)) != Z_OK) {
				out._inflated.clear();
				return out;
			}
			out._inflated.resize(outLen);
			data = out._inflated;
		}
		ParseSubrecords(data, out._items);
		return out;
	}
}
