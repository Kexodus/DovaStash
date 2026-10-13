#include "home/Cell.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "Util.h"
#include "dova/core/Template.h"
#include "esp/Vmad.h"

namespace dova::editor::home
{
	using esp::FormKey;
	using esp::MakeSig;
	using esp::Sig;

	namespace
	{
		constexpr Sig kACHR = MakeSig("ACHR");
		constexpr Sig kCONT = MakeSig("CONT");
		constexpr Sig kNPC_ = MakeSig("NPC_");
		constexpr Sig kFLST = MakeSig("FLST");

		// Item record types a plaque or case script can be made for (esm_dump.py DISPLAY_ITEM_TYPES).
		constexpr Sig kDisplayItemTypes[] = { MakeSig("ARMO"), MakeSig("WEAP"), MakeSig("BOOK"), MakeSig("MISC"), MakeSig("ALCH"),
			MakeSig("INGR"), MakeSig("SLGM"), MakeSig("AMMO"), MakeSig("SCRL") };

		// A script's properties after merging the base record's with the reference's (the reference wins).
		struct MergedProp
		{
			esp::VmadProperty::Type type{ esp::VmadProperty::Type::None };
			std::vector<FormKey>    forms;
			std::int32_t            intValue{ 0 };
			bool                    boolValue{ false };
		};
		struct MergedScript
		{
			std::string                       name;
			std::map<std::string, MergedProp> props;  // lower-case property name
		};

		void Merge(esp::LoadOrder& a_lo, std::uint32_t a_plugin, std::span<const std::byte> a_vmad, std::vector<MergedScript>& a_out)
		{
			for (auto& s : esp::DecodeVmad(a_vmad)) {
				auto it = std::ranges::find_if(a_out, [&](const MergedScript& m) { return core::IEquals(m.name, s.name); });
				if (it == a_out.end()) {
					a_out.push_back(MergedScript{ s.name, {} });
					it = std::prev(a_out.end());
				}
				for (const auto& p : s.properties) {
					MergedProp mp{ p.type, {}, p.intValue, p.boolValue };
					for (const auto raw : p.objects) {
						mp.forms.push_back(a_lo.Resolve(a_plugin, raw));
					}
					it->props[core::ToLower(p.name)] = std::move(mp);
				}
			}
		}

		bool IsDisplayItem(const esp::RecordRef* a_rec)
		{
			return a_rec && std::ranges::find(kDisplayItemTypes, a_rec->type) != std::end(kDisplayItemTypes);
		}

		std::vector<FormKey> DisplayItems(esp::LoadOrder& a_lo, const MergedScript& a_script)
		{
			std::vector<FormKey> out;
			auto                 add = [&](const FormKey& k) {
                if (std::ranges::find(out, k) == out.end()) {
                    out.push_back(k);
                }
			};
			for (const auto& [name, prop] : a_script.props) {
				for (const auto& f : prop.forms) {
					const auto* rec = a_lo.Find(f);
					if (IsDisplayItem(rec)) {
						add(f);
					} else if (rec && rec->type == kFLST) {
						const auto subs = a_lo.Read(*rec);
						for (const auto& lnam : subs.GetAll(MakeSig("LNAM"))) {
							const auto item = a_lo.Resolve(rec->plugin, esp::Read<std::uint32_t>(lnam, 0));
							if (IsDisplayItem(a_lo.Find(item))) {
								add(item);
							}
						}
					}
				}
			}
			return out;
		}

		std::string RecordLabel(esp::LoadOrder& a_lo, const FormKey& a_key)
		{
			const auto* rec = a_lo.Find(a_key);
			if (!rec) {
				return a_lo.Format(a_key);
			}
			const auto subs = a_lo.Read(*rec);
			auto       edid = a_lo.EditorId(subs);
			return edid.empty() ? a_lo.Format(a_key) : edid;
		}

		std::optional<FormKey> FindExteriorCell(esp::LoadOrder& a_lo, const FormKey& a_world, std::int32_t a_x, std::int32_t a_y)
		{
			std::optional<FormKey> found;
			for (std::uint32_t i = 0; i < a_lo.PluginCount(); ++i) {
				for (const auto& c : a_lo.CellsOf(i)) {
					if (c.worldPersistent || c.world != a_world) {
						continue;
					}
					const auto subs = a_lo.Read(c.rec);
					const auto xclc = subs.Get(MakeSig("XCLC"));
					if (xclc && xclc->size() >= 8 && esp::Read<std::int32_t>(*xclc, 0) == a_x && esp::Read<std::int32_t>(*xclc, 4) == a_y) {
						found = c.key;
					}
				}
			}
			return found;
		}

