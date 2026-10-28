#include "home/Drafts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <numeric>
#include <unordered_map>

#include "Util.h"
#include "dova/core/DisplayCategories.h"
#include "dova/core/Template.h"
#include "esp/Items.h"

namespace dova::editor::home
{
	using esp::FormKey;
	using esp::MakeSig;
	using esp::Sig;

	namespace
	{
		constexpr Sig kItemSigs[] = { MakeSig("WEAP"), MakeSig("ARMO"), MakeSig("AMMO"), MakeSig("ALCH"), MakeSig("INGR"), MakeSig("BOOK"), MakeSig("SCRL"),
			MakeSig("SLGM"), MakeSig("MISC"), MakeSig("KEYM"), MakeSig("LIGH") };
		constexpr Sig kStationSigs[] = { MakeSig("FURN"), MakeSig("ACTI"), MakeSig("STAT"), MakeSig("MSTT") };

		bool IsItemSig(Sig a_sig) { return std::ranges::find(kItemSigs, a_sig) != std::end(kItemSigs); }

		// Words in a cell name that make it the lesser part of a home (the main cell is the other one).
		bool SubCellName(std::string_view a_name, std::string_view a_editorId)
		{
			static constexpr const char* words[] = { "cellar", "basement", "cave", "crypt", "attic", "lower", "upper", "vault", "sewer", "tunnel", "mine",
				"grotto", "dungeon", "undercroft", "catacomb", "garden", "storage" };
			const auto tokens = core::DraftTokens(std::string(a_name) + " " + std::string(a_editorId));
			return std::ranges::any_of(words, [&](const char* w) {
				const std::string word = w;
				return core::DraftWordsMatch(tokens, std::span(&word, 1));
			});
		}

		// The middle of a reference's bounds in the world and their extent (Z rotation only; X/Y swap when turned).
		void Box(const PlacedRef& a_ref, std::array<float, 3>& a_center, std::array<float, 3>& a_size)
		{
			a_center = a_ref.position;
			a_size = { 0, 0, 0 };
			if (!a_ref.hasBounds) {
				return;
			}
			const float s = a_ref.scale;
			const float lx = (a_ref.bounds[0] + a_ref.bounds[3]) * 0.5f * s;
			const float ly = (a_ref.bounds[1] + a_ref.bounds[4]) * 0.5f * s;
			const float lz = (a_ref.bounds[2] + a_ref.bounds[5]) * 0.5f * s;
			const float c = std::cos(a_ref.rotation[2]);
			const float n = std::sin(a_ref.rotation[2]);
			a_center = { a_ref.position[0] + lx * c + ly * n, a_ref.position[1] - lx * n + ly * c, a_ref.position[2] + lz };
			const float w = static_cast<float>(a_ref.bounds[3] - a_ref.bounds[0]) * s;
			const float d = static_cast<float>(a_ref.bounds[4] - a_ref.bounds[1]) * s;
			const float h = static_cast<float>(a_ref.bounds[5] - a_ref.bounds[2]) * s;
			a_size = std::abs(n) > std::abs(c) ? std::array{ d, w, h } : std::array{ w, d, h };
		}

		// The game's name; a display without one is named by its kind ("Mannequin", not PlayerHouseMannequin).
		std::string GameName(const PlacedRef& a_ref) { return !a_ref.baseName.empty() ? a_ref.baseName : a_ref.display ? DisplayWord(a_ref) : a_ref.baseEditorId; }

		bool Digit(std::string_view a_text, std::size_t a_at) { return a_at < a_text.size() && a_text[a_at] >= '0' && a_text[a_at] <= '9'; }

		// "BYOHHouse" and the house's number: a Hearthfire house's reference.
		bool HearthfireRef(std::string_view a_editorId) { return a_editorId.starts_with("BYOHHouse") && Digit(a_editorId, 9); }

		// A piece of a Hearthfire house ("BYOHHouse1InteriorRoom06Part007Chest02"): initially disabled with no enable
		// parent, the house's building script enables it when the player builds its room.
		bool BuiltByHouseScript(std::string_view a_editorId)
		{
			if (!HearthfireRef(a_editorId)) {
				return false;
			}
			for (auto at = a_editorId.find("Part"); at != std::string_view::npos; at = a_editorId.find("Part", at + 1)) {
				if (Digit(a_editorId, at + 4)) {
					return true;
				}
			}
			return false;
		}

		// A container no draft lists: initially disabled with no enable parent (only a quest's script brings it; a
		// Hearthfire piece stays, its house builds it), or a scripted weapon mount (Wynter_WeaponMountPoint: its script
		// shows one weapon, it is no storage).
		bool LeftOutOfDrafts(const PlacedRef& a_ref)
		{
			if (a_ref.initiallyDisabled && !a_ref.enableParent && !BuiltByHouseScript(a_ref.editorId)) {
				return true;
			}
			if (a_ref.scripts.empty()) {
				return false;
			}
			auto mount = [](std::string_view a_text) { return IContains(a_text, "WeaponRack") || IContains(a_text, "WeaponMount"); };
			return mount(a_ref.baseEditorId) || std::ranges::any_of(a_ref.scripts, [&](const std::string& s) { return mount(s); });
		}

