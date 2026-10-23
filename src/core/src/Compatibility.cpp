#include "dova/core/Compatibility.h"

#include <algorithm>
#include <iterator>
#include <optional>

#include "JsonUtil.h"

namespace dova::core
{
	using detail::json;

	namespace
	{
		bool PluginIn(std::string_view a_plugin, std::span<const std::string> a_plugins)
		{
			return std::ranges::any_of(a_plugins, [&](const std::string& p) { return IEquals(p, a_plugin); });
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

		// A list of form references; bad entries are errors and are skipped.
		std::vector<FormRef> ReadRefs(const json& a_value, const std::string& a_source, const std::string& a_where, Diagnostics& a_diags)
		{
			std::vector<FormRef> out;
			const auto           list = detail::ReadStringList(a_value, a_source, a_where, a_diags);
			if (!list) {
				return out;
			}
			for (const auto& s : *list) {
				if (auto ref = FormRef::Parse(s)) {
					if (std::ranges::find(out, *ref) == out.end()) {
						out.push_back(std::move(*ref));
					}
				} else {
					a_diags.Error(a_source, a_where + ": invalid form reference \"" + s + "\" (skipped): " + FormRefError(s));
				}
			}
			return out;
		}

		std::optional<SelfSortingHome> ParseEntry(const json& a_value, const std::string& a_source, const std::string& a_where, Diagnostics& a_diags)
		{
			if (!a_value.is_object()) {
				a_diags.Error(a_source, a_where + ": expected an object, got " + detail::TypeName(a_value) + " (skipped)");
				return std::nullopt;
			}
			const auto pluginIt = a_value.find("plugin");
			if (pluginIt == a_value.end() || !pluginIt->is_string() || !IsPluginFileName(pluginIt->get<std::string>())) {
				a_diags.Error(a_source, a_where + ": \"plugin\" must be a plugin file name (.esp, .esm or .esl) (skipped)");
				return std::nullopt;
			}
			SelfSortingHome home;
			home.plugin = pluginIt->get<std::string>();
			home.source = a_source;
			for (const auto& [key, value] : a_value.items()) {
				const auto where = a_where + "." + key;
				if (key == "plugin") {
					continue;
				}
				if ((key == "name" || key == "note") && value.is_string()) {
					(key == "name" ? home.name : home.note) = value.get<std::string>();
				} else if (key == "cells" || key == "containers") {
					(key == "cells" ? home.cells : home.containers) = ReadRefs(value, a_source, where, a_diags);
				} else if (key == "name" || key == "note") {
					a_diags.Error(a_source, where + ": expected a string (ignored)");
				} else {
					a_diags.Warn(a_source, where + ": unknown field (ignored)");
				}
			}
			// A narrowed entry whose every reference was bad must not widen to the whole plugin.
			const bool narrowed = a_value.contains("cells") || a_value.contains("containers");
			if (narrowed && home.WholePlugin()) {
				a_diags.Error(a_source, a_where + ": no usable \"cells\" or \"containers\" (skipped)");
				return std::nullopt;
			}
			return home;
		}
	}

	std::vector<SelfSortingHome> ParseSelfSorting(const json& a_doc, std::string_view a_source, Diagnostics& a_diags)
	{
		const std::string            source(a_source);
		std::vector<SelfSortingHome> out;
		if (!a_doc.is_object()) {
			a_diags.Error(source, "expected a JSON object at the top level");
			return out;
		}
		if (const auto it = a_doc.find("$schema"); it == a_doc.end()) {
			a_diags.Warn(source, "missing \"$schema\"; assuming \"dovastash-selfsorting-1\"");
		} else if (!it->is_string() || it->get<std::string>() != kSelfSortingSchema) {
			a_diags.Error(source, "unsupported \"$schema\" (expected \"dovastash-selfsorting-1\")");
			return out;
		}
		const auto homes = a_doc.find("homes");
		if (homes == a_doc.end() || !homes->is_array()) {
			a_diags.Error(source, "\"homes\": expected an array");
			return out;
		}
		for (const auto& [key, value] : a_doc.items()) {
			if (key != "$schema" && key != "homes" && key != "note") {
				a_diags.Warn(source, "unknown field \"" + key + "\" (ignored)");
			}
		}
		for (std::size_t i = 0; i < homes->size(); ++i) {
			if (auto home = ParseEntry((*homes)[i], source, "homes[" + std::to_string(i) + "]", a_diags)) {
				out.push_back(std::move(*home));
			}
		}
		return out;
	}

