#include "Game/Displays.h"

#include "Game/Eligibility.h"
#include "Game/FormUtil.h"
#include "Game/ItemAdapter.h"

#undef GetObject  // windows.h: RE::BSScript::Variable::GetObject

namespace dova::game
{
	namespace
	{
		using ScriptPtr = RE::BSTSmartPointer<RE::BSScript::Object>;

		constexpr std::string_view kRackScript = "WeaponRackActivateSCRIPT";
		constexpr std::string_view kMannequinScript = "MannequinActivatorSCRIPT";
		constexpr std::string_view kRackTriggerKeyword = "WRackTrigger";

		// Scripts bound to `a_ref` (copied out under the VM's lock), without the bare engine base types.
		std::vector<ScriptPtr> BoundScripts(RE::TESObjectREFR* a_ref)
		{
			std::vector<ScriptPtr> out;
			auto*                  vm = a_ref ? RE::BSScript::Internal::VirtualMachine::GetSingleton() : nullptr;
			auto*                  policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (!policy) {
				return out;
			}
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
				auto*       object = script.get();
				const auto* type = object ? object->GetTypeInfo() : nullptr;
				const char* name = type ? type->GetName() : nullptr;
				if (!name || core::IEquals(name, "ObjectReference") || core::IEquals(name, "Actor")) {
					continue;
				}
				out.emplace_back(object);
			}
			return out;
		}

		std::string ScriptName(const ScriptPtr& a_script)
		{
			const auto* type = a_script ? a_script->GetTypeInfo() : nullptr;
			const char* name = type ? type->GetName() : nullptr;
			return name ? name : "";
		}

		// "::isPlaced_var" -> "isPlaced"
		std::string_view BareName(std::string_view a_name)
		{
			if (a_name.starts_with("::")) {
				a_name.remove_prefix(2);
			}
			if (a_name.ends_with("_var")) {
				a_name.remove_suffix(4);
			}
			return a_name;
		}

		struct Var
		{
			std::string                     name;  // bare
			const RE::BSScript::Variable*   value{ nullptr };
		};

		// Every variable of the script and the scripts it extends (auto properties included).
		std::vector<Var> Variables(const ScriptPtr& a_script)
		{
			std::vector<Var> out;
			if (!a_script) {
				return out;
			}
			for (auto* type = a_script->GetTypeInfo(); type; type = type->GetParent()) {
				const auto* vars = type->GetVariableIter();
				for (std::uint32_t i = 0; vars && i < type->GetNumVariables(); ++i) {
					if (const auto* value = a_script->GetVariable(vars[i].name)) {
						out.push_back(Var{ std::string(BareName(vars[i].name.c_str())), value });
					}
				}
			}
			return out;
		}

		const RE::BSScript::Variable* FindVar(const std::vector<Var>& a_vars, std::string_view a_name)
		{
			const auto it = std::ranges::find_if(a_vars, [&](const Var& v) { return core::IEquals(v.name, a_name); });
			return it == a_vars.end() ? nullptr : it->value;
		}

		// The form an object variable holds, or nullptr (none, or not a form: aliases, active effects).
		RE::TESForm* FormOf(const RE::BSScript::Variable* a_var)
		{
			if (!a_var || !a_var->IsObject()) {
				return nullptr;
			}
			const auto object = a_var->GetObject();
			auto*      vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto*      policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (!object || !policy) {
				return nullptr;
			}
			const auto handle = object->GetHandle();
			auto*      form = RE::TESForm::LookupByID(static_cast<RE::FormID>(handle & 0xFFFFFFFF));
			if (!form || !policy->HandleIsType(static_cast<RE::VMTypeID>(form->GetFormType()), handle)) {
				return nullptr;
			}
			return form;
		}

		bool IsDisplayItem(RE::TESForm* a_form)
		{
			auto* object = a_form ? a_form->As<RE::TESBoundObject>() : nullptr;
			if (!object || object->Is(RE::FormType::KeyMaster) || object->Is(RE::FormType::Light)) {
				return false;
			}
			core::ItemInfo info;
			return FillBaseInfo(info, object) && info.playable && !info.name.empty();
		}

