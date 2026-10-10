#include "dova/core/Layout.h"

#include <algorithm>
#include <set>

#include "JsonUtil.h"
#include "dova/core/Category.h"
#include "dova/core/Version.h"

namespace dova::core
{
	using detail::json;
	using ojson = nlohmann::ordered_json;

	namespace
	{
		ojson ToOrdered(const json& a_value)
		{
			return ojson::parse(a_value.dump());
		}

		// Appends `a_extra`'s keys that `a_out` does not already have.
		void AppendExtra(ojson& a_out, const json& a_extra)
		{
			if (!a_extra.is_object()) {
				return;
			}
			for (const auto& [key, value] : a_extra.items()) {
				if (!a_out.contains(key)) {
					a_out[key] = ToOrdered(value);
				}
			}
		}

		std::optional<std::int32_t> ReadInt32(const json& a_value)
		{
			if (!a_value.is_number_integer()) {
				return std::nullopt;
			}
			const auto v = a_value.get<std::int64_t>();
			return static_cast<std::int32_t>(std::clamp<std::int64_t>(v, INT32_MIN, INT32_MAX));
		}

		void ValidateKeepList(const std::vector<std::string>& a_list, const std::string& a_source, const std::string& a_where, Diagnostics& a_diags)
		{
			for (std::size_t i = 0; i < a_list.size(); ++i) {
				if (KeepSelector::Parse(a_list[i]).kind == KeepSelector::Kind::Invalid) {
					a_diags.Warn(a_source, a_where + "[" + std::to_string(i) + "]: \"" + a_list[i] +
						"\" is not a token, form reference or category pattern (kept, but ignored)");
				}
			}
		}

		std::optional<QuotaRule> ParseQuotaRule(const json& a_value, const std::string& a_source, const std::string& a_where, Diagnostics& a_diags)
		{
			if (!a_value.is_object()) {
				a_diags.Error(a_source, a_where + ": expected an object, got " + detail::TypeName(a_value));
				return std::nullopt;
			}
			QuotaRule rule;
			bool      ok = true;
			bool      haveQuota = false;
			for (const auto& [key, value] : a_value.items()) {
				const auto where = a_where + "." + key;
				if (key == "category") {
					if (!value.is_string() || !IsValidCategoryPattern(value.get<std::string>())) {
						a_diags.Error(a_source, where + ": expected a category pattern string");
						ok = false;
					} else {
						rule.category = value.get<std::string>();
					}
				} else if (key == "keywordsAny") {
					if (auto list = detail::ReadStringList(value, a_source, where, a_diags)) {
						rule.keywordsAny = std::move(*list);
					} else {
						ok = false;
					}
				} else if (key == "forms") {
					if (auto list = detail::ReadStringList(value, a_source, where, a_diags)) {
						std::vector<FormRef> forms;
						for (const auto& s : *list) {
							if (auto ref = FormRef::Parse(s)) {
								forms.push_back(std::move(*ref));
							} else {
								a_diags.Error(a_source, where + ": invalid form reference \"" + s + "\"");
								ok = false;
							}
						}
						rule.forms = std::move(forms);
					} else {
						ok = false;
					}
				} else if (key == "quota") {
					if (auto q = ReadInt32(value); q && *q >= 0) {
						rule.quota = *q;
						haveQuota = true;
					} else {
						a_diags.Error(a_source, where + ": expected a non-negative integer");
						ok = false;
					}
				} else {
					rule.extra[key] = value;
				}
			}
			if (!haveQuota && ok) {
				a_diags.Error(a_source, a_where + ": missing \"quota\"");
				ok = false;
			}
			if (!rule.category && !rule.keywordsAny && !rule.forms && ok) {
				a_diags.Error(a_source, a_where + ": needs at least one of \"category\", \"keywordsAny\", \"forms\"");
				ok = false;
			}
			if (!ok) {
				return std::nullopt;
			}
			return rule;
		}

