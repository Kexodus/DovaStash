#include "dova/core/HomeCells.h"

#include <algorithm>
#include <cmath>

#include "JsonUtil.h"

namespace dova::core
{
	using detail::json;
	using ojson = nlohmann::ordered_json;

	namespace
	{
		ojson ToOrdered(const json& a_value)
		{
			return ojson::parse(a_value.dump());
		}

		std::optional<HomeCell> ParseEntry(const json& a_value, const std::string& a_source, const std::string& a_where, Diagnostics& a_diags)
		{
			if (a_value.is_string()) {
				auto ref = FormRef::Parse(a_value.get<std::string>());
				if (!ref) {
					a_diags.Error(a_source, a_where + ": invalid form reference \"" + a_value.get<std::string>() + "\" (kept verbatim): " + FormRefError(a_value.get<std::string>()));
					return std::nullopt;
				}
				return HomeCell{ std::move(*ref) };
			}
			if (!a_value.is_object()) {
				a_diags.Error(a_source, a_where + ": expected a cell form reference or an object, got " + detail::TypeName(a_value) + " (kept verbatim)");
				return std::nullopt;
			}
			const auto cellIt = a_value.find("cell");
			if (cellIt == a_value.end() || !cellIt->is_string()) {
				a_diags.Error(a_source, a_where + ": missing \"cell\" (kept verbatim)");
				return std::nullopt;
			}
			auto ref = FormRef::Parse(cellIt->get<std::string>());
			if (!ref) {
				a_diags.Error(a_source, a_where + ".cell: invalid form reference \"" + cellIt->get<std::string>() + "\" (kept verbatim): " + FormRefError(cellIt->get<std::string>()));
				return std::nullopt;
			}
			HomeCell entry{ std::move(*ref) };
			for (const auto& [key, value] : a_value.items()) {
				if (key == "cell") {
					continue;
				}
				if (key == "label" && value.is_string()) {
					entry.label = value.get<std::string>();
				} else if (key == "master" && value.is_string() && FormRef::Parse(value.get<std::string>())) {
					entry.master = FormRef::Parse(value.get<std::string>());
				} else if (key == "note" && value.is_string()) {
					entry.note = value.get<std::string>();
				} else {
					if (key == "master" && value.is_string()) {
						a_diags.Error(a_source, a_where + ".master: invalid form reference \"" + value.get<std::string>() + "\" (kept verbatim, ignored): " +
							FormRefError(value.get<std::string>()));
					} else if (key == "label" || key == "note" || key == "master") {
						a_diags.Error(a_source, a_where + "." + key + ": unexpected type " + detail::TypeName(value) + " (kept verbatim, ignored)");
					} else {
						a_diags.Warn(a_source, a_where + "." + key + ": unknown field (preserved)" + detail::DidYouMean(key, { "cell", "label", "master", "note" }));
					}
					entry.extra[key] = value;
				}
			}
			return entry;
		}
	}

	bool ParseHomeCells(const json& a_value, std::string_view a_source, std::vector<HomeCell>& a_out, json& a_unparsed, Diagnostics& a_diags)
	{
		const std::string source(a_source);
		if (!a_unparsed.is_array()) {
			a_unparsed = json::array();
		}
		if (a_value.is_string() || a_value.is_object()) {
			// A single entry where a list was expected (as the enum fields accept a scalar).
			if (auto entry = ParseEntry(a_value, source, "cells", a_diags)) {
				a_out.push_back(std::move(*entry));
			} else {
				a_unparsed.push_back(a_value);
			}
			return true;
		}
		if (!a_value.is_array()) {
			a_diags.Error(source, "cells: expected an array of cells, got " + detail::TypeName(a_value) + " (kept verbatim, ignored)");
			return false;
		}
		for (std::size_t i = 0; i < a_value.size(); ++i) {
			if (auto entry = ParseEntry(a_value[i], source, "cells[" + std::to_string(i) + "]", a_diags)) {
				a_out.push_back(std::move(*entry));
			} else {
				a_unparsed.push_back(a_value[i]);
			}
		}
		return true;
	}

