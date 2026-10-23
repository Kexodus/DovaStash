#include "UI/MessageBoxUI.h"

#include "Controller.h"
#include "Game/FormUtil.h"
#include "Home/Store.h"
#include "Sort/SortEngine.h"
#include "Settings.h"
#include "UI/AreaPrompt.h"
#include "UI/Labels.h"
#include "UI/MessageBox.h"
#include "UI/SaveTemplate.h"
#include "UI/UIExtensions.h"

namespace dova::ui
{
	namespace
	{
		constexpr std::size_t kPerPage = kOptionsPerPage;
		using Option = MenuOption;  // paged by ShowOptions (UI/MessageBox.h)

		const core::ContainerBinding* FindBinding(RE::FormID a_cellId, const core::FormRef& a_container)
		{
			auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
			return home && home->layout ? home->layout->FindContainer(a_container) : nullptr;
		}

		// "Chest", or the reference's id when it is not loaded (menu bodies name the container).
		std::string ContainerName(const core::FormRef& a_container)
		{
			auto* ref = game::LookupRef(a_container);
			return ref ? game::RefName(ref) : a_container.ToString();
		}
	}

	void MessageBoxUI::Notify(std::string_view a_message, bool a_failed)
	{
		logger::info("Notify: {}", a_message);
		(void)a_failed;  // the HUD shows a failure like any other line
		ShowNotification(a_message);
	}

	void MessageBoxUI::Confirm(std::string a_question, std::function<void()> a_onYes, std::function<void()> a_onNo)
	{
		ShowMessageBox(a_question, { "Yes", "No" }, [onYes = std::move(a_onYes), onNo = std::move(a_onNo)](std::size_t a_index) {
			if (a_index == 0 && onYes) {
				onYes();
			} else if (a_index != 0 && onNo) {
				onNo();  // back to the menu the question came from
			}
		});
	}

	void MessageBoxUI::OpenSetupPrompt(home::Home& a_home, RE::TESObjectREFR* a_container)
	{
		const auto          cellId = a_home.cellId;
		std::vector<Option> options;
		std::string         note;  // the template line under the question

		if (a_home.backup) {
			// The player's own setup, kept outside Data (DESIGN §6.6): first, as it is what they had.
			options.push_back({ std::format(kRestoreLastSetup, a_home.backup->date), [cellId]() { Controller::Get().RestoreBackup(cellId); } });
		}
		if (a_container) {
			if (auto ref = game::ToFormRef(a_container)) {
				options.push_back({ std::format(kMakeMasterSetup, game::RefName(a_container)), [cellId, ref = *ref]() {
									   Controller::Get().MakeMaster(cellId, ref);
								   } });
			}
		}
		if (const auto* best = a_home.BestTemplate()) {
			note = std::format("\nTemplate available: {} (by {})", best->layout.name, AuthorOf(best->layout));
			const auto* fit = home::HomeRegistry::Get().BestTemplateFit(a_home);
			const bool  mayNotFit = fit && !fit->fits;
			if (mayNotFit) {
				note += std::format(", {}", kMayNotFit);  // the auto-enable guard (DESIGN §9.1)
			}
			// A template the guard doubts asks first, as under Advanced...; No comes back here.
			options.push_back({ std::format("Use template \"{}\"", best->layout.name),
				[this, cellId, ask = mayNotFit, question = TemplateQuestion(a_home), ref = game::ToFormRef(a_container)]() {
					if (!ask) {
						Controller::Get().UseTemplate(cellId);
						return;
					}
					Confirm(question, [cellId]() { Controller::Get().UseTemplate(cellId); }, [this, cellId, ref]() {
						if (auto* current = home::HomeRegistry::Get().CurrentIf(cellId)) {
							OpenSetupPrompt(*current, ref ? game::LookupRef(*ref) : nullptr);
						}
					});
				} });
		}
		// One question whenever a setup entry is offered; only adding this area (DESIGN §6.7) names the home alone.
		auto body = options.empty() ? std::format("Dova Stash: {}", a_home.name) : std::format("Dova Stash: set up {}?{}", a_home.name, note);
		if (const auto offer = home::HomeRegistry::Get().OfferArea()) {
			// Multi-cell homes: this cell joins the home the player just came from.
			options.push_back({ std::format(kAddArea, offer->homeName), [cellId]() { AddAreaNamed(cellId); } });  // named first when UIExtensions is there
		}
		if (options.empty()) {
			Notify(std::format("Dova Stash: press {} at a container to set up this home", Controller::HotkeyName()));
			return;
		}
		if (_panel) {
			options.push_back({ kOpenPanel, [panel = _panel, cellId, ref = game::ToFormRef(a_container)]() {
								   auto* current = home::HomeRegistry::Get().CurrentIf(cellId);
								   if (current) {
									   panel->OpenSetupPrompt(*current, ref ? game::LookupRef(*ref) : nullptr);
								   }
							   } });
		}
		ShowOptions(std::move(body), std::move(options));
	}

