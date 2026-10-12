#include "dova/core/Template.h"

#include <algorithm>
#include <set>
#include <system_error>

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

		// True if a ranks before b.
		bool RanksBefore(const TemplateCandidate& a_lhs, const TemplateCandidate& a_rhs)
		{
			if (a_lhs.layout.priority != a_rhs.layout.priority) {
				return a_lhs.layout.priority > a_rhs.layout.priority;
			}
			if (const bool lhsUser = IsUserTemplate(a_lhs.source); lhsUser != IsUserTemplate(a_rhs.source)) {
				return lhsUser;
			}
			if (const auto c = CompareVersionStrings(a_lhs.layout.version, a_rhs.layout.version); c != 0) {
				return c > 0;
			}
			return ICompare(a_lhs.source, a_rhs.source) < 0;
		}
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
		if (a_template.basedOn) {
			a_diags.Warn(source, "templates should not contain \"basedOn\"");
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
		}
		if (a_template.master) {
			check(*a_template.master, "master");
		}
		for (const auto& c : a_template.containers) {
			check(c.ref, "container");
		}
		for (const auto& d : a_template.displays) {
			check(d.ref, "display");  // its items and set are soft references: never needed in requires (TemplateDisplays.h)
		}
		ValidateDisplays(a_template, a_source, a_diags);
	}

	void ValidateLayoutCategories(const Layout& a_layout, const CategorySet& a_categories, std::string_view a_source, Diagnostics& a_diags)
	{
		const std::string source(a_source);
		for (const auto& c : a_layout.containers) {
			for (const auto& p : c.categories) {
				if (!a_categories.AnyMatches(p)) {
					a_diags.Warn(source, "container " + c.ref.ToString() + ": category pattern \"" + p + "\" matches no known category");
				}
			}
		}
		if (a_layout.keep) {
			auto checkList = [&](const std::vector<std::string>& a_list, std::string_view a_name) {
				for (const auto& e : a_list) {
					const auto sel = KeepSelector::Parse(e);
					if (sel.kind == KeepSelector::Kind::Category && !a_categories.AnyMatches(sel.pattern)) {
						a_diags.Warn(source, "keep." + std::string(a_name) + ": category pattern \"" + e + "\" matches no known category");
					}
				}
			};
			checkList(a_layout.keep->always, "always");
			checkList(a_layout.keep->never, "never");
			for (const auto& q : a_layout.keep->quotas) {
				if (q.category && !a_categories.AnyMatches(*q.category)) {
					a_diags.Warn(source, "keep quota: category pattern \"" + *q.category + "\" matches no known category");
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
			auto missing = MissingRequirements(t.layout, a_loadedPlugins);
			if (!missing.empty()) {
				res.rejected.push_back({ i, std::move(missing) });
				continue;
			}
			eligible.push_back(i);
		}
		std::ranges::stable_sort(eligible, [&](std::size_t a_lhs, std::size_t a_rhs) { return RanksBefore(a_templates[a_lhs], a_templates[a_rhs]); });

		if (!eligible.empty()) {
			res.chosen = eligible.front();
			res.alternatives.assign(eligible.begin() + 1, eligible.end());
		}
		if (a_playerLayout) {
			res.kind = LayoutResolution::Kind::Player;
		} else if (res.chosen) {
			res.kind = LayoutResolution::Kind::Template;
		}
		return res;
	}

	std::string MakeBasedOn(std::string_view a_source, std::string_view a_version)
	{
		std::string out(a_source);
		out += '@';
		out += a_version;
		return out;
	}

	Layout ApplyTemplate(const TemplateCandidate& a_template, const Layout* a_existing)
	{
		Layout layout = a_template.layout;
		layout.learned.clear();
		layout.unparsedLearned = nlohmann::json::object();
		layout.basedOn = MakeBasedOn(a_template.source, a_template.layout.version);

		if (!a_existing) {
			return layout;
		}

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

		// Containers in both: template categories win; the player's ignored flag, explicit forms,
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
			for (const auto& f : old->forms) {
				if (std::ranges::find(c.forms, f) == c.forms.end()) {
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

		// Learned destinations: every entry whose container is still part of the layout (not the master).
		for (const auto& [form, container] : a_existing->learned) {
			if (layout.FindContainer(container) && !layout.IsMaster(container)) {
				layout.learned.emplace(form, container);
			}
		}
		layout.unparsedLearned = a_existing->unparsedLearned;

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
		out.basedOn.reset();
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
		if (std::ranges::any_of(kReserved, [&](std::string_view a_reserved) { return IEquals(a_reserved, out); })) {
			out += '_';
		}
		return out.empty() ? std::string("Template") : out;
	}
}
