#include "dova/core/Template.h"

#include <algorithm>
#include <set>
#include <system_error>
#include <utility>

#include "JsonUtil.h"
#include "dova/core/TemplateDisplays.h"
#include "dova/core/Version.h"

namespace dova::core
{
	namespace
	{
		bool PluginLoaded(std::string_view a_plugin, std::span<const std::string> a_loaded)
		{
			return std::ranges::any_of(a_loaded, [&](const std::string& p) { return IEquals(p, a_plugin); });
		}

		// "Name.esp" / ".esm" / ".esl": a file name, no path or form id.
		bool IsPluginFileName(std::string_view a_name)
		{
			if (a_name.size() <= 4 || a_name.find_first_of("|/\\") != std::string_view::npos) {
				return false;
			}
			const auto ext = a_name.substr(a_name.size() - 4);
			return IEquals(ext, ".esp") || IEquals(ext, ".esm") || IEquals(ext, ".esl");
		}

		// True if a ranks before b.
		bool RanksBefore(const TemplateCandidate& a_lhs, const TemplateCandidate& a_rhs)
		{
			if (const auto c = CompareTemplateRank(a_lhs.layout, IsUserTemplate(a_lhs.source), a_rhs.layout, IsUserTemplate(a_rhs.source)); c != 0) {
				return c < 0;
			}
			return ICompare(a_lhs.source, a_rhs.source) < 0;
		}
	}

	int CompareTemplateRank(const Layout& a_lhs, bool a_lhsUser, const Layout& a_rhs, bool a_rhsUser)
	{
		if (a_lhs.autoDraft != a_rhs.autoDraft) {
			return a_lhs.autoDraft ? 1 : -1;  // a template someone made before an auto-draft, whatever its priority (DESIGN §6.8)
		}
		if (a_lhs.priority != a_rhs.priority) {
			return a_lhs.priority > a_rhs.priority ? -1 : 1;
		}
		if (a_lhsUser != a_rhsUser) {
			return a_lhsUser ? -1 : 1;
		}
		if (const auto c = CompareVersionStrings(a_lhs.version, a_rhs.version); c != 0) {
			return c > 0 ? -1 : 1;
		}
		return 0;
	}

	bool IsUserTemplate(std::string_view a_source)
	{
		if (a_source.size() <= kUserTemplatePrefix.size()) {
			return false;
		}
		for (std::size_t i = 0; i < kUserTemplatePrefix.size(); ++i) {
			const char c = a_source[i] == '\\' ? '/' : a_source[i];
			const auto lower = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
			const char p = kUserTemplatePrefix[i];
			const auto pLower = static_cast<char>(p >= 'A' && p <= 'Z' ? p - 'A' + 'a' : p);
			if (lower != pLower) {
				return false;
			}
		}
		return true;
	}

	std::vector<TemplateCandidate> LoadTemplateDirectory(const std::filesystem::path& a_dir, Diagnostics& a_diags, std::string_view a_sourcePrefix)
	{
		std::vector<TemplateCandidate> out;
		std::error_code                ec;
		if (!std::filesystem::is_directory(a_dir, ec)) {
			return out;
		}
		std::vector<std::filesystem::path> files;
		for (auto it = std::filesystem::recursive_directory_iterator(a_dir, std::filesystem::directory_options::skip_permission_denied, ec);
			 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
			if (it->is_regular_file(ec) && IEquals(detail::PathToUtf8(it->path().extension()), ".json")) {
				files.push_back(it->path());
			}
		}
		for (const auto& file : files) {
			const auto source = std::string(a_sourcePrefix) + detail::PathToUtf8(file.lexically_relative(a_dir));
			const auto text = detail::ReadTextFile(file);
			if (!text) {
				a_diags.Error(source, "cannot read file");
				continue;
			}
			auto layout = ParseLayoutText(*text, source, a_diags);
			if (!layout) {
				continue;
			}
			ValidateTemplate(*layout, source, a_diags);
			out.push_back(TemplateCandidate{ source, std::move(*layout) });
		}
		std::ranges::sort(out, [](const auto& a_lhs, const auto& a_rhs) { return ICompare(a_lhs.source, a_rhs.source) < 0; });
		return out;
	}

