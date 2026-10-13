#include "UI/Panel/PanelModel.h"

#include "Controller.h"
#include "Game/Eligibility.h"
#include "Game/FormUtil.h"
#include "Home/HomeRegistry.h"
#include "Home/Store.h"
#include "Settings.h"
#include "UI/UIExtensions.h"
#include "Integrations/QuickLoot.h"
#include "Sort/SortEngine.h"
#include "UI/MenuFramework.h"

#include <DovaStash/Version.h>

namespace dova::ui::panel
{
	namespace
	{
		constexpr std::int64_t kVisibleMs = 1500;     // panel counts as visible this long after its last frame
		constexpr std::int64_t kAutoRefreshMs = 2000;  // light rebuild period while visible

		std::int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		std::string Capitalize(std::string a_text)
		{
			if (!a_text.empty() && a_text[0] >= 'a' && a_text[0] <= 'z') {
				a_text[0] = static_cast<char>(a_text[0] - 'a' + 'A');
			}
			return a_text;
		}

		std::string CategoryName(std::string_view a_id)
		{
			const auto* cat = home::Store::Get().Categories().Find(a_id);
			if (cat && !cat->name.empty()) {
				return cat->name;
			}
			const auto dot = a_id.rfind('.');
			return Capitalize(std::string(dot == std::string_view::npos ? a_id : a_id.substr(dot + 1)));
		}

		std::string PatternLabel(std::string_view a_pattern)
		{
			if (a_pattern == "*") {
				return "Everything";
			}
			if (a_pattern.ends_with(".*")) {
				return "All " + CategoryName(a_pattern.substr(0, a_pattern.size() - 2));
			}
			return CategoryName(a_pattern);
		}

		std::vector<core::Diagnostic> Dedupe(const std::vector<core::Diagnostic>& a_items)
		{
			std::vector<core::Diagnostic> out;
			std::set<std::string>         seen;
			for (const auto& d : a_items) {
				if (seen.insert(d.ToString()).second) {
					out.push_back(d);
				}
			}
			return out;
		}

		std::string RuntimeName()
		{
			switch (REL::Module::GetRuntime()) {
			case REL::Module::Runtime::AE:
				return "Skyrim AE";
			case REL::Module::Runtime::SE:
				return "Skyrim SE";
			case REL::Module::Runtime::VR:
				return "Skyrim VR";
			default:
				return "Skyrim";
			}
		}
	}

	PanelModel& PanelModel::Get()
	{
		static PanelModel model;
		return model;
	}

	// --- any thread ------------------------------------------------------------------------------------

	std::shared_ptr<const HomeSnapshot> PanelModel::Home() const
	{
		std::scoped_lock lock(_lock);
		return _home;
	}

	std::shared_ptr<const GlobalSnapshot> PanelModel::Global() const
	{
		std::scoped_lock lock(_lock);
		return _global;
	}

	std::string PanelModel::Status() const
	{
		std::scoped_lock lock(_lock);
		return _status;
	}

	ExportState PanelModel::Export() const
	{
		std::scoped_lock lock(_lock);
		return _export;
	}

	void PanelModel::BeginExport()
	{
		std::scoped_lock lock(_lock);
		_export = ExportState{};
		_export.pending = true;
	}

