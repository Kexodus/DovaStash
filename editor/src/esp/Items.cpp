#include "esp/Items.h"

#include <algorithm>

namespace dova::editor::esp
{
	namespace
	{
		std::optional<core::ItemType> TypeOf(Sig a_sig)
		{
			switch (a_sig) {
			case MakeSig("WEAP"):
				return core::ItemType::Weapon;
			case MakeSig("ARMO"):
				return core::ItemType::Armor;
			case MakeSig("AMMO"):
				return core::ItemType::Ammo;
			case MakeSig("ALCH"):
				return core::ItemType::Alchemy;
			case MakeSig("INGR"):
				return core::ItemType::Ingredient;
			case MakeSig("BOOK"):
				return core::ItemType::Book;
			case MakeSig("SCRL"):
				return core::ItemType::Scroll;
			case MakeSig("SLGM"):
				return core::ItemType::SoulGem;
			case MakeSig("MISC"):
				return core::ItemType::Misc;
			case MakeSig("KEYM"):
				return core::ItemType::Key;
			case MakeSig("LIGH"):
				return core::ItemType::Light;
			default:
				return std::nullopt;
			}
		}

		constexpr Sig kItemTypes[] = { MakeSig("WEAP"), MakeSig("ARMO"), MakeSig("AMMO"), MakeSig("ALCH"), MakeSig("INGR"), MakeSig("BOOK"), MakeSig("SCRL"),
			MakeSig("SLGM"), MakeSig("MISC"), MakeSig("KEYM"), MakeSig("LIGH") };

		// Value and weight: the first two fields of DATA for most types.
		void ValueWeight(const Subrecords& a_subs, core::ItemInfo& a_info, std::size_t a_valueAt = 0, std::size_t a_weightAt = 4)
		{
			if (const auto d = a_subs.Get(MakeSig("DATA")); d && d->size() >= a_weightAt + 4) {
				a_info.value = Read<std::int32_t>(*d, a_valueAt);
				a_info.weight = Read<float>(*d, a_weightAt);
			}
		}
	}