	void ValidateTemplate(const Layout& a_template, std::string_view a_source, Diagnostics& a_diags)
	{
		const std::string source(a_source);
		if (!a_template.learned.empty() || !a_template.unparsedLearned.empty()) {
			a_diags.Warn(source, "templates should not contain \"learned\" (it is dropped when the template is applied)");
		}
		if (!a_template.declined.empty() || !a_template.unparsedDeclined.empty()) {
			a_diags.Warn(source, "templates should not contain \"declined\" (the player's own answers; dropped when the template is applied)");
		}
		if (a_template.basedOn) {
			a_diags.Warn(source, "templates should not contain \"basedOn\"");
		}
		if (!a_template.hiddenTemplates.empty()) {
			a_diags.Warn(source, "templates should not contain \"hiddenTemplates\" (the player's own choice; dropped when the template is applied)");
		}
		if (!a_template.master) {
			a_diags.Warn(source, "no \"master\" container: items without a destination will stay in the player inventory");
		}
		std::set<std::string> reported;
		auto check = [&](const FormRef& a_ref, std::string_view a_what) {
			if (PluginLoaded(a_ref.plugin, a_template.requiredPlugins) || !reported.insert(ToLower(a_ref.plugin)).second) {
				return;
			}
			a_diags.Warn(source, std::string(a_what) + " references " + a_ref.plugin + ", which is not listed in \"requires\"");
		};
		if (a_template.cell) {
			check(*a_template.cell, "cell");
		}
		for (const auto& c : a_template.cells) {
			check(c.cell, "cells");
			if (c.master) {
				check(*c.master, "cells master");
			}
		}
		if (a_template.master) {
			check(*a_template.master, "master");
		}
		for (const auto& c : a_template.containers) {
			check(c.ref, "container");
			for (const auto& f : c.forms) {
				check(f, "container forms");  // hard references, as ReferencedPlugins counts them (DESIGN §6.5)
			}
		}
		for (const auto& d : a_template.displays) {
			check(d.ref, "display");  // its items and set are soft references: never needed in requires (TemplateDisplays.h)
		}
		if (a_template.keep) {
			for (const auto* list : { &a_template.keep->always, &a_template.keep->never }) {
				for (const auto& entry : *list) {
					if (const auto sel = KeepSelector::Parse(entry); sel.kind == KeepSelector::Kind::Form) {
						check(sel.form, "keep");
					}
				}
			}
			for (const auto& q : a_template.keep->quotas) {
				if (q.forms) {
					for (const auto& f : *q.forms) {
						check(f, "keep quota forms");
					}
				}
			}
		}
		for (const auto& c : a_template.conflictPlugins) {
			if (!IsPluginFileName(c)) {
				a_diags.Warn(source, "conflicts: \"" + c + "\" is not a plugin file name (.esp, .esm or .esl)");
			} else if (PluginLoaded(c, a_template.requiredPlugins)) {
				a_diags.Warn(source, "conflicts: " + c + " is also in \"requires\"; the template is never offered");
			} else if (a_template.cell && IEquals(a_template.cell->plugin, c)) {
				a_diags.Warn(source, "conflicts: " + c + " is the plugin of the template's own cell; the template is never offered");
			}
		}
		ValidateDisplays(a_template, a_source, a_diags);
	}

	void ValidateLayoutCategories(const Layout& a_layout, const CategorySet& a_categories, std::string_view a_source, Diagnostics& a_diags)
	{
		const std::string source(a_source);
		// Near-miss candidates: every id, and "<group>.*" for each id other ids sit under.
		std::vector<std::string>      groupPatterns;
		std::vector<std::string_view> known = a_categories.Ids();
		for (const auto id : a_categories.Ids()) {
			const std::string prefix = std::string(id) + ".";
			if (std::ranges::any_of(known, [&](std::string_view a_other) { return a_other.starts_with(prefix); })) {
				groupPatterns.push_back(prefix + "*");
			}
		}
		known.insert(known.end(), groupPatterns.begin(), groupPatterns.end());
		const auto unknown = [&](std::string_view a_pattern) {
			return "category pattern \"" + std::string(a_pattern) + "\" matches no known category" + detail::DidYouMean(a_pattern, known);
		};
		for (const auto& c : a_layout.containers) {
			for (const auto& p : c.categories) {
				if (!a_categories.AnyMatches(p)) {
					a_diags.Warn(source, "container " + c.ref.ToString() + ": " + unknown(p));
				}
			}
		}
		for (const auto& d : a_layout.displays) {
			if (d.ignored) {
				continue;
			}
			for (const auto& p : d.categories) {
				if (!a_categories.AnyMatches(p)) {
					a_diags.Warn(source, "display " + d.ref.ToString() + ": " + unknown(p));
				}
			}
		}
		if (a_layout.keep) {
			// A misspelt token ("equiped") parses as a category pattern: point to the tokens too.
			auto keepKnown = known;
			keepKnown.insert(keepKnown.end(), { "equipped", "favorited", "quest" });
			auto checkList = [&](const std::vector<std::string>& a_list, std::string_view a_name) {
				for (const auto& e : a_list) {
					const auto sel = KeepSelector::Parse(e);
					if (sel.kind == KeepSelector::Kind::Category && !a_categories.AnyMatches(sel.pattern)) {
						a_diags.Warn(source, "keep." + std::string(a_name) + ": category pattern \"" + e + "\" matches no known category" +
							detail::DidYouMean(e, keepKnown));
					}
				}
			};
			checkList(a_layout.keep->always, "always");
			checkList(a_layout.keep->never, "never");
			for (const auto& q : a_layout.keep->quotas) {
				if (q.category && !a_categories.AnyMatches(*q.category)) {
					a_diags.Warn(source, "keep quota: " + unknown(*q.category));
				}
			}
		}
	}