	void PanelModel::RequestRefresh(Refresh a_refresh)
	{
		if (a_refresh == Refresh::Full) {
			_fullQueued = true;
		}
		if (_refreshQueued.exchange(true)) {
			return;  // a rebuild is already queued; it picks up _fullQueued
		}
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			_refreshQueued = false;
			return;
		}
		tasks->AddTask([this]() {
			_refreshQueued = false;
			Rebuild(_fullQueued.exchange(false) ? Refresh::Full : Refresh::Light);
		});
	}

	void PanelModel::Post(std::function<void()> a_action, Refresh a_refresh)
	{
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			return;
		}
		tasks->AddTask([this, action = std::move(a_action), a_refresh]() {
			if (action) {
				action();
			}
			Rebuild(a_refresh);
		});
	}

	void PanelModel::MarkRendered()
	{
		const auto now = NowMs();
		_lastRenderMs = now;
		if (now - _lastAutoRefreshMs.load() >= kAutoRefreshMs) {
			_lastAutoRefreshMs = now;
			RequestRefresh(Refresh::Light);
		}
	}

	bool PanelModel::RecentlyRendered() const
	{
		return NowMs() - _lastRenderMs.load() < kVisibleMs;
	}

	void PanelModel::SetStatus(std::string a_message)
	{
		std::scoped_lock lock(_lock);
		_status = std::move(a_message);
	}

	void PanelModel::SetExport(ExportState a_state)
	{
		std::scoped_lock lock(_lock);
		a_state.serial = _export.serial + 1;
		_export = std::move(a_state);
	}

	// --- main thread -----------------------------------------------------------------------------------

	void PanelModel::Focus(std::uint32_t a_cellId, std::optional<core::FormRef> a_container, Tab a_tab)
	{
		_focusCell = a_cellId;
		_focus = std::move(a_container);
		_focusTab = a_tab;
		++_focusSerial;
		Rebuild(Refresh::Full);
	}

	std::shared_ptr<const CategoryTree> PanelModel::Categories()
	{
		if (_categories) {
			return _categories;
		}
		auto tree = std::make_shared<CategoryTree>();

		std::unordered_map<std::string, std::size_t> index;
		std::function<std::size_t(const std::string&)> ensure = [&](const std::string& a_id) -> std::size_t {
			if (const auto it = index.find(a_id); it != index.end()) {
				return it->second;
			}
			std::optional<std::size_t> parent;
			if (const auto dot = a_id.rfind('.'); dot != std::string::npos) {
				parent = ensure(a_id.substr(0, dot));
			}
			const auto self = tree->nodes.size();
			tree->nodes.push_back(CategoryNode{ a_id, CategoryName(a_id), {} });
			index.emplace(a_id, self);
			if (parent) {
				tree->nodes[*parent].children.push_back(self);
			} else {
				tree->roots.push_back(self);
			}
			return self;
		};
		for (const auto& c : home::Store::Get().Categories().Categories()) {
			ensure(c.id);
		}
		_categories = std::move(tree);
		return _categories;
	}

	void PanelModel::Rebuild(Refresh a_refresh)
	{
		auto snap = std::make_shared<HomeSnapshot>();
		snap->serial = ++_serial;
		snap->categories = Categories();
		snap->focusSerial = _focusSerial;
		snap->focusTab = _focusTab;
		snap->globalKeep = home::Store::Get().GlobalKeep();

		const auto previous = Home();
		auto       publish = [&]() {
			std::scoped_lock lock(_lock);
			_home = std::move(snap);
		};

		std::string reason;
		auto*       home = home::HomeRegistry::Get().Current(&reason);
		// Multi-cell homes (DESIGN §6.7): this cell can join the home the player just came from.
		if (const auto offer = home::HomeRegistry::Get().OfferArea()) {
			snap->addAreaTo = offer->homeName;
			snap->addAreaCell = offer->cellId;
			snap->addAreaName = home::AreaName(RE::TESForm::LookupByID<RE::TESObjectCELL>(offer->cellId), offer->cell);
			home = home::HomeRegistry::Get().Current(&reason);  // OfferArea may have loaded the cell
		}
		if (!home) {
			snap->notHomeReason = reason.empty() ? "not a player home"s : reason;
			publish();
			return;
		}

		snap->inHome = true;
		snap->cellId = home->cellId;
		snap->cellText = home->cell.ToString();
		snap->homeName = home->name;
		snap->enabled = home->layout.has_value();
		snap->layoutBroken = home->layoutBroken;
		if (home->MultiCell() || home->extra) {
			for (const auto& area : home->areas) {
				snap->areas.push_back(AreaRow{ area.cell, area.cell.ToString(), area.name, area.main, area.id == home->cellId });
			}
			if (const auto* here = home->extra ? home->HereArea() : nullptr) {
				snap->hereArea = here->name;
			}
		}
		for (const auto& excluded : home->areasExcluded) {
			snap->areasExcluded.push_back(excluded.ToString());
		}

		const auto* layout = home->layout ? &*home->layout : nullptr;
		std::map<core::FormRef, std::size_t> learnedPerContainer;
		const auto                           display = home::HomeRegistry::Get().Display();  // learned categories, worked out once per change
		if (layout) {
			snap->layoutName = layout->name;
			snap->layoutAuthor = layout->author;
			snap->layoutVersion = layout->version;
			snap->basedOn = layout->basedOn;
			snap->keep = layout->keep;
			if (layout->keep) {
				snap->keepJson = core::KeepConfigToJson(*layout->keep).dump();
			}
			for (const auto& [item, container] : layout->learned) {
				++learnedPerContainer[container];
			}
		}

		auto addRow = [&](const core::FormRef& a_ref, const core::ContainerBinding* a_binding, bool a_master) {
			ContainerRow row;
			row.ref = a_ref;
			row.refText = a_ref.ToString();
			row.master = a_master;
			row.managed = a_master || a_binding != nullptr;
			auto* ref = game::LookupRef(a_ref);
			row.gameName = ref ? game::RefName(ref) : std::string{};
			row.label = a_binding && !a_binding->label.empty() ? a_binding->label : !row.gameName.empty() ? row.gameName : row.refText;
			if (a_binding) {
				row.patterns = a_binding->categories;
				row.ignored = a_binding->ignored;
				row.note = a_binding->note;
				row.explicitForms = a_binding->forms.size();
				for (const auto& p : a_binding->categories) {
					row.patternsText += row.patternsText.empty() ? PatternLabel(p) : ", " + PatternLabel(p);
				}
			}
			if (const auto it = learnedPerContainer.find(a_ref); it != learnedPerContainer.end()) {
				row.learnedCount = it->second;
			}
			if (display) {
				if (const auto it = display->learnedCategories.find(a_ref); it != display->learnedCategories.end()) {
					for (const auto& [id, count] : it->second) {
						row.learnedByCategory[id] = count;
						row.learnedText += (row.learnedText.empty() ? "" : ", ") + CategoryName(id);
					}
				}
			}
			core::Reach reach = core::Reach::Here;
			const auto  verdict = ref ? home::CheckHomeContainer(ref, *home, &reach) :
			                            game::Verdict::Reject(home->MultiCell() ? "not in memory here (in another area and not persistent, or its plugin is missing)" :
			                                                                      "reference not found (plugin missing?)");
			row.available = verdict.ok;
			row.reason = verdict.reason;
			row.warning = verdict.warning;
			if (home->MultiCell() && ref && reach != core::Reach::Here) {
				const auto* area = home::AreaOf(*home, ref);
				row.area = area ? area->name : std::string{};
				row.remote = verdict.ok;
			}
			snap->containers.push_back(std::move(row));
		};

		if (layout) {
			if (layout->master) {
				snap->masterRow = snap->containers.size();
				addRow(*layout->master, nullptr, true);
			}
			for (const auto& c : layout->containers) {
				if (!layout->IsMaster(c.ref)) {
					addRow(c.ref, &c, false);
				}
			}
		}
		const bool focusHere = _focus && _focusCell == home->cellId;
		if (focusHere && std::ranges::none_of(snap->containers, [&](const ContainerRow& r) { return r.ref == *_focus; })) {
			addRow(*_focus, nullptr, false);
		}
		if (focusHere) {
			for (std::size_t i = 0; i < snap->containers.size(); ++i) {
				if (snap->containers[i].ref == *_focus) {
					snap->focusRow = i;
					break;
				}
			}
		}

		if (layout) {
			for (const auto& [item, container] : layout->learned) {
				LearnedRow row;
				row.item = item;
				row.itemText = item.ToString();
				auto*       form = game::LookupForm(item);
				const char* name = form ? form->GetName() : nullptr;
				row.itemName = name && *name ? std::string(name) : (form ? row.itemText : row.itemText + " (not loaded)");
				row.containerText = container.ToString();
				const auto* binding = layout->FindContainer(container);
				row.containerManaged = binding != nullptr;
				if (binding && !binding->label.empty()) {
					row.containerLabel = binding->label;
				} else if (auto* ref = game::LookupRef(container)) {
					row.containerLabel = game::RefName(ref);
				} else {
					row.containerLabel = row.containerText;
				}
				snap->learned.push_back(std::move(row));
			}
			std::ranges::sort(snap->learned, [](const LearnedRow& a, const LearnedRow& b) { return core::ICompare(a.itemName, b.itemName) < 0; });

			core::Diagnostics diags;
			core::ValidateLayoutCategories(*layout, home::Store::Get().Categories(), core::LayoutFileName(home->cell), diags);
			for (const auto& d : diags.Items()) {
				snap->layoutWarnings.push_back(d.ToString());
			}
		}

		// Eligible templates for this cell: best first, then the alternatives (already ranked).
		const auto templates = home::Store::Get().Templates();
		auto       addTemplate = [&](std::size_t a_index, bool a_best) {
			if (a_index >= templates.size()) {
				return;
			}
			const auto&    t = templates[a_index];
			TemplateOption option;
			option.source = t.source;
			option.name = t.layout.name;
			option.author = t.layout.author;
			option.version = t.layout.version;
			option.priority = t.layout.priority;
			option.best = a_best;
			option.applied = layout && layout->basedOn && *layout->basedOn == core::MakeBasedOn(t.source, t.layout.version);
			snap->templates.push_back(std::move(option));
		};
		if (home->resolution.chosen) {
			addTemplate(*home->resolution.chosen, true);
		}
		for (const auto index : home->resolution.alternatives) {
			addTemplate(index, false);
		}

		// Saved setups outside Data (a folder listing): on Full, or when the home changed; otherwise carried over.
		if (a_refresh == Refresh::Full || !previous || previous->cellId != snap->cellId) {
			for (auto& b : home::Store::Get().HomeBackups(home->cell)) {
				// The latest copy is only worth offering when it differs from the setup as it is now (one file read;
				// DESIGN §6.6). Without a setup, or with a broken file, every copy is.
				const bool same = !b.history && layout && !home->layoutBroken && home::Store::Get().HomeBackupMatches(b.path, *layout);
				snap->backups.push_back(BackupOption{ std::move(b.path), std::move(b.date), b.history, same });
			}
		} else {
			snap->backups = previous->backups;
		}

		// Item counts per category (Retrieve). Only recomputed on Full; otherwise carried over.
		if (layout && a_refresh == Refresh::Full) {
			const auto counts = sort::CountByCategory(*home);
			const auto& tree = *snap->categories;
			snap->nodeCounts.assign(tree.nodes.size(), 0);
			for (const auto& [id, n] : counts) {
				snap->totalCount += n;
				if (id.empty()) {
					continue;
				}
				for (std::size_t i = 0; i < tree.nodes.size(); ++i) {
					if (core::CategoryPatternMatches(tree.nodes[i].id + ".*", id)) {
						snap->nodeCounts[i] += n;
					}
				}
			}
			snap->countsValid = true;
		} else if (layout && previous && previous->countsValid && previous->cellId == snap->cellId && previous->categories == snap->categories) {
			snap->nodeCounts = previous->nodeCounts;
			snap->totalCount = previous->totalCount;
			snap->countsValid = true;
		}

		publish();
	}

	void PanelModel::BuildGlobal()
	{
		auto        global = std::make_shared<GlobalSnapshot>();
		const auto& settings = Settings::GetSingleton();
		global->settings = settings.Get();
		global->isVR = REL::Module::IsVR();
		global->quickLootReady = integrations::quickloot::IsReady();
		global->uiExtensions = ui::uiext::IsInstalled();
		global->hotkeyName = Controller::HotkeyName();
		global->globalKeep = home::Store::Get().GlobalKeep();
		global->globalKeepJson = core::KeepConfigToJson(global->globalKeep).dump();

		const auto& store = home::Store::Get();
		global->categoryCount = store.Categories().Size();
		global->categoryDiagnostics = Dedupe(store.CategoryDiagnostics().Items());
		global->templateDiagnostics = Dedupe(store.TemplateDiagnostics().Items());
		global->settingsDiagnostics = Dedupe(store.SettingsDiagnostics().Items());

		const auto& loaded = game::LoadedPlugins();
		for (const auto& t : store.Templates()) {
			TemplateInfo info;
			info.source = t.source;
			info.name = t.layout.name;
			info.author = t.layout.author;
			info.version = t.layout.version;
			info.priority = t.layout.priority;
			if (t.layout.cell) {
				info.cellText = t.layout.cell->ToString();
				auto* form = game::LookupForm(*t.layout.cell);
				auto* cell = form ? form->As<RE::TESObjectCELL>() : nullptr;
				info.cellName = cell ? game::CellName(cell) : std::string{};
			}
			info.required = t.layout.requiredPlugins;
			info.missing = core::MissingRequirements(t.layout, loaded);
			for (const auto& d : global->templateDiagnostics) {
				if (d.source == t.source) {
					info.diagnostics.push_back(d.ToString());
				}
			}
			global->templates.push_back(std::move(info));
		}

		global->version = std::string(version::kString);
		global->runtime = std::format("{} {}", RuntimeName(), REL::Module::get().version().string());
		global->frameworkVersion = menu_framework::GetVersion();

		std::scoped_lock lock(_lock);
		_global = std::move(global);
	}
}
