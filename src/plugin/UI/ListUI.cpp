#include "UI/ListUI.h"

#include "Controller.h"
#include "Game/FormUtil.h"
#include "Icons/IconService.h"
#include "Sort/SortEngine.h"
#include "UI/AreaPrompt.h"
#include "UI/ArmorSetMenu.h"
#include "UI/DisplayMenu.h"
#include "UI/Labels.h"
#include "UI/MessageBox.h"
#include "UI/SaveTemplate.h"
#include "UI/UIExtensions.h"

namespace dova::ui
{
	namespace
	{
		struct Item
		{
			std::string           text;
			std::function<void()> action;  // nullptr = does nothing when chosen
			core::IconRef         icon;    // left of the text when the list menu allows it (Icons/IconService)
			bool                  toggle{ false };  // flips something this list shows: it stays open and updates (List::rebuild)
		};

		struct List
		{
			std::string           title;  // the header: which menu this is
			std::vector<Item>     items;
			std::function<void()> onBack;
			// Lists with toggles: the same list built again from the current state. Set by Rebuildable.
			std::function<std::shared_ptr<const List>()> rebuild;
		};

		// `a_build()`, with `rebuild` building it again. nullptr when there is nothing to show.
		std::shared_ptr<const List> Rebuildable(std::function<std::shared_ptr<List>()> a_build)
		{
			auto list = a_build();
			if (list) {
				list->rebuild = [a_build]() { return Rebuildable(a_build); };
			}
			return list;
		}

		std::size_t FirstItem(const List& a_list)
		{
			return a_list.onBack ? 2 : 1;  // after the header and "< Back"
		}

		// The header ("-- Whitepeak Tower --"; UIListMenu has no unselectable rows, so choosing it does
		// nothing), "< Back" when there is an `onBack`, then the items.
		std::vector<uiext::ListEntry> Entries(const List& a_list)
		{
			std::vector<uiext::ListEntry> entries;
			entries.reserve(a_list.items.size() + FirstItem(a_list));
			entries.push_back({ .text = std::format("-- {} --", a_list.title.empty() ? "Dova Stash"s : a_list.title), .stayOpen = true });
			if (a_list.onBack) {
				entries.push_back({ .text = "< Back" });
			}
			for (const auto& item : a_list.items) {
				entries.push_back({ .text = item.text, .icon = item.icon, .stayOpen = item.toggle && a_list.rebuild != nullptr });
			}
			return entries;
		}

		bool Show(std::shared_ptr<const List> a_list, std::int32_t a_select = -1);

		// The items of `a_all` from `a_start` on, as one page of at most uiext::kMaxListEntries entries: when the rest
		// does not fit, the page ends with "More..." (the next page, whose "< Back" comes back to this one).
		std::shared_ptr<const List> PageOf(const std::shared_ptr<const List>& a_all, std::size_t a_start, std::function<void()> a_onBack)
		{
			auto       page = std::make_shared<List>(List{ a_all->title, {}, std::move(a_onBack) });
			const auto room = uiext::kMaxListEntries - FirstItem(*page);
			const auto begin = a_all->items.begin() + static_cast<std::ptrdiff_t>(std::min(a_start, a_all->items.size()));
			if (static_cast<std::size_t>(a_all->items.end() - begin) <= room) {
				page->items.assign(begin, a_all->items.end());
				return page;
			}
			const auto shown = room - 1;
			page->items.assign(begin, begin + static_cast<std::ptrdiff_t>(shown));
			page->items.push_back({ "More...", [a_all, a_start, shown, back = page->onBack]() {
									   const auto again = [a_all, a_start, back]() { Show(PageOf(a_all, a_start, back)); };
									   Show(PageOf(a_all, a_start + shown, again));
								   } });
			return page;
		}