		std::string LandTexture(esp::LoadOrder& a_lo, const FormKey& a_ltex)
		{
			const auto* ltex = a_lo.Find(a_ltex, MakeSig("LTEX"));
			if (!ltex) {
				return {};
			}
			const auto ls = a_lo.Read(*ltex);
			const auto tnam = ls.Get(MakeSig("TNAM"));
			if (!tnam || tnam->size() < 4) {
				return {};
			}
			const auto* txst = a_lo.Find(a_lo.Resolve(ltex->plugin, esp::Read<std::uint32_t>(*tnam, 0)), MakeSig("TXST"));
			if (!txst) {
				return {};
			}
			const auto ts = a_lo.Read(*txst);
			const auto tx00 = ts.Get(MakeSig("TX00"));
			return tx00 ? esp::ZString(*tx00) : std::string{};
		}

		std::optional<CellLand> LoadLand(esp::LoadOrder& a_lo, const FormKey& a_cell)
		{
			const auto rec = a_lo.CellLandRecord(a_cell);
			if (!rec) {
				return std::nullopt;
			}
			const auto subs = a_lo.Read(*rec);
			const auto vhgt = subs.Get(MakeSig("VHGT"));
			const auto* cell = a_lo.FindCell(a_cell);
			if (!vhgt || vhgt->size() < 4 + 33 * 33 || !cell) {
				return std::nullopt;
			}
			CellLand land;
			float     row = esp::Read<float>(*vhgt, 0);
			for (std::size_t y = 0; y < 33; ++y) {
				row += static_cast<float>(static_cast<std::int8_t>((*vhgt)[4 + y * 33]));
				float col = row;
				land.heights[y * 33] = col * 8.0f;
				for (std::size_t x = 1; x < 33; ++x) {
					col += static_cast<float>(static_cast<std::int8_t>((*vhgt)[4 + y * 33 + x]));
					land.heights[y * 33 + x] = col * 8.0f;
				}
			}
			if (const auto vnml = subs.Get(MakeSig("VNML")); vnml && vnml->size() >= 33 * 33 * 3) {
				land.normals.resize(33 * 33);
				for (std::size_t i = 0; i < 33 * 33; ++i) {
					for (std::size_t k = 0; k < 3; ++k) {
						land.normals[i][k] = static_cast<float>(static_cast<std::int8_t>((*vnml)[i * 3 + k])) / 127.0f;
					}
				}
			}
			if (const auto vclr = subs.Get(MakeSig("VCLR")); vclr && vclr->size() >= 33 * 33 * 3) {
				land.colors.resize(33 * 33);
				for (std::size_t i = 0; i < 33 * 33; ++i) {
					land.colors[i] = static_cast<std::uint32_t>((*vclr)[i * 3]) | (static_cast<std::uint32_t>((*vclr)[i * 3 + 1]) << 8) |
					                 (static_cast<std::uint32_t>((*vclr)[i * 3 + 2]) << 16) | 0xFF000000u;
				}
			}
			for (const auto& btxt : subs.GetAll(MakeSig("BTXT"))) {
				if (btxt.size() < 8) {
					continue;
				}
				const auto quadrant = static_cast<std::uint8_t>(btxt[4]);
				const auto raw = esp::Read<std::uint32_t>(btxt, 0);
				if (quadrant < 4 && raw != 0) {
					land.textures[quadrant] = LandTexture(a_lo, a_lo.Resolve(rec->plugin, raw));
				}
			}
			return land;
		}

		bool HasScript(const PlacedRef& a_ref, std::string_view a_name)
		{
			return std::ranges::any_of(a_ref.scripts, [&](const std::string& s) { return core::IEquals(s, a_name); });
		}
	}

	bool PlacedRef::IsContainer() const noexcept { return baseType == kCONT; }

