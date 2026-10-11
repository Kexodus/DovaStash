#include "Game/Eligibility.h"

#include "Game/FormUtil.h"
#include "Log.h"

namespace dova::game
{
	namespace
	{
		constexpr std::string_view kPlayerHouseKeyword = "LocTypePlayerHouse";

		std::string OwnerName(RE::TESForm* a_owner)
		{
			if (!a_owner) {
				return "nobody";
			}
			if (const char* name = a_owner->GetName(); name && *name) {
				return name;
			}
			return std::format("{:08X}", a_owner->GetFormID());
		}

		RE::BGSEncounterZone* EncounterZone(RE::TESObjectCELL* a_cell)
		{
			if (!a_cell) {
				return nullptr;
			}
			if (const auto* loaded = a_cell->GetRuntimeData().loadedData; loaded && loaded->encounterZone) {
				return loaded->encounterZone;
			}
			return a_cell->extraList.GetEncounterZone();
		}

		bool DisabledImpl(RE::TESObjectREFR* a_ref, int a_depth)
		{
			if (!a_ref) {
				return false;
			}
			if (a_ref->IsDisabled() || a_ref->IsDeleted() || a_ref->IsMarkedForDeletion()) {
				return true;
			}
			if (a_depth >= 8) {
				return false;
			}
			const auto* xParent = a_ref->extraList.GetByType<RE::ExtraEnableStateParent>();
			if (!xParent) {
				return false;
			}
			auto parent = xParent->parent.get();
			if (!parent) {
				return false;
			}
			const bool parentDisabled = DisabledImpl(parent.get(), a_depth + 1);
			const bool opposite = (xParent->flags & 1) != 0;  // "set enable state to opposite of parent"
			return opposite ? !parentDisabled : parentDisabled;
		}
	}

	bool IsPlayerOwner(RE::TESForm* a_owner)
	{
		if (!a_owner) {
			return false;
		}
		const auto id = a_owner->GetFormID();
		if (id == kPlayerBaseID || id == kPlayerFactionID) {
			return true;
		}
		if (const auto* faction = a_owner->As<RE::TESFaction>()) {
			const auto* player = Player();
			return player && player->IsInFaction(faction);
		}
		return false;
	}

	bool IsPlayerHouse(RE::TESObjectCELL* a_cell)
	{
		// The cell's own location only. Walking up to parent locations let a mod that tags a whole town
		// or hold with LocTypePlayerHouse open every cell below it (ownership and respawn checks).
		const auto* loc = a_cell ? a_cell->GetLocation() : nullptr;
		return loc && loc->HasKeywordString(kPlayerHouseKeyword);
	}

	bool CellNeverResets(RE::TESObjectCELL* a_cell)
	{
		const auto* zone = EncounterZone(a_cell);
		return zone && zone->data.flags.any(RE::ENCOUNTER_ZONE_DATA::Flag::kNeverResets);
	}

	bool IsEffectivelyDisabled(RE::TESObjectREFR* a_ref)
	{
		return DisabledImpl(a_ref, 0);
	}