	std::vector<SelfSortingHome> ParseSelfSortingText(std::string_view a_text, std::string_view a_source, Diagnostics& a_diags)
	{
		const auto doc = detail::ParseJson(a_text, a_source, a_diags);
		return doc ? ParseSelfSorting(*doc, a_source, a_diags) : std::vector<SelfSortingHome>{};
	}

	std::vector<SelfSortingHome> LoadSelfSortingDirectory(const std::filesystem::path& a_dir, Diagnostics& a_diags)
	{
		std::vector<SelfSortingHome> out;
		std::error_code              ec;
		if (!std::filesystem::is_directory(a_dir, ec)) {
			a_diags.Warn(detail::PathToUtf8(a_dir), "directory not found");
			return out;
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
		for (const auto& file : files) {
			const auto source = "SelfSorting/" + detail::PathToUtf8(file.filename());
			const auto text = detail::ReadTextFile(file);
			if (!text) {
				a_diags.Error(source, "cannot read file");
				continue;
			}
			auto homes = ParseSelfSortingText(*text, source, a_diags);
			std::ranges::move(homes, std::back_inserter(out));
		}
		return out;
	}

	SelfSortingHomes::SelfSortingHomes(std::vector<SelfSortingHome> a_entries, std::span<const std::string> a_loadedPlugins)
	{
		for (auto& e : a_entries) {
			if (PluginIn(e.plugin, a_loadedPlugins)) {
				_entries.push_back(std::move(e));
			}
		}
	}

	const SelfSortingHome* SelfSortingHomes::ForCell(const FormRef& a_cell) const
	{
		for (const auto& e : _entries) {
			if (e.WholePlugin() ? IEquals(e.plugin, a_cell.plugin) : std::ranges::find(e.cells, a_cell) != e.cells.end()) {
				return &e;
			}
		}
		return nullptr;
	}

	const SelfSortingHome* SelfSortingHomes::ForContainer(const FormRef& a_ref, const FormRef* a_base) const
	{
		for (const auto& e : _entries) {
			const auto listed = [&](const FormRef& a_form) { return std::ranges::find(e.containers, a_form) != e.containers.end(); };
			if (listed(a_ref) || (a_base && listed(*a_base))) {
				return &e;
			}
		}
		return nullptr;
	}

	// --- Template fit -----------------------------------------------------------------------------------------------

	std::vector<FormRef> TemplateFitRefs(const Layout& a_template)
	{
		std::vector<FormRef> out;
		const auto           add = [&](const FormRef& a_ref) {
			if (std::ranges::find(out, a_ref) == out.end()) {
				out.push_back(a_ref);
			}
		};
		if (a_template.master) {
			add(*a_template.master);
		}
		for (const auto& c : a_template.containers) {
			add(c.ref);
		}
		for (const auto& d : a_template.displays) {
			add(d.ref);
		}
		return out;
	}

	TemplateFit JudgeTemplateFit(const Layout& a_template, const std::function<RefPresence(const FormRef&)>& a_probe)
	{
		TemplateFit fit;
		for (const auto& ref : TemplateFitRefs(a_template)) {
			switch (a_probe ? a_probe(ref) : RefPresence::Unknown) {
			case RefPresence::Present:
				++fit.present;
				break;
			case RefPresence::NotBuilt:
				++fit.notBuilt;
				break;
			case RefPresence::Disabled:
				++fit.disabled;
				break;
			case RefPresence::Missing:
				++fit.missing;
				break;
			case RefPresence::Unknown:
				++fit.unknown;
				break;
			}
		}
		fit.checked = fit.present + fit.notBuilt + fit.disabled + fit.missing;
		fit.fits = fit.checked == 0 || (fit.present + fit.notBuilt) * 100 >= kTemplateFitPercent * fit.checked;
		return fit;
	}
}