	std::optional<core::ItemInfo> ReadItem(LoadOrder& a_lo, const FormKey& a_key, const RecordRef& a_rec)
	{
		const auto type = TypeOf(a_rec.type);
		if (!type) {
			return std::nullopt;
		}
		const auto     subs = a_lo.Read(a_rec);
		core::ItemInfo info;
		info.form = a_lo.ToFormRef(a_key);
		info.type = *type;
		info.count = 1;
		info.name = a_lo.FullName(a_rec, subs);
		if (const auto kwda = subs.Get(MakeSig("KWDA"))) {
			info.keywords = a_lo.KeywordIds(a_rec.plugin, *kwda);
		}
		bool playable = true;
		switch (*type) {
		case core::ItemType::Weapon:
			ValueWeight(subs, info);
			if (const auto d = subs.Get(MakeSig("DNAM")); d && !d->empty()) {
				const auto anim = static_cast<std::uint8_t>((*d)[0]);
				if (anim <= static_cast<std::uint8_t>(core::WeaponType::Crossbow)) {
					info.weaponType = static_cast<core::WeaponType>(anim);
				}
				if (d->size() >= 14 && (Read<std::uint16_t>(*d, 12) & 0x80) != 0) {
					playable = false;  // DNAM flag: non-playable
				}
			}
			info.enchanted = subs.Get(MakeSig("EITM")).has_value();
			break;
		case core::ItemType::Armor:
			ValueWeight(subs, info);
			if (const auto b = subs.Get(MakeSig("BOD2")); b && b->size() >= 8) {
				info.armorSlots = Read<std::uint32_t>(*b, 0);
				const auto t = Read<std::uint32_t>(*b, 4);
				info.armorType = t == 1 ? core::ArmorType::Heavy : t == 0 ? core::ArmorType::Light : core::ArmorType::Clothing;
			} else if (const auto o = subs.Get(MakeSig("BODT")); o && o->size() >= 12) {
				info.armorSlots = Read<std::uint32_t>(*o, 0);
				const auto t = Read<std::uint32_t>(*o, 8);
				info.armorType = t == 1 ? core::ArmorType::Heavy : t == 0 ? core::ArmorType::Light : core::ArmorType::Clothing;
			} else {
				info.armorType = core::ArmorType::Clothing;
			}
			if ((a_rec.flags & 0x4) != 0) {
				playable = false;  // record flag: non-playable
			}
			info.enchanted = subs.Get(MakeSig("EITM")).has_value();
			break;
		case core::ItemType::Ammo:
			if (const auto d = subs.Get(MakeSig("DATA")); d && d->size() >= 16) {
				const auto flags = Read<std::uint32_t>(*d, 4);
				info.typeFlags = flags;
				info.ammoKind = (flags & 0x4) != 0 ? core::AmmoKind::Arrow : core::AmmoKind::Bolt;
				playable = (flags & 0x2) == 0;
				info.value = Read<std::int32_t>(*d, 12);
				if (d->size() >= 20) {
					info.weight = Read<float>(*d, 16);
				}
			}
			break;
		case core::ItemType::Alchemy:
			if (const auto d = subs.Get(MakeSig("DATA")); d && d->size() >= 4) {
				info.weight = Read<float>(*d, 0);
			}
			if (const auto e = subs.Get(MakeSig("ENIT")); e && e->size() >= 8) {
				info.value = Read<std::int32_t>(*e, 0);
				const auto flags = Read<std::uint32_t>(*e, 4);
				info.typeFlags = flags;
				info.alchemyKind = (flags & 0x20000) != 0 ? core::AlchemyKind::Poison : (flags & 0x2) != 0 ? core::AlchemyKind::Food : core::AlchemyKind::Potion;
			} else {
				info.alchemyKind = core::AlchemyKind::Potion;
			}
			break;
		case core::ItemType::Book:
			if (const auto d = subs.Get(MakeSig("DATA")); d && d->size() >= 16) {
				const auto flags = static_cast<std::uint8_t>((*d)[0]);
				const auto kind = static_cast<std::uint8_t>((*d)[1]);
				info.typeFlags = flags;
				info.bookTeaches = (flags & 0x4) != 0 ? core::BookTeaches::Spell : (flags & 0x1) != 0 ? core::BookTeaches::Skill : core::BookTeaches::None;
				info.bookIsNote = kind == 0xFF;
				info.value = Read<std::int32_t>(*d, 8);
				info.weight = Read<float>(*d, 12);
			} else {
				info.bookTeaches = core::BookTeaches::None;
			}
			break;
		case core::ItemType::SoulGem:
			ValueWeight(subs, info);
			{
				const auto soul = subs.Get(MakeSig("SOUL"));
				const auto cap = subs.Get(MakeSig("SLCP"));
				info.soulSize = soul && !soul->empty() ? static_cast<std::uint8_t>((*soul)[0]) : std::uint8_t{ 0 };
				info.gemSize = cap && !cap->empty() ? static_cast<std::uint8_t>((*cap)[0]) : std::uint8_t{ 0 };
				info.soulGemState = *info.soulSize != 0 ? core::SoulGemState::Filled : core::SoulGemState::Empty;
			}
			break;
		case core::ItemType::Light:
			if (const auto d = subs.Get(MakeSig("DATA")); d && d->size() >= 48) {
				if ((Read<std::uint32_t>(*d, 12) & 0x2) == 0) {
					return std::nullopt;  // cannot be carried: a lamp, not a torch
				}
				info.value = Read<std::int32_t>(*d, 40);
				info.weight = Read<float>(*d, 44);
			} else {
				return std::nullopt;
			}
			break;
		default:
			ValueWeight(subs, info);
			break;
		}
		if (info.name.empty()) {
			playable = false;  // script tokens: never moved (as the DLL decides)
		}
		info.playable = playable;
		return info;
	}

	ItemCatalog BuildItemCatalog(LoadOrder& a_lo)
	{
		ItemCatalog cat;
		std::vector<std::pair<FormKey, core::ItemInfo>> all;
		for (const auto sig : kItemTypes) {
			a_lo.ForEachRecord(sig, [&](const FormKey& a_key, const RecordRef& a_rec) {
				if (a_rec.flags & kRecDeleted) {
					return;
				}
				auto item = ReadItem(a_lo, a_key, a_rec);
				if (!item) {
					return;
				}
				if (!item->playable) {
					++cat.skipped;
					return;
				}
				all.emplace_back(a_key, std::move(*item));
			});
		}
		std::ranges::sort(all, [](const auto& a, const auto& b) {
			if (a.second.type != b.second.type) {
				return a.second.type < b.second.type;
			}
			if (const int c = core::ICompare(a.second.name, b.second.name); c != 0) {
				return c < 0;
			}
			return a.second.form < b.second.form;
		});
		cat.items.reserve(all.size());
		for (auto& [key, item] : all) {
			cat.index[key] = cat.items.size();
			cat.items.push_back(std::move(item));
		}
		return cat;
	}
}
