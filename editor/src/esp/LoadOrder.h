#pragma once

// A set of plugins in load order (later overrides earlier), the C++ counterpart of esm_dump.py's
// LoadOrder: load-order independent form keys, a lazily built index of base records, localized strings
// (loose Strings files or the plugin's BSA), and a per-plugin cell directory (cells, their worldspace and
// the groups holding their placed references).

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "dova/core/FormRef.h"
#include "esp/Bsa.h"
#include "esp/Plugin.h"

namespace dova::editor
{
	class DataFiles;
}

namespace dova::editor::esp
{
	class ScriptLibrary;

	// A form identified by the file that defines it (index into LoadOrder's file table) and its local id.
	struct FormKey
	{
		static constexpr std::uint32_t kNoFile = 0xFFFFFFFF;

		std::uint32_t file{ kNoFile };
		std::uint32_t id{ 0 };

		[[nodiscard]] bool Valid() const noexcept { return file != kNoFile; }
		friend bool        operator==(const FormKey&, const FormKey&) = default;
	};

	struct FormKeyHash
	{
		[[nodiscard]] std::size_t operator()(const FormKey& a_key) const noexcept
		{
			return std::hash<std::uint64_t>{}((static_cast<std::uint64_t>(a_key.file) << 32) | a_key.id);
		}
	};

	using FormKeySet = std::unordered_set<FormKey, FormKeyHash>;
	template <class V>
	using FormKeyMap = std::unordered_map<FormKey, V, FormKeyHash>;

	// A record somewhere in the load order.
	struct RecordRef
	{
		std::uint32_t plugin{ 0 };  // index into LoadOrder's plugins
		Sig           type{ 0 };
		std::uint32_t flags{ 0 };
		std::uint32_t rawId{ 0 };
		std::size_t   offset{ 0 };
	};

	struct CellEntry
	{
		RecordRef              rec;
		FormKey                key;
		std::optional<FormKey> world;               // exterior cells: the worldspace
		bool                   worldPersistent{ false };  // the worldspace's persistent cell
	};

	class LoadOrder
	{
	public:
		LoadOrder(const DataFiles& a_files, std::string a_language);
		~LoadOrder();

		// Adds the plugin after its masters (recursively, each once). Missing masters are reported in
		// `a_errors` and skipped; returns false when `a_name` itself cannot be loaded.
		bool AddWithMasters(std::string_view a_name, std::vector<std::string>& a_errors);

		// The compiled scripts the game would run (display detection in home::LoadCell); Workspace sets them.
		// nullptr: none known, so script-dependent displays (plaques, cases) are not recognised.
		void                         SetScripts(std::shared_ptr<ScriptLibrary> a_scripts) { _scripts = std::move(a_scripts); }
		[[nodiscard]] ScriptLibrary* Scripts() const noexcept { return _scripts.get(); }

		[[nodiscard]] std::size_t                 PluginCount() const noexcept { return _plugins.size(); }
		[[nodiscard]] const Plugin&               PluginAt(std::size_t a_index) const { return *_plugins[a_index].plugin; }
		[[nodiscard]] std::optional<std::uint32_t> PluginIndex(std::string_view a_name) const;

		// File table (every plugin name seen, loaded or only named as a master).
		[[nodiscard]] std::uint32_t      FileId(std::string_view a_name);
		[[nodiscard]] const std::string& FileName(std::uint32_t a_file) const;

		[[nodiscard]] FormKey       Resolve(std::uint32_t a_plugin, std::uint32_t a_rawId) const;
		[[nodiscard]] FormKey       KeyOf(const RecordRef& a_rec) const { return Resolve(a_rec.plugin, a_rec.rawId); }
		[[nodiscard]] core::FormRef ToFormRef(const FormKey& a_key) const;
		[[nodiscard]] std::string   Format(const FormKey& a_key) const;  // "Plugin.esm|0x00ABCDEF"
		[[nodiscard]] std::optional<FormKey> ParseFormRef(std::string_view a_text);

		// Winning top-level record of one of the indexed types (base objects, keywords, locations,
		// encounter zones, factions, form lists, NPCs, items). `a_type` 0 = any indexed type.
		[[nodiscard]] const RecordRef* Find(const FormKey& a_key, Sig a_type = 0);

