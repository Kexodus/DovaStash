#include "Controller.h"

#include "Game/Eligibility.h"
#include "Game/FormUtil.h"
#include "Game/ItemAdapter.h"
#include "Home/Store.h"
#include "Settings.h"

#include <thread>

namespace dova
{
	namespace
	{
		home::HomeRegistry& Registry()
		{
			return home::HomeRegistry::Get();
		}

		RE::TESObjectCELL* PlayerCell()
		{
			auto* player = game::Player();
			return player ? player->GetParentCell() : nullptr;
		}

		std::string_view LearnResultName(core::LearnResult a_result)
		{
			switch (a_result) {
			case core::LearnResult::Recorded:
				return "recorded";
			case core::LearnResult::Updated:
				return "updated";
			case core::LearnResult::Unchanged:
				return "unchanged";
			case core::LearnResult::Removed:
				return "removed (container is the natural destination)";
			case core::LearnResult::RejectedMaster:
				return "ignored (master)";
			case core::LearnResult::RejectedIgnored:
				return "ignored (container is ignored)";
			case core::LearnResult::RejectedItem:
				return "ignored (quest/non-playable item)";
			default:
				return "?";
			}
		}

		std::string Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) {
				return {};
			}
			const auto last = a_text.find_last_not_of(" \t\r\n");
			return std::string(a_text.substr(first, last - first + 1));
		}

		// File stem for an exported template: ASCII letters, digits, space, '-', '_', '.'; everything
		// else becomes '_' (std::filesystem::path(std::string) is ANSI on Windows, so stay ASCII).
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
			}
			static constexpr std::array kReserved{ "CON"sv, "PRN"sv, "AUX"sv, "NUL"sv, "COM1"sv, "COM2"sv, "COM3"sv, "COM4"sv, "COM5"sv, "COM6"sv,
				"COM7"sv, "COM8"sv, "COM9"sv, "LPT1"sv, "LPT2"sv, "LPT3"sv, "LPT4"sv, "LPT5"sv, "LPT6"sv, "LPT7"sv, "LPT8"sv, "LPT9"sv };
			if (std::ranges::any_of(kReserved, [&](std::string_view r) { return core::IEquals(r, out); })) {
				out += '_';
			}
			return out.empty() ? "Template"s : out;
		}

		// Plugins a template must list in "requires": every plugin its form references point into.
		void AddReferencedPlugins(const core::Layout& a_layout, std::vector<std::string>& a_requires)
		{
			auto add = [&](const std::string& a_plugin) {
				if (!a_plugin.empty() && std::ranges::none_of(a_requires, [&](const std::string& p) { return core::IEquals(p, a_plugin); })) {
					a_requires.push_back(a_plugin);
				}
			};
			if (a_layout.cell) {
				add(a_layout.cell->plugin);
			}
			if (a_layout.master) {
				add(a_layout.master->plugin);
			}
			for (const auto& c : a_layout.containers) {
				add(c.ref.plugin);
				for (const auto& f : c.forms) {
					add(f.plugin);
				}
			}
			if (a_layout.keep) {
				for (const auto* list : { &a_layout.keep->always, &a_layout.keep->never }) {
					for (const auto& entry : *list) {
						if (const auto sel = core::KeepSelector::Parse(entry); sel.kind == core::KeepSelector::Kind::Form) {
							add(sel.form.plugin);
						}
					}
				}
				for (const auto& q : a_layout.keep->quotas) {
					if (q.forms) {
						for (const auto& f : *q.forms) {
							add(f.plugin);
						}
					}
				}
			}
		}

		// Bindings of `a_layout` that fail the safety checks right now (logged).
		int CountUnavailable(const core::Layout& a_layout, RE::TESObjectCELL* a_cell)
		{
			int  unavailable = 0;
			auto check = [&](const core::FormRef& a_ref) {
				const auto verdict = game::CheckContainer(game::LookupRef(a_ref), a_cell);
				if (!verdict.ok) {
					++unavailable;
					logger::warn("Template container {} unavailable: {}", a_ref.ToString(), verdict.reason);
				}
			};
			if (a_layout.master) {
				check(*a_layout.master);
			}
			for (const auto& c : a_layout.containers) {
				check(c.ref);
			}
			return unavailable;
		}
	}

	Controller& Controller::Get()
	{
		static Controller controller;
		return controller;
	}

	void Controller::Init()
	{
		home::Store::Get().Load();
		_ui = ui::CreateStashUI();
	}

	ui::IStashUI& Controller::UI()
	{
		if (!_ui) {
			_ui = ui::CreateStashUI();
		}
		return *_ui;
	}

	std::string Controller::HotkeyName()
	{
		// DirectInput scan codes for the keys people are likely to pick.
		static const std::unordered_map<std::int32_t, std::string_view> names{
			{ 0x02, "1" }, { 0x03, "2" }, { 0x04, "3" }, { 0x05, "4" }, { 0x06, "5" }, { 0x07, "6" }, { 0x08, "7" }, { 0x09, "8" },
			{ 0x0A, "9" }, { 0x0B, "0" }, { 0x10, "Q" }, { 0x11, "W" }, { 0x12, "E" }, { 0x13, "R" }, { 0x14, "T" }, { 0x15, "Y" },
			{ 0x16, "U" }, { 0x17, "I" }, { 0x18, "O" }, { 0x19, "P" }, { 0x1E, "A" }, { 0x1F, "S" }, { 0x20, "D" }, { 0x21, "F" },
			{ 0x22, "G" }, { 0x23, "H" }, { 0x24, "J" }, { 0x25, "K" }, { 0x26, "L" }, { 0x2C, "Z" }, { 0x2D, "X" }, { 0x2E, "C" },
			{ 0x2F, "V" }, { 0x30, "B" }, { 0x31, "N" }, { 0x32, "M" }, { 0x3B, "F1" }, { 0x3C, "F2" }, { 0x3D, "F3" }, { 0x3E, "F4" },
			{ 0x3F, "F5" }, { 0x40, "F6" }, { 0x41, "F7" }, { 0x42, "F8" }, { 0x43, "F9" }, { 0x44, "F10" }, { 0x57, "F11" }, { 0x58, "F12" },
			{ 0xC7, "Home" }, { 0xC9, "Page Up" }, { 0xCF, "End" }, { 0xD1, "Page Down" }, { 0xD2, "Insert" }, { 0xD3, "Delete" },
		};
		const auto code = Settings::GetSingleton().GetInput().hotkey;
		if (const auto it = names.find(code); it != names.end()) {
			return std::string(it->second);
		}
		return std::format("the Dova Stash hotkey ({})", code);
	}

	// --- triggers ------------------------------------------------------------------------------------

	void Controller::OnHotkey(RE::RefHandle a_crosshairTarget)
	{
		std::string reason;
		auto*       home = Registry().Current(&reason);
		if (!home) {
			UI().Notify(std::format("Dova Stash: not a player home ({})", reason));
			return;
		}
		auto* cell = PlayerCell();

		auto  targetHold = game::RefFromHandle(a_crosshairTarget);  // keeps the ref alive for this call
		auto* target = targetHold.get();
		if (target && (!game::IsContainerRef(target) || target->GetParentCell() != cell)) {
			target = nullptr;  // NPCs, doors, items...: behave as if nothing was targeted
		}

		if (target) {
			const auto ref = game::ToFormRef(target);
			if (home->layout && ref && home->layout->IsMaster(*ref)) {
				UI().OpenMasterMenu(*home, target);
				return;
			}
			const auto verdict = game::CheckContainer(target, cell);
			if (!verdict.ok) {
				UI().Notify(std::format("Dova Stash: can't use {} ({})", game::RefName(target), verdict.reason));
				return;
			}
			if (!verdict.warning.empty()) {
				logger::warn("{}", verdict.warning);
			}
			if (home->layoutBroken) {
				UI().Notify("Dova Stash: this home's layout file is broken (see DovaStash.log)");
				return;
			}
			if (!home->layout) {
				UI().OpenSetupPrompt(*home, target);
			} else {
				UI().OpenContainerMenu(*home, target);
			}
			return;
		}

		if (home->layout) {
			RE::TESObjectREFR* master = nullptr;
			if (home->layout->master) {
				master = game::LookupRef(*home->layout->master);
				if (master && !game::CheckContainer(master, cell).ok) {
					master = nullptr;
				}
			}
			UI().OpenMasterMenu(*home, master);
			return;
		}
		if (home->layoutBroken) {
			UI().Notify("Dova Stash: this home's layout file is broken (see DovaStash.log)");
			return;
		}
		if (home->BestTemplate()) {
			UI().OpenSetupPrompt(*home, nullptr);
			return;
		}
		UI().Notify(std::format("Dova Stash: look at a container and press {} to set up {}", HotkeyName(), home->name));
	}

	void Controller::OnCellMaybeChanged()
	{
		UI().OnHomeChanged();
		auto* home = Registry().Current();
		if (!home || home->layout || home->layoutBroken) {
			return;
		}
		const auto* best = home->BestTemplate();
		if (!best || !Registry().MarkNotified(home->cellId)) {
			return;
		}
		const auto name = best->layout.name;
		const auto author = best->layout.author.empty() ? "unknown"s : best->layout.author;

		if (Settings::GetSingleton().GetTemplates().autoEnable) {
			if (Registry().ApplyBestTemplate(*home)) {
				UI().Notify(std::format("Dova Stash: {} layout enabled (by {})", name, author));
			}
			return;
		}
		UI().Notify(std::format("Dova Stash: {} layout available (by {}). Press {} at any container to enable.", name, author, HotkeyName()));
	}

	void Controller::OnContainerMenuOpened(RE::FormID a_target)
	{
		_openContainer = a_target;
		logger::debug("ContainerMenu opened on {:08X}", a_target);
	}

	void Controller::OnContainerMenuClosed()
	{
		const auto closed = _openContainer;
		_openContainer = 0;
		logger::debug("ContainerMenu closed ({:08X})", closed);

		Registry().Flush();  // debounced learned destinations
		UI().OnHomeChanged();

		if (!closed || !Settings::GetSingleton().GetSorting().distributeOnMasterClose) {
			return;
		}
		auto* home = Registry().Current();
		if (!home || !home->layout || !home->layout->master) {
			return;
		}
		auto* master = game::LookupRef(*home->layout->master);
		if (master && master->GetFormID() == closed) {
			RunOperation(home->cellId, sort::Op::Distribute);
		}
	}

	void Controller::OnItemStored(RE::FormID a_container, RE::FormID a_baseObject)
	{
		if (!Settings::GetSingleton().GetSorting().learn) {
			return;
		}
		// Only learn hand placements: the ContainerMenu must be open on exactly this container.
		if (_openContainer == 0 || _openContainer != a_container) {
			return;
		}
		auto* home = Registry().Current();
		if (!home || !home->layout) {
			return;
		}
		auto* containerForm = RE::TESForm::LookupByID(a_container);
		auto* ref = containerForm ? containerForm->AsReference() : nullptr;
		auto* cell = PlayerCell();
		const auto verdict = game::CheckContainer(ref, cell);
		if (!verdict.ok) {
			logger::debug("Learn: {:08X} not usable ({})", a_container, verdict.reason);
			return;
		}
		const auto containerRef = game::ToFormRef(ref);
		if (!containerRef || home->layout->IsMaster(*containerRef)) {
			return;
		}
		// Scripted containers (bookshelves, display cases...) may reject or hand back items: never add
		// one to the layout automatically. Learning into one the player bound on purpose is fine.
		if (verdict.scripted && !home->layout->FindContainer(*containerRef)) {
			logger::info("Learn: {} not added ({})", game::Describe(ref), verdict.warning);
			return;
		}

		auto* baseForm = RE::TESForm::LookupByID(a_baseObject);
		auto* object = baseForm ? baseForm->As<RE::TESBoundObject>() : nullptr;
		core::ItemInfo info;
		if (!object || !game::FillBaseInfo(info, object) || !info.form.IsValid()) {
			return;  // not a storable type, or a dynamic form (cannot be persisted)
		}
		info.count = 1;

		const bool wasManaged = home->layout->FindContainer(*containerRef) != nullptr;
		const auto result = core::Learn(*home->layout, info, *containerRef, home::Store::Get().Categories());
		logger::info("Learn: {} -> {}: {}", game::Describe(object), game::Describe(ref), LearnResultName(result));

		switch (result) {
		case core::LearnResult::Recorded:
		case core::LearnResult::Updated:
		case core::LearnResult::Removed:
			if (!wasManaged) {
				if (auto* binding = home->layout->FindContainer(*containerRef); binding && binding->label.empty()) {
					binding->label = game::RefName(ref);
				}
				logger::info("Learn: {} is now a managed container", game::Describe(ref));
				if (!verdict.warning.empty()) {
					logger::warn("{}", verdict.warning);
				}
			}
			Registry().MarkDirty(*home);  // written when the container menu closes
			break;
		default:
			break;
		}
	}

	void Controller::OnSaveGame()
	{
		Registry().Flush();
	}

	// --- actions -------------------------------------------------------------------------------------

	void Controller::RunWhenContainerMenuClosed(std::function<void()> a_fn, int a_retries)
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui || !ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME)) {
			a_fn();
			return;
		}
		if (a_retries <= 0) {
			logger::warn("Operation dropped: a ContainerMenu stayed open");
			UI().Notify("Dova Stash: close the container first");
			return;
		}
		// The SKSE task queue drains until empty, so re-queueing directly would spin within one frame.
		std::thread([this, fn = std::move(a_fn), a_retries]() mutable {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			SKSE::GetTaskInterface()->AddTask([this, fn = std::move(fn), a_retries]() mutable {
				RunWhenContainerMenuClosed(std::move(fn), a_retries - 1);
			});
		}).detach();
	}

	void Controller::RunOperation(RE::FormID a_cellId, sort::Op a_op, std::string a_pattern)
	{
		RunWhenContainerMenuClosed([this, a_cellId, a_op, pattern = std::move(a_pattern)]() {
			auto* home = Registry().CurrentIf(a_cellId);
			if (!home) {
				UI().Notify("Dova Stash: you are no longer in that home");
				return;
			}
			const auto result = sort::Run(*home, a_op, pattern);
			Registry().Flush();
			if (!result.message.empty()) {
				UI().Notify(result.message);
			}
		});
	}

	RE::TESObjectREFR* Controller::UsableContainer(home::Home& a_home, const core::FormRef& a_container)
	{
		auto*      ref = game::LookupRef(a_container);
		const auto verdict = game::CheckContainer(ref, PlayerCell());
		if (!verdict.ok) {
			UI().Notify(std::format("Dova Stash: can't use this container ({})", verdict.reason));
			return nullptr;
		}
		if (!verdict.warning.empty()) {
			logger::warn("{} (bound manually in {})", verdict.warning, a_home.name);
		}
		return ref;
	}

	core::ContainerBinding* Controller::EnsureBinding(home::Home& a_home, const core::FormRef& a_container)
	{
		if (!a_home.layout || a_home.layout->IsMaster(a_container)) {
			return nullptr;
		}
		if (auto* binding = a_home.layout->FindContainer(a_container)) {
			return binding;
		}
		core::ContainerBinding binding;
		binding.ref = a_container;
		binding.label = game::RefName(game::LookupRef(a_container));
		a_home.layout->containers.push_back(std::move(binding));
		return &a_home.layout->containers.back();
	}

	void Controller::MakeMaster(RE::FormID a_cellId, const core::FormRef& a_container)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home) {
			return;
		}
		if (home->layoutBroken) {
			UI().Notify("Dova Stash: this home's layout file is broken (see DovaStash.log)");
			return;
		}
		auto* ref = UsableContainer(*home, a_container);
		if (!ref) {
			return;
		}
		auto& layout = Registry().EnsureLayout(*home);
		std::erase_if(layout.containers, [&](const core::ContainerBinding& c) { return c.ref == a_container; });
		std::erase_if(layout.learned, [&](const auto& a_pair) { return a_pair.second == a_container; });
		layout.master = a_container;
		Registry().Save(*home);
		logger::info("Master of {} is now {}", home->name, game::Describe(ref));
		UI().Notify(std::format("Dova Stash: {} is now the master container", game::RefName(ref)));
	}

	void Controller::UseTemplate(RE::FormID a_cellId)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home || !home->BestTemplate()) {
			return;
		}
		if (home->layoutBroken) {
			UI().Notify("Dova Stash: this home's layout file is broken (see DovaStash.log)");
			return;
		}
		const auto name = home->BestTemplate()->layout.name;
		if (!Registry().ApplyBestTemplate(*home)) {
			UI().Notify("Dova Stash: could not apply the template (see log)");
			return;
		}

		// Report bindings that fail the safety checks right now (disabled Hearthfire furniture, etc.).
		int   unavailable = 0;
		auto* cell = PlayerCell();
		auto  check = [&](const core::FormRef& a_ref) {
			const auto verdict = game::CheckContainer(game::LookupRef(a_ref), cell);
			if (!verdict.ok) {
				++unavailable;
				logger::warn("Template container {} unavailable: {}", a_ref.ToString(), verdict.reason);
			}
		};
		if (home->layout->master) {
			check(*home->layout->master);
		}
		for (const auto& c : home->layout->containers) {
			check(c.ref);
		}
		UI().Notify(unavailable == 0 ?
						std::format("Dova Stash: {} layout enabled", name) :
						std::format("Dova Stash: {} layout enabled ({} containers unavailable for now)", name, unavailable));
	}

	void Controller::ResetHome(RE::FormID a_cellId)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home) {
			return;
		}
		if (Registry().Reset(*home)) {
			UI().Notify(std::format("Dova Stash: {} reset (layout backed up)", home->name));
		} else {
			UI().Notify("Dova Stash: reset failed (see log)");
		}
	}

	void Controller::ToggleCategory(RE::FormID a_cellId, const core::FormRef& a_container, const std::string& a_pattern)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home || !UsableContainer(*home, a_container)) {
			return;
		}
		auto* binding = EnsureBinding(*home, a_container);
		if (!binding) {
			return;
		}
		auto& patterns = binding->categories;
		const auto it = std::ranges::find_if(patterns, [&](const std::string& p) { return core::IEquals(p, a_pattern); });
		if (it != patterns.end()) {
			patterns.erase(it);
			logger::info("{}: unbound {}", binding->label, a_pattern);
		} else {
			patterns.push_back(a_pattern);
			logger::info("{}: bound {}", binding->label, a_pattern);
		}
		Registry().Save(*home);
	}

	void Controller::ClearCategories(RE::FormID a_cellId, const core::FormRef& a_container)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		if (auto* binding = home->layout->FindContainer(a_container)) {
			binding->categories.clear();
			Registry().Save(*home);
			UI().Notify(std::format("Dova Stash: categories cleared for {}", binding->label));
		}
	}

	void Controller::SetIgnored(RE::FormID a_cellId, const core::FormRef& a_container, bool a_ignored)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home) {
			return;
		}
		if (!a_ignored) {
			if (auto* binding = home->layout ? home->layout->FindContainer(a_container) : nullptr) {
				binding->ignored = false;
				Registry().Save(*home);
				UI().Notify(std::format("Dova Stash: {} is managed again", binding->label));
			}
			return;
		}
		// Ignoring needs no safety check (it only removes the container from sorting).
		auto* binding = EnsureBinding(*home, a_container);
		if (!binding) {
			return;
		}
		binding->ignored = true;
		Registry().Save(*home);
		UI().Notify(std::format("Dova Stash: {} is now ignored", binding->label));
	}

	// --- editor actions (M4 panel) -------------------------------------------------------------------

	void Controller::UseTemplate(RE::FormID a_cellId, std::string_view a_source)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home) {
			return;
		}
		if (const auto* best = home->BestTemplate(); best && best->source == a_source) {
			UseTemplate(a_cellId);
			return;
		}
		if (home->layoutBroken) {
			UI().Notify("Dova Stash: this home's layout file is broken (see DovaStash.log)");
			return;
		}
		// Only templates the resolver found eligible for this cell (cell match + requirements met).
		const auto                     templates = home::Store::Get().Templates();
		const core::TemplateCandidate* chosen = nullptr;
		for (const auto index : home->resolution.alternatives) {
			if (index < templates.size() && templates[index].source == a_source) {
				chosen = &templates[index];
				break;
			}
		}
		if (!chosen) {
			UI().Notify("Dova Stash: that template is not available for this home");
			return;
		}
		if (!Registry().ApplyTemplate(*home, *chosen)) {
			UI().Notify("Dova Stash: could not apply the template (see log)");
			return;
		}
		const auto unavailable = CountUnavailable(*home->layout, PlayerCell());
		const auto name = chosen->layout.name;
		UI().Notify(unavailable == 0 ?
						std::format("Dova Stash: {} layout enabled", name) :
						std::format("Dova Stash: {} layout enabled ({} containers unavailable for now)", name, unavailable));
	}

	void Controller::SetLabel(RE::FormID a_cellId, const core::FormRef& a_container, std::string a_label)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home || !home->layout || home->layout->IsMaster(a_container)) {
			return;
		}
		auto* binding = home->layout->FindContainer(a_container);
		if (!binding) {
			if (!UsableContainer(*home, a_container)) {
				return;
			}
			binding = EnsureBinding(*home, a_container);
		}
		if (!binding) {
			return;
		}
		auto label = Trim(a_label);
		if (label.empty()) {
			label = game::RefName(game::LookupRef(a_container));
		}
		if (label == binding->label) {
			return;
		}
		logger::info("{}: renamed to {}", binding->label, label);
		binding->label = std::move(label);
		Registry().Save(*home);
	}

	void Controller::ForgetLearned(RE::FormID a_cellId, const core::FormRef& a_container)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		const auto removed = std::erase_if(home->layout->learned, [&](const auto& a_pair) { return a_pair.second == a_container; });
		if (removed == 0) {
			return;
		}
		Registry().Save(*home);
		const auto* binding = home->layout->FindContainer(a_container);
		const auto  label = binding ? binding->label : a_container.ToString();
		logger::info("{}: forgot {} learned destination(s)", label, removed);
		UI().Notify(std::format("Dova Stash: forgot {} learned item{} for {}", removed, removed == 1 ? "" : "s", label));
	}

	void Controller::RemoveLearned(RE::FormID a_cellId, const core::FormRef& a_item)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		if (home->layout->learned.erase(a_item) > 0) {
			logger::info("Learned destination of {} removed", a_item.ToString());
			Registry().Save(*home);
		}
	}

	void Controller::SetKeep(RE::FormID a_cellId, std::optional<core::KeepConfig> a_keep)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		if (home->layoutBroken) {
			UI().Notify("Dova Stash: this home's layout file is broken (see DovaStash.log)");
			return;
		}
		if (!a_keep) {
			home->layout->keep.reset();
			Registry().Save(*home);
			UI().Notify("Dova Stash: keep rules reset to the global defaults");
			return;
		}

		auto keep = std::move(*a_keep);
		auto clean = [](std::vector<std::string>& a_list) {
			std::vector<std::string> out;
			for (auto& entry : a_list) {
				auto text = Trim(entry);
				if (!text.empty() && std::ranges::none_of(out, [&](const std::string& e) { return core::IEquals(e, text); })) {
					out.push_back(std::move(text));
				}
			}
			a_list = std::move(out);
		};
		clean(keep.always);
		clean(keep.never);
		for (auto& q : keep.quotas) {
			if (q.category) {
				*q.category = Trim(*q.category);
				if (q.category->empty()) {
					q.category.reset();
				}
			}
			q.quota = std::max(q.quota, 0);
		}
		std::erase_if(keep.quotas, [](const core::QuotaRule& q) {
			return !q.category && (!q.keywordsAny || q.keywordsAny->empty()) && (!q.forms || q.forms->empty());
		});

		// Round-trip through the JSON schema so what we keep in memory is exactly what loads next time.
		core::Diagnostics               diags;
		std::optional<core::KeepConfig> parsed;
		try {
			parsed = core::ParseKeepConfig(nlohmann::json::parse(core::KeepConfigToJson(keep).dump()), "keep", diags);
		} catch (const std::exception& e) {
			logger::error("Keep rules for {}: {}", home->name, e.what());
		}
		home::Store::LogDiagnostics(diags, "Keep rules");
		if (!parsed) {
			UI().Notify("Dova Stash: keep rules not saved (see log)");
			return;
		}
		home->layout->keep = std::move(*parsed);
		if (!Registry().Save(*home)) {
			UI().Notify("Dova Stash: keep rules not saved (see log)");
			return;
		}
		core::Diagnostics catDiags;
		core::ValidateLayoutCategories(*home->layout, home::Store::Get().Categories(), home->name, catDiags);
		home::Store::LogDiagnostics(catDiags, "Keep rules");
		const auto warnings = diags.Items().size() + catDiags.Items().size();
		UI().Notify(warnings == 0 ? "Dova Stash: keep rules saved"s :
									std::format("Dova Stash: keep rules saved ({} warning{}, see log)", warnings, warnings == 1 ? "" : "s"));
	}

	Controller::ExportResult Controller::ExportTemplate(RE::FormID a_cellId, const ExportRequest& a_request)
	{
		ExportResult result;
		auto*        home = Registry().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			result.message = "You are no longer in that home, or it is not set up.";
			return result;
		}
		const auto name = Trim(a_request.name);
		if (name.empty()) {
			result.message = "Enter a template name.";
			return result;
		}
		auto version = Trim(a_request.version);
		if (version.empty()) {
			version = "1.0.0";
		}

		const auto stem = TemplateFileStem(name);
		const auto source = std::format("Templates/User/{}.json", stem);
		const auto path = std::filesystem::path{ home::Store::kRoot } / "Templates" / "User" / (stem + ".json");
		result.path = path.generic_string();

		std::error_code ec;
		if (!a_request.overwrite && std::filesystem::exists(path, ec)) {
			result.status = ExportResult::Status::Exists;
			result.message = std::format("{} already exists.", result.path);
			return result;
		}

		auto tpl = core::ExportAsTemplate(*home->layout);
		tpl.name = name;
		tpl.author = Trim(a_request.author);
		tpl.version = version;
		tpl.cell = home->cell;
		AddReferencedPlugins(tpl, tpl.requiredPlugins);

		bool ok = false;
		try {
			ok = core::SaveLayoutFile(path, tpl);
		} catch (const std::exception& e) {
			logger::error("Template export to {} failed: {}", result.path, e.what());
		}
		if (!ok) {
			logger::error("Template export to {} failed", result.path);
			result.message = std::format("Could not write {} (see log).", result.path);
			return result;
		}

		core::Diagnostics diags;
		core::ValidateTemplate(tpl, source, diags);
		core::ValidateLayoutCategories(tpl, home::Store::Get().Categories(), source, diags);
		home::Store::LogDiagnostics(diags, "Template export");
		for (const auto& d : diags.Items()) {
			result.warnings.push_back(d.ToString());
		}
		logger::info("Exported {} as template {} v{} to {}", home->name, tpl.name, tpl.version, result.path);
		result.status = ExportResult::Status::Ok;
		result.message = std::format("Exported to {}. It is offered from the next game start.", result.path);
		UI().Notify(std::format("Dova Stash: exported template \"{}\"", tpl.name));
		return result;
	}
}
