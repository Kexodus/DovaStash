#include "dova/core/Category.h"

#include <algorithm>
#include <map>
#include <system_error>

#include "JsonUtil.h"
#include "dova/core/Enums.h"

namespace dova::core
{
	using detail::json;

	// --- Patterns ----------------------------------------------------------------------------------

	namespace
	{
		bool IsIdChar(char a_ch) noexcept
		{
			return (a_ch >= 'a' && a_ch <= 'z') || (a_ch >= 'A' && a_ch <= 'Z') || (a_ch >= '0' && a_ch <= '9') || a_ch == '_' || a_ch == '-';
		}

		// Is `a_prefix` the id itself or an ancestor of it (case-insensitive)?
		bool IsSelfOrDescendant(std::string_view a_prefix, std::string_view a_id) noexcept
		{
			if (a_id.size() < a_prefix.size() || !IEquals(a_id.substr(0, a_prefix.size()), a_prefix)) {
				return false;
			}
			return a_id.size() == a_prefix.size() || a_id[a_prefix.size()] == '.';
		}
	}

	bool IsValidCategoryId(std::string_view a_id)
	{
		if (a_id.empty() || a_id.front() == '.' || a_id.back() == '.') {
			return false;
		}
		char prev = '\0';
		for (const char ch : a_id) {
			if (ch == '.') {
				if (prev == '.') {
					return false;
				}
			} else if (!IsIdChar(ch)) {
				return false;
			}
			prev = ch;
		}
		return true;
	}

	bool IsValidCategoryPattern(std::string_view a_pattern)
	{
		if (a_pattern == "*") {
			return true;
		}
		if (a_pattern.size() > 2 && a_pattern.ends_with(".*")) {
			return IsValidCategoryId(a_pattern.substr(0, a_pattern.size() - 2));
		}
		return IsValidCategoryId(a_pattern);
	}

	bool CategoryPatternMatches(std::string_view a_pattern, std::string_view a_categoryId)
	{
		if (!IsValidCategoryPattern(a_pattern) || !IsValidCategoryId(a_categoryId)) {
			return false;
		}
		if (a_pattern == "*") {
			return true;
		}
		if (a_pattern.ends_with(".*")) {
			return IsSelfOrDescendant(a_pattern.substr(0, a_pattern.size() - 2), a_categoryId);
		}
		return IEquals(a_pattern, a_categoryId);
	}

	int CategoryDepth(std::string_view a_id)
	{
		if (a_id.empty()) {
			return 0;
		}
		return 1 + static_cast<int>(std::ranges::count(a_id, '.'));
	}

	int CategoryPatternSpecificity(std::string_view a_pattern)
	{
		if (!IsValidCategoryPattern(a_pattern)) {
			return -1;
		}
		if (a_pattern == "*") {
			return 0;
		}
		if (a_pattern.ends_with(".*")) {
			return CategoryDepth(a_pattern.substr(0, a_pattern.size() - 2));  // 1..N
		}
		return 1000 + CategoryDepth(a_pattern);  // exact always beats wildcards
	}

	bool CategoryMatch::Empty() const noexcept
	{
		return !formTypes && !keywordsAny && !keywordsAll && !keywordsNone && !forms && !formLists && !plugins && !weaponTypes &&
		       !armorTypes && !bookTeaches && !alchemy && !soulGem && !enchanted && !ammo;
	}

	// --- Parsing -----------------------------------------------------------------------------------

	namespace
	{
		struct Ctx
		{
			std::string  source;
			Diagnostics& diags;
			bool         failed{ false };

			void Error(const std::string& a_msg)
			{
				diags.Error(source, a_msg);
				failed = true;
			}
			void Warn(const std::string& a_msg) { diags.Warn(source, a_msg); }
		};