	std::vector<std::string> MissingRequirements(const Layout& a_layout, std::span<const std::string> a_loadedPlugins)
	{
		std::vector<std::string> missing;
		for (const auto& r : a_layout.requiredPlugins) {
			if (!PluginLoaded(r, a_loadedPlugins)) {
				missing.push_back(r);
			}
		}
		return missing;
	}

	std::vector<std::string> LoadedConflicts(const Layout& a_layout, std::span<const std::string> a_loadedPlugins)
	{
		std::vector<std::string> loaded;
		for (const auto& c : a_layout.conflictPlugins) {
			if (PluginLoaded(c, a_loadedPlugins) && !PluginLoaded(c, loaded)) {
				loaded.push_back(c);
			}
		}
		return loaded;
	}

	LayoutResolution ResolveLayout(const Layout* a_playerLayout, std::span<const TemplateCandidate> a_templates, const FormRef& a_cell,
		std::span<const std::string> a_loadedPlugins)
	{
		LayoutResolution res;
		std::vector<std::size_t> eligible;
		for (std::size_t i = 0; i < a_templates.size(); ++i) {
			const auto& t = a_templates[i];
			if (!t.layout.cell || *t.layout.cell != a_cell) {
				continue;
			}
			if (a_playerLayout && IsTemplateHidden(*a_playerLayout, t.source)) {
				res.hidden.push_back(i);
				continue;
			}
			auto missing = MissingRequirements(t.layout, a_loadedPlugins);
			auto conflicting = LoadedConflicts(t.layout, a_loadedPlugins);
			if (!missing.empty() || !conflicting.empty()) {
				res.rejected.push_back({ i, std::move(missing), std::move(conflicting) });
				continue;
			}
			eligible.push_back(i);
		}
		std::ranges::stable_sort(eligible, [&](std::size_t a_lhs, std::size_t a_rhs) { return RanksBefore(a_templates[a_lhs], a_templates[a_rhs]); });
		std::ranges::stable_sort(res.hidden, [&](std::size_t a_lhs, std::size_t a_rhs) { return RanksBefore(a_templates[a_lhs], a_templates[a_rhs]); });

		if (!eligible.empty()) {
			res.chosen = eligible.front();
			res.alternatives.assign(eligible.begin() + 1, eligible.end());
		}
		if (!a_playerLayout && !res.chosen) {
			// No template of its own: the best eligible template that lists the cell as one of its home's areas.
			for (std::size_t i = 0; i < a_templates.size(); ++i) {
				const auto& t = a_templates[i];
				if (!t.layout.cell || *t.layout.cell == a_cell || std::ranges::none_of(t.layout.cells, [&](const HomeCell& c) { return c.cell == a_cell; }) ||
					!MissingRequirements(t.layout, a_loadedPlugins).empty() || !LoadedConflicts(t.layout, a_loadedPlugins).empty()) {
					continue;
				}
				if (!res.partOf || RanksBefore(t, a_templates[*res.partOf])) {
					res.partOf = i;
				}
			}
		}
		if (a_playerLayout) {
			res.kind = LayoutResolution::Kind::Player;
		} else if (res.chosen) {
			res.kind = LayoutResolution::Kind::Template;
		}
		return res;
	}

	bool IsTemplateHidden(const Layout& a_layout, std::string_view a_source)
	{
		return std::ranges::any_of(a_layout.hiddenTemplates, [&](const std::string& a_hidden) { return IEquals(a_hidden, a_source); });
	}