		ojson QuotaRuleToJson(const QuotaRule& a_rule)
		{
			ojson out = ojson::object();
			if (a_rule.category) {
				out["category"] = *a_rule.category;
			}
			if (a_rule.keywordsAny) {
				out["keywordsAny"] = *a_rule.keywordsAny;
			}
			if (a_rule.forms) {
				ojson forms = ojson::array();
				for (const auto& f : *a_rule.forms) {
					forms.push_back(f.ToString());
				}
				out["forms"] = std::move(forms);
			}
			out["quota"] = a_rule.quota;
			AppendExtra(out, a_rule.extra);
			return out;
		}

		std::optional<ContainerBinding> ParseContainer(const json& a_value, const std::string& a_source, const std::string& a_where, Diagnostics& a_diags)
		{
			if (!a_value.is_object()) {
				a_diags.Error(a_source, a_where + ": expected an object, got " + detail::TypeName(a_value) + " (kept verbatim)");
				return std::nullopt;
			}
			const auto refIt = a_value.find("ref");
			if (refIt == a_value.end() || !refIt->is_string()) {
				a_diags.Error(a_source, a_where + ": missing \"ref\" (kept verbatim)");
				return std::nullopt;
			}
			auto ref = FormRef::Parse(refIt->get<std::string>());
			if (!ref) {
				a_diags.Error(a_source, a_where + ".ref: invalid form reference \"" + refIt->get<std::string>() + "\" (kept verbatim)");
				return std::nullopt;
			}

			ContainerBinding c;
			c.ref = std::move(*ref);
			for (const auto& [key, value] : a_value.items()) {
				const auto where = a_where + "." + key;
				if (key == "ref") {
					continue;
				}
				if (key == "label" && value.is_string()) {
					c.label = value.get<std::string>();
				} else if (key == "ignored" && value.is_boolean()) {
					c.ignored = value.get<bool>();
				} else if (key == "categories" && (value.is_array() || value.is_string())) {
					if (auto list = detail::ReadStringList(value, a_source, where, a_diags)) {
						c.categories = std::move(*list);
						for (const auto& p : c.categories) {
							if (!IsValidCategoryPattern(p)) {
								a_diags.Warn(a_source, where + ": invalid category pattern \"" + p + "\" (kept, never matches)");
							}
						}
					}
				} else if (key == "forms" && (value.is_array() || value.is_string())) {
					if (auto list = detail::ReadStringList(value, a_source, where, a_diags)) {
						for (const auto& s : *list) {
							if (auto f = FormRef::Parse(s)) {
								c.forms.push_back(std::move(*f));
							} else {
								a_diags.Error(a_source, where + ": invalid form reference \"" + s + "\" (kept verbatim, ignored)");
								c.unparsedForms.push_back(s);
							}
						}
					}
				} else {
					if (key == "label" || key == "ignored" || key == "categories" || key == "forms") {
						a_diags.Error(a_source, where + ": unexpected type " + detail::TypeName(value) + " (kept verbatim, ignored)");
					} else {
						a_diags.Warn(a_source, where + ": unknown field (preserved)");
					}
					c.extra[key] = value;
				}
			}
			return c;
		}

		ojson ContainerToJson(const ContainerBinding& a_c)
		{
			ojson out = ojson::object();
			out["ref"] = a_c.ref.ToString();
			if (!a_c.label.empty()) {
				out["label"] = a_c.label;
			}
			if (!a_c.categories.empty()) {
				out["categories"] = a_c.categories;
			}
			if (!a_c.forms.empty() || !a_c.unparsedForms.empty()) {
				ojson forms = ojson::array();
				for (const auto& f : a_c.forms) {
					forms.push_back(f.ToString());
				}
				for (const auto& s : a_c.unparsedForms) {
					forms.push_back(s);
				}
				out["forms"] = std::move(forms);
			}
			if (a_c.ignored) {
				out["ignored"] = true;
			}
			AppendExtra(out, a_c.extra);
			return out;
		}