	std::string DisplayLabel(const PlacedRef& a_ref)
	{
		if (!a_ref.display) {
			return {};
		}
		std::string s(core::DisplayKindName(*a_ref.display));
		if (a_ref.rackType) {
			static constexpr const char* names[] = { "?", "Any", "Shield", "NoDaggerOrBow", "Dagger", "LargeCase" };
			const auto                   t = static_cast<int>(*a_ref.rackType);
			s += " (";
			s += (t >= 1 && t <= 5) ? names[t] : "?";
			s += ")";
		} else if (!a_ref.displayItems.empty()) {
			s += " (" + std::to_string(a_ref.displayItems.size()) + " item" + (a_ref.displayItems.size() == 1 ? "" : "s") + ")";
		}
		return s;
	}

	std::optional<CellInfo> LoadCell(esp::LoadOrder& a_lo, const FormKey& a_cell, const LoadCellOptions& a_options)
	{
		const auto* entry = a_lo.FindCell(a_cell);
		if (!entry) {
			return std::nullopt;
		}
		CellInfo info;
		info.key = a_cell;
		info.world = entry->world;
		info.lastOverride = a_lo.PluginAt(entry->rec.plugin).Name();
		{
			const auto subs = a_lo.Read(entry->rec);
			info.editorId = a_lo.EditorId(subs);
			info.name = a_lo.FullName(entry->rec, subs);
			if (const auto data = subs.Get(MakeSig("DATA")); data && !data->empty()) {
				info.interior = (static_cast<std::uint8_t>((*data)[0]) & 1) != 0;
			}
			if (const auto xclc = subs.Get(MakeSig("XCLC")); xclc && xclc->size() >= 8) {
				info.grid = std::array{ esp::Read<std::int32_t>(*xclc, 0), esp::Read<std::int32_t>(*xclc, 4) };
			}
			if (const auto xown = subs.Get(MakeSig("XOWN")); xown && xown->size() >= 4) {
				info.owner = a_lo.Resolve(entry->rec.plugin, esp::Read<std::uint32_t>(*xown, 0));
				info.ownerLabel = RecordLabel(a_lo, *info.owner);
				const bool skyrim = core::IEquals(a_lo.FileName(info.owner->file), "Skyrim.esm");
				info.ownedByPlayer = skyrim && (info.owner->id == 0x7 || info.owner->id == 0xDB1);  // Player, PlayerFaction
			}
			if (const auto xlcn = subs.Get(MakeSig("XLCN")); xlcn && xlcn->size() >= 4) {
				info.location = a_lo.Resolve(entry->rec.plugin, esp::Read<std::uint32_t>(*xlcn, 0));
				info.locationLabel = RecordLabel(a_lo, *info.location);
				if (const auto* loc = a_lo.Find(*info.location, MakeSig("LCTN"))) {
					const auto ls = a_lo.Read(*loc);
					if (const auto kwda = ls.Get(MakeSig("KWDA"))) {
						for (std::size_t i = 0; i + 4 <= kwda->size(); i += 4) {
							info.locationKeywords.push_back(RecordLabel(a_lo, a_lo.Resolve(loc->plugin, esp::Read<std::uint32_t>(*kwda, i))));
						}
					}
				}
			}
			if (const auto xezn = subs.Get(MakeSig("XEZN")); xezn && xezn->size() >= 4) {
				const auto ez = a_lo.Resolve(entry->rec.plugin, esp::Read<std::uint32_t>(*xezn, 0));
				info.encounterZone = RecordLabel(a_lo, ez);
				if (const auto* ezRec = a_lo.Find(ez, MakeSig("ECZN"))) {
					const auto es = a_lo.Read(*ezRec);
					if (const auto data = es.Get(MakeSig("DATA")); data && data->size() >= 11) {
						info.neverResets = (static_cast<std::uint8_t>((*data)[10]) & 1) != 0;
					}
				}
			}
		}

		if (!info.interior) {
			info.land = LoadLand(a_lo, a_cell);
		}

		std::vector<std::pair<std::size_t, FormKey>> fronts;  // ref index -> linked ref (keyword-less XLKR)
		for (const auto& rec : a_lo.CellRefRecords(a_cell)) {
			if (rec.flags & esp::kRecDeleted) {
				continue;
			}
			const auto subs = a_lo.Read(rec);
			PlacedRef  r;
			r.key = a_lo.KeyOf(rec);
			r.recordType = rec.type;
			r.editorId = a_lo.EditorId(subs);
			r.persistent = (rec.flags & esp::kRecPersistent) != 0;
			r.initiallyDisabled = (rec.flags & esp::kRecInitiallyDisabled) != 0;
			if (const auto name = subs.Get(MakeSig("NAME")); name && name->size() >= 4) {
				r.base = a_lo.Resolve(rec.plugin, esp::Read<std::uint32_t>(*name, 0));
			}
			if (const auto data = subs.Get(MakeSig("DATA")); data && data->size() >= 24) {
				for (std::size_t i = 0; i < 3; ++i) {
					r.position[i] = esp::Read<float>(*data, i * 4);
					r.rotation[i] = esp::Read<float>(*data, 12 + i * 4);
				}
			}
			if (const auto xscl = subs.Get(MakeSig("XSCL")); xscl && xscl->size() >= 4) {
				r.scale = esp::Read<float>(*xscl, 0);
			}
			if (const auto xesp = subs.Get(MakeSig("XESP")); xesp && xesp->size() >= 4) {
				r.enableParent = a_lo.Resolve(rec.plugin, esp::Read<std::uint32_t>(*xesp, 0));
				r.enableOpposite = xesp->size() >= 8 && (esp::Read<std::uint32_t>(*xesp, 4) & 1) != 0;
			}
			if (const auto xtel = subs.Get(MakeSig("XTEL")); xtel && xtel->size() >= 4) {
				r.teleportDoor = a_lo.Resolve(rec.plugin, esp::Read<std::uint32_t>(*xtel, 0));
				if (xtel->size() >= 16) {
					r.teleportPosition = std::array{ esp::Read<float>(*xtel, 4), esp::Read<float>(*xtel, 8), esp::Read<float>(*xtel, 12) };
				}
			}
			for (const auto& xlkr : subs.GetAll(MakeSig("XLKR"))) {
				if (xlkr.size() >= 8 && esp::Read<std::uint32_t>(xlkr, 0) == 0) {
					fronts.emplace_back(info.refs.size(), a_lo.Resolve(rec.plugin, esp::Read<std::uint32_t>(xlkr, 4)));
				}
			}

			std::vector<MergedScript> scripts;
			if (const auto* base = r.base.Valid() ? a_lo.Find(r.base) : nullptr) {
				r.baseType = base->type;
				const auto bs = a_lo.Read(*base);
				r.baseEditorId = a_lo.EditorId(bs);
				r.baseName = a_lo.FullName(*base, bs);
				// Armor: MODL links the armature; the world model is MOD2 (male) or MOD4 (female).
				const bool armor = base->type == MakeSig("ARMO");
				for (const auto sig : armor ? std::initializer_list<Sig>{ MakeSig("MOD2"), MakeSig("MOD4") } : std::initializer_list<Sig>{ MakeSig("MODL") }) {
					if (const auto modl = bs.Get(sig); modl && modl->size() > 1) {
						r.model = esp::ZString(*modl);
						break;
					}
				}
				if (const auto obnd = bs.Get(MakeSig("OBND")); obnd && obnd->size() >= 12) {
					for (std::size_t i = 0; i < 6; ++i) {
						r.bounds[i] = esp::Read<std::int16_t>(*obnd, i * 2);
					}
					r.hasBounds = true;
				}
				if (base->type == kCONT) {
					if (const auto cd = bs.Get(MakeSig("DATA")); cd && !cd->empty()) {
						r.respawns = (static_cast<std::uint8_t>((*cd)[0]) & 0x02) != 0;
					}
				}
				if (const auto vmad = bs.Get(MakeSig("VMAD"))) {
					Merge(a_lo, base->plugin, *vmad, scripts);
				}
			}
			if (const auto vmad = subs.Get(MakeSig("VMAD"))) {
				Merge(a_lo, rec.plugin, *vmad, scripts);
			}
			for (const auto& s : scripts) {
				r.scripts.push_back(s.name);
			}

			// Displays (DESIGN §8.4, as esm_dump.py find_displays).
			const auto findScript = [&](std::string_view n) -> const MergedScript* {
				const auto it = std::ranges::find_if(scripts, [&](const MergedScript& m) { return core::IEquals(m.name, n); });
				return it == scripts.end() ? nullptr : &*it;
			};
			if (rec.type == kACHR || r.baseType == kNPC_) {
				if (findScript("MannequinActivatorSCRIPT")) {
					r.display = core::DisplayKind::Mannequin;
				}
			} else if (const auto* rack = findScript("WeaponRackActivateSCRIPT")) {
				r.display = core::DisplayKind::Rack;
				int type = 1;
				if (const auto it = rack->props.find("racktype"); it != rack->props.end()) {
					type = it->second.intValue;
				}
				if (const auto it = rack->props.find("patch14coaracks"); it != rack->props.end() && it->second.boolValue) {
					type = 3;
				}
				if (type >= 1 && type <= 5) {
					r.rackType = static_cast<core::RackType>(type);
				}
			} else if (r.baseType == MakeSig("ACTI") || r.baseType == kCONT || r.baseType == MakeSig("MSTT") || r.baseType == MakeSig("STAT")) {
				for (const auto& s : scripts) {
					auto items = DisplayItems(a_lo, s);
					if (!items.empty()) {
						r.display = r.baseType == kCONT ? core::DisplayKind::Case : core::DisplayKind::Item;
						r.displayItems = std::move(items);
						break;
					}
				}
			}
			info.refs.push_back(std::move(r));
		}

		for (const auto& [index, target] : fronts) {
			const auto it = std::ranges::find_if(info.refs, [&](const PlacedRef& p) { return p.key == target; });
			if (it != info.refs.end() && it->IsContainer() && info.refs[index].baseType == MakeSig("ACTI")) {
				info.refs[index].opens = target;
			}
		}

		if (a_options.resolveDoors) {
			esp::FormKeySet wanted;
			for (const auto& r : info.refs) {
				if (r.teleportDoor) {
					wanted.insert(*r.teleportDoor);
				}
			}
			const auto cells = wanted.empty() ? esp::FormKeyMap<FormKey>{} : a_lo.FindCellsOfRefs(wanted);
			for (const auto& r : info.refs) {
				if (!r.teleportDoor) {
					continue;
				}
				DoorLink   link{ r.key, *r.teleportDoor, std::nullopt, {}, false };
				const auto it = cells.find(*r.teleportDoor);
				const auto* dest = it != cells.end() ? a_lo.FindCell(it->second) : nullptr;
				if (!dest) {
					link.destinationLabel = "(other side not in the loaded plugins)";
				} else if (dest->worldPersistent && dest->world && r.teleportPosition) {
					// A persistent exterior door lives in the worldspace's persistent cell: the place the
					// player arrives at (XTEL position) gives the grid cell.
					const auto gx = static_cast<std::int32_t>(std::floor((*r.teleportPosition)[0] / 4096.0f));
					const auto gy = static_cast<std::int32_t>(std::floor((*r.teleportPosition)[1] / 4096.0f));
					link.destinationCell = FindExteriorCell(a_lo, *dest->world, gx, gy);
					link.destinationLabel = RecordLabel(a_lo, *dest->world) + " (" + std::to_string(gx) + ", " + std::to_string(gy) + ")";
					if (link.destinationCell) {
						if (const auto* ext = a_lo.FindCell(*link.destinationCell)) {
							if (auto edid = a_lo.EditorId(a_lo.Read(ext->rec)); !edid.empty()) {
								link.destinationLabel += " " + edid;
							}
						}
					}
				} else {
					link.destinationCell = it->second;
					link.destinationInterior = !dest->world.has_value();
					const auto edid = a_lo.EditorId(a_lo.Read(dest->rec));
					link.destinationLabel = edid.empty() ? a_lo.Format(it->second) : edid;
				}
				info.doors.push_back(std::move(link));
			}
		}
		return info;
	}

