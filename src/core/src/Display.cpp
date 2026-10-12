#include "dova/core/Display.h"

#include <algorithm>
#include <array>
#include <map>
#include <string>

#include "dova/core/ArmorSets.h"
#include "dova/core/Category.h"

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
				for (std::size_t i = 0; i < _spots.size(); ++i) {  // chosen sets first: their pieces wait for them
					if (_spots[i].kind == DisplayKind::Mannequin && (_spots[i].chosen || !_spots[i].set.empty())) {
						FillMannequinSet(i);
					}
				}
				Pass(DisplayKind::Mannequin, [&](std::size_t i) {
					if (!_spots[i].chosen && _spots[i].set.empty()) {
						FillMannequin(i);
					}
				});
				for (const auto rack : { RackType::Shield, RackType::Dagger, RackType::NoDaggerOrBow, RackType::LargeCase, RackType::Any }) {
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

			// A rack or mannequin with categories takes only items in one of them.
			bool Allowed(const DisplaySpot& a_spot, std::size_t a_stock) const
			{
				if (a_spot.categories.empty()) {
					return true;
				}
				return std::ranges::any_of(a_spot.categories, [&](const std::string& p) {
					return std::ranges::any_of(_stocks[a_stock].categories, [&](const std::string& id) { return CategoryPatternMatches(p, id); });
				});
			}

			bool Free(std::size_t a_stock) const
			{
				return _left[a_stock] > 0 && !_used.contains(_stocks[a_stock].item.form) && !_reserved.contains(_stocks[a_stock].item.form);
			}

			// Free armor this mannequin may wear (`a_only`: pieces of its chosen set), best first.
			std::vector<std::size_t> MannequinCandidates(const DisplaySpot& a_spot, const std::vector<FormRef>* a_only) const
			{
				std::vector<std::size_t> out;
				for (std::size_t s = 0; s < _stocks.size(); ++s) {
					const auto& item = _stocks[s].item;
					if (Free(s) && MannequinTakes(item) && Allowed(a_spot, s) && (!a_only || std::ranges::find(*a_only, item.form) != a_only->end())) {
						out.push_back(s);
					}
				}
				std::ranges::stable_sort(out, [&](std::size_t a_lhs, std::size_t a_rhs) { return Better(a_spot, a_lhs, a_rhs); });
				return out;
			}

			// Candidate sets: the preferred items, if any are here; else the armor sets among the candidates
			// (GroupArmorPieces). The one with a body piece covering the most slot groups wins, then value.
			void FillMannequin(std::size_t a_spot)
			{
				const auto&             spot = _spots[a_spot];
				const auto              candidates = MannequinCandidates(spot, nullptr);
				std::vector<ArmorPiece> pieces;
				pieces.reserve(candidates.size());
				std::vector<std::size_t> preferred;
				for (std::size_t k = 0; k < candidates.size(); ++k) {
					pieces.push_back(ArmorPieceOf(_stocks[candidates[k]].item));
					if (Preferred(spot, _stocks[candidates[k]].item)) {
						preferred.push_back(k);
					}
				}
				// Members come in candidate order, so best first.
				const auto sets = preferred.empty() ? GroupArmorPieces(pieces) : std::vector<std::vector<std::size_t>>{ preferred };

				std::vector<std::size_t> bestPick;
				int                      bestGroups = -1;
				double                   bestValue = -1.0;
				for (const auto& members : sets) {
					auto pick = PickOutfit(pieces, members, 0, true);
					if (pick.empty()) {
						continue;  // no body piece: not a set
					}
					std::uint32_t slots = 0;
					double        value = 0.0;
					for (const auto k : pick) {
						slots |= pieces[k].slots;
						value += Score(_stocks[candidates[k]].item);
					}
					const int groups = SlotGroupCount(slots);
					if (groups > bestGroups || (groups == bestGroups && value > bestValue)) {
						bestGroups = groups;
						bestValue = value;
						bestPick = std::move(pick);
					}
				}
				for (const auto k : bestPick) {
					Take(a_spot, candidates[k]);
				}
			}

			// A chosen set: only its pieces, on the slots it leaves free; nothing while it wears anything else
			// (or what it wears is unknown).
			void FillMannequinSet(std::size_t a_spot)
			{
				const auto& spot = _spots[a_spot];
				const auto  inSet = [&](const FormRef& a_form) { return std::ranges::find(spot.set, a_form) != spot.set.end(); };
				if ((spot.full && spot.holds.empty()) || !std::ranges::all_of(spot.holds, inSet)) {
					return;
				}
				const auto              candidates = MannequinCandidates(spot, &spot.set);
				std::vector<ArmorPiece> pieces;
				std::vector<std::size_t> order;
				for (std::size_t k = 0; k < candidates.size(); ++k) {
					pieces.push_back(ArmorPieceOf(_stocks[candidates[k]].item));
					order.push_back(k);
				}
				for (const auto k : PickOutfit(pieces, order, spot.worn, false)) {
					Take(a_spot, candidates[k]);
				}
			}

			void FillRack(std::size_t a_spot)
			{
				const auto&                spot = _spots[a_spot];
				std::optional<std::size_t> best;
				for (std::size_t s = 0; s < _stocks.size(); ++s) {
					if (!Free(s) || !RackTakes(spot.rack, _stocks[s].item, spot.crossbows) || !Allowed(spot, s) ||
						std::ranges::find(spot.refuses, _stocks[s].item.form) != spot.refuses.end()) {
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
		if (a_item.type != ItemType::Weapon) {
			return {};
		}
		// WeaponRackActivateSCRIPT.PlaceItem goes by keyword, in this order.
		static constexpr std::array<std::pair<std::string_view, std::string_view>, 9> kByKeyword{ {
			{ "WeapTypeSword", "SwordPivot01" },
			{ "WeapTypeWarAxe", "WarAxePivot01" },
			{ "WeapTypeMace", "MacePivot01" },
			{ "WeapTypeGreatsword", "GreatSwordPivot01" },
			{ "WeapTypeBattleaxe", "BattleAxePivot01" },
			{ "WeapTypeWarhammer", "WarhammerPivot01" },
			{ "WeapTypeBow", "BowPivot01" },
			{ "WeapTypeStaff", "StaffPivot01" },
			{ "WeapTypeDagger", "DaggerPivot01" },
		} };
		for (const auto& [keyword, pivot] : kByKeyword) {
			if (HasKeyword(a_item, keyword)) {
				return pivot;
			}
		}
		if (!a_item.weaponType) {
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
			return "BattleAxePivot01";  // warhammers carry WeapTypeWarhammer
		case WeaponType::Bow:
		case WeaponType::Crossbow:
			return "BowPivot01";
		case WeaponType::Staff:
			return "StaffPivot01";
		default:
			return {};  // hand-to-hand: no rack node holds it
		}
	}

	bool RackTakes(RackType a_rack, const ItemInfo& a_item, bool a_crossbows)
	{
		const auto pivot = RackPivot(a_item);
		if (pivot.empty()) {
			return false;
		}
		// Crossbows carry WeapTypeBow; only the Unofficial Patch's racks have a node made for them.
		if (a_item.weaponType == WeaponType::Crossbow && !a_crossbows) {
			return false;
		}
		const bool shield = IsShield(a_item);
		const bool dagger = !shield && pivot == "DaggerPivot01";
		const bool bow = !shield && pivot == "BowPivot01";
		switch (a_rack) {
		case RackType::Any:
		case RackType::LargeCase:
			return !shield;
		case RackType::Shield:
			return shield;
		case RackType::NoDaggerOrBow:
			return !shield && !dagger && !bow;
		case RackType::Dagger:
			return dagger;
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