		std::optional<FormRef> ReadFormRefField(const json& a_value, const std::string& a_source, const std::string& a_where, Diagnostics& a_diags)
		{
			if (!a_value.is_string()) {
				a_diags.Error(a_source, a_where + ": expected a form reference string (kept verbatim)");
				return std::nullopt;
			}
			auto ref = FormRef::Parse(a_value.get<std::string>());
			if (!ref) {
				a_diags.Error(a_source, a_where + ": invalid form reference \"" + a_value.get<std::string>() + "\" (kept verbatim)");
			}
			return ref;
		}
	}

	KeepSelector KeepSelector::Parse(std::string_view a_text)
	{
		KeepSelector s;
		if (IEquals(a_text, "equipped")) {
			s.kind = Kind::Equipped;
		} else if (IEquals(a_text, "favorited")) {
			s.kind = Kind::Favorited;
		} else if (IEquals(a_text, "quest")) {
			s.kind = Kind::Quest;
		} else if (a_text.find('|') != std::string_view::npos) {
			if (auto ref = FormRef::Parse(a_text)) {
				s.kind = Kind::Form;
				s.form = std::move(*ref);
			}
		} else if (IsValidCategoryPattern(a_text)) {
			s.kind = Kind::Category;
			s.pattern = std::string(a_text);
		}
		return s;
	}

	const ContainerBinding* Layout::FindContainer(const FormRef& a_ref) const
	{
		const auto it = std::ranges::find_if(containers, [&](const ContainerBinding& c) { return c.ref == a_ref; });
		return it == containers.end() ? nullptr : &*it;
	}

	ContainerBinding* Layout::FindContainer(const FormRef& a_ref)
	{
		const auto it = std::ranges::find_if(containers, [&](const ContainerBinding& c) { return c.ref == a_ref; });
		return it == containers.end() ? nullptr : &*it;
	}

	// --- Keep config -------------------------------------------------------------------------------

	std::optional<KeepConfig> ParseKeepConfig(const json& a_value, std::string_view a_source, Diagnostics& a_diags)
	{
		const std::string source(a_source);
		if (!a_value.is_object()) {
			a_diags.Error(source, "keep: expected an object, got " + detail::TypeName(a_value));
			return std::nullopt;
		}
		KeepConfig keep;
		bool       sawQuotas = false;
		bool       sawRules = false;
		for (const auto& [key, value] : a_value.items()) {
			const auto where = "keep." + key;
			if (key == "inherit") {
				if (value.is_boolean()) {
					keep.inherit = value.get<bool>();
				} else {
					a_diags.Error(source, where + ": expected true or false (kept verbatim; inherit defaults to true)");
					keep.extra[key] = value;
				}
			} else if (key == "always" || key == "never") {
				auto list = detail::ReadStringList(value, source, where, a_diags);
				if (!list) {
					keep.extra[key] = value;
					continue;
				}
				ValidateKeepList(*list, source, where, a_diags);
				(key == "always" ? keep.always : keep.never) = std::move(*list);
			} else if (key == "quotas" || key == "rules") {
				(key == "rules" ? sawRules : sawQuotas) = true;
				if (!value.is_array()) {
					a_diags.Error(source, where + ": expected an array (kept verbatim)");
					keep.extra[key] = value;
					continue;
				}
				for (std::size_t i = 0; i < value.size(); ++i) {
					if (auto rule = ParseQuotaRule(value[i], source, where + "[" + std::to_string(i) + "]", a_diags)) {
						keep.quotas.push_back(std::move(*rule));
					} else {
						keep.unparsedQuotas.push_back(value[i]);
					}
				}
			} else {
				a_diags.Warn(source, where + ": unknown field (preserved)");
				keep.extra[key] = value;
			}
		}
		if (sawQuotas && sawRules) {
			a_diags.Warn(source, "keep: both \"quotas\" and \"rules\" given; they are merged and saved as \"quotas\"");
		}
		keep.quotasKeyIsRules = sawRules && !sawQuotas;
		return keep;
	}

