#include "UI/MessageBoxUI.h"

#include "Controller.h"
#include "Game/FormUtil.h"
#include "Home/Store.h"
#include "Sort/SortEngine.h"
#include "Settings.h"
#include "UI/Labels.h"
#include "UI/MessageBox.h"
#include "UI/SaveTemplate.h"

namespace dova::ui
{
	namespace
	{
		constexpr std::size_t kPerPage = 5;  // + More / Back / Cancel = at most 8 buttons

		struct Option
		{
			std::string           label;
			std::function<void()> action;
		};

		// Shows options[page*kPerPage ...] with More/Back/Cancel. `a_onBack` (optional) is offered on the
		// first page; later pages go back to the previous page.
		void ShowPaged(std::string a_body, std::shared_ptr<const std::vector<Option>> a_options, std::function<void()> a_onBack, std::size_t a_page)
		{
			const auto total = a_options->size();
			const auto pages = std::max<std::size_t>(1, (total + kPerPage - 1) / kPerPage);
			a_page = std::min(a_page, pages - 1);
			const auto first = a_page * kPerPage;
			const auto last = std::min(first + kPerPage, total);

			std::vector<std::string>           labels;
			std::vector<std::function<void()>> actions;
			for (auto i = first; i < last; ++i) {
				labels.push_back((*a_options)[i].label);
				actions.push_back((*a_options)[i].action);
			}
			if (last < total) {
				labels.emplace_back("More...");
				actions.emplace_back([=]() { ShowPaged(a_body, a_options, a_onBack, a_page + 1); });
			}
			if (a_page > 0) {
				labels.emplace_back("Back");
				actions.emplace_back([=]() { ShowPaged(a_body, a_options, a_onBack, a_page - 1); });
			} else if (a_onBack) {
				labels.emplace_back("Back");
				actions.emplace_back(a_onBack);
			}
			labels.emplace_back("Cancel");
			actions.emplace_back(nullptr);

			auto body = a_body;
			if (pages > 1) {
				body += std::format("\n(page {}/{})", a_page + 1, pages);
			}
			ShowMessageBox(body, labels, [actions = std::move(actions)](std::size_t a_index) {
				if (a_index < actions.size() && actions[a_index]) {
					actions[a_index]();
				}
			});
		}

		void ShowOptions(std::string a_body, std::vector<Option> a_options, std::function<void()> a_onBack = {}, std::size_t a_page = 0)
		{
			ShowPaged(std::move(a_body), std::make_shared<const std::vector<Option>>(std::move(a_options)), std::move(a_onBack), a_page);
		}

		const core::ContainerBinding* FindBinding(RE::FormID a_cellId, const core::FormRef& a_container)
		{
			auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
			return home && home->layout ? home->layout->FindContainer(a_container) : nullptr;
		}
	}

	void MessageBoxUI::Notify(std::string_view a_message)
	{
		logger::info("Notify: {}", a_message);
		ShowNotification(a_message);
	}

	void MessageBoxUI::Confirm(std::string a_question, std::function<void()> a_onYes)
	{
		ShowMessageBox(a_question, { "Yes", "No" }, [onYes = std::move(a_onYes)](std::size_t a_index) {
			if (a_index == 0 && onYes) {
				onYes();
			}
		});
	}

	void MessageBoxUI::OpenSetupPrompt(home::Home& a_home, RE::TESObjectREFR* a_container)
	{
		const auto          cellId = a_home.cellId;
		std::vector<Option> options;
		std::string         body;

		if (a_home.backup) {
			// The player's own setup, kept outside Data (DESIGN §6.6): first, as it is what they had.
			body = std::format("Dova Stash: set up {}?", a_home.name);
			options.push_back({ std::format(kRestoreLastSetup, a_home.backup->date), [cellId]() { Controller::Get().RestoreBackup(cellId); } });
		}
		if (a_container) {
			if (auto ref = game::ToFormRef(a_container)) {
				body = std::format("Dova Stash: set up {}?", a_home.name);
				options.push_back({ std::format("Make {} the master", game::RefName(a_container)), [cellId, ref = *ref]() {
									   Controller::Get().MakeMaster(cellId, ref);
								   } });
			}
		} else {
			body = std::format("Dova Stash: {}", a_home.name);
		}
		if (const auto* best = a_home.BestTemplate()) {
			if (body.empty()) {
				body = std::format("Dova Stash: set up {}?", a_home.name);
			}
			body += std::format("\nTemplate available: {} (by {})", best->layout.name, best->layout.author);
			options.push_back({ std::format("Use template \"{}\"", best->layout.name), [cellId]() { Controller::Get().UseTemplate(cellId); } });
		}
		if (const auto offer = home::HomeRegistry::Get().OfferArea()) {
			// Multi-cell homes (DESIGN §6.7): this cell joins the home the player just came from.
			if (body.empty()) {
				body = std::format("Dova Stash: {}", a_home.name);
			}
			options.push_back({ std::format(kAddArea, offer->homeName), [cellId]() { Controller::Get().AddArea(cellId); } });
		}
		if (options.empty()) {
			Notify(std::format("Dova Stash: look at a container and press {} to set up this home", Controller::HotkeyName()));
			return;
		}
		ShowOptions(std::move(body), std::move(options));
	}