		// Every winning record of an indexed type (`a_type` 0: all), in no particular order.
		void ForEachRecord(Sig a_type, const std::function<void(const FormKey&, const RecordRef&)>& a_fn);

		// EditorIDs of the keywords in a KWDA subrecord of a record of `a_plugin` (cached per keyword).
		[[nodiscard]] std::vector<std::string> KeywordIds(std::uint32_t a_plugin, std::span<const std::byte> a_kwda);
		[[nodiscard]] const std::string&       KeywordId(const FormKey& a_keyword);

		[[nodiscard]] Subrecords  Read(const RecordRef& a_rec) const;
		[[nodiscard]] std::string EditorId(const Subrecords& a_subs) const;
		[[nodiscard]] std::string FullName(const RecordRef& a_rec, const Subrecords& a_subs);
		[[nodiscard]] std::string LString(std::uint32_t a_plugin, std::span<const std::byte> a_data);

		// Cells defined or overridden by a plugin, in file order.
		[[nodiscard]] const std::vector<CellEntry>& CellsOf(std::uint32_t a_plugin);
		// The winning record of a cell.
		[[nodiscard]] const CellEntry* FindCell(const FormKey& a_cell);
		// The cell's name (FULL) from the first plugin whose record of it has one: a later override may drop it.
		[[nodiscard]] std::string FirstCellName(const FormKey& a_cell);
		[[nodiscard]] std::optional<FormKey> FindCellByEditorId(std::string_view a_editorId);

		// Winning REFR/ACHR records placed in the cell. Exterior cells also get the worldspace's persistent
		// references whose position lies inside the cell's grid square (where the game stores them).
		[[nodiscard]] std::vector<RecordRef> CellRefRecords(const FormKey& a_cell);
		// The REFR/ACHR records one plugin has in a cell (its own records, winning or not; an exterior's include that
		// plugin's persistent references of the worldspace inside the cell's grid square). Cheap: one plugin is read.
		[[nodiscard]] std::vector<RecordRef> PluginCellRefRecords(std::uint32_t a_plugin, const FormKey& a_cell);

		// The winning LAND record of an exterior cell (terrain heights, normals, colors, textures).
		[[nodiscard]] std::optional<RecordRef> CellLandRecord(const FormKey& a_cell);

		// For each wanted reference, the cell it is placed in (worldspace persistent cell for persistent
		// exterior references). Walks every loaded plugin's cell groups once.
		[[nodiscard]] FormKeyMap<FormKey> FindCellsOfRefs(const FormKeySet& a_refs);

	private:
		struct ChildGroup
		{
			std::uint32_t plugin;
			std::size_t   offset;
			std::uint32_t size;
			std::int32_t  type;
		};

		struct CellDirectory
		{
			std::vector<CellEntry>              cells;
			FormKeyMap<std::vector<ChildGroup>> children;
		};

		struct Loaded
		{
			std::unique_ptr<Plugin>                                        plugin;
			std::uint32_t                                                  self{ FormKey::kNoFile };
			std::vector<std::uint32_t>                                     masters;  // master index -> file id
			std::optional<std::unordered_map<std::uint32_t, std::string>> strings;
			bool                                                           stringsLoaded{ false };
			std::unique_ptr<CellDirectory>                                 cells;
		};

		void        BuildIndex();
		void        LoadStrings(Loaded& a_loaded);
		const CellDirectory& Directory(std::uint32_t a_plugin);
		Bsa*        OpenBsa(const std::filesystem::path& a_path);

		const DataFiles&                                       _files;
		std::shared_ptr<ScriptLibrary>                         _scripts;
		std::string                                            _language;
		std::vector<Loaded>                                    _plugins;
		std::vector<std::string>                               _fileNames;
		std::unordered_map<std::string, std::uint32_t>         _fileIds;  // lower-case name -> id
		bool                                                   _indexBuilt{ false };
		FormKeyMap<RecordRef>                                  _index;
		FormKeyMap<std::string>                                _keywordIds;
		std::unordered_map<std::wstring, std::unique_ptr<Bsa>> _bsas;
	};
}
