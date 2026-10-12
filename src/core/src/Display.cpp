#include "dova/core/Display.h"

#include <algorithm>
#include <array>
#include <map>
#include <string>

namespace dova::core
{
	namespace
	{
		// Biped slot bits (bit 0 = slot 30).
		constexpr std::uint32_t kSlotBody = 1u << 2;     // 32
		constexpr std::uint32_t kSlotShield = 1u << 9;   // 39
		// Head, hair, body, hands, forearms, feet, calves, circlet: what a mannequin shows.
		constexpr std::uint32_t kMannequinSlots = (1u << 0) | (1u << 1) | kSlotBody | (1u << 3) | (1u << 4) | (1u << 7) | (1u << 8) | (1u << 12);

		bool HasKeyword(const ItemInfo& a_item, std::string_view a_keyword)
		{
			return std::ranges::any_of(a_item.keywords, [&](const std::string& k) { return IEquals(k, a_keyword); });
		}

		bool IsShield(const ItemInfo& a_item)
		{
			return a_item.type == ItemType::Armor && (HasKeyword(a_item, "ArmorShield") || (a_item.armorSlots & kSlotShield) != 0);
		}

		// The set a mannequin shows: the item's material keyword, or the item itself.
		std::string MaterialKey(const ItemInfo& a_item)
		{
			for (const auto& k : a_item.keywords) {
				if (ToLower(k).find("armormaterial") != std::string::npos) {
					return ToLower(k);
				}
			}
			return "form:" + a_item.form.ToString();
		}

		double Score(const ItemInfo& a_item)
		{
			return static_cast<double>(a_item.value) + (HasKeyword(a_item, "DaedricArtifact") ? 1.0e7 : 0.0);
		}

		class Planner
		{
		public:
			Planner(std::span<const DisplaySpot> a_spots, std::span<const DisplayStock> a_stocks, const std::set<FormRef>& a_shown) :
				_spots(a_spots),
				_stocks(a_stocks),
				_used(a_shown)
			{
				_left.reserve(a_stocks.size());
				for (const auto& s : a_stocks) {
					_left.push_back(s.item.form.IsValid() && SortPlanner::CanMove(s.item) ? std::min(s.available, s.item.count) : 0);
				}
				for (const auto& spot : a_spots) {
					if (spot.kind == DisplayKind::Item || spot.kind == DisplayKind::Case) {
						_reserved.insert(spot.items.begin(), spot.items.end());
					}
				}
			}

			std::vector<DisplayAssignment> Run()
			{
				Pass(DisplayKind::Item, [&](std::size_t i) { FillItem(i); });
				Pass(DisplayKind::Case, [&](std::size_t i) { FillCase(i); });
				Pass(DisplayKind::Mannequin, [&](std::size_t i) { FillMannequin(i); });
				for (const auto rack : { RackType::Shield, RackType::Dagger, RackType::NoDaggerOrBow, RackType::Any }) {
					for (std::size_t i = 0; i < _spots.size(); ++i) {
						if (_spots[i].kind == DisplayKind::Rack && _spots[i].rack == rack && !_spots[i].full) {
							FillRack(i);
						}
					}
				}
				return std::move(_out);
			}

		private:
			template <class F>
			void Pass(DisplayKind a_kind, F&& a_fill)
			{
				for (std::size_t i = 0; i < _spots.size(); ++i) {
					if (_spots[i].kind == a_kind && (a_kind == DisplayKind::Case || !_spots[i].full)) {
						a_fill(i);
					}
				}
			}

			static bool Preferred(const DisplaySpot& a_spot, const ItemInfo& a_item)
			{
				return std::ranges::find(a_spot.items, a_item.form) != a_spot.items.end();
			}

			// Better candidate for a display: preferred, artifact / value, the player's copy, input order.
			bool Better(const DisplaySpot& a_spot, std::size_t a_lhs, std::size_t a_rhs) const
			{
				const auto& l = _stocks[a_lhs];
				const auto& r = _stocks[a_rhs];
				if (const bool lp = Preferred(a_spot, l.item), rp = Preferred(a_spot, r.item); lp != rp) {
					return lp;
				}
				if (const auto ls = Score(l.item), rs = Score(r.item); ls != rs) {
					return ls > rs;
				}
				if (l.from.IsPlayer() != r.from.IsPlayer()) {
					return l.from.IsPlayer();
				}
				return a_lhs < a_rhs;
			}