		// "; did you mean \"Heavy\"?" for a near miss of an enum name ("OneHandedSword"), or for a value that contains one
		// ("HeavyArmor", "Weapon" for WEAP), the longest when several do; else empty.
		template <class E>
		std::string EnumHint(std::string_view a_name)
		{
			const auto                    all = ValidNames<E>();
			std::vector<std::string_view> names;
			for (std::size_t start = 0; start < all.size();) {
				const auto comma = all.find(", ", start);
				const auto end = comma == std::string::npos ? all.size() : comma;
				names.emplace_back(std::string_view(all).substr(start, end - start));
				start = comma == std::string::npos ? all.size() : comma + 2;
			}
			if (auto hint = detail::DidYouMean(a_name, names); !hint.empty()) {
				return hint;
			}
			const auto       lower = ToLower(a_name);
			std::string_view best;
			for (const auto n : names) {
				if (n.size() >= 4 && n.size() > best.size() && lower.find(ToLower(n)) != std::string::npos) {
					best = n;
				}
			}
			return best.empty() ? std::string() : "; did you mean \"" + std::string(best) + "\"?";
		}

		template <class E, class Parser>
		std::optional<std::vector<E>> ReadEnumList(Ctx& a_ctx, const json& a_value, const std::string& a_where, Parser a_parse)
		{
			auto names = detail::ReadStringList(a_value, a_ctx.source, a_where, a_ctx.diags);
			if (!names) {
				a_ctx.failed = true;
				return std::nullopt;
			}
			std::vector<E> out;
			for (const auto& name : *names) {
				if (auto v = a_parse(name)) {
					if (std::ranges::find(out, *v) == out.end()) {
						out.push_back(*v);
					}
				} else {
					a_ctx.Error(a_where + ": unknown value \"" + name + "\" (one of " + ValidNames<E>() + ")" + EnumHint<E>(name));
				}
			}
			if (out.empty() && names->empty()) {
				a_ctx.Warn(a_where + ": empty list matches nothing");
			}
			return out;
		}

		std::optional<std::vector<std::string>> ReadKeywordList(Ctx& a_ctx, const json& a_value, const std::string& a_where, bool a_warnEmpty)
		{
			auto names = detail::ReadStringList(a_value, a_ctx.source, a_where, a_ctx.diags);
			if (!names) {
				a_ctx.failed = true;
				return std::nullopt;
			}
			std::erase_if(*names, [](const std::string& s) { return s.empty(); });
			if (a_warnEmpty && names->empty()) {
				a_ctx.Warn(a_where + ": empty list matches nothing");
			}
			return names;
		}

		std::optional<std::vector<FormRef>> ReadFormList(Ctx& a_ctx, const json& a_value, const std::string& a_where)
		{
			auto names = detail::ReadStringList(a_value, a_ctx.source, a_where, a_ctx.diags);
			if (!names) {
				a_ctx.failed = true;
				return std::nullopt;
			}
			std::vector<FormRef> out;
			for (const auto& name : *names) {
				if (auto ref = FormRef::Parse(name)) {
					out.push_back(std::move(*ref));
				} else {
					a_ctx.Warn(a_where + ": invalid form reference \"" + name + "\" (ignored): " + FormRefError(name));
				}
			}
			return out;
		}