	ojson KeepConfigToJson(const KeepConfig& a_keep)
	{
		ojson out = ojson::object();
		if (a_keep.inherit) {
			out["inherit"] = *a_keep.inherit;
		}
		if (!a_keep.always.empty()) {
			out["always"] = a_keep.always;
		}
		if (!a_keep.never.empty()) {
			out["never"] = a_keep.never;
		}
		if (!a_keep.quotas.empty() || !a_keep.unparsedQuotas.empty()) {
			ojson rules = ojson::array();
			for (const auto& r : a_keep.quotas) {
				rules.push_back(QuotaRuleToJson(r));
			}
			for (const auto& r : a_keep.unparsedQuotas) {
				rules.push_back(ToOrdered(r));
			}
			out[a_keep.quotasKeyIsRules ? "rules" : "quotas"] = std::move(rules);
		}
		AppendExtra(out, a_keep.extra);
		return out;
	}

	// --- Layout ------------------------------------------------------------------------------------

	std::optional<Layout> ParseLayout(const json& a_doc, std::string_view a_source, Diagnostics& a_diags)
	{
		const std::string source(a_source);
		if (!a_doc.is_object()) {
			a_diags.Error(source, "expected a JSON object at the top level");
			return std::nullopt;
		}
		if (const auto it = a_doc.find("$schema"); it == a_doc.end()) {
			a_diags.Warn(source, "missing \"$schema\"; assuming \"dovastash-layout-1\"");
		} else if (!it->is_string() || it->get<std::string>() != kLayoutSchema) {
			a_diags.Error(source, "unsupported \"$schema\" (expected \"dovastash-layout-1\")");
			return std::nullopt;
		}

		Layout layout;
		for (const auto& [key, value] : a_doc.items()) {
			if (key == "$schema") {
				continue;
			}
			if (key == "name" || key == "author" || key == "version" || key == "basedOn") {
				if (!value.is_string()) {
					a_diags.Error(source, key + ": expected a string (kept verbatim)");
					layout.extra[key] = value;
					continue;
				}
				auto s = value.get<std::string>();
				if (key == "name") {
					layout.name = std::move(s);
				} else if (key == "author") {
					layout.author = std::move(s);
				} else if (key == "version") {
					if (!s.empty() && !SemVer::Parse(s)) {
						a_diags.Warn(source, "version \"" + s + "\" is not a semantic version; it sorts below every valid version");
					}
					layout.version = std::move(s);
				} else {
					layout.basedOn = std::move(s);
				}
			} else if (key == "priority") {
				if (auto p = ReadInt32(value)) {
					layout.priority = *p;
				} else {
					a_diags.Error(source, "priority: expected an integer (kept verbatim)");
					layout.extra[key] = value;
				}
			} else if (key == "requires") {
				if (auto list = detail::ReadStringList(value, source, key, a_diags)) {
					layout.requiredPlugins = std::move(*list);
				} else {
					layout.extra[key] = value;
				}
			} else if (key == "cell" || key == "master") {
				if (auto ref = ReadFormRefField(value, source, key, a_diags)) {
					(key == "cell" ? layout.cell : layout.master) = std::move(*ref);
				} else {
					layout.extra[key] = value;
				}
			} else if (key == "containers") {
				if (!value.is_array()) {
					// Unusable document: saving would replace the player's hand-edited data.
					a_diags.Error(source, "containers: expected an array, got " + detail::TypeName(value) + "; the file is left untouched");
					return std::nullopt;
				}
				for (std::size_t i = 0; i < value.size(); ++i) {
					auto c = ParseContainer(value[i], source, "containers[" + std::to_string(i) + "]", a_diags);
					if (!c) {
						layout.unparsedContainers.push_back(value[i]);
						continue;
					}
					if (layout.FindContainer(c->ref)) {
						a_diags.Warn(source, "containers[" + std::to_string(i) + "]: duplicate ref " + c->ref.ToString() + "; the first entry is used");
					}
					layout.containers.push_back(std::move(*c));
				}
			} else if (key == "keep") {
				if (auto keep = ParseKeepConfig(value, source, a_diags)) {
					layout.keep = std::move(*keep);
				} else {
					layout.extra[key] = value;
				}
			} else if (key == "learned") {
				if (!value.is_object()) {
					a_diags.Error(source, "learned: expected an object, got " + detail::TypeName(value) + "; the file is left untouched");
					return std::nullopt;
				}
				for (const auto& [formText, containerValue] : value.items()) {
					auto form = FormRef::Parse(formText);
					auto target = containerValue.is_string() ? FormRef::Parse(containerValue.get<std::string>()) : std::nullopt;
					if (!form || !target) {
						a_diags.Error(source, "learned[\"" + formText + "\"]: invalid form reference (kept verbatim)");
						layout.unparsedLearned[formText] = containerValue;
						continue;
					}
					if (!layout.learned.emplace(std::move(*form), std::move(*target)).second) {
						a_diags.Warn(source, "learned[\"" + formText + "\"]: duplicate entry (same form spelled differently); the first is used");
					}
				}
			} else {
				a_diags.Warn(source, "unknown field \"" + key + "\" (preserved)");
				layout.extra[key] = value;
			}
		}

		if (!layout.cell && !layout.extra.contains("cell")) {
			a_diags.Error(source, "missing \"cell\"");
		}
		if (layout.master) {
			if (const auto* c = layout.FindContainer(*layout.master); c && c->ignored) {
				a_diags.Warn(source, "the master container is also listed as ignored");
			}
		}
		return layout;
	}