		std::string CellLabel(esp::LoadOrder& a_lo, const FormKey& a_cell)
		{
			const auto* e = a_lo.FindCell(a_cell);
			if (!e) {
				return a_lo.Format(a_cell);
			}
			if (e->world) {
				if (const auto* w = a_lo.Find(*e->world, MakeSig("WRLD"))) {
					const auto subs = a_lo.Read(*w);
					auto       name = a_lo.FullName(*w, subs);
					return name.empty() ? a_lo.EditorId(subs) : name;
				}
			}
			const auto subs = a_lo.Read(e->rec);
			auto       name = a_lo.FullName(e->rec, subs);
			if (name.empty()) {
				name = a_lo.FirstCellName(a_cell);  // an override that dropped the name
			}
			return name.empty() ? a_lo.EditorId(subs) : name;
		}

		struct UnionFind
		{
			std::vector<std::size_t> parent;
			explicit UnionFind(std::size_t a_n) :
				parent(a_n)
			{
				std::iota(parent.begin(), parent.end(), std::size_t{ 0 });
			}
			std::size_t Find(std::size_t a_x)
			{
				while (parent[a_x] != a_x) {
					a_x = parent[a_x] = parent[parent[a_x]];
				}
				return a_x;
			}
			void Unite(std::size_t a_a, std::size_t a_b) { parent[Find(a_a)] = Find(a_b); }
		};
	}

	TemplateCells TemplateCellsOf(esp::LoadOrder& a_lo, const TemplateIndex& a_templates)
	{
		TemplateCells out;
		for (const auto& t : a_templates.templates) {
			for (const auto& c : t.layout.cells) {
				if (const auto key = a_lo.ParseFormRef(c.cell.ToString())) {
					out.try_emplace(*key, false);
				}
			}
			if (t.layout.cell) {
				if (const auto key = a_lo.ParseFormRef(t.layout.cell->ToString())) {
					out[*key] = true;
				}
			}
		}
		return out;
	}

	std::optional<std::size_t> FindHome(std::span<const HomeEntry> a_homes, std::string_view a_wanted)
	{
		for (const auto exact : { true, false }) {
			for (std::size_t i = 0; i < a_homes.size(); ++i) {
				const auto& h = a_homes[i];
				if (exact ? core::IEquals(h.editorId, a_wanted) : IContains(h.name, a_wanted) || IContains(h.editorId, a_wanted)) {
					return i;
				}
			}
			if (exact) {
				for (std::size_t i = 0; i < a_homes.size(); ++i) {
					if (core::IEquals(a_homes[i].name, a_wanted)) {
						return i;
					}
				}
			}
		}
		return std::nullopt;
	}

