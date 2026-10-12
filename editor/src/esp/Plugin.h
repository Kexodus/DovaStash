#pragma once

// Memory-mapped reader for Skyrim SE plugin files (.esm/.esp/.esl), the C++ counterpart of
// tools/esm_dump.py's Plugin class: TES4 header (masters, light/localized flags), top-level groups,
// a generic group walker and on-demand subrecords (zlib-compressed records inflated). Read-only.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dova::editor::esp
{
	using Sig = std::uint32_t;

	[[nodiscard]] constexpr Sig MakeSig(const char (&a_s)[5]) noexcept
	{
		return static_cast<Sig>(static_cast<unsigned char>(a_s[0])) | (static_cast<Sig>(static_cast<unsigned char>(a_s[1])) << 8) |
		       (static_cast<Sig>(static_cast<unsigned char>(a_s[2])) << 16) | (static_cast<Sig>(static_cast<unsigned char>(a_s[3])) << 24);
	}
	[[nodiscard]] std::string SigToString(Sig a_sig);

	inline constexpr Sig kGRUP = MakeSig("GRUP");

	inline constexpr std::uint32_t kRecDeleted = 0x20;
	inline constexpr std::uint32_t kRecPersistent = 0x400;
	inline constexpr std::uint32_t kRecInitiallyDisabled = 0x800;
	inline constexpr std::uint32_t kRecCompressed = 0x40000;

	template <class T>
	[[nodiscard]] T Read(const std::byte* a_p) noexcept
	{
		T v;
		std::memcpy(&v, a_p, sizeof(T));
		return v;
	}
	template <class T>
	[[nodiscard]] T Read(std::span<const std::byte> a_data, std::size_t a_offset = 0) noexcept
	{
		if (a_offset + sizeof(T) > a_data.size()) {
			return T{};
		}
		return Read<T>(a_data.data() + a_offset);
	}

	struct GroupHeader
	{
		std::size_t   offset{ 0 };
		std::uint32_t size{ 0 };  // includes the 24-byte header
		std::uint32_t label{ 0 };
		std::int32_t  type{ 0 };  // 0 top, 1 world children, 2/3 interior blocks, 4/5 exterior blocks, 6 cell children, 8/9/10 refs
	};

	struct RecordHeader
	{
		Sig           type{ 0 };
		std::uint32_t flags{ 0 };
		std::uint32_t rawId{ 0 };  // file-local form id (master index in the top byte)
		std::size_t   offset{ 0 };
	};

	// Subrecords of one record. Spans point into the mapped file or into `_inflated`; not copyable.
	class Subrecords
	{
	public:
		Subrecords() = default;
		Subrecords(const Subrecords&) = delete;
		Subrecords& operator=(const Subrecords&) = delete;
		Subrecords(Subrecords&&) noexcept = default;
		Subrecords& operator=(Subrecords&&) noexcept = default;

		[[nodiscard]] std::optional<std::span<const std::byte>> Get(Sig a_sig) const;
		[[nodiscard]] std::vector<std::span<const std::byte>>   GetAll(Sig a_sig) const;
		[[nodiscard]] const auto&                               Items() const noexcept { return _items; }

	private:
		friend class Plugin;
		std::vector<std::byte>                                  _inflated;
		std::vector<std::pair<Sig, std::span<const std::byte>>> _items;
	};

	class Plugin
	{
	public:
		// `a_name` is the name the load order uses for the file (masters name it this way).
		[[nodiscard]] static std::unique_ptr<Plugin> Open(const std::filesystem::path& a_path, std::string a_name, std::string& a_error);
		~Plugin();
		Plugin(const Plugin&) = delete;
		Plugin& operator=(const Plugin&) = delete;

		[[nodiscard]] const std::string&              Name() const noexcept { return _name; }
		[[nodiscard]] const std::filesystem::path&    Path() const noexcept { return _path; }
		[[nodiscard]] const std::vector<std::string>& Masters() const noexcept { return _masters; }
		[[nodiscard]] bool                            Light() const noexcept { return _light; }
		[[nodiscard]] bool                            Localized() const noexcept { return _localized; }
		[[nodiscard]] bool                            MasterFlag() const noexcept { return (_headerFlags & 1) != 0; }
		[[nodiscard]] std::span<const std::byte>      Data() const noexcept { return { _data, _size }; }

		// Top-level group of a record type ("CELL", "WRLD", "CONT"...).
		[[nodiscard]] std::optional<GroupHeader> TopGroup(Sig a_type) const;

		[[nodiscard]] Subrecords ReadSubrecords(std::size_t a_recordOffset) const;

		// Visits [a_start, a_end): a_onGroup(const GroupHeader&) -> bool (descend?), a_onRecord(const RecordHeader&).
		// Stops quietly at anything malformed (a size running past the end).
		template <class OnGroup, class OnRecord>
		void Walk(std::size_t a_start, std::size_t a_end, OnGroup& a_onGroup, OnRecord& a_onRecord) const
		{
			if (a_end > _size) {
				a_end = _size;
			}
			std::size_t pos = a_start;
			while (pos + 24 <= a_end) {
				const Sig           type = Read<Sig>(_data + pos);
				const std::uint32_t size = Read<std::uint32_t>(_data + pos + 4);
				if (type == kGRUP) {
					if (size < 24 || pos + size > a_end) {
						return;
					}
					const GroupHeader g{ pos, size, Read<std::uint32_t>(_data + pos + 8), Read<std::int32_t>(_data + pos + 12) };
					if (a_onGroup(g)) {
						Walk(pos + 24, pos + size, a_onGroup, a_onRecord);
					}
					pos += size;
				} else {
					if (pos + 24 + size > a_end) {
						return;
					}
					a_onRecord(RecordHeader{ type, Read<std::uint32_t>(_data + pos + 8), Read<std::uint32_t>(_data + pos + 12), pos });
					pos += 24 + static_cast<std::size_t>(size);
				}
			}
		}

		// Records directly inside the top-level group of `a_type` (children groups skipped).
		template <class F>
		void ForEachTopRecord(Sig a_type, F&& a_fn) const
		{
			const auto g = TopGroup(a_type);
			if (!g) {
				return;
			}
			auto onGroup = [](const GroupHeader&) { return false; };
			auto onRecord = [&](const RecordHeader& r) {
				if (r.type == a_type) {
					a_fn(r);
				}
			};
			Walk(g->offset + 24, g->offset + g->size, onGroup, onRecord);
		}

	private:
		Plugin() = default;

		std::filesystem::path                 _path;
		std::string                           _name;
		void*                                 _file{ nullptr };
		void*                                 _mapping{ nullptr };
		const std::byte*                      _data{ nullptr };
		std::size_t                           _size{ 0 };
		std::uint32_t                         _headerFlags{ 0 };
		bool                                  _light{ false };
		bool                                  _localized{ false };
		std::vector<std::string>              _masters;
		std::unordered_map<Sig, GroupHeader>  _top;
	};

	// Splits raw subrecord bytes (handles XXXX for oversized fields).
	void ParseSubrecords(std::span<const std::byte> a_data, std::vector<std::pair<Sig, std::span<const std::byte>>>& a_out);

	// NUL-terminated string field as UTF-8.
	[[nodiscard]] std::string ZString(std::span<const std::byte> a_data);
}