	void MessageBoxUI::OpenMasterMenu(home::Home& a_home, RE::TESObjectREFR* a_master)
	{
		const auto cellId = a_home.cellId;
		auto       body = std::format("Dova Stash: {}", a_home.name);
		if (!a_master && a_home.layout && a_home.layout->master) {
			body += std::format(" ({})", kMasterNotHere);  // in another area of the home, as the list's title says
		}

		std::vector<Option> options;
		options.push_back({ "Deposit", [cellId]() { Controller::Get().RunOperation(cellId, sort::Op::Deposit); } });
		if (sort::Restocks(a_home)) {
			options.push_back({ "Restock", [cellId]() { Controller::Get().RunOperation(cellId, sort::Op::Restock); } });
		}
		options.push_back({ "Sort all", [cellId]() { Controller::Get().RunOperation(cellId, sort::Op::SortAll); } });
		// Before the optional entries, so Retrieve and Browse stay on the first page (5 per page). Save as template...
		// and Reset home... are under it, as in the lists (ListUI).
		options.push_back({ "Advanced...", [this, cellId]() { OpenAdvancedMenu(cellId); } });
		if (a_home.layout && a_home.layout->HasSellChest()) {
			for (auto& [text, action] : SellEntries(a_home, std::nullopt, [this, cellId]() { ReopenMasterMenu(cellId); })) {
				options.push_back({ std::move(text), std::move(action) });
			}
		}
		// Names go through UIExtensions' text box, as in the lists: only when it is installed.
		const bool textBox = uiext::IsInstalled();
		if (const auto masterRef = a_master ? game::ToFormRef(a_master) : std::nullopt; masterRef && textBox) {
			options.push_back({ std::format("Rename {}...", game::RefName(a_master)), [this, cellId, ref = *masterRef]() {
								   if (!RenameContainerNamed(cellId, ref)) {
									   Notify("Dova Stash: close the open menu first", true);
								   }
							   } });
			if (HasCustomName(a_master)) {
				options.push_back({ "Restore original name", [cellId, ref = *masterRef]() { Controller::Get().SetLabel(cellId, ref, {}); } });
			}
		}
		if (const auto* area = a_home.extra ? a_home.HereArea() : nullptr) {
			// In an extra cell of a multi-cell home (DESIGN §6.7).
			if (textBox) {
				options.push_back({ std::format(kRenameArea, area->name), [this, cellId, cell = area->cell, name = area->name]() {
									   if (!RenameAreaNamed(cellId, cell, name)) {
										   Notify("Dova Stash: close the open menu first", true);
									   }
								   } });
			}
			options.push_back({ std::format(kRemoveArea, a_home.name), [this, cellId, cell = area->cell, question = std::format(kConfirmRemoveArea, area->name, a_home.name)]() {
								   Confirm(question, [cellId, cell]() { Controller::Get().RemoveArea(cellId, cell); }, [this, cellId]() { ReopenMasterMenu(cellId); });
							   } });
		}
		ShowOptions(std::move(body), std::move(options));
	}

