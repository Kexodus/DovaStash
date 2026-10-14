#include "dova/core/DisplayCategories.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace dova::core
{
	namespace
	{
		// What a display holds, as bits.
		enum Holds : std::uint16_t
		{
			kWeapon = 1 << 0,    // a weapon a rack takes, other than a dagger, bow or crossbow
			kDagger = 1 << 1,
			kBow = 1 << 2,
			kCrossbow = 1 << 3,
			kShield = 1 << 4,
			kWorn = 1 << 5,      // armor a mannequin wears
			kBook = 1 << 6,
		};
		constexpr std::uint16_t kAnyWeapon = kWeapon | kDagger | kBow | kCrossbow;

		std::uint16_t Takes(const DisplayFit& a_fit)
		{
			switch (a_fit.kind) {
			case DisplayKind::Rack:
				{
					const std::uint16_t any = kWeapon | kDagger | kBow | (a_fit.crossbows ? kCrossbow : 0);
					if (!a_fit.rack) {
						return any | kShield;
					}
					switch (*a_fit.rack) {
					case RackType::Any:
					case RackType::LargeCase:
						return any;
					case RackType::Shield:
						return kShield;
					case RackType::NoDaggerOrBow:
						return kWeapon;
					case RackType::Dagger:
						return kDagger;
					default:
						return 0;
					}
				}
			case DisplayKind::Mannequin:
				return kWorn;
			case DisplayKind::Bookshelf:
				return kBook;
			default:
				return 0;  // item plaques and cases: made for their own items
			}
		}

		bool Has(const std::optional<std::vector<ItemType>>& a_types, ItemType a_type)
		{
			return !a_types || std::ranges::find(*a_types, a_type) != a_types->end();
		}

		// WeaponRackActivateSCRIPT's keywords: what each one puts a weapon on.
		std::optional<std::uint16_t> WeapTypeKeyword(std::string_view a_keyword)
		{
			static constexpr std::array<std::string_view, 7> kOther{ "WeapTypeSword", "WeapTypeWarAxe", "WeapTypeMace", "WeapTypeGreatsword",
				"WeapTypeBattleaxe", "WeapTypeWarhammer", "WeapTypeStaff" };
			if (IEquals(a_keyword, "WeapTypeDagger")) {
				return kDagger;
			}
			if (IEquals(a_keyword, "WeapTypeBow")) {
				return static_cast<std::uint16_t>(kBow | kCrossbow);  // crossbows carry WeapTypeBow
			}
			if (std::ranges::any_of(kOther, [&](std::string_view k) { return IEquals(a_keyword, k); })) {
				return kWeapon;
			}
			return std::nullopt;
		}

		std::uint16_t WeaponHolds(const CategoryMatch& a_m)
		{
			std::uint16_t out = kAnyWeapon;
			if (a_m.weaponTypes) {
				out = 0;
				for (const auto t : *a_m.weaponTypes) {
					switch (t) {
					case WeaponType::OneHandDagger:
						out |= kDagger;
						break;
					case WeaponType::Bow:
						out |= kBow;
						break;
					case WeaponType::Crossbow:
						out |= kCrossbow;
						break;
					case WeaponType::HandToHand:
						break;  // no rack node holds it
					default:
						out |= kWeapon;
						break;
					}
				}
			}
			// keywordsAny made only of WeapType* keywords narrows it; one unknown keyword could be on any weapon.
			if (a_m.keywordsAny && !a_m.keywordsAny->empty()) {
				std::uint16_t any = 0;
				bool          known = true;
				for (const auto& k : *a_m.keywordsAny) {
					if (const auto bits = WeapTypeKeyword(k)) {
						any |= *bits;
					} else {
						known = false;
					}
				}
				if (known) {
					out &= any;
				}
			}
			if (a_m.keywordsAll) {
				for (const auto& k : *a_m.keywordsAll) {
					if (const auto bits = WeapTypeKeyword(k)) {
						out &= *bits;
					}
				}
			}
			return out;
		}

		bool ShieldKeyword(std::string_view a_keyword)
		{
			return ToLower(a_keyword).find("shield") != std::string::npos;  // ArmorShield, OCF_ArmorShield_Heavy, OCF_EquipTypeShield
		}

		// Keywords of armor pieces that are no shield (the game's and OCF's slot keywords).
		bool PieceKeyword(std::string_view a_keyword)
		{
			static constexpr std::array<std::string_view, 24> kPieces{ "ArmorHelmet", "ArmorCuirass", "ArmorGauntlets", "ArmorBoots", "ArmorJewelry",
				"ClothingHead", "ClothingBody", "ClothingHands", "ClothingFeet", "ClothingRing", "ClothingNecklace", "ClothingCirclet",
				"VendorItemJewelry", "VendorItemClothing", "OCF_ArmorTypeHead_Main", "OCF_ArmorTypeBody_Main", "OCF_ArmorTypeHands_Main",
				"OCF_ArmorTypeFeet_Main", "OCF_HandTypeRing", "OCF_BodyTypeAmulet", "OCF_BodyTypeTorc", "OCF_BodyTypeHighNecklace",
				"OCF_HeadTypeCirclet", "OCF_HeadTypeCrown" };
			return std::ranges::any_of(kPieces, [&](std::string_view k) { return IEquals(a_keyword, k); });
		}

		// Keywords of armor no mannequin wears: shields, rings, necklaces (MannequinTakes: no slot it shows).
		bool UnwornKeyword(std::string_view a_keyword)
		{
			static constexpr std::array<std::string_view, 6> kUnworn{ "ClothingRing", "ClothingNecklace", "OCF_HandTypeRing", "OCF_BodyTypeAmulet",
				"OCF_BodyTypeTorc", "OCF_BodyTypeHighNecklace" };
			return ShieldKeyword(a_keyword) || std::ranges::any_of(kUnworn, [&](std::string_view k) { return IEquals(a_keyword, k); });
		}

		std::uint16_t ArmorHolds(const CategoryMatch& a_m)
		{
			bool shield = true;
			bool worn = true;
			if (a_m.armorTypes && std::ranges::none_of(*a_m.armorTypes, [](ArmorType t) { return t == ArmorType::Heavy || t == ArmorType::Light; })) {
				shield = false;  // shields are heavy or light armor
			}
			if (a_m.keywordsAll) {
				for (const auto& k : *a_m.keywordsAll) {
					shield = shield && !PieceKeyword(k);
					worn = worn && !UnwornKeyword(k);
				}
			}
			if (a_m.keywordsNone && std::ranges::any_of(*a_m.keywordsNone, [](const std::string& k) { return IEquals(k, "ArmorShield"); })) {
				shield = false;
			}
			if (a_m.keywordsAny) {
				// One of them: a shield when one of them could be on a shield; worn when one could be on worn armor.
				shield = shield && std::ranges::any_of(*a_m.keywordsAny, [](const std::string& k) { return ShieldKeyword(k) || !PieceKeyword(k); });
				worn = worn && std::ranges::any_of(*a_m.keywordsAny, [](const std::string& k) { return !UnwornKeyword(k); });
			}
			return static_cast<std::uint16_t>((shield ? kShield : 0) | (worn ? kWorn : 0));
		}

		// What items a display holds a category can match.
		std::uint16_t CanMatch(const CategoryMatch& a_m)
		{
			if (a_m.Empty()) {
				return 0;  // a grouping category matches nothing itself
			}
			bool weapon = Has(a_m.formTypes, ItemType::Weapon);
			bool armor = Has(a_m.formTypes, ItemType::Armor);
			bool book = Has(a_m.formTypes, ItemType::Book);
			if (a_m.weaponTypes) {
				armor = book = false;
			}
			if (a_m.armorTypes) {
				weapon = book = false;
			}
			if (a_m.bookTeaches) {
				weapon = armor = false;
			}
			if (a_m.alchemy || a_m.soulGem || a_m.ammo) {
				weapon = armor = book = false;  // potions, food, soul gems, ammo: on no display
			}
			std::uint16_t out = 0;
			if (weapon) {
				out |= WeaponHolds(a_m);
			}
			if (armor) {
				out |= ArmorHolds(a_m);
			}
			if (book) {
				out |= kBook;
			}
			return out;
		}

		std::string_view RootOf(std::string_view a_id)
		{
			return a_id.substr(0, a_id.find('.'));
		}

		std::string Where(const DisplayBinding& a_d)
		{
			return "display " + a_d.ref.ToString() + (a_d.label.empty() ? std::string() : " (" + a_d.label + ")");
		}
	}

	bool CategoryFitsDisplay(const CategoryMatch& a_match, const DisplayFit& a_fit)
	{
		return (CanMatch(a_match) & Takes(a_fit)) != 0;
	}

	std::optional<bool> PatternFitsDisplay(const CategorySet& a_categories, std::string_view a_pattern, const DisplayFit& a_fit)
	{
		bool known = false;
		for (const auto& c : a_categories.Categories()) {
			if (c.match.Empty() || !CategoryPatternMatches(a_pattern, c.id)) {
				continue;
			}
			known = true;
			if (CategoryFitsDisplay(c.match, a_fit)) {
				return true;
			}
		}
		return known ? std::optional(false) : std::nullopt;
	}

	std::vector<std::string> DisplayCategoryChoices(const CategorySet& a_categories, const DisplayFit& a_fit)
	{
		std::vector<std::string> out;
		if (Takes(a_fit) == 0) {
			return out;
		}
		std::vector<std::string_view> roots;
		for (const auto& c : a_categories.Categories()) {
			if (const auto root = RootOf(c.id); std::ranges::find(roots, root) == roots.end()) {
				roots.push_back(root);
			}
		}
		const auto fits = [&](const std::string& a_pattern) { return PatternFitsDisplay(a_categories, a_pattern, a_fit) == true; };
		for (const auto root : roots) {
			if (auto all = std::string(root) + ".*"; fits(all)) {
				out.push_back(std::move(all));
			}
			for (const auto& c : a_categories.Categories()) {
				if (c.id == root || RootOf(c.id) != root) {
					continue;
				}
				if (auto choice = c.match.Empty() ? c.id + ".*" : c.id; fits(choice)) {
					out.push_back(std::move(choice));
				}
			}
		}
		return out;
	}

	std::vector<std::string> UnfitDisplayCategories(const CategorySet& a_categories, std::span<const std::string> a_patterns, const DisplayFit& a_fit)
	{
		std::vector<std::string> out;
		for (const auto& p : a_patterns) {
			if (PatternFitsDisplay(a_categories, p, a_fit) == false) {
				out.push_back(p);
			}
		}
		return out;
	}

	std::string DisplayFitNoun(const DisplayFit& a_fit)
	{
		switch (a_fit.kind) {
		case DisplayKind::Rack:
			if (!a_fit.rack) {
				return "rack";
			}
			switch (*a_fit.rack) {
			case RackType::Shield:
				return "shield rack";
			case RackType::Dagger:
				return "dagger rack";
			case RackType::NoDaggerOrBow:
				return "rack without daggers or bows";
			case RackType::LargeCase:
				return "weapon case";
			default:
				return "weapon rack";
			}
		case DisplayKind::Mannequin:
			return "mannequin";
		case DisplayKind::Bookshelf:
			return "bookshelf";
		case DisplayKind::Item:
			return "item plaque";
		case DisplayKind::Case:
			return "display case";
		default:
			return "display";
		}
	}

	std::string UnfitCategoryMessage(const CategorySet& a_categories, std::string_view a_pattern, const DisplayFit& a_fit)
	{
		std::string name(a_pattern);
		if (a_pattern == "*") {
			name = "Everything";
		} else {
			const auto id = a_pattern.ends_with(".*") ? a_pattern.substr(0, a_pattern.size() - 2) : a_pattern;
			if (const auto* c = a_categories.Find(ToLower(id))) {
				name = c->name;
			}
		}
		return name + " can't go on a " + DisplayFitNoun(a_fit);
	}

	void ValidateDisplayCategories(const Layout& a_layout, const CategorySet& a_categories, const DisplayFitOf& a_fitOf, std::string_view a_source,
		Diagnostics& a_diags)
	{
		const std::string source(a_source);
		for (const auto& d : a_layout.displays) {
			if (d.ignored || d.categories.empty()) {
				continue;
			}
			auto fit = a_fitOf ? a_fitOf(d) : std::nullopt;
			if (!fit && d.kind) {
				fit = DisplayFit{ *d.kind, std::nullopt, true };
			}
			if (!fit) {
				continue;
			}
			if (fit->kind == DisplayKind::Item || fit->kind == DisplayKind::Case) {
				if (!d.kind) {
					a_diags.Warn(source, Where(d) + ": \"categories\" are only used by racks, mannequins and bookshelves (ignored)");
				}
				continue;
			}
			const auto unfit = UnfitDisplayCategories(a_categories, d.categories, *fit);
			const bool none = unfit.size() == d.categories.size();
			for (const auto& p : unfit) {
				a_diags.Warn(source, Where(d) + ": " + UnfitCategoryMessage(a_categories, p, *fit) + (none && &p == &unfit.back() ? "; nothing goes on it" : ""));
			}
		}
	}
}