		// Shows `a_list` (see Entries), with the Dova Stash logo above it and entry `a_select` selected
		// (-1: the menu's default). "< Back" and cancelling run `onBack`. Toggles and the header keep
		// the menu open and update it in place; when the menu cannot do that (its SWF lacks what
		// uiext needs) choosing them closes it like any entry, and the list reopens on the same entry.
		// A list longer than UIListMenu takes is paged (PageOf; lists with toggles stay short).
		// Returns false if the list could not open.
		bool Show(std::shared_ptr<const List> a_list, std::int32_t a_select)
		{
			if (!a_list->rebuild && FirstItem(*a_list) + a_list->items.size() > uiext::kMaxListEntries) {
				logger::debug("List: \"{}\" has {} items; shown in pages", a_list->title, a_list->items.size());
				return Show(PageOf(a_list, 0, a_list->onBack), a_select);
			}
			// The list on screen: a toggle replaces it with the rebuilt one, while the menu stays open.
			auto               current = std::make_shared<std::shared_ptr<const List>>(a_list);
			uiext::ListOptions options;
			options.logo = true;
			options.select = a_select;
			options.onStay = [current](std::int32_t a_index) {
				const auto list = *current;
				const auto first = FirstItem(*list);
				if (a_index >= static_cast<std::int32_t>(first) && static_cast<std::size_t>(a_index) < first + list->items.size()) {
					const auto& item = list->items[static_cast<std::size_t>(a_index) - first];
					logger::info("List: toggled \"{}\"", item.text);
					if (item.action) {
						item.action();
					}
					if (auto rebuilt = list->rebuild ? list->rebuild() : nullptr) {
						*current = std::move(rebuilt);
					}
				}
				return Entries(**current);
			};
			return uiext::OpenList(
				Entries(*a_list),
				[current](std::int32_t a_index) {
					const auto list = *current;
					const auto first = FirstItem(*list);
					if (a_index == uiext::kNotShown) {
						// The menu never came up (UIExtensions logged why): say so, never a silent nothing. Not a Back.
						constexpr auto kMessage = "Dova Stash: the list menu did not open (see DovaStash.log)"sv;
						logger::info("Notify: {}", kMessage);
						ShowNotification(kMessage);
						return;
					}
					if (a_index == 0) {
						logger::info("List: chose the header \"{}\"", list->title);
						if (!Show(list, 0)) {
							logger::info("List: \"{}\" could not reopen", list->title);
						}
						return;
					}
					const bool back = list->onBack && a_index == 1;
					if (back || a_index < 0 || static_cast<std::size_t>(a_index) >= first + list->items.size()) {
						logger::info("List: {}", back ? "back" : list->onBack ? "cancelled (back)" : "cancelled");
						if (list->onBack) {
							list->onBack();
						}
						return;
					}
					const auto& item = list->items[static_cast<std::size_t>(a_index) - first];
					logger::info("List: chose \"{}\"", item.text);
					if (item.action) {
						item.action();
					}
					if (item.toggle && list->rebuild) {
						if (auto rebuilt = list->rebuild(); !rebuilt || !Show(std::move(rebuilt), a_index)) {
							logger::info("List: \"{}\" could not reopen", list->title);
						}
					}
				},
				std::move(options));
		}

		// Shows a flat list headed by `a_title` (see Show).
		bool ShowList(std::string a_title, std::vector<Item> a_items, std::function<void()> a_onBack = {})
		{
			return Show(std::make_shared<List>(List{ std::move(a_title), std::move(a_items), std::move(a_onBack) }));
		}

		// "Chest", or the reference's id when it is not loaded.
		std::string NameOf(const core::FormRef& a_ref)
		{
			auto* ref = game::LookupRef(a_ref);
			return ref ? game::RefName(ref) : a_ref.ToString();
		}

		// Yes runs `a_onYes`; No runs `a_onNo` (the menu the question came from, so the player is not left with none).
		void Confirm(std::string a_question, std::function<void()> a_onYes, std::function<void()> a_onNo = {})
		{
			ShowMessageBox(a_question, { "Yes", "No" }, [onYes = std::move(a_onYes), onNo = std::move(a_onNo)](std::size_t a_index) {
				if (a_index == 0 && onYes) {
					onYes();
				} else if (a_index != 0 && onNo) {
					onNo();
				}
			});
		}