	ojson HomeCellsToJson(std::span<const HomeCell> a_cells, const json& a_unparsed)
	{
		ojson out = ojson::array();
		for (const auto& c : a_cells) {
			const bool plain = c.label.empty() && !c.master && c.note.empty() && (!c.extra.is_object() || c.extra.empty());
			if (plain) {
				out.push_back(c.cell.ToString());
				continue;
			}
			ojson entry = ojson::object();
			entry["cell"] = c.cell.ToString();
			if (!c.label.empty()) {
				entry["label"] = c.label;
			}
			if (c.master) {
				entry["master"] = c.master->ToString();
			}
			if (!c.note.empty()) {
				entry["note"] = c.note;
			}
			if (c.extra.is_object()) {
				for (const auto& [key, value] : c.extra.items()) {
					if (!entry.contains(key)) {
						entry[key] = ToOrdered(value);
					}
				}
			}
			out.push_back(std::move(entry));
		}
		if (a_unparsed.is_array()) {
			for (const auto& u : a_unparsed) {
				out.push_back(ToOrdered(u));
			}
		}
		return out;
	}

	void ValidateHomeCells(const std::optional<FormRef>& a_main, std::span<const HomeCell> a_cells, std::string_view a_source, Diagnostics& a_diags,
		const std::optional<FormRef>& a_homeMaster)
	{
		const std::string source(a_source);
		for (std::size_t i = 0; i < a_cells.size(); ++i) {
			const auto& cell = a_cells[i].cell;
			const auto  where = "cells[" + std::to_string(i) + "]";
			if (a_main && *a_main == cell) {
				a_diags.Warn(source, where + ": " + cell.ToString() + " is the home's main cell (ignored)");
				continue;
			}
			const auto first = std::ranges::find_if(a_cells.begin(), a_cells.begin() + static_cast<std::ptrdiff_t>(i),
				[&](const HomeCell& c) { return c.cell == cell; });
			if (first != a_cells.begin() + static_cast<std::ptrdiff_t>(i)) {
				a_diags.Warn(source, where + ": duplicate cell " + cell.ToString() + "; the first entry is used");
			}
			if (a_homeMaster && a_cells[i].master && *a_cells[i].master == *a_homeMaster) {
				a_diags.Warn(source, where + ".master: " + a_homeMaster->ToString() + " is the home's master (ignored)");
			}
		}
	}

	std::vector<HomeCell> MergeHomeCells(std::span<const HomeCell> a_template, std::span<const HomeCell> a_player)
	{
		std::vector<HomeCell> out(a_template.begin(), a_template.end());
		for (const auto& old : a_player) {
			const auto it = std::ranges::find_if(out, [&](const HomeCell& c) { return c.cell == old.cell; });
			if (it == out.end()) {
				out.push_back(old);
				continue;
			}
			if (it->label.empty()) {
				it->label = old.label;
			}
			if (!it->master) {
				it->master = old.master;
			}
			if (it->note.empty()) {
				it->note = old.note;
			}
			if (it->extra.is_object() && old.extra.is_object()) {
				for (const auto& [key, value] : old.extra.items()) {
					if (!it->extra.contains(key)) {
						it->extra[key] = value;
					}
				}
			}
		}
		return out;
	}

	bool SetHomeCellLabel(std::vector<HomeCell>& a_cells, const FormRef& a_cell, std::string_view a_label)
	{
		const auto it = std::ranges::find(a_cells, a_cell, &HomeCell::cell);
		if (it == a_cells.end()) {
			return false;
		}
		const auto first = a_label.find_first_not_of(" \t\r\n");
		const auto last = a_label.find_last_not_of(" \t\r\n");
		it->label = first == std::string_view::npos ? std::string{} : std::string(a_label.substr(first, last - first + 1));
		return true;
	}

	std::vector<FormRef> AreaMasters(std::span<const HomeCell> a_cells, const std::optional<FormRef>& a_homeMaster)
	{
		std::vector<FormRef> out;
		for (const auto& c : a_cells) {
			if (c.master && (!a_homeMaster || *c.master != *a_homeMaster) && std::ranges::find(out, *c.master) == out.end()) {
				out.push_back(*c.master);
			}
		}
		return out;
	}