	std::vector<HomeEntry> FindAllHomes(esp::LoadOrder& a_lo, const ScanProgress& a_progress, const std::atomic<bool>* a_cancel, const TemplateCells* a_templateCells)
	{
		auto named = [&](const FormKey& a_cell) { return a_templateCells && a_templateCells->contains(a_cell); };
		auto namedMain = [&](const FormKey& a_cell) {
			if (!a_templateCells) {
				return false;
			}
			const auto it = a_templateCells->find(a_cell);
			return it != a_templateCells->end() && it->second;
		};
		struct Seen
		{
			HomeCandidate        candidate;
			std::string          lastPlugin;
			std::vector<FormKey> doors;
			std::vector<std::optional<std::array<float, 3>>> arrivals;  // per door: where the player arrives (XTEL)
			bool                 ownedByOther{ false };
			int                  displays{ 0 };
		};
		std::size_t total = 0;
		for (std::uint32_t i = 0; i < a_lo.PluginCount(); ++i) {
			for (const auto& c : a_lo.CellsOf(i)) {
				total += c.world ? 0 : 1;
			}
		}
		esp::FormKeyMap<Seen> seen;
		esp::FormKeySet       visited;
		std::size_t           done = 0;
		for (std::uint32_t i = 0; i < a_lo.PluginCount(); ++i) {
			const auto& name = a_lo.PluginAt(i).Name();
			if (a_progress) {
				a_progress(done, total, "Reading the cells of " + name);
			}
			for (const auto& c : a_lo.CellsOf(i)) {
				if (c.world || c.worldPersistent) {
					continue;
				}
				++done;
				if (a_cancel && a_cancel->load()) {
					return {};
				}
				if (!visited.insert(c.key).second) {
					continue;
				}
				const auto cell = LoadCell(a_lo, c.key, LoadCellOptions{ .resolveDoors = false });
				if (!cell || !cell->interior) {
					continue;
				}
				auto h = ScoreHome(*cell, false);
				if (h.containers == 0 && !h.Likely() && !named(c.key)) {
					continue;
				}
				if (named(c.key) && !h.Likely()) {
					h.reasons.push_back("a template names it");
				}
				Seen s;
				s.lastPlugin = cell->lastOverride;
				s.ownedByOther = cell->owner.has_value() && !cell->ownedByPlayer;
				s.displays = h.racks + h.mannequins + h.plaques;
				for (const auto& r : cell->refs) {
					if (r.teleportDoor) {
						s.doors.push_back(*r.teleportDoor);
						s.arrivals.push_back(r.teleportPosition);
					}
				}
				s.candidate = std::move(h);
				seen.emplace(c.key, std::move(s));
			}
		}

		std::vector<FormKey> likely;
		for (const auto& [key, s] : seen) {
			if (s.candidate.Likely() || named(key)) {
				likely.push_back(key);
			}
		}
		std::ranges::sort(likely, [](const FormKey& a, const FormKey& b) { return a.file != b.file ? a.file < b.file : a.id < b.id; });
		if (a_progress) {
			a_progress(total, total, "Following the doors of " + std::to_string(likely.size()) + " homes");
		}
		esp::FormKeySet wanted;
		for (const auto& k : likely) {
			for (const auto& d : seen.at(k).doors) {
				wanted.insert(d);
			}
		}
		const auto doorCells = wanted.empty() ? esp::FormKeyMap<FormKey>{} : a_lo.FindCellsOfRefs(wanted);

		std::unordered_map<FormKey, std::size_t, esp::FormKeyHash> indexOf;
		for (std::size_t i = 0; i < likely.size(); ++i) {
			indexOf[likely[i]] = i;
		}
		// The grounds (Hearthfire's BYOHHouse*Exterior): the exterior cells around a home's load door (its arrival cell
		// and the 8 next to it) that the home's own plugin edits and places containers in that a template can use or
		// that appear later, when another plugin defines the cell (a city or a worldspace the home's plugin adds is never
		// joined). A persistent exterior door lives in the worldspace's persistent cell: the arrival position (XTEL)
		// gives the grid square, as LoadCell does.
		struct GridCell
		{
			FormKey      key;
			std::int32_t x{ 0 }, y{ 0 };
		};
		std::map<std::uint32_t, esp::FormKeyMap<std::vector<GridCell>>> edited;  // plugin -> worldspace -> the exterior cells it edits
		auto editedBy = [&](std::uint32_t a_plugin, const FormKey& a_world) -> const std::vector<GridCell>* {
			auto [it, fresh] = edited.try_emplace(a_plugin);
			if (fresh) {
				for (const auto& c : a_lo.CellsOf(a_plugin)) {
					if (!c.world || c.worldPersistent) {
						continue;
					}
					const auto subs = a_lo.Read(c.rec);
					if (const auto xclc = subs.Get(MakeSig("XCLC")); xclc && xclc->size() >= 8) {
						it->second[*c.world].push_back({ c.key, esp::Read<std::int32_t>(*xclc, 0), esp::Read<std::int32_t>(*xclc, 4) });
					}
				}
			}
			const auto found = it->second.find(a_world);
			return found != it->second.end() ? &found->second : nullptr;
		};
		// The containers a plugin adds to a cell that a template can use or that appear later (UsableContainer and not
		// LeftOutOfDrafts, from the plugin's own records: reading the whole cell for every door would take seconds).
		esp::FormKeyMap<std::unordered_map<std::uint32_t, int>> outside;  // cell -> plugin -> count
		auto ownContainers = [&](std::uint32_t a_plugin, const FormKey& a_cell) {
			auto [it, fresh] = outside[a_cell].try_emplace(a_plugin, 0);
			if (!fresh) {
				return it->second;
			}
			const auto self = a_lo.FileId(a_lo.PluginAt(a_plugin).Name());
			for (const auto& rec : a_lo.PluginCellRefRecords(a_plugin, a_cell)) {
				const auto key = a_lo.KeyOf(rec);
				if ((rec.flags & esp::kRecDeleted) || key.file != self) {
					continue;
				}
				const auto subs = a_lo.Read(rec);
				const auto name = subs.Get(MakeSig("NAME"));
				const auto* base = name && name->size() >= 4 ? a_lo.Find(a_lo.Resolve(a_plugin, esp::Read<std::uint32_t>(*name, 0)), MakeSig("CONT")) : nullptr;
				if (!base) {
					continue;
				}
				const auto data = a_lo.Read(*base).Get(MakeSig("DATA"));
				const bool respawns = data && !data->empty() && (static_cast<std::uint8_t>((*data)[0]) & 0x02) != 0;
				const auto xesp = subs.Get(MakeSig("XESP"));
				const bool removed = xesp && xesp->size() >= 8 && (esp::Read<std::uint32_t>(*xesp, 4) & 1) != 0;
				const bool leftOut = (rec.flags & esp::kRecInitiallyDisabled) && !(xesp && xesp->size() >= 4) && !BuiltByHouseScript(a_lo.EditorId(subs));
				bool       other = false;
				if (const auto xown = subs.Get(MakeSig("XOWN")); xown && xown->size() >= 4) {
					const auto owner = a_lo.Resolve(a_plugin, esp::Read<std::uint32_t>(*xown, 0));
					other = !(core::IEquals(a_lo.FileName(owner.file), "Skyrim.esm") && (owner.id == 0x7 || owner.id == 0xDB1));
				}
				it->second += respawns || removed || leftOut || other ? 0 : 1;
			}
			return it->second;
		};
		std::vector<std::vector<std::pair<FormKey, int>>> grounds(likely.size());  // exterior cells joined as the grounds, with their containers
		auto joinGrounds = [&](std::size_t a_home, const esp::CellEntry& a_dest, const std::optional<std::array<float, 3>>& a_arrival) {
			const auto file = likely[a_home].file;
			const auto plugin = a_lo.PluginIndex(a_lo.FileName(file));
			if (!plugin) {
				return;
			}
			std::int32_t gx = 0, gy = 0;
			if (a_dest.worldPersistent) {
				if (!a_arrival) {
					return;
				}
				gx = static_cast<std::int32_t>(std::floor((*a_arrival)[0] / 4096.0f));
				gy = static_cast<std::int32_t>(std::floor((*a_arrival)[1] / 4096.0f));
			} else if (const auto xclc = a_lo.Read(a_dest.rec).Get(MakeSig("XCLC")); xclc && xclc->size() >= 8) {
				gx = esp::Read<std::int32_t>(*xclc, 0);
				gy = esp::Read<std::int32_t>(*xclc, 4);
			} else {
				return;
			}
			const auto* cells = editedBy(*plugin, *a_dest.world);
			if (!cells) {
				return;
			}
			std::vector<GridCell> near;  // nearest first
			for (const auto& c : *cells) {
				if (std::abs(c.x - gx) <= 1 && std::abs(c.y - gy) <= 1 && c.key.file != file && std::ranges::none_of(near, [&](const GridCell& n) { return n.key == c.key; })) {
					near.push_back(c);
				}
			}
			std::ranges::stable_sort(near, [&](const GridCell& a, const GridCell& b) { return std::abs(a.x - gx) + std::abs(a.y - gy) < std::abs(b.x - gx) + std::abs(b.y - gy); });
			auto& list = grounds[a_home];
			for (const auto& c : near) {
				if (std::ranges::none_of(list, [&](const auto& g) { return g.first == c.key; })) {
					if (const int n = ownContainers(*plugin, c.key); n > 0) {
						list.emplace_back(c.key, n);
					}
				}
			}
			// The cell with the most of them is "Grounds" (a Hearthfire home's chest outside), the nearest first on a tie.
			std::ranges::stable_sort(list, [](const auto& a, const auto& b) { return a.second > b.second; });
		};

		UnionFind                                  uf(likely.size());
		std::vector<std::vector<FormKey>>          storage(likely.size());  // non-likely cells reached from a home
		for (std::size_t i = 0; i < likely.size(); ++i) {
			const auto& here = seen.at(likely[i]);
			for (std::size_t k = 0; k < here.doors.size(); ++k) {
				const auto it = doorCells.find(here.doors[k]);
				if (it == doorCells.end() || it->second == likely[i]) {
					continue;
				}
				const auto* dest = a_lo.FindCell(it->second);
				if (!dest) {
					continue;
				}
				if (dest->world) {
					joinGrounds(i, *dest, here.arrivals[k]);
					continue;
				}
				if (const auto j = indexOf.find(it->second); j != indexOf.end()) {
					uf.Unite(i, j->second);
				} else if (const auto s = seen.find(it->second); s != seen.end() && s->second.candidate.containers > s->second.candidate.respawning &&
				                                                s->second.candidate.respawning == 0 && !s->second.ownedByOther) {
					if (std::ranges::find(storage[i], it->second) == storage[i].end()) {
						storage[i].push_back(it->second);
					}
				}
			}
		}

		std::map<std::size_t, std::vector<std::size_t>> groups;
		for (std::size_t i = 0; i < likely.size(); ++i) {
			groups[uf.Find(i)].push_back(i);
		}
		std::vector<HomeEntry> out;
		esp::FormKeySet        claimed;  // a storage cell joins one home only
		for (auto& [root, members] : groups) {
			std::ranges::stable_sort(members, [&](std::size_t a, std::size_t b) {
				const bool ta = namedMain(likely[a]), tb = namedMain(likely[b]);
				if (ta != tb) {
					return ta;  // the template's main cell, as its author chose it
				}
				const auto& ca = seen.at(likely[a]).candidate;
				const auto& cb = seen.at(likely[b]).candidate;
				const bool  sa = SubCellName(ca.name, ca.editorId), sb = SubCellName(cb.name, cb.editorId);
				if (sa != sb) {
					return !sa;
				}
				if (ca.score != cb.score) {
					return ca.score > cb.score;
				}
				return ca.containers > cb.containers;
			});
			const auto& mainKey = likely[members.front()];
			const auto& main = seen.at(mainKey);
			HomeEntry   e;
			e.cell = mainKey;
			e.cellRef = a_lo.Format(mainKey);
			e.name = main.candidate.name.empty() ? main.candidate.editorId : main.candidate.name;
			e.editorId = main.candidate.editorId;
			e.plugin = a_lo.FileName(mainKey.file);
			e.lastPlugin = main.lastPlugin;
			e.candidate = main.candidate;
			std::vector<FormKey> cells{ mainKey };
			for (std::size_t k = 1; k < members.size(); ++k) {
				const auto& key = likely[members[k]];
				const auto& s = seen.at(key);
				e.extra.push_back({ key, a_lo.Format(key), s.candidate.name, s.candidate.editorId, true });
				cells.push_back(key);
			}
			for (const auto m : members) {
				for (const auto& key : storage[m]) {
					if (std::ranges::find(cells, key) == cells.end() && claimed.insert(key).second) {
						const auto& s = seen.at(key);
						e.extra.push_back({ key, a_lo.Format(key), s.candidate.name, s.candidate.editorId, false });
						cells.push_back(key);
					}
				}
			}
			for (const auto m : members) {
				for (const auto& [key, count] : grounds[m]) {
					if (claimed.insert(key).second) {
						const auto* c = a_lo.FindCell(key);
						const auto  n = std::ranges::count_if(e.extra, [](const HomeEntry::Extra& x) { return x.exterior; });
						e.extra.push_back({ key, a_lo.Format(key), n == 0 ? std::string("Grounds") : std::format("Grounds {}", n + 1),
							c ? a_lo.EditorId(a_lo.Read(c->rec)) : std::string{}, false, true });
						e.containers += count;
					}
				}
			}
			for (const auto& key : cells) {
				const auto& s = seen.at(key);
				e.containers += s.candidate.containers;
				e.displays += s.displays;
				for (const auto& d : s.doors) {
					HomeEntry::Door door{ d, {}, false };
					if (const auto it = doorCells.find(d); it != doorCells.end()) {
						const auto* dest = a_lo.FindCell(it->second);
						door.exterior = dest && dest->world.has_value();
						door.leadsTo = CellLabel(a_lo, it->second);
					}
					e.doors.push_back(std::move(door));
				}
			}
			out.push_back(std::move(e));
		}
		std::ranges::sort(out, [&](const HomeEntry& a, const HomeEntry& b) {
			const auto pa = a_lo.PluginIndex(a.plugin).value_or(0), pb = a_lo.PluginIndex(b.plugin).value_or(0);
			if (pa != pb) {
				return pa < pb;
			}
			return core::ICompare(a.name, b.name) < 0;
		});
		return out;
	}