	bool HideTemplate(Layout& a_layout, std::string_view a_source)
	{
		if (a_source.empty() || IsTemplateHidden(a_layout, a_source) || a_layout.extra.contains("hiddenTemplates")) {
			return false;
		}
		a_layout.hiddenTemplates.emplace_back(a_source);
		// "<source>@<version>": the setup no longer follows a template that is not offered here.
		if (a_layout.basedOn) {
			const auto at = a_layout.basedOn->rfind('@');
			if (IEquals(std::string_view(*a_layout.basedOn).substr(0, at), a_source)) {
				a_layout.basedOn.reset();
			}
		}
		return true;
	}

	bool UnhideTemplate(Layout& a_layout, std::string_view a_source)
	{
		return std::erase_if(a_layout.hiddenTemplates, [&](const std::string& a_hidden) { return IEquals(a_hidden, a_source); }) > 0;
	}

	std::string MakeBasedOn(std::string_view a_source, std::string_view a_version)
	{
		std::string out(a_source);
		out += '@';
		out += a_version;
		return out;
	}

	const TemplateCandidate* FindBasedOn(const Layout& a_layout, std::span<const TemplateCandidate> a_templates)
	{
		if (!a_layout.basedOn) {
			return nullptr;
		}
		const auto it = std::ranges::find_if(a_templates, [&](const TemplateCandidate& t) { return MakeBasedOn(t.source, t.layout.version) == *a_layout.basedOn; });
		return it != a_templates.end() ? &*it : nullptr;
	}

	namespace
	{
		// "<source>@<version>" split at the last '@' (no '@': all source, empty version).
		std::pair<std::string_view, std::string_view> SplitBasedOn(std::string_view a_basedOn)
		{
			const auto at = a_basedOn.rfind('@');
			if (at == std::string_view::npos) {
				return { a_basedOn, {} };
			}
			return { a_basedOn.substr(0, at), a_basedOn.substr(at + 1) };
		}

		// A setup made from an older version of a template holds nothing of the player's, judged by its structure (the
		// old file is gone): nothing learned or declined, no template removed, keep and master as the template has
		// them, and every container and display one of the template's with its label and ignored/sell flags, no
		// explicit item bindings, nothing unreadable. Categories cannot be told apart from the old version's.
		// `a_fresh`: the current version applied fresh (and the caller's step after applying). `a_areaMaster`: the setup is
		// for one of `a_fresh`'s extra cells (a retired template it replaces), whose area master may be the setup's master.
		bool LooksUntouched(const Layout& a_layout, const Layout& a_fresh, bool a_areaMaster = false)
		{
			const auto& t = a_fresh;
			const auto  area = a_areaMaster && a_layout.cell ? std::ranges::find(t.cells, *a_layout.cell, &HomeCell::cell) : t.cells.end();
			const bool  masterOk = a_layout.master == t.master || (area != t.cells.end() && area->master && a_layout.master == area->master);
			if (!a_layout.learned.empty() || !a_layout.unparsedLearned.empty() || !a_layout.declined.empty() ||
				!a_layout.unparsedDeclined.empty() || !a_layout.hiddenTemplates.empty() || !a_layout.unparsedContainers.empty() ||
				!a_layout.unparsedDisplays.empty() || !a_layout.unparsedCells.empty() || !masterOk ||
				a_layout.keep.has_value() != t.keep.has_value() || (a_layout.keep && KeepConfigToJson(*a_layout.keep) != KeepConfigToJson(*t.keep))) {
				return false;
			}
			for (const auto& c : a_layout.containers) {
				const auto* mine = t.FindContainer(c.ref);
				if (!mine || !c.forms.empty() || !c.unparsedForms.empty() || c.label != mine->label || c.ignored != mine->ignored ||
					c.sell != mine->sell) {
					return false;
				}
			}
			for (const auto& d : a_layout.displays) {
				const auto it = std::ranges::find(t.displays, d.ref, &DisplayBinding::ref);
				if (it == t.displays.end() || !d.unparsedItems.empty() || d.label != it->label || d.ignored != it->ignored) {
					return false;
				}
			}
			return true;
		}
	}

	const TemplateCandidate* FindBasedOnSource(const Layout& a_layout, std::span<const TemplateCandidate> a_templates)
	{
		if (!a_layout.basedOn) {
			return nullptr;
		}
		const auto source = SplitBasedOn(*a_layout.basedOn).first;
		const auto it = std::ranges::find_if(a_templates, [&](const TemplateCandidate& t) { return IEquals(t.source, source); });
		return it != a_templates.end() ? &*it : nullptr;
	}

	bool TemplateUpdated(const Layout& a_layout, std::span<const TemplateCandidate> a_templates)
	{
		const auto* tmpl = FindBasedOnSource(a_layout, a_templates);
		return tmpl && CompareVersionStrings(SplitBasedOn(*a_layout.basedOn).second, tmpl->layout.version) < 0;
	}