		std::optional<CategoryMatch> ParseMatch(Ctx& a_ctx, const json& a_match, const std::string& a_where)
		{
			CategoryMatch m;
			if (!a_match.is_object()) {
				a_ctx.Error(a_where + ": expected an object, got " + detail::TypeName(a_match));
				return std::nullopt;
			}
			for (const auto& [key, value] : a_match.items()) {
				const std::string where = a_where + "." + key;
				if (key == "formTypes") {
					m.formTypes = ReadEnumList<ItemType>(a_ctx, value, where, ParseItemType);
				} else if (key == "keywordsAny") {
					m.keywordsAny = ReadKeywordList(a_ctx, value, where, true);
				} else if (key == "keywordsAll") {
					m.keywordsAll = ReadKeywordList(a_ctx, value, where, false);
				} else if (key == "keywordsNone") {
					m.keywordsNone = ReadKeywordList(a_ctx, value, where, false);
				} else if (key == "forms") {
					m.forms = ReadFormList(a_ctx, value, where);
				} else if (key == "formLists") {
					m.formLists = ReadFormList(a_ctx, value, where);
				} else if (key == "plugins") {
					m.plugins = ReadKeywordList(a_ctx, value, where, true);
				} else if (key == "weaponTypes") {
					m.weaponTypes = ReadEnumList<WeaponType>(a_ctx, value, where, ParseWeaponType);
				} else if (key == "armorTypes") {
					m.armorTypes = ReadEnumList<ArmorType>(a_ctx, value, where, ParseArmorType);
				} else if (key == "bookTeaches") {
					m.bookTeaches = ReadEnumList<BookTeaches>(a_ctx, value, where, ParseBookTeaches);
				} else if (key == "alchemy") {
					m.alchemy = ReadEnumList<AlchemyKind>(a_ctx, value, where, ParseAlchemyKind);
				} else if (key == "soulGem") {
					m.soulGem = ReadEnumList<SoulGemState>(a_ctx, value, where, ParseSoulGemState);
				} else if (key == "ammo") {
					m.ammo = ReadEnumList<AmmoKind>(a_ctx, value, where, ParseAmmoKind);
				} else if (key == "enchanted") {
					if (!value.is_boolean()) {
						a_ctx.Error(where + ": expected true or false, got " + detail::TypeName(value));
					} else {
						m.enchanted = value.get<bool>();
					}
				} else if (key.starts_with('_') || key.starts_with('$')) {
					// comment-style keys
				} else {
					a_ctx.Error(where + ": unknown predicate \"" + key + "\" (one of formTypes, keywordsAny, keywordsAll, keywordsNone, forms, formLists, plugins, " +
								"weaponTypes, armorTypes, bookTeaches, alchemy, soulGem, enchanted, ammo)" +
								detail::DidYouMean(key, { "formTypes", "keywordsAny", "keywordsAll", "keywordsNone", "forms", "formLists", "plugins", "weaponTypes",
															"armorTypes", "bookTeaches", "alchemy", "soulGem", "enchanted", "ammo" }));
				}
			}
			if (m.forms && m.forms->empty() && !m.formLists) {
				a_ctx.Warn(a_where + ".forms: no valid forms; category matches nothing");
			}
			return m;
		}

		std::optional<CategoryDef> ParseCategory(Ctx& a_ctx, const json& a_cat, const std::string& a_where)
		{
			if (!a_cat.is_object()) {
				a_ctx.Error(a_where + ": expected an object, got " + detail::TypeName(a_cat));
				return std::nullopt;
			}
			CategoryDef def;
			def.source = a_ctx.source;

			const auto idIt = a_cat.find("id");
			if (idIt == a_cat.end() || !idIt->is_string()) {
				a_ctx.Error(a_where + ": missing string \"id\"");
				return std::nullopt;
			}
			def.id = ToLower(idIt->get<std::string>());
			if (!IsValidCategoryId(def.id)) {
				a_ctx.Error(a_where + ": invalid category id \"" + idIt->get<std::string>() + "\" (use dot-separated [a-z0-9_-] segments)");
				return std::nullopt;
			}
			const std::string where = a_where + "(" + def.id + ")";

			for (const auto& [key, value] : a_cat.items()) {
				if (key == "id") {
					continue;
				}
				if (key == "name") {
					if (value.is_string()) {
						def.name = value.get<std::string>();
					} else {
						a_ctx.Warn(where + ".name: expected a string (ignored)");
					}
				} else if (key == "priority") {
					if (value.is_number_unsigned() && value.get<std::uint64_t>() > static_cast<std::uint64_t>(INT32_MAX)) {
						def.priority = INT32_MAX;  // get<int64_t>() would wrap a value above INT64_MAX to a negative priority
					} else if (value.is_number_integer()) {
						const auto p = value.get<std::int64_t>();
						def.priority = static_cast<std::int32_t>(std::clamp<std::int64_t>(p, INT32_MIN, INT32_MAX));
					} else {
						a_ctx.Error(where + ".priority: expected an integer, got " + detail::TypeName(value));
					}
				} else if (key == "match") {
					if (auto m = ParseMatch(a_ctx, value, where + ".match")) {
						def.match = std::move(*m);
					}
				} else if (key == "icon") {
					// Cosmetic: problems are warnings and only drop the icon, never the category.
					Diagnostics iconDiags;
					def.icon = ParseIconRef(value, where + ".icon", iconDiags);
					for (const auto& d : iconDiags.Items()) {
						a_ctx.Warn(d.source + ": " + d.message + (d.severity == Diagnostic::Severity::Error ? " (icon ignored)" : ""));
					}
				} else if (key == "description" || key.starts_with('_') || key.starts_with('$')) {
					// documentation only
				} else {
					a_ctx.Warn(where + ": unknown field \"" + key + "\" (ignored)" +
							   detail::DidYouMean(key, { "id", "name", "priority", "match", "icon", "description" }));
				}
			}
			if (def.name.empty()) {
				def.name = def.id;
			}
			return def;
		}
	}

