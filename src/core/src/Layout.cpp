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
			if (v < INT32_MIN || v > INT32_MAX) {
			return std::nullopt;
			}
			return static_cast<std::int32_t>(v);
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
				} else if (key == "note" && value.is_string()) {
					c.note = value.get<std::string>();
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
					if (key == "label" || key == "note" || key == "ignored" || key == "categories" || key == "forms") {
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
			if (!a_c.note.empty()) {
				out["note"] = a_c.note;
			}
			AppendExtra(out, a_c.extra);
			return out;
		}

		std::optional<DisplayBinding> ParseDisplay(const json& a_value, const std::string& a_source, const std::string& a_where, Diagnostics& a_diags)
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

			DisplayBinding d;
			d.ref = std::move(*ref);
			for (const auto& [key, value] : a_value.items()) {
				const auto where = a_where + "." + key;
				if (key == "ref") {
					continue;
				}
				if (key == "kind" && value.is_string() && ParseDisplayKind(value.get<std::string>())) {
					d.kind = ParseDisplayKind(value.get<std::string>());
				} else if (key == "label" && value.is_string()) {
					d.label = value.get<std::string>();
				} else if (key == "note" && value.is_string()) {
					d.note = value.get<std::string>();
				} else if (key == "categories" && (value.is_array() || value.is_string())) {
					if (auto list = detail::ReadStringList(value, a_source, where, a_diags)) {
						d.categories = std::move(*list);
						for (const auto& p : d.categories) {
							if (!IsValidCategoryPattern(p)) {
								a_diags.Warn(a_source, where + ": invalid category pattern \"" + p + "\" (kept, never matches)");
							}
						}
					}
				} else if (key == "ignored" && value.is_boolean()) {
					d.ignored = value.get<bool>();
				} else if (key == "items" && (value.is_array() || value.is_string())) {
					if (auto list = detail::ReadStringList(value, a_source, where, a_diags)) {
						for (const auto& s : *list) {
							if (auto f = FormRef::Parse(s)) {
								d.items.push_back(std::move(*f));
							} else {
								a_diags.Error(a_source, where + ": invalid form reference \"" + s + "\" (kept verbatim, ignored)");
								d.unparsedItems.push_back(s);
							}
						}
					}
				} else if (key == "only" && value.is_boolean()) {
					d.only = value.get<bool>();
				} else if (key == "set" && value.is_string() && FormRef::Parse(value.get<std::string>())) {
					d.set = FormRef::Parse(value.get<std::string>());
				} else {
					if (key == "kind") {
						a_diags.Error(a_source, where + ": expected \"rack\", \"mannequin\", \"item\" or \"case\" (kept verbatim, ignored)");
					} else if (key == "set") {
						a_diags.Error(a_source, where + ": expected a form reference to a piece of the set (kept verbatim, ignored)");
					} else if (key == "label" || key == "note" || key == "ignored" || key == "items" || key == "categories" || key == "only") {
						a_diags.Error(a_source, where + ": unexpected type " + detail::TypeName(value) + " (kept verbatim, ignored)");
					} else {
						a_diags.Warn(a_source, where + ": unknown field (preserved)");
					}
					d.extra[key] = value;
				}
			}
			return d;
		}

		ojson DisplayToJson(const DisplayBinding& a_d)
		{
			ojson out = ojson::object();
			out["ref"] = a_d.ref.ToString();
			if (a_d.kind) {
				out["kind"] = DisplayKindName(*a_d.kind);
			}
			if (!a_d.label.empty()) {
				out["label"] = a_d.label;
			}
			if (!a_d.categories.empty()) {
				out["categories"] = a_d.categories;
			}
			if (!a_d.items.empty() || !a_d.unparsedItems.empty()) {
				ojson items = ojson::array();
				for (const auto& f : a_d.items) {
					items.push_back(f.ToString());
				}
				for (const auto& s : a_d.unparsedItems) {
					items.push_back(s);
				}
				out["items"] = std::move(items);
			}
			if (a_d.only) {
				out["only"] = true;
			}
			if (a_d.set) {
				out["set"] = a_d.set->ToString();
			}
			if (a_d.ignored) {
				out["ignored"] = true;
			}
			if (!a_d.note.empty()) {
				out["note"] = a_d.note;
			}
			AppendExtra(out, a_d.extra);
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

	const DisplayBinding* Layout::FindDisplay(const FormRef& a_ref) const
	{
		const auto it = std::ranges::find_if(displays, [&](const DisplayBinding& d) { return d.ref == a_ref; });
		return it == displays.end() ? nullptr : &*it;
	}

	DisplayBinding* Layout::FindDisplay(const FormRef& a_ref)
	{
		const auto it = std::ranges::find_if(displays, [&](const DisplayBinding& d) { return d.ref == a_ref; });
		return it == displays.end() ? nullptr : &*it;
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
			if (key == "name" || key == "note" || key == "author" || key == "version" || key == "basedOn") {
				if (!value.is_string()) {
					a_diags.Error(source, key + ": expected a string (kept verbatim)");
					layout.extra[key] = value;
					continue;
				}
				auto s = value.get<std::string>();
				if (key == "name") {
					layout.name = std::move(s);
				} else if (key == "note") {
					layout.note = std::move(s);
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
			} else if (key == "autoDraft") {
				if (value.is_boolean()) {
					layout.autoDraft = value.get<bool>();
				} else {
					a_diags.Error(source, "autoDraft: expected true or false (kept verbatim)");
					layout.extra[key] = value;
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
			} else if (key == "cells") {
				// More cells of a multi-cell home (HomeCells.h). A value of the wrong type is kept verbatim.
				if (!ParseHomeCells(value, source, layout.cells, layout.unparsedCells, a_diags)) {
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
						// The first entry is the binding (FindContainer, the planner's availability check); a second one
						// is kept verbatim and never used, so the two cannot disagree on ignored / categories.
						a_diags.Warn(source, "containers[" + std::to_string(i) + "]: duplicate ref " + c->ref.ToString() + "; the first entry is used (this one is kept verbatim, ignored)");
						layout.unparsedContainers.push_back(value[i]);
						continue;
					}
					layout.containers.push_back(std::move(*c));
				}
			} else if (key == "displays") {
				if (!value.is_array()) {
					a_diags.Error(source, "displays: expected an array, got " + detail::TypeName(value) + "; the file is left untouched");
					return std::nullopt;
				}
				for (std::size_t i = 0; i < value.size(); ++i) {
					auto d = ParseDisplay(value[i], source, "displays[" + std::to_string(i) + "]", a_diags);
					if (!d) {
						layout.unparsedDisplays.push_back(value[i]);
						continue;
					}
					if (layout.FindDisplay(d->ref)) {
						a_diags.Warn(source, "displays[" + std::to_string(i) + "]: duplicate ref " + d->ref.ToString() + "; the first entry is used (this one is kept verbatim, ignored)");
						layout.unparsedDisplays.push_back(value[i]);
						continue;
					}
					layout.displays.push_back(std::move(*d));
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
			} else if (key == "declined") {
				// Turned-down category suggestions (Learning.h): container ref -> category ids. A value of the
				// wrong shape is kept verbatim, never dropped.
				if (!value.is_object()) {
					a_diags.Error(source, "declined: expected an object, got " + detail::TypeName(value) + " (kept verbatim)");
					layout.extra[key] = value;
					continue;
				}
				for (const auto& [refText, idsValue] : value.items()) {
					auto ref = FormRef::Parse(refText);
					bool ok = ref.has_value() && idsValue.is_array();
					std::vector<std::string> ids;
					if (ok) {
						for (const auto& id : idsValue) {
							if (!id.is_string() || id.get<std::string>().empty()) {
								ok = false;
								break;
							}
							const auto text = id.get<std::string>();
							if (std::ranges::none_of(ids, [&](const std::string& e) { return IEquals(e, text); })) {
								ids.push_back(text);
							}
						}
					}
					if (!ok) {
						a_diags.Error(source, "declined[\"" + refText + "\"]: expected a container reference and a list of category ids (kept verbatim)");
						layout.unparsedDeclined[refText] = idsValue;
						continue;
					}
					auto& list = layout.declined[*ref];
					for (auto& id : ids) {
						if (std::ranges::none_of(list, [&](const std::string& e) { return IEquals(e, id); })) {
							list.push_back(std::move(id));
						}
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
		ValidateHomeCells(layout.cell, layout.cells, source, a_diags);
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
		// A scalar of the wrong type was kept verbatim in `extra` (ParseLayout): while the field is still at its
		// default it is written back as it was (AppendExtra, at the end), not replaced by the empty default.
		const auto verbatim = [&](const char* a_key) { return a_layout.extra.is_object() && a_layout.extra.contains(a_key); };
		if (a_layout.autoDraft) {
			out["autoDraft"] = true;  // first, so a person opening the file sees it (DESIGN §6.8)
		}
		if (!a_layout.name.empty() || !verbatim("name")) {
			out["name"] = a_layout.name;
		}
		if (!a_layout.note.empty()) {
			out["note"] = a_layout.note;
		}
		if (!a_layout.author.empty() || !verbatim("author")) {
			out["author"] = a_layout.author;
		}
		if (!a_layout.version.empty() || !verbatim("version")) {
			out["version"] = a_layout.version;
		}
		if (a_layout.priority != 0 || !verbatim("priority")) {
			out["priority"] = a_layout.priority;
		}
		if (!a_layout.requiredPlugins.empty()) {
			out["requires"] = a_layout.requiredPlugins;
		}
		if (a_layout.cell) {
			out["cell"] = a_layout.cell->ToString();
		}
		if (!a_layout.cells.empty() || (a_layout.unparsedCells.is_array() && !a_layout.unparsedCells.empty())) {
			out["cells"] = HomeCellsToJson(a_layout.cells, a_layout.unparsedCells);
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
		if (!a_layout.displays.empty() || !a_layout.unparsedDisplays.empty()) {
			ojson displays = ojson::array();
			for (const auto& d : a_layout.displays) {
				displays.push_back(DisplayToJson(d));
			}
			for (const auto& d : a_layout.unparsedDisplays) {
				displays.push_back(ToOrdered(d));
			}
			out["displays"] = std::move(displays);
		}
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
		if (!a_layout.declined.empty() || !a_layout.unparsedDeclined.empty()) {
			ojson declined = ojson::object();
			for (const auto& [container, ids] : a_layout.declined) {
				if (!ids.empty()) {
					declined[container.ToString()] = ids;
				}
			}
			AppendExtra(declined, a_layout.unparsedDeclined);
			if (!declined.empty()) {
				out["declined"] = std::move(declined);
			}
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

	bool SameSetup(const Layout& a_lhs, const Layout& a_rhs)
	{
		auto lhs = LayoutToJson(a_lhs);
		auto rhs = LayoutToJson(a_rhs);
		lhs.erase("cell");
		rhs.erase("cell");
		return lhs == rhs;
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