	bool HasOwnChanges(const Layout& a_layout, const TemplateCandidate* a_basedOn, const std::function<void(Layout&)>& a_afterApply)
	{
		if (!a_layout.basedOn || !a_basedOn || MakeBasedOn(a_basedOn->source, a_basedOn->layout.version) != *a_layout.basedOn) {
			return true;
		}
		auto again = ApplyTemplate(*a_basedOn, &a_layout);
		again.cell = a_layout.cell;
		again.conflictPlugins = a_layout.conflictPlugins;  // only decides where a template is offered, never a setup
		if (a_afterApply) {
			a_afterApply(again);
		}
		return !SameSetup(again, a_layout);
	}

	const TemplateCandidate* FindReplacement(const Layout& a_layout, std::span<const TemplateCandidate> a_templates)
	{
		if (!a_layout.basedOn || !a_layout.cell || FindBasedOnSource(a_layout, a_templates)) {
			return nullptr;
		}
		const auto source = SplitBasedOn(*a_layout.basedOn).first;
		const TemplateCandidate* best = nullptr;
		for (const auto& t : a_templates) {
			const bool replaces = std::ranges::any_of(t.layout.replaces, [&](const std::string& r) { return IEquals(r, source); });
			const bool lists = std::ranges::any_of(t.layout.cells, [&](const HomeCell& c) { return c.cell == *a_layout.cell; });
			if (replaces && lists && (!best || RanksBefore(t, *best))) {
				best = &t;
			}
		}
		return best;
	}

	bool IsUntouchedTemplateSetup(const Layout& a_layout, std::span<const TemplateCandidate> a_templates, const std::function<void(Layout&)>& a_afterApply)
	{
		const auto* tmpl = FindBasedOn(a_layout, a_templates);
		if (!tmpl) {
			// A retired template that a whole-home template replaces (`replaces`, the old file gone): judged by structure
			// against the template that replaces it, the area's own master standing in for the home's.
			if (const auto* by = FindReplacement(a_layout, a_templates)) {
				auto current = ApplyTemplate(*by);
				if (a_afterApply) {
					a_afterApply(current);
				}
				return LooksUntouched(a_layout, current, true);
			}
			// An older version of a template still there: judged by structure. Custom, gone or newer: false.
			if (!TemplateUpdated(a_layout, a_templates)) {
				return false;
			}
			auto current = ApplyTemplate(*FindBasedOnSource(a_layout, a_templates));
			if (a_afterApply) {
				a_afterApply(current);
			}
			return LooksUntouched(a_layout, current);
		}
		auto fresh = ApplyTemplate(*tmpl);
		fresh.cell = a_layout.cell;
		fresh.conflictPlugins = a_layout.conflictPlugins;  // only decides where a template is offered, never a setup
		if (a_afterApply) {
			a_afterApply(fresh);
		}
		// SameSetup compares everything written (learned, declined, keep, hiddenTemplates, unknown keys...).
		return SameSetup(fresh, a_layout);
	}

	std::vector<FormRef> AbsorbableCells(const Layout& a_home, const std::function<const Layout*(const FormRef&)>& a_cellLayout,
		std::span<const TemplateCandidate> a_templates, const std::function<void(Layout&)>& a_afterApply)
	{
		std::vector<FormRef> out;
		if (!a_cellLayout) {
			return out;
		}
		for (const auto& c : a_home.cells) {
			if ((a_home.cell && c.cell == *a_home.cell) || std::ranges::find(out, c.cell) != out.end()) {
				continue;
			}
			const auto* own = a_cellLayout(c.cell);
			if (own && IsUntouchedTemplateSetup(*own, a_templates, a_afterApply)) {
				out.push_back(c.cell);
			}
		}
		return out;
	}