		void AddItems(RE::TESForm* a_form, std::vector<RE::TESBoundObject*>& a_out)
		{
			if (!a_form) {
				return;
			}
			if (auto* list = a_form->As<RE::BGSListForm>()) {
				for (auto* member : list->forms) {
					if (IsDisplayItem(member) && std::ranges::find(a_out, member) == a_out.end()) {
						a_out.push_back(member->As<RE::TESBoundObject>());
					}
				}
				return;
			}
			if (IsDisplayItem(a_form) && std::ranges::find(a_out, a_form) == a_out.end()) {
				a_out.push_back(a_form->As<RE::TESBoundObject>());
			}
		}

		std::vector<RE::TESBoundObject*> ItemsOf(const std::vector<Var>& a_vars)
		{
			std::vector<RE::TESBoundObject*> out;
			for (const auto& v : a_vars) {
				AddItems(FormOf(v.value), out);
			}
			return out;
		}

		// The script's "placed" flag: a bool, or an int that is 0 when empty.
		std::optional<bool> PlacedFlag(const std::vector<Var>& a_vars)
		{
			for (const auto& v : a_vars) {
				if (core::ToLower(v.name).find("placed") == std::string::npos) {
					continue;
				}
				if (v.value->IsBool()) {
					return v.value->GetBool();
				}
				if (v.value->IsInt()) {
					return v.value->GetSInt() != 0;
				}
			}
			return std::nullopt;
		}

		bool HandlesEvent(const ScriptPtr& a_script, std::string_view a_event)
		{
			for (auto* type = a_script ? a_script->GetTypeInfo() : nullptr; type; type = type->GetParent()) {
				const auto* funcs = type->GetMemberFuncIter();
				for (std::uint32_t i = 0; funcs && i < type->GetNumMemberFuncs(); ++i) {
					if (const auto& f = funcs[i].func; f && core::IEquals(f->GetName().c_str(), a_event)) {
						return true;
					}
				}
				const auto* states = type->GetNamedStateIter();
				for (std::uint32_t s = 0; states && s < type->GetNumNamedStates(); ++s) {
					const auto* sf = states[s].GetFuncIter();
					for (std::uint32_t i = 0; sf && i < states[s].GetNumFuncs(); ++i) {
						if (const auto& f = sf[i].func; f && core::IEquals(f->GetName().c_str(), a_event)) {
							return true;
						}
					}
				}
			}
			return false;
		}

		RE::BGSKeyword* RackTriggerKeyword(const std::vector<Var>& a_vars)
		{
			if (auto* form = FormOf(FindVar(a_vars, "WRackTrigger")); form && form->Is(RE::FormType::Keyword)) {
				return form->As<RE::BGSKeyword>();
			}
			return RE::TESForm::LookupByEditorID<RE::BGSKeyword>(kRackTriggerKeyword);
		}