	void MessageBoxUI::OpenAdvancedMenu(RE::FormID a_cellId)
	{
		auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		// Everyday actions first, the one that asks last.
		std::vector<Option> options;
		options.push_back({ "Retrieve...", [this, a_cellId]() { OpenRetrieveMenu(a_cellId); } });
		options.push_back({ "Browse containers...", [this, a_cellId]() { OpenBrowseMenu(a_cellId); } });
		if (home->BestTemplate()) {
			// Asks only when it would replace changes of the player's own, or may not fit (as the panel does).
			options.push_back({ TemplateEntry(*home), [this, a_cellId, ask = AskBeforeTemplate(*home), question = TemplateQuestion(*home)]() {
								   if (ask) {
									   Confirm(question, [a_cellId]() { Controller::Get().UseTemplate(a_cellId); }, [this, a_cellId]() { OpenAdvancedMenu(a_cellId); });
								   } else {
									   Controller::Get().UseTemplate(a_cellId);
								   }
							   } });
		}
		options.push_back({ kSaveAsTemplate, [this, a_cellId]() { SaveAsTemplate(a_cellId, [this, a_cellId]() { OpenAdvancedMenu(a_cellId); }); } });
		options.push_back({ "Reset home...", [this, a_cellId, name = home->name]() {
							   Confirm(std::format(kConfirmResetHome, name), [a_cellId]() { Controller::Get().ResetHome(a_cellId); }, [this, a_cellId]() { OpenAdvancedMenu(a_cellId); });
						   } });
		if (_panel) {
			// What Advanced... opened before these moved here: the panel's This home page.
			options.push_back({ kOpenPanel, [panel = _panel, a_cellId]() {
								   auto* current = home::HomeRegistry::Get().CurrentIf(a_cellId);
								   if (!current) {
									   return;
								   }
								   RE::TESObjectREFR* master = nullptr;
								   if (current->layout && current->layout->master) {
									   master = game::LookupRef(*current->layout->master);
								   }
								   panel->OpenMasterMenu(*current, master);
							   } });
		}
		ShowOptions(std::format("Dova Stash: {}: Advanced", home->name), std::move(options), [this, a_cellId]() { ReopenMasterMenu(a_cellId); });
	}

