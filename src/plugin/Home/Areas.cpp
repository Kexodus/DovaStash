#include "Home/Areas.h"

#include "Game/FormUtil.h"
#include "Home/HomeRegistry.h"
#include "Home/Store.h"
#include "Log.h"

#include <mutex>

namespace dova::home
{
	namespace
	{
		// Worldspace and grid of every exterior cell seen in memory this session. An exterior cell that is not
		// loaded may not be in memory at all; once seen, its persistent references can be matched to it while
		// they sit in the worldspace's persistent cell.
		struct GridInfo
		{
			RE::FormID     worldspace{ 0 };
			core::GridCell grid;
		};
		std::mutex                               g_gridLock;
		std::unordered_map<RE::FormID, GridInfo> g_grids;

		void Remember(RE::TESObjectCELL* a_cell)
		{
			if (!a_cell || !a_cell->IsExteriorCell()) {
				return;
			}
			const auto* coords = a_cell->GetCoordinates();
			const auto* world = a_cell->GetRuntimeData().worldSpace;
			if (!coords || !world) {
				return;
			}
			std::scoped_lock lock(g_gridLock);
			g_grids[a_cell->GetFormID()] = GridInfo{ world->GetFormID(), core::GridCell{ coords->cellX, coords->cellY } };
		}

		std::optional<GridInfo> KnownGrid(RE::FormID a_cell)
		{
			std::scoped_lock lock(g_gridLock);
			const auto       it = g_grids.find(a_cell);
			return it != g_grids.end() ? std::optional<GridInfo>(it->second) : std::nullopt;
		}

		std::string OwnerName(RE::TESForm* a_owner)
		{
			if (const char* name = a_owner ? a_owner->GetName() : nullptr; name && *name) {
				return name;
			}
			return a_owner ? std::format("{:08X}", a_owner->GetFormID()) : "nobody"s;
		}

		std::string Utf8(const std::filesystem::path& a_path)
		{
			try {
				const auto text = a_path.u8string();
				return std::string(text.begin(), text.end());
			} catch (...) {
				return {};
			}
		}
	}

	const Area* Home::AreaById(RE::FormID a_cell) const
	{
		if (a_cell == 0) {
			return nullptr;
		}
		const auto it = std::ranges::find(areas, a_cell, &Area::id);
		return it != areas.end() ? &*it : nullptr;
	}

	std::string AreaName(RE::TESObjectCELL* a_cell, const core::FormRef& a_ref)
	{
		if (a_cell) {
			if (const char* name = a_cell->GetName(); name && *name) {
				return name;
			}
			if (const char* edid = a_cell->GetFormEditorID(); edid && *edid) {
				return edid;
			}
		}
		return a_ref.ToString();
	}

	void BuildAreas(Home& a_home)
	{
		a_home.areas.clear();
		a_home.areasExcluded.clear();
		const std::span<const core::HomeCell> cells = a_home.layout ? std::span<const core::HomeCell>(a_home.layout->cells) : std::span<const core::HomeCell>{};
		a_home.areasFrom = core::HomeCellRefs(a_home.cell, cells);

		const auto& store = Store::Get();
		const auto  refs = core::EffectiveHomeCells(
			a_home.cell, cells, [&](const core::FormRef& a_cell) { return store.HasPlayerLayoutFile(a_cell); }, &a_home.areasExcluded);
		for (std::size_t i = 0; i < refs.size(); ++i) {
			Area area;
			area.cell = refs[i];
			area.main = i == 0;
			area.id = game::FormIdOf(refs[i]).value_or(0);
			if (area.id == 0 && !area.main) {
				logger::debug("{}: area {} not used (its plugin is not loaded)", a_home.name, refs[i].ToString());
				continue;
			}
			auto* cell = area.id ? RE::TESForm::LookupByID<RE::TESObjectCELL>(area.id) : nullptr;
			if (cell) {
				area.interior = cell->IsInteriorCell();
				Remember(cell);
			}
			if (const auto grid = KnownGrid(area.id)) {
				area.interior = false;
				area.worldspace = grid->worldspace;
				area.grid = grid->grid;
			}
			const auto entry = std::ranges::find(cells, refs[i], &core::HomeCell::cell);
			if (entry != cells.end() && !entry->label.empty()) {
				area.name = entry->label;
			} else if (area.main && !a_home.name.empty()) {
				area.name = a_home.name;
			} else {
				area.name = AreaName(cell, refs[i]);
			}
			a_home.areas.push_back(std::move(area));
		}
	}