		std::optional<FoundDisplay> Rack(RE::TESObjectREFR* a_ref, const ScriptPtr& a_script)
		{
			const auto vars = Variables(a_script);
			auto*      keyword = RackTriggerKeyword(vars);
			auto*      trigger = keyword ? a_ref->GetLinkedRef(keyword) : nullptr;
			if (!trigger) {
				logger::debug("{}: weapon rack without a trigger (WRackTrigger linked ref); not a display", Describe(a_ref));
				return std::nullopt;
			}
			const auto* typeVar = FindVar(vars, "RackType");
			const auto* coaVar = FindVar(vars, "Patch14COARacks");
			const auto  type = typeVar && typeVar->IsInt() ? typeVar->GetSInt() : 1;
			const bool  coa = coaVar && coaVar->IsBool() && coaVar->GetBool();

			FoundDisplay d;
			d.ref = a_ref->GetFormID();
			d.kind = core::DisplayKind::Rack;
			d.trigger = trigger->GetFormID();
			d.script = ScriptName(a_script);
			if (coa || type == 3) {
				d.rack = core::RackType::NoDaggerOrBow;
			} else if (type == 1 || type == 2 || type == 4) {
				d.rack = static_cast<core::RackType>(type);
			} else {
				logger::debug("{}: weapon rack of unknown type {}; not a display", Describe(a_ref), type);
				return std::nullopt;
			}
			return d;
		}
	}

	std::optional<FoundDisplay> InspectDisplay(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref || a_ref->IsDeleted()) {
			return std::nullopt;
		}
		const auto* base = a_ref->GetBaseObject();
		if (!base) {
			return std::nullopt;
		}
		const bool actor = a_ref->Is(RE::FormType::ActorCharacter);
		const bool container = !actor && base->Is(RE::FormType::Container);
		// Furniture and harvestables point at items too (chopping blocks: firewood and axes).
		if (base->Is(RE::FormType::Furniture) || base->Is(RE::FormType::Flora) || base->Is(RE::FormType::Tree)) {
			return std::nullopt;
		}
		for (const auto& script : BoundScripts(a_ref)) {
			const auto name = ScriptName(script);
			if (actor) {
				if (core::IEquals(name, kMannequinScript)) {
					return FoundDisplay{ a_ref->GetFormID(), core::DisplayKind::Mannequin, core::RackType::Any, 0, {}, std::nullopt, name };
				}
				continue;
			}
			if (core::IEquals(name, kRackScript)) {
				return Rack(a_ref, script);
			}
			const auto vars = Variables(script);
			auto       items = ItemsOf(vars);
			if (items.empty()) {
				continue;
			}
			if (container) {
				if (HandlesEvent(script, "OnItemAdded")) {
					return FoundDisplay{ a_ref->GetFormID(), core::DisplayKind::Case, core::RackType::Any, 0, std::move(items), std::nullopt, name };
				}
				continue;
			}
			if (const auto placed = PlacedFlag(vars)) {
				return FoundDisplay{ a_ref->GetFormID(), core::DisplayKind::Item, core::RackType::Any, 0, std::move(items), placed, name };
			}
		}
		return std::nullopt;
	}

	std::optional<FoundDisplay> DisplayAt(RE::TESObjectREFR* a_ref)
	{
		if (auto d = InspectDisplay(a_ref)) {
			return d;
		}
		const auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
		auto*       cell = a_ref ? a_ref->GetParentCell() : nullptr;
		auto*       root = a_ref ? a_ref->Get3D() : nullptr;
		if (!base || !cell) {
			return std::nullopt;
		}
		const bool activator = base->Is(RE::FormType::Activator);
		const bool gear = base->Is(RE::FormType::Weapon) || base->Is(RE::FormType::Armor);
		if ((!activator || !root) && !gear) {
			return std::nullopt;
		}
		// An activator in front of a mannequin: the mannequin standing inside it. A weapon or shield: the
		// rack whose trigger it sits in.
		const auto                  position = a_ref->GetPosition();
		std::optional<FoundDisplay> best;
		float                       bestDistance = std::numeric_limits<float>::max();
		for (auto& d : FindDisplays(cell)) {
			float distance = 0.0f;
			if (activator && d.kind == core::DisplayKind::Mannequin) {
				auto* actor = RE::TESForm::LookupByID<RE::TESObjectREFR>(d.ref);
				if (!actor) {
					continue;
				}
				distance = root->worldBound.center.GetDistance(actor->GetPosition());
				if (distance > root->worldBound.radius) {
					continue;
				}
			} else if (gear && d.kind == core::DisplayKind::Rack) {
				auto* trigger = RE::TESForm::LookupByID<RE::TESObjectREFR>(d.trigger);
				auto* triggerRoot = trigger ? trigger->Get3D() : nullptr;
				if (!triggerRoot) {
					continue;
				}
				distance = triggerRoot->worldBound.center.GetDistance(position);
				if (distance > triggerRoot->worldBound.radius) {
					continue;
				}
			} else {
				continue;
			}
			if (distance < bestDistance) {
				bestDistance = distance;
				best = std::move(d);
			}
		}
		return best;
	}

	std::optional<FoundDisplay> DeclaredDisplay(RE::TESObjectREFR* a_ref, core::DisplayKind a_kind)
	{
		if (!a_ref) {
			return std::nullopt;
		}
		FoundDisplay d;
		d.ref = a_ref->GetFormID();
		d.kind = a_kind;
		d.script = "layout";
		switch (a_kind) {
		case core::DisplayKind::Rack:
			{
				auto* keyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(kRackTriggerKeyword);
				auto* trigger = keyword ? a_ref->GetLinkedRef(keyword) : nullptr;
				if (!trigger) {
					return std::nullopt;
				}
				d.trigger = trigger->GetFormID();
				return d;
			}
		case core::DisplayKind::Mannequin:
			return a_ref->Is(RE::FormType::ActorCharacter) ? std::optional(d) : std::nullopt;
		case core::DisplayKind::Case:
			return IsContainerRef(a_ref) ? std::optional(d) : std::nullopt;
		case core::DisplayKind::Item:
			return d;
		default:
			return std::nullopt;
		}
	}

	std::vector<FoundDisplay> FindDisplays(RE::TESObjectCELL* a_cell)
	{
		std::vector<FoundDisplay> out;
		if (!a_cell) {
			return out;
		}
		a_cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
			if (auto d = InspectDisplay(a_ref)) {
				logger::trace("Display: {} is a {} ({})", Describe(a_ref), core::DisplayKindName(d->kind), d->script);
				out.push_back(std::move(*d));
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		std::ranges::sort(out, {}, &FoundDisplay::ref);
		return out;
	}

	bool DisplayAvailable(const FoundDisplay& a_display)
	{
		auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_display.ref);
		if (!ref || ref->IsDeleted()) {
			return false;
		}
		if (a_display.kind == core::DisplayKind::Rack) {
			// The activator itself is disabled while the rack holds something; the trigger tells whether
			// the rack is built at all (unbuilt Hearthfire wings disable both through an enable parent).
			auto* trigger = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_display.trigger);
			return trigger && !IsEffectivelyDisabled(trigger) && trigger->Get3D();
		}
		if (IsEffectivelyDisabled(ref)) {
			return false;
		}
		if (a_display.kind == core::DisplayKind::Mannequin) {
			const auto* actor = ref->As<RE::Actor>();
			return actor && !actor->IsDead();
		}
		return true;
	}

	bool DisplayFull(const FoundDisplay& a_display)
	{
		auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_display.ref);
		if (!ref) {
			return true;
		}
		switch (a_display.kind) {
		case core::DisplayKind::Rack:
			return ref->IsDisabled();
		case core::DisplayKind::Mannequin:
			return !DisplayHolds(a_display).empty();
		case core::DisplayKind::Item:
			return a_display.placed.value_or(false);
		default:
			return false;
		}
	}

	std::vector<RE::TESBoundObject*> DisplayHolds(const FoundDisplay& a_display)
	{
		std::vector<RE::TESBoundObject*> out;
		auto*                            ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_display.ref);
		if (!ref || (a_display.kind != core::DisplayKind::Mannequin && a_display.kind != core::DisplayKind::Case)) {
			return out;
		}
		for (const auto& [object, data] : ref->GetInventory()) {
			if (object && data.first > 0 && (a_display.kind == core::DisplayKind::Case || object->Is(RE::FormType::Armor))) {
				out.push_back(object);
			}
		}
		return out;
	}

	std::set<RE::FormID> LooseGear(RE::TESObjectCELL* a_cell)
	{
		std::set<RE::FormID> out;
		if (!a_cell) {
			return out;
		}
		a_cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
			const auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
			if (base && (base->Is(RE::FormType::Weapon) || base->Is(RE::FormType::Armor)) && !IsEffectivelyDisabled(a_ref)) {
				out.insert(base->GetFormID());
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		return out;
	}
}