	std::optional<Layout> ParseLayoutText(std::string_view a_text, std::string_view a_source, Diagnostics& a_diags)
	{
		auto doc = detail::ParseJson(a_text, a_source, a_diags);
		if (!doc) {
			return std::nullopt;
		}
		return ParseLayout(*doc, a_source, a_diags);
	}

	ojson LayoutToJson(const Layout& a_layout)
	{
		ojson out = ojson::object();
		out["$schema"] = kLayoutSchema;
		out["name"] = a_layout.name;
		out["author"] = a_layout.author;
		out["version"] = a_layout.version;
		out["priority"] = a_layout.priority;
		if (!a_layout.requiredPlugins.empty()) {
			out["requires"] = a_layout.requiredPlugins;
		}
		if (a_layout.cell) {
			out["cell"] = a_layout.cell->ToString();
		}
		if (a_layout.master) {
			out["master"] = a_layout.master->ToString();
		}
		ojson containers = ojson::array();
		for (const auto& c : a_layout.containers) {
			containers.push_back(ContainerToJson(c));
		}
		for (const auto& c : a_layout.unparsedContainers) {
			containers.push_back(ToOrdered(c));
		}
		out["containers"] = std::move(containers);
		if (a_layout.keep) {
			out["keep"] = KeepConfigToJson(*a_layout.keep);
		}
		if (!a_layout.learned.empty() || !a_layout.unparsedLearned.empty()) {
			ojson learned = ojson::object();
			for (const auto& [form, container] : a_layout.learned) {
				learned[form.ToString()] = container.ToString();
			}
			AppendExtra(learned, a_layout.unparsedLearned);
			out["learned"] = std::move(learned);
		}
		if (a_layout.basedOn) {
			out["basedOn"] = *a_layout.basedOn;
		}
		AppendExtra(out, a_layout.extra);
		return out;
	}

	std::string SerializeLayout(const Layout& a_layout)
	{
		return detail::Dump(LayoutToJson(a_layout));
	}

	std::optional<Layout> LoadLayoutFile(const std::filesystem::path& a_path, std::string_view a_source, Diagnostics& a_diags)
	{
		const auto text = detail::ReadTextFile(a_path);
		if (!text) {
			a_diags.Error(std::string(a_source), "cannot read file");
			return std::nullopt;
		}
		return ParseLayoutText(*text, a_source, a_diags);
	}

	bool SaveLayoutFile(const std::filesystem::path& a_path, const Layout& a_layout)
	{
		return detail::WriteTextFileAtomic(a_path, SerializeLayout(a_layout));
	}

	std::string LayoutFileName(const FormRef& a_cell)
	{
		// "Skyrim.esm|0x000165A7" -> "Skyrim.esm_000165A7.json"
		auto text = a_cell.ToString();
		const auto bar = text.find("|0x");
		return text.substr(0, bar) + "_" + text.substr(bar + 3) + ".json";
	}
}
