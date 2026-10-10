#include "Game/ItemAdapter.h"

#include "Game/FormUtil.h"

namespace dova::game
{
	namespace
	{
		std::optional<core::ItemType> MapType(RE::FormType a_type)
		{
			using T = core::ItemType;
			switch (a_type) {
			case RE::FormType::Weapon:
				return T::Weapon;
			case RE::FormType::Armor:
				return T::Armor;
			case RE::FormType::Ammo:
				return T::Ammo;
			case RE::FormType::AlchemyItem:
				return T::Alchemy;
			case RE::FormType::Ingredient:
				return T::Ingredient;
			case RE::FormType::Book:
				return T::Book;
			case RE::FormType::Scroll:
				return T::Scroll;
			case RE::FormType::SoulGem:
				return T::SoulGem;
			case RE::FormType::Misc:
				return T::Misc;
			case RE::FormType::KeyMaster:
				return T::Key;
			case RE::FormType::Light:
				return T::Light;
			default:
				return std::nullopt;
			}
		}

		std::optional<core::WeaponType> MapWeaponType(RE::WEAPON_TYPE a_type)
		{
			using W = core::WeaponType;
			switch (a_type) {
			case RE::WEAPON_TYPE::kHandToHandMelee:
				return W::HandToHand;
			case RE::WEAPON_TYPE::kOneHandSword:
				return W::OneHandSword;
			case RE::WEAPON_TYPE::kOneHandDagger:
				return W::OneHandDagger;
			case RE::WEAPON_TYPE::kOneHandAxe:
				return W::OneHandAxe;
			case RE::WEAPON_TYPE::kOneHandMace:
				return W::OneHandMace;
			case RE::WEAPON_TYPE::kTwoHandSword:
				return W::TwoHandSword;
			case RE::WEAPON_TYPE::kTwoHandAxe:
				return W::TwoHandAxe;
			case RE::WEAPON_TYPE::kBow:
				return W::Bow;
			case RE::WEAPON_TYPE::kStaff:
				return W::Staff;
			case RE::WEAPON_TYPE::kCrossbow:
				return W::Crossbow;
			default:
				return std::nullopt;
			}
		}

		bool HasExtraEnchantment(RE::ExtraDataList* a_extra)
		{
			if (!a_extra) {
				return false;
			}
			const auto* xEnch = a_extra->GetByType<RE::ExtraEnchantment>();
			return xEnch && xEnch->enchantment;
		}

		RE::InventoryEntryData* FindChangesEntry(RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_object)
		{
			auto* changes = a_ref ? a_ref->GetInventoryChanges() : nullptr;
			if (!changes || !changes->entryList) {
				return nullptr;
			}
			for (auto* entry : *changes->entryList) {
				if (entry && entry->object == a_object) {
					return entry;
				}
			}
			return nullptr;
		}
	}

	std::uint32_t StackTable::Add(const StackRef& a_stack)
	{
		const auto id = _next++;
		_stacks.emplace(id, a_stack);
		return id;
	}

	const StackRef* StackTable::Find(std::uint32_t a_stackId) const
	{
		const auto it = _stacks.find(a_stackId);
		return it != _stacks.end() ? &it->second : nullptr;
	}

	bool IsProtectedExtraList(RE::ExtraDataList* a_extra)
	{
		if (!a_extra) {
			return false;
		}
		return a_extra->HasType<RE::ExtraWorn>() || a_extra->HasType<RE::ExtraWornLeft>() ||
		       a_extra->HasType<RE::ExtraHotkey>() || a_extra->HasQuestObjectAlias();
	}