	bool IsContainerRef(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) {
			return false;
		}
		const auto* base = a_ref->GetBaseObject();
		return base && base->Is(RE::FormType::Container);
	}

	std::vector<std::string> AttachedScripts(RE::TESObjectREFR* a_ref)
	{
		std::vector<std::string> out;
		auto*                    vm = a_ref ? RE::BSScript::Internal::VirtualMachine::GetSingleton() : nullptr;
		auto*                    policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
		if (!policy) {
			return out;
		}
		// Same handle the VM binds ObjectReference scripts to (see IVirtualMachine::TraceForm).
		const auto handle = policy->GetHandleForObject(a_ref->GetFormType(), a_ref);
		if (handle == policy->EmptyHandle()) {
			return out;
		}
		RE::BSSpinLockGuard locker(vm->attachedScriptsLock);
		const auto          it = vm->attachedScripts.find(handle);
		if (it == vm->attachedScripts.end()) {
			return out;
		}
		for (const auto& script : it->second) {
			const auto* object = script.get();
			const auto* type = object ? object->GetTypeInfo() : nullptr;
			const char* name = type ? type->GetName() : nullptr;
			// The bare "ObjectReference" type is the engine's base class: the VM binds one whenever a
			// script holds this reference (a property, an alias fill). It has no events or behaviour,
			// so it must not mark the container as scripted.
			if (name && core::IEquals(name, "ObjectReference")) {
				if (log::FirstTime(std::format("bare-objref:{:08X}", a_ref->GetFormID()))) {
					logger::trace("{}: bare ObjectReference script binding ignored", Describe(a_ref));
				}
				continue;
			}
			out.emplace_back(name && *name ? name : "<unknown script>");
		}
		return out;
	}

	Verdict CheckCell(RE::TESObjectCELL* a_cell)
	{
		if (!a_cell) {
			return Verdict::Reject("no cell");
		}
		if ((a_cell->GetFormID() >> 24) == 0xFF) {
			return Verdict::Reject("temporary cell");
		}

		auto*      owner = a_cell->GetOwner();
		const bool playerOwned = IsPlayerOwner(owner);
		const bool playerHouse = IsPlayerHouse(a_cell);

		if (!a_cell->IsInteriorCell() && !playerOwned) {
			return Verdict::Reject("exterior cell not owned by you");
		}
		if (owner && !playerOwned && !playerHouse) {
			return Verdict::Reject(std::format("owned by {}", OwnerName(owner)));
		}
		return Verdict::Accept();
	}

	Verdict CheckContainer(RE::TESObjectREFR* a_ref, RE::TESObjectCELL* a_homeCell)
	{
		if (!a_ref) {
			return Verdict::Reject("nothing targeted");
		}
		auto* base = a_ref->GetBaseObject();
		auto* cont = base ? base->As<RE::TESObjectCONT>() : nullptr;
		if (!cont) {
			return Verdict::Reject("not a container");
		}
		if ((a_ref->GetFormID() >> 24) == 0xFF) {
			return Verdict::Reject("temporary container (cannot be remembered)");
		}
		if (a_ref->GetParentCell() != a_homeCell) {
			return Verdict::Reject("container is not in this home's cell");
		}
		if (IsEffectivelyDisabled(a_ref)) {
			return Verdict::Reject("container is disabled (not built yet?)");
		}
		auto verdict = Verdict::Accept();
		if (auto* owner = a_ref->extraList.GetOwner()) {
			if (!IsPlayerOwner(owner)) {
				return Verdict::Reject(std::format("container is owned by {}", OwnerName(owner)));
			}
		} else if (auto* cellOwner = a_homeCell ? a_homeCell->GetOwner() : nullptr; cellOwner && !IsPlayerOwner(cellOwner)) {
			// The container inherits the cell's owner.
			if (!IsPlayerHouse(a_homeCell)) {
				return Verdict::Reject(std::format("container belongs to {} (owner of this place)", OwnerName(cellOwner)));
			}
			verdict.AddWarning(std::format("{} is in a player house still owned by {}", Describe(a_ref), OwnerName(cellOwner)));
		}
		if (cont->data.flags.any(RE::CONT_DATA::Flag::kRespawn)) {
			// A respawning container is emptied when its cell resets. Only a cell whose encounter zone
			// never resets is safe; being a player house is not a guarantee (DESIGN §9).
			if (!CellNeverResets(a_homeCell)) {
				return Verdict::Reject("this container restocks itself when the area resets (items would be lost)");
			}
			verdict.AddWarning(std::format("{} has the respawn flag; accepted because the cell never resets", Describe(a_ref)));
		}
		if (const auto scripts = AttachedScripts(a_ref); !scripts.empty()) {
			std::string names;
			for (const auto& s : scripts) {
				names += names.empty() ? s : ", " + s;
			}
			verdict.scripted = true;
			verdict.AddWarning(std::format("This container runs a script and may reject or return items ({})", names));
		}
		return verdict;
	}
}