	void MessageBoxUI::OpenMasterMenu(home::Home& a_home, RE::TESObjectREFR* a_master)
	{
		const auto cellId = a_home.cellId;
		auto       body = std::format("Dova Stash: {}", a_home.name);
		if (!a_master) {
			body += "\n(the master container is not available here)";
		}

		std::vector<Option> options;
		options.push_back({ "Deposit", [cellId]() { Controller::Get().RunOperation(cellId, sort::Op::Deposit); } });
		options.push_back({ "Sort all", [cellId]() { Controller::Get().RunOperation(cellId, sort::Op::SortAll); } });
		options.push_back({ "Retrieve...", [this, cellId]() { OpenRetrieveMenu(cellId); } });
		options.push_back({ "Browse containers...", [this, cellId]() { OpenBrowseMenu(cellId); } });
		if (const auto* best = a_home.BestTemplate()) {
			const bool basedOnTemplate = a_home.layout && a_home.layout->basedOn && a_home.layout->basedOn->starts_with(best->source + "@");
			const auto name = best->layout.name;
			options.push_back({ basedOnTemplate ? std::format("Re-apply template \"{}\"", name) : std::format("Use template \"{}\"", name),
				[this, cellId, name]() {
					Confirm(std::format("Use the template \"{}\"?\nYour learned destinations, keep rules and ignored containers are kept; the master and category bindings are replaced by the template's. A backup is saved; no items are moved.", name),
						[cellId]() { Controller::Get().UseTemplate(cellId); });
				} });
		}
		options.push_back({ kSaveAsTemplate, [cellId]() { SaveAsTemplate(cellId); } });
		if (const auto* area = a_home.extra ? a_home.HereArea() : nullptr) {
			// In an extra cell of a multi-cell home (DESIGN §6.7).
			options.push_back({ std::format(kRemoveArea, a_home.name), [this, cellId, cell = area->cell, question = std::format(kConfirmRemoveArea, area->name, a_home.name)]() {
								   Confirm(question, [cellId, cell]() { Controller::Get().RemoveArea(cellId, cell); });
							   } });
		}
		options.push_back({ "Reset home...", [this, cellId, name = a_home.name]() {
							   Confirm(std::format("Reset Dova Stash for {}?\nYour setup is backed up (.bak); no items are moved.", name),
								   [cellId]() { Controller::Get().ResetHome(cellId); });
						   } });
		ShowOptions(std::move(body), std::move(options));
	}

	void MessageBoxUI::OpenRetrieveMenu(RE::FormID a_cellId)
	{
		auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		const auto counts = sort::CountByCategory(*home);
		std::int32_t total = 0;
		for (const auto& [id, n] : counts) {
			total += n;
		}
		if (total == 0) {
			Notify("Dova Stash: nothing to retrieve");
			return;
		}

		std::vector<Option> options;
		options.push_back({ std::format("Everything ({})", total), [a_cellId]() { Controller::Get().RunOperation(a_cellId, sort::Op::Retrieve, "*"); } });
		for (const auto& root : RootCategories()) {
			const auto pattern = root + ".*";
			const auto n = CountMatching(counts, pattern);
			if (n == 0) {
				continue;
			}
			const auto children = ChildCategories(root);
			const auto populated = std::ranges::count_if(children, [&](const std::string& id) { return CountMatching(counts, id + ".*") > 0; });
			if (populated <= 1) {
				options.push_back({ std::format("{} ({})", CategoryName(root), n),
					[a_cellId, pattern]() { Controller::Get().RunOperation(a_cellId, sort::Op::Retrieve, pattern); } });
			} else {
				options.push_back({ std::format("{}... ({})", CategoryName(root), n), [this, a_cellId, root]() { OpenRetrieveRoot(a_cellId, root); } });
			}
		}
		ShowOptions("Dova Stash: retrieve what?", std::move(options));
	}

