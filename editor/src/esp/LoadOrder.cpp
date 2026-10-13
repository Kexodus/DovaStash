#include "esp/LoadOrder.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <unordered_map>

#include "Util.h"
#include "data/DataFiles.h"

namespace dova::editor::esp
{
	namespace
	{
		// Record types kept in the form index: what placed references point at, plus what cell headers,
		// display scripts and templates refer to.
		constexpr Sig kIndexedTypes[] = {
			MakeSig("ACTI"), MakeSig("ADDN"), MakeSig("ALCH"), MakeSig("AMMO"), MakeSig("ARMO"), MakeSig("BOOK"), MakeSig("CONT"),
			MakeSig("DOOR"), MakeSig("ECZN"), MakeSig("FACT"), MakeSig("FLOR"), MakeSig("FLST"), MakeSig("FURN"), MakeSig("IDLM"),
			MakeSig("INGR"), MakeSig("KEYM"), MakeSig("KYWD"), MakeSig("LCTN"), MakeSig("LIGH"), MakeSig("LVLC"), MakeSig("LVLN"),
			MakeSig("MISC"), MakeSig("MSTT"), MakeSig("NPC_"), MakeSig("SCRL"), MakeSig("SLGM"), MakeSig("SOUN"), MakeSig("STAT"),
			MakeSig("TACT"), MakeSig("TREE"), MakeSig("TXST"), MakeSig("WEAP"), MakeSig("WRLD"), MakeSig("SPEL"), MakeSig("HAZD"),
			MakeSig("ARTO"), MakeSig("MOVS"), MakeSig("GRAS"), MakeSig("ASPC"), MakeSig("LTEX"), MakeSig("ARMA"), MakeSig("RACE")
		};

		constexpr Sig kCELL = MakeSig("CELL");
		constexpr Sig kWRLD = MakeSig("WRLD");
		constexpr Sig kREFR = MakeSig("REFR");
		constexpr Sig kACHR = MakeSig("ACHR");
	}

	LoadOrder::LoadOrder(const DataFiles& a_files, std::string a_language) :
		_files(a_files),
		_language(std::move(a_language))
	{}

	LoadOrder::~LoadOrder() = default;

	std::uint32_t LoadOrder::FileId(std::string_view a_name)
	{
		const auto key = core::ToLower(a_name);
		if (const auto it = _fileIds.find(key); it != _fileIds.end()) {
			return it->second;
		}
		const auto id = static_cast<std::uint32_t>(_fileNames.size());
		_fileNames.emplace_back(a_name);
		_fileIds.emplace(key, id);
		return id;
	}

	const std::string& LoadOrder::FileName(std::uint32_t a_file) const
	{
		static const std::string unknown = "<unknown>";
		return a_file < _fileNames.size() ? _fileNames[a_file] : unknown;
	}

	std::optional<std::uint32_t> LoadOrder::PluginIndex(std::string_view a_name) const
	{
		for (std::uint32_t i = 0; i < _plugins.size(); ++i) {
			if (core::IEquals(_plugins[i].plugin->Name(), a_name)) {
				return i;
			}
		}
		return std::nullopt;
	}

	bool LoadOrder::AddWithMasters(std::string_view a_name, std::vector<std::string>& a_errors)
	{
		if (PluginIndex(a_name)) {
			return true;
		}
		const auto path = _files.Find(a_name);
		if (!path) {
			a_errors.push_back("plugin not found: " + std::string(a_name));
			return false;
		}
		std::string err;
		auto        plugin = Plugin::Open(*path, std::string(a_name), err);
		if (!plugin) {
			a_errors.push_back(err);
			return false;
		}
		for (const auto& m : plugin->Masters()) {
			AddWithMasters(m, a_errors);
		}
		Loaded l;
		l.self = FileId(a_name);
		for (const auto& m : plugin->Masters()) {
			l.masters.push_back(FileId(m));
		}
		l.plugin = std::move(plugin);
		_plugins.push_back(std::move(l));
		_indexBuilt = false;
		return true;
	}

	FormKey LoadOrder::Resolve(std::uint32_t a_plugin, std::uint32_t a_rawId) const
	{
		const auto& l = _plugins[a_plugin];
		const auto  idx = a_rawId >> 24;
		if (idx < l.masters.size()) {
			return { l.masters[idx], a_rawId & 0x00FFFFFF };
		}
		return { l.self, l.plugin->Light() ? (a_rawId & 0xFFF) : (a_rawId & 0x00FFFFFF) };
	}

	core::FormRef LoadOrder::ToFormRef(const FormKey& a_key) const
	{
		return core::FormRef{ FileName(a_key.file), a_key.id };
	}

