// Headless bulk drafts and the "where items go" preview (docs/EDITOR.md §3.4, §3.5, §5.6):
//   --draft-all [--only <text>] [--compare] [--save-to <folder> [--author <name>] [--include-templated]]
//   --preview <cell> [--sample haul|all|<pattern>] [--draft]
// --draft-all --save-to writes auto-drafts ("autoDraft": true, DESIGN §6.8): a home's own earlier auto-draft is
// refreshed, a template someone made in the folder is never replaced (core::CheckAutoDraftTarget), and the
// output says which homes were left alone and why.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <format>
#include <map>

#include "Cli.h"
#include "Util.h"
#include "dova/core/Preview.h"
#include "dova/core/Template.h"
#include "esp/Items.h"
#include "home/DovaData.h"
#include "home/Drafts.h"
#include "home/Preview.h"
#include "home/TemplateFile.h"

namespace dova::editor
{
	namespace
	{
		void Out(const std::string& a_text) { std::fputs(a_text.c_str(), stdout); }
		void Err(const std::string& a_text) { std::fputs(a_text.c_str(), stderr); }

		using Clock = std::chrono::steady_clock;
		long long Ms(Clock::time_point a_start) { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - a_start).count(); }

		std::string Join(const std::vector<std::string>& a_items, const char* a_sep = ", ")
		{
			std::string out;
			for (const auto& i : a_items) {
				out += (out.empty() ? "" : a_sep) + i;
			}
			return out;
		}

		std::string Status(const std::vector<const home::KnownTemplate*>& a_templates)
		{
			if (a_templates.empty()) {
				return "no template";
			}
			// Auto-drafts are not a person's template: the next --draft-all --save-to may refresh them.
			const bool user = std::ranges::any_of(a_templates, [](const home::KnownTemplate* t) { return t->user && !t->layout.autoDraft; });
			const bool shipped = std::ranges::any_of(a_templates, [](const home::KnownTemplate* t) { return !t->user && !t->layout.autoDraft; });
			return user && shipped ? "user + shipped template" : user ? "user template" : shipped ? "shipped template" : "auto-draft only";
		}

		// The templates of a home: its main cell's, and those of its other cells.
		std::vector<const home::KnownTemplate*> TemplatesOf(const home::TemplateIndex& a_index, esp::LoadOrder& a_lo, const home::HomeEntry& a_home, bool a_mainOnly)
		{
			auto out = a_index.ForCell(a_lo.ToFormRef(a_home.cell));
			if (!a_mainOnly) {
				for (const auto& e : a_home.extra) {
					for (const auto* t : a_index.ForCell(a_lo.ToFormRef(e.cell))) {
						if (std::ranges::find(out, t) == out.end()) {
							out.push_back(t);
						}
					}
				}
			}
			return out;
		}

		std::string Label(const core::DraftObject& a_o) { return a_o.name.empty() ? (a_o.editorId.empty() ? a_o.id : a_o.editorId) : a_o.name; }

