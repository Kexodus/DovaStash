#include "AutoTest/Steps.h"

#include "AutoTest/LogCapture.h"
#include "Controller.h"
#include "Events/EventSinks.h"
#include "Game/Displays.h"
#include "Game/Eligibility.h"
#include "Game/FormUtil.h"
#include "Game/ItemMover.h"
#include "Home/HomeRegistry.h"
#include "Home/Store.h"
#include "Hooks/CraftingHooks.h"
#include "Input/InputHandler.h"
#include "Settings.h"
#include "Sort/CraftingStorage.h"
#include "Sort/DisplayFiller.h"
#include "UI/ArmorSetMenu.h"
#include "UI/MessageBox.h"
#include "UI/Panel/DisplaysModel.h"
#include "UI/Panel/KeyCapture.h"
#include "UI/Panel/PanelModel.h"
#include "UI/UIExtensions.h"

#include <fstream>
#include <regex>
#include <sstream>

namespace dova::autotest
{
	void Context::Record(bool a_ok, std::string a_what, std::string a_detail)
	{
		if (a_ok) {
			logger::info("AutoTest:   ok   {}{}", a_what, a_detail.empty() ? "" : std::format(" ({})", a_detail));
		} else {
			logger::warn("AutoTest:   FAIL {}{}", a_what, a_detail.empty() ? "" : std::format(": {}", a_detail));
		}
		if (test) {
			test->checks.push_back(CheckRecord{ a_ok, std::move(a_what), std::move(a_detail) });
		}
	}

	namespace
	{
		constexpr auto kLayoutDir = "Data/SKSE/Plugins/DovaStash/Layouts"sv;
		constexpr auto kUserTemplateDir = "Data/SKSE/Plugins/DovaStash/Templates/User"sv;

		// --- small helpers ---------------------------------------------------------------------------------

		std::string Text(const Json& a_value)
		{
			return a_value.is_string() ? a_value.get<std::string>() : a_value.dump();
		}

		std::optional<core::FormRef> Ref(const Json& a_value)
		{
			return a_value.is_string() ? core::FormRef::Parse(a_value.get<std::string>()) : std::nullopt;
		}

		RE::TESBoundObject* Item(const Json& a_step, std::string_view a_key = "item")
		{
			const auto it = a_step.find(a_key);
			if (it == a_step.end()) {
				return nullptr;
			}
			const auto ref = Ref(*it);
			auto*      form = ref ? game::LookupForm(*ref) : nullptr;
			return form ? form->As<RE::TESBoundObject>() : nullptr;
		}

		std::string Name(const RE::TESForm* a_form)
		{
			return a_form ? game::Describe(a_form) : "(none)"s;
		}

		home::Home* CurrentHome()
		{
			return home::HomeRegistry::Get().Current();
		}

		RE::TESObjectCELL* PlayerCell()
		{
			auto* player = game::Player();
			return player ? player->GetParentCell() : nullptr;
		}

		// "home", "displays", "settings", "about": a panel page (the panel check's status, panelVisible).
		ui::panel::Page PanelPage(std::string_view a_name)
		{
			if (core::IEquals(a_name, "displays")) {
				return ui::panel::Page::Displays;
			}
			if (core::IEquals(a_name, "settings")) {
				return ui::panel::Page::Settings;
			}
			if (core::IEquals(a_name, "about")) {
				return ui::panel::Page::About;
			}
			return ui::panel::Page::Home;
		}

		// "player", "master" (the current home's master), or a FormRef.
		RE::TESObjectREFR* Holder(const Json& a_value)
		{
			const auto text = a_value.is_string() ? a_value.get<std::string>() : "player"s;
			if (core::IEquals(text, "player")) {
				return game::Player();
			}
			if (core::IEquals(text, "master")) {
				auto* home = CurrentHome();
				return home && home->layout && home->layout->master ? game::LookupRef(*home->layout->master) : nullptr;
			}
			const auto ref = core::FormRef::Parse(text);
			return ref ? game::LookupRef(*ref) : nullptr;
		}

		std::string HolderName(const Json& a_value)
		{
			return a_value.is_string() ? a_value.get<std::string>() : "player"s;
		}

		// Failure text when `a_value` does not meet the step's eq / ne / ge / gt / le / lt; nullopt when it does.
		std::optional<std::string> Compare(std::int64_t a_value, const Json& a_step)
		{
			static constexpr std::array<std::pair<std::string_view, std::string_view>, 6> kOps{ { { "eq", "==" }, { "ne", "!=" }, { "ge", ">=" },
				{ "gt", ">" }, { "le", "<=" }, { "lt", "<" } } };
			for (const auto& [key, symbol] : kOps) {
				const auto it = a_step.find(key);
				if (it == a_step.end() || !it->is_number()) {
					continue;
				}
				const auto want = it->get<std::int64_t>();
				const bool ok = key == "eq" ? a_value == want : key == "ne" ? a_value != want : key == "ge" ? a_value >= want :
				                key == "gt" ? a_value > want : key == "le" ? a_value <= want : a_value < want;
				if (!ok) {
					return std::format("is {}, expected {} {}", a_value, symbol, want);
				}
			}
			return std::nullopt;
		}

		std::string Expectation(const Json& a_step)
		{
			std::string out;
			for (const auto* key : { "eq", "ne", "ge", "gt", "le", "lt" }) {
				if (const auto it = a_step.find(key); it != a_step.end()) {
					out += std::format(" {} {}", key, it->dump());
				}
			}
			return out;
		}

		std::string ThreadId()
		{
			std::ostringstream out;
			out << std::this_thread::get_id();
			return out.str();
		}

		// The menus that are open, for failure details ("HUD Menu, Cursor Menu").
		std::string OpenMenus()
		{
			std::string out;
			auto*       ui = RE::UI::GetSingleton();
			if (!ui) {
				return out;
			}
			for (const auto& [name, entry] : ui->menuMap) {
				if (ui->IsMenuOpen(name.c_str())) {
					out += (out.empty() ? "" : ", ") + std::string(name.c_str());
				}
			}
			return out;
		}

		// --- inventories -----------------------------------------------------------------------------------

		bool IsTempered(const RE::ExtraDataList* a_x)
		{
			const auto* health = a_x->GetByType<RE::ExtraHealth>();
			return health && health->health > 1.0001f;
		}

		bool IsRenamed(const RE::ExtraDataList* a_x, std::string_view a_name = {})
		{
			const auto* text = a_x->GetByType<RE::ExtraTextDisplayData>();
			if (!text || text->ownerInstance.get() != RE::ExtraTextDisplayData::DisplayDataType::kCustomName) {
				return false;
			}
			return a_name.empty() || std::string_view(text->displayName.c_str()).find(a_name) != std::string_view::npos;
		}

		bool IsStolen(const RE::ExtraDataList* a_x)
		{
			const auto* owner = a_x->GetByType<RE::ExtraOwnership>();
			return owner && owner->owner && owner->owner->GetFormID() != game::kPlayerBaseID;
		}

		bool IsWorn(const RE::ExtraDataList* a_x)
		{
			return a_x->HasType<RE::ExtraWorn>() || a_x->HasType<RE::ExtraWornLeft>();
		}

		bool IsSpecial(const RE::ExtraDataList* a_x)
		{
			return IsTempered(a_x) || IsRenamed(a_x) || IsStolen(a_x) || IsWorn(a_x) || a_x->HasType<RE::ExtraHotkey>() ||
			       a_x->HasType<RE::ExtraEnchantment>() || a_x->HasType<RE::ExtraSoul>();
		}

		// Copies of `a_object` in `a_ref`, all of them or only those matching `a_filter` ("plain", "tempered",
		// "renamed" or "renamed:<text>", "stolen", "worn", "favorite").
		std::int32_t CountIn(RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_object, std::string_view a_filter = {})
		{
			if (!a_ref || !a_object) {
				return 0;
			}
			const auto inventory = a_ref->GetInventory([&](RE::TESBoundObject& a_o) { return &a_o == a_object; });
			const auto it = inventory.find(a_object);
			if (it == inventory.end()) {
				return 0;
			}
			const auto total = it->second.first;
			if (a_filter.empty()) {
				return total;
			}
			const auto* entry = it->second.second.get();
			std::int32_t matching = 0;
			std::int32_t special = 0;
			if (entry && entry->extraLists) {
				for (const auto* x : *entry->extraLists) {
					if (!x) {
						continue;
					}
					const auto n = x->GetCount();
					if (IsSpecial(x)) {
						special += n;
					}
					bool match = false;
					if (a_filter == "tempered") {
						match = IsTempered(x);
					} else if (a_filter.starts_with("renamed")) {
						match = IsRenamed(x, a_filter.size() > 8 ? a_filter.substr(8) : std::string_view{});
					} else if (a_filter == "stolen") {
						match = IsStolen(x);
					} else if (a_filter == "worn") {
						match = IsWorn(x);
					} else if (a_filter == "favorite") {
						match = x->HasType<RE::ExtraHotkey>();
					}
					if (match) {
						matching += n;
					}
				}
			}
			return a_filter == "plain" ? total - special : matching;
		}

		// Harness ignore list (every snapshot, every plan): forms that are no items. A container's base inventory can
		// list a leveled list (LVLI, e.g. Skyrim.esm|0x0010FCFB LItemBarrelFoodSame70 in a Hearthfire barrel) until the
		// engine resolves it into items when the container is first opened or resets; it then "disappears". Not a move
		// of Dova Stash's: left out.
		bool IsEngineToken(const RE::TESBoundObject* a_object)
		{
			return a_object->Is(RE::FormType::LeveledItem);
		}

		void Collect(Snapshot& a_snap, const std::string& a_holder, RE::TESObjectREFR* a_ref)
		{
			if (!a_ref) {
				return;
			}
			auto& counts = a_snap.holders[a_holder];
			for (const auto& [object, count] : a_ref->GetInventoryCounts()) {
				if (object && count > 0 && !IsEngineToken(object)) {
					const auto ref = game::ToFormRef(object);
					counts[ref ? ref->ToString() : game::Describe(object)] += count;
				}
			}
		}

		// The entries of the hotkey menu that is up: the pending UIExtensions list, else the open message box's
		// buttons (the last box Dova Stash queued). Empty when neither is up.
		std::vector<std::string> MenuEntries(std::string* a_source = nullptr)
		{
			if (auto list = ui::uiext::PendingList(); list.serial != 0) {
				if (a_source) {
					*a_source = "list";
				}
				return list.entries;
			}
			if (!MessageBoxText().empty()) {
				if (a_source) {
					*a_source = "message box";
				}
				return ui::LastMessageBoxButtons();
			}
			return {};
		}

		// The entry `a_text` names: the one equal to it, else the first that starts with it. -1 when none.
		std::int32_t FindEntry(const std::vector<std::string>& a_entries, const std::string& a_text)
		{
			for (std::size_t i = 0; i < a_entries.size(); ++i) {
				if (a_entries[i] == a_text) {
					return static_cast<std::int32_t>(i);
				}
			}
			for (std::size_t i = 0; i < a_entries.size(); ++i) {
				if (a_entries[i].starts_with(a_text)) {
					return static_cast<std::int32_t>(i);
				}
			}
			return -1;
		}

		// A weapon or shield lying in the world: what a weapon rack holds (the item itself, moved onto it). Not books
		// or plaque items: a bookshelf shows copies of the books it holds, which would count twice.
		bool IsLooseItem(RE::TESObjectREFR* a_ref)
		{
			const auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
			if (!base || a_ref->IsDisabled() || a_ref->IsDeleted()) {
				return false;
			}
			const auto* armor = base->As<RE::TESObjectARMO>();
			return base->Is(RE::FormType::Weapon) || (armor && armor->IsShield());
		}

		// Where the racks of `a_displays` hold their weapon: each trigger's bounds (the test Displays' RackHoldsItem
		// uses). A loose weapon anywhere else (a pickaxe by a Hearthfire woodpile) is the game's: NPCs take it.
		std::vector<std::pair<RE::NiPoint3, float>> RackZones(const std::vector<game::FoundDisplay>& a_displays)
		{
			std::vector<std::pair<RE::NiPoint3, float>> zones;
			for (const auto& d : a_displays) {
				auto* trigger = d.kind == core::DisplayKind::Rack ? RE::TESForm::LookupByID<RE::TESObjectREFR>(d.trigger) : nullptr;
				auto* root = trigger ? trigger->Get3D() : nullptr;
				if (!root) {
					continue;
				}
				const bool bounded = root->worldBound.radius > 1.0f;
				zones.emplace_back(bounded ? root->worldBound.center : root->world.translate, bounded ? std::clamp(root->worldBound.radius, 48.0f, 160.0f) : 96.0f);
			}
			return zones;
		}