	bool FillBaseInfo(core::ItemInfo& a_info, RE::TESBoundObject* a_object)
	{
		if (!a_object) {
			return false;
		}
		const auto type = MapType(a_object->GetFormType());
		if (!type) {
			return false;
		}
		if (*type == core::ItemType::Light) {
			const auto* light = a_object->As<RE::TESObjectLIGH>();
			if (!light || !light->CanBeCarried()) {
				return false;
			}
		}

		a_info.type = *type;
		if (auto ref = ToFormRef(a_object)) {
			a_info.form = std::move(*ref);
		} else {
			a_info.form = {};  // dynamic form: invalid FormRef, never persisted
		}
		a_info.name.clear();
		if (const char* name = a_object->GetName(); name) {
			a_info.name = name;
		}
		a_info.weight = a_object->GetWeight();
		a_info.value = a_object->GetGoldValue();

		a_info.keywords.clear();
		if (const auto* kwForm = a_object->As<RE::BGSKeywordForm>()) {
			for (const auto* kw : kwForm->GetKeywords()) {
				if (!kw) {
					continue;
				}
				if (const char* edid = kw->GetFormEditorID(); edid && *edid) {
					a_info.keywords.emplace_back(edid);
				}
			}
		}

		if (const auto* ench = a_object->As<RE::TESEnchantableForm>(); ench && ench->formEnchanting) {
			a_info.enchanted = true;
		}

		bool playable = a_object->GetPlayable();
		switch (*type) {
		case core::ItemType::Weapon:
			if (const auto* weap = a_object->As<RE::TESObjectWEAP>()) {
				a_info.weaponType = MapWeaponType(weap->GetWeaponType());
			}
			break;
		case core::ItemType::Armor:
			if (const auto* armo = a_object->As<RE::TESObjectARMO>()) {
				if (armo->IsHeavyArmor()) {
					a_info.armorType = core::ArmorType::Heavy;
				} else if (armo->IsLightArmor()) {
					a_info.armorType = core::ArmorType::Light;
				} else {
					a_info.armorType = core::ArmorType::Clothing;
				}
				// ARMO record flag 0x4 = Non-Playable (skin/armor addons on NPC races etc.).
				if ((armo->GetFormFlags() & RE::TESObjectARMO::RecordFlags::kNonPlayable) != 0) {
					playable = false;
				}
			}
			break;
		case core::ItemType::Book:
			if (const auto* book = a_object->As<RE::TESObjectBOOK>()) {
				a_info.bookTeaches = book->TeachesSpell() ? core::BookTeaches::Spell :
				                     book->TeachesSkill() ? core::BookTeaches::Skill :
				                                            core::BookTeaches::None;
			}
			break;
		case core::ItemType::Alchemy:
			if (const auto* alch = a_object->As<RE::AlchemyItem>()) {
				a_info.alchemyKind = alch->IsPoison() ? core::AlchemyKind::Poison :
				                     alch->IsFood()   ? core::AlchemyKind::Food :
				                                        core::AlchemyKind::Potion;
			}
			break;
		case core::ItemType::SoulGem:
			if (const auto* gem = a_object->As<RE::TESSoulGem>()) {
				a_info.soulGemState = gem->GetContainedSoul() != RE::SOUL_LEVEL::kNone ? core::SoulGemState::Filled : core::SoulGemState::Empty;
			}
			break;
		case core::ItemType::Ammo:
			if (auto* ammo = a_object->As<RE::TESAmmo>()) {
				a_info.ammoKind = ammo->IsBolt() ? core::AmmoKind::Bolt : core::AmmoKind::Arrow;
			}
			break;
		default:
			break;
		}
		// Nameless items are script tokens (mods track state with unnamed MISC items). The container menu
		// does not show them, so moving one would make it vanish for the player: never movable.
		if (a_info.name.empty()) {
			playable = false;
		}
		a_info.playable = playable;
		return true;
	}