	Layout ApplyTemplate(const TemplateCandidate& a_template, const Layout* a_existing)
	{
		Layout layout = a_template.layout;
		layout.learned.clear();
		layout.unparsedLearned = nlohmann::json::object();
		layout.declined.clear();
		layout.unparsedDeclined = nlohmann::json::object();
		layout.basedOn = MakeBasedOn(a_template.source, a_template.layout.version);
		layout.autoDraft = false;  // the player's layout now, not a file "Draft all homes" may refresh
		layout.hiddenTemplates.clear();  // the player's, never a template's
		layout.replaces.clear();  // which retired files the template takes over: the template's business

		if (!a_existing) {
			return layout;
		}

		// The templates the player removed from this home stay removed; the one applied now is wanted here again.
		layout.hiddenTemplates = a_existing->hiddenTemplates;
		UnhideTemplate(layout, a_template.source);

		// Adds `a_from`'s keys that `a_to` does not have (both objects; anything else is left alone).
		const auto mergeExtra = [](nlohmann::json& a_to, const nlohmann::json& a_from) {
			if (!a_to.is_object() || !a_from.is_object()) {
				return;
			}
			for (const auto& [key, value] : a_from.items()) {
				if (!a_to.contains(key)) {
					a_to[key] = value;
				}
			}
		};

		// Containers in both: template categories and its sell chest win (the player's "sell" is not kept: the
		// template's categories could otherwise send other items there); the player's ignored flag, explicit forms,
		// label and note (when the template has none) and extra keys are kept.
		for (auto& c : layout.containers) {
			const auto* old = a_existing->FindContainer(c.ref);
			if (!old) {
				continue;
			}
			c.ignored = c.ignored || old->ignored;
			if (c.label.empty()) {
				c.label = old->label;
			}
			if (c.note.empty()) {
				c.note = old->note;
			}
			// A container the template makes the sell chest: the player's own item bindings to it would send those
			// items to be sold, so they are not carried over (never lose an item).
			const bool newSellChest = c.sell && !c.ignored && !layout.IsMaster(c.ref) && !a_existing->IsSellChest(c.ref);
			for (const auto& f : old->forms) {
				if (!newSellChest && std::ranges::find(c.forms, f) == c.forms.end()) {
					c.forms.push_back(f);
				}
			}
			for (const auto& f : old->unparsedForms) {
				if (std::ranges::find(c.unparsedForms, f) == c.unparsedForms.end()) {
					c.unparsedForms.push_back(f);
				}
			}
			mergeExtra(c.extra, old->extra);
		}

		// Containers only the player layout knows (bound by hand or added by Learn): kept without
		// categories, so the template's category bindings decide where categories go, while learned
		// destinations, explicit forms, labels and ignored flags survive.
		for (const auto& old : a_existing->containers) {
			if (layout.FindContainer(old.ref) || layout.IsMaster(old.ref)) {
				continue;
			}
			ContainerBinding c = old;
			c.categories.clear();
			c.sell = false;  // a role, like the master: it comes from the template (DESIGN §8.8)
			layout.containers.push_back(std::move(c));
		}
		// The previous master, if the template uses another one, stays known as a plain container.
		if (a_existing->master && !layout.IsMaster(*a_existing->master) && !layout.FindContainer(*a_existing->master)) {
			ContainerBinding c;
			c.ref = *a_existing->master;
			c.label = "Previous master";
			layout.containers.push_back(std::move(c));
		}

		// Displays: the template's entries win; the player's ignored flag, and kind, label, note, categories,
		// items (with their "only") and set where the template gives none, are kept. Displays only the player layout names are kept as they are.
		for (auto& d : layout.displays) {
			const auto* old = a_existing->FindDisplay(d.ref);
			if (!old) {
				continue;
			}
			d.ignored = d.ignored || old->ignored;
			if (!d.kind) {
				d.kind = old->kind;
			}
			if (d.label.empty()) {
				d.label = old->label;
			}
			if (d.note.empty()) {
				d.note = old->note;
			}
			if (d.categories.empty()) {
				d.categories = old->categories;
			}
			if (d.items.empty() && d.unparsedItems.empty()) {
				d.items = old->items;
				d.unparsedItems = old->unparsedItems;
				d.only = old->only;  // "only" belongs to the items it limits the display to
			}
			if (!d.set) {
				d.set = old->set;
			}
			mergeExtra(d.extra, old->extra);
		}
		for (const auto& old : a_existing->displays) {
			if (!layout.FindDisplay(old.ref)) {
				layout.displays.push_back(old);
			}
		}

		// Learned destinations: every entry whose container is still part of the layout (not the master), except a
		// container the template makes the sell chest (Deposit would sell what the player put there).
		const auto newSellChest = [&](const FormRef& a_ref) { return layout.IsSellChest(a_ref) && !a_existing->IsSellChest(a_ref); };
		for (const auto& [form, container] : a_existing->learned) {
			if (layout.FindContainer(container) && !layout.IsMaster(container) && !newSellChest(container)) {
				layout.learned.emplace(form, container);
			}
		}
		layout.unparsedLearned = a_existing->unparsedLearned;
		// Declined category suggestions (Learning.h): the player's answers for containers still in the layout.
		for (const auto& [container, ids] : a_existing->declined) {
			if (layout.FindContainer(container) && !layout.IsMaster(container) && !newSellChest(container)) {
				layout.declined.emplace(container, ids);
			}
		}
		layout.unparsedDeclined = a_existing->unparsedDeclined;

		// The home's cells (DESIGN §6.7): the template's, plus the ones the player added (never dropped).
		layout.cells = MergeHomeCells(layout.cells, a_existing->cells);
		if (a_existing->unparsedCells.is_array()) {
			if (!layout.unparsedCells.is_array()) {
				layout.unparsedCells = nlohmann::json::array();
			}
			for (const auto& entry : a_existing->unparsedCells) {
				if (std::ranges::find(layout.unparsedCells, entry) == layout.unparsedCells.end()) {
					layout.unparsedCells.push_back(entry);
				}
			}
		}

		// The player's keep block replaces the template's.
		if (a_existing->keep) {
			layout.keep = a_existing->keep;
		}

		// Unknown top-level keys and entries we could not parse are never dropped.
		mergeExtra(layout.extra, a_existing->extra);
		if (a_existing->unparsedDisplays.is_array()) {
			if (!layout.unparsedDisplays.is_array()) {
				layout.unparsedDisplays = nlohmann::json::array();
			}
			for (const auto& entry : a_existing->unparsedDisplays) {
				if (std::ranges::find(layout.unparsedDisplays, entry) == layout.unparsedDisplays.end()) {
					layout.unparsedDisplays.push_back(entry);
				}
			}
		}
		if (a_existing->unparsedContainers.is_array()) {
			if (!layout.unparsedContainers.is_array()) {
				layout.unparsedContainers = nlohmann::json::array();
			}
			for (const auto& entry : a_existing->unparsedContainers) {
				if (std::ranges::find(layout.unparsedContainers, entry) == layout.unparsedContainers.end()) {
					layout.unparsedContainers.push_back(entry);
				}
			}
		}
		return layout;
	}