	std::vector<core::DraftObject> BuildDraftObjects(esp::LoadOrder& a_lo, std::span<const CellInfo> a_cells, const core::CategorySet* a_categories,
		std::vector<std::pair<std::size_t, std::size_t>>* a_where)
	{
		std::vector<core::DraftObject>                     out;
		esp::FormKeyMap<std::vector<std::string>>          itemCategories;
		esp::FormKeyMap<const PlacedRef*>                  byKey;
		for (const auto& cell : a_cells) {
			for (const auto& r : cell.refs) {
				byKey.emplace(r.key, &r);
			}
		}
		// What brings a reference that appears later: the Hearthfire room its EditorID or an enable parent's names, else
		// its enable parent.
		auto appearsWith = [&](const PlacedRef& a_ref) {
			const PlacedRef* at = &a_ref;
			for (int hop = 0; at && hop < 8; ++hop) {
				if (auto room = HearthfireRoomKey(at->editorId); !room.empty()) {
					return room;
				}
				if (!at->enableParent) {
					break;
				}
				const auto it = byKey.find(*at->enableParent);
				at = it != byKey.end() ? it->second : nullptr;
			}
			return a_ref.enableParent ? a_lo.Format(*a_ref.enableParent) : std::string{};
		};
		for (std::size_t ci = 0; ci < a_cells.size(); ++ci) {
			const auto& cell = a_cells[ci];
			const auto  cellRef = a_lo.Format(cell.key);
			for (std::size_t ri = 0; ri < cell.refs.size(); ++ri) {
				const auto&       r = cell.refs[ri];
				core::DraftObject o;
				o.id = a_lo.Format(r.key);
				o.cell = cellRef;
				o.editorId = r.baseEditorId;
				o.name = r.baseName;
				o.keywords = r.keywords;
				o.scripts = r.scripts;
				Box(r, o.center, o.size);
				// A container's key makes its wing (core's areas); a station's or an item's limits it to its wing's
				// containers, so only a Hearthfire room's (the room's other choices stand where it would): a station an
				// upgrade brings still counts for the drawers always there beside it.
				o.appearsLater = r.initiallyDisabled || (r.enableParent && !r.enableOpposite);
				if (o.appearsLater) {
					o.appearsWith = r.IsContainer() && !r.display ? appearsWith(r) : HearthfireRoomKey(appearsWith(r));
				}
				if (r.display) {
					o.kind = core::DraftObject::Kind::Display;
					o.keywords.push_back("display:" + std::string(core::DisplayKindName(*r.display)));
				} else if (r.IsContainer()) {
					if (LeftOutOfDrafts(r)) {
						continue;
					}
					o.kind = core::DraftObject::Kind::Container;
					o.respawns = r.respawns;
					o.removedByUpgrade = r.enableParent && r.enableOpposite;
					o.ownedByOther = r.ownedByOther;
				} else if (r.teleportDoor) {
					o.kind = core::DraftObject::Kind::Door;
					for (const auto& d : cell.doors) {
						if (d.door == r.key) {
							o.exteriorDoor = !d.destinationInterior && d.destinationCell.has_value();
							o.leadsTo = d.destinationLabel;
						}
					}
				} else if (IsItemSig(r.baseType)) {
					if (!a_categories || r.initiallyDisabled) {
						continue;
					}
					auto it = itemCategories.find(r.base);
					if (it == itemCategories.end()) {
						std::vector<std::string> cats;
						if (const auto* rec = a_lo.Find(r.base)) {
							if (const auto item = esp::ReadItem(a_lo, r.base, *rec); item && item->playable) {
								cats = a_categories->MatchAll(*item);
							}
						}
						it = itemCategories.emplace(r.base, std::move(cats)).first;
					}
					if (it->second.empty()) {
						continue;
					}
					o.kind = core::DraftObject::Kind::Item;
					o.categories = it->second;
				} else if (std::ranges::find(kStationSigs, r.baseType) != std::end(kStationSigs)) {
					if (r.baseEditorId.empty() && r.baseName.empty()) {
						continue;
					}
					o.kind = core::DraftObject::Kind::Other;
				} else {
					continue;
				}
				if (a_where) {
					a_where->emplace_back(ci, ri);
				}
				out.push_back(std::move(o));
			}
		}
		return out;
	}

