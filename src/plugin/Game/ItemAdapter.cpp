#include "Game/ItemAdapter.h"

#include "Game/FormUtil.h"
#include "Game/ItemMover.h"

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

		// Inventory Injector's FixNote: a book whose model file name contains "note" is shown as a note.
		bool ModelLooksLikeNote(const char* a_model)
		{
			if (!a_model || !*a_model) {
				return false;
			}
			const auto stem = core::ToLower(std::filesystem::path(a_model).stem().string());
			return stem.find("note") != std::string::npos;
		}

		// The Elder Scrolls (vanilla, Dawnguard and the mods that copy them) use Clutter\ElderScroll\ models.
		bool ModelLooksLikeElderScroll(const char* a_model)
		{
			return a_model && core::ToLower(a_model).find("elderscroll") != std::string::npos;
		}

		// What SKSE's scaleform extension reports for a magic item (potion, ingredient, scroll): the
		// costliest effect's actor values, plus I4's "effectKeywords" (keywords of every base effect).
		void FillMagicInfo(core::ItemInfo& a_info, const RE::MagicItem* a_magic)
		{
			if (!a_magic) {
				return;
			}
			if (const auto* effect = a_magic->GetCostliestEffectItem(); effect && effect->baseEffect) {
				const auto& data = effect->baseEffect->data;
				a_info.actorValue = static_cast<std::int32_t>(data.primaryAV);
				a_info.resistance = static_cast<std::int32_t>(data.resistVariable);
				a_info.magicSchool = static_cast<std::int32_t>(data.associatedSkill);
			}
			for (const auto* effect : a_magic->effects) {
				if (!effect || !effect->baseEffect) {
					continue;
				}
				for (const auto* kw : effect->baseEffect->GetKeywords()) {
					if (!kw) {
						continue;
					}
					if (const char* edid = kw->GetFormEditorID(); edid && *edid) {
						a_info.effectKeywords.emplace_back(edid);
					}
				}
			}
		}

		// The root of a variant chain: an enchanted weapon's CNAM template (TESObjectWEAP::templateWeapon) or
		// an enchanted armor's TNAM template (TESObjectARMO::templateArmor), followed to the item that has
		// none. Cycles and absurd chains stop after a few steps. nullptr: the object is its own root.
		RE::TESBoundObject* FamilyRoot(RE::TESBoundObject* a_object)
		{
			RE::TESBoundObject* root = nullptr;
			auto*               current = a_object;
			for (int depth = 0; current && depth < 8; ++depth) {
				RE::TESBoundObject* next = nullptr;
				if (auto* weap = current->As<RE::TESObjectWEAP>()) {
					next = weap->templateWeapon;
				} else if (auto* armo = current->As<RE::TESObjectARMO>()) {
					next = armo->templateArmor;
				}
				if (!next || next == current || next == a_object) {
					break;
				}
				root = next;
				current = next;
			}
			return root;
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

		// Ammo is equipped as a whole: the game shows one equipped entry with every copy, and a RemoveItem with no
		// extra list takes from the worn stack first (that is how a shot arrow leaves the quiver). So when a worn
		// copy of an ammo is there, its plain copies count as worn too: they stay, and are never moved with
		// RemoveItem and no extra list, which would empty the quiver instead (DESIGN §8.2).
		bool PlainCopiesWorn(RE::TESBoundObject* a_object, const RE::InventoryEntryData* a_entry)
		{
			if (!a_object || !a_object->IsAmmo() || !a_entry || !a_entry->extraLists) {
				return false;
			}
			return std::ranges::any_of(*a_entry->extraLists, [](RE::ExtraDataList* a_list) {
				return a_list && a_list->GetCount() > 0 && (a_list->HasType<RE::ExtraWorn>() || a_list->HasType<RE::ExtraWornLeft>());
			});
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
		a_info.family.reset();
		if (auto* root = FamilyRoot(a_object)) {
			if (auto ref = ToFormRef(root); ref && ref->IsValid()) {
				a_info.family = std::move(*ref);  // a dynamic root cannot be persisted: then no family
			}
		}
		a_info.name.clear();
		if (const char* name = a_object->GetName(); name) {
			a_info.name = name;
		}
		a_info.weight = a_object->GetWeight();
		a_info.value = a_object->GetGoldValue();

		// Classification state is derived from this object only: a reused ItemInfo must not keep the
		// previous object's kinds or its base enchantment (an unenchanted item after an enchanted one
		// would otherwise match "enchanted": true).
		a_info.weaponType.reset();
		a_info.armorType.reset();
		a_info.bookTeaches.reset();
		a_info.alchemyKind.reset();
		a_info.soulGemState.reset();
		a_info.ammoKind.reset();
		a_info.enchanted = false;

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

		// UI detail for icon resolution (core/Icons.h): what SKSE's scaleform extension puts on a SkyUI
		// inventory entry. Reset first: callers may reuse an ItemInfo.
		a_info.armorSlots = 0;
		a_info.typeFlags = 0;
		a_info.bookIsNote = false;
		a_info.elderScroll = false;
		a_info.equipSlot.reset();
		a_info.useSound.reset();
		a_info.actorValue.reset();
		a_info.resistance.reset();
		a_info.magicSchool.reset();
		a_info.effectKeywords.clear();
		a_info.teachesSkill.reset();
		a_info.teachesSpell.reset();
		a_info.gemSize.reset();
		a_info.soulSize.reset();

		bool playable = a_object->GetPlayable();
		switch (*type) {
		case core::ItemType::Weapon:
			if (const auto* weap = a_object->As<RE::TESObjectWEAP>()) {
				a_info.weaponType = MapWeaponType(weap->GetWeaponType());
				if (const auto* slot = weap->GetEquipSlot()) {
					a_info.equipSlot = ToFormRef(slot);
				}
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
				a_info.armorSlots = armo->GetSlotMask().underlying();
				// ARMO record flag 0x4 = Non-Playable (skin/armor addons on NPC races etc.).
				if ((armo->GetFormFlags() & RE::TESObjectARMO::RecordFlags::kNonPlayable) != 0) {
					playable = false;
				}
			}
			break;
		case core::ItemType::Book:
			if (auto* book = a_object->As<RE::TESObjectBOOK>()) {
				a_info.bookTeaches = book->TeachesSpell() ? core::BookTeaches::Spell :
				                     book->TeachesSkill() ? core::BookTeaches::Skill :
				                                            core::BookTeaches::None;
				a_info.typeFlags = book->data.flags.underlying();
				a_info.bookIsNote = book->data.type == RE::OBJ_BOOK::Type::kNoteScroll || ModelLooksLikeNote(book->GetModel());
				a_info.elderScroll = ModelLooksLikeElderScroll(book->GetModel());
				if (book->TeachesSpell()) {
					if (const auto* spell = book->GetSpell()) {
						a_info.teachesSpell = ToFormRef(spell);
					}
				} else if (book->TeachesSkill()) {
					a_info.teachesSkill = static_cast<std::int32_t>(book->GetSkill());
				}
			}
			break;
		case core::ItemType::Alchemy:
			if (const auto* alch = a_object->As<RE::AlchemyItem>()) {
				a_info.alchemyKind = alch->IsPoison() ? core::AlchemyKind::Poison :
				                     alch->IsFood()   ? core::AlchemyKind::Food :
				                                        core::AlchemyKind::Potion;
				a_info.typeFlags = alch->data.flags.underlying();
				if (alch->data.consumptionSound) {
					a_info.useSound = ToFormRef(alch->data.consumptionSound);
				}
			}
			FillMagicInfo(a_info, a_object->As<RE::MagicItem>());
			break;
		case core::ItemType::Ingredient:
		case core::ItemType::Scroll:
			FillMagicInfo(a_info, a_object->As<RE::MagicItem>());
			break;
		case core::ItemType::SoulGem:
			if (const auto* gem = a_object->As<RE::TESSoulGem>()) {
				a_info.soulGemState = gem->GetContainedSoul() != RE::SOUL_LEVEL::kNone ? core::SoulGemState::Filled : core::SoulGemState::Empty;
				a_info.gemSize = static_cast<std::uint8_t>(gem->GetMaximumCapacity());
				a_info.soulSize = static_cast<std::uint8_t>(gem->GetContainedSoul());
			}
			break;
		case core::ItemType::Ammo:
			if (auto* ammo = a_object->As<RE::TESAmmo>()) {
				a_info.ammoKind = ammo->IsBolt() ? core::AmmoKind::Bolt : core::AmmoKind::Arrow;
				// AMMO_DATA flags, rebuilt from the accessors (the raw struct moves on VR).
				a_info.typeFlags = (ammo->IgnoresNormalWeaponResistance() ? 0x1u : 0u) | (ammo->GetPlayable() ? 0u : 0x2u) |
				                   (ammo->IsBolt() ? 0u : 0x4u);
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
			base.returns = ItemMover::Returns(object->GetFormID());

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
						info.soulSize = static_cast<std::uint8_t>(xList->GetSoulLevel());
					}
					// Read-only: ExtraDataList::GetDisplayName would add ExtraTextDisplayData to tempered stacks.
					if (const auto* xText = xList->GetByType<RE::ExtraTextDisplayData>(); xText && !xText->displayName.empty()) {
						info.name = xText->displayName.c_str();
					}
					info.customized = xList->HasType<RE::ExtraHealth>() || xList->HasType<RE::ExtraTextDisplayData>() ||
					                  HasExtraEnchantment(xList) || xList->HasType<RE::ExtraPoison>();
					info.stackId = a_table.Add(StackRef{ owner, object, xList, count });
					items.push_back(std::move(info));
				}
			}

			const auto plain = total - extraTotal;
			if (plain > 0) {
				core::ItemInfo info = base;
				info.count = plain;
				info.questItem = inconsistent || entryQuest;
				info.equipped = PlainCopiesWorn(object, entry.get());  // ammo: the rest of an equipped quiver
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
		result.isProtected = PlainCopiesWorn(a_object, entry);
		return result;
	}

	bool IsDistinctExtraList(RE::ExtraDataList* a_extra)
	{
		if (!a_extra) {
			return false;
		}
		for (const auto& data : *a_extra) {
			switch (data.GetType()) {
			case RE::ExtraDataType::kCount:
			case RE::ExtraDataType::kUniqueID:
			case RE::ExtraDataType::kOwnership:
				break;
			default:
				return true;
			}
		}
		return false;
	}

	std::vector<RE::ExtraDataList*> SpecialStacks(RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_object)
	{
		auto out = ExtraStacks(a_ref, a_object);
		std::erase_if(out, [](RE::ExtraDataList* x) { return !IsDistinctExtraList(x); });
		return out;
	}

	std::vector<RE::ExtraDataList*> ExtraStacks(RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_object)
	{
		std::vector<RE::ExtraDataList*> out;
		auto*                           entry = FindChangesEntry(a_ref, a_object);
		if (entry && entry->extraLists) {
			for (auto* xList : *entry->extraLists) {
				if (xList && xList->GetCount() > 0) {
					out.push_back(xList);
				}
			}
		}
		return out;
	}
}