	std::vector<HomeCandidate> FindHomes(esp::LoadOrder& a_lo, std::uint32_t a_plugin, bool a_exteriors)
	{
		std::vector<HomeCandidate> out;
		const auto                 self = a_lo.FileId(a_lo.PluginAt(a_plugin).Name());
		for (const auto& c : a_lo.CellsOf(a_plugin)) {
			if (c.worldPersistent || (c.world && !a_exteriors)) {
				continue;
			}
			const auto cell = LoadCell(a_lo, c.key, LoadCellOptions{ .resolveDoors = false });
			if (!cell || (!cell->interior && !a_exteriors)) {
				continue;
			}
			HomeCandidate h;
			h.cell = c.key;
			h.editorId = cell->editorId;
			h.name = cell->name;
			h.interior = cell->interior;
			h.overridesOther = c.key.file != self;
			h.references = static_cast<int>(cell->refs.size());
			for (const auto& r : cell->refs) {
				if (r.IsContainer()) {
					++h.containers;
					h.respawning += r.respawns ? 1 : 0;
				}
				if (r.display) {
					switch (*r.display) {
					case core::DisplayKind::Rack:
						++h.racks;
						break;
					case core::DisplayKind::Mannequin:
						++h.mannequins;
						break;
					default:
						++h.plaques;
						break;
					}
				}
			}
			h.playerHouseKeyword = std::ranges::any_of(cell->locationKeywords, [](const std::string& k) { return core::IEquals(k, "LocTypePlayerHouse"); });
			h.ownedByPlayer = cell->ownedByPlayer;
			h.neverResets = cell->neverResets;
			if (h.playerHouseKeyword) {
				h.score += 4;
				h.reasons.push_back("LocTypePlayerHouse");
			}
			if (h.ownedByPlayer) {
				h.score += 3;
				h.reasons.push_back("owned by the player");
			}
			if (h.neverResets) {
				h.score += 1;
				h.reasons.push_back("never resets");
			}
			const int usable = h.containers - h.respawning;
			if (usable >= 1) {
				h.score += usable >= 5 ? 2 : 1;
				h.reasons.push_back(std::to_string(usable) + " usable containers");
			}
			if (h.racks + h.mannequins + h.plaques > 0) {
				h.score += 1;
				h.reasons.push_back("displays");
			}
			if (h.mannequins > 0 && h.respawning == 0) {
				h.score += 2;
				h.reasons.push_back("mannequins, nothing respawns");
			}
			if (h.containers > 0 || h.score >= 2) {
				out.push_back(std::move(h));
			}
		}
		std::ranges::stable_sort(out, [](const HomeCandidate& a, const HomeCandidate& b) {
			return a.score != b.score ? a.score > b.score : a.containers > b.containers;
		});
		return out;
	}