	std::string LoadOrder::Format(const FormKey& a_key) const { return ToFormRef(a_key).ToString(); }

	std::optional<FormKey> LoadOrder::ParseFormRef(std::string_view a_text)
	{
		const auto ref = core::FormRef::Parse(a_text);
		if (!ref) {
			return std::nullopt;
		}
		return FormKey{ FileId(ref->plugin), ref->localId };
	}

	void LoadOrder::BuildIndex()
	{
		_index.clear();
		for (std::uint32_t i = 0; i < _plugins.size(); ++i) {
			const auto& pl = *_plugins[i].plugin;
			for (const auto type : kIndexedTypes) {
				pl.ForEachTopRecord(type, [&](const RecordHeader& r) {
					_index[Resolve(i, r.rawId)] = RecordRef{ i, r.type, r.flags, r.rawId, r.offset };
				});
			}
		}
		_indexBuilt = true;
	}

	const RecordRef* LoadOrder::Find(const FormKey& a_key, Sig a_type)
	{
		if (!_indexBuilt) {
			BuildIndex();
		}
		const auto it = _index.find(a_key);
		if (it == _index.end() || (a_type != 0 && it->second.type != a_type)) {
			return nullptr;
		}
		return &it->second;
	}

	void LoadOrder::ForEachRecord(Sig a_type, const std::function<void(const FormKey&, const RecordRef&)>& a_fn)
	{
		if (!_indexBuilt) {
			BuildIndex();
		}
		for (const auto& [key, rec] : _index) {
			if (a_type == 0 || rec.type == a_type) {
				a_fn(key, rec);
			}
		}
	}

	const std::string& LoadOrder::KeywordId(const FormKey& a_keyword)
	{
		if (const auto it = _keywordIds.find(a_keyword); it != _keywordIds.end()) {
			return it->second;
		}
		std::string edid;
		if (const auto* rec = Find(a_keyword, MakeSig("KYWD"))) {
			edid = EditorId(Read(*rec));
		}
		return _keywordIds.emplace(a_keyword, std::move(edid)).first->second;
	}

	std::vector<std::string> LoadOrder::KeywordIds(std::uint32_t a_plugin, std::span<const std::byte> a_kwda)
	{
		std::vector<std::string> out;
		for (std::size_t i = 0; i + 4 <= a_kwda.size(); i += 4) {
			const auto& edid = KeywordId(Resolve(a_plugin, esp::Read<std::uint32_t>(a_kwda, i)));
			if (!edid.empty()) {
				out.push_back(edid);
			}
		}
		return out;
	}

	Subrecords LoadOrder::Read(const RecordRef& a_rec) const { return _plugins[a_rec.plugin].plugin->ReadSubrecords(a_rec.offset); }

	std::string LoadOrder::EditorId(const Subrecords& a_subs) const
	{
		const auto d = a_subs.Get(MakeSig("EDID"));
		return d ? ZString(*d) : std::string{};
	}

	std::string LoadOrder::FullName(const RecordRef& a_rec, const Subrecords& a_subs)
	{
		const auto d = a_subs.Get(MakeSig("FULL"));
		return d ? LString(a_rec.plugin, *d) : std::string{};
	}

	Bsa* LoadOrder::OpenBsa(const std::filesystem::path& a_path)
	{
		auto [it, inserted] = _bsas.try_emplace(a_path.native());
		if (inserted) {
			it->second = Bsa::Open(a_path);
		}
		return it->second.get();
	}

	void LoadOrder::LoadStrings(Loaded& a_loaded)
	{
		a_loaded.stringsLoaded = true;
		const auto stem = PathToUtf8(std::filesystem::path(Widen(a_loaded.plugin->Name())).stem());
		const auto file = stem + "_" + _language + ".strings";
		if (const auto loose = _files.Find("Strings/" + file)) {
			std::ifstream          in(*loose, std::ios::binary | std::ios::ate);
			std::vector<std::byte> raw(static_cast<std::size_t>(std::max<std::streamoff>(in.tellg(), 0)));
			in.seekg(0);
			in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
			a_loaded.strings = ParseStringsFile(raw, false);
			return;
		}
		const std::string candidates[] = { stem + ".bsa", stem + " - Main.bsa", stem + " - Interface.bsa", stem + " - Strings.bsa",
			"Skyrim - Interface.bsa", "Skyrim - Patch.bsa" };
		for (const auto& c : candidates) {
			const auto path = _files.Find(c);
			if (!path) {
				continue;
			}
			if (auto* bsa = OpenBsa(*path)) {
				if (const auto raw = bsa->Read("strings\\" + file)) {
					a_loaded.strings = ParseStringsFile(*raw, false);
					return;
				}
			}
		}
	}