	std::vector<std::string> ReferencedPlugins(const Layout& a_layout)
	{
		std::vector<std::string> out;
		const auto add = [&](const FormRef& a_ref) {
			if (!a_ref.plugin.empty() && !PluginLoaded(a_ref.plugin, out)) {
				out.push_back(a_ref.plugin);
			}
		};
		if (a_layout.cell) {
			add(*a_layout.cell);
		}
		for (const auto& c : a_layout.cells) {
			add(c.cell);
		}
		if (a_layout.master) {
			add(*a_layout.master);
		}
		for (const auto& c : a_layout.cells) {
			if (c.master) {
				add(*c.master);
			}
		}
		for (const auto& c : a_layout.containers) {
			add(c.ref);
			for (const auto& f : c.forms) {
				add(f);
			}
		}
		for (const auto& d : a_layout.displays) {
			add(d.ref);  // not its items or set: soft references, skipped where they don't exist (TemplateDisplays.h)
		}
		if (a_layout.keep) {
			for (const auto* list : { &a_layout.keep->always, &a_layout.keep->never }) {
				for (const auto& entry : *list) {
					if (const auto sel = KeepSelector::Parse(entry); sel.kind == KeepSelector::Kind::Form) {
						add(sel.form);
					}
				}
			}
			for (const auto& q : a_layout.keep->quotas) {
				if (q.forms) {
					for (const auto& f : *q.forms) {
						add(f);
					}
				}
			}
		}
		return out;
	}

	Layout ExportAsTemplate(const Layout& a_layout, const ExportOptions& a_options, std::size_t* a_learnedKept)
	{
		Layout      out = a_layout;
		std::size_t kept = 0;
		if (a_options.learnedAsForms) {
			const auto boundExplicitly = [&](const FormRef& a_form) {
				return std::ranges::any_of(out.containers, [&](const ContainerBinding& c) { return std::ranges::find(c.forms, a_form) != c.forms.end(); });
			};
			for (const auto& [form, container] : a_layout.learned) {
				if (out.IsMaster(container) || boundExplicitly(form)) {
					continue;
				}
				auto* binding = out.FindContainer(container);
				if (binding && binding->ignored) {
					continue;
				}
				if (!binding) {
					ContainerBinding entry;
					entry.ref = container;
					out.containers.push_back(std::move(entry));
					binding = &out.containers.back();
				}
				binding->forms.push_back(form);
				++kept;
			}
		}
		if (a_learnedKept) {
			*a_learnedKept = kept;
		}
		out.learned.clear();
		out.unparsedLearned = nlohmann::json::object();
		out.declined.clear();  // the player's answers, not part of a shared setup
		out.unparsedDeclined = nlohmann::json::object();
		out.basedOn.reset();
		out.hiddenTemplates.clear();  // which templates this player removed from the home: not part of a shared setup
		out.autoDraft = false;  // the player's own setup
		for (auto& plugin : ReferencedPlugins(out)) {
			if (!PluginLoaded(plugin, out.requiredPlugins)) {
				out.requiredPlugins.push_back(std::move(plugin));
			}
		}
		return out;
	}

