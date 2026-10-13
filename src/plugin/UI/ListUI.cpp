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

		// Shows `a_list` (see Entries), with the Dova Stash logo above it and entry `a_select` selected
		// (-1: the menu's default). "< Back" and cancelling run `onBack`. Toggles and the header keep
		// the menu open and update it in place; when the menu cannot do that (its SWF lacks what
		// uiext needs) choosing them closes it like any entry, and the list reopens on the same entry.
		// Returns false if the list could not open.
		bool Show(std::shared_ptr<const List> a_list, std::int32_t a_select = -1)
		{
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

		void Confirm(std::string a_question, std::function<void()> a_onYes)
		{
			ShowMessageBox(a_question, { "Yes", "No" }, [onYes = std::move(a_onYes)](std::size_t a_index) {
				if (a_index == 0 && onYes) {
					onYes();
				}
			});
		}

		const core::ContainerBinding* FindBinding(RE::FormID a_cellId, const core::FormRef& a_container)
		{
			auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
			return home && home->layout ? home->layout->FindContainer(a_container) : nullptr;
		}

		std::int32_t ItemCount(RE::TESObjectREFR* a_ref)
		{
			std::int32_t total = 0;
			if (a_ref) {
				for (const auto& [object, data] : a_ref->GetInventory()) {
					if (object && data.first > 0) {
						total += data.first;
					}
				}
			}
			return total;
		}

		// The player renamed it (Controller::SetLabel stores the name on the reference).
		bool HasCustomName(RE::TESObjectREFR* a_ref)
		{
			return a_ref && a_ref->extraList.HasType(RE::ExtraDataType::kTextDisplayData);
		}

		void ListUnavailable(IStashUI& a_ui)
		{
			a_ui.Notify("Dova Stash: close the open menu first");
		}
	}

	ListUI::ListUI(std::shared_ptr<IStashUI> a_editor, std::shared_ptr<IStashUI> a_fallback) :
		_editor(std::move(a_editor)),
		_fallback(std::move(a_fallback))
	{}

	void ListUI::Notify(std::string_view a_message)
	{
		logger::info("Notify: {}", a_message);
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
		} else if (ref && !home->layout->IsMaster(*a_container)) {
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
		RE::TESObjectREFR* master = nullptr;
		if (home->layout && home->layout->master) {
			master = game::LookupRef(*home->layout->master);
		}
		OpenMasterMenu(*home, master);
	}

	void ListUI::OpenSetupPrompt(home::Home& a_home, RE::TESObjectREFR* a_container)
	{
		const auto        cellId = a_home.cellId;
		const auto        ref = game::ToFormRef(a_container);
		std::vector<Item> items;
		if (a_home.backup) {
			// The player's own setup, kept outside Data (DESIGN §6.6): first, as it is what they had.
			items.push_back({ std::format(kRestoreLastSetup, a_home.backup->date), [cellId]() { Controller::Get().RestoreBackup(cellId); } });
		}
		if (ref) {
			items.push_back({ std::format("Make {} the master container", game::RefName(a_container)),
				[cellId, ref = *ref]() { Controller::Get().MakeMaster(cellId, ref); } });
		}
		if (const auto* best = a_home.BestTemplate()) {
			const auto author = best->layout.author.empty() ? "unknown"s : best->layout.author;
			items.push_back({ std::format("Use template \"{}\" (by {})", best->layout.name, author), [cellId]() { Controller::Get().UseTemplate(cellId); } });
		}
		if (const auto offer = home::HomeRegistry::Get().OfferArea()) {
			// Multi-cell homes (DESIGN §6.7): this cell joins the home the player just came from, named first.
			items.push_back({ std::format(kAddArea, offer->homeName), [cellId]() { AddAreaNamed(cellId); } });
		}
		if (items.empty()) {
			Notify(std::format("Dova Stash: look at a container and press {} to set up this home", Controller::HotkeyName()));
			return;
		}
		items.push_back({ "Advanced...", [this, cellId, ref]() { OpenEditor(cellId, ref); } });
		if (!ShowList(a_home.name, std::move(items)) && _fallback) {
			_fallback->OpenSetupPrompt(a_home, a_container);
		}
	}

	void ListUI::OpenMasterMenu(home::Home& a_home, RE::TESObjectREFR* a_master)
	{
		const auto cellId = a_home.cellId;
		const auto masterRef = a_master ? game::ToFormRef(a_master) : std::nullopt;

		std::vector<Item> items;
		items.push_back({ "Deposit", [cellId]() { Controller::Get().RunOperation(cellId, sort::Op::Deposit); } });
		items.push_back({ "Sort all", [cellId]() { Controller::Get().RunOperation(cellId, sort::Op::SortAll); } });
		items.push_back({ "Retrieve...", [this, cellId]() { OpenRetrieve(cellId); } });
		items.push_back({ "Browse containers...", [this, cellId]() { OpenBrowse(cellId); } });
		if (masterRef) {
			items.push_back({ std::format("Rename {}...", game::RefName(a_master)), [this, cellId, ref = *masterRef]() { OpenRename(cellId, ref); } });
			if (HasCustomName(a_master)) {
				items.push_back({ "Restore original name", [cellId, ref = *masterRef]() { Controller::Get().SetLabel(cellId, ref, {}); } });
			}
		}
		if (const auto* best = a_home.BestTemplate()) {
			const bool basedOnTemplate = a_home.layout && a_home.layout->basedOn && a_home.layout->basedOn->starts_with(best->source + "@");
			const auto name = best->layout.name;
			items.push_back({ basedOnTemplate ? std::format("Re-apply template \"{}\"", name) : std::format("Use template \"{}\"", name), [cellId, name]() {
								 Confirm(std::format("Use the template \"{}\"?\nYour learned destinations, keep rules and ignored containers are kept; the master and category bindings are replaced by the template's. A backup is saved; no items are moved.", name),
									 [cellId]() { Controller::Get().UseTemplate(cellId); });
							 } });
		}
		items.push_back({ kSaveAsTemplate, [cellId]() { SaveAsTemplate(cellId); } });
		items.push_back({ "Advanced...", [this, cellId, masterRef]() { OpenEditor(cellId, masterRef); } });
		if (const auto* area = a_home.extra ? a_home.HereArea() : nullptr) {
			// In an extra cell of a multi-cell home (DESIGN §6.7): name it, or take it out of the home.
			items.push_back({ std::format(kRenameArea, area->name), [this, cellId, cell = area->cell, name = area->name]() {
								 if (!RenameAreaNamed(cellId, cell, name)) {
									 ListUnavailable(*this);
								 }
							 } });
			items.push_back({ std::format(kRemoveArea, a_home.name), [cellId, cell = area->cell, question = std::format(kConfirmRemoveArea, area->name, a_home.name)]() {
								 Confirm(question, [cellId, cell]() { Controller::Get().RemoveArea(cellId, cell); });
							 } });
		}
		items.push_back({ "Reset home...", [cellId, name = a_home.name]() {
							 Confirm(std::format("Reset Dova Stash for {}?\nYour setup is backed up (.bak); no items are moved.", name),
								 [cellId]() { Controller::Get().ResetHome(cellId); });
						 } });
		if (!ShowList(a_home.name, std::move(items)) && _fallback) {
			_fallback->OpenMasterMenu(a_home, a_master);
		}
	}

	void ListUI::OpenContainerMenu(home::Home& a_home, RE::TESObjectREFR* a_container)
	{
		const auto ref = game::ToFormRef(a_container);
		if (!ref || !a_home.layout) {
			return;
		}
		const auto  cellId = a_home.cellId;
		const auto* binding = a_home.layout->FindContainer(*ref);
		auto        summary = RoleSummary(DescribeContainer(a_container), 3);
		if (summary.empty()) {
			summary = binding && binding->ignored ? "ignored" : "nothing assigned";
		}

		std::vector<Item> items;
		// The container's own icon (what it is for), as the overlay and QuickLoot show it.
		items.push_back({ std::format("Categories... ({})", summary), [this, cellId, ref = *ref]() { OpenAssignGroups(cellId, ref); },
			icons::ForContainer(*a_home.layout, a_container) });
		items.push_back({ std::format("Open {}", game::RefName(a_container)), [cellId, ref = *ref]() { Controller::Get().Browse(cellId, ref); } });
		items.push_back({ "Rename...", [this, cellId, ref = *ref]() { OpenRename(cellId, ref); } });
		if (HasCustomName(a_container)) {
			items.push_back({ "Restore original name", [cellId, ref = *ref]() { Controller::Get().SetLabel(cellId, ref, {}); } });
		}
		std::size_t learned = 0;
		for (const auto& [item, container] : a_home.layout->learned) {
			if (container == *ref) {
				++learned;
			}
		}
		if (learned > 0) {
			items.push_back({ std::format("Learned items ({})...", learned), [this, cellId, ref = *ref]() { OpenLearned(cellId, ref); } });
		}
		if (binding && !binding->categories.empty()) {
			items.push_back({ "Clear categories", [cellId, ref = *ref]() { Controller::Get().ClearCategories(cellId, ref); } });
		}
		const bool ignored = binding && binding->ignored;
		items.push_back({ ignored ? "Stop ignoring" : "Ignore", [cellId, ref = *ref, ignored]() { Controller::Get().SetIgnored(cellId, ref, !ignored); } });
		if (binding || learned > 0) {
			items.push_back({ "Remove...", [this, cellId, ref = *ref, name = game::RefName(a_container)]() {
								 Confirm(std::format("Remove {} from Dova Stash?\n{}", name, kRemoveExplained), [cellId, ref]() { Controller::Get().RemoveContainer(cellId, ref); });
							 } });
		}
		items.push_back({ "Make master", [cellId, ref = *ref]() { Controller::Get().MakeMaster(cellId, ref); } });
		items.push_back({ "Advanced...", [this, cellId, ref = *ref]() { OpenEditor(cellId, ref); } });
		if (!ShowList(game::RefName(a_container), std::move(items)) && _fallback) {
			_fallback->OpenContainerMenu(a_home, a_container);
		}
	}

	void ListUI::OpenDisplayMenu(const DisplayMenu& a_menu)
	{
		const auto openCategories = [this, cellId = a_menu.cellId, ref = a_menu.ref, kind = a_menu.kind, name = a_menu.name]() { OpenDisplayCategories(cellId, ref, kind, name); };
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
		if (!ShowList(std::format("{}: Armor set", a_menu.name), std::move(items),
				[cellId = a_menu.cellId, ref = a_menu.ref]() { Controller::Get().ReopenDisplayMenu(cellId, ref); })) {
			IStashUI::OpenArmorSetMenu(a_menu);
		}
	}

	void ListUI::OpenDisplayCategories(RE::FormID a_cellId, const core::FormRef& a_display, core::DisplayKind a_kind, const std::string& a_name)
	{
		const auto list = Rebuildable([a_cellId, a_display, a_kind, a_name]() {
			const auto        patterns = DisplayCategories(a_cellId, a_display);
			std::vector<Item> items;
			for (const auto& choice : DisplayCategoryChoices(a_kind)) {
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
			Notify("Dova Stash: no categories are loaded (see the log)");
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
				std::string mark = "[ ]";
				if (HasPattern(binding, all) || HasPattern(binding, "*") || (children.empty() && Covers(binding, root))) {
					mark = "[x]";
				} else if (Covers(binding, root) || std::ranges::any_of(children, [&](const std::string& id) { return Covers(binding, id); })) {
					mark = "[~]";
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
			items.push_back({ std::format("{} All {}", HasPattern(binding, all) ? "[x]" : "[ ]", CategoryName(a_root)), toggle(all), icons::ForCategory(a_root), true });
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
		items.push_back({ std::format("Everything ({})", total), [a_cellId]() { Controller::Get().RunOperation(a_cellId, sort::Op::Retrieve, "*"); }, icons::ForPattern("*") });
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
			const auto name = game::RefName(ref);
			items.push_back({ a_what.empty() || core::IEquals(a_what, name) ? std::format("{} ({} items)", name, ItemCount(ref)) :
			                                                                  std::format("{} - {} ({} items)", name, a_what, ItemCount(ref)),
				[a_cellId, a_ref]() { Controller::Get().Browse(a_cellId, a_ref); }, icons::ForContainer(*home->layout, ref) });
		};
		if (home->layout->master) {
			add(*home->layout->master, "master");
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
		if (!ShowList("Browse containers", std::move(items), [this, a_cellId]() { ReopenMaster(a_cellId); })) {
			ListUnavailable(*this);
		}
	}

	void ListUI::OpenRename(RE::FormID a_cellId, const core::FormRef& a_container)
	{
		auto* ref = game::LookupRef(a_container);
		if (!ref) {
			return;
		}
		const bool opened = uiext::OpenTextEntry(game::RefName(ref), [a_cellId, a_container](std::optional<std::string> a_text) {
			// The text box reports an empty text when it is cancelled: that must never clear a name.
			// "Restore original name" is a separate list entry.
			if (a_text && a_text->find_first_not_of(" \t") != std::string::npos) {
				Controller::Get().SetLabel(a_cellId, a_container, *a_text);
			}
		});
		if (!opened) {
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
		items.insert(items.begin(), Item{ std::format("Forget all {} learned items", count), [a_cellId, a_container]() {
											 Controller::Get().ForgetLearned(a_cellId, a_container);
										 } });
		if (!ShowList(std::format("{}: Learned items", NameOf(a_container)), std::move(items), [this, a_cellId, a_container]() { ReopenContainer(a_cellId, a_container); })) {
			ListUnavailable(*this);
		}
	}
}