	std::vector<FormRef> HomeCellRefs(const FormRef& a_main, std::span<const HomeCell> a_cells)
	{
		std::vector<FormRef> out{ a_main };
		for (const auto& c : a_cells) {
			if (std::ranges::find(out, c.cell) == out.end()) {
				out.push_back(c.cell);
			}
		}
		return out;
	}

	std::vector<FormRef> EffectiveHomeCells(const FormRef& a_main, std::span<const HomeCell> a_cells, const std::function<bool(const FormRef&)>& a_hasOwnLayout,
		std::vector<FormRef>* a_excluded)
	{
		auto out = HomeCellRefs(a_main, a_cells);
		if (!a_hasOwnLayout) {
			return out;
		}
		std::vector<FormRef> kept{ out.front() };
		for (std::size_t i = 1; i < out.size(); ++i) {
			if (a_hasOwnLayout(out[i])) {
				if (a_excluded) {
					a_excluded->push_back(out[i]);
				}
			} else {
				kept.push_back(out[i]);
			}
		}
		return kept;
	}

	CellMembership ResolveCellMembership(const FormRef& a_cell, bool a_hasOwnLayout, std::span<const HomeCellsEntry> a_index)
	{
		CellMembership out;
		out.home = a_cell;
		if (a_hasOwnLayout) {
			return out;  // the player's own setup for this cell always wins
		}
		std::vector<FormRef> claims;
		for (const auto& entry : a_index) {
			if (entry.home == a_cell || std::ranges::find(claims, entry.home) != claims.end()) {
				continue;
			}
			if (std::ranges::find(entry.cells, a_cell) != entry.cells.end()) {
				claims.push_back(entry.home);
			}
		}
		if (claims.empty()) {
			return out;
		}
		std::ranges::sort(claims);
		out.kind = CellMembership::Kind::Extra;
		out.home = claims.front();
		out.otherClaims.assign(claims.begin() + 1, claims.end());
		return out;
	}

	Reach JudgeReach(const RefPlacement& a_place) noexcept
	{
		if (!a_place.inHome) {
			return Reach::Outside;
		}
		if (a_place.inPlayerCell) {
			return Reach::Here;
		}
		if (a_place.cellAttached) {
			return Reach::Loaded;
		}
		return a_place.persistent ? Reach::Remote : Reach::Unreachable;
	}

	bool IsReachable(Reach a_reach) noexcept
	{
		return a_reach == Reach::Here || a_reach == Reach::Loaded || a_reach == Reach::Remote;
	}

	std::string_view ReachName(Reach a_reach) noexcept
	{
		switch (a_reach) {
		case Reach::Here:
			return "here";
		case Reach::Loaded:
			return "loaded";
		case Reach::Remote:
			return "remote (persistent)";
		case Reach::Unreachable:
			return "unreachable (not persistent)";
		case Reach::Outside:
			return "outside the home";
		default:
			return "?";
		}
	}

	GridCell GridOf(float a_x, float a_y) noexcept
	{
		constexpr float kCellSize = 4096.0f;
		return GridCell{ static_cast<std::int32_t>(std::floor(a_x / kCellSize)), static_cast<std::int32_t>(std::floor(a_y / kCellSize)) };
	}

	std::optional<FormRef> CellFromLayoutFileName(std::string_view a_fileName)
	{
		constexpr std::string_view kExt = ".json";
		if (a_fileName.size() <= kExt.size() || !IEquals(a_fileName.substr(a_fileName.size() - kExt.size()), kExt)) {
			return std::nullopt;
		}
		const auto stem = a_fileName.substr(0, a_fileName.size() - kExt.size());
		const auto bar = stem.rfind('_');
		if (bar == std::string_view::npos || bar == 0 || stem.size() - bar - 1 != 8) {
			return std::nullopt;
		}
		const auto hex = stem.substr(bar + 1);
		if (!std::ranges::all_of(hex, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); })) {
			return std::nullopt;
		}
		return FormRef::Parse(std::string(stem.substr(0, bar)) + "|0x" + std::string(hex));
	}
}