	core::Layout LayoutFromPlan(esp::LoadOrder& a_lo, std::span<const CellInfo> a_cells, std::span<const core::DraftObject> a_objects, const core::DraftPlan& a_plan,
		const core::DraftRules* a_rules)
	{
		core::Layout t;
		if (a_cells.empty()) {
			return t;
		}
		const auto& main = a_cells.front();
		t.name = main.name.empty() ? main.editorId : main.name;
		t.version = "1.0.0";
		t.cell = a_lo.ToFormRef(main.key);
		int outside = 0;  // the grounds: "Grounds", then "Grounds 2"...
		for (std::size_t i = 1; i < a_cells.size(); ++i) {
			core::HomeCell hc;
			hc.cell = a_lo.ToFormRef(a_cells[i].key);
			hc.label = a_cells[i].interior ? a_cells[i].name : ++outside == 1 ? std::string("Grounds") : std::format("Grounds {}", outside);
			t.cells.push_back(std::move(hc));
		}
		core::ApplyDraftPlan(a_plan, a_objects, t);
		// An area master per joined cell (DESIGN §6.7 cells[].master): the draft's master rule over that cell alone,
		// unless it picks the home's master. A container it takes that has no categories leaves `containers`.
		for (std::size_t i = 1; i < a_cells.size(); ++i) {
			const auto                     cellId = a_lo.Format(a_cells[i].key);
			std::vector<core::DraftObject> own;
			for (const auto& o : a_objects) {
				if (o.cell == cellId) {
					own.push_back(o);
				}
			}
			core::DraftOptions options;
			options.mainCell = cellId;
			const auto area = core::SuggestDraft(own, a_rules ? *a_rules : core::DefaultDraftRules(), options);
			const auto ref = area.master ? core::FormRef::Parse(own[*area.master].id) : std::nullopt;
			if (!ref || t.IsMaster(*ref)) {
				continue;
			}
			if (const auto* b = t.FindContainer(*ref); b && (b->ignored || !b->categories.empty() || !b->forms.empty())) {
				continue;  // ignored in the home's plan, or given categories there: no area master rather than a guess
			}
			t.cells[i - 1].master = *ref;
			std::erase_if(t.containers, [&](const core::ContainerBinding& b) { return b.ref == *ref; });
		}
		std::ranges::stable_sort(t.containers, [](const core::ContainerBinding& a, const core::ContainerBinding& b) { return a.ref < b.ref; });
		for (const auto& cell : a_cells) {
			for (const auto& r : cell.refs) {
				if (!r.display) {
					continue;
				}
				core::DisplayBinding d;
				d.ref = a_lo.ToFormRef(r.key);
				d.kind = r.display;
				d.label = GameName(r);
				if (*r.display == core::DisplayKind::Item || *r.display == core::DisplayKind::Case) {
					for (const auto& item : r.displayItems) {
						d.items.push_back(a_lo.ToFormRef(item));
					}
				} else if (r.rackType == core::RackType::Shield) {
					d.categories = { "armor.shields" };
				} else if (r.rackType == core::RackType::Dagger) {
					d.categories = { "weapons.daggers" };
				}
				t.displays.push_back(std::move(d));
			}
		}
		t.note = "Draft from Dova Stash Editor: each container takes the item categories listed (see Categories/00_Default.json); ignored ones are never used. "
		         "Anything without a container of its own goes to the master.";
		t.requiredPlugins = core::ReferencedPlugins(t);
		return t;
	}

