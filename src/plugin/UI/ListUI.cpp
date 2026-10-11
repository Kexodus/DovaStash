#include "UI/ListUI.h"

#include "Controller.h"
#include "Game/FormUtil.h"
#include "Sort/SortEngine.h"
#include "UI/Labels.h"
#include "UI/MessageBox.h"
#include "UI/UIExtensions.h"

namespace dova::ui
{
	namespace
	{
		struct Item
		{
			std::string           text;
			std::function<void()> action;  // nullptr = does nothing when chosen
		};

		// Shows a flat list. `a_onBack` (optional) is offered as the first entry and also runs when the
		// list is cancelled. Returns false if the list could not open.
		bool ShowList(std::vector<Item> a_items, std::function<void()> a_onBack = {})
		{
			if (a_onBack) {
				a_items.insert(a_items.begin(), Item{ "< Back", a_onBack });
			}
			std::vector<uiext::ListEntry>      entries;
			std::vector<std::function<void()>> actions;
			std::vector<std::string>           texts;
			entries.reserve(a_items.size());
			actions.reserve(a_items.size());
			for (auto& item : a_items) {
				texts.push_back(item.text);
				entries.push_back({ std::move(item.text) });
				actions.push_back(std::move(item.action));
			}
			return uiext::OpenList(std::move(entries), [actions = std::move(actions), texts = std::move(texts), onBack = std::move(a_onBack)](std::int32_t a_index) {
				if (a_index < 0 || static_cast<std::size_t>(a_index) >= actions.size()) {
					logger::info("List: cancelled{}", onBack ? " (back)" : "");
					if (onBack) {
						onBack();
					}
					return;
				}
				logger::info("List: chose \"{}\"", texts[a_index]);
				if (actions[a_index]) {
					actions[a_index]();
				}
			});
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
		if (ref) {
			items.push_back({ std::format("Make {} the master container", game::RefName(a_container)),
				[cellId, ref = *ref]() { Controller::Get().MakeMaster(cellId, ref); } });
		}
		if (const auto* best = a_home.BestTemplate()) {
			const auto author = best->layout.author.empty() ? "unknown"s : best->layout.author;
			items.push_back({ std::format("Use template \"{}\" (by {})", best->layout.name, author), [cellId]() { Controller::Get().UseTemplate(cellId); } });
		}
		if (items.empty()) {
			Notify(std::format("Dova Stash: look at a container and press {} to set up this home", Controller::HotkeyName()));
			return;
		}
		items.push_back({ "Advanced...", [this, cellId, ref]() { OpenEditor(cellId, ref); } });
		if (!ShowList(std::move(items)) && _fallback) {
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
		}
		if (const auto* best = a_home.BestTemplate()) {
			const bool basedOnTemplate = a_home.layout && a_home.layout->basedOn.has_value();
			const auto name = best->layout.name;
			items.push_back({ basedOnTemplate ? std::format("Re-apply template \"{}\"", name) : std::format("Use template \"{}\"", name), [cellId, name]() {
								 Confirm(std::format("Use the category bindings of the template \"{}\"?\nYour learned destinations, keep rules and ignored containers are kept; category bindings are replaced by the template's. A backup is saved; no items are moved.", name),
									 [cellId]() { Controller::Get().UseTemplate(cellId); });
							 } });
		}
		items.push_back({ "Advanced...", [this, cellId, masterRef]() { OpenEditor(cellId, masterRef); } });
		items.push_back({ "Reset home...", [cellId, name = a_home.name]() {
							 Confirm(std::format("Reset Dova Stash for {}?\nYour layout file is backed up (.bak); no items are moved.", name),
								 [cellId]() { Controller::Get().ResetHome(cellId); });
						 } });
		if (!ShowList(std::move(items)) && _fallback) {
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
		items.push_back({ std::format("Categories... ({})", summary), [this, cellId, ref = *ref]() { OpenAssignGroups(cellId, ref); } });
		items.push_back({ std::format("Open {}", game::RefName(a_container)), [cellId, ref = *ref]() { Controller::Get().Browse(cellId, ref); } });
		items.push_back({ "Rename...", [this, cellId, ref = *ref]() { OpenRename(cellId, ref); } });
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
		items.push_back({ "Make master", [cellId, ref = *ref]() { Controller::Get().MakeMaster(cellId, ref); } });
		items.push_back({ "Advanced...", [this, cellId, ref = *ref]() { OpenEditor(cellId, ref); } });
		if (!ShowList(std::move(items)) && _fallback) {
			_fallback->OpenContainerMenu(a_home, a_container);
		}
	}

	void ListUI::OpenAssignGroups(RE::FormID a_cellId, const core::FormRef& a_container)
	{
		const auto* binding = FindBinding(a_cellId, a_container);
		const auto  roots = RootCategories();
		if (roots.empty()) {
			Notify("Dova Stash: no categories are loaded (see the log)");
			return;
		}

		// [x] all of the group here, [~] some of it, [ ] none.
		std::vector<Item> items;
		for (const auto& root : roots) {
			const auto children = ChildCategories(root);
			const auto all = root + ".*";
			std::string mark = "[ ]";
			if (HasPattern(binding, all) || HasPattern(binding, "*") || (children.empty() && Covers(binding, root))) {
				mark = "[x]";
			} else if (Covers(binding, root) || std::ranges::any_of(children, [&](const std::string& id) { return Covers(binding, id); })) {
				mark = "[~]";
			}
			if (children.empty()) {
				items.push_back({ std::format("{} {}", mark, CategoryName(root)), [this, a_cellId, a_container, all]() {
									 Controller::Get().ToggleCategory(a_cellId, a_container, all);
									 OpenAssignGroups(a_cellId, a_container);
								 } });
			} else {
				items.push_back({ std::format("{} {}...", mark, CategoryName(root)),
					[this, a_cellId, a_container, root]() { OpenAssignGroup(a_cellId, a_container, root); } });
			}
		}
		if (!ShowList(std::move(items), [this, a_cellId, a_container]() { ReopenContainer(a_cellId, a_container); })) {
			ListUnavailable(*this);
		}
	}

	void ListUI::OpenAssignGroup(RE::FormID a_cellId, const core::FormRef& a_container, const std::string& a_root)
	{
		const auto* binding = FindBinding(a_cellId, a_container);
		const auto  all = a_root + ".*";

		auto toggle = [this, a_cellId, a_container, a_root](std::string a_pattern) {
			return [this, a_cellId, a_container, a_root, pattern = std::move(a_pattern)]() {
				Controller::Get().ToggleCategory(a_cellId, a_container, pattern);
				OpenAssignGroup(a_cellId, a_container, a_root);
			};
		};
		// [x] bound exactly, [+] covered by a wider pattern, [ ] not routed here.
		std::vector<Item> items;
		items.push_back({ std::format("{} All {}", HasPattern(binding, all) ? "[x]" : "[ ]", CategoryName(a_root)), toggle(all) });
		for (const auto& id : ChildCategories(a_root)) {
			const auto mark = HasPattern(binding, id) ? "[x]" : Covers(binding, id) ? "[+]" : "[ ]";
			items.push_back({ std::format("{} {}", mark, CategoryName(id)), toggle(id) });
		}
		if (!ShowList(std::move(items), [this, a_cellId, a_container]() { OpenAssignGroups(a_cellId, a_container); })) {
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
		items.push_back({ std::format("Everything ({})", total), [a_cellId]() { Controller::Get().RunOperation(a_cellId, sort::Op::Retrieve, "*"); } });
		for (const auto& root : RootCategories()) {
			const auto all = root + ".*";
			const auto n = CountMatching(counts, all);
			if (n == 0) {
				continue;
			}
			const auto children = ChildCategories(root);
			const auto populated = std::ranges::count_if(children, [&](const std::string& id) { return CountMatching(counts, id + ".*") > 0; });
			if (populated <= 1) {
				items.push_back({ std::format("{} ({})", CategoryName(root), n), [a_cellId, all]() { Controller::Get().RunOperation(a_cellId, sort::Op::Retrieve, all); } });
			} else {
				items.push_back({ std::format("{}... ({})", CategoryName(root), n), [this, a_cellId, root]() { OpenRetrieveGroup(a_cellId, root); } });
			}
		}
		if (!ShowList(std::move(items), [this, a_cellId]() { ReopenMaster(a_cellId); })) {
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
			[a_cellId, all]() { Controller::Get().RunOperation(a_cellId, sort::Op::Retrieve, all); } });
		for (const auto& id : ChildCategories(a_root)) {
			const auto pattern = id + ".*";
			if (const auto n = CountMatching(counts, pattern); n > 0) {
				items.push_back({ std::format("{} ({})", CategoryName(id), n), [a_cellId, pattern]() { Controller::Get().RunOperation(a_cellId, sort::Op::Retrieve, pattern); } });
			}
		}
		if (!ShowList(std::move(items), [this, a_cellId]() { OpenRetrieve(a_cellId); })) {
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
			items.push_back({ std::format("{} - {} ({} items)", game::RefName(ref), a_what, ItemCount(ref)),
				[a_cellId, a_ref]() { Controller::Get().Browse(a_cellId, a_ref); } });
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
		if (!ShowList(std::move(items), [this, a_cellId]() { ReopenMaster(a_cellId); })) {
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
			if (a_text) {
				// An empty name restores the original one (Controller::SetLabel).
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
		if (!ShowList(std::move(items), [this, a_cellId, a_container]() { ReopenContainer(a_cellId, a_container); })) {
			ListUnavailable(*this);
		}
	}
}
