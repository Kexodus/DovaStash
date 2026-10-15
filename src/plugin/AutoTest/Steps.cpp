#include "AutoTest/Steps.h"

#include "AutoTest/LogCapture.h"
#include "Controller.h"
#include "Events/EventSinks.h"
#include "Game/FormUtil.h"
#include "Home/HomeRegistry.h"
#include "Home/Store.h"
#include "Hooks/CraftingHooks.h"
#include "Settings.h"
#include "Sort/CraftingStorage.h"
#include "Sort/DisplayFiller.h"

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

		void Collect(Snapshot& a_snap, const std::string& a_holder, RE::TESObjectREFR* a_ref)
		{
			if (!a_ref) {
				return;
			}
			auto& counts = a_snap.holders[a_holder];
			for (const auto& [object, count] : a_ref->GetInventoryCounts()) {
				if (object && count > 0) {
					const auto ref = game::ToFormRef(object);
					counts[ref ? ref->ToString() : game::Describe(object)] += count;
				}
			}
		}

		// The player and every container of the player's cell.
		Snapshot TakeSnapshot()
		{
			Snapshot snap;
			Collect(snap, "player", game::Player());
			if (auto* cell = PlayerCell()) {
				cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
					if (a_ref && game::IsContainerRef(a_ref) && !a_ref->IsDisabled()) {
						const auto ref = game::ToFormRef(a_ref);
						Collect(snap, ref ? ref->ToString() : game::Describe(a_ref), a_ref);
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
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
					const auto  want = s.value("source", ""s);
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
					c);
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
				RE::TESObjectREFR* target = object;
				for (int depth = 0; depth < 8; ++depth) {
					const auto* parent = target->extraList.GetByType<RE::ExtraEnableStateParent>();
					auto        next = parent ? parent->parent.get() : RE::NiPointer<RE::TESObjectREFR>{};
					if (!next || !next->IsDisabled()) {
						break;
					}
					target = next.get();
				}
				logger::info("AutoTest: enabling {}", Name(target));
				Console("enable", target);
				if (target != object && object->IsDisabled()) {
					Console("enable", object);
				}
				return WaitFor(std::format("{} enabled", ref->ToString()), 5.0, [object]() { return !object->IsDisabled(); }, c, false);
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
			if (a_kind == "displayUse") {
				const auto cell = cellId();
				if (const auto ref = cell ? refArg() : std::nullopt) {
					Controller::Get().SetDisplayUsed(*cell, *ref, s.value("on", true));
				}
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
				auto wait = WaitFor(std::format("container menu of {} open", ref->ToString()), 10.0, menuUp, c);
				wait.deadline = Clock::now() + 4s;
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
				if (store) {
					player->RemoveItem(item, s.value("count", 1), RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, other);
				} else {
					other->RemoveItem(item, s.value("count", 1), RE::ITEM_REMOVE_REASON::kRemove, nullptr, player);
				}
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
				station->ActivateRef(player, 0, nullptr, 0, false);
				const auto menu = s.value("menu", std::string(RE::CraftingMenu::MENU_NAME));
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
				auto wait = WaitFor(std::format("{} open", menu), s.value("timeout", 20.0), menuUp, c);
				wait.deadline = Clock::now() + 6s;
				// Activating the station from the plugin may not seat the player: then the console does it.
				wait.finish = [&c, menuUp, menu, stationId = station->GetFormID(), timeout = s.value("timeout", 20.0)](bool a_timedOut) {
					if (!a_timedOut) {
						return;
					}
					logger::info("AutoTest: the station did not open {} (open menus: {}); activating it with the console", menu, OpenMenus());
					Console("activate player", RE::TESForm::LookupByID<RE::TESObjectREFR>(stationId));
					c.next = WaitFor(std::format("{} open", menu), timeout, menuUp, c);
				};
				return wait;
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
				// Only the plan's own home (refused at startup when a layout of the player's existed).
				const bool backup = a_kind.ends_with("Backup");
				const auto path = backup ? BackupFile(c.home) : std::optional(LayoutFile(c.home));
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