	void MessageBoxUI::ReopenMasterMenu(RE::FormID a_cellId)
	{
		auto* current = home::HomeRegistry::Get().CurrentIf(a_cellId);
		if (!current) {
			return;
		}
		RE::TESObjectREFR* master = nullptr;
		if (current->layout && current->layout->master) {
			master = game::LookupRef(*current->layout->master);
		}
		OpenMasterMenu(*current, master);
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
		ShowOptions("Dova Stash: Retrieve", std::move(options), [this, a_cellId]() { OpenAdvancedMenu(a_cellId); });
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
			options.push_back({ BrowseEntryText(ref, a_what), [a_cellId, a_ref]() { Controller::Get().Browse(a_cellId, a_ref); } });
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
		ShowOptions("Dova Stash: Browse containers", std::move(options), [this, a_cellId]() { OpenAdvancedMenu(a_cellId); });
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
		ShowOptions(std::format("Dova Stash: Retrieve: {}", CategoryName(a_root)), std::move(options),
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

		const bool  sellChest = a_home.layout->IsSellChest(*ref);
		std::string state;
		if (!binding) {
			state = kNotManaged;  // as the list's Categories... entry says
			state[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(state[0])));
			state += " (put items in by hand to teach it, or assign categories)";
		} else if (sellChest) {
			state = RoleSummary(DescribeContainer(a_container), 3);  // "Sell: Clutter"
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

		ContainerMenuHooks hooks;
		hooks.categoriesText = kCategoriesEntry;
		hooks.openCategories = [this, cellId, ref = *ref]() { OpenAssignRoots(cellId, ref); };
		if (uiext::IsInstalled()) {  // the text box, as in the lists
			hooks.rename = [this, cellId, ref = *ref]() {
				if (!RenameContainerNamed(cellId, ref)) {
					Notify("Dova Stash: close the open menu first", true);
				}
			};
		}
		hooks.openLearned = [this, cellId, ref = *ref]() { OpenLearnedMenu(cellId, ref); };
		if (_panel) {
			hooks.openPanel = [panel = _panel, cellId, ref = *ref]() {
				auto* current = home::HomeRegistry::Get().CurrentIf(cellId);
				auto* object = game::LookupRef(ref);
				if (current && object) {
					panel->OpenContainerMenu(*current, object);
				}
			};
		}
		hooks.confirm = [this, cellId, ref = *ref](std::string a_question, std::function<void()> a_onYes) {
			Confirm(std::move(a_question), std::move(a_onYes), [this, cellId, ref]() { ReopenContainerMenu(cellId, ref); });
		};
		hooks.back = [this, cellId, ref = *ref]() { ReopenContainerMenu(cellId, ref); };
		std::vector<Option> options;
		for (auto& entry : ContainerMenuEntries(a_home, a_container, hooks)) {
			options.push_back({ std::move(entry.text), std::move(entry.action) });
		}
		ShowOptions(std::move(body), std::move(options));
	}

	void MessageBoxUI::OpenLearnedMenu(RE::FormID a_cellId, const core::FormRef& a_container, std::size_t a_page)
	{
		auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		// Learning: putting an item into a managed container by hand makes that container its destination
		// from then on. Listed here so it can be undone (as in the lists, ListUI::OpenLearned).
		std::vector<Option> options;
		std::size_t         count = 0;
		for (const auto& [item, container] : home->layout->learned) {
			if (!(container == a_container)) {
				continue;
			}
			++count;
			auto*       form = game::LookupForm(item);
			std::string name = form && form->GetName() && *form->GetName() ? form->GetName() : item.ToString();
			const auto  page = count / kPerPage;  // the Forget all entry comes first
			options.push_back({ std::format("Forget {}", name), [this, a_cellId, a_container, item, page]() {
								   Controller::Get().RemoveLearned(a_cellId, item);
								   OpenLearnedMenu(a_cellId, a_container, page);
							   } });
		}
		if (count == 0) {
			ReopenContainerMenu(a_cellId, a_container);
			return;
		}
		auto*      ref = game::LookupRef(a_container);
		const auto name = ref ? game::RefName(ref) : a_container.ToString();
		options.insert(options.begin(), Option{ ForgetAllEntry(count),[this, a_cellId, a_container, count, name]() {
												   Confirm(ForgetLearnedQuestion(count, name), [this, a_cellId, a_container]() {
													   Controller::Get().ForgetLearned(a_cellId, a_container);
													   ReopenContainerMenu(a_cellId, a_container);
												   }, [this, a_cellId, a_container]() { OpenLearnedMenu(a_cellId, a_container); });
											   } });
		ShowOptions(std::format("Dova Stash: {}: Learned items", name), std::move(options),
			[this, a_cellId, a_container]() { ReopenContainerMenu(a_cellId, a_container); }, a_page);
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
			if (HasPattern(binding, all) || HasPattern(binding, "*") || (children.empty() && Covers(binding, root))) {
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
					[this, a_cellId, a_container, root, page]() { OpenAssignChildren(a_cellId, a_container, root, page); } });
			}
		}
		if (options.empty()) {
			Notify("Dova Stash: no categories are loaded (see DovaStash.log)", true);
			return;
		}
		ShowOptions(std::format("Dova Stash: {}: Categories", ContainerName(a_container)), std::move(options),
			[this, a_cellId, a_container]() { ReopenContainerMenu(a_cellId, a_container); }, a_page);
	}

	void MessageBoxUI::OpenAssignChildren(RE::FormID a_cellId, const core::FormRef& a_container, const std::string& a_root, std::size_t a_rootPage, std::size_t a_page)
	{
		const auto* binding = FindBinding(a_cellId, a_container);
		const auto  learned = LearnedOf(a_cellId, a_container);

		std::vector<Option> options;
		const auto          all = a_root + ".*";
		const auto          allMark = HasPattern(binding, all) ? "[x]" : Covers(binding, a_root) ? "[+]" : "[ ]";
		options.push_back({ std::format("{} All {}", allMark, CategoryName(a_root)),
			[this, a_cellId, a_container, a_root, a_rootPage, all]() {
				Controller::Get().ToggleCategory(a_cellId, a_container, all);
				OpenAssignChildren(a_cellId, a_container, a_root, a_rootPage, 0);
			} });

		const auto children = ChildCategories(a_root);
		for (std::size_t i = 0; i < children.size(); ++i) {
			const auto& id = children[i];
			const auto  page = (i + 1) / kPerPage;
			// [x] bound exactly, [+] covered by a wider pattern, [ ] not routed here.
			const auto mark = HasPattern(binding, id) ? "[x]" : Covers(binding, id) ? "[+]" : "[ ]";
			options.push_back({ std::format("{} {}{}", mark, CategoryName(id), LearnedSuffix(learned, id + ".*")), [this, a_cellId, a_container, a_root, a_rootPage, id, page]() {
								   Controller::Get().ToggleCategory(a_cellId, a_container, id);
								   OpenAssignChildren(a_cellId, a_container, a_root, a_rootPage, page);
							   } });
		}
		ShowOptions(std::format("Dova Stash: {}: {}", ContainerName(a_container), CategoryName(a_root)), std::move(options),
			[this, a_cellId, a_container, a_rootPage]() { OpenAssignRoots(a_cellId, a_container, a_rootPage); }, a_page);
	}
}