	core::Layout DraftTemplate(esp::LoadOrder& a_lo, const CellInfo& a_cell, core::Diagnostics& a_diags)
	{
		core::Layout t;
		t.name = a_cell.name.empty() ? a_cell.editorId : a_cell.name;
		t.author = "";
		t.version = "1.0.0";
		t.cell = a_lo.ToFormRef(a_cell.key);

		std::vector<const PlacedRef*> containers;
		for (const auto& r : a_cell.refs) {
			if (r.IsContainer()) {
				containers.push_back(&r);
			}
		}
		std::ranges::sort(containers, [&](const PlacedRef* a, const PlacedRef* b) { return a_lo.ToFormRef(a->key) < a_lo.ToFormRef(b->key); });

		const PlacedRef* master = nullptr;
		for (const auto* r : containers) {
			core::ContainerBinding b;
			b.ref = a_lo.ToFormRef(r->key);
			b.label = r->baseName.empty() ? r->baseEditorId : r->baseName;
			if (r->respawns) {
				b.ignored = true;
				b.note = "Respawns: what you store here can disappear.";
			} else if (core::IEquals(r->baseEditorId, "PlayerBookShelfContainer") || HasScript(*r, "PlayerBookShelfContainerScript")) {
				b.ignored = true;
				b.note = "Scripted bookshelf: it only holds books and hands everything else back.";
			} else if (r->enableOpposite && !HasScript(*r, "BYOHDivertItemScript")) {
				b.ignored = true;
				b.note = "Goes away with a house upgrade: anything inside would be lost.";
			} else if (!r->initiallyDisabled && !r->enableParent) {
				const bool chest = IContains(r->baseEditorId, "chest") || IContains(r->baseName, "chest");
				if (!master || (chest && !(IContains(master->baseEditorId, "chest") || IContains(master->baseName, "chest")))) {
					master = r;
				}
			}
			t.containers.push_back(std::move(b));
		}
		if (master) {
			t.master = a_lo.ToFormRef(master->key);
			std::erase_if(t.containers, [&](const core::ContainerBinding& b) { return b.ref == *t.master; });
		}
		t.note = "Draft from Dova Stash Editor: give the containers their categories (see Categories/00_Default.json); ignored ones are never used.";
		t.requiredPlugins = core::ReferencedPlugins(t);
		core::ValidateTemplate(t, "draft", a_diags);
		return t;
	}
}