	std::string LoadOrder::LString(std::uint32_t a_plugin, std::span<const std::byte> a_data)
	{
		auto& l = _plugins[a_plugin];
		if (!l.plugin->Localized()) {
			return ZString(a_data);
		}
		if (a_data.size() < 4) {
			return {};
		}
		const auto sid = esp::Read<std::uint32_t>(a_data.data());
		if (sid == 0) {
			return {};
		}
		if (!l.stringsLoaded) {
			LoadStrings(l);
		}
		if (l.strings) {
			if (const auto it = l.strings->find(sid); it != l.strings->end()) {
				return it->second;
			}
		}
		return "<lstring " + std::to_string(sid) + ">";
	}

	const LoadOrder::CellDirectory& LoadOrder::Directory(std::uint32_t a_plugin)
	{
		auto& l = _plugins[a_plugin];
		if (l.cells) {
			return *l.cells;
		}
		l.cells = std::make_unique<CellDirectory>();
		auto&       dir = *l.cells;
		const auto& pl = *l.plugin;
		const auto  data = pl.Data();

		struct Context
		{
			std::optional<FormKey> world;
			bool                   worldChildren{ false };  // directly inside a worldspace's children group
		};

		// Manual recursion: the context (current worldspace) changes with the group nesting.
		std::function<void(std::size_t, std::size_t, Context)> scan = [&](std::size_t a_start, std::size_t a_end, Context a_ctx) {
			std::size_t pos = a_start;
			while (pos + 24 <= a_end) {
				const auto type = esp::Read<Sig>(data, pos);
				const auto size = esp::Read<std::uint32_t>(data, pos + 4);
				if (type == kGRUP) {
					if (size < 24 || pos + size > a_end) {
						return;
					}
					const auto label = esp::Read<std::uint32_t>(data, pos + 8);
					const auto gtype = esp::Read<std::int32_t>(data, pos + 12);
					if (gtype == 1) {
						scan(pos + 24, pos + size, Context{ Resolve(a_plugin, label), true });
					} else if (gtype >= 2 && gtype <= 5) {
						scan(pos + 24, pos + size, Context{ a_ctx.world, false });
					} else if (gtype == 6) {
						auto&       list = dir.children[Resolve(a_plugin, label)];
						std::size_t sub = pos + 24;
						while (sub + 24 <= pos + size) {
							const auto ssize = esp::Read<std::uint32_t>(data, sub + 4);
							if (esp::Read<Sig>(data, sub) != kGRUP || ssize < 24 || sub + ssize > pos + size) {
								break;
							}
							list.push_back(ChildGroup{ a_plugin, sub, ssize, esp::Read<std::int32_t>(data, sub + 12) });
							sub += ssize;
						}
					}
					pos += size;
				} else {
					if (pos + 24 + size > a_end) {
						return;
					}
					if (type == kCELL) {
						const auto flags = esp::Read<std::uint32_t>(data, pos + 8);
						const auto raw = esp::Read<std::uint32_t>(data, pos + 12);
						const RecordRef rec{ a_plugin, type, flags, raw, pos };
						dir.cells.push_back(CellEntry{ rec, Resolve(a_plugin, raw), a_ctx.world, a_ctx.world.has_value() && a_ctx.worldChildren });
					}
					pos += 24 + static_cast<std::size_t>(size);
				}
			}
		};
		for (const auto top : { kCELL, kWRLD }) {
			if (const auto g = pl.TopGroup(top)) {
				scan(g->offset + 24, g->offset + g->size, Context{});
			}
		}
		return dir;
	}

	const std::vector<CellEntry>& LoadOrder::CellsOf(std::uint32_t a_plugin) { return Directory(a_plugin).cells; }

	const CellEntry* LoadOrder::FindCell(const FormKey& a_cell)
	{
		const CellEntry* win = nullptr;
		for (std::uint32_t i = 0; i < _plugins.size(); ++i) {
			for (const auto& c : Directory(i).cells) {
				if (c.key == a_cell) {
					win = &c;
				}
			}
		}
		return win;
	}

	std::optional<FormKey> LoadOrder::FindCellByEditorId(std::string_view a_editorId)
	{
		for (std::uint32_t i = 0; i < _plugins.size(); ++i) {
			for (const auto& c : Directory(i).cells) {
				if (core::IEquals(EditorId(Read(c.rec)), a_editorId)) {
					return c.key;
				}
			}
		}
		return std::nullopt;
	}