	const Area* AreaOf(const Home& a_home, RE::TESObjectREFR* a_ref)
	{
		auto* cell = a_ref ? a_ref->GetParentCell() : nullptr;
		if (!cell) {
			return nullptr;
		}
		if (const auto* area = a_home.AreaById(cell->GetFormID())) {
			return area;
		}
		// A persistent exterior reference whose own cell is not loaded is kept in its worldspace's persistent
		// cell: match it by worldspace and grid position.
		auto* world = a_ref->GetWorldspace();
		if (!world || world->persistentCell != cell) {
			return nullptr;
		}
		const auto grid = core::GridOf(a_ref->GetPositionX(), a_ref->GetPositionY());
		for (const auto& area : a_home.areas) {
			if (area.grid && *area.grid == grid && area.worldspace == world->GetFormID()) {
				return &area;
			}
		}
		return nullptr;
	}

	game::Verdict CheckHomeContainer(RE::TESObjectREFR* a_ref, const Home& a_home, core::Reach* a_reach)
	{
		auto* player = game::Player();
		auto* playerCell = player ? player->GetParentCell() : nullptr;
		if (a_reach) {
			*a_reach = core::Reach::Here;
		}
		if (!a_home.MultiCell() || !a_ref || a_ref->GetParentCell() == playerCell) {
			return game::CheckContainer(a_ref, playerCell);  // a single-cell home: exactly as before
		}
		auto*       cell = a_ref->GetParentCell();
		const auto* area = AreaOf(a_home, a_ref);
		const core::RefPlacement place{
			.inHome = area != nullptr,
			.inPlayerCell = false,
			.cellAttached = cell && area && area->id == cell->GetFormID() && cell->IsAttached(),
			.persistent = a_ref->IsPersistent(),
		};
		const auto reach = core::JudgeReach(place);
		if (a_reach) {
			*a_reach = reach;
		}
		if (reach == core::Reach::Outside) {
			return game::Verdict::Reject("container is not in this home");
		}
		if (reach == core::Reach::Unreachable) {
			return game::Verdict::Reject(std::format("only reachable in {} (not a persistent reference)", area->name));
		}
		// In the home and reachable: the usual checks, against the container's own cell (its owner, whether it
		// never resets). A persistent exterior reference in its worldspace's persistent cell has neither, so a
		// respawning one is refused there.
		return game::CheckContainer(a_ref, cell);
	}

	game::Verdict CheckExtraCell(RE::TESObjectCELL* a_cell)
	{
		if (!a_cell) {
			return game::Verdict::Reject("no cell");
		}
		if ((a_cell->GetFormID() >> 24) == 0xFF) {
			return game::Verdict::Reject("temporary cell");
		}
		auto* owner = a_cell->GetOwner();
		if (owner && !game::IsPlayerOwner(owner) && !game::IsPlayerHouse(a_cell)) {
			return game::Verdict::Reject(std::format("owned by {}", OwnerName(owner)));
		}
		Remember(a_cell);
		return game::Verdict::Accept();
	}