			void Take(std::size_t a_spot, std::size_t a_stock)
			{
				--_left[a_stock];
				_used.insert(_stocks[a_stock].item.form);
				_out.push_back(DisplayAssignment{ a_spot, a_stock, 1 });
			}

			void FillItem(std::size_t a_spot)
			{
				const auto&                spot = _spots[a_spot];
				std::optional<std::size_t> best;
				for (std::size_t s = 0; s < _stocks.size(); ++s) {
					const auto& item = _stocks[s].item;
					if (_left[s] <= 0 || item.customized || !Preferred(spot, item)) {
						continue;
					}
					if (!best || Better(spot, s, *best)) {
						best = s;
					}
				}
				if (best) {
					Take(a_spot, *best);
				}
			}

			void FillCase(std::size_t a_spot)
			{
				const auto& spot = _spots[a_spot];
				for (const auto& form : spot.items) {
					if (_used.contains(form) || std::ranges::find(spot.holds, form) != spot.holds.end()) {
						continue;
					}
					std::optional<std::size_t> best;
					for (std::size_t s = 0; s < _stocks.size(); ++s) {
						if (_left[s] > 0 && _stocks[s].item.form == form && (!best || Better(spot, s, *best))) {
							best = s;
						}
					}
					if (best) {
						Take(a_spot, *best);
					}
				}
			}

			bool Free(std::size_t a_stock) const
			{
				return _left[a_stock] > 0 && !_used.contains(_stocks[a_stock].item.form) && !_reserved.contains(_stocks[a_stock].item.form);
			}

			void FillMannequin(std::size_t a_spot)
			{
				const auto& spot = _spots[a_spot];
				// Candidate sets: preferred items, if any are here; else one per material.
				std::map<std::string, std::vector<std::size_t>> sets;
				for (std::size_t s = 0; s < _stocks.size(); ++s) {
					if (Free(s) && MannequinTakes(_stocks[s].item)) {
						sets[Preferred(spot, _stocks[s].item) ? std::string("\x01preferred") : MaterialKey(_stocks[s].item)].push_back(s);
					}
				}
				if (const auto it = sets.find("\x01preferred"); it != sets.end()) {
					auto preferred = std::move(it->second);
					sets.clear();
					sets.emplace("\x01preferred", std::move(preferred));
				}

				std::vector<std::size_t> bestPick;
				double                   bestValue = -1.0;
				for (auto& [key, members] : sets) {
					std::ranges::stable_sort(members, [&](std::size_t a_lhs, std::size_t a_rhs) { return Better(spot, a_lhs, a_rhs); });
					std::vector<std::size_t> pick;
					std::uint32_t            slots = 0;
					std::set<FormRef>        forms;
					const auto               add = [&](std::size_t a_s) {
						const auto& item = _stocks[a_s].item;
						if ((slots & item.armorSlots) != 0 || forms.contains(item.form)) {
							return;
						}
						slots |= item.armorSlots;
						forms.insert(item.form);
						pick.push_back(a_s);
					};
					for (const auto s : members) {
						if ((_stocks[s].item.armorSlots & kSlotBody) != 0) {
							add(s);
							break;
						}
					}
					if (pick.empty()) {
						continue;  // no body piece: not a set
					}
					for (const auto s : members) {
						add(s);
					}
					double value = 0.0;
					for (const auto s : pick) {
						value += Score(_stocks[s].item);
					}
					if (value > bestValue) {
						bestValue = value;
						bestPick = std::move(pick);
					}
				}
				for (const auto s : bestPick) {
					Take(a_spot, s);
				}
			}