	std::vector<std::string> ValidateDraft(core::Layout& a_layout, const core::CategorySet* a_categories)
	{
		a_layout.requiredPlugins = core::ReferencedPlugins(a_layout);
		core::Diagnostics diags;
		core::ValidateTemplate(a_layout, "template", diags);
		if (a_categories) {
			core::ValidateLayoutCategories(a_layout, *a_categories, "template", diags);
			core::ValidateDisplayCategories(a_layout, *a_categories, {}, "template", diags);  // by each entry's kind
		}
		std::vector<std::string> out;
		for (const auto& d : diags.Items()) {
			out.push_back(d.ToString());
		}
		return out;
	}

	HomeDraft DraftHome(esp::LoadOrder& a_lo, const HomeEntry& a_home, const DraftContext& a_context)
	{
		HomeDraft d;
		std::vector<FormKey> keys{ a_home.cell };
		for (const auto& e : a_home.extra) {
			keys.push_back(e.cell);
		}
		for (const auto& k : keys) {
			if (auto cell = LoadCell(a_lo, k, LoadCellOptions{ .resolveDoors = false })) {
				// The doors were resolved once for every home (FindAllHomes).
				for (const auto& r : cell->refs) {
					if (!r.teleportDoor) {
						continue;
					}
					for (const auto& door : a_home.doors) {
						if (door.door == *r.teleportDoor) {
							DoorLink link;
							link.door = r.key;
							link.destination = *r.teleportDoor;
							link.destinationLabel = door.leadsTo;
							link.destinationInterior = !door.exterior;
							if (door.exterior) {
								link.destinationCell = FormKey{};  // known to lead outside
							}
							cell->doors.push_back(std::move(link));
						}
					}
				}
				d.cells.push_back(std::move(*cell));
			}
		}
		if (d.cells.empty()) {
			d.diagnostics.push_back("the home's cell cannot be read");
			return d;
		}
		d.objects = BuildDraftObjects(a_lo, d.cells, a_context.categories, &d.where);
		// A joined exterior (the grounds): only the containers the home's own plugin places there.
		for (std::size_t k = d.objects.size(); k-- > 0;) {
			const auto [ci, ri] = d.where[k];
			if (ci > 0 && !d.cells[ci].interior && d.objects[k].kind == core::DraftObject::Kind::Container && d.cells[ci].refs[ri].key.file != a_home.cell.file) {
				d.objects.erase(d.objects.begin() + static_cast<std::ptrdiff_t>(k));
				d.where.erase(d.where.begin() + static_cast<std::ptrdiff_t>(k));
			}
		}
		d.plan = core::SuggestDraft(d.objects, a_context.rules ? *a_context.rules : core::DefaultDraftRules(), MakeDraftOptions(a_lo.Format(a_home.cell), a_context));
		d.layout = LayoutFromPlan(a_lo, d.cells, d.objects, d.plan, a_context.rules);
		d.diagnostics = ValidateDraft(d.layout, a_context.categories);
		return d;
	}