	void LogAreas(const Home& a_home)
	{
		if (!a_home.MultiCell() && a_home.areasExcluded.empty()) {
			return;
		}
		logger::debug("{}: {} area(s){}", a_home.name, a_home.areas.size(), a_home.extra ? " (you are in an extra one)" : "");
		for (const auto& area : a_home.areas) {
			logger::debug("  area {} [{}]{}{}{}", area.name, area.cell.ToString(), area.main ? " main" : "",
				!area.interior ? "" : *area.interior ? " interior" : " exterior", area.id == a_home.cellId ? ", you are here" : "");
		}
		for (const auto& excluded : a_home.areasExcluded) {
			logger::debug("  {} is listed but has a setup of its own: not part of {}", excluded.ToString(), a_home.name);
		}
		if (!a_home.layout) {
			return;
		}
		const auto describe = [&](const core::FormRef& a_ref, std::string_view a_label) {
			auto* ref = game::LookupRef(a_ref);
			if (!ref) {
				logger::debug("  {} [{}]: not in memory (not persistent, in an area that is not loaded; or its plugin is missing)", a_label,
					a_ref.ToString());
				return;
			}
			const auto* area = AreaOf(a_home, ref);
			const auto  where = area ? area->name : "no area of this home"s;
			logger::debug("  {} [{}] in {}: {}", a_label.empty() ? game::RefName(ref) : std::string(a_label), a_ref.ToString(), where,
				ref->IsPersistent() ? "reachable from every area (persistent)"s : std::format("reachable only in {} (not persistent)", where));
		};
		if (a_home.layout->master) {
			describe(*a_home.layout->master, "master");
		}
		for (const auto& c : a_home.layout->containers) {
			if (!c.ignored && !a_home.layout->IsMaster(c.ref)) {
				describe(c.ref, c.label);
			}
		}
	}

	// --- Store: which home a cell belongs to ------------------------------------------------------------------

	bool Store::HasPlayerLayoutFile(const core::FormRef& a_cell) const
	{
		std::error_code ec;
		const bool      exists = std::filesystem::exists(LayoutPath(a_cell), ec);
		return exists || ec;  // unknown: as if there were one (a cell whose file we cannot see is never taken over)
	}

	core::CellMembership Store::MembershipOf(const core::FormRef& a_cell) const
	{
		auto membership = core::ResolveCellMembership(a_cell, HasPlayerLayoutFile(a_cell), _homeCells);
		if (!membership.otherClaims.empty()) {
			std::string others;
			for (const auto& c : membership.otherClaims) {
				others += (others.empty() ? "" : ", ") + c.ToString();
			}
			const auto text = std::format("Cell {} is listed by several homes; it is part of {} (also listed by {})", a_cell.ToString(),
				membership.home.ToString(), others);
			if (log::FirstTime("cells-claims:" + a_cell.ToString())) {
				logger::warn("{}", text);
			} else {
				logger::debug("{}", text);
			}
		}
		return membership;
	}

	void Store::IndexHomeCells(const core::FormRef& a_cell, const core::Layout* a_layout) const
	{
		std::erase_if(_homeCells, [&](const core::HomeCellsEntry& e) { return e.home == a_cell; });
		if (!a_layout || a_layout->cells.empty()) {
			return;
		}
		core::HomeCellsEntry entry{ a_cell, {} };
		for (const auto& c : a_layout->cells) {
			if (c.cell != a_cell && std::ranges::find(entry.cells, c.cell) == entry.cells.end()) {
				entry.cells.push_back(c.cell);
			}
		}
		if (!entry.cells.empty()) {
			_homeCells.push_back(std::move(entry));
		}
	}

	void Store::LoadHomeCellIndex()
	{
		_homeCells.clear();
		const auto      dir = std::filesystem::path{ kRoot } / "Layouts";
		std::error_code ec;
		std::size_t     extra = 0;
		for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
			std::error_code fileEc;
			if (!it->is_regular_file(fileEc)) {
				continue;
			}
			const auto name = Utf8(it->path().filename());
			const auto cell = core::CellFromLayoutFileName(name);
			if (!cell) {
				continue;  // backups (.bak), temp files, anything else
			}
			core::Diagnostics diags;  // reported when the home is entered (LoadPlayerLayout)
			std::optional<core::Layout> layout;
			try {
				layout = core::LoadLayoutFile(it->path(), "Layouts/" + name, diags);
			} catch (const std::exception& e) {
				logger::debug("Multi-cell homes: cannot read {}: {}", name, e.what());
			}
			if (layout && !layout->cells.empty()) {
				IndexHomeCells(*cell, &*layout);
				++extra;
				logger::debug("Multi-cell homes: {} lists {} more cell(s)", name, layout->cells.size());
			}
		}
		if (extra > 0) {
			logger::info("Multi-cell homes: {} setup(s) span more than one cell", extra);
		}
	}
}