	std::vector<core::ItemInfo> SnapshotInventory(RE::TESObjectREFR* a_ref, StackTable& a_table)
	{
		std::vector<core::ItemInfo> items;
		if (!a_ref) {
			return items;
		}
		const auto owner = a_ref->GetFormID();

		auto inventory = a_ref->GetInventory();
		for (auto& [object, data] : inventory) {
			auto& [total, entry] = data;
			if (!object || total <= 0) {
				continue;
			}

			core::ItemInfo base;
			if (!FillBaseInfo(base, object)) {
				continue;
			}

			const bool entryQuest = entry && entry->IsQuestObject();

			// Pass 1: count what the extra lists claim, so an inconsistent entry is frozen as a whole.
			std::int32_t extraTotal = 0;
			if (entry && entry->extraLists) {
				for (auto* xList : *entry->extraLists) {
					if (xList) {
						extraTotal += std::max(0, xList->GetCount());
					}
				}
			}
			const bool inconsistent = extraTotal > total;
			if (inconsistent) {
				logger::warn("Inventory of {}: {} extra-list count {} exceeds total {}; leaving every stack of it untouched",
					Describe(a_ref), Describe(object), extraTotal, total);
			}

			if (entry && entry->extraLists) {
				for (auto* xList : *entry->extraLists) {
					if (!xList) {
						continue;
					}
					const auto count = xList->GetCount();
					if (count <= 0) {
						continue;
					}
					core::ItemInfo info = base;
					info.count = count;
					info.enchanted = base.enchanted || HasExtraEnchantment(xList);
					info.equipped = xList->HasType<RE::ExtraWorn>() || xList->HasType<RE::ExtraWornLeft>();
					info.favorited = xList->HasType<RE::ExtraHotkey>();
					info.questItem = inconsistent || entryQuest || xList->HasQuestObjectAlias();
					if (info.type == core::ItemType::SoulGem && xList->GetSoulLevel() != RE::SOUL_LEVEL::kNone) {
						info.soulGemState = core::SoulGemState::Filled;
					}
					// Read-only: ExtraDataList::GetDisplayName would add ExtraTextDisplayData to tempered stacks.
					if (const auto* xText = xList->GetByType<RE::ExtraTextDisplayData>(); xText && !xText->displayName.empty()) {
						info.name = xText->displayName.c_str();
					}
					info.stackId = a_table.Add(StackRef{ owner, object, xList, count });
					items.push_back(std::move(info));
				}
			}

			const auto plain = total - extraTotal;
			if (plain > 0) {
				core::ItemInfo info = base;
				info.count = plain;
				info.questItem = inconsistent || entryQuest;
				info.stackId = a_table.Add(StackRef{ owner, object, nullptr, plain });
				items.push_back(std::move(info));
			}
		}
		return items;
	}

	std::int32_t LiveCount(RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_object)
	{
		if (!a_ref || !a_object) {
			return 0;
		}
		const auto counts = a_ref->GetInventoryCounts([a_object](RE::TESBoundObject& a_obj) { return &a_obj == a_object; });
		const auto it = counts.find(a_object);
		return it != counts.end() ? it->second : 0;
	}

	LiveStack FindLiveStack(RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_object, RE::ExtraDataList* a_extra)
	{
		LiveStack result;
		if (!a_ref || !a_object) {
			return result;
		}
		auto* entry = FindChangesEntry(a_ref, a_object);

		if (a_extra) {
			// Pointer identity only: never dereference a list we have not found in the live inventory.
			if (!entry || !entry->extraLists) {
				return result;
			}
			for (auto* xList : *entry->extraLists) {
				if (xList == a_extra) {
					result.found = true;
					result.count = xList->GetCount();
					result.isProtected = IsProtectedExtraList(xList);
					return result;
				}
			}
			return result;
		}

		std::int32_t extraTotal = 0;
		if (entry && entry->extraLists) {
			for (auto* xList : *entry->extraLists) {
				if (xList) {
					extraTotal += std::max(0, xList->GetCount());
				}
			}
		}
		const auto total = LiveCount(a_ref, a_object);
		result.found = total - extraTotal > 0;
		result.count = std::max(0, total - extraTotal);
		return result;
	}

	std::vector<RE::ExtraDataList*> ProtectedExtraLists(RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_object)
	{
		std::vector<RE::ExtraDataList*> out;
		auto*                           entry = FindChangesEntry(a_ref, a_object);
		if (entry && entry->extraLists) {
			for (auto* xList : *entry->extraLists) {
				if (IsProtectedExtraList(xList)) {
					out.push_back(xList);
				}
			}
		}
		return out;
	}
}