	void SetSuggestion(HomeDraft& a_draft, std::size_t a_container, std::size_t a_suggestion, bool a_accepted)
	{
		if (a_container >= a_draft.plan.containers.size()) {
			return;
		}
		auto& c = a_draft.plan.containers[a_container];
		if (a_suggestion >= c.suggestions.size()) {
			return;
		}
		auto& s = c.suggestions[a_suggestion];
		s.accepted = a_accepted;
		const auto ref = core::FormRef::Parse(a_draft.objects[c.object].id);
		if (!ref) {
			return;
		}
		auto* b = a_draft.layout.FindContainer(*ref);
		if (!b) {
			return;  // the master, or removed by the author
		}
		const auto has = std::ranges::find(b->categories, s.pattern);
		if (a_accepted && has == b->categories.end()) {
			b->categories.push_back(s.pattern);
		} else if (!a_accepted && has != b->categories.end()) {
			b->categories.erase(has);
		}
	}

	std::optional<core::FormRef> CategoryDestination(const core::Layout& a_layout, std::string_view a_category)
	{
		const core::ContainerBinding* best = nullptr;
		int                           bestSpec = -1;
		for (const auto& c : a_layout.containers) {
			if (c.ignored) {
				continue;
			}
			for (const auto& p : c.categories) {
				if (core::CategoryPatternMatches(p, a_category)) {
					const int spec = core::CategoryPatternSpecificity(p);
					if (spec > bestSpec) {
						bestSpec = spec;
						best = &c;
					}
				}
			}
		}
		if (best) {
			return best->ref;
		}
		return a_layout.master;
	}

	TemplateComparison CompareToTemplate(const core::Layout& a_draft, const core::Layout& a_template, const core::CategorySet& a_categories)
	{
		TemplateComparison r;
		r.sameMaster = a_draft.master && a_template.master && *a_draft.master == *a_template.master;
		for (const auto& t : a_template.containers) {
			const auto* d = a_draft.FindContainer(t.ref);
			if (t.ignored) {
				++r.ignoredInTemplate;
				r.ignoredAgree += (d && d->ignored) || (!d && a_draft.FindDisplay(t.ref)) ? 1 : 0;  // a scripted bookshelf the draft lists as a display only
				continue;
			}
			if (t.categories.empty()) {
				continue;
			}
			++r.containers;
			std::vector<std::string> mine = d && !d->ignored ? d->categories : std::vector<std::string>{};
			if (!d && a_draft.master && *a_draft.master == t.ref) {
				mine = {};
			}
			auto sortedT = t.categories, sortedD = mine;
			std::ranges::sort(sortedT);
			std::ranges::sort(sortedD);
			std::string verdict;
			if (sortedT == sortedD) {
				++r.exact;
				verdict = "same";
			} else if (mine.empty()) {
				++r.missing;
				verdict = d && d->ignored ? "draft ignores it" : (a_draft.master && *a_draft.master == t.ref ? "draft made it the master" : "draft gives none");
			} else {
				// Overlap: a pattern of one covers a category the other names.
				bool shared = false;
				for (const auto& c : a_categories.Categories()) {
					const bool inT = std::ranges::any_of(t.categories, [&](const std::string& p) { return core::CategoryPatternMatches(p, c.id); });
					const bool inD = std::ranges::any_of(mine, [&](const std::string& p) { return core::CategoryPatternMatches(p, c.id); });
					shared |= inT && inD;
				}
				if (shared) {
					++r.overlap;
					verdict = "overlap";
				} else {
					++r.different;
					verdict = "different";
				}
			}
			std::string tl, dl;
			for (const auto& p : t.categories) {
				tl += (tl.empty() ? "" : ", ") + p;
			}
			for (const auto& p : mine) {
				dl += (dl.empty() ? "" : ", ") + p;
			}
			r.details.push_back(std::format("{:<9} {}  template [{}]  draft [{}]", verdict, t.label.empty() ? t.ref.ToString() : t.label, tl, dl.empty() ? "-" : dl));
		}
		for (const auto& c : a_categories.Categories()) {
			// Leaves only: a group ("weapons") is the sum of its children.
			if (std::ranges::any_of(a_categories.Categories(), [&](const core::CategorySet::Category& o) { return o.id.starts_with(c.id + "."); })) {
				continue;
			}
			++r.categories;
			const auto a = CategoryDestination(a_draft, c.id);
			const auto b = CategoryDestination(a_template, c.id);
			r.sameDestination += a == b ? 1 : 0;
		}
		return r;
	}

	core::Layout DraftTemplate(esp::LoadOrder& a_lo, const CellInfo& a_cell, core::Diagnostics& a_diags, const core::CategorySet* a_categories, const core::DraftRules* a_rules)
	{
		const std::span<const CellInfo> cells(&a_cell, 1);
		const auto                      objects = BuildDraftObjects(a_lo, cells, a_categories);
		const DraftContext              ctx{ a_categories, a_rules, nullptr };
		const auto plan = core::SuggestDraft(objects, a_rules ? *a_rules : core::DefaultDraftRules(), MakeDraftOptions(a_lo.Format(a_cell.key), ctx));
		auto       t = LayoutFromPlan(a_lo, cells, objects, plan, a_rules);
		core::ValidateTemplate(t, "draft", a_diags);
		return t;
	}

	ItemNameIndex BuildItemNameIndex(const esp::ItemCatalog& a_catalog, const core::CategorySet& a_categories)
	{
		ItemNameIndex   out;
		std::unordered_map<std::string, bool> ambiguous;
		for (const auto& item : a_catalog.items) {
			const auto cat = a_categories.Classify(item);
			if (!cat || core::CategoryPatternMatches("misc.clutter", *cat)) {
				continue;
			}
			const auto name = core::ToLower(item.name);
			const auto [it, inserted] = out.emplace(name, *cat);
			if (!inserted && it->second != *cat) {
				ambiguous[name] = true;
			}
		}
		for (const auto& [name, yes] : ambiguous) {
			out.erase(name);
		}
		return out;
	}