	std::vector<RecordRef> LoadOrder::CellRefRecords(const FormKey& a_cell)
	{
		FormKeyMap<RecordRef> refs;
		std::vector<FormKey>  order;
		auto add = [&](std::uint32_t a_plugin, const RecordHeader& r) {
			if (r.type != kREFR && r.type != kACHR) {
				return;
			}
			const auto key = Resolve(a_plugin, r.rawId);
			auto [it, inserted] = refs.insert_or_assign(key, RecordRef{ a_plugin, r.type, r.flags, r.rawId, r.offset });
			if (inserted) {
				order.push_back(key);
			}
		};
		auto noDescend = [](const GroupHeader&) { return false; };

		for (std::uint32_t i = 0; i < _plugins.size(); ++i) {
			const auto& dir = Directory(i);
			const auto  it = dir.children.find(a_cell);
			if (it == dir.children.end()) {
				continue;
			}
			for (const auto& g : it->second) {
				auto onRecord = [&](const RecordHeader& r) { add(i, r); };
				_plugins[i].plugin->Walk(g.offset + 24, g.offset + g.size, noDescend, onRecord);
			}
		}

		// Exterior: persistent references of the worldspace inside this cell's grid square.
		const auto* cell = FindCell(a_cell);
		if (cell && cell->world && !cell->worldPersistent) {
			const auto subs = Read(cell->rec);
			const auto xclc = subs.Get(MakeSig("XCLC"));
			if (xclc && xclc->size() >= 8) {
				const auto gx = esp::Read<std::int32_t>(*xclc, 0);
				const auto gy = esp::Read<std::int32_t>(*xclc, 4);
				for (std::uint32_t i = 0; i < _plugins.size(); ++i) {
					const auto& dir = Directory(i);
					for (const auto& c : dir.cells) {
						if (!c.worldPersistent || c.world != cell->world) {
							continue;
						}
						const auto it = dir.children.find(c.key);
						if (it == dir.children.end()) {
							continue;
						}
						for (const auto& g : it->second) {
							if (g.type != 8) {
								continue;
							}
							auto onRecord = [&](const RecordHeader& r) {
								if (r.type != kREFR && r.type != kACHR) {
									return;
								}
								const auto rs = _plugins[i].plugin->ReadSubrecords(r.offset);
								const auto pos = rs.Get(MakeSig("DATA"));
								if (!pos || pos->size() < 8) {
									return;
								}
								const auto x = esp::Read<float>(*pos, 0);
								const auto y = esp::Read<float>(*pos, 4);
								if (static_cast<std::int32_t>(std::floor(x / 4096.0f)) == gx && static_cast<std::int32_t>(std::floor(y / 4096.0f)) == gy) {
									add(i, r);
								}
							};
							_plugins[i].plugin->Walk(g.offset + 24, g.offset + g.size, noDescend, onRecord);
						}
					}
				}
			}
		}

		std::vector<RecordRef> out;
		out.reserve(order.size());
		for (const auto& k : order) {
			out.push_back(refs.at(k));
		}
		return out;
	}

	std::optional<RecordRef> LoadOrder::CellLandRecord(const FormKey& a_cell)
	{
		std::optional<RecordRef> found;
		auto                     noDescend = [](const GroupHeader&) { return false; };
		for (std::uint32_t i = 0; i < _plugins.size(); ++i) {
			const auto& dir = Directory(i);
			const auto  it = dir.children.find(a_cell);
			if (it == dir.children.end()) {
				continue;
			}
			for (const auto& g : it->second) {
				auto onRecord = [&](const RecordHeader& r) {
					if (r.type == MakeSig("LAND")) {
						found = RecordRef{ i, r.type, r.flags, r.rawId, r.offset };
					}
				};
				_plugins[i].plugin->Walk(g.offset + 24, g.offset + g.size, noDescend, onRecord);
			}
		}
		return found;
	}

	FormKeyMap<FormKey> LoadOrder::FindCellsOfRefs(const FormKeySet& a_refs)
	{
		FormKeyMap<FormKey> out;
		auto                noDescend = [](const GroupHeader&) { return false; };
		for (std::uint32_t i = 0; i < _plugins.size() && out.size() < a_refs.size(); ++i) {
			const auto& dir = Directory(i);
			for (const auto& [cell, groups] : dir.children) {
				for (const auto& g : groups) {
					auto onRecord = [&](const RecordHeader& r) {
						if (r.type == kREFR || r.type == kACHR) {
							const auto key = Resolve(i, r.rawId);
							if (a_refs.contains(key)) {
								out[key] = cell;
							}
						}
					};
					_plugins[i].plugin->Walk(g.offset + 24, g.offset + g.size, noDescend, onRecord);
				}
			}
		}
		return out;
	}
}