		const core::ContainerBinding* FindBinding(RE::FormID a_cellId, const core::FormRef& a_container)
		{
			auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
			return home && home->layout ? home->layout->FindContainer(a_container) : nullptr;
		}

		void ListUnavailable(IStashUI& a_ui)
		{
			a_ui.Notify("Dova Stash: close the open menu first", true);
		}

		// A list an earlier press asked for has not closed yet (a quick second press while it opens): nothing,
		// rather than a message box on top of it.
		bool ListOpening()
		{
			if (!uiext::IsBusy()) {
				return false;
			}
			logger::debug("Hotkey: a list is already opening");
			return true;
		}
	}

	ListUI::ListUI(std::shared_ptr<IStashUI> a_editor, std::shared_ptr<IStashUI> a_fallback) :
		_editor(std::move(a_editor)),
		_fallback(std::move(a_fallback)),
		_editorIsPanel(_editor && _editor != _fallback)
	{}

	void ListUI::Notify(std::string_view a_message, bool a_failed)
	{
		logger::info("Notify: {}", a_message);
		(void)a_failed;  // the HUD shows a failure like any other line
		ShowNotification(a_message);
	}

	void ListUI::OnHomeChanged()
	{
		if (_editor) {
			_editor->OnHomeChanged();
		}
	}

	void ListUI::OpenEditor(RE::FormID a_cellId, std::optional<core::FormRef> a_container)
	{
		auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
		if (!home || !_editor) {
			return;
		}
		auto* ref = a_container ? game::LookupRef(*a_container) : nullptr;
		if (!home->layout) {
			_editor->OpenSetupPrompt(*home, ref);
		} else if (ref && !home->layout->IsAnyMaster(*a_container)) {
			_editor->OpenContainerMenu(*home, ref);
		} else {
			_editor->OpenMasterMenu(*home, ref);
		}
	}

	void ListUI::ReopenContainer(RE::FormID a_cellId, const core::FormRef& a_container)
	{
		auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
		auto* ref = game::LookupRef(a_container);
		if (home && ref) {
			OpenContainerMenu(*home, ref);
		}
	}

	void ListUI::ReopenMaster(RE::FormID a_cellId)
	{
		auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
		if (!home) {
			return;
		}
		OpenMasterMenu(*home, home::CatchAllHere(*home));  // as the hotkey opens it (Controller::OnHotkey)
	}

