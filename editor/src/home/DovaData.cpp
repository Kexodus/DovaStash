#include "home/DovaData.h"

#include <algorithm>
#include <map>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "Util.h"
#include "Workspace.h"
#include "dova/core/Template.h"
#include "home/TemplateFile.h"

namespace fs = std::filesystem;

namespace dova::editor::home
{
	namespace
	{
		const wchar_t* const kDovaDir = L"SKSE\\Plugins\\DovaStash";

		fs::path ExeDir()
		{
			wchar_t exe[MAX_PATH]{};
			if (::GetModuleFileNameW(nullptr, exe, MAX_PATH) > 0) {
				return fs::path(exe).parent_path();
			}
			return {};
		}

		// Roots that hold Dova Stash's data: the merged Data folder's, else the fallbacks.
		std::vector<fs::path> DovaRoots(const Workspace& a_ws, const wchar_t* a_probe)
		{
			std::error_code       ec;
			std::vector<fs::path> roots;
			for (const auto& r : a_ws.Files().Roots()) {
				if (fs::exists(r / kDovaDir / a_probe, ec)) {
					roots.push_back(r);
				}
			}
			if (!roots.empty()) {
				return roots;
			}
			// The editor ships in the mod folder's Tools/ (and may sit in the mod folder itself).
			for (const auto& dir : { ExeDir(), ExeDir().parent_path() }) {
				if (!dir.empty() && fs::exists(dir / kDovaDir / a_probe, ec)) {
					return { dir };
				}
			}
			for (const auto& m : a_ws.Mods()) {
				if (core::IEquals(m.name, "Dova Stash") && fs::exists(m.dir / kDovaDir / a_probe, ec)) {
					return { m.dir };
				}
			}
			for (const auto& m : a_ws.Mods()) {
				if (!m.separator && fs::exists(m.dir / kDovaDir / L"Categories" / L"00_Default.json", ec) && fs::exists(m.dir / kDovaDir / a_probe, ec)) {
					return { m.dir };
				}
			}
			return {};
		}

		std::string OriginOf(const Workspace& a_ws, const fs::path& a_root)
		{
			for (const auto& m : a_ws.Mods()) {
				if (m.dir == a_root) {
					return m.name;
				}
			}
			if (a_ws.Instance() && a_root == a_ws.Instance()->overwriteDir) {
				return "Overwrite";
			}
			return a_root == a_ws.DataDir() ? std::string("Data") : PathToUtf8(a_root.filename());
		}
	}

	CategoryData LoadCategories(const Workspace& a_ws)
	{
		CategoryData                    out;
		std::map<std::string, fs::path> files;
		std::error_code                 ec;
		for (const auto& root : DovaRoots(a_ws, L"Categories")) {
			const auto dir = root / kDovaDir / L"Categories";
			for (const auto& e : fs::directory_iterator(dir, ec)) {
				if (e.is_regular_file(ec) && IEndsWith(PathToUtf8(e.path().filename()), ".json")) {
					files[core::ToLower(PathToUtf8(e.path().filename()))] = e.path();
					out.source = PathToUtf8(fs::path(dir).make_preferred());
				}
			}
		}
		core::Diagnostics                           diags;
		std::vector<std::vector<core::CategoryDef>> parsed;
		for (const auto& [name, path] : files) {
			if (const auto text = ReadTextFile(path)) {
				parsed.push_back(core::ParseCategoryText(*text, name, diags));
			}
		}
		out.defs = core::MergeCategoryFiles(parsed, diags);
		if (!out.defs.empty()) {
			out.set = core::CategorySet::Compile(out.defs, [](const core::FormRef&) { return std::vector<core::FormRef>{}; }, diags);
		}
		for (const auto& d : diags.Items()) {
			out.problems.push_back(d.ToString());
		}
		return out;
	}

	std::vector<const KnownTemplate*> TemplateIndex::ForCell(const core::FormRef& a_cell) const
	{
		std::vector<const KnownTemplate*> out;
		for (const auto& t : templates) {
			if ((t.layout.cell && *t.layout.cell == a_cell) || std::ranges::any_of(t.layout.cells, [&](const core::HomeCell& c) { return c.cell == a_cell; })) {
				out.push_back(&t);
			}
		}
		return out;
	}

	TemplateIndex LoadTemplateIndex(const Workspace& a_ws)
	{
		TemplateIndex                                           out;
		std::map<std::string, std::pair<fs::path, fs::path>>    files;  // lower-case relative path -> (file, root)
		std::error_code                                         ec;
		auto                                                    roots = DovaRoots(a_ws, L"Templates");
		if (a_ws.Instance()) {
			const auto mine = a_ws.Instance()->modsDir / PathFromUtf8(kTemplatesMod);
			if (std::ranges::find(roots, mine) == roots.end() && fs::exists(mine / kDovaDir / L"Templates", ec)) {
				roots.push_back(mine);
			}
		}
		for (const auto& root : roots) {
			const auto dir = root / kDovaDir / L"Templates";
			for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
				if (!it->is_regular_file(ec) || !IEndsWith(PathToUtf8(it->path().filename()), ".json")) {
					continue;
				}
				auto rel = PathToUtf8(it->path().lexically_relative(dir));
				std::ranges::replace(rel, '\\', '/');
				files[core::ToLower(rel)] = { it->path(), root };
			}
		}
		for (const auto& [rel, where] : files) {
			core::Diagnostics diags;
			const auto&       [path, root] = where;
			auto              relative = PathToUtf8(path.lexically_relative(root / kDovaDir / L"Templates"));
			std::ranges::replace(relative, '\\', '/');
			KnownTemplate t;
			t.source = "Templates/" + relative;
			auto layout = core::LoadLayoutFile(path, t.source, diags);
			if (!layout) {
				out.problems.push_back(t.source + ": cannot be read");
				continue;
			}
			t.path = path;
			t.origin = OriginOf(a_ws, root);
			t.user = core::IsUserTemplate(t.source) || core::IEquals(t.origin, kTemplatesMod) || core::IEquals(t.origin, "Overwrite");
			t.layout = std::move(*layout);
			out.templates.push_back(std::move(t));
		}
		return out;
	}

	RulesData LoadDraftRules(const Workspace& a_ws)
	{
		RulesData out;
		out.rules = core::DefaultDraftRules();
		out.source = "built in";
		const auto roots = DovaRoots(a_ws, L"DraftRules.json");
		if (roots.empty()) {
			return out;
		}
		const auto path = roots.back() / kDovaDir / L"DraftRules.json";
		core::Diagnostics diags;
		if (const auto text = ReadTextFile(path)) {
			if (auto rules = core::ParseDraftRules(*text, PathToUtf8(path), diags)) {
				out.rules = std::move(*rules);
				out.source = PathToUtf8(fs::path(path).make_preferred());
			}
		}
		for (const auto& d : diags.Items()) {
			out.problems.push_back(d.ToString());
		}
		return out;
	}

	fs::path SaveBase(const Workspace& a_ws)
	{
		if (a_ws.Instance()) {
			return a_ws.Instance()->modsDir / PathFromUtf8(kTemplatesMod);
		}
		return a_ws.DataDir();
	}
}