		// The active load order, or with --base-game only the game's own plugins (base game, DLCs, Creation Club):
		// the homes as the shipped templates were written for them.
		std::unique_ptr<esp::LoadOrder> OpenFor(const Workspace& a_ws, const Args& a_args, std::vector<std::string>& a_errors)
		{
			if (!a_args.baseGame) {
				return a_ws.OpenLoadOrder(a_errors);
			}
			auto lo = a_ws.NewLoadOrder();
			for (const char* p : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" }) {
				lo->AddWithMasters(p, a_errors);
			}
			for (const auto& p : a_ws.BasePlugins()) {
				lo->AddWithMasters(p, a_errors);
			}
			return lo;
		}
	}

	int DraftAllCommand(const Workspace& a_ws, const Args& a_args)
	{
		const auto               start = Clock::now();
		std::vector<std::string> errors;
		auto                     lo = OpenFor(a_ws, a_args, errors);
		for (const auto& e : errors) {
			Err("warning: " + e + "\n");
		}
		const auto loadMs = Ms(start);
		const auto categories = home::LoadCategories(a_ws);
		const auto rules = home::LoadDraftRules(a_ws);
		const auto templates = home::LoadTemplateIndex(a_ws);
		for (const auto& p : rules.problems) {
			Err("draft rules: " + p + "\n");
		}
		if (!categories.set) {
			Err("warning: no Dova Stash categories found (Categories/*.json): suggestions cannot be checked against them\n");
		}
		const auto scanStart = Clock::now();
		std::size_t lastPct = 101;
		auto        homes = home::FindAllHomes(*lo, [&](std::size_t a_done, std::size_t a_total, const std::string&) {
            const auto pct = a_total ? a_done * 100 / a_total : 100;
            if (pct / 10 != lastPct / 10) {
                lastPct = pct;
                Err(std::format("scanning cells: {}%\r", pct));
            }
		});
		Err("\n");
		if (a_args.only) {
			std::erase_if(homes, [&](const home::HomeEntry& h) {
				return !IContains(h.name, *a_args.only) && !IContains(h.editorId, *a_args.only) && !IContains(h.plugin, *a_args.only) && !IContains(h.cellRef, *a_args.only);
			});
		}
		Out(a_ws.Describe() + "\n");
		Out(std::format("Load order: {} plugins ({} ms). Homes: {} ({} ms). Categories: {} from {}. Draft rules: {}. Templates known: {}.\n\n", lo->PluginCount(),
			loadMs, homes.size(), Ms(scanStart), categories.defs.size(), categories.source.empty() ? "nowhere" : categories.source, rules.source,
			templates.templates.size()));

		const auto               catalogStart = Clock::now();
		const auto               catalog = esp::BuildItemCatalog(*lo);
		const auto               itemNames = categories.set ? home::BuildItemNameIndex(catalog, *categories.set) : home::ItemNameIndex{};
		Out(std::format("Items: {} playable in the load order ({} ms)\n\n", catalog.items.size(), Ms(catalogStart)));
		const home::DraftContext ctx{ categories.set ? &*categories.set : nullptr, &rules.rules, &itemNames };
		struct Totals
		{
			int homes{ 0 }, compared{ 0 }, containers{ 0 }, exact{ 0 }, overlap{ 0 }, different{ 0 }, missing{ 0 }, categories{ 0 }, sameDest{ 0 }, masters{ 0 },
				ignoredT{ 0 }, ignoredAgree{ 0 }, saved{ 0 }, refreshed{ 0 }, leftAlone{ 0 }, suggestions{ 0 }, accepted{ 0 };
		} total;
		// The template files already in the destination: a home that has one someone made is never written over.
		std::vector<core::TemplateFileInfo> existing;
		if (a_args.saveTo) {
			existing = core::ScanTemplateFiles(home::TemplatesDir(PathFromUtf8(*a_args.saveTo)));
		}
		for (const auto& h : homes) {
			const auto tmpl = TemplatesOf(templates, *lo, h, false);
			const bool personsTemplate = std::ranges::any_of(tmpl, [](const home::KnownTemplate* t) { return !t->layout.autoDraft; });
			const auto start1 = Clock::now();
			auto       draft = home::DraftHome(*lo, h, ctx);
			++total.homes;
			std::string cells = h.cellRef;
			for (const auto& e : h.extra) {
				cells += std::format(" + {} ({}{})", e.name.empty() ? e.editorId : e.name, e.cellRef, e.likely ? "" : ", storage behind a door");
			}
			Out(std::format("== {}  [{}]  {}  {}\n   cells: {}\n   {} containers, {} displays; draft in {} ms\n", h.name, Status(tmpl), h.plugin,
				h.lastPlugin != h.plugin ? "(last edited by " + h.lastPlugin + ")" : "", cells, h.containers, h.displays, Ms(start1)));
			for (const auto* t : tmpl) {
				Out(std::format("   template: {} ({}, {}{})\n", t->source, t->origin, t->user ? "user" : "shipped", t->layout.autoDraft ? ", auto-draft" : ""));
			}
			Out("   master: " + draft.plan.masterReason + "\n");
			if (!draft.plan.masterRunnersUp.empty()) {
				Out("     runners-up: " + Join(draft.plan.masterRunnersUp) + "\n");
			}
			for (const auto& c : draft.plan.containers) {
				const auto& o = draft.objects[c.object];
				if (draft.plan.master && *draft.plan.master == c.object) {
					continue;
				}
				if (c.ignored) {
					Out(std::format("   - {:<28} ignored: {}\n", Label(o), c.note));
					continue;
				}
				std::vector<std::string> acc, rej;
				for (const auto& s : c.suggestions) {
					++total.suggestions;
					total.accepted += s.accepted ? 1 : 0;
					(s.accepted ? acc : rej).push_back(std::format("{} {:.0f}%", s.pattern, s.confidence * 100.0f));
				}
				Out(std::format("   - {:<28} {}{}\n", Label(o), acc.empty() ? std::string("(nothing: takes what the master would)") : Join(acc),
					rej.empty() ? std::string{} : "   [not accepted: " + Join(rej) + "]"));
				if (a_args.verbose) {
					for (const auto& s : c.suggestions) {
						Out(std::format("       {} {} {:.0f}% ({}): {}\n", s.accepted ? "+" : " ", s.pattern, s.confidence * 100.0f, core::DraftSourceName(s.source), s.reason));
					}
				}
			}
			for (const auto& d : draft.diagnostics) {
				Out("   check: " + d + "\n");
			}
			if (a_args.compare && categories.set) {
				for (const auto* t : tmpl) {
					if (!(t->layout.cell && *t->layout.cell == draft.layout.cell)) {
						continue;  // a cellar's own template: compared on its own run
					}
					const auto cmp = home::CompareToTemplate(draft.layout, t->layout, *categories.set);
					++total.compared;
					total.containers += cmp.containers;
					total.exact += cmp.exact;
					total.overlap += cmp.overlap;
					total.different += cmp.different;
					total.missing += cmp.missing;
					total.categories += cmp.categories;
					total.sameDest += cmp.sameDestination;
					total.masters += cmp.sameMaster ? 1 : 0;
					total.ignoredT += cmp.ignoredInTemplate;
					total.ignoredAgree += cmp.ignoredAgree;
					Out(std::format("   compared with {}: master {}; containers with categories {}: {} same, {} overlap, {} different, {} missing; "
					                "{} of {} categories go to the same container; ignored {}/{} agree\n",
						t->source, cmp.sameMaster ? "same" : "different", cmp.containers, cmp.exact, cmp.overlap, cmp.different, cmp.missing, cmp.sameDestination,
						cmp.categories, cmp.ignoredAgree, cmp.ignoredInTemplate));
					if (a_args.verbose) {
						for (const auto& line : cmp.details) {
							Out("     " + line + "\n");
						}
					}
				}
			}
			if (a_args.saveTo && personsTemplate && !a_args.includeTemplated) {
				++total.leftAlone;
				Out("   not saved: this home has a template (--include-templated drafts it anyway, never over a template in the folder)\n");
			} else if (a_args.saveTo) {
				if (a_args.author) {
					draft.layout.author = *a_args.author;
				}
				const auto base = PathFromUtf8(*a_args.saveTo);
				const auto target = home::TemplatePath(base, draft.layout, h.name);
				const auto check = core::CheckAutoDraftTarget(existing, target, home::HomeCellRefs(*lo, h));
				if (check.verdict == core::AutoDraftVerdict::Person || check.verdict == core::AutoDraftVerdict::OtherHome) {
					++total.leftAlone;
					Out(std::format("   not saved: {} is {}; left as it is\n", PathToUtf8(check.blocker),
						check.verdict == core::AutoDraftVerdict::Person ? "a template someone made (not an auto-draft)" : "another home's auto-draft (same home name)"));
				} else if (const auto r = home::SaveTemplateFile(base, draft.layout, { .autoDraft = true }, h.name); r.ok) {
					++total.saved;
					total.refreshed += r.replaced ? 1 : 0;
					Out(std::format("   saved auto-draft version {} to {}{}\n", r.version, PathToUtf8(r.path),
						r.replaced ? " (refreshed the earlier auto-draft, kept as " + PathToUtf8(r.backup.filename()) + ")" : ""));
					std::erase_if(existing, [&](const core::TemplateFileInfo& f) { return f.path == r.path; });
					if (auto info = core::ReadTemplateFileInfo(r.path)) {
						existing.push_back(std::move(*info));  // a second home of the same name sees it
					}
				} else if (r.skipped) {
					++total.leftAlone;
					Out("   not saved: " + r.error + "\n");
				} else {
					Err("error: " + r.error + "\n");
				}
			}
			Out("\n");
		}
		Out(std::format("{} homes drafted, {} suggestions ({} accepted){}\n", total.homes, total.suggestions, total.accepted,
			a_args.saveTo ? std::format(", {} auto-drafts saved to {} ({} refreshed), {} homes left alone (each \"not saved\" above says why)", total.saved, *a_args.saveTo,
								total.refreshed, total.leftAlone)
						  : std::string{}));
		if (a_args.compare && total.compared > 0) {
			Out(std::format("Against {} templates: masters {}/{} same; containers with categories {}: {} same, {} overlap, {} different, {} missing; "
			                "categories to the same container {}/{} ({:.0f}%); ignored {}/{} agree\n",
				total.compared, total.masters, total.compared, total.containers, total.exact, total.overlap, total.different, total.missing, total.sameDest,
				total.categories, total.categories ? 100.0 * total.sameDest / total.categories : 0.0, total.ignoredAgree, total.ignoredT));
		}
		return 0;
	}

	int PreviewCommand(const Workspace& a_ws, const Args& a_args)
	{
		const auto               start = Clock::now();
		std::vector<std::string> errors;
		auto                     lo = OpenFor(a_ws, a_args, errors);
		for (const auto& e : errors) {
			Err("warning: " + e + "\n");
		}
		auto key = lo->ParseFormRef(*a_args.preview);
		if (!key || !lo->FindCell(*key)) {
			key = lo->FindCellByEditorId(*a_args.preview);
		}
		if (!key) {
			Err("error: cell \"" + *a_args.preview + "\" not found in the load order\n");
			return 1;
		}
		const auto categories = home::LoadCategories(a_ws);
		if (!categories.set) {
			Err("error: no Dova Stash categories found (Categories/*.json)\n");
			return 1;
		}
		const auto rules = home::LoadDraftRules(a_ws);
		const auto templates = home::LoadTemplateIndex(a_ws);
		auto       cell = home::LoadCell(*lo, *key);
		if (!cell) {
			Err("error: cell cannot be read\n");
			return 1;
		}
		std::vector<home::CellInfo> cells{ std::move(*cell) };
		core::Layout                layout;
		std::string                 what;
		const auto                  found = templates.ForCell(lo->ToFormRef(*key));
		const home::KnownTemplate*  chosen = nullptr;
		for (const auto* t : found) {
			if (t->layout.cell && *t->layout.cell == lo->ToFormRef(*key) && (!chosen || t->layout.priority > chosen->layout.priority)) {
				chosen = t;
			}
		}
		if (chosen && !a_args.useDraft) {
			layout = chosen->layout;
			what = "template " + chosen->source;
			for (const auto& extra : layout.cells) {
				if (const auto k = lo->ParseFormRef(extra.cell.ToString())) {
					if (auto c = home::LoadCell(*lo, *k, home::LoadCellOptions{ .resolveDoors = false })) {
						cells.push_back(std::move(*c));
					}
				}
			}
		} else {
			core::Diagnostics diags;
			layout = home::DraftTemplate(*lo, cells.front(), diags, &*categories.set, &rules.rules);
			what = chosen ? "the draft (a template exists: drop --draft to use it)" : "the draft (no template for this cell)";
		}
		const auto catStart = Clock::now();
		const auto catalog = esp::BuildItemCatalog(*lo);
		const auto catMs = Ms(catStart);
		home::SampleKind kind = home::SampleKind::Haul;
		std::string      pattern;
		if (core::IEquals(a_args.sample, "all")) {
			kind = home::SampleKind::All;
		} else if (!core::IEquals(a_args.sample, "haul")) {
			kind = home::SampleKind::Category;
			pattern = core::ToLower(a_args.sample);
			if (!core::IsValidCategoryPattern(pattern)) {
				Err("error: --sample wants haul, all or a category pattern (\"alchemy.*\")\n");
				return 2;
			}
		}
		const auto items = home::MakeSample(catalog, *categories.set, kind, pattern);
		const auto setsStart = Clock::now();
		const auto sets = home::BuildArmorSetIndex(*lo, catalog);
		const auto setsMs = Ms(setsStart);
		const auto spots = home::DisplaySpots(*lo, cells, layout, &sets);
		const auto result = core::PreviewWhereItemsGo(layout, *categories.set, items, spots);

		std::map<core::FormRef, std::string> labels;
		for (const auto& c : cells) {
			for (const auto& r : c.refs) {
				labels[lo->ToFormRef(r.key)] = r.baseName.empty() ? r.baseEditorId : r.baseName;
			}
		}
		for (const auto& c : layout.containers) {
			if (!c.label.empty()) {
				labels[c.ref] = c.label;
			}
		}
		std::int32_t copies = 0;
		for (const auto& i : items) {
			copies += i.count;
		}
		Out(std::format("{} {} \"{}\": {} ({} ms; {} items in the load order, {} non-playable or nameless left out, catalog {} ms)\n", lo->Format(*key), cells.front().editorId,
			cells.front().name, what, Ms(start), catalog.items.size(), catalog.skipped, catMs));
		Out(std::format("Armor sets: {} from {} pieces ({} ms)\n", sets.sets.size(), sets.pieces.size(), setsMs));
		Out(std::format("Sample: {}{}: {} stacks, {} items\n\n", home::SampleName(kind), pattern.empty() ? "" : " " + pattern, items.size(), copies));
		for (const auto& c : result.byContainer) {
			std::vector<std::string> per;
			std::vector<std::pair<std::string, std::int32_t>> sorted(c.perCategory.begin(), c.perCategory.end());
			std::ranges::sort(sorted, [](const auto& a, const auto& b) { return a.second > b.second; });
			for (const auto& [cat, n] : sorted) {
				per.push_back(std::format("{} {}", cat.empty() ? "(no category)" : cat, n));
			}
			Out(std::format("{} {:<34} {:>5} items  {}\n", c.master ? "M" : " ", labels.contains(c.ref) ? labels[c.ref] : c.ref.ToString(), c.total,
				c.items.empty() ? std::string("-- receives nothing") : Join(per)));
			if (a_args.verbose) {
				for (std::size_t k = 0; k < c.items.size() && k < 12; ++k) {
					const auto& e = c.items[k];
					Out(std::format("      {} x{} ({}{})\n", items[e.item].name, e.count, core::DestinationReasonName(e.reason), e.category.empty() ? "" : ": " + e.category));
				}
				if (c.items.size() > 12) {
					Out(std::format("      ... {} more\n", c.items.size() - 12));
				}
			}
		}
		if (!result.onDisplays.empty()) {
			Out(std::format("\nOn displays: {}\n", result.onDisplays.size()));
			for (std::size_t k = 0; k < result.onDisplays.size() && k < 15; ++k) {
				const auto& d = result.onDisplays[k];
				Out(std::format("   {} <- {}\n", labels.contains(d.ref) ? labels[d.ref] : d.ref.ToString(), items[d.item].name));
			}
		}
		const auto empty = std::ranges::count_if(result.byContainer, [](const core::PreviewContainer& c) { return !c.master && c.items.empty(); });
		Out(std::format("\n{} items to containers by their bindings, {} to the master; {} containers receive nothing; {} never moved; {} with nowhere to go\n",
			result.toContainers, result.toMaster, empty, result.neverMoved.size(), result.noDestination.size()));
		return 0;
	}
}
