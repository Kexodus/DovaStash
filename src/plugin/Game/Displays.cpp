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
		constexpr std::string_view kRackTriggerWaiting = "WaitingForReference";  // the trigger state that notices a take
		constexpr std::string_view kBookshelfScript = "PlayerBookShelfContainerScript";
		constexpr std::string_view kShelfContainerKeyword = "BookShelfContainer";  // PlayerBookShelfClickTrigger -> its shelf
		constexpr std::string_view kBookTriggerIgnore = "IgnoreBooks";  // book trigger states (PlayerBookShelfTriggerSCRIPT)
		constexpr std::string_view kBookTriggerWait = "WaitForBooks";
		constexpr int              kBookSlots = 18;    // PlacedBook01..18, BookShelfBook01..18
		constexpr int              kBookTriggers = 4;  // BookShelfTrigger01..04

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

		bool HasState(const ScriptPtr& a_script, std::string_view a_state)
		{
			for (auto* type = a_script ? a_script->GetTypeInfo() : nullptr; type; type = type->GetParent()) {
				const auto* states = type->GetNamedStateIter();
				for (std::uint32_t s = 0; states && s < type->GetNumNamedStates(); ++s) {
					if (core::IEquals(states[s].name.c_str(), a_state)) {
						return true;
					}
				}
			}
			return false;
		}

		// The activator's WeaponRackActivateSCRIPT, if it keeps the placed weapon (PlayersDroppedWeapon).
		ScriptPtr RackScript(RE::TESObjectREFR* a_rack)
		{
			for (auto& s : BoundScripts(a_rack)) {
				if (core::IEquals(ScriptName(s), kRackScript) && s->GetProperty("PlayersDroppedWeapon")) {
					return s;
				}
			}
			return {};
		}

		// "PlacedBook01", "BookShelfTrigger02Ref".
		std::string Numbered(std::string_view a_prefix, int a_index, std::string_view a_suffix = {})
		{
			return std::format("{}{:02}{}", a_prefix, a_index, a_suffix);
		}

		std::int32_t IntOf(const RE::BSScript::Variable* a_var)
		{
			return a_var && a_var->IsInt() ? a_var->GetSInt() : 0;
		}

		// An object variable that is not None (also one whose form no longer exists: the script compares it
		// with None, so that slot counts as taken).
		bool HoldsObject(const RE::BSScript::Variable* a_var)
		{
			return a_var && a_var->IsObject() && a_var->GetObject();
		}

		// PlayerBookShelfContainerScript, or a home mod's copy of it with the same variables and UpdateBooks.
		bool IsBookshelfScript(const ScriptPtr& a_script)
		{
			if (core::IEquals(ScriptName(a_script), kBookshelfScript)) {
				return true;
			}
			const auto vars = Variables(a_script);
			return FindVar(vars, "MaxBooksAllowed") && FindVar(vars, "CurrentBookAmount") && FindVar(vars, "PlacedBook01") &&
			       HandlesEvent(a_script, "UpdateBooks");
		}

		ScriptPtr BookshelfScript(RE::TESObjectREFR* a_ref)
		{
			if (!a_ref || !IsContainerRef(a_ref)) {
				return {};
			}
			for (auto& s : BoundScripts(a_ref)) {
				if (IsBookshelfScript(s)) {
					return s;
				}
			}
			return {};
		}

		RE::BGSKeyword* KeywordOf(const std::vector<Var>& a_vars, std::string_view a_name)
		{
			auto* form = FormOf(FindVar(a_vars, a_name));
			return form ? form->As<RE::BGSKeyword>() : nullptr;
		}

		// The book triggers of a shelf that can ignore books: BookShelfTrigger01Ref..04Ref (set on its first cell
		// load), else its BookShelfTrigger01..04 linked refs.
		std::vector<ScriptPtr> BookTriggers(RE::TESObjectREFR* a_shelf, const std::vector<Var>& a_vars)
		{
			std::vector<RE::TESObjectREFR*> refs;
			for (int i = 1; i <= kBookTriggers; ++i) {
				auto* form = FormOf(FindVar(a_vars, Numbered("BookShelfTrigger", i, "Ref")));
				auto* ref = form ? form->As<RE::TESObjectREFR>() : nullptr;
				if (!ref) {
					auto* keyword = KeywordOf(a_vars, Numbered("BookShelfTrigger", i));
					ref = keyword ? a_shelf->GetLinkedRef(keyword) : nullptr;
				}
				if (ref && std::ranges::find(refs, ref) == refs.end()) {
					refs.push_back(ref);
				}
			}
			std::vector<ScriptPtr> out;
			for (auto* ref : refs) {
				for (auto& s : BoundScripts(ref)) {
					if (HasState(s, kBookTriggerIgnore)) {
						out.push_back(s);
					}
				}
			}
			return out;
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
			} else if (type == 1 || type == 2 || type == 4 || type == 5) {
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
			// Before cases: a bookshelf's PlacedBookNN slots hold books too.
			if (container && IsBookshelfScript(script)) {
				return FoundDisplay{ a_ref->GetFormID(), core::DisplayKind::Bookshelf, core::RackType::Any, 0, {}, std::nullopt, name };
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
		if (auto* shelf = BookshelfAt(a_ref); shelf && shelf != a_ref) {
			return InspectDisplay(shelf);
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

	RE::TESObjectREFR* BookshelfAt(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) {
			return nullptr;
		}
		if (BookshelfScript(a_ref)) {
			return a_ref;
		}
		const auto* base = a_ref->GetBaseObject();
		auto*       cell = a_ref->GetParentCell();
		if (!base || !cell) {
			return nullptr;
		}
		const auto shelf = [&](RE::TESObjectREFR* a_candidate) {
			return a_candidate && a_candidate != a_ref && a_candidate->GetParentCell() == cell && BookshelfScript(a_candidate);
		};
		if (base->Is(RE::FormType::Activator)) {
			// PlayerBookShelfClickTrigger links with BookShelfContainer (and keeps it in ShelfContainer); the
			// book triggers and home mods' activators use the plain linked ref.
			if (auto* linked = a_ref->GetLinkedRef(nullptr); shelf(linked)) {
				return linked;
			}
			if (auto* keyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(kShelfContainerKeyword)) {
				if (auto* linked = a_ref->GetLinkedRef(keyword); shelf(linked)) {
					return linked;
				}
			}
			for (const auto& script : BoundScripts(a_ref)) {
				for (const auto& v : Variables(script)) {
					auto* form = FormOf(v.value);
					if (auto* ref = form ? form->As<RE::TESObjectREFR>() : nullptr; shelf(ref)) {
						return ref;
					}
				}
			}
			return nullptr;
		}
		if (!base->Is(RE::FormType::Book)) {
			return nullptr;
		}
		// A book on a shelf: the copy its script placed on a marker (PlacedBookNNRef).
		RE::TESObjectREFR* found = nullptr;
		cell->ForEachReference([&](RE::TESObjectREFR* a_candidate) {
			const auto script = a_candidate && a_candidate != a_ref ? BookshelfScript(a_candidate) : ScriptPtr{};
			if (script) {
				const auto vars = Variables(script);
				for (int i = 1; i <= kBookSlots; ++i) {
					if (FormOf(FindVar(vars, Numbered("PlacedBook", i, "Ref"))) == a_ref) {
						found = a_candidate;
						return RE::BSContainer::ForEachResult::kStop;
					}
				}
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		return found;
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
		case core::DisplayKind::Bookshelf:
			return std::nullopt;  // without its script, how many books fit is unknown: nothing goes on it
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
		case core::DisplayKind::Case:
			{
				const auto holds = DisplayHolds(a_display);
				return !a_display.items.empty() && std::ranges::all_of(a_display.items, [&](RE::TESBoundObject* a_item) {
					return std::ranges::find(holds, a_item) != holds.end();
				});
			}
		case core::DisplayKind::Bookshelf:
			return core::BookshelfRoom(BookshelfStateOf(a_display)) <= 0;
		default:
			return false;
		}
	}

	std::vector<RE::TESBoundObject*> DisplayHolds(const FoundDisplay& a_display)
	{
		std::vector<RE::TESBoundObject*> out;
		auto*                            ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_display.ref);
		const auto kind = a_display.kind;
		if (!ref || (kind != core::DisplayKind::Mannequin && kind != core::DisplayKind::Case && kind != core::DisplayKind::Bookshelf)) {
			return out;
		}
		for (const auto& [object, data] : ref->GetInventory()) {
			if (object && data.first > 0 &&
				(kind == core::DisplayKind::Case || (kind == core::DisplayKind::Mannequin && object->Is(RE::FormType::Armor)) ||
					(kind == core::DisplayKind::Bookshelf && object->Is(RE::FormType::Book)))) {
				out.push_back(object);
			}
		}
		return out;
	}

	core::BookshelfState BookshelfStateOf(const FoundDisplay& a_shelf)
	{
		core::BookshelfState out;
		auto*                shelf = a_shelf.kind == core::DisplayKind::Bookshelf ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a_shelf.ref) : nullptr;
		const auto           script = BookshelfScript(shelf);
		if (!script) {
			return out;  // no capacity: nothing goes on it
		}
		const auto vars = Variables(script);
		out.capacity = IntOf(FindVar(vars, "MaxBooksAllowed"));
		out.counted = IntOf(FindVar(vars, "CurrentBookAmount"));
		for (int i = 1; i <= kBookSlots; ++i) {
			const auto* slot = FindVar(vars, Numbered("PlacedBook", i));
			if (!slot) {
				break;
			}
			out.slots.push_back(HoldsObject(slot));
		}
		// Like the script's CountMaxBooks: the markers linked with BookShelfBook01, 02... up to the first missing.
		std::int32_t markers = 0;
		for (int i = 1; i <= kBookSlots; ++i) {
			auto* keyword = KeywordOf(vars, Numbered("BookShelfBook", i));
			if (!keyword) {
				if (i == 1) {
					markers = -1;  // a copy without these keywords: unknown, the script's own count decides
				}
				break;
			}
			if (!shelf->GetLinkedRef(keyword)) {
				break;
			}
			++markers;
		}
		out.markers = markers;
		for (const auto& [object, data] : shelf->GetInventory()) {
			if (object && object->Is(RE::FormType::Book) && data.first > 0) {
				out.inside += data.first;
			}
		}
		return out;
	}

	std::vector<RE::FormID> BookshelfSlots(const FoundDisplay& a_shelf)
	{
		std::vector<RE::FormID> out;
		auto*                   shelf = a_shelf.kind == core::DisplayKind::Bookshelf ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a_shelf.ref) : nullptr;
		const auto              script = BookshelfScript(shelf);
		if (!script) {
			return out;
		}
		const auto vars = Variables(script);
		for (int i = 1; i <= kBookSlots; ++i) {
			auto* form = FormOf(FindVar(vars, Numbered("PlacedBook", i)));
			out.push_back(form ? form->GetFormID() : 0);
		}
		return out;
	}

	bool DisableRack(const FoundDisplay& a_rack)
	{
		auto* rack = a_rack.kind == core::DisplayKind::Rack ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a_rack.ref) : nullptr;
		if (!rack) {
			return false;
		}
		if (!RackScript(rack)) {
			logger::debug("{}: no {} with PlayersDroppedWeapon; waiting for its trigger instead", Describe(rack), kRackScript);
			return false;
		}
		if (!rack->IsDisabled()) {
			rack->Disable();
		}
		return true;
	}

	namespace
	{
		// Sets the flag once the VM ran the call.
		class DoneFlag final : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			explicit DoneFlag(std::shared_ptr<std::atomic<bool>> a_flag) :
				_flag(std::move(a_flag))
			{}

			void operator()(RE::BSScript::Variable) override { *_flag = true; }
			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

		private:
			std::shared_ptr<std::atomic<bool>> _flag;
		};

		// Runs `a_fn` on the main thread once the VM ran the call.
		class Then final : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			explicit Then(std::function<void()> a_fn) :
				_fn(std::move(a_fn))
			{}

			void operator()(RE::BSScript::Variable) override
			{
				if (_fn) {
					SKSE::GetTaskInterface()->AddTask(std::exchange(_fn, nullptr));
				}
			}
			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

		private:
			std::function<void()> _fn;
		};

		// Runs `a_fn` on the main thread after `a_delay`.
		void After(std::chrono::milliseconds a_delay, std::function<void()> a_fn)
		{
			std::thread([a_delay, fn = std::move(a_fn)]() mutable {
				std::this_thread::sleep_for(a_delay);
				SKSE::GetTaskInterface()->AddTask(std::move(fn));
			}).detach();
		}

		bool GotoState(ScriptPtr a_script, std::string_view a_state, std::function<void()> a_then = nullptr)
		{
			auto*                                                    vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
			if (a_then) {
				callback.reset(new Then(std::move(a_then)));
			}
			return vm && vm->DispatchMethodCall(a_script, "GotoState", RE::MakeFunctionArguments(RE::BSFixedString(a_state)), callback);
		}
	}

	std::shared_ptr<std::atomic<bool>> PapyrusMoveToNode(RE::TESObjectREFR* a_item, RE::TESObjectREFR* a_target, std::string_view a_node)
	{
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
		if (!policy || !a_item || !a_target) {
			return nullptr;
		}
		const auto handle = policy->GetHandleForObject(a_item->GetFormType(), a_item);
		if (handle == policy->EmptyHandle()) {
			return nullptr;
		}
		auto                                                     flag = std::make_shared<std::atomic<bool>>(false);
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{ new DoneFlag(flag) };
		auto* args = RE::MakeFunctionArguments(static_cast<RE::TESObjectREFR*>(a_target), RE::BSFixedString(a_node));
		if (!vm->DispatchMethodCall(handle, "ObjectReference", "MoveToNode", args, callback)) {
			return nullptr;
		}
		return flag;
	}

	std::shared_ptr<std::atomic<int>> RefreshBookshelf(const FoundDisplay& a_shelf)
	{
		auto       status = std::make_shared<std::atomic<int>>(0);
		auto*      vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		auto*      shelf = a_shelf.kind == core::DisplayKind::Bookshelf ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a_shelf.ref) : nullptr;
		auto       script = BookshelfScript(shelf);
		const auto name = Describe(shelf);
		if (!vm || !script || !HandlesEvent(script, "UpdateBooks")) {
			logger::debug("{}: its script can't be asked to place the books; they show the next time it is opened", name);
			*status = -1;
			return status;
		}
		const char* current = script->currentState.c_str();
		if (const std::string_view state = current ? current : ""; !state.empty()) {
			// PlacingBooks (its menu just closed: its own UpdateBooks runs and restores the triggers) or the
			// Unofficial Patch's PickingUp: leave it to the script; the books show the next time it is opened.
			logger::debug("{}: its script is busy ({}); the books show the next time it is opened", name, state);
			*status = -1;
			return status;
		}
		const auto triggers = BookTriggers(shelf, Variables(script));
		// 3. A moment after UpdateBooks returned (the trigger events of the copies it replaced are past), the
		// triggers watch the books again.
		const auto restore = [triggers, status, name](int a_status) {
			After(std::chrono::milliseconds(250), [triggers, status, name, a_status]() {
				for (const auto& t : triggers) {
					if (!GotoState(t, kBookTriggerWait)) {
						logger::warn("{}: a book trigger could not be set back to {}; opening the shelf once fixes that", name, kBookTriggerWait);
					}
				}
				*status = a_status;
			});
		};
		// 2. The script's own UpdateBooks.
		auto update = [script, status, restore, name]() mutable {
			auto*                                                    vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{ new Then([restore, name]() {
				logger::debug("{}: its UpdateBooks placed the books on their markers", name);
				restore(1);
			}) };
			if (!vm || !vm->DispatchMethodCall(script, "UpdateBooks", RE::MakeFunctionArguments(), callback)) {
				logger::warn("{}: its UpdateBooks could not be called; the books show the next time it is opened", name);
				restore(-1);  // nothing was replaced
			}
		};
		// 1. The triggers ignore the copies UpdateBooks removes (they would take the books out of the shelf).
		if (triggers.empty()) {
			update();
			return status;
		}
		auto pending = std::make_shared<std::atomic<int>>(static_cast<int>(triggers.size()));
		auto failed = std::make_shared<std::atomic<bool>>(false);
		auto next = [pending, failed, update, restore, name]() mutable {
			if (--*pending > 0) {
				return;
			}
			if (*failed) {
				logger::warn("{}: a book trigger could not be told to ignore books; the books show the next time it is opened", name);
				restore(-1);
				return;
			}
			update();
		};
		for (const auto& t : triggers) {
			if (!GotoState(t, kBookTriggerIgnore, next)) {
				*failed = true;
				auto now = next;
				now();
			}
		}
		return status;
	}

	namespace
	{
		// The angles the game turns back into `a_rotate` (a reference's 3D is built as
		// NiMatrix3::SetEulerAnglesXYZ(angle), the inverse of the ToEulerAnglesXYZ the MoveToNode native
		// uses). CommonLib's ToEulerAnglesXYZ has the same formulas but goes through its NiFastATan2,
		// which drops Gamebryo's `if (offset)` and so negates the result whenever |y| <= |x|; its
		// gimbal-lock branches have the wrong sign too. std::atan2 and a consistent gimbal case instead.
		RE::NiPoint3 AnglesOf(const RE::NiMatrix3& a_rotate)
		{
			const auto&  e = a_rotate.entry;
			const float  sinY = -e[0][2];
			const float  cosY = std::sqrt(e[0][0] * e[0][0] + e[0][1] * e[0][1]);
			RE::NiPoint3 angle;
			angle.y = std::atan2(sinY, cosY);
			if (cosY > 1.0e-5f) {
				angle.x = std::atan2(e[1][2], e[2][2]);
				angle.z = std::atan2(e[0][1], e[0][0]);
			} else if (sinY > 0.0f) {
				angle.x = std::atan2(e[1][0], e[1][1]);  // y = +90 degrees: only x - z matters
				angle.z = 0.0f;
			} else {
				angle.x = std::atan2(-e[1][0], e[1][1]);  // y = -90 degrees: only x + z matters
				angle.z = 0.0f;
			}
			return angle;
		}

		bool HasNode(RE::TESObjectREFR* a_ref, std::string_view a_node)
		{
			auto* root = a_ref ? a_ref->Get3D() : nullptr;
			return root && root->GetObjectByName(RE::BSFixedString(a_node));
		}

		// The Unofficial Patch's WeaponRackActivateSCRIPT (it has USKPForswornStavesList) and its variables
		// (which live in the script object, so it is kept with them).
		struct PatchedRack
		{
			ScriptPtr        script;
			std::vector<Var> vars;
		};

		std::optional<PatchedRack> PatchedRackScript(const FoundDisplay& a_rack)
		{
			auto*       rack = a_rack.kind == core::DisplayKind::Rack ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a_rack.ref) : nullptr;
			PatchedRack out{ rack ? RackScript(rack) : ScriptPtr{}, {} };
			out.vars = Variables(out.script);
			if (!FindVar(out.vars, "USKPForswornStavesList")) {
				return std::nullopt;
			}
			return out;
		}

		RE::BGSListForm* ForswornStaves(const std::vector<Var>& a_vars)
		{
			auto* form = FormOf(FindVar(a_vars, "USKPForswornStavesList"));
			return form ? form->As<RE::BGSListForm>() : nullptr;
		}
	}

	RackFit RackFitOf(const FoundDisplay& a_rack)
	{
		RackFit    out;
		const auto patched = PatchedRackScript(a_rack);
		auto*      trigger = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_rack.trigger);
		if (!patched || !trigger) {
			return out;
		}
		out.crossbows = HasNode(trigger, "CrossbowPivot01");
		if (auto* staves = ForswornStaves(patched->vars); staves && HasNode(trigger, "BogusPivot")) {
			staves->ForEachForm([&](RE::TESForm* a_form) {
				if (auto* object = a_form ? a_form->As<RE::TESBoundObject>() : nullptr) {
					out.refuses.push_back(object);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}
		return out;
	}

	RackPlacement RackNode(const FoundDisplay& a_rack, RE::TESObjectREFR* a_item, std::string_view a_pivot)
	{
		RackPlacement out{ std::string(a_pivot), {} };
		const auto    patched = PatchedRackScript(a_rack);
		auto*         trigger = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_rack.trigger);
		auto*         base = a_item ? a_item->GetBaseObject() : nullptr;
		if (!patched || !trigger || !base) {
			return out;  // the vanilla script: core's choice
		}
		const auto form = [&](std::string_view a_name) -> RE::TESForm* { return FormOf(FindVar(patched->vars, a_name)); };
		const auto keyword = [&](std::string_view a_name) {
			auto* k = form(a_name);
			return k && k->Is(RE::FormType::Keyword) && a_item->HasKeyword(k->As<RE::BGSKeyword>());
		};
		auto*      staves = ForswornStaves(patched->vars);
		const bool forsworn = staves && staves->HasForm(base);

		std::string_view node = a_pivot;
		if (a_pivot == "StaffPivot01") {
			// CheckRackType, then HandlePlayerItem: staves the rack turns away.
			if (forsworn && HasNode(trigger, "BogusPivot")) {
				out.node.clear();
				out.refusal = "its rack has no room for a Forsworn staff";
				return out;
			}
			if (HasNode(trigger, "WRDisplayCase01") && (HasNode(a_item, "Staff3rdPerson:0") || HasNode(a_item, "3rdPersonStaff04:1"))) {
				out.node.clear();
				out.refusal = "the staff is too long for the display case";
				return out;
			}
			if (a_rack.rack == core::RackType::NoDaggerOrBow) {  // PlaceStaffCoA
				node = HasNode(a_item, "Staff3rdPerson:0") ? "StaffPivotFalmer" : "StaffPivot01";
			} else if (base == form("DA16SkullOfCorruption")) {  // PlaceStaff
				node = "StaffPivotCorruption";
			} else if (base == form("DA14SanguineRose")) {
				node = "StaffPivotSanguine";
			} else if (HasNode(a_item, "Staff3rdPerson:0") || base == form("DA15Wabbajack")) {
				node = "StaffPivotFalmer";
			} else if (forsworn) {
				node = "StaffPivotForsworn";
			} else if (HasNode(a_item, "DragonPriestStaff3rd:0") || HasNode(a_item, "MQ_DragonPriestStaff3rd01:0")) {
				node = "StaffPivot03";
			} else if (HasNode(a_item, "Staff01:0") || HasNode(a_item, "Staff03:1") || base == form("MG07StaffOfMagnus")) {
				node = "StaffPivot01";
			} else {
				node = "StaffPivot02";
			}
		} else if (a_pivot == "BowPivot01") {  // PlaceBow
			if (HasNode(a_item, "CrossbowRoot") && HasNode(trigger, "CrossbowPivot01")) {
				node = HasNode(a_item, "DwarvenCrossbow.nif") ? "CrossbowPivot02" : "CrossbowPivot01";
			}
		} else if (a_pivot == "ShieldPivot01") {  // PlaceShield
			if (base == form("ArmorShieldOfYsgramor")) {
				node = "ShieldPivotYsgramor";
			} else if (keyword("ArmorMaterialDwarven") && base != form("DA13Spellbreaker")) {
				node = "ShieldPivotDwarven";
			} else if (keyword("ArmorMaterialDaedric")) {
				node = "ShieldPivotDaedric";
			} else if (keyword("ArmorMaterialDragonPlate")) {
				node = "ShieldPivotDragonBone";
			}
		}
		if (node != a_pivot) {
			if (HasNode(trigger, node)) {
				out.node = std::string(node);
			} else {
				logger::debug("{}: its trigger has no {} for {}; using {}", Describe(RE::TESForm::LookupByID(a_rack.ref)), node, Describe(a_item), a_pivot);
			}
		}
		return out;
	}

	bool MoveToNodeDirect(RE::TESObjectREFR* a_item, RE::TESObjectREFR* a_target, std::string_view a_node)
	{
		auto* root = a_target ? a_target->Get3D() : nullptr;
		auto* node = root ? root->GetObjectByName(RE::BSFixedString(a_node)) : nullptr;
		if (!a_item || !node) {
			return false;
		}
		// TESObjectREFR::MoveTo_Impl (private in CommonLib), called the way CommonLib's MoveToNode calls it.
		using MoveTo_t = void (*)(RE::TESObjectREFR*, const RE::ObjectRefHandle&, RE::TESObjectCELL*, RE::TESWorldSpace*, const RE::NiPoint3&,
			const RE::NiPoint3&);
		static REL::Relocation<MoveTo_t> moveTo{ RELOCATION_ID(56227, 56626) };
		const auto                       handle = a_target->GetHandle();
		const auto                       angle = AnglesOf(node->world.rotate);
		moveTo(a_item, handle, a_target->GetParentCell(), a_item->GetWorldspace(), node->world.translate, angle);
		return true;
	}

	void RecordRackItem(const FoundDisplay& a_rack, RE::TESObjectREFR* a_item)
	{
		auto* rack = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_rack.ref);
		auto* trigger = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_rack.trigger);
		if (a_rack.kind != core::DisplayKind::Rack || !rack || !trigger || !a_item || !rack->IsDisabled()) {
			return;
		}
		auto  script = RackScript(rack);
		auto* dropped = script ? script->GetProperty("PlayersDroppedWeapon") : nullptr;
		if (!dropped) {
			return;  // no script to tell (the trigger disabled the activator)
		}
		dropped->Pack(a_item);
		if (auto* init = script->GetProperty("PlacedItemInit")) {
			init->SetBool(true);  // Unofficial Patch: PlayersDroppedWeapon is reliable
		}
		std::string_view state = "unchanged";
		auto*            vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		for (auto& s : BoundScripts(trigger)) {
			if (!vm || !HasState(s, kRackTriggerWaiting) || core::IEquals(s->currentState.c_str(), kRackTriggerWaiting)) {
				continue;
			}
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
			vm->DispatchMethodCall(s, "GotoState", RE::MakeFunctionArguments(RE::BSFixedString(kRackTriggerWaiting)), callback);
			state = kRackTriggerWaiting;
		}
		logger::debug("{}: holds {} (activator disabled, trigger state {})", Describe(rack), Describe(a_item), state);
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

	namespace
	{
		std::atomic<RE::FormID> g_hidden{ 0 };  // the dropped weapon HideDropped keeps out of sight (0 = none)

		// App-culls (hides) or shows the root of `a_id`'s 3D; only the rendering changes. Nothing without 3D
		// (the reference is gone, picked up or unloaded: a 3D loaded later is a fresh, visible copy).
		void Cull(RE::FormID a_id, bool a_cull)
		{
			auto* ref = a_id != 0 ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a_id) : nullptr;
			if (auto* root = ref ? ref->Get3D() : nullptr) {
				root->SetAppCulled(a_cull);
			}
		}

		// Hides the weapon the moment its 3D loads (also when it loads again before it is up), so it is
		// never seen falling in front of the player or hanging in the air before it is moved onto the rack.
		class LoadedSink final : public RE::BSTEventSink<RE::TESObjectLoadedEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
			{
				if (a_event && a_event->loaded && a_event->formID != 0 && a_event->formID == g_hidden.load()) {
					Cull(a_event->formID, true);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		LoadedSink g_loadedSink;
	}

	void HideDropped(RE::TESObjectREFR* a_item)
	{
		static const bool registered = [] {
			auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
			if (!holder) {
				logger::warn("Displays: no script event source; a weapon dropped for a rack shows until its 3D is seen");
				return false;
			}
			holder->AddEventSink<RE::TESObjectLoadedEvent>(&g_loadedSink);
			logger::debug("Displays: object-loaded sink registered (weapons for racks stay hidden until they are up)");
			return true;
		}();
		(void)registered;
		if (!a_item) {
			return;
		}
		const auto id = a_item->GetFormID();
		if (const auto previous = g_hidden.exchange(id); previous != 0 && previous != id) {
			logger::debug("Displays: {:08X} was still hidden; showing it", previous);
			Cull(previous, false);
		}
		Cull(id, true);
	}

	void ShowDropped()
	{
		if (const auto id = g_hidden.exchange(0); id != 0) {
			Cull(id, false);
		}
	}
}