			void FillRack(std::size_t a_spot)
			{
				const auto&                spot = _spots[a_spot];
				std::optional<std::size_t> best;
				for (std::size_t s = 0; s < _stocks.size(); ++s) {
					if (!Free(s) || !RackTakes(spot.rack, _stocks[s].item)) {
						continue;
					}
					if (!best || Better(spot, s, *best)) {
						best = s;
					}
				}
				if (best) {
					Take(a_spot, *best);
				}
			}

			std::span<const DisplaySpot>   _spots;
			std::span<const DisplayStock>  _stocks;
			std::vector<std::int32_t>      _left;
			std::set<FormRef>              _used;
			std::set<FormRef>              _reserved;
			std::vector<DisplayAssignment> _out;
		};
	}

	std::string_view DisplayKindName(DisplayKind a_kind) noexcept
	{
		switch (a_kind) {
		case DisplayKind::Rack:
			return "rack";
		case DisplayKind::Mannequin:
			return "mannequin";
		case DisplayKind::Item:
			return "item";
		case DisplayKind::Case:
			return "case";
		default:
			return "?";
		}
	}

	std::optional<DisplayKind> ParseDisplayKind(std::string_view a_text)
	{
		for (const auto kind : { DisplayKind::Rack, DisplayKind::Mannequin, DisplayKind::Item, DisplayKind::Case }) {
			if (IEquals(a_text, DisplayKindName(kind))) {
				return kind;
			}
		}
		return std::nullopt;
	}

	std::string_view RackPivot(const ItemInfo& a_item)
	{
		if (IsShield(a_item)) {
			return "ShieldPivot01";
		}
		if (a_item.type != ItemType::Weapon || !a_item.weaponType) {
			return {};
		}
		switch (*a_item.weaponType) {
		case WeaponType::OneHandSword:
			return "SwordPivot01";
		case WeaponType::OneHandDagger:
			return "DaggerPivot01";
		case WeaponType::OneHandAxe:
			return "WarAxePivot01";
		case WeaponType::OneHandMace:
			return "MacePivot01";
		case WeaponType::TwoHandSword:
			return "GreatSwordPivot01";
		case WeaponType::TwoHandAxe:
			return HasKeyword(a_item, "WeaponTypeWarhammer") ? "WarhammerPivot01" : "BattleAxePivot01";
		case WeaponType::Bow:
			return "BowPivot01";
		case WeaponType::Staff:
			return "StaffPivot01";
		default:
			return {};  // hand-to-hand, crossbows: no rack node holds them
		}
	}

	bool RackTakes(RackType a_rack, const ItemInfo& a_item)
	{
		if (RackPivot(a_item).empty()) {
			return false;
		}
		const bool shield = IsShield(a_item);
		const auto weapon = a_item.weaponType.value_or(WeaponType::HandToHand);
		switch (a_rack) {
		case RackType::Any:
			return !shield;
		case RackType::Shield:
			return shield;
		case RackType::NoDaggerOrBow:
			return !shield && weapon != WeaponType::OneHandDagger && weapon != WeaponType::Bow;
		case RackType::Dagger:
			return !shield && weapon == WeaponType::OneHandDagger;
		default:
			return false;
		}
	}

	bool MannequinTakes(const ItemInfo& a_item)
	{
		return a_item.type == ItemType::Armor && !IsShield(a_item) && (a_item.armorSlots & kMannequinSlots) != 0;
	}

	std::vector<DisplayAssignment> PlanDisplays(std::span<const DisplaySpot> a_spots, std::span<const DisplayStock> a_stocks, const std::set<FormRef>& a_shown)
	{
		return Planner(a_spots, a_stocks, a_shown).Run();
	}

	void ReserveForDisplays(std::vector<Move>& a_moves, std::span<const DisplayStock> a_stocks, std::span<const DisplayAssignment> a_assignments)
	{
		for (const auto& a : a_assignments) {
			if (a.stock >= a_stocks.size()) {
				continue;
			}
			const auto& stock = a_stocks[a.stock];
			const auto  it = std::ranges::find_if(a_moves, [&](const Move& m) { return m.stackId == stock.item.stackId && m.from == stock.from; });
			if (it != a_moves.end()) {
				it->count -= a.count;
			}
		}
		std::erase_if(a_moves, [](const Move& m) { return m.count <= 0; });
	}
}