	void MessageBoxUI::OpenBrowseMenu(RE::FormID a_cellId)
	{
		auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		std::vector<Option> options;
		auto                add = [&](const core::FormRef& a_ref, const std::string& a_what) {
			auto* ref = game::LookupRef(a_ref);
			if (!ref || ref->IsDisabled()) {
				return;
			}
			options.push_back({ std::format("{} ({})", game::RefName(ref), a_what), [a_cellId, a_ref]() { Controller::Get().Browse(a_cellId, a_ref); } });
		};
		if (home->layout->master) {
			add(*home->layout->master, "master");
		}
		for (const auto& c : home->layout->containers) {
			if (!c.ignored) {
				const auto summary = RoleSummary(DescribeContainer(game::LookupRef(c.ref)), 2);
				add(c.ref, summary.empty() ? c.label : summary);
			}
		}
		if (options.empty()) {
			Notify("Dova Stash: no containers to open");
			return;
		}
		ShowOptions("Dova Stash: open which container?", std::move(options));
	}

	void MessageBoxUI::OpenRetrieveRoot(RE::FormID a_cellId, const std::string& a_root)
	{
		auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		const auto counts = sort::CountByCategory(*home);

		std::vector<Option> options;
		const auto          all = a_root + ".*";
		options.push_back({ std::format("All {} ({})", CategoryName(a_root), CountMatching(counts, all)),
			[a_cellId, all]() { Controller::Get().RunOperation(a_cellId, sort::Op::Retrieve, all); } });
		for (const auto& id : ChildCategories(a_root)) {
			const auto pattern = id + ".*";
			const auto n = CountMatching(counts, pattern);
			if (n > 0) {
				options.push_back({ std::format("{} ({})", CategoryName(id), n),
					[a_cellId, pattern]() { Controller::Get().RunOperation(a_cellId, sort::Op::Retrieve, pattern); } });
			}
		}
		ShowOptions(std::format("Dova Stash: retrieve {}", CategoryName(a_root)), std::move(options),
			[this, a_cellId]() { OpenRetrieveMenu(a_cellId); });
	}

	void MessageBoxUI::OpenContainerMenu(home::Home& a_home, RE::TESObjectREFR* a_container)
	{
		const auto ref = game::ToFormRef(a_container);
		if (!ref || !a_home.layout) {
			return;
		}
		const auto  cellId = a_home.cellId;
		const auto* binding = a_home.layout->FindContainer(*ref);

		std::string state;
		if (!binding) {
			state = "Not managed yet (put items in by hand to teach it, or assign categories)";
		} else if (binding->ignored) {
			state = "Ignored by Dova Stash";
		} else if (binding->categories.empty()) {
			const auto summary = RoleSummary(DescribeContainer(a_container), 3);
			state = summary.empty() ? "No categories" : summary;  // "Books, learned"
		} else {
			for (const auto& p : binding->categories) {
				state += state.empty() ? PatternLabel(p) : ", " + PatternLabel(p);
			}
		}
		auto body = std::format("Dova Stash: {}\n{}", game::RefName(a_container), state);

		std::vector<Option> options;
		options.push_back({ "Assign categories...", [this, cellId, ref = *ref]() { OpenAssignRoots(cellId, ref); } });
		options.push_back({ std::format("Open {}", game::RefName(a_container)), [cellId, ref = *ref]() { Controller::Get().Browse(cellId, ref); } });
		if (binding && !binding->categories.empty()) {
			options.push_back({ "Clear categories", [cellId, ref = *ref]() { Controller::Get().ClearCategories(cellId, ref); } });
		}
		const bool ignored = binding && binding->ignored;
		options.push_back({ ignored ? "Stop ignoring" : "Ignore", [cellId, ref = *ref, ignored]() { Controller::Get().SetIgnored(cellId, ref, !ignored); } });
		const bool learned = std::ranges::any_of(a_home.layout->learned, [&](const auto& a_pair) { return a_pair.second == *ref; });
		if (binding || learned) {
			options.push_back({ "Remove...", [this, cellId, ref = *ref, name = game::RefName(a_container)]() {
								   Confirm(std::format("Remove {} from Dova Stash?\n{}", name, kRemoveExplained), [cellId, ref]() { Controller::Get().RemoveContainer(cellId, ref); });
							   } });
		}
		options.push_back({ "Make master", [cellId, ref = *ref]() { Controller::Get().MakeMaster(cellId, ref); } });
		ShowOptions(std::move(body), std::move(options));
	}