	std::vector<CategoryDef> ParseCategoryDocument(const json& a_doc, std::string_view a_source, Diagnostics& a_diags)
	{
		std::vector<CategoryDef> out;
		const std::string        source(a_source);
		if (!a_doc.is_object()) {
			a_diags.Error(source, "expected a JSON object at the top level");
			return out;
		}
		const auto schemaIt = a_doc.find("$schema");
		if (schemaIt == a_doc.end()) {
			a_diags.Warn(source, "missing \"$schema\"; assuming \"dovastash-categories-1\"");
		} else if (!schemaIt->is_string() || schemaIt->get<std::string>() != kCategoriesSchema) {
			a_diags.Error(source, "unsupported \"$schema\" (expected \"dovastash-categories-1\")");
			return out;
		}
		for (const auto& [key, value] : a_doc.items()) {  // before the "categories" check: "categorys" gets its hint
			if (key != "$schema" && key != "categories" && key != "description" && !key.starts_with('_')) {
				a_diags.Warn(source, "unknown top-level field \"" + key + "\" (ignored)" + detail::DidYouMean(key, { "$schema", "categories", "description" }));
			}
		}
		const auto catsIt = a_doc.find("categories");
		if (catsIt == a_doc.end() || !catsIt->is_array()) {
			a_diags.Error(source, "missing \"categories\" array");
			return out;
		}

		std::map<std::string, std::size_t> seen;
		for (std::size_t i = 0; i < catsIt->size(); ++i) {
			Ctx  ctx{ source, a_diags };
			auto def = ParseCategory(ctx, (*catsIt)[i], "categories[" + std::to_string(i) + "]");
			if (!def || ctx.failed) {
				if (def) {
					a_diags.Error(source, "category \"" + def->id + "\" skipped because of the errors above");
				}
				continue;
			}
			if (const auto it = seen.find(def->id); it != seen.end()) {
				a_diags.Warn(source, "duplicate category id \"" + def->id + "\" in the same file; the later definition wins");
				out[it->second] = std::move(*def);
			} else {
				seen.emplace(def->id, out.size());
				out.push_back(std::move(*def));
			}
		}
		return out;
	}

	std::vector<CategoryDef> ParseCategoryText(std::string_view a_text, std::string_view a_source, Diagnostics& a_diags)
	{
		auto doc = detail::ParseJson(a_text, a_source, a_diags);
		if (!doc) {
			return {};
		}
		return ParseCategoryDocument(*doc, a_source, a_diags);
	}