		// The player, every container of the player's cell, what its displays hold (a mannequin's worn armor;
		// a rack's weapon lies in the world: every weapon and shield within a rack's trigger bounds, as "world")
		// and, in a home over several cells (DESIGN §6.7), the layout's containers in its other cells that are in
		// memory (persistent ones): a Deposit from the grounds into the hall, or onto a mannequin, is a move, not a
		// loss. Other loose items are left out: NPCs pick them up or eat them, which is not Dova Stash's doing.
		Snapshot TakeSnapshot()
		{
			Snapshot snap;
			Collect(snap, "player", game::Player());
			std::set<RE::TESObjectREFR*> seen;
			if (auto* cell = PlayerCell()) {
				auto&      world = snap.holders["world"];
				const auto displays = game::FindDisplays(cell);
				const auto zones = RackZones(displays);
				const auto onRack = [&](RE::TESObjectREFR* a_ref) {
					return std::ranges::any_of(zones, [&](const auto& a_zone) { return a_ref->GetPosition().GetDistance(a_zone.first) <= a_zone.second; });
				};
				cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
					if (a_ref && game::IsContainerRef(a_ref) && !a_ref->IsDisabled()) {
						const auto ref = game::ToFormRef(a_ref);
						Collect(snap, ref ? ref->ToString() : game::Describe(a_ref), a_ref);
						seen.insert(a_ref);
					} else if (IsLooseItem(a_ref) && onRack(a_ref)) {
						auto*      base = a_ref->GetBaseObject();
						const auto ref = game::ToFormRef(base);
						world[ref ? ref->ToString() : game::Describe(base)] += std::max<std::int32_t>(1, a_ref->extraList.GetCount());
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
				for (const auto& d : displays) {
					auto* object = RE::TESForm::LookupByID<RE::TESObjectREFR>(d.ref);
					if (d.kind == core::DisplayKind::Mannequin && object && !seen.contains(object)) {
						Collect(snap, "mannequin " + game::Describe(object), object);
						seen.insert(object);
					}
				}
			}
			if (auto* home = CurrentHome(); home && home->layout) {
				auto add = [&](const core::FormRef& a_ref) {
					auto* object = game::LookupRef(a_ref);
					if (object && !seen.contains(object) && game::IsContainerRef(object) && !object->IsDisabled()) {
						Collect(snap, a_ref.ToString(), object);
						seen.insert(object);
					}
				};
				if (home->layout->master) {
					add(*home->layout->master);
				}
				for (const auto& c : home->layout->containers) {
					add(c.ref);
				}
			}
			return snap;
		}

		std::map<std::string, std::int32_t> Totals(const Snapshot& a_snap)
		{
			std::map<std::string, std::int32_t> out;
			for (const auto& [holder, counts] : a_snap.holders) {
				for (const auto& [item, n] : counts) {
					out[item] += n;
				}
			}
			return out;
		}

		// --- files -------------------------------------------------------------------------------------------

		std::filesystem::path LayoutFile(const core::FormRef& a_cell)
		{
			return home::Store::Get().LayoutPath(a_cell);
		}

		std::optional<std::filesystem::path> BackupFile(const core::FormRef& a_cell)
		{
			const auto& dir = home::Store::BackupDir();
			if (!dir) {
				return std::nullopt;
			}
			const auto name = core::LayoutFileName(a_cell);
			return *dir / std::filesystem::path(std::u8string(name.begin(), name.end()));
		}

		std::optional<Json> ReadJson(const std::filesystem::path& a_path, std::string& a_error)
		{
			std::ifstream in(a_path, std::ios::binary);
			if (!in) {
				a_error = "file not found";
				return std::nullopt;
			}
			try {
				return Json::parse(in);
			} catch (const std::exception& e) {
				a_error = std::format("not JSON: {}", e.what());
				return std::nullopt;
			}
		}

		std::filesystem::path DirOf(std::string_view a_name)
		{
			if (core::IEquals(a_name, "Backups")) {
				return home::Store::BackupDir().value_or(std::filesystem::path{});
			}
			if (core::IEquals(a_name, "TemplatesUser")) {
				return std::filesystem::path{ kUserTemplateDir };
			}
			return std::filesystem::path{ kLayoutDir };
		}

		// `a_want` is in `a_have`: equal scalars, a substring of a string, an object whose keys all match, or (for
		// arrays) an element that does.
		bool Contains(const Json& a_have, const Json& a_want)
		{
			if (a_want.is_array()) {
				return a_have.is_array() && std::ranges::all_of(a_want, [&](const Json& w) { return Contains(a_have, w); });
			}
			if (a_have.is_array()) {
				return std::ranges::any_of(a_have, [&](const Json& e) { return Contains(e, a_want); });
			}
			if (a_want.is_object() && a_have.is_object()) {
				for (const auto& [key, value] : a_want.items()) {
					const auto it = a_have.find(key);
					if (it == a_have.end() || !(value.is_object() || value.is_array() ? Contains(*it, value) : *it == value)) {
						return false;
					}
				}
				return true;
			}
			if (a_want.is_string() && a_have.is_string()) {
				return core::IEquals(a_have.get<std::string>(), a_want.get<std::string>()) ||
				       a_have.get<std::string>().find(a_want.get<std::string>()) != std::string::npos;
			}
			return a_have == a_want;
		}

		CheckRecord JsonCheck(const std::string& a_what, const std::optional<Json>& a_doc, const std::string& a_error, const Json& a_step)
		{
			CheckRecord r{ false, a_what, {} };
			if (!a_doc) {
				r.ok = a_step.value("absent", false) && a_error == "file not found";
				r.detail = a_error;
				return r;
			}
			const auto pointerText = a_step.value("pointer", ""s);
			Json::json_pointer pointer;
			try {
				pointer = Json::json_pointer(pointerText);
			} catch (const std::exception& e) {
				r.detail = std::format("bad pointer: {}", e.what());
				return r;
			}
			const bool exists = a_doc->contains(pointer);
			if (a_step.value("absent", false)) {
				r.ok = !exists;
				r.detail = exists ? std::format("is {}", (*a_doc)[pointer].dump()) : "";
				return r;
			}
			if (!exists) {
				r.detail = "missing";
				return r;
			}
			const auto& value = (*a_doc)[pointer];
			r.ok = true;
			if (const auto it = a_step.find("eq"); it != a_step.end()) {
				r.ok = value == *it || (value.is_string() && it->is_string() && core::IEquals(value.get<std::string>(), it->get<std::string>()));
			}
			if (const auto it = a_step.find("contains"); r.ok && it != a_step.end()) {
				r.ok = Contains(value, *it);
			}
			if (const auto it = a_step.find("lacks"); r.ok && it != a_step.end()) {
				r.ok = !Contains(value, *it);
			}
			if (const auto it = a_step.find("size"); r.ok && it != a_step.end()) {
				r.ok = (value.is_array() || value.is_object()) && value.size() == it->get<std::size_t>();
			}
			if (!r.ok) {
				auto text = value.dump();
				if (text.size() > 600) {
					text = text.substr(0, 600) + "...";
				}
				r.detail = std::format("is {}", text);
			}
			return r;
		}

		// --- checks ----------------------------------------------------------------------------------------

		// Records `a_eval` now, or (with "within": seconds) keeps evaluating it until it passes or time is up.
		std::optional<Wait> Check(const Json& a_step, Context& a_ctx, std::function<CheckRecord()> a_eval)
		{
			const double within = a_step.value("within", 0.0);
			auto         first = a_eval();
			if (first.ok || within <= 0.0) {
				a_ctx.Record(first.ok, std::move(first.what), std::move(first.detail));
				return std::nullopt;
			}
			auto last = std::make_shared<CheckRecord>(std::move(first));
			Wait wait;
			wait.what = last->what;
			wait.deadline = Clock::now() + std::chrono::milliseconds(static_cast<std::int64_t>(within * 1000.0));
			wait.done = [a_eval, last]() {
				*last = a_eval();
				return last->ok;
			};
			wait.finish = [&a_ctx, last](bool) { a_ctx.Record(last->ok, last->what, last->detail); };
			return wait;
		}

		std::uint64_t SinceMark(const Json& a_step, const Context& a_ctx, std::string_view a_default = "test")
		{
			const auto since = a_step.value("since", std::string(a_default));
			return since == "run"    ? a_ctx.runMark :
			       since == "plan"   ? a_ctx.planMark :
			       since == "step"   ? a_ctx.stepMark :
			       since == "action" ? a_ctx.actionMark :
			                           a_ctx.testMark;
		}

		CheckRecord LogCheck(const Json& a_step, std::uint64_t a_since, std::string a_prefix)
		{
			const auto        contains = a_step.value("contains", ""s);
			const auto        regexText = a_step.value("regex", ""s);
			const bool        absent = a_step.value("absent", false);
			std::optional<std::regex> pattern;
			if (!regexText.empty()) {
				pattern.emplace(regexText, std::regex::ECMAScript | std::regex::icase);
			}
			std::int64_t matches = 0;
			std::string  firstMatch;
			std::optional<double> number;
			for (const auto& line : logcap::Since(a_since)) {
				if (!a_prefix.empty() && !line.text.starts_with(a_prefix)) {
					continue;
				}
				bool hit = false;
				if (pattern) {
					std::smatch m;
					hit = std::regex_search(line.text, m, *pattern);
					if (hit && m.size() > 1 && !number) {
						try {
							number = std::stod(m[1].str());
						} catch (...) {
						}
					}
				} else {
					hit = line.text.find(contains) != std::string::npos;
				}
				if (hit) {
					++matches;
					if (firstMatch.empty()) {
						firstMatch = line.text;
					}
				}
			}
			const auto what = std::format("{}{} \"{}\"", a_prefix.empty() ? "log" : "notify", absent ? " lacks" : " has",
				pattern ? regexText : contains);
			CheckRecord r{ false, what, {} };
			if (absent) {
				r.ok = matches == 0;
				r.detail = r.ok ? "" : std::format("found: {}", firstMatch);
				return r;
			}
			if (const auto it = a_step.find("count"); it != a_step.end()) {
				r.ok = matches == it->get<std::int64_t>();
				r.detail = std::format("{} line(s){}", matches, firstMatch.empty() ? "" : ": " + firstMatch);
				return r;
			}
			r.ok = matches > 0;
			if (r.ok && a_step.contains("max")) {
				r.ok = number && *number <= a_step["max"].get<double>();
				r.detail = number ? std::format("{} (max {})", *number, a_step["max"].get<double>()) : "no number captured";
				return r;
			}
			r.detail = r.ok ? firstMatch : "not found";
			return r;
		}

		std::optional<Wait> RunCheck(const std::string& a_kind, const Json& s, Context& c)
		{
			if (a_kind == "count") {
				return Check(s, c, [s, &c]() {
					auto*       holder = Holder(s.value("in", Json("player")));
					auto*       item = Item(s);
					const auto  filter = s.value("filter", ""s);
					CheckRecord r{ false, std::format("count {}{} in {}{}", Name(item), filter.empty() ? "" : " (" + filter + ")", HolderName(s.value("in", Json("player"))),
											  Expectation(s)),
						{} };
					if (!holder || !item) {
						r.detail = !holder ? "holder not found" : "item not found";
						return r;
					}
					auto n = static_cast<std::int64_t>(CountIn(holder, item, filter));
					if (const auto from = s.find("from"); from != s.end()) {
						// A change since a snapshot: what was there before (a home's own clutter) does not count.
						const auto snap = c.snapshots.find(from->get<std::string>());
						const auto key = HolderName(s.value("in", Json("player")));
						const auto itemRef = game::ToFormRef(item);
						auto       holderKey = key;
						if (core::IEquals(key, "master")) {
							auto* home = CurrentHome();
							holderKey = home && home->layout && home->layout->master ? home->layout->master->ToString() : key;
						} else if (const auto ref = core::FormRef::Parse(key)) {
							holderKey = ref->ToString();
						}
						if (snap != c.snapshots.end() && itemRef) {
							const auto& holders = snap->second.holders;
							const auto  h = holders.find(holderKey);
							if (h != holders.end()) {
								const auto i = h->second.find(itemRef->ToString());
								n -= i != h->second.end() ? i->second : 0;
							}
						}
						r.what += std::format(" (change since {})", from->get<std::string>());
					}
					const auto failure = Compare(n, s);
					r.ok = !failure;
					r.detail = failure.value_or(std::format("{}", n));
					return r;
				});
			}
			if (a_kind == "gold") {
				return Check(s, c, [s, &c]() {
					auto*      gold = RE::TESForm::LookupByID<RE::TESBoundObject>(0xF);
					const auto now = CountIn(game::Player(), gold);
					CheckRecord r{ false, "gold", {} };
					std::int64_t want = 0;
					if (const auto snap = s.find("snapshot"); snap != s.end()) {
						const auto it = c.snapshots.find(snap->get<std::string>());
						if (it == c.snapshots.end()) {
							r.detail = "no such snapshot";
							return r;
						}
						const auto& player = it->second.holders["player"];
						const auto  g = player.find("Skyrim.esm|0x0000000F");
						want = g != player.end() ? g->second : 0;
						const auto quoted = c.quote ? c.quote->gold : 0;
						want += s.value("plusQuote", false) ? quoted : 0;
						want -= s.value("minusQuote", false) ? quoted : 0;
						want += s.value("plus", 0);
						r.what = std::format("gold == snapshot {}{}", snap->get<std::string>(), s.value("plusQuote", false) ? " + quote" : s.value("minusQuote", false) ? " - quote" : "");
						r.ok = now == want;
						r.detail = std::format("is {}, expected {}", now, want);
						return r;
					}
					const auto failure = Compare(now, s);
					r.what += Expectation(s);
					r.ok = !failure;
					r.detail = failure.value_or(std::format("{}", now));
					return r;
				});
			}
			if (a_kind == "log" || a_kind == "notify") {
				const auto since = SinceMark(s, c);
				const auto prefix = a_kind == "notify" ? "Notify: "s : ""s;
				auto       step = s;
				if (!s.contains("within") && !s.value("absent", false)) {
					step["within"] = 3.0;  // most lines come from a task or a short delay
				}
				return Check(step, c, [step, since, prefix]() { return LogCheck(step, since, prefix); });
			}
			if (a_kind == "noErrors") {
				const auto since = SinceMark(s, c);
				return Check(s, c, [s, since]() {
					std::vector<std::string> except;
					for (const auto& e : s.value("except", Json::array())) {
						except.push_back(Text(e));
					}
					std::vector<std::string> errors;
					for (const auto& line : logcap::Since(since)) {
						if (line.level >= spdlog::level::err &&
							std::ranges::none_of(except, [&](const std::string& e) { return line.text.find(e) != std::string::npos; })) {
							errors.push_back(line.text);
						}
					}
					return CheckRecord{ errors.empty(), "no [error] lines", errors.empty() ? "" : std::format("{} error(s), first: {}", errors.size(), errors.front()) };
				});
			}
			if (a_kind == "layout") {
				const auto cell = s.contains("cell") ? Ref(s["cell"]).value_or(c.home) : c.home;
				return Check(s, c, [s, cell]() {
					std::string error;
					const auto  doc = ReadJson(LayoutFile(cell), error);
					return JsonCheck(std::format("layout {}{}", s.value("pointer", ""s), s.contains("eq") ? " == " + s["eq"].dump() :
																								s.contains("contains") ? " has " + s["contains"].dump() :
																								s.contains("lacks") ? " lacks " + s["lacks"].dump() :
																								s.value("absent", false) ? " absent" : ""),
						doc, error, s);
				});
			}
			if (a_kind == "templateFile") {
				return Check(s, c, [s]() {
					const auto  name = s.value("name", ""s);
					const auto  path = std::filesystem::path{ kUserTemplateDir } / std::filesystem::path(std::u8string(name.begin(), name.end()));
					std::string error;
					const auto  doc = ReadJson(path, error);
					return JsonCheck(std::format("template {} {}{}", name, s.value("pointer", ""s), s.contains("eq") ? " == " + s["eq"].dump() : ""), doc, error, s);
				});
			}
			if (a_kind == "backupFile") {
				return Check(s, c, [s, home = c.home]() {
					const auto  path = BackupFile(home);
					std::string error = "no backup folder";
					const auto  doc = path ? ReadJson(*path, error) : std::nullopt;
					return JsonCheck(std::format("backup copy {}", s.value("pointer", ""s)), doc, error, s);
				});
			}
			if (a_kind == "layoutFile") {
				const auto cell = s.contains("cell") ? Ref(s["cell"]).value_or(c.home) : c.home;
				return Check(s, c, [s, cell]() {
					std::error_code ec;
					const bool      exists = std::filesystem::exists(LayoutFile(cell), ec);
					const bool      want = s.value("exists", true);
					std::string     content;
					if (exists && s.contains("text")) {
						std::ifstream     in(LayoutFile(cell), std::ios::binary);
						std::stringstream buffer;
						buffer << in.rdbuf();
						content = buffer.str();
					}
					const bool textOk = !s.contains("text") || content == s["text"].get<std::string>();
					return CheckRecord{ exists == want && textOk, std::format("layout file {}{}", want ? "exists" : "absent", s.contains("text") ? " with its text unchanged" : ""),
						std::format("exists: {}{}", exists, textOk ? "" : ", text changed") };
				});
			}
			if (a_kind == "files") {
				return Check(s, c, [s]() {
					const auto  dir = DirOf(s.value("dir", "Layouts"s));
					const auto  regexText = s.value("regex", ".*"s);
					std::regex  pattern(regexText, std::regex::ECMAScript | std::regex::icase);
					std::int64_t n = 0;
					std::error_code ec;
					for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
						const auto name = it->path().filename().string();
						if (std::regex_match(name, pattern)) {
							++n;
						}
					}
					const auto failure = Compare(n, s);
					return CheckRecord{ !failure, std::format("files {}/{}{}", s.value("dir", "Layouts"s), regexText, Expectation(s)), failure.value_or(std::format("{}", n)) };
				});
			}
			if (a_kind == "menuEntries") {
				// The hotkey menu that is up: "contains" (each text starts an entry), "lacks" (no entry starts with
				// it), "eq" (exactly these entries, in order), "order" (entries starting with these texts, in this order,
				// others between them allowed), "body" (a message box's text contains it).
				return Check(s, c, [s]() {
					std::string source;
					const auto  entries = MenuEntries(&source);
					CheckRecord r{ true, "hotkey menu entries", {} };
					std::string problems;
					for (const auto& want : s.value("contains", Json::array())) {
						if (FindEntry(entries, want.get<std::string>()) < 0) {
							problems += std::format("{}missing \"{}\"", problems.empty() ? "" : "; ", want.get<std::string>());
						}
					}
					for (const auto& unwanted : s.value("lacks", Json::array())) {
						if (FindEntry(entries, unwanted.get<std::string>()) >= 0) {
							problems += std::format("{}has \"{}\"", problems.empty() ? "" : "; ", unwanted.get<std::string>());
						}
					}
					if (s.contains("eq") && s["eq"].get<std::vector<std::string>>() != entries) {
						problems += std::format("{}not exactly {}", problems.empty() ? "" : "; ", s["eq"].dump());
					}
					std::int32_t last = -1;
					for (const auto& want : s.value("order", Json::array())) {
						const auto text = want.get<std::string>();
						std::int32_t at = -1;
						for (std::size_t i = static_cast<std::size_t>(last + 1); i < entries.size(); ++i) {
							if (entries[i].starts_with(text)) {
								at = static_cast<std::int32_t>(i);
								break;
							}
						}
						if (at < 0) {
							problems += std::format("{}\"{}\" not after entry {}", problems.empty() ? "" : "; ", text, last);
							break;
						}
						last = at;
					}
					if (s.contains("body")) {
						const auto body = MessageBoxText();
						if (body.find(s["body"].get<std::string>()) == std::string::npos) {
							problems += std::format("{}body is \"{}\"", problems.empty() ? "" : "; ", body);
						}
					}
					std::string shown;
					for (const auto& e : entries) {
						shown += std::format("{}\"{}\"", shown.empty() ? "" : ", ", e);
					}
					r.ok = !entries.empty() && problems.empty();
					r.detail = entries.empty() ? "no hotkey menu is up" : std::format("{} ({}: {})", problems, source, shown);
					return r;
				});
			}
			if (a_kind == "panel") {
				// What the Menu Framework panel shows for this home, from a snapshot built now (main thread): a template's
				// row ("template": source, with "hidden" / "applied" / "best" true or false), an item in the keep
				// rules' Add item picker ("inventoryHas": form ref), the container marked aimed ("aimed": form ref, or
				// null for none), whether Use template asks first ("ownChanges": bool), a row of All templates
				// ("catalogue": {"source", "installed"}: Only installed homes shows it), the name a keep rule's item shows
				// ("keepEntry": form ref, "name": text; "" = not loaded) and the display marked aimed on the Displays page
				// ("displayAimed": form ref, or null; the page's snapshot is rebuilt in a task: use "within").
				return Check(s, c, [s]() {
					auto& model = ui::panel::PanelModel::Get();
					model.Rebuild(ui::panel::Refresh::Light);
					const auto  snap = model.Home();
					CheckRecord r{ false, "panel", {} };
					if (!snap) {
						r.detail = "no snapshot";
						return r;
					}
					if (s.contains("template")) {
						const auto source = s["template"].get<std::string>();
						r.what = std::format("panel template {}", source);
						const auto it = std::ranges::find(snap->templates, source, &ui::panel::TemplateOption::source);
						if (it == snap->templates.end()) {
							r.detail = "not listed for this home";
							return r;
						}
						r.ok = true;
						for (const auto& [key, value] : std::initializer_list<std::pair<const char*, bool>>{ { "hidden", it->hidden }, { "applied", it->applied }, { "best", it->best } }) {
							if (s.contains(key)) {
								r.what += std::format(" {} {}", key, s[key].get<bool>());
								if (s[key].get<bool>() != value) {
									r.ok = false;
									r.detail += std::format("{}{} is {}", r.detail.empty() ? "" : "; ", key, value);
								}
							}
						}
						return r;
					}
					if (s.contains("inventoryHas")) {
						const auto ref = Ref(s["inventoryHas"]);
						r.what = std::format("panel keep picker lists {}", ref ? ref->ToString() : "?");
						r.ok = ref && std::ranges::any_of(snap->inventory, [&](const ui::panel::InventoryEntry& e) { return e.refText == ref->ToString(); });
						r.detail = std::format("{} items listed", snap->inventory.size());
						return r;
					}
					if (s.contains("aimed")) {
						const auto want = s["aimed"].is_null() ? std::optional<core::FormRef>{} : Ref(s["aimed"]);
						const auto* row = snap->aimedRow && *snap->aimedRow < snap->containers.size() ? &snap->containers[*snap->aimedRow] : nullptr;
						r.what = std::format("panel aimed {}", want ? want->ToString() : "none"s);
						r.ok = want ? row && row->ref == *want : row == nullptr;
						r.detail = row ? std::format("aimed is {} ({})", row->refText, row->managed ? "managed" : "not managed") : "nothing aimed";
						if (s.contains("managed") && row) {
							// An unmanaged container the player looks at gets a row of its own.
							r.what += std::format(" managed {}", s["managed"].get<bool>());
							r.ok = r.ok && row->managed == s["managed"].get<bool>();
						}
						return r;
					}
					if (s.contains("keepEntry")) {
						const auto ref = Ref(s["keepEntry"]);
						const auto want = s.value("name", ""s);
						const auto it = ref ? snap->keepNames.find(ref->ToString()) : snap->keepNames.end();
						r.what = std::format("panel keep rule {} shows \"{}\"", ref ? ref->ToString() : "?"s, want);
						r.ok = it != snap->keepNames.end() && it->second == want;
						r.detail = it == snap->keepNames.end() ? std::format("not in the keep rules ({} named)", snap->keepNames.size()) : std::format("shows \"{}\"", it->second);
						return r;
					}
					if (s.contains("displayAimed")) {
						const auto want = s["displayAimed"].is_null() ? std::optional<core::FormRef>{} : Ref(s["displayAimed"]);
						auto&      displays = ui::panel::DisplaysModel::Get();
						displays.RequestRefresh(true);
						const auto dsnap = displays.Snapshot();
						const auto* row = dsnap && dsnap->aimedRow && *dsnap->aimedRow < dsnap->rows.size() ? &dsnap->rows[*dsnap->aimedRow] : nullptr;
						r.what = std::format("panel displays aimed {}", want ? want->ToString() : "none"s);
						r.ok = want ? row && row->ref == *want : dsnap && row == nullptr;
						r.detail = row ? std::format("aimed is {}", row->refText) : dsnap ? "nothing aimed"s : "no snapshot"s;
						return r;
					}
					if (s.contains("ownChanges")) {
						const bool want = s["ownChanges"].get<bool>();
						r.what = std::format("panel ownChanges {}", want);
						r.ok = snap->ownChanges == want;
						r.detail = std::format("is {}", snap->ownChanges);
						return r;
					}
					if (s.contains("catalogue")) {
						const auto& cat = s["catalogue"];
						const auto  source = cat.value("source", ""s);
						model.BuildGlobal();
						const auto global = model.Global();
						r.what = std::format("panel All templates lists {}", source);
						const ui::panel::TemplateInfo* info = nullptr;
						if (global) {
							for (const auto& t : global->templates) {
								if (core::IEquals(t.source, source)) {
									info = &t;
									break;
								}
							}
						}
						if (!info) {
							r.detail = "not listed";
							return r;
						}
						r.ok = true;
						if (cat.contains("installed")) {
							r.what += std::format(" installed {}", cat["installed"].get<bool>());
							r.ok = info->Installed() == cat["installed"].get<bool>();
							r.detail = std::format("installed is {} (home loaded {}, {} missing)", info->Installed(), info->homeLoaded, info->missing.size());
						}
						return r;
					}
					if (s.contains("status")) {
						// The status line of a page ("page": home, displays, settings, about): "contains" (text), "empty"
						// (true: nothing shown there), "error" (red). Only filled while the panel is drawn (panelVisible).
						const auto& st = s["status"];
						const auto  pageName = st.value("page", "home"s);
						const auto  text = model.Status(PanelPage(pageName));
						const bool  error = model.StatusIsError();
						r.what = std::format("panel status on {} {}", pageName, st.dump());
						r.ok = true;
						if (st.contains("contains")) {
							r.ok = r.ok && text.find(st["contains"].get<std::string>()) != std::string::npos;
						}
						if (st.value("empty", false)) {
							r.ok = r.ok && text.empty();
						}
						if (st.contains("error")) {
							r.ok = r.ok && !text.empty() && error == st["error"].get<bool>();
						}
						r.detail = text.empty() ? "no status" : std::format("\"{}\"{}", text, error ? " (red)" : "");
						return r;
					}
					r.detail = "nothing to check (template, inventoryHas, aimed, keepEntry, displayAimed, ownChanges, catalogue or status)";
					return r;
				});
			}
			if (a_kind == "displayHolds") {
				// What a display holds now: "ref" (the display: rack activator, mannequin, case or bookshelf container) and
				// "item" (a base form; "holds": false for "does not hold it"). A mannequin, case or bookshelf: its inventory
				// (game::DisplayHolds); a rack: a loose reference of the item inside its trigger's bounds. "refs" (an array in
				// place of "ref"): one of those displays holds it (none of them, with "holds": false), for a home with many
				// racks or shelves where the filler picks which one.
				return Check(s, c, [s]() {
					const Json refs = s.contains("refs") ? s["refs"] : Json::array({ s.value("ref", Json()) });
					auto*      item = Item(s);
					const bool want = s.value("holds", true);
					CheckRecord r{ false, std::format("display {} {} {}", s.contains("refs") ? "one of " + refs.dump() : Text(s.value("ref", Json())),
											 want ? "holds" : "does not hold", Name(item)), {} };
					if (!item) {
						r.detail = "item not found";
						return r;
					}
					bool        holds = false;
					bool        judged = false;  // at least one of them could be looked at
					std::string details;
					for (const auto& one : refs) {
						const auto ref = Ref(one);
						auto*      object = ref ? game::LookupRef(*ref) : nullptr;
						const auto found = object ? game::InspectDisplay(object) : std::nullopt;
						std::string detail;
						if (!found) {
							detail = !object ? "display not found" : "not a display Dova Stash knows";
						} else if (found->kind == core::DisplayKind::Rack) {
							auto* trigger = RE::TESForm::LookupByID<RE::TESObjectREFR>(found->trigger);
							auto* root = trigger ? trigger->Get3D() : nullptr;
							auto* cell = object->GetParentCell();
							if (!root || !cell) {
								detail = "the rack's trigger has no 3D";
							} else {
								const auto  center = root->worldBound.radius > 1.0f ? root->worldBound.center : root->world.translate;
								const float radius = std::clamp(root->worldBound.radius, 48.0f, 160.0f);
								bool        here = false;
								cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
									if (a_ref && a_ref->GetBaseObject() == item && !a_ref->IsDisabled() && a_ref->GetPosition().GetDistance(center) <= radius) {
										here = true;
										return RE::BSContainer::ForEachResult::kStop;
									}
									return RE::BSContainer::ForEachResult::kContinue;
								});
								holds = holds || here;
								judged = true;
								detail = std::format("rack {}", here ? "holds it" : "does not hold it");
							}
						} else {
							const auto held = game::DisplayHolds(*found);
							const bool here = std::ranges::find(held, item) != held.end();
							holds = holds || here;
							judged = true;
							detail = std::format("{} {}", core::DisplayKindName(found->kind), here ? "holds it" : "does not hold it");
						}
						details += (details.empty() ? "" : "; ") + (refs.size() > 1 ? Text(one) + ": " : ""s) + detail;
					}
					r.ok = judged && holds == want;
					r.detail = std::move(details);
					return r;
				});
			}
			if (a_kind == "conserved") {
				return Check(s, c, [s, &c]() {
					const auto  name = s.value("snapshot", ""s);
					CheckRecord r{ false, std::format("no item lost or gained since snapshot {}", name), {} };
					const auto  it = c.snapshots.find(name);
					if (it == c.snapshots.end()) {
						r.detail = "no such snapshot";
						return r;
					}
					std::set<std::string> ignore;
					for (const auto& e : s.value("ignore", Json::array())) {
						if (const auto ref = Ref(e)) {
							ignore.insert(ref->ToString());
						}
					}
					const auto before = Totals(it->second);
					const auto after = Totals(TakeSnapshot());
					std::set<std::string> items;
					for (const auto& [k, v] : before) {
						items.insert(k);
					}
					for (const auto& [k, v] : after) {
						items.insert(k);
					}
					std::string diffs;
					for (const auto& item : items) {
						if (ignore.contains(item)) {
							continue;
						}
						const auto b = before.contains(item) ? before.at(item) : 0;
						const auto a = after.contains(item) ? after.at(item) : 0;
						if (a != b) {
							auto* form = core::FormRef::Parse(item) ? game::LookupForm(*core::FormRef::Parse(item)) : nullptr;
							diffs += std::format("{}{} {} -> {}", diffs.empty() ? "" : "; ", form ? Name(form) : item, b, a);
						}
					}
					r.ok = diffs.empty();
					r.detail = diffs;
					return r;
				});
			}
			if (a_kind == "crafting") {
				return Check(s, c, [s]() {
					namespace storage = sort::crafting::storage;
					CheckRecord r{ true, "crafting from storage", {} };
					if (s.contains("active")) {
						const bool want = s["active"].get<bool>();
						r.what += want ? " session active" : " session not active";
						r.ok = storage::Active() == want;
						r.detail = std::format("active: {}", storage::Active());
					}
					if (r.ok && s.contains("item")) {
						auto*      item = Item(s);
						const auto n = storage::Stored(item);
						r.what += std::format(", {} stored{}", Name(item), Expectation(s));
						const auto failure = Compare(n, s);
						r.ok = !failure;
						r.detail = failure.value_or(std::format("{}", n));
					}
					return r;
				});
			}
			if (a_kind == "hooks") {
				return Check(s, c, [s]() {
					const bool want = s.value("installed", true);
					return CheckRecord{ hooks::crafting::Installed() == want, std::format("crafting hooks {}", want ? "installed" : "not installed"),
						std::format("installed: {}", hooks::crafting::Installed()) };
				});
			}
			if (a_kind == "hookedCount") {
				// The count a crafting menu gets (the hooked count of one object, SE 15869 / AE 16109): with a session
				// active, the player's own copies plus what the hook adds for storage. Asked on the UI thread, as the
				// menu does. (CommonLib's InventoryChanges::GetItemCount is another function, which the menus do not
				// use for materials and the hook does not change.)
				auto* item = Item(s);
				auto  value = std::make_shared<std::atomic<std::int64_t>>(std::numeric_limits<std::int64_t>::min());
				SKSE::GetTaskInterface()->AddUITask([item, value]() {
					auto*      player = game::Player();
					const auto n = player && item ? static_cast<std::int64_t>(CountIn(player, item) + hooks::crafting::Added(item)) : -1;
					logger::info("AutoTest: count of {} asked on the UI thread ({}): {}", Name(item), ThreadId(), n);
					*value = n;
				});
				Wait wait;
				wait.what = "count on the UI thread";
				wait.deadline = Clock::now() + 10s;
				wait.done = [value]() { return value->load() != std::numeric_limits<std::int64_t>::min(); };
				wait.finish = [s, item, value, &c](bool a_timedOut) {
					const auto  n = value->load();
					const auto  failure = a_timedOut ? std::optional<std::string>("the UI task did not run") : Compare(n, s);
					c.Record(!failure, std::format("player count of {} seen by the crafting menu (UI thread){}", Name(item), Expectation(s)),
						failure.value_or(std::format("{}", n)) + std::format(" (carried {})", CountIn(game::Player(), item)));
				};
				return wait;
			}
			if (a_kind == "hookedCountTask") {
				return Check(s, c, [s]() {
					auto*       item = Item(s);
					auto*       player = game::Player();
					CheckRecord r{ false, std::format("player count of {} seen by the crafting menu (task thread){}", Name(item), Expectation(s)), {} };
					if (!item || !player) {
						r.detail = "no item or player";
						return r;
					}
					const auto n = static_cast<std::int64_t>(CountIn(player, item) + hooks::crafting::Added(item));
					const auto failure = Compare(n, s);
					r.ok = !failure;
					r.detail = failure.value_or(std::format("{}", n)) + std::format(" (carried {})", CountIn(player, item));
					return r;
				});
			}
			if (a_kind == "menu") {
				return Check(s, c, [s]() {
					const auto name = s.value("name", ""s);
					const bool want = s.value("open", true);
					auto*      ui = RE::UI::GetSingleton();
					const bool open = ui && ui->IsMenuOpen(name);
					return CheckRecord{ open == want, std::format("{} {}", name, want ? "open" : "closed"), std::format("open: {}", open) };
				});
			}
			if (a_kind == "quote") {
				return Check(s, c, [s, &c]() {
					CheckRecord r{ false, "sale quote" + Expectation(s), {} };
					if (!c.quote) {
						r.detail = "no quote yet";
						return r;
					}
					const auto& q = *c.quote;
					auto        failure = Compare(q.items, s);
					if (!failure && s.contains("minGold") && q.gold < s["minGold"].get<std::int64_t>()) {
						failure = std::format("gold {} < {}", q.gold, s["minGold"].get<std::int64_t>());
					}
					if (!failure && s.contains("stays") && q.stays.find(s["stays"].get<std::string>()) == std::string::npos) {
						failure = std::format("stays \"{}\" lacks \"{}\"", q.stays, s["stays"].get<std::string>());
					}
					r.ok = !failure;
					r.detail = failure.value_or(std::format("{} items for {} gold: {} / {}", q.items, q.gold, q.what, q.stays));
					return r;
				});
			}
			if (a_kind == "bestTemplate") {
				return Check(s, c, [s]() {
					auto*       home = CurrentHome();
					const auto* best = home ? home->BestTemplate() : nullptr;
					// "source": null = no template is offered for this cell.
					if (s.contains("source") && s["source"].is_null()) {
						return CheckRecord{ best == nullptr, "no template offered", best ? best->source : "none" };
					}
					const auto want = s.value("source", ""s);
					return CheckRecord{ best && core::IEquals(best->source, want), std::format("best template is {}", want), best ? best->source : "none" };
				});
			}
			if (a_kind == "home") {
				return Check(s, c, [s]() {
					std::string reason;
					auto*       home = home::HomeRegistry::Get().Current(&reason);
					const bool  want = s.value("is", true);
					CheckRecord r{ (home != nullptr) == want, std::format("the player's cell is {}a home", want ? "" : "not "), home ? home->name : reason };
					if (r.ok && home && s.contains("name")) {
						r.ok = home->name == s["name"].get<std::string>();
					}
					if (r.ok && home && s.contains("layout")) {
						r.ok = home->layout.has_value() == s["layout"].get<bool>();
						r.detail += std::format(", layout: {}", home->layout.has_value());
					}
					return r;
				});
			}
			c.Record(false, std::format("unknown check \"{}\"", a_kind));
			return std::nullopt;
		}

		// --- actions ---------------------------------------------------------------------------------------

		void AbortTest(Context& a_ctx, std::string a_what, std::string a_detail)
		{
			a_ctx.Record(false, std::move(a_what), std::move(a_detail));
			a_ctx.abortTest = true;
		}

		Wait WaitFor(std::string a_what, double a_seconds, std::function<bool()> a_done, Context& a_ctx, bool a_abortOnTimeout = true,
			std::function<void()> a_then = {})
		{
			Wait wait;
			wait.what = a_what;
			wait.deadline = Clock::now() + std::chrono::milliseconds(static_cast<std::int64_t>(a_seconds * 1000.0));
			wait.done = std::move(a_done);
			wait.finish = [&a_ctx, a_what, a_seconds, a_abortOnTimeout, then = std::move(a_then)](bool a_timedOut) {
				if (a_timedOut) {
					a_ctx.Record(false, a_what, std::format("timed out after {} s", a_seconds));
					if (a_abortOnTimeout) {
						a_ctx.abortTest = true;
					}
					return;
				}
				if (then) {
					then();
				}
			};
			return wait;
		}

		void ApplySetting(Settings::Values& a_v, const std::string& a_key, const Json& a_value)
		{
			const auto i = [&]() { return a_value.is_boolean() ? (a_value.get<bool>() ? 1 : 0) : a_value.get<std::int32_t>(); };
			const auto b = [&]() { return a_value.is_boolean() ? a_value.get<bool>() : a_value.get<std::int32_t>() != 0; };
			if (a_key == "iLogLevel") {
				a_v.general.logLevel = i();
			} else if (a_key == "bAutoEnableTemplates") {
				a_v.templates.autoEnable = b();
			} else if (a_key == "bDistributeOnMasterClose") {
				a_v.sorting.distributeOnMasterClose = b();
			} else if (a_key == "bSortOnArrival") {
				a_v.sorting.sortOnArrival = b();
			} else if (a_key == "bLearn") {
				a_v.sorting.learn = b();
			} else if (a_key == "iSuggestCategoryAfter") {
				a_v.sorting.suggestCategoryAfter = i();
			} else if (a_key == "iLearnUndoSeconds") {
				a_v.sorting.learnUndoSeconds = i();
			} else if (a_key == "bFillDisplays") {
				a_v.sorting.fillDisplays = b();
			} else if (a_key == "bRestockAfterDeposit") {
				a_v.sorting.restockAfterDeposit = b();
			} else if (a_key == "iCraftingSupply") {
				a_v.sorting.craftingSupply = i();
			} else if (a_key == "bCraftingSupplyGear") {
				a_v.sorting.craftingSupplyGear = b();
			} else if (a_key == "iSellPricePercent") {
				a_v.selling.pricePercent = i();
			} else if (a_key == "iAutoSellHours") {
				a_v.selling.autoSellHours = i();
			} else if (a_key == "iMode") {
				a_v.ui.mode = i();
			} else {
				logger::warn("AutoTest: unknown setting {}", a_key);
			}
		}

		// A furniture reference of the player's cell: `ref`, or the first with keyword `keyword`.
		RE::TESObjectREFR* FindFurniture(const Json& a_step)
		{
			if (const auto ref = Ref(a_step.value("ref", Json())); ref) {
				return game::LookupRef(*ref);
			}
			const auto         keyword = a_step.value("keyword", ""s);
			RE::TESObjectREFR* found = nullptr;
			if (auto* cell = PlayerCell()) {
				cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
					auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
					auto* furniture = base ? base->As<RE::TESFurniture>() : nullptr;
					if (furniture && !a_ref->IsDisabled() && furniture->HasKeywordString(keyword)) {
						found = a_ref;
						return RE::BSContainer::ForEachResult::kStop;
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			return found;
		}

		// A load door of the player's cell that leads to another cell (`ref`, or the first one).
		RE::TESObjectREFR* FindExitDoor(const Json& a_step)
		{
			if (const auto ref = Ref(a_step.value("ref", Json())); ref) {
				return game::LookupRef(*ref);
			}
			RE::TESObjectREFR* found = nullptr;
			if (auto* cell = PlayerCell()) {
				cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
					const auto* teleport = a_ref ? a_ref->extraList.GetByType<RE::ExtraTeleport>() : nullptr;
					if (teleport && teleport->teleportData && !a_ref->IsDisabled()) {
						const auto target = teleport->teleportData->linkedDoor.get();
						if (target && target->GetParentCell() != cell) {
							found = a_ref;
							return RE::BSContainer::ForEachResult::kStop;
						}
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			return found;
		}

		RE::TESObjectREFR* FindRespawningContainer()
		{
			RE::TESObjectREFR* found = nullptr;
			if (auto* cell = PlayerCell()) {
				cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
					auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
					auto* cont = base ? base->As<RE::TESObjectCONT>() : nullptr;
					if (cont && !a_ref->IsDisabled() && cont->data.flags.any(RE::CONT_DATA::Flag::kRespawn)) {
						found = a_ref;
						return RE::BSContainer::ForEachResult::kStop;
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			return found;
		}

		std::optional<Wait> RunAction(const std::string& a_kind, const Json& s, Context& c)
		{
			auto* player = game::Player();
			// Controller actions take the cell the menu was opened in: the player's cell, which must be a home.
			auto cellId = [&]() -> std::optional<RE::FormID> {
				auto* home = CurrentHome();
				if (!home) {
					AbortTest(c, a_kind, "the player is not in a home");
					return std::nullopt;
				}
				// Only the plan's home (or a cell its own `enter` steps went to): a step before `enter` would otherwise
				// act on the home the previous plan left the player in.
				const bool planHome = !c.home.IsValid() || home->cell == c.home || c.planCells.contains(home->cellId) ||
				                      std::ranges::any_of(home->areas, [&](const home::Area& a_area) { return a_area.cell == c.home; });
				if (!planHome) {
					AbortTest(c, a_kind, std::format("the player is in {}, not the plan's home (enter it first)", home->name));
					return std::nullopt;
				}
				return home->cellId;
			};
			auto refArg = [&](std::string_view a_key = "ref") -> std::optional<core::FormRef> {
				const auto ref = Ref(s.value(std::string(a_key), Json()));
				if (!ref) {
					AbortTest(c, a_kind, std::format("\"{}\" is not a form ref", a_key));
				}
				return ref;
			};

			if (a_kind == "enter" && s.value("any", false)) {
				// An exterior (coc to a marker): any other cell will do.
				const auto target = s.value("coc", ""s);
				auto*      from = PlayerCell();
				const auto fromId = from ? from->GetFormID() : 0;
				Console("coc " + target);
				auto settled = std::make_shared<std::optional<Clock::time_point>>();
				return WaitFor(std::format("enter {}", target), s.value("timeout", 90.0), [fromId, settled]() {
					auto* cell = PlayerCell();
					if (!cell || cell->GetFormID() == fromId || !InCell(std::nullopt)) {
						return false;
					}
					if (!*settled) {
						*settled = Clock::now();
					}
					return Clock::now() - **settled > 3s;
				},
					c, true, [&c]() {
						if (auto* cell = PlayerCell()) {
							c.planCells.insert(cell->GetFormID());
						}
					});
			}
			if (a_kind == "require") {
				// The home as this game has it: furniture a player buys or builds first (Breezehome's racks, Hearthfire)
				// may be off. Then the test is skipped, not failed.
				const auto ref = Ref(s.value("enabled", Json()));
				auto*      object = ref ? game::LookupRef(*ref) : nullptr;
				if (!object || object->IsDisabled()) {
					c.skipReason = std::format("{} is {} in this game ({})", Text(s.value("enabled", Json())), object ? "disabled" : "not loaded",
						s.value("why", "not built or bought yet"s));
					c.abortTest = true;
				}
				return std::nullopt;
			}
			if (a_kind == "buyHome") {
				// What buying every decoration of a vanilla home does: each disabled container and furniture of the
				// cell whose enable parent is a (disabled) decoration marker is enabled through that marker. References
				// that are enabled as the opposite of their parent (the clutter a decoration replaces) are left alone.
				std::set<RE::TESObjectREFR*> markers;
				if (auto* cell = PlayerCell()) {
					cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
						auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
						if (!base || !a_ref->IsDisabled() || !(base->Is(RE::FormType::Container) || base->Is(RE::FormType::Furniture) || base->Is(RE::FormType::Activator))) {
							return RE::BSContainer::ForEachResult::kContinue;
						}
						RE::TESObjectREFR* current = a_ref;
						for (int depth = 0; depth < 8; ++depth) {
							const auto* parent = current->extraList.GetByType<RE::ExtraEnableStateParent>();
							if (!parent || (parent->flags & 1) != 0) {
								break;
							}
							auto next = parent->parent.get();
							if (!next || !next->IsDisabled()) {
								break;
							}
							current = next.get();
						}
						if (current != a_ref) {
							markers.insert(current);
						}
						return RE::BSContainer::ForEachResult::kContinue;
					});
				}
				for (auto* marker : markers) {
					marker->Enable(false);
				}
				logger::info("AutoTest: {} decoration marker(s) enabled", markers.size());
				const auto until = Clock::now() + 2s;
				return WaitFor("decorations enabled", 10.0, [until]() { return Clock::now() >= until; }, c, false);
			}
			if (a_kind == "clearContainer") {
				// A container of the test home emptied (its own clutter, so counts start from nothing). Test game only.
				const auto ref = refArg();
				auto*      object = ref ? game::LookupRef(*ref) : nullptr;
				if (!object) {
					if (ref) {
						AbortTest(c, "clearContainer", ref->ToString() + " not found");
					}
					return std::nullopt;
				}
				Console("removeallitems", object);
				const auto until = Clock::now() + 1s;
				return WaitFor("container cleared", 10.0, [until]() { return Clock::now() >= until; }, c, false);
			}
			if (a_kind == "clearInventory") {
				// The character a coc game starts with carries a few things (armor, potions, a sword): gone, so the counts
				// of a test start from nothing. Nothing is saved.
				Console("player.removeallitems");
				const auto until = Clock::now() + 1s;
				return WaitFor("inventory cleared", 10.0, [until]() { return Clock::now() >= until; }, c, false);
			}
			if (a_kind == "enable") {
				// What buying a decoration does: its enable parent (or the reference itself) is enabled.
				const auto ref = refArg();
				auto*      object = ref ? game::LookupRef(*ref) : nullptr;
				if (!object) {
					if (ref) {
						AbortTest(c, "enable", ref->ToString() + " not found");
					}
					return std::nullopt;
				}
				// The whole enable-parent chain (a Hearthfire wing: built-wing marker -> furniture marker -> station), top
				// first: a disabled marker in the middle keeps the station unusable although it is enabled itself.
				std::vector<RE::TESObjectREFR*> chain{ object };
				for (int depth = 0; depth < 8; ++depth) {
					const auto* parent = chain.back()->extraList.GetByType<RE::ExtraEnableStateParent>();
					auto        next = parent ? parent->parent.get() : RE::NiPointer<RE::TESObjectREFR>{};
					if (!next) {
						break;
					}
					chain.push_back(next.get());
				}
				if (game::IsEffectivelyDisabled(object)) {
					c.planEnabled.insert(object->GetFormID());  // off in this save until now (sit's skipIfClosed)
				}
				for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
					if ((*it)->IsDisabled()) {
						logger::info("AutoTest: enabling {}", Name(*it));
						Console("enable", *it);
					}
				}
				// Enabled through its parents and, for something in the player's cell, with its 3D attached to the cell: a
				// station enabled a moment ago has none yet, and sitting at it opens no menu (Heljarchen's and Lakeview's
				// anvils in earlier runs).
				const bool here = object->GetParentCell() == PlayerCell();
				return WaitFor(std::format("{} enabled", ref->ToString()), 10.0, [object, here]() {
					if (object->IsDisabled() || game::IsEffectivelyDisabled(object)) {
						return false;
					}
					auto* cell = object->GetParentCell();
					return !here || (object->Is3DLoaded() && object->Get3D() && cell && cell->IsAttached());
				},
					c, false);
			}
			if (a_kind == "enter") {
				std::string target = s.value("coc", ""s);
				const auto  cellRef = s.contains("cell") ? Ref(s["cell"]) : (target.empty() && c.homeCoc.empty() ? std::optional(c.home) : std::nullopt);
				if (target.empty()) {
					target = cellRef ? CocTarget(*cellRef) : c.homeCoc;
				}
				if (target.empty()) {
					AbortTest(c, "enter", "no coc target");
					c.abortPlan = true;
					return std::nullopt;
				}
				logger::info("AutoTest: coc {}", target);
				Console("coc " + target);
				auto settled = std::make_shared<std::optional<Clock::time_point>>();
				const double settle = s.value("settle", 3.0);
				auto wait = WaitFor(std::format("enter {}", target), s.value("timeout", 90.0),
					[target, settled, settle]() {
						auto* cell = PlayerCell();
						const char* edid = cell ? cell->GetFormEditorID() : nullptr;
						if (!InCell(std::nullopt) || !edid || !core::IEquals(edid, target)) {
							settled->reset();
							return false;
						}
						if (!*settled) {
							*settled = Clock::now();
						}
						return Clock::now() - **settled >= std::chrono::milliseconds(static_cast<std::int64_t>(settle * 1000.0));
					},
					c, true, [&c, target]() {
						events::RegisterPlayerSinks();  // as kNewGame / kPostLoadGame do; a game started by coc sends neither
						c.Record(true, std::format("entered {}", target));
						if (auto* cell = PlayerCell()) {
							c.planCells.insert(cell->GetFormID());
						}
					});
				auto finish = wait.finish;
				wait.finish = [finish, &c](bool a_timedOut) {
					finish(a_timedOut);
					if (a_timedOut) {
						c.abortPlan = true;
					}
				};
				return wait;
			}
			if (a_kind == "console") {
				Console(s.value("cmd", ""s));
				return std::nullopt;
			}
			if (a_kind == "additem") {
				auto*      item = Item(s);
				auto*      target = Holder(s.value("to", Json("player")));
				const auto count = s.value("count", 1);
				if (!item || !target) {
					AbortTest(c, "additem", !item ? "item not found: " + Text(s.value("item", Json())) : "target not found");
					return std::nullopt;
				}
				const auto extra = s.value("extra", Json::object());
				if (extra.empty()) {
					target->AddObjectToContainer(item, nullptr, count, nullptr);
				} else {
					// A special copy (tempered, renamed, someone else's): placed in the world with that extra data on
					// its reference, then picked up by the player, so the game itself builds the inventory stack (as
					// with an item dropped and taken again; someone else's becomes stolen).
					if (target != player || !player) {
						AbortTest(c, "additem", "special copies can only be given to the player");
						return std::nullopt;
					}
					for (int k = 0; k < count; ++k) {
						auto placed = player->PlaceObjectAtMe(item, false);
						if (!placed) {
							AbortTest(c, "additem", "could not place " + Name(item));
							return std::nullopt;
						}
						if (extra.contains("health")) {
							placed->extraList.Add(new RE::ExtraHealth(extra["health"].get<float>()));
						}
						if (extra.contains("name")) {
							placed->SetDisplayName(RE::BSFixedString(extra["name"].get<std::string>()), true);
						}
						if (extra.contains("owner")) {
							const auto owner = Ref(extra["owner"]);
							if (auto* form = owner ? game::LookupForm(*owner) : nullptr) {
								placed->extraList.SetOwner(form);
							}
						}
						player->PickUpObject(placed.get(), 1, false, false);
					}
				}
				logger::info("AutoTest: added {} x{} to {}", Name(item), count, HolderName(s.value("to", Json("player"))));
				return std::nullopt;
			}
			if (a_kind == "additemMany") {
				// One copy of each of the first `count` ordinary misc items of Skyrim.esm (a big inventory).
				auto*      dh = RE::TESDataHandler::GetSingleton();
				const auto count = s.value("count", 100);
				int        added = 0;
				for (auto* misc : dh ? dh->GetFormArray<RE::TESObjectMISC>() : RE::BSTArray<RE::TESObjectMISC*>{}) {
					if (added >= count) {
						break;
					}
					const char* name = misc ? misc->GetName() : nullptr;
					if (!misc || !name || !*name || misc->GetGoldValue() <= 0 || (misc->GetFormID() >> 24) != 0 || misc->GetFormID() == 0xF ||
						misc->GetFormID() == 0xA) {
						continue;
					}
					player->AddObjectToContainer(misc, nullptr, 1, nullptr);
					++added;
				}
				logger::info("AutoTest: added {} different misc items", added);
				return std::nullopt;
			}
			if (a_kind == "removeitem") {
				auto* item = Item(s);
				auto* from = Holder(s.value("from", Json("player")));
				if (!item || !from) {
					AbortTest(c, "removeitem", "item or holder not found");
					return std::nullopt;
				}
				from->RemoveItem(item, s.value("count", 1), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				return std::nullopt;
			}
			if (a_kind == "equip") {
				auto* item = Item(s);
				if (!item || !player) {
					AbortTest(c, "equip", "item not found");
					return std::nullopt;
				}
				RE::ActorEquipManager::GetSingleton()->EquipObject(player, item, nullptr, s.value("count", 1), nullptr, false, true, false, true);
				return WaitFor(std::format("{} equipped", Name(item)), 5.0, [item]() { return CountIn(game::Player(), item, "worn") > 0; }, c, false);
			}
			if (a_kind == "settings") {
				auto values = Settings::GetSingleton().Get();
				for (const auto& [key, value] : s.value("set", Json::object()).items()) {
					ApplySetting(values, key, value);
				}
				Controller::Get().ApplySettings(values);
				return std::nullopt;
			}
			if (a_kind == "snapshot") {
				c.snapshots[s.value("name", "default"s)] = TakeSnapshot();
				return std::nullopt;
			}
			if (a_kind == "wait") {
				const auto until = Clock::now() + std::chrono::milliseconds(static_cast<std::int64_t>(s.value("seconds", 1.0) * 1000.0));
				return WaitFor("wait", s.value("seconds", 1.0) + 5.0, [until]() { return Clock::now() >= until; }, c, false);
			}
			if (a_kind == "waitIdle") {
				return WaitFor("Dova Stash idle (displays filled, no container menu)", s.value("timeout", 60.0), []() {
					auto* ui = RE::UI::GetSingleton();
					return !display::Busy() && !(ui && ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME));
				},
					c, false);
			}
			if (a_kind == "makeMaster") {
				const auto cell = cellId();
				const auto ref = cell ? refArg() : std::nullopt;
				if (ref) {
					Controller::Get().MakeMaster(*cell, *ref);
				}
				return std::nullopt;
			}
			if (a_kind == "useTemplate") {
				if (const auto cell = cellId()) {
					if (s.contains("source")) {
						Controller::Get().UseTemplate(*cell, s["source"].get<std::string>());
					} else {
						Controller::Get().UseTemplate(*cell);
					}
				}
				return std::nullopt;
			}
			if (a_kind == "hideTemplate") {
				// The panel's Remove... (`hidden`: true, the default) and Add back under Templates.
				if (const auto cell = cellId()) {
					Controller::Get().SetTemplateHidden(*cell, s.at("source").get<std::string>(), s.value("hidden", true));
				}
				return std::nullopt;
			}
			if (a_kind == "reset") {
				if (const auto cell = cellId()) {
					Controller::Get().ResetHome(*cell);
				}
				return std::nullopt;
			}
			if (a_kind == "restore") {
				if (const auto cell = cellId()) {
					Controller::Get().RestoreBackup(*cell);
				}
				return std::nullopt;
			}
			if (a_kind == "bind" || a_kind == "unbind" || a_kind == "displayCategory") {
				const auto cell = cellId();
				const auto ref = cell ? refArg() : std::nullopt;
				auto*      home = CurrentHome();
				if (!ref || !home) {
					return std::nullopt;
				}
				const auto category = s.value("category", ""s);
				if (a_kind == "displayCategory") {
					Controller::Get().ToggleDisplayCategory(*cell, *ref, category);
					return std::nullopt;
				}
				const auto* binding = home->layout ? home->layout->FindContainer(*ref) : nullptr;
				const bool  bound = binding && std::ranges::any_of(binding->categories, [&](const std::string& p) { return core::IEquals(p, category); });
				if (bound != (a_kind == "bind")) {
					Controller::Get().ToggleCategory(*cell, *ref, category);
				}
				return std::nullopt;
			}
			if (a_kind == "ignore") {
				const auto cell = cellId();
				if (const auto ref = cell ? refArg() : std::nullopt) {
					Controller::Get().SetIgnored(*cell, *ref, s.value("on", true));
				}
				return std::nullopt;
			}
			if (a_kind == "removeContainer") {
				const auto cell = cellId();
				if (const auto ref = cell ? refArg() : std::nullopt) {
					Controller::Get().RemoveContainer(*cell, *ref);
				}
				return std::nullopt;
			}
			if (a_kind == "label") {
				const auto cell = cellId();
				if (const auto ref = cell ? refArg() : std::nullopt) {
					Controller::Get().SetLabel(*cell, *ref, s.value("label", ""s));
				}
				return std::nullopt;
			}
			if (a_kind == "aim") {
				// What the crosshair reports, as if the player looked at "ref" ("ref": null, at nothing); "clear": true
				// gives it back to the game. The panel and the hotkey's target read it (events::CrosshairTarget).
				if (s.value("clear", false)) {
					events::OverrideCrosshair(std::nullopt);
					return std::nullopt;
				}
				if (s.contains("ref") && s["ref"].is_null()) {
					events::OverrideCrosshair(RE::RefHandle{ 0 });
					return std::nullopt;
				}
				const auto ref = refArg();
				auto*      object = ref ? game::LookupRef(*ref) : nullptr;
				if (ref && !object) {
					AbortTest(c, a_kind, std::format("{} is not loaded", ref->ToString()));
					return std::nullopt;
				}
				if (object) {
					events::OverrideCrosshair(object->GetHandle().native_handle());
				}
				return std::nullopt;
			}
			if (a_kind == "sellChest") {
				const auto cell = cellId();
				if (const auto ref = cell ? refArg() : std::nullopt) {
					Controller::Get().SetSellChest(*cell, *ref, s.value("on", true));
				}
				return std::nullopt;
			}
			if (a_kind == "quote") {
				auto* home = CurrentHome();
				if (!home) {
					AbortTest(c, "quote", "not in a home");
					return std::nullopt;
				}
				c.quoteChest = s.contains("chest") ? Ref(s["chest"]) : std::nullopt;
				c.quote = sort::selling::QuoteSale(*home, c.quoteChest);
				logger::info("AutoTest: quote: {} items for {} gold ({}; {})", c.quote->items, c.quote->gold, c.quote->what, c.quote->stays);
				return std::nullopt;
			}
			if (a_kind == "sellConfirmed") {
				// What "Yes" in the "Sell N items for G gold?" box does, with the numbers that box showed (the last quote).
				const auto cell = cellId();
				if (!cell || !c.quote) {
					if (!c.quote) {
						AbortTest(c, "sellConfirmed", "no quote");
					}
					return std::nullopt;
				}
				Controller::Get().SellConfirmed(*cell, c.quoteChest, c.quote->items, c.quote->gold);
				return std::nullopt;
			}
			if (a_kind == "buyBack") {
				if (const auto cell = cellId()) {
					Controller::Get().BuyBackConfirmed(*cell);
				}
				return std::nullopt;
			}
			if (a_kind == "op") {
				const auto cell = cellId();
				if (!cell) {
					return std::nullopt;
				}
				const auto name = s.value("op", ""s);
				static const std::map<std::string, sort::Op, std::less<>> kOps{ { "Deposit", sort::Op::Deposit }, { "Distribute", sort::Op::Distribute },
					{ "SortAll", sort::Op::SortAll }, { "Retrieve", sort::Op::Retrieve }, { "Restock", sort::Op::Restock } };
				const auto it = kOps.find(name);
				if (it == kOps.end()) {
					AbortTest(c, "op", "unknown op " + name);
					return std::nullopt;
				}
				Controller::Get().RunOperation(*cell, it->second, s.value("pattern", ""s));
				return std::nullopt;
			}
			if (a_kind == "keep") {
				const auto cell = cellId();
				if (!cell) {
					return std::nullopt;
				}
				if (s["keep"].is_null()) {
					Controller::Get().SetKeep(*cell, std::nullopt);
					return std::nullopt;
				}
				core::Diagnostics diags;
				auto              keep = core::ParseKeepConfig(s["keep"], "AutoTest", diags);
				if (!keep) {
					AbortTest(c, "keep", "the keep block does not parse");
					return std::nullopt;
				}
				Controller::Get().SetKeep(*cell, std::move(keep));
				return std::nullopt;
			}
			if (a_kind == "chooseArmorSet") {
				// The set picker's choice for a mannequin (hotkey menu and Displays page): "set" names a piece of the set,
				// null or absent = Auto.
				const auto cell = cellId();
				if (const auto ref = cell ? refArg() : std::nullopt) {
					std::optional<core::FormRef> set;
					if (s.contains("set") && !s["set"].is_null()) {
						set = Ref(s["set"]);
						if (!set) {
							AbortTest(c, a_kind, "\"set\" is not a form ref");
							return std::nullopt;
						}
					}
					ui::ChooseArmorSet(*cell, *ref, set);
				}
				return std::nullopt;
			}
			if (a_kind == "displayUse") {
				const auto cell = cellId();
				if (const auto ref = cell ? refArg() : std::nullopt) {
					Controller::Get().SetDisplayUsed(*cell, *ref, s.value("on", true));
				}
				return std::nullopt;
			}
			if (a_kind == "displayCategories") {
				// A used display's categories, as its picker ticks them: "ref" and "categories" (patterns, also ones
				// the picker would not offer; [] clears). The display must be in use (displayUse first).
				const auto cell = cellId();
				const auto ref = cell ? refArg() : std::nullopt;
				if (!ref) {
					return std::nullopt;
				}
				auto*       home = CurrentHome();
				const auto* binding = home && home->layout ? home->layout->FindDisplay(*ref) : nullptr;
				if (!binding) {
					AbortTest(c, a_kind, std::format("{} is not a display of the setup (displayUse first)", ref->ToString()));
					return std::nullopt;
				}
				std::vector<std::string> want;
				for (const auto& p : s.value("categories", Json::array())) {
					want.push_back(p.get<std::string>());
				}
				const auto current = binding->categories;  // toggling changes the binding
				for (const auto& p : current) {
					if (std::ranges::none_of(want, [&](const std::string& w) { return core::IEquals(w, p); })) {
						Controller::Get().ToggleDisplayCategory(*cell, *ref, p);
					}
				}
				for (const auto& w : want) {
					if (std::ranges::none_of(current, [&](const std::string& p) { return core::IEquals(w, p); })) {
						Controller::Get().ToggleDisplayCategory(*cell, *ref, w);
					}
				}
				return std::nullopt;
			}
			if (a_kind == "setCategories") {
				// One container's categories, as the category picker sets them (Clear categories, then a tick per
				// pattern): "ref" and "categories" (patterns; [] clears). The rest of the setup stays.
				const auto cell = cellId();
				const auto ref = cell ? refArg() : std::nullopt;
				if (!ref) {
					return std::nullopt;
				}
				auto*       home = CurrentHome();
				const auto* binding = home && home->layout ? home->layout->FindContainer(*ref) : nullptr;
				if (binding && !binding->categories.empty()) {
					Controller::Get().ClearCategories(*cell, *ref);
				}
				for (const auto& pattern : s.value("categories", Json::array())) {
					Controller::Get().ToggleCategory(*cell, *ref, pattern.get<std::string>());
				}
				logger::info("AutoTest: categories of {} set to {}", ref->ToString(), s.value("categories", Json::array()).dump());
				return std::nullopt;
			}
			if (a_kind == "panelVisible") {
				// The Menu Framework panel as if it were drawn now ("page": home, displays, settings, about): for 1.5 s
				// the Controller's notices go to its status line (PanelModel::SetStatus) instead of the HUD.
				ui::panel::PanelModel::Get().MarkRendered(PanelPage(s.value("page", "home"s)));
				return std::nullopt;
			}
			if (a_kind == "bindHotkey") {
				// Settings > Hotkey's press-to-bind (UI/Panel/KeyCapture.h) as the page runs it: Begin, one key press
				// through the capture's own input path (FeedForTest), then the page's apply (Controller::ApplySettings).
				// "target": "keyboard" (default) or "gamepad"; "code": the SKSE key code pressed; "cancel": true ends the
				// wait instead; "clear": true is the gamepad row's Clear (no wait). Logs what the page would show.
				namespace kc = ui::panel::key_capture;
				const auto target = s.value("target", "keyboard"s) == "gamepad" ? kc::Target::Gamepad : kc::Target::Keyboard;
				auto       values = Settings::GetSingleton().Get();
				auto&      slot = target == kc::Target::Gamepad ? values.input.gamepadHotkey : values.input.hotkey;
				if (s.value("clear", false)) {
					slot = Settings::kNoKey;
				} else {
					const bool before = kc::FrameworkKeyEnabled();
					kc::Begin(target);
					const bool during = kc::FrameworkKeyEnabled();
					if (s.value("cancel", false)) {
						kc::Cancel();
					} else {
						kc::FeedForTest(s.value("code", 0));
					}
					const auto code = kc::TakeResult(target);
					logger::info("Hotkey capture (test): {}; framework key on before {}, during {}, after {}", code ? input::KeyName(*code) : "nothing bound"s,
						before, during, kc::FrameworkKeyEnabled());
					if (!code) {
						return std::nullopt;
					}
					slot = *code;
				}
				Controller::Get().ApplySettings(values);
				const auto now = Settings::GetSingleton().GetInput();
				const auto bound = target == kc::Target::Gamepad ? now.gamepadHotkey : now.hotkey;
				logger::info("Hotkey capture (test): hotkey is now {}, gamepad {}; clash \"{}\"", Controller::HotkeyName(),
					now.gamepadHotkey == Settings::kNoKey ? "off"s : input::KeyName(now.gamepadHotkey), bound == Settings::kNoKey ? ""s : kc::Clash(target, bound));
				return std::nullopt;
			}
			if (a_kind == "fill") {
				const auto cell = cellId();
				if (const auto ref = cell ? refArg() : std::nullopt) {
					Controller::Get().FillDisplay(*cell, *ref);
				}
				return std::nullopt;
			}
			if (a_kind == "export") {
				const auto cell = cellId();
				if (!cell) {
					return std::nullopt;
				}
				Controller::ExportRequest request;
				request.name = s.value("name", ""s);
				request.overwrite = s.value("overwrite", false);
				request.learnedAsForms = s.value("learnedAsForms", true);
				const auto result = Controller::Get().ExportTemplate(*cell, request);
				static constexpr std::array kStatus{ "ok", "exists", "failed" };
				const auto status = kStatus[static_cast<std::size_t>(result.status)];
				const auto want = s.value("expect", "ok"s);
				c.Record(status == want, std::format("Save as template \"{}\" -> {}", request.name, want),
					std::format("{} (version {}, {})", status, result.version, result.message));
				return std::nullopt;
			}
			if (a_kind == "addArea") {
				const auto offer = home::HomeRegistry::Get().OfferArea();
				if (!offer) {
					AbortTest(c, "addArea", "no area offered here");
					return std::nullopt;
				}
				Controller::Get().AddArea(offer->cellId, s.value("label", ""s));
				return std::nullopt;
			}
			if (a_kind == "renameArea" || a_kind == "removeArea") {
				auto* home = CurrentHome();
				auto* cell = PlayerCell();
				if (!home || !cell) {
					AbortTest(c, a_kind, "not in a home");
					return std::nullopt;
				}
				const auto area = s.contains("cell") ? Ref(s["cell"]) : game::ToFormRef(cell);
				if (!area) {
					AbortTest(c, a_kind, "no area");
					return std::nullopt;
				}
				if (a_kind == "renameArea") {
					Controller::Get().RenameArea(home->cellId, *area, s.value("label", ""s));
				} else {
					Controller::Get().RemoveArea(home->cellId, *area);
				}
				return std::nullopt;
			}
			if (a_kind == "hotkey") {
				RE::RefHandle handle = 0;
				const auto    target = s.value("target", ""s);
				RE::TESObjectREFR* ref = nullptr;
				if (target == "respawning") {
					ref = FindRespawningContainer();
				} else if (!target.empty()) {
					const auto formRef = core::FormRef::Parse(target);
					ref = formRef ? game::LookupRef(*formRef) : nullptr;
				}
				if (!target.empty() && !ref) {
					AbortTest(c, "hotkey", "target not found: " + target);
					return std::nullopt;
				}
				if (ref) {
					handle = ref->GetHandle().native_handle();
					logger::info("AutoTest: hotkey at {}", Name(ref));
				}
				Controller::Get().OnHotkey(handle);
				return std::nullopt;
			}
			if (a_kind == "openContainer") {
				const auto cell = cellId();
				const auto ref = cell ? refArg() : std::nullopt;
				if (!ref) {
					return std::nullopt;
				}
				// "Browse containers..." (the master menu) opens a container's menu from anywhere in the home. If it does
				// not, that is recorded, and the container is activated with the console instead so the test goes on.
				Controller::Get().Browse(*cell, *ref);
				// The Controller learns which container is open through its menu sink (a task): wait for the menu.
				auto opened = std::make_shared<std::optional<Clock::time_point>>();
				auto menuUp = [opened]() {
					auto* ui = RE::UI::GetSingleton();
					if (!ui || !ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME)) {
						return false;
					}
					if (!*opened) {
						*opened = Clock::now();
					}
					return Clock::now() - **opened > 500ms;
				};
				auto wait = WaitFor(std::format("container menu of {} open", ref->ToString()), 16.0, menuUp, c);
				// Past Browse's whole sequence (settle up to 3 s, activate, 3 s, the console's activation, 8 s): the
				// harness never races Browse's own retry.
				wait.deadline = Clock::now() + 16s;
				wait.finish = [&c, menuUp, target = *ref](bool a_timedOut) {
					if (!a_timedOut) {
						return;
					}
					c.Record(false, std::format("Browse: the container menu of {} did not open (Controller::Browse)", target.ToString()), "open menus: " + OpenMenus());
					Console("activate player", game::LookupRef(target));
					auto again = WaitFor(std::format("container menu of {} open (console activate)", target.ToString()), 6.0, menuUp, c, false);
					again.finish = [&c, target](bool a_timedOut2) {
						if (!a_timedOut2) {
							return;
						}
						// No container menu opens in this session (see the failure above): what the menu sink would tell
						// the Controller is told directly, so the steps that need an open container (Learn) still run.
						auto* object = game::LookupRef(target);
						logger::info("AutoTest: no container menu opens; telling the Controller that {} is open (as its menu sink would)", target.ToString());
						if (object) {
							c.simulatedContainer = object->GetFormID();
							Controller::Get().OnContainerMenuOpened(object->GetFormID());
						}
					};
					c.next = std::move(again);
				};
				return wait;
			}
			if (a_kind == "choose") {
				// Chooses an entry of the hotkey menu that is up, the way the player would: "entry" (its text, or the
				// start of it) or "index" (-1: Esc, which cancels; a list with a parent goes back to it). Waits for the
				// menu to be on screen first, then for what the choice opens.
				const auto entry = s.value("entry", ""s);
				const auto index = s.value("index", -1);
				auto       ready = []() { return ui::uiext::PendingList().shown || !MessageBoxText().empty(); };
				return WaitFor("a hotkey menu on screen", 10.0, ready, c, true, [&c, entry, index]() {
					std::string source;
					const auto  entries = MenuEntries(&source);
					auto chosen = entry.empty() ? index : FindEntry(entries, entry);
					const bool cancel = entry.empty() && index == -1;
					if (cancel && source != "list") {
						chosen = static_cast<std::int32_t>(entries.size()) - 1;  // a message box's last button cancels
					}
					if (!cancel && (chosen < 0 || static_cast<std::size_t>(chosen) >= entries.size())) {
						c.Record(false, std::format("choose \"{}\"", entry.empty() ? std::to_string(index) : entry), std::format("not in the {}", source));
						c.abortTest = true;
						return;
					}
					logger::info("AutoTest: choosing \"{}\" in the {}", chosen < 0 ? "(Esc)"s : entries[static_cast<std::size_t>(chosen)], source);
					if (source == "list") {
						ui::uiext::ChooseForTest(chosen);
					} else {
						PressMessageBoxButton(chosen);
					}
					// What the choice opens comes a moment later (a list, a box, a notification): settle.
					const auto since = Clock::now();
					c.next = WaitFor("the menu after the choice", 10.0, [since]() {
						const auto list = ui::uiext::PendingList();
						return Clock::now() - since > 1s && (list.serial == 0 || list.shown);
					},
						c, false);
				});
			}
			if (a_kind == "answerMessageBox") {
				const auto text = MessageBoxText();
				const auto button = s.value("button", 0);
				if (text.empty()) {
					AbortTest(c, "answerMessageBox", "no message box is open");
					return std::nullopt;
				}
				logger::info("AutoTest: answering message box \"{}\" with button {}", text, button);
				PressMessageBoxButton(button);
				return WaitFor("message box answered", 10.0, []() { return MessageBoxText().empty(); }, c, false);
			}
			if (a_kind == "closeMenu") {
				const auto name = s.value("menu", std::string(RE::ContainerMenu::MENU_NAME));
				if (name == RE::ContainerMenu::MENU_NAME && c.simulatedContainer) {
					c.simulatedContainer = 0;
					Controller::Get().OnContainerMenuClosed();
					return std::nullopt;
				}
				if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
					queue->AddMessage(name, RE::UI_MESSAGE_TYPE::kHide, nullptr);
				}
				return WaitFor(std::format("{} closed", name), 10.0, [name]() {
					auto* ui = RE::UI::GetSingleton();
					return ui && !ui->IsMenuOpen(name);
				},
					c, false);
			}
			if (a_kind == "store" || a_kind == "take") {
				// By hand: what the container menu does when the player puts an item in or takes it out.
				auto*      item = Item(s);
				const bool store = a_kind == "store";
				auto*      other = Holder(s.value(store ? "to" : "from", Json("master")));
				if (!item || !other || !player) {
					AbortTest(c, a_kind, "item or container not found");
					return std::nullopt;
				}
				// For a short grace after its own moves Dova Stash ignores container events (ItemMover::IsSuppressing,
				// so scripts answering a move are not mistaken for the player). A player cannot store anything that
				// fast; the harness can, right after a Distribute: wait it out, or the arrival goes unnoticed.
				auto act = [item, other, player, store, count = s.value("count", 1)]() {
					if (store) {
						player->RemoveItem(item, count, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, other);
					} else {
						other->RemoveItem(item, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, player);
					}
				};
				if (game::ItemMover::IsSuppressing()) {
					return WaitFor("Dova Stash's own moves settled", 5.0, []() { return !game::ItemMover::IsSuppressing(); }, c, false, act);
				}
				act();
				return std::nullopt;
			}
			if (a_kind == "acceptSuggestion" || a_kind == "declineSuggestion") {
				const auto cell = cellId();
				if (const auto ref = cell ? refArg() : std::nullopt) {
					if (a_kind == "acceptSuggestion") {
						Controller::Get().AcceptSuggestion(*cell, *ref, s.value("category", ""s));
					} else {
						Controller::Get().DeclineSuggestion(*cell, *ref, s.value("category", ""s));
					}
				}
				return std::nullopt;
			}
			if (a_kind == "exitDoor") {
				auto* door = FindExitDoor(s);
				auto* from = PlayerCell();
				if (!door || !player || !from) {
					AbortTest(c, "exitDoor", "no load door here");
					return std::nullopt;
				}
				// Where the door leads: the player is moved to the linked door (player.moveto), which loads that cell as
				// walking through would.
				const auto* teleport = door->extraList.GetByType<RE::ExtraTeleport>();
				const auto  linked = teleport && teleport->teleportData ? teleport->teleportData->linkedDoor.get() : RE::NiPointer<RE::TESObjectREFR>{};
				if (!linked) {
					AbortTest(c, "exitDoor", "the door leads nowhere");
					return std::nullopt;
				}
				logger::info("AutoTest: through {} to {}", Name(door), Name(linked.get()));
				Console(std::format("player.moveto {:08X}", linked->GetFormID()));
				const auto fromId = from->GetFormID();
				auto       settled = std::make_shared<std::optional<Clock::time_point>>();
				return WaitFor("through the door", 60.0, [fromId, settled]() {
					auto* cell = PlayerCell();
					if (!cell || cell->GetFormID() == fromId || !InCell(std::nullopt)) {
						return false;
					}
					if (!*settled) {
						*settled = Clock::now();
					}
					return Clock::now() - **settled > 3s;
				},
					c);
			}
			if (a_kind == "sit") {
				auto* station = FindFurniture(s);
				if (!station || !player) {
					AbortTest(c, "sit", "no such station in this cell");
					return std::nullopt;
				}
				logger::info("AutoTest: sitting down at {}", Name(station));
				player->MoveTo(station);
				const auto menu = s.value("menu", std::string(RE::CraftingMenu::MENU_NAME));
				const auto timeout = s.value("timeout", 20.0);
				// "skipIfClosed": a station that was off in this save until the plan's own `enable` turned it on (a wing the
				// save never built) and still opens no menu after every fallback is skipped with the reason, not failed. A
				// station that was already there fails: then the menu should have opened.
				const bool skipIfClosed = s.value("skipIfClosed", false) && c.planEnabled.contains(station->GetFormID());
				auto       opened = std::make_shared<std::optional<Clock::time_point>>();
				auto       menuUp = [menu, opened]() {
                    auto* ui = RE::UI::GetSingleton();
                    if (!ui || !ui->IsMenuOpen(menu)) {
                        return false;
                    }
                    if (!*opened) {
                        *opened = Clock::now();
                    }
                    return Clock::now() - **opened > 1s;
				};
				// Right after a MoveTo (or a coc a moment ago) the player has no 3D yet and the furniture does not seat
				// them: the activation is accepted (the crafting hook fires) but no menu opens. Settle first. A station
				// the plan has just enabled (a Hearthfire wing) needs its own 3D too.
				const auto stationId = station->GetFormID();
				const auto until = Clock::now() + 1500ms;
				return WaitFor("settled at the station", 10.0, [until, stationId]() {
					auto* p = game::Player();
					auto* st = RE::TESForm::LookupByID<RE::TESObjectREFR>(stationId);
					return Clock::now() >= until && p && p->Is3DLoaded() && st && !st->IsDisabled() && st->Is3DLoaded();
				},
					c, false, [&c, menu, menuUp, stationId, timeout, skipIfClosed]() {
						auto* p = game::Player();
						auto* st = RE::TESForm::LookupByID<RE::TESObjectREFR>(stationId);
						if (!p || !st) {
							return;
						}
						st->ActivateRef(p, 0, nullptr, 0, false);
						auto wait = WaitFor(std::format("{} open", menu), timeout, menuUp, c);
						wait.deadline = Clock::now() + 6s;
						// Activating the station from the plugin may not seat the player: then the console does it.
						wait.finish = [&c, menuUp, menu, stationId, timeout, skipIfClosed](bool a_timedOut) {
							if (!a_timedOut) {
								return;
							}
							logger::info("AutoTest: the station did not open {} (open menus: {}); activating it with the console", menu, OpenMenus());
							Console("activate player", RE::TESForm::LookupByID<RE::TESObjectREFR>(stationId));
							auto again = WaitFor(std::format("{} open", menu), timeout, menuUp, c);
							again.deadline = Clock::now() + 6s;
							// Still nothing (the station's 3D came late): stand at it again and activate once more.
							again.finish = [&c, menuUp, menu, stationId, timeout, skipIfClosed](bool a_timedOut2) {
								if (!a_timedOut2) {
									return;
								}
								auto* p = game::Player();
								auto* st = RE::TESForm::LookupByID<RE::TESObjectREFR>(stationId);
								logger::info("AutoTest: the station still did not open {} (open menus: {}); moving to it and activating again", menu, OpenMenus());
								if (p && st) {
									p->MoveTo(st);
								}
								Console("activate player", st);
								// Then keep at it until the menu opens: 6 s after the last activation (or the last moment the
								// menu was up: it can open and close again while the player sits down), activate the station
								// again. Seated at it, the menu is on its way: activate again only after 10 s seated. Never
								// move the player here: a MoveTo unseats them and closes a menu that is opening. A timeout
								// now means the station really opens nothing.
								auto lastTry = std::make_shared<Clock::time_point>(Clock::now());
								auto seatedSince = std::make_shared<std::optional<Clock::time_point>>();
								auto retry = [menuUp, menu, stationId, lastTry, seatedSince]() {
									if (menuUp()) {
										return true;
									}
									auto* ui = RE::UI::GetSingleton();
									if (ui && ui->IsMenuOpen(menu)) {
										*lastTry = Clock::now();
										return false;
									}
									auto* p = game::Player();
									auto* st = RE::TESForm::LookupByID<RE::TESObjectREFR>(stationId);
									if (!p || !st) {
										return false;
									}
									const auto seat = p->GetOccupiedFurniture().get();
									const bool seated = seat && seat.get() == st;
									if (!seated) {
										seatedSince->reset();
									} else if (!*seatedSince) {
										*seatedSince = Clock::now();
										logger::info("AutoTest: seated at {}, waiting for {}", Name(st), menu);
									}
									if (Clock::now() - *lastTry < 6s || (seated && Clock::now() - **seatedSince < 10s)) {
										return false;
									}
									*lastTry = Clock::now();
									seatedSince->reset();
									logger::info("AutoTest: {} still not open (open menus: {}{}); activating {} again", menu, OpenMenus(), seated ? ", seated" : "", Name(st));
									Console("activate player", st);
									return false;
								};
								auto last = WaitFor(std::format("{} open", menu), timeout, retry, c);
								if (skipIfClosed) {
									last.finish = [&c, menu, stationId, timeout](bool a_timedOut3) {
										if (!a_timedOut3) {
											return;
										}
										auto* station = RE::TESForm::LookupByID<RE::TESObjectREFR>(stationId);
										c.skipReason = std::format("the station opened no menu: {} opened no {} after three activations ({} s each; open menus: {})",
											station ? Name(station) : "?"s, menu, timeout, OpenMenus());
										c.abortTest = true;
									};
								}
								c.next = std::move(last);
							};
							c.next = std::move(again);
						};
						c.next = std::move(wait);
					});
			}
			if (a_kind == "consume") {
				// What a crafting menu does with a material (kRemove, no target): through PlayerCharacter::RemoveItem,
				// where the craft-from-storage hook sits.
				auto* item = Item(s);
				if (!item || !player) {
					AbortTest(c, "consume", "item not found");
					return std::nullopt;
				}
				// On the UI thread, where the crafting menu itself consumes materials (SKSE tasks run on worker threads).
				const auto count = s.value("count", 1);
				auto       done = std::make_shared<std::atomic<bool>>(false);
				SKSE::GetTaskInterface()->AddUITask([item, count, done]() {
					logger::info("AutoTest: consuming {} x{} on the UI thread ({})", Name(item), count, ThreadId());
					if (auto* p = game::Player()) {
						p->RemoveItem(item, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
					}
					*done = true;
				});
				return WaitFor("consumed on the UI thread", 10.0, [done]() { return done->load(); }, c);
			}
			if (a_kind == "craft") {
				// A craft through the open crafting menu's own code, as the player's click makes it: in a UI task (where
				// the menu runs), the recipe menu's recipe list is searched for a recipe that uses `uses` and whose
				// components should be there (carried + stored), the recipe is chosen ("ChooseItem"), and the menu's own
				// "CraftButtonPress" callback (what its Scaleform calls when the player crafts) crafts it: the menu
				// checks its hooked counts, consumes the materials through PlayerCharacter::RemoveItem and adds the
				// result.
				auto* uses = Item(s, "uses");
				auto* product = s.contains("product") ? Item(s, "product") : nullptr;  // optional: that recipe only
				if (!uses || !player || (s.contains("product") && !product)) {
					AbortTest(c, "craft", "item not found");
					return std::nullopt;
				}
				struct Outcome
				{
					std::atomic<bool>   done{ false };
					bool                listed{ false };
					bool                pressed{ false };
					RE::TESBoundObject* made{ nullptr };
					std::int32_t        madeCount{ 1 };
					std::int32_t        before{ 0 };
					std::string         detail;
				};
				auto outcome = std::make_shared<Outcome>();
				SKSE::GetTaskInterface()->AddUITask([uses, product, outcome]() {
					class Callbacks final : public RE::FxDelegateHandler::CallbackProcessor
					{
					public:
						void Process(const RE::GString& a_name, RE::FxDelegateHandler::CallbackFn* a_fn) override { fns.emplace(a_name.c_str(), a_fn); }
						std::map<std::string, RE::FxDelegateHandler::CallbackFn*> fns;
					};
					auto finish = [&](std::string a_detail) {
						outcome->detail = std::move(a_detail);
						outcome->done = true;
					};
					auto*      p = game::Player();
					auto*      ui = RE::UI::GetSingleton();
					const auto menu = ui ? ui->GetMenu<RE::CraftingMenu>() : RE::GPtr<RE::CraftingMenu>{};
					auto*      sub = menu ? menu->GetCraftingSubMenu() : nullptr;
					auto*      recipes = sub ? skyrim_cast<RE::CraftingSubMenus::ConstructibleObjectMenu*>(sub) : nullptr;
					if (!p || !recipes) {
						return finish("no recipe menu open");
					}
					logger::info("AutoTest: crafting on the UI thread ({}): {} recipes listed", ThreadId(), recipes->recipes.size());
					std::optional<std::uint32_t> pick;
					RE::TESBoundObject*          made = nullptr;
					std::uint16_t                madeCount = 1;
					for (std::uint32_t i = 0; i < recipes->recipes.size() && !pick; ++i) {
						auto* recipe = recipes->recipes[i].constructibleObject;
						if (!recipe) {
							continue;
						}
						bool usesIt = false;
						bool enough = true;
						std::string parts;
						recipe->requiredItems.ForEachContainerObject([&](RE::ContainerObject& a_part) {
							auto*      object = a_part.obj;
							const auto carried = object ? CountIn(game::Player(), object) : 0;
							// Picked by what should be there (carried + the session's stored count), so the craft below is the
							// menu's own verdict on what its hooked count says.
							const auto have = carried + sort::crafting::storage::Stored(object);
							usesIt = usesIt || object == uses;
							enough = enough && have >= a_part.count;
							parts += std::format("{}{} {}/{} (carried {}, hooked count {})", parts.empty() ? "" : ", ", Name(object), have, a_part.count, carried,
								carried + hooks::crafting::Added(object));
							return RE::BSContainer::ForEachResult::kContinue;
						});
						auto* result = recipe->createdItem ? recipe->createdItem->As<RE::TESBoundObject>() : nullptr;
						if (!usesIt || (product && result != product)) {
							continue;
						}
						outcome->listed = true;
						logger::info("AutoTest:   recipe {} for {}: {}{}", i, Name(result), parts, enough ? "" : " (not enough)");
						if (enough && result) {
							pick = i;
							made = result;
							madeCount = std::max<std::uint16_t>(1, recipe->data.numConstructed);
						}
					}
					if (!pick) {
						return finish(outcome->listed ? "no listed recipe with enough materials" : std::format("no listed recipe uses {}", Name(uses)));
					}
					Callbacks callbacks;
					recipes->Accept(&callbacks);
					std::string names;
					for (const auto& [name, fn] : callbacks.fns) {
						names += (names.empty() ? "" : ", ") + name;
					}
					logger::info("AutoTest:   the recipe menu's callbacks: {}", names);
					// The recipe menu registers "CraftButtonPress" (what its Scaleform calls when the player crafts), after
					// "ChooseItem" (the selection).
					const auto craft = callbacks.fns.find("CraftButtonPress");
					if (craft == callbacks.fns.end() || !craft->second) {
						return finish("the recipe menu has no CraftButtonPress callback");
					}
					if (const auto choose = callbacks.fns.find("ChooseItem"); choose != callbacks.fns.end() && choose->second) {
						const RE::GFxValue         index(static_cast<double>(*pick));
						const RE::FxDelegateArgs args(RE::GFxValue{}, recipes, recipes->view.get(), &index, 1);
						choose->second(args);
					}
					recipes->currentIndex = *pick;
					outcome->made = made;
					outcome->madeCount = madeCount;
					outcome->before = CountIn(game::Player(), made);
					logger::info("AutoTest:   crafting {} (recipe {}) through the menu's CraftButtonPress", Name(made), *pick);
					const RE::FxDelegateArgs args(RE::GFxValue{}, recipes, recipes->view.get(), nullptr, 0);
					craft->second(args);
					outcome->pressed = true;
					finish(std::format("{} (recipe {})", Name(made), *pick));
				});
				// Then the menu asks "Do you want to create this item?" (a message box, shown a frame later): Yes is pressed
				// in a UI task, as the player's click would, and the craft runs from the box's callback.
				auto crafted = [outcome]() {
					return outcome->made && CountIn(game::Player(), outcome->made) - outcome->before >= outcome->madeCount;
				};
				auto record = [outcome, &c]() {
					const auto now = outcome->made ? CountIn(game::Player(), outcome->made) : 0;
					c.Record(outcome->made && now - outcome->before >= outcome->madeCount, "crafted through the crafting menu",
						std::format("{}: {} -> {}", outcome->detail, outcome->before, now));
				};
				return WaitFor("crafting menu answered", 10.0, [outcome]() { return outcome->done.load(); }, c, true, [outcome, uses, crafted, record, &c]() {
					c.Record(outcome->listed, std::format("the crafting menu lists a recipe that uses {}", Name(uses)), outcome->detail);
					if (!outcome->pressed) {
						c.Record(false, "crafted through the crafting menu", outcome->detail);
						return;
					}
					Wait confirm;
					confirm.what = "the crafting menu's confirmation";
					confirm.deadline = Clock::now() + 5s;
					confirm.done = [crafted]() { return crafted() || !MessageBoxText().empty(); };
					confirm.finish = [crafted, record, &c](bool) {
						const auto text = MessageBoxText();
						if (crafted() || text.empty()) {
							record();
							return;
						}
						auto pressed = std::make_shared<std::atomic<bool>>(false);
						SKSE::GetTaskInterface()->AddUITask([text, pressed]() {
							logger::info("AutoTest:   answering \"{}\" with Yes on the UI thread ({})", text, ThreadId());
							PressMessageBoxButton(0);
							*pressed = true;
						});
						Wait done;
						done.what = "crafted after the confirmation";
						done.deadline = Clock::now() + 5s;
						done.done = [crafted, pressed]() { return pressed->load() && crafted(); };
						done.finish = [record](bool) { record(); };
						c.next = std::move(done);
					};
					c.next = std::move(confirm);
				});
			}
			if (a_kind == "writeLayout" || a_kind == "deleteLayout" || a_kind == "writeBackup" || a_kind == "deleteBackup") {
				// The plan's own home (refused at startup when a layout of the player's existed), or with "cell" another
				// cell this plan has entered (an `enter` step), for a second home's setup. Another cell's existing file
				// is only replaced when this run wrote it (the runner sets the player's own aside before the game starts).
				const bool backup = a_kind.ends_with("Backup");
				auto       home = c.home;
				if (s.contains("cell")) {
					const auto cellRef = Ref(s["cell"]);
					auto*      cellForm = cellRef ? game::LookupForm(*cellRef) : nullptr;
					if (!cellRef || !cellForm || !(*cellRef == c.home || c.planCells.contains(cellForm->GetFormID()))) {
						AbortTest(c, a_kind, std::format("{} is not a cell this plan entered", Text(s["cell"])));
						return std::nullopt;
					}
					if (!(*cellRef == c.home) && !backup && std::filesystem::exists(LayoutFile(*cellRef)) && !c.writtenLayouts.contains(cellRef->ToString())) {
						AbortTest(c, a_kind, std::format("{} has a setup file this run did not write", cellRef->ToString()));
						return std::nullopt;
					}
					home = *cellRef;
					if (!backup) {
						c.writtenLayouts.insert(cellRef->ToString());
					}
				}
				const auto path = backup ? BackupFile(home) : std::optional(LayoutFile(home));
				if (!path) {
					AbortTest(c, a_kind, "no path");
					return std::nullopt;
				}
				std::error_code ec;
				if (a_kind.starts_with("delete")) {
					std::filesystem::remove(*path, ec);
				} else {
					std::ofstream out(*path, std::ios::binary | std::ios::trunc);
					out << s.value("text", ""s);
				}
				logger::info("AutoTest: {} {}", a_kind, home::Store::PathText(*path));
				return std::nullopt;
			}
			if (a_kind == "ownCell") {
				// Buying the home: the player joins the faction that owns the cell.
				auto* cell = PlayerCell();
				auto* owner = cell ? cell->GetOwner() : nullptr;
				if (auto* faction = owner ? owner->As<RE::TESFaction>() : nullptr; faction && player && !player->IsInFaction(faction)) {
					player->AddToFaction(faction, 0);
					logger::info("AutoTest: the player joined {} (owner of the cell)", Name(faction));
				} else if (owner && !owner->As<RE::TESFaction>()) {
					cell->SetOwner(RE::TESForm::LookupByID(game::kPlayerFactionID));
					logger::info("AutoTest: the cell's owner {} replaced by the player faction", Name(owner));
				}
				return std::nullopt;
			}
			c.Record(false, std::format("unknown action \"{}\"", a_kind));
			c.abortTest = true;
			return std::nullopt;
		}
	}

	std::string MessageBoxText()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui || !ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME)) {
			return {};
		}
		const auto menu = ui->GetMenu(RE::MessageBoxMenu::MENU_NAME);
		if (!menu || !menu->uiMovie) {
			return "(no movie)";
		}
		for (const auto* path : { "_root.MessageMenu.MessageText.text", "_root.MessageMenu.MessageText.htmlText", "_root.MessageText.text" }) {
			RE::GFxValue value;
			if (menu->uiMovie->GetVariable(&value, path) && value.IsString()) {
				return value.GetString();
			}
		}
		return "(text not found)";
	}

	bool PressMessageBoxButton(std::int32_t a_index)
	{
		auto*      ui = RE::UI::GetSingleton();
		const auto menu = ui && ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME) ? ui->GetMenu(RE::MessageBoxMenu::MENU_NAME) : nullptr;
		if (!menu || !menu->fxDelegate || !menu->uiMovie) {
			return false;
		}
		// What the box's own button does: GameDelegate.call("buttonPress", [index]) (the first value is the response id).
		RE::GFxValue args[2];
		args[1].SetNumber(static_cast<double>(a_index));
		menu->fxDelegate->Callback(menu->uiMovie.get(), "buttonPress", args, 2);
		return true;
	}

	void Console(std::string_view a_command, RE::TESObjectREFR* a_target)
	{
		const auto factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
		const auto script = factory ? factory->Create() : nullptr;
		if (!script) {
			logger::error("AutoTest: no script factory; cannot run \"{}\"", a_command);
			return;
		}
		logger::info("AutoTest: console: {}", a_command);
		script->SetCommand(a_command);
		script->CompileAndRun(a_target);
		delete script;
	}

	bool InCell(std::optional<RE::FormID> a_cell)
	{
		auto* ui = RE::UI::GetSingleton();
		auto* player = game::Player();
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!ui || !cell || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) || ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || !player->Get3D()) {
			return false;
		}
		if (a_cell && cell->GetFormID() != *a_cell) {
			return false;
		}
		return cell->IsAttached();
	}

	std::string CocTarget(const core::FormRef& a_cell)
	{
		auto*       form = game::LookupForm(a_cell);
		auto*       cell = form ? form->As<RE::TESObjectCELL>() : nullptr;
		const char* edid = cell ? cell->GetFormEditorID() : nullptr;
		return edid ? std::string(edid) : std::string{};
	}

	std::optional<Wait> RunStep(const Json& a_step, Context& a_ctx)
	{
		try {
			if (const auto it = a_step.find("do"); it != a_step.end()) {
				const auto kind = it->get<std::string>();
				a_ctx.actionMark = a_ctx.stepMark;
				auto wait = RunAction(kind, a_step, a_ctx);
				// Moves raise container-changed events that Dova Stash handles in later tasks (learning, items that come
				// back): let them run before the next step, as they would between two things a player does.
				static const std::set<std::string, std::less<>> kMoves{ "op", "additem", "additemMany", "removeitem", "store", "take", "consume",
					"sellConfirmed", "buyBack", "fill", "makeMaster", "useTemplate", "reset", "restore" };
				if (!wait && !a_ctx.abortTest && kMoves.contains(kind)) {
					const auto until = Clock::now() + std::chrono::milliseconds(static_cast<std::int64_t>(a_step.value("settle", 0.5) * 1000.0));
					return WaitFor("settle", 10.0, [until]() { return Clock::now() >= until; }, a_ctx, false);
				}
				return wait;
			}
			if (const auto it = a_step.find("check"); it != a_step.end()) {
				return RunCheck(it->get<std::string>(), a_step, a_ctx);
			}
			a_ctx.Record(false, "step without \"do\" or \"check\"", a_step.dump());
		} catch (const std::exception& e) {
			a_ctx.Record(false, std::format("step {} threw", a_step.dump()), e.what());
			a_ctx.abortTest = true;
		}
		return std::nullopt;
	}
}