	void MessageBoxUI::ReopenContainerMenu(RE::FormID a_cellId, const core::FormRef& a_container)
	{
		auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
		auto* ref = game::LookupRef(a_container);
		if (home && ref) {
			OpenContainerMenu(*home, ref);
		}
	}

	void MessageBoxUI::OpenAssignRoots(RE::FormID a_cellId, const core::FormRef& a_container, std::size_t a_page)
	{
		const auto* binding = FindBinding(a_cellId, a_container);
		const auto  learned = LearnedOf(a_cellId, a_container);

		std::vector<Option> options;
		const auto          roots = RootCategories();
		for (std::size_t i = 0; i < roots.size(); ++i) {
			const auto& root = roots[i];
			const auto  children = ChildCategories(root);
			const auto  all = root + ".*";
			const auto  page = i / kPerPage;

			std::string mark = "[ ]";
			if (HasPattern(binding, all) || (children.empty() && Covers(binding, root))) {
				mark = "[x]";
			} else if (Covers(binding, root) || std::ranges::any_of(children, [&](const std::string& id) { return Covers(binding, id); })) {
				mark = "[~]";
			}

			if (children.empty()) {
				options.push_back({ std::format("{} {}{}", mark, CategoryName(root), LearnedSuffix(learned, all)), [this, a_cellId, a_container, all, page]() {
									   Controller::Get().ToggleCategory(a_cellId, a_container, all);
									   OpenAssignRoots(a_cellId, a_container, page);
								   } });
			} else {
				options.push_back({ std::format("{} {}...{}", mark, CategoryName(root), LearnedSuffix(learned, all)),
					[this, a_cellId, a_container, root]() { OpenAssignChildren(a_cellId, a_container, root); } });
			}
		}
		if (options.empty()) {
			Notify("Dova Stash: no categories are loaded (see the log)");
			return;
		}
		ShowOptions("Dova Stash: categories for this container", std::move(options),
			[this, a_cellId, a_container]() { ReopenContainerMenu(a_cellId, a_container); }, a_page);
	}

	void MessageBoxUI::OpenAssignChildren(RE::FormID a_cellId, const core::FormRef& a_container, const std::string& a_root, std::size_t a_page)
	{
		const auto* binding = FindBinding(a_cellId, a_container);
		const auto  learned = LearnedOf(a_cellId, a_container);

		std::vector<Option> options;
		const auto          all = a_root + ".*";
		options.push_back({ std::format("{} All {}", HasPattern(binding, all) ? "[x]" : "[ ]", CategoryName(a_root)),
			[this, a_cellId, a_container, a_root, all]() {
				Controller::Get().ToggleCategory(a_cellId, a_container, all);
				OpenAssignChildren(a_cellId, a_container, a_root, 0);
			} });

		const auto children = ChildCategories(a_root);
		for (std::size_t i = 0; i < children.size(); ++i) {
			const auto& id = children[i];
			const auto  page = (i + 1) / kPerPage;
			// [x] bound exactly, [+] covered by a wider pattern, [ ] not routed here.
			const auto mark = HasPattern(binding, id) ? "[x]" : Covers(binding, id) ? "[+]" : "[ ]";
			options.push_back({ std::format("{} {}{}", mark, CategoryName(id), LearnedSuffix(learned, id + ".*")), [this, a_cellId, a_container, a_root, id, page]() {
								   Controller::Get().ToggleCategory(a_cellId, a_container, id);
								   OpenAssignChildren(a_cellId, a_container, a_root, page);
							   } });
		}
		ShowOptions(std::format("Dova Stash: {} for this container", CategoryName(a_root)), std::move(options),
			[this, a_cellId, a_container]() { OpenAssignRoots(a_cellId, a_container); }, a_page);
	}
}