	nlohmann::ordered_json CategoryToJson(const CategoryDef& a_def)
	{
		using ojson = nlohmann::ordered_json;
		ojson out = ojson::object();
		out["id"] = a_def.id;
		if (!a_def.name.empty() && a_def.name != a_def.id) {
			out["name"] = a_def.name;
		}
		if (a_def.priority != 0) {
			out["priority"] = a_def.priority;
		}
		if (a_def.icon) {
			out["icon"] = ojson::parse(IconRefToJson(*a_def.icon).dump());
		}

		const auto& m = a_def.match;
		if (m.Empty()) {
			return out;
		}
		ojson match = ojson::object();
		const auto strings = [](const std::vector<std::string>& a_list) { return ojson(a_list); };
		const auto forms = [](const std::vector<FormRef>& a_list) {
			ojson arr = ojson::array();
			for (const auto& f : a_list) {
				arr.push_back(f.ToString());
			}
			return arr;
		};
		const auto enums = [](const auto& a_list) {
			ojson arr = ojson::array();
			for (const auto v : a_list) {
				arr.push_back(std::string(ToString(v)));
			}
			return arr;
		};
		if (m.formTypes) {
			match["formTypes"] = enums(*m.formTypes);
		}
		if (m.keywordsAny) {
			match["keywordsAny"] = strings(*m.keywordsAny);
		}
		if (m.keywordsAll) {
			match["keywordsAll"] = strings(*m.keywordsAll);
		}
		if (m.keywordsNone) {
			match["keywordsNone"] = strings(*m.keywordsNone);
		}
		if (m.forms) {
			match["forms"] = forms(*m.forms);
		}
		if (m.formLists) {
			match["formLists"] = forms(*m.formLists);
		}
		if (m.plugins) {
			match["plugins"] = strings(*m.plugins);
		}
		if (m.weaponTypes) {
			match["weaponTypes"] = enums(*m.weaponTypes);
		}
		if (m.armorTypes) {
			match["armorTypes"] = enums(*m.armorTypes);
		}
		if (m.bookTeaches) {
			match["bookTeaches"] = enums(*m.bookTeaches);
		}
		if (m.alchemy) {
			match["alchemy"] = enums(*m.alchemy);
		}
		if (m.soulGem) {
			match["soulGem"] = enums(*m.soulGem);
		}
		if (m.enchanted) {
			match["enchanted"] = *m.enchanted;
		}
		if (m.ammo) {
			match["ammo"] = enums(*m.ammo);
		}
		out["match"] = std::move(match);
		return out;
	}

	std::vector<CategoryDef> MergeCategoryFiles(std::span<const std::vector<CategoryDef>> a_files, Diagnostics& a_diags)
	{
		std::vector<CategoryDef>                     merged;
		std::unordered_map<std::string, std::size_t> index;
		for (const auto& file : a_files) {
			for (const auto& def : file) {
				if (const auto it = index.find(def.id); it != index.end()) {
					auto& prev = merged[it->second];
					if (prev.source != def.source) {
						a_diags.Warn(def.source, "category \"" + def.id + "\" overrides the definition from " + prev.source);
					}
					prev = def;
				} else {
					index.emplace(def.id, merged.size());
					merged.push_back(def);
				}
			}
		}
		return merged;
	}

	std::vector<CategoryDef> LoadCategoryDirectory(const std::filesystem::path& a_dir, Diagnostics& a_diags)
	{
		std::error_code ec;
		if (!std::filesystem::is_directory(a_dir, ec)) {
			a_diags.Warn(detail::PathToUtf8(a_dir), "categories directory not found");
			return {};
		}
		std::vector<std::filesystem::path> files;
		for (const auto& entry : std::filesystem::directory_iterator(a_dir, ec)) {
			if (entry.is_regular_file(ec) && IEquals(detail::PathToUtf8(entry.path().extension()), ".json")) {
				files.push_back(entry.path());
			}
		}
		std::ranges::sort(files, [](const auto& a_lhs, const auto& a_rhs) {
			const auto l = detail::PathToUtf8(a_lhs.filename());
			const auto r = detail::PathToUtf8(a_rhs.filename());
			const int  c = ICompare(l, r);
			return c != 0 ? c < 0 : l < r;
		});

		std::vector<std::vector<CategoryDef>> parsed;
		for (const auto& file : files) {
			const auto source = "Categories/" + detail::PathToUtf8(file.filename());
			const auto text = detail::ReadTextFile(file);
			if (!text) {
				a_diags.Error(source, "cannot read file");
				continue;
			}
			parsed.push_back(ParseCategoryText(*text, source, a_diags));
		}
		return MergeCategoryFiles(parsed, a_diags);
	}

	// --- Compiled set -------------------------------------------------------------------------------

	namespace
	{
		std::vector<std::string> LowerAll(const std::vector<std::string>& a_in)
		{
			std::vector<std::string> out;
			out.reserve(a_in.size());
			for (const auto& s : a_in) {
				out.push_back(ToLower(s));
			}
			return out;
		}