	core::DraftOptions MakeDraftOptions(const std::string& a_mainCell, const DraftContext& a_context)
	{
		core::DraftOptions options;
		options.mainCell = a_mainCell;
		if (const auto* cats = a_context.categories) {
			options.knownPattern = [cats](std::string_view p) { return cats->AnyMatches(p); };
			options.categoryNames = core::DraftCategoryNames(*cats);
		}
		if (const auto* names = a_context.itemNames) {
			options.itemCategoryByName = [names](std::string_view a_name) -> std::optional<std::string> {
				auto key = core::ToLower(a_name);
				for (int attempt = 0; attempt < 3; ++attempt) {
					if (const auto it = names->find(key); it != names->end()) {
						return it->second;
					}
					// "Lavender Sprigs" -> "lavender sprig", "Potatoes" -> "potato"
					if (attempt == 0 && key.ends_with('s')) {
						key.pop_back();
					} else if (attempt == 1 && key.ends_with('e')) {
						key.pop_back();
					} else {
						break;
					}
				}
				return std::nullopt;
			};
		}
		return options;
	}

	std::string HearthfireRoomKey(std::string_view a_editorId)
	{
		if (!HearthfireRef(a_editorId)) {
			return {};
		}
		const auto at = a_editorId.find("Room");
		if (at == std::string_view::npos || !Digit(a_editorId, at + 4)) {
			return {};
		}
		auto end = at + 4;
		while (Digit(a_editorId, end)) {
			++end;
		}
		// One key whatever the piece says: "BYOHHouse1InteriorRoom06", "BYOHHouse1Room06CraftingTrigger" and
		// "BYOHHouse1InteriorWorkbenchRoom06Ref" are the same room.
		return std::string(a_editorId.substr(0, 10)) + std::string(a_editorId.substr(at, end - at));
	}

	std::string WingName(std::string_view a_appearsWith)
	{
		// The rooms of every Hearthfire house, by number, as the shipped templates name them.
		static constexpr const char* rooms[] = { "Small house", "Main hall", "Bedrooms", "Greenhouse", "Enchanter's tower", "Storage room", "Trophy room",
			"Alchemy laboratory", "Armory", "Kitchen", "Library", "Cellar" };
		if (!HearthfireRef(a_appearsWith)) {
			return {};
		}
		const auto at = a_appearsWith.rfind("Room");
		if (at == std::string_view::npos || !Digit(a_appearsWith, at + 4)) {
			return {};
		}
		int n = 0;
		for (auto k = at + 4; Digit(a_appearsWith, k); ++k) {
			n = n * 10 + (a_appearsWith[k] - '0');
		}
		return n >= 1 && n <= static_cast<int>(std::size(rooms)) ? std::string(rooms[n - 1]) : std::string{};
	}

	std::vector<std::string> NearestStations(std::span<const core::DraftObject> a_objects, const core::DraftRules& a_rules, float a_radius)
	{
		using Kind = core::DraftObject::Kind;
		std::vector<std::size_t> stations;  // the objects a station rule matches (as core::SuggestDraft: keywords, then words)
		for (std::size_t i = 0; i < a_objects.size(); ++i) {
			const auto& o = a_objects[i];
			if (o.kind == Kind::Container || o.kind == Kind::Item || o.kind == Kind::Door || o.name.empty()) {
				continue;
			}
			auto       tokens = core::DraftTokens(o.editorId);
			const auto n = core::DraftTokens(o.name);
			tokens.insert(tokens.end(), n.begin(), n.end());
			const bool station = std::ranges::any_of(a_rules.stations, [&](const core::DraftStationRule& r) {
				return std::ranges::any_of(r.keywords, [&](const std::string& k) { return std::ranges::any_of(o.keywords, [&](const std::string& h) { return core::IEquals(h, k); }); }) ||
				       std::ranges::any_of(r.words, [&](const std::vector<std::string>& w) { return core::DraftWordsMatch(tokens, w); });
			});
			if (station) {
				stations.push_back(i);
			}
		}
		std::vector<std::string> out(a_objects.size());
		for (std::size_t ci = 0; ci < a_objects.size(); ++ci) {
			const auto& c = a_objects[ci];
			if (c.kind != Kind::Container) {
				continue;
			}
			const core::DraftObject* best = nullptr;
			float                    bestDist = a_radius;
			for (const auto si : stations) {
				const auto& o = a_objects[si];
				if (o.cell != c.cell || (o.appearsLater && !o.appearsWith.empty() && o.appearsWith != c.appearsWith)) {
					continue;  // another cell's, another wing's
				}
				const float dx = o.center[0] - c.center[0], dy = o.center[1] - c.center[1], dz = (o.center[2] - c.center[2]) * 2.0f;
				if (const float dist = std::sqrt(dx * dx + dy * dy + dz * dz); dist < bestDist) {
					bestDist = dist;
					best = &o;
				}
			}
			if (best) {
				out[ci] = core::ToLower(best->name);
			}
		}
		return out;
	}

	std::vector<core::FormRef> HomeCellRefs(const esp::LoadOrder& a_lo, const HomeEntry& a_home)
	{
		std::vector<core::FormRef> out{ a_lo.ToFormRef(a_home.cell) };
		for (const auto& e : a_home.extra) {
			out.push_back(a_lo.ToFormRef(e.cell));
		}
		return out;
	}
}