	std::string TemplateFileStem(std::string_view a_name)
	{
		std::string out;
		for (const char c : a_name) {
			const auto u = static_cast<unsigned char>(c);
			const bool ok = (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || c == ' ' || c == '-' || c == '_' || c == '.';
			out += ok ? c : '_';
		}
		while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
			out.pop_back();
		}
		while (!out.empty() && (out.front() == ' ' || out.front() == '.')) {
			out.erase(out.begin());
		}
		if (out.size() > 64) {
			out.resize(64);
			while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
				out.pop_back();
			}
		}
		static constexpr std::string_view kReserved[]{ "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
			"LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };
		// Windows reserves these names with any extension too ("CON.txt" is the console): the part before the
		// first '.' decides, and the '_' goes right after it.
		const auto name = std::string_view(out).substr(0, out.find('.'));
		if (std::ranges::any_of(kReserved, [&](std::string_view a_reserved) { return IEquals(a_reserved, name); })) {
			out.insert(name.size(), 1, '_');
		}
		return out.empty() ? std::string("Template") : out;
	}

	std::optional<TemplateFileInfo> ReadTemplateFileInfo(const std::filesystem::path& a_path)
	{
		std::error_code ec;
		if (!std::filesystem::exists(a_path, ec)) {
			return std::nullopt;
		}
		TemplateFileInfo info;
		info.path = a_path;
		if (const auto text = detail::ReadTextFile(a_path)) {
			Diagnostics diags;  // the file's own problems are not the bulk draft's business
			if (const auto layout = ParseLayoutText(*text, detail::PathToUtf8(a_path), diags)) {
				info.parsed = true;
				info.autoDraft = layout->autoDraft;
				if (layout->cell) {
					info.cells.push_back(*layout->cell);
				}
				for (const auto& c : layout->cells) {
					info.cells.push_back(c.cell);
				}
			}
		}
		return info;
	}

	std::vector<TemplateFileInfo> ScanTemplateFiles(const std::filesystem::path& a_dir)
	{
		std::vector<TemplateFileInfo> out;
		std::error_code               ec;
		if (!std::filesystem::is_directory(a_dir, ec)) {
			return out;
		}
		for (auto it = std::filesystem::recursive_directory_iterator(a_dir, std::filesystem::directory_options::skip_permission_denied, ec);
			 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
			if (it->is_regular_file(ec) && IEquals(detail::PathToUtf8(it->path().extension()), ".json")) {
				if (auto info = ReadTemplateFileInfo(it->path())) {
					out.push_back(std::move(*info));
				}
			}
		}
		std::ranges::sort(out, [](const auto& a_lhs, const auto& a_rhs) { return ICompare(detail::PathToUtf8(a_lhs.path), detail::PathToUtf8(a_rhs.path)) < 0; });
		return out;
	}

	AutoDraftCheck CheckAutoDraftTarget(std::span<const TemplateFileInfo> a_files, const std::filesystem::path& a_target, std::span<const FormRef> a_cells)
	{
		const auto forHome = [&](const TemplateFileInfo& a_file) {
			return std::ranges::any_of(a_file.cells, [&](const FormRef& c) { return std::ranges::find(a_cells, c) != a_cells.end(); });
		};
		const auto isTarget = [&](const std::filesystem::path& a_path) {
			std::error_code ec;
			return std::filesystem::equivalent(a_path, a_target, ec) ||
			       IEquals(detail::PathToUtf8(a_path.lexically_normal()), detail::PathToUtf8(a_target.lexically_normal()));
		};
		AutoDraftCheck out;
		if (const auto there = ReadTemplateFileInfo(a_target)) {
			if (!there->parsed || !there->autoDraft) {
				return { AutoDraftVerdict::Person, a_target };
			}
			if (!forHome(*there)) {
				return { AutoDraftVerdict::OtherHome, a_target };
			}
			out.verdict = AutoDraftVerdict::Refresh;
		}
		for (const auto& f : a_files) {
			if (f.parsed && !f.autoDraft && forHome(f) && !isTarget(f.path)) {
				return { AutoDraftVerdict::Person, f.path };
			}
		}
		return out;
	}
}