		template <class T>
		bool ContainsOpt(const std::optional<std::vector<T>>& a_list, const std::optional<T>& a_value)
		{
			return a_value && std::ranges::find(*a_list, *a_value) != a_list->end();
		}

		bool HasKeyword(std::span<const std::string> a_lowerKeywords, const std::string& a_lowerKw)
		{
			return std::ranges::find(a_lowerKeywords, a_lowerKw) != a_lowerKeywords.end();
		}
	}

	CategorySet CategorySet::Compile(std::vector<CategoryDef> a_defs, const FormListResolver& a_resolver, Diagnostics& a_diags)
	{
		CategorySet set;
		set._info.reserve(a_defs.size());

		std::vector<Compiled> compiled;
		compiled.reserve(a_defs.size());

		for (auto& def : a_defs) {
			if (!IsValidCategoryId(def.id)) {
				a_diags.Error(def.source, "invalid category id \"" + def.id + "\" (skipped)");
				continue;
			}
			def.id = ToLower(def.id);
			std::size_t idx = set._info.size();
			if (const auto it = set._byId.find(def.id); it != set._byId.end()) {
				// Replace in place so the original definition-order slot is kept.
				a_diags.Warn(def.source, "duplicate category id \"" + def.id + "\" at compile time; the later definition wins");
				idx = it->second;
				set._info[idx].name = def.name.empty() ? def.id : def.name;
				set._info[idx].priority = def.priority;
				set._info[idx].source = def.source;
				set._info[idx].icon = def.icon;
			} else {
				set._byId.emplace(def.id, idx);
				set._info.push_back(Category{ def.id, def.name.empty() ? def.id : def.name, def.priority, CategoryDepth(def.id), idx, def.source, def.icon });
				compiled.emplace_back();
			}
			set._info[idx].match = def.match;

			auto& c = compiled[idx];
			c = Compiled{};
			c.info = idx;
			const auto& m = def.match;
			c.groupOnly = m.Empty();
			c.formTypes = m.formTypes;
			if (m.keywordsAny) {
				c.keywordsAny = LowerAll(*m.keywordsAny);
			}
			if (m.keywordsAll) {
				c.keywordsAll = LowerAll(*m.keywordsAll);
			}
			if (m.keywordsNone) {
				c.keywordsNone = LowerAll(*m.keywordsNone);
			}
			if (m.plugins) {
				c.plugins = LowerAll(*m.plugins);
			}
			if (m.forms || m.formLists) {
				std::unordered_set<FormRef> members;
				if (m.forms) {
					members.insert(m.forms->begin(), m.forms->end());
				}
				if (m.formLists) {
					for (const auto& list : *m.formLists) {
						std::vector<FormRef> resolved;
						if (a_resolver) {
							resolved = a_resolver(list);
						}
						if (resolved.empty()) {
							a_diags.Warn(def.source, "category \"" + def.id + "\": formList " + list.ToString() + " could not be resolved (missing plugin, wrong form type or empty list)");
						}
						members.insert(resolved.begin(), resolved.end());
					}
				}
				c.membership = std::move(members);
			}
			c.weaponTypes = m.weaponTypes;
			c.armorTypes = m.armorTypes;
			c.bookTeaches = m.bookTeaches;
			c.alchemy = m.alchemy;
			c.soulGem = m.soulGem;
			c.enchanted = m.enchanted;
			c.ammo = m.ammo;
		}

		// Rank: priority desc, depth desc, definition order asc.
		std::vector<std::size_t> order(set._info.size());
		for (std::size_t i = 0; i < order.size(); ++i) {
			order[i] = i;
		}
		std::ranges::stable_sort(order, [&](std::size_t a_lhs, std::size_t a_rhs) {
			const auto& l = set._info[a_lhs];
			const auto& r = set._info[a_rhs];
			if (l.priority != r.priority) {
				return l.priority > r.priority;
			}
			if (l.depth != r.depth) {
				return l.depth > r.depth;
			}
			return l.order < r.order;
		});
		set._rankOfInfo.assign(set._info.size(), 0);
		set._ranked.reserve(order.size());
		for (const auto i : order) {
			set._rankOfInfo[i] = set._ranked.size();
			set._ranked.push_back(std::move(compiled[i]));
		}
		return set;
	}