	void ListUI::OpenSetupPrompt(home::Home& a_home, RE::TESObjectREFR* a_container)
	{
		if (ListOpening()) {
			return;
		}
		const auto        cellId = a_home.cellId;
		const auto        ref = game::ToFormRef(a_container);
		std::vector<Item> items;
		if (a_home.backup) {
			// The player's own setup, kept outside Data (DESIGN §6.6): first, as it is what they had.
			items.push_back({ std::format(kRestoreLastSetup, a_home.backup->date), [cellId]() { Controller::Get().RestoreBackup(cellId); } });
		}
		if (ref) {
			items.push_back({ std::format(kMakeMasterSetup, game::RefName(a_container)),
				[cellId, ref = *ref]() { Controller::Get().MakeMaster(cellId, ref); } });
		}
		if (const auto* best = a_home.BestTemplate()) {
			const auto author = AuthorOf(best->layout);
			const auto* fit = home::HomeRegistry::Get().BestTemplateFit(a_home);  // the auto-enable guard (DESIGN §9.1)
			const bool  mayNotFit = fit && !fit->fits;
			// A template the guard doubts asks first, as under Advanced...; No comes back here.
			items.push_back({ std::format("Use template \"{}\" ({})", best->layout.name, mayNotFit ? std::string(kMayNotFit) : "by " + author),
				[this, cellId, mayNotFit, question = TemplateQuestion(a_home), ref]() {
					if (!mayNotFit) {
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
		if (const auto offer = home::HomeRegistry::Get().OfferArea()) {
			// Multi-cell homes (DESIGN §6.7): this cell joins the home the player just came from, named first.
			items.push_back({ std::format(kAddArea, offer->homeName), [cellId]() { AddAreaNamed(cellId); } });
		}
		if (items.empty()) {
			Notify(std::format("Dova Stash: press {} at a container to set up this home", Controller::HotkeyName()));
			return;
		}
		if (_editorIsPanel) {  // without the panel, message boxes would only repeat this list
			items.push_back({ kOpenPanel, [this, cellId, ref]() { OpenEditor(cellId, ref); } });
		}
		if (!ShowList(a_home.name, std::move(items)) && _fallback) {
			_fallback->OpenSetupPrompt(a_home, a_container);
		}
	}

	void ListUI::OpenPartOfPrompt(home::Home& a_home, std::string_view a_template)
	{
		if (ListOpening()) {
			return;
		}
		std::vector<Item> items;
		items.push_back({ std::format(kUsePartOfTemplate, a_template), [cellId = a_home.cellId]() { Controller::Get().UsePartOfTemplate(cellId); } });
		if (!ShowList(a_home.name, std::move(items))) {
			IStashUI::OpenPartOfPrompt(a_home, a_template);
		}
	}

	void ListUI::OpenMasterMenu(home::Home& a_home, RE::TESObjectREFR* a_master)
	{
		if (ListOpening()) {
			return;
		}
		const auto cellId = a_home.cellId;
		const auto masterRef = a_master ? game::ToFormRef(a_master) : std::nullopt;

		std::vector<Item> items;
		items.push_back({ "Deposit", [cellId]() { Controller::Get().RunOperation(cellId, sort::Op::Deposit); } });
		if (sort::Restocks(a_home)) {
			items.push_back({ "Restock", [cellId]() { Controller::Get().RunOperation(cellId, sort::Op::Restock); } });
		}
		items.push_back({ "Sort all", [cellId]() { Controller::Get().RunOperation(cellId, sort::Op::SortAll); } });
		items.push_back({ "Retrieve...", [this, cellId]() { OpenRetrieve(cellId); } });
		items.push_back({ "Advanced...", [this, cellId]() { OpenAdvanced(cellId); } });  // Browse, the template, save, reset, the panel
		if (a_home.layout && a_home.layout->HasSellChest()) {
			for (auto& entry : SellEntries(a_home, std::nullopt, [this, cellId]() { ReopenMaster(cellId); })) {
				items.push_back({ std::move(entry.first), std::move(entry.second) });
			}
		}
		if (masterRef) {
			items.push_back({ std::format("Rename {}...", game::RefName(a_master)), [this, cellId, ref = *masterRef]() { OpenRename(cellId, ref); } });
			if (HasCustomName(a_master)) {
				items.push_back({ "Restore original name", [cellId, ref = *masterRef]() { Controller::Get().SetLabel(cellId, ref, {}); } });
			}
		}
		if (const auto* area = a_home.extra ? a_home.HereArea() : nullptr) {
			// In an extra cell of a multi-cell home (DESIGN §6.7): name it, or take it out of the home.
			items.push_back({ std::format(kRenameArea, area->name), [this, cellId, cell = area->cell, name = area->name]() {
								 if (!RenameAreaNamed(cellId, cell, name)) {
									 ListUnavailable(*this);
								 }
							 } });
			items.push_back({ std::format(kRemoveArea, a_home.DisplayName()), [this, cellId, cell = area->cell, question = std::format(kConfirmRemoveArea, area->name, a_home.DisplayName())]() {
								 Confirm(question, [cellId, cell]() { Controller::Get().RemoveArea(cellId, cell); }, [this, cellId]() { ReopenMaster(cellId); });
							 } });
		}
		// Save as template... and Reset home... are under Advanced... (as in message boxes, whose first page they
		// would not fit). The master is in another area of the home: Deposit still works where its containers are loaded.
		const bool masterAway = !a_master && a_home.layout && a_home.layout->master;
		if (!ShowList(masterAway ? std::format("{} ({})", a_home.name, kMasterNotHere) : a_home.name, std::move(items)) && _fallback) {
			_fallback->OpenMasterMenu(a_home, a_master);
		}
	}

	void ListUI::OpenContainerMenu(home::Home& a_home, RE::TESObjectREFR* a_container)
	{
		const auto ref = game::ToFormRef(a_container);
		if (!ref || !a_home.layout || ListOpening()) {
			return;
		}
		const auto  cellId = a_home.cellId;
		const auto* binding = a_home.layout->FindContainer(*ref);
		auto        summary = RoleSummary(DescribeContainer(a_container), 3);
		if (summary.empty()) {
			summary = binding && binding->ignored ? "ignored" : kNotManaged;
		}

		ContainerMenuHooks hooks;
		hooks.categoriesText = std::format("{} ({})", kCategoriesEntry, summary);
		hooks.openCategories = [this, cellId, ref = *ref]() { OpenAssignGroups(cellId, ref); };
		hooks.rename = [this, cellId, ref = *ref]() { OpenRename(cellId, ref); };
		hooks.openLearned = [this, cellId, ref = *ref]() { OpenLearned(cellId, ref); };
		if (_editorIsPanel) {
			hooks.openPanel = [this, cellId, ref = *ref]() { OpenEditor(cellId, ref); };
		}
		hooks.confirm = [this, cellId, ref = *ref](std::string a_question, std::function<void()> a_onYes) {
			Confirm(std::move(a_question), std::move(a_onYes), [this, cellId, ref]() { ReopenContainer(cellId, ref); });
		};
		hooks.back = [this, cellId, ref = *ref]() { ReopenContainer(cellId, ref); };
		std::vector<Item> items;
		for (auto& entry : ContainerMenuEntries(a_home, a_container, hooks)) {
			items.push_back({ std::move(entry.text), std::move(entry.action) });
		}
		if (!items.empty()) {
			items.front().icon = icons::ForContainer(*a_home.layout, a_container);  // what it is for, as the overlay and QuickLoot show it
		}
		if (!ShowList(game::RefName(a_container), std::move(items)) && _fallback) {
			_fallback->OpenContainerMenu(a_home, a_container);
		}
	}

	void ListUI::OpenDisplayMenu(const DisplayMenu& a_menu)
	{
		if (ListOpening()) {
			return;  // a quick second press while the first list is on its way: it would fall back to a message box on top
		}
		const auto openCategories = [this, cellId = a_menu.cellId, ref = a_menu.ref, fit = a_menu.fit, name = a_menu.name]() { OpenDisplayCategories(cellId, ref, fit, name); };
		std::vector<Item> list;
		for (auto& o : DisplayMenuOptions(a_menu, openCategories)) {
			list.push_back({ std::move(o.text), std::move(o.action), std::move(o.icon) });
		}
		if (!ShowList(a_menu.name, std::move(list))) {
			IStashUI::OpenDisplayMenu(a_menu);
		}
	}

	void ListUI::OpenArmorSetMenu(const ArmorSetMenu& a_menu)
	{
		std::vector<Item> items;
		for (const auto& c : a_menu.choices) {
			items.push_back({ c.text, [cellId = a_menu.cellId, ref = a_menu.ref, set = c.set]() {
								 ChooseArmorSet(cellId, ref, set);
								 Controller::Get().ReopenDisplayMenu(cellId, ref);
							 } });
		}
		if (a_menu.notShown > 0) {
			// Past UIListMenu's 128 entries: say where the rest are. Choosing it shows the picker again.
			items.push_back({ _editorIsPanel ? "More sets: Dova Stash > Displays"s : std::format("({} more sets)", a_menu.notShown),
				[cellId = a_menu.cellId, ref = a_menu.ref]() { OpenArmorSets(cellId, ref); } });
		}
		if (!ShowList(std::format("{}: Armor set", a_menu.name), std::move(items),
				[cellId = a_menu.cellId, ref = a_menu.ref]() { Controller::Get().ReopenDisplayMenu(cellId, ref); })) {
			IStashUI::OpenArmorSetMenu(a_menu);
		}
	}

	void ListUI::OpenDisplayCategories(RE::FormID a_cellId, const core::FormRef& a_display, const core::DisplayFit& a_fit, const std::string& a_name)
	{
		const auto list = Rebuildable([a_cellId, a_display, a_fit, a_name]() {
			const auto        patterns = DisplayCategories(a_cellId, a_display);
			std::vector<Item> items;
			for (const auto& choice : DisplayCategoryChoices(a_fit)) {
				items.push_back({ DisplayCategoryEntry(patterns, choice),
					[a_cellId, a_display, choice]() { Controller::Get().ToggleDisplayCategory(a_cellId, a_display, choice); }, icons::ForPattern(choice), true });
			}
			return std::make_shared<List>(List{ std::format("{}: Categories", a_name), std::move(items),
				[a_cellId, a_display]() { Controller::Get().ReopenDisplayMenu(a_cellId, a_display); } });
		});
		if (!Show(list)) {
			ListUnavailable(*this);
		}
	}

	void ListUI::OpenAssignGroups(RE::FormID a_cellId, const core::FormRef& a_container)
	{
		if (RootCategories().empty()) {
			Notify("Dova Stash: no categories are loaded (see DovaStash.log)", true);
			return;
		}
		const auto list = Rebuildable([this, a_cellId, a_container]() {
			const auto* binding = FindBinding(a_cellId, a_container);
			const auto  learned = LearnedOf(a_cellId, a_container);
			// [x] all of the group here, [~] some of it, [ ] none.
			std::vector<Item> items;
			for (const auto& root : RootCategories()) {
				const auto  children = ChildCategories(root);
				const auto  all = root + ".*";
				auto        mark = kMarkNone;
				if (HasPattern(binding, all) || HasPattern(binding, "*") || (children.empty() && Covers(binding, root))) {
					mark = kMarkAll;
				} else if (Covers(binding, root) || std::ranges::any_of(children, [&](const std::string& id) { return Covers(binding, id); })) {
					mark = kMarkSome;
				}
				if (children.empty()) {
					items.push_back({ std::format("{} {}{}", mark, CategoryName(root), LearnedSuffix(learned, all)),
						[a_cellId, a_container, all]() { Controller::Get().ToggleCategory(a_cellId, a_container, all); }, icons::ForCategory(root), true });
				} else {
					items.push_back({ std::format("{} {}...{}", mark, CategoryName(root), LearnedSuffix(learned, all)),
						[this, a_cellId, a_container, root]() { OpenAssignGroup(a_cellId, a_container, root); }, icons::ForCategory(root) });
				}
			}
			return std::make_shared<List>(List{ std::format("{}: Categories", NameOf(a_container)), std::move(items),
				[this, a_cellId, a_container]() { ReopenContainer(a_cellId, a_container); } });
		});
		if (!Show(list)) {
			ListUnavailable(*this);
		}
	}

	void ListUI::OpenAssignGroup(RE::FormID a_cellId, const core::FormRef& a_container, const std::string& a_root)
	{
		const auto list = Rebuildable([this, a_cellId, a_container, a_root]() {
			const auto* binding = FindBinding(a_cellId, a_container);
			const auto  all = a_root + ".*";
			const auto  learned = LearnedOf(a_cellId, a_container);
			const auto  toggle = [a_cellId, a_container](std::string a_pattern) {
				return [a_cellId, a_container, pattern = std::move(a_pattern)]() { Controller::Get().ToggleCategory(a_cellId, a_container, pattern); };
			};
			// [x] bound exactly, [+] covered by a wider pattern, [ ] not routed here.
			std::vector<Item> items;
			const auto allMark = HasPattern(binding, all) ? "[x]" : Covers(binding, a_root) ? "[+]" : "[ ]";
			items.push_back({ std::format("{} All {}", allMark, CategoryName(a_root)), toggle(all), icons::ForCategory(a_root), true });
			for (const auto& id : ChildCategories(a_root)) {
				const auto mark = HasPattern(binding, id) ? "[x]" : Covers(binding, id) ? "[+]" : "[ ]";
				items.push_back({ std::format("{} {}{}", mark, CategoryName(id), LearnedSuffix(learned, id + ".*")), toggle(id), icons::ForCategory(id), true });
			}
			return std::make_shared<List>(List{ std::format("{}: {}", NameOf(a_container), CategoryName(a_root)), std::move(items),
				[this, a_cellId, a_container]() { OpenAssignGroups(a_cellId, a_container); } });
		});
		if (!Show(list)) {
			ListUnavailable(*this);
		}
	}

	void ListUI::OpenAdvanced(RE::FormID a_cellId)
	{
		auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		// Everyday actions first, the one that asks last.
		std::vector<Item> items;
		items.push_back({ "Browse containers...", [this, a_cellId]() { OpenBrowse(a_cellId); } });
		if (home->BestTemplate()) {
			// Asks only when it would replace changes of the player's own, or may not fit (as the panel does).
			items.push_back({ TemplateEntry(*home), [this, a_cellId, ask = AskBeforeTemplate(*home), question = TemplateQuestion(*home)]() {
								 if (ask) {
									 Confirm(question, [a_cellId]() { Controller::Get().UseTemplate(a_cellId); }, [this, a_cellId]() { OpenAdvanced(a_cellId); });
								 } else {
									 Controller::Get().UseTemplate(a_cellId);
								 }
							 } });
		}
		items.push_back({ kSaveAsTemplate, [this, a_cellId]() { SaveAsTemplate(a_cellId, [this, a_cellId]() { OpenAdvanced(a_cellId); }); } });
		items.push_back({ "Reset home...", [this, a_cellId, name = home->name]() {
							 Confirm(std::format(kConfirmResetHome, name), [a_cellId]() { Controller::Get().ResetHome(a_cellId); }, [this, a_cellId]() { OpenAdvanced(a_cellId); });
						 } });
		if (_editorIsPanel) {
			// What Advanced... opened before these moved here: the panel's This home page.
			items.push_back({ kOpenPanel, [this, a_cellId, master = home->layout->master]() { OpenEditor(a_cellId, master); } });
		}
		if (!ShowList("Advanced", std::move(items), [this, a_cellId]() { ReopenMaster(a_cellId); })) {
			ListUnavailable(*this);
		}
	}

	void ListUI::OpenRetrieve(RE::FormID a_cellId)
	{
		auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		const auto   counts = sort::CountByCategory(*home);
		std::int32_t total = 0;
		for (const auto& [id, n] : counts) {
			total += n;
		}
		if (total == 0) {
			Notify("Dova Stash: nothing to retrieve");
			return;
		}

		std::vector<Item> items;
		items.push_back({ std::format("Everything ({})", total), [this, a_cellId, total]() {
							 if (total <= kRetrieveAllAskAbove) {
								 Controller::Get().RunOperation(a_cellId, sort::Op::Retrieve, "*");
								 return;
							 }
							 Confirm(std::format(kConfirmRetrieveAll, total), [a_cellId]() { Controller::Get().RunOperation(a_cellId, sort::Op::Retrieve, "*"); },
								 [this, a_cellId]() { OpenRetrieve(a_cellId); });
						 },
			icons::ForPattern("*") });
		for (const auto& root : RootCategories()) {
			const auto all = root + ".*";
			const auto n = CountMatching(counts, all);
			if (n == 0) {
				continue;
			}
			const auto children = ChildCategories(root);
			const auto populated = std::ranges::count_if(children, [&](const std::string& id) { return CountMatching(counts, id + ".*") > 0; });
			if (populated <= 1) {
				items.push_back({ std::format("{} ({})", CategoryName(root), n), [a_cellId, all]() { Controller::Get().RunOperation(a_cellId, sort::Op::Retrieve, all); },
					icons::ForCategory(root) });
			} else {
				items.push_back({ std::format("{}... ({})", CategoryName(root), n), [this, a_cellId, root]() { OpenRetrieveGroup(a_cellId, root); }, icons::ForCategory(root) });
			}
		}
		if (!ShowList("Retrieve", std::move(items), [this, a_cellId]() { ReopenMaster(a_cellId); })) {
			ListUnavailable(*this);
		}
	}

	void ListUI::OpenRetrieveGroup(RE::FormID a_cellId, const std::string& a_root)
	{
		auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		const auto counts = sort::CountByCategory(*home);
		const auto all = a_root + ".*";

		std::vector<Item> items;
		items.push_back({ std::format("All {} ({})", CategoryName(a_root), CountMatching(counts, all)),
			[a_cellId, all]() { Controller::Get().RunOperation(a_cellId, sort::Op::Retrieve, all); }, icons::ForCategory(a_root) });
		for (const auto& id : ChildCategories(a_root)) {
			const auto pattern = id + ".*";
			if (const auto n = CountMatching(counts, pattern); n > 0) {
				items.push_back({ std::format("{} ({})", CategoryName(id), n), [a_cellId, pattern]() { Controller::Get().RunOperation(a_cellId, sort::Op::Retrieve, pattern); },
					icons::ForCategory(id) });
			}
		}
		if (!ShowList(std::format("Retrieve: {}", CategoryName(a_root)), std::move(items), [this, a_cellId]() { OpenRetrieve(a_cellId); })) {
			ListUnavailable(*this);
		}
	}

	void ListUI::OpenBrowse(RE::FormID a_cellId)
	{
		auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		std::vector<Item> items;
		auto add = [&](const core::FormRef& a_ref, const std::string& a_what) {
			auto* ref = game::LookupRef(a_ref);
			if (!ref || ref->IsDisabled()) {
				return;
			}
			items.push_back({ BrowseEntryText(ref, a_what), [a_cellId, a_ref]() { Controller::Get().Browse(a_cellId, a_ref); },
				icons::ForContainer(*home->layout, ref) });
		};
		if (home->layout->master) {
			add(*home->layout->master, "master");
		}
		for (const auto& [m, role] : AreaMasterRoles(*home)) {
			add(m, role);  // an area's master (DESIGN §6.7): it holds that area's leftovers
		}
		for (const auto& c : home->layout->containers) {
			if (c.ignored) {
				continue;
			}
			const auto summary = RoleSummary(DescribeContainer(game::LookupRef(c.ref)), 2);
			add(c.ref, summary.empty() ? c.label : summary);
		}
		if (items.empty()) {
			Notify("Dova Stash: no containers to open");
			return;
		}
		if (!ShowList("Browse containers", std::move(items), [this, a_cellId]() { OpenAdvanced(a_cellId); })) {
			ListUnavailable(*this);
		}
	}

	void ListUI::OpenRename(RE::FormID a_cellId, const core::FormRef& a_container)
	{
		if (game::LookupRef(a_container) && !RenameContainerNamed(a_cellId, a_container)) {
			ListUnavailable(*this);
		}
	}

	void ListUI::OpenLearned(RE::FormID a_cellId, const core::FormRef& a_container)
	{
		auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		// Learning: putting an item into a managed container by hand makes that container the item's
		// destination from then on, ahead of its category. Listed here so it can be undone.
		std::vector<Item> items;
		std::size_t       count = 0;
		for (const auto& [item, container] : home->layout->learned) {
			if (!(container == a_container)) {
				continue;
			}
			++count;
			auto*       form = game::LookupForm(item);
			std::string name = form && form->GetName() && *form->GetName() ? form->GetName() : item.ToString();
			items.push_back({ std::format("Forget {}", name), [this, a_cellId, a_container, item]() {
								 Controller::Get().RemoveLearned(a_cellId, item);
								 OpenLearned(a_cellId, a_container);
							 } });
		}
		if (count == 0) {
			ReopenContainer(a_cellId, a_container);
			return;
		}
		items.insert(items.begin(), Item{ ForgetAllEntry(count), [this, a_cellId, a_container, count]() {
											 Confirm(ForgetLearnedQuestion(count, NameOf(a_container)),
												 [this, a_cellId, a_container]() {
													 Controller::Get().ForgetLearned(a_cellId, a_container);
													 ReopenContainer(a_cellId, a_container);
												 },
												 [this, a_cellId, a_container]() { OpenLearned(a_cellId, a_container); });
										 } });
		if (!ShowList(std::format("{}: Learned items", NameOf(a_container)), std::move(items), [this, a_cellId, a_container]() { ReopenContainer(a_cellId, a_container); })) {
			ListUnavailable(*this);
		}
	}
}