	bool CategorySet::Evaluate(const Compiled& a_c, const ItemInfo& a_item, std::span<const std::string> a_kw)
	{
		if (a_c.groupOnly) {
			return false;
		}
		if (a_c.formTypes && std::ranges::find(*a_c.formTypes, a_item.type) == a_c.formTypes->end()) {
			return false;
		}
		if (a_c.keywordsAny && std::ranges::none_of(*a_c.keywordsAny, [&](const std::string& k) { return HasKeyword(a_kw, k); })) {
			return false;
		}
		if (a_c.keywordsAll && !std::ranges::all_of(*a_c.keywordsAll, [&](const std::string& k) { return HasKeyword(a_kw, k); })) {
			return false;
		}
		if (a_c.keywordsNone && std::ranges::any_of(*a_c.keywordsNone, [&](const std::string& k) { return HasKeyword(a_kw, k); })) {
			return false;
		}
		if (a_c.membership && !a_c.membership->contains(a_item.form)) {
			return false;
		}
		if (a_c.plugins && std::ranges::none_of(*a_c.plugins, [&](const std::string& p) { return IEquals(p, a_item.form.plugin); })) {
			return false;
		}
		if (a_c.weaponTypes && !ContainsOpt(a_c.weaponTypes, a_item.weaponType)) {
			return false;
		}
		if (a_c.armorTypes && !ContainsOpt(a_c.armorTypes, a_item.armorType)) {
			return false;
		}
		if (a_c.bookTeaches && !ContainsOpt(a_c.bookTeaches, a_item.bookTeaches)) {
			return false;
		}
		if (a_c.alchemy && !ContainsOpt(a_c.alchemy, a_item.alchemyKind)) {
			return false;
		}
		if (a_c.soulGem && !ContainsOpt(a_c.soulGem, a_item.soulGemState)) {
			return false;
		}
		if (a_c.enchanted && *a_c.enchanted != a_item.enchanted) {
			return false;
		}
		if (a_c.ammo && !ContainsOpt(a_c.ammo, a_item.ammoKind)) {
			return false;
		}
		return true;
	}

	std::optional<std::string> CategorySet::Classify(const ItemInfo& a_item) const
	{
		const auto kw = LowerAll(a_item.keywords);
		for (const auto& c : _ranked) {
			if (Evaluate(c, a_item, kw)) {
				return _info[c.info].id;
			}
		}
		return std::nullopt;
	}

	std::vector<std::string> CategorySet::MatchAll(const ItemInfo& a_item) const
	{
		std::vector<std::string> out;
		const auto               kw = LowerAll(a_item.keywords);
		for (const auto& c : _ranked) {
			if (Evaluate(c, a_item, kw)) {
				out.push_back(_info[c.info].id);
			}
		}
		return out;
	}

	bool CategorySet::ItemMatches(std::string_view a_categoryId, const ItemInfo& a_item) const
	{
		const auto it = _byId.find(ToLower(a_categoryId));
		if (it == _byId.end()) {
			return false;
		}
		const auto kw = LowerAll(a_item.keywords);
		return Evaluate(_ranked[_rankOfInfo[it->second]], a_item, kw);
	}

	const CategorySet::Category* CategorySet::Find(std::string_view a_id) const
	{
		const auto it = _byId.find(ToLower(a_id));
		return it == _byId.end() ? nullptr : &_info[it->second];
	}

	bool CategorySet::AnyMatches(std::string_view a_pattern) const
	{
		return std::ranges::any_of(_info, [&](const Category& c) { return CategoryPatternMatches(a_pattern, c.id); });
	}

	std::vector<std::string_view> CategorySet::Ids() const
	{
		std::vector<std::string_view> ids;
		ids.reserve(_info.size());
		for (const auto& c : _info) {
			ids.emplace_back(c.id);
		}
		return ids;
	}
}
