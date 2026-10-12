#include "Controller.h"

#include "Game/Eligibility.h"
#include "Game/FormUtil.h"
#include "Game/ItemAdapter.h"
#include "Game/ItemMover.h"
#include "Home/Store.h"
#include "Input/InputHandler.h"
#include "Integrations/QuickLoot.h"
#include "Log.h"
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

		// Runs `a_fn` on the main thread after `a_delay` (the SKSE task queue drains in one frame, so a
		// delay needs a helper thread).
		void Later(std::chrono::milliseconds a_delay, std::function<void()> a_fn)
		{
			std::thread([a_delay, fn = std::move(a_fn)]() mutable {
				std::this_thread::sleep_for(a_delay);
				SKSE::GetTaskInterface()->AddTask(std::move(fn));
			}).detach();
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

		// Trims and de-duplicates entries, clamps quotas, drops selector-less quotas, then round-trips the
		// result through the JSON schema so what we keep in memory is exactly what loads next time.
		std::optional<core::KeepConfig> NormalizeKeep(core::KeepConfig a_keep, core::Diagnostics& a_diags)
		{
			auto keep = std::move(a_keep);
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
			try {
				return core::ParseKeepConfig(nlohmann::json::parse(core::KeepConfigToJson(keep).dump()), "keep", a_diags);
			} catch (const std::exception& e) {
				logger::error("Keep rules: {}", e.what());
				return std::nullopt;
			}
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
				if (!c.ignored) {  // ignored bindings (e.g. respawning barrels a template marks off-limits) never hold items
					check(c.ref);
				}
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
		EnsureUI();
	}

	void Controller::ApplySettings(Settings::Values a_values)
	{
		const auto before = Settings::GetSingleton().Get();
		const auto changed = Settings::GetSingleton().Update(std::move(a_values));
		if (changed.empty()) {
			return;
		}
		const auto& now = Settings::GetSingleton();
		if (now.GetGeneral().logLevel != before.general.logLevel) {
			log::SetLevel(now.GetGeneral().logLevel);
		}
		if (now.GetUI().mode != before.ui.mode) {
			// Menu callbacks still in flight (an open list, a message box) point at the old UI objects:
			// keep them alive for the rest of the session (a few small objects per mode change).
			static std::vector<ui::StashUIs> retired;
			retired.push_back(std::move(_ui));
			_ui = ui::CreateStashUIs();
			logger::info("UI rebuilt for iMode={}", now.GetUI().mode);
		}
		integrations::quickloot::Refresh();
		UI().OnHomeChanged();
		if (&Editor() != &UI()) {
			Editor().OnHomeChanged();
		}
	}

	void Controller::EnsureUI()
	{
		if (!_ui.world) {
			_ui = ui::CreateStashUIs();
		}
	}

	ui::IStashUI& Controller::UI()
	{
		EnsureUI();
		return *_ui.world;
	}

	ui::IStashUI& Controller::Editor()
	{
		EnsureUI();
		return _ui.editor ? *_ui.editor : *_ui.world;
	}

	void Controller::Notify(std::string_view a_message)
	{
		auto& editor = Editor();
		if (&editor != &UI() && editor.IsActive()) {
			editor.Notify(a_message);
		} else {
			UI().Notify(a_message);
		}
	}

	bool Controller::SaveLayout(home::Home& a_home)
	{
		const bool ok = Registry().Save(a_home);
		integrations::quickloot::Refresh();
		return ok;
	}

	std::string Controller::HotkeyName()
	{
		return input::KeyName(Settings::GetSingleton().GetInput().hotkey);
	}

	// --- triggers ------------------------------------------------------------------------------------

	void Controller::OnHotkey(RE::RefHandle a_crosshairTarget)
	{
		std::string reason;
		auto*       home = Registry().Current(&reason);
		if (!home) {
			Notify(std::format("Dova Stash: not a player home ({})", reason));
			return;
		}
		auto* cell = PlayerCell();

		auto  targetHold = game::RefFromHandle(a_crosshairTarget);  // keeps the ref alive for this call
		auto* target = targetHold.get();
		if (target && target->GetParentCell() == cell) {
			// A shelf or rack that opens a hidden chest stands for that chest.
			if (auto* behind = game::ContainerBehind(target); behind && behind != target) {
				logger::debug("Hotkey: {} opens {}", game::Describe(target), game::Describe(behind));
				target = behind;
			}
		}
		if (target && (!game::IsContainerRef(target) || target->GetParentCell() != cell)) {
			target = nullptr;  // NPCs, doors, items...: behave as if nothing was targeted
		}

		logger::debug("Hotkey in {} (layout: {}), target {}", home->name, home->layout ? "yes" : "no", target ? game::Describe(target) : "none"s);
		if (target) {
			const auto ref = game::ToFormRef(target);
			if (home->layout && ref && home->layout->IsMaster(*ref)) {
				logger::debug("Hotkey -> master menu");
				UI().OpenMasterMenu(*home, target);
				return;
			}
			const auto verdict = game::CheckContainer(target, cell);
			if (!verdict.ok) {
				Notify(std::format("Dova Stash: can't use {} ({})", game::RefName(target), verdict.reason));
				return;
			}
			if (!verdict.warning.empty()) {
				if (log::FirstTime(std::format("warn:{:08X}", target->GetFormID()))) {
					logger::warn("{}: {}", game::Describe(target), verdict.warning);
				} else {
					logger::debug("{}: {}", game::Describe(target), verdict.warning);
				}
			}
			if (home->layoutBroken) {
				Notify("Dova Stash: this home's layout file is broken (see DovaStash.log)");
				return;
			}
			if (!home->layout) {
				logger::debug("Hotkey -> setup prompt");
				UI().OpenSetupPrompt(*home, target);
			} else {
				logger::debug("Hotkey -> container menu");
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
			Notify("Dova Stash: this home's layout file is broken (see DovaStash.log)");
			return;
		}
		if (home->BestTemplate()) {
			UI().OpenSetupPrompt(*home, nullptr);
			return;
		}
		Notify(std::format("Dova Stash: look at a container and press {} to set up {}", HotkeyName(), home->name));
	}

	void Controller::OnCellMaybeChanged()
	{
		UI().OnHomeChanged();
		if (&Editor() != &UI()) {
			Editor().OnHomeChanged();
		}
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
				Notify(std::format("Dova Stash: {} layout enabled (by {})", name, author));
			}
			return;
		}
		Notify(std::format("Dova Stash: {} layout available (by {}). Press {} at any container to enable.", name, author, HotkeyName()));
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
		if (&Editor() != &UI()) {
			Editor().OnHomeChanged();
		}

		if (const auto deferred = std::exchange(_arrivalDeferredCell, 0)) {
			// The items that arrived while the paused menu was open. The menu can still be in its close
			// animation here, so wait until it is really gone.
			RunWhenContainerMenuClosed([this, deferred]() { SortArrivals(deferred); });
		}
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

	bool Controller::OnMasterArrival(RE::FormID a_container)
	{
		auto* home = Registry().Current();
		if (!home || !home->layout || !home->layout->master) {
			return false;
		}
		auto* master = game::LookupRef(*home->layout->master);
		if (!master || master->GetFormID() != a_container) {
			return false;
		}
		if (!Settings::GetSingleton().GetSorting().sortOnArrival) {
			return true;
		}
		// Debounced: dragging ten items in sorts once, ~0.4 s after the last one.
		const auto serial = ++_arrivalSerial;
		const auto cellId = home->cellId;
		Later(std::chrono::milliseconds(400), [this, serial, cellId]() {
			if (serial == _arrivalSerial) {
				SortArrivals(cellId);
			}
		});
		return true;
	}

	void Controller::SortArrivals(RE::FormID a_cellId)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home || !home->layout || !home->layout->master) {
			return;
		}
		// DESIGN §8.2: never move items out of a container while its menu shows them, unless the menu does
		// not pause the game (Skyrim Souls), where waiting for the close would defeat the point. Paused:
		// defer to the close.
		auto* ui = RE::UI::GetSingleton();
		if (ui && ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME) && ui->GameIsPaused()) {
			if (_arrivalDeferredCell != a_cellId) {
				logger::debug("Sort on arrival: container menu open and the game is paused, sorting when it closes");
			}
			_arrivalDeferredCell = a_cellId;
			return;
		}
		_arrivalDeferredCell = 0;
		// Unpaused menu (Skyrim Souls): every move is still validated against the live inventory by the
		// ItemMover, and the open menus are told to rebuild their lists afterwards.
		logger::info("Sort on arrival in {}", home->name);
		const auto result = sort::Run(*home, sort::Op::Distribute);
		Registry().Flush();
		if (result.items <= 0) {
			return;
		}
		if (auto* master = game::LookupRef(*home->layout->master)) {
			RE::SendUIMessage::SendInventoryUpdateMessage(master, nullptr);
		}
		if (auto* player = game::Player()) {
			RE::SendUIMessage::SendInventoryUpdateMessage(player, nullptr);
		}
		integrations::quickloot::Refresh();
		Notify(result.message);
	}

	void Controller::ReportLearned(const std::string& a_container, const std::string& a_item)
	{
		_learnBatch[a_container].push_back(a_item);
		const auto serial = ++_learnSerial;
		Later(std::chrono::milliseconds(700), [this, serial]() {
			if (serial != _learnSerial) {
				return;
			}
			for (const auto& [container, items] : _learnBatch) {
				Notify(items.size() == 1 ?
						   std::format("Dova Stash: {} now goes to {}", items.front(), container) :
						   std::format("Dova Stash: {} items now go to {} ({}, ...)", items.size(), container, items.front()));
			}
			_learnBatch.clear();
		});
	}

	void Controller::OnItemReturned(RE::FormID a_container, RE::FormID a_baseObject)
	{
		if (_openContainer == a_container) {
			return;  // the player took it out by hand
		}
		if (game::ItemMover::NoteReturned(a_baseObject, a_container)) {
			logger::info("{} came back from {} right after it was stored (a script returns it); it stays where it is from now on",
				game::Describe(RE::TESForm::LookupByID(a_baseObject)), game::Describe(RE::TESForm::LookupByID(a_container)));
		}
	}

	void Controller::OnItemStored(RE::FormID a_container, RE::FormID a_baseObject)
	{
		if (OnMasterArrival(a_container)) {
			return;  // the master is an inbox: never learned, sorted on arrival
		}
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
			if (log::FirstTime(std::format("learn-scripted:{:08X}", ref->GetFormID()))) {
				logger::info("Learn: {} not added automatically ({}). Bind it from the menu to use it anyway.", game::Describe(ref), verdict.warning);
			} else {
				logger::debug("Learn: {} not added ({})", game::Describe(ref), verdict.warning);
			}
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
			if (result != core::LearnResult::Removed) {
				const auto* binding = home->layout->FindContainer(*containerRef);
				ReportLearned(binding && !binding->label.empty() ? binding->label : game::RefName(ref), info.name.empty() ? game::Describe(object) : info.name);
			}
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
			Notify("Dova Stash: close the container first");
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
				Notify("Dova Stash: you are no longer in that home");
				return;
			}
			const auto result = sort::Run(*home, a_op, pattern);
			Registry().Flush();
			integrations::quickloot::Refresh();
			if (!result.message.empty()) {
				Notify(result.message);
			}
		});
	}

	RE::TESObjectREFR* Controller::UsableContainer(home::Home& a_home, const core::FormRef& a_container)
	{
		auto*      ref = game::LookupRef(a_container);
		const auto verdict = game::CheckContainer(ref, PlayerCell());
		if (!verdict.ok) {
			Notify(std::format("Dova Stash: can't use this container ({})", verdict.reason));
			return nullptr;
		}
		if (!verdict.warning.empty()) {
			if (log::FirstTime(std::format("warn:{:08X}", ref->GetFormID()))) {
				logger::warn("{}: {} (bound manually in {})", game::Describe(ref), verdict.warning, a_home.name);
			} else {
				logger::debug("{}: {} (bound manually in {})", game::Describe(ref), verdict.warning, a_home.name);
			}
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
			Notify("Dova Stash: this home's layout file is broken (see DovaStash.log)");
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
		SaveLayout(*home);
		logger::info("Master of {} is now {}", home->name, game::Describe(ref));
		Notify(std::format("Dova Stash: {} is now the master container", game::RefName(ref)));
	}

	void Controller::UseTemplate(RE::FormID a_cellId)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home || !home->BestTemplate()) {
			return;
		}
		if (home->layoutBroken) {
			Notify("Dova Stash: this home's layout file is broken (see DovaStash.log)");
			return;
		}
		const auto name = home->BestTemplate()->layout.name;
		if (!Registry().ApplyBestTemplate(*home)) {
			Notify("Dova Stash: could not apply the template (see log)");
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
			if (!c.ignored) {  // ignored bindings never hold items
				check(c.ref);
			}
		}
		Notify(unavailable == 0 ?
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
			Notify(std::format("Dova Stash: {} reset (layout backed up)", home->name));
		} else {
			Notify("Dova Stash: reset failed (see log)");
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
		SaveLayout(*home);
	}

	void Controller::ClearCategories(RE::FormID a_cellId, const core::FormRef& a_container)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		if (auto* binding = home->layout->FindContainer(a_container)) {
			binding->categories.clear();
			SaveLayout(*home);
			Notify(std::format("Dova Stash: categories cleared for {}", binding->label));
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
				SaveLayout(*home);
				Notify(std::format("Dova Stash: {} is managed again", binding->label));
			}
			return;
		}
		// Ignoring needs no safety check (it only removes the container from sorting).
		auto* binding = EnsureBinding(*home, a_container);
		if (!binding) {
			return;
		}
		binding->ignored = true;
		SaveLayout(*home);
		Notify(std::format("Dova Stash: {} is now ignored", binding->label));
	}

	void Controller::Browse(RE::FormID a_cellId, const core::FormRef& a_container)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home) {
			return;
		}
		auto* ref = UsableContainer(*home, a_container);
		auto* player = game::Player();
		if (!ref || !player) {
			return;
		}
		// Default activation of a container by the player opens its ContainerMenu, exactly as if the
		// player had walked up to it.
		RunWhenContainerMenuClosed([ref = RE::NiPointer<RE::TESObjectREFR>(ref), player]() {
			ref->ActivateRef(player, 0, nullptr, 0, false);
		});
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
			Notify("Dova Stash: this home's layout file is broken (see DovaStash.log)");
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
			Notify("Dova Stash: that template is not available for this home");
			return;
		}
		if (!Registry().ApplyTemplate(*home, *chosen)) {
			Notify("Dova Stash: could not apply the template (see log)");
			return;
		}
		const auto unavailable = CountUnavailable(*home->layout, PlayerCell());
		const auto name = chosen->layout.name;
		Notify(unavailable == 0 ?
						std::format("Dova Stash: {} layout enabled", name) :
						std::format("Dova Stash: {} layout enabled ({} containers unavailable for now)", name, unavailable));
	}

	void Controller::SetLabel(RE::FormID a_cellId, const core::FormRef& a_container, std::string a_label)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		auto* ref = game::LookupRef(a_container);
		if (!ref) {
			return;
		}

		// The name lives on the reference itself (ExtraTextDisplayData, saved with the game), so every
		// UI that shows the container's name uses it: activate prompt, ContainerMenu title, QuickLoot,
		// our lists. An empty name restores the original one.
		auto label = Trim(a_label);
		if (label.empty()) {
			ref->extraList.RemoveByType(RE::ExtraDataType::kTextDisplayData);
			label = game::RefName(ref);
			logger::info("{}: custom name removed", game::Describe(ref));
		} else {
			ref->SetDisplayName(RE::BSFixedString(label), true);
			logger::info("{}: renamed to \"{}\" in game", game::Describe(ref), label);
		}
		integrations::quickloot::Refresh();

		if (home->layout->IsMaster(a_container)) {
			Notify(std::format("Dova Stash: master container is now \"{}\"", label));
			return;  // the master has no layout label
		}
		auto* binding = home->layout->FindContainer(a_container);
		if (!binding) {
			if (!UsableContainer(*home, a_container)) {
				return;
			}
			binding = EnsureBinding(*home, a_container);
		}
		if (!binding || label == binding->label) {
			return;
		}
		binding->label = label;
		SaveLayout(*home);
		Notify(std::format("Dova Stash: renamed to \"{}\"", label));
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
		SaveLayout(*home);
		const auto* binding = home->layout->FindContainer(a_container);
		const auto  label = binding ? binding->label : a_container.ToString();
		logger::info("{}: forgot {} learned destination(s)", label, removed);
		Notify(std::format("Dova Stash: forgot {} learned item{} for {}", removed, removed == 1 ? "" : "s", label));
	}

	void Controller::RemoveLearned(RE::FormID a_cellId, const core::FormRef& a_item)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		if (home->layout->learned.erase(a_item) > 0) {
			logger::info("Learned destination of {} removed", a_item.ToString());
			SaveLayout(*home);
		}
	}

	void Controller::SetKeep(RE::FormID a_cellId, std::optional<core::KeepConfig> a_keep)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		if (home->layoutBroken) {
			Notify("Dova Stash: this home's layout file is broken (see DovaStash.log)");
			return;
		}
		if (!a_keep) {
			home->layout->keep.reset();
			SaveLayout(*home);
			Notify("Dova Stash: keep rules reset to the global defaults");
			return;
		}

		core::Diagnostics diags;
		auto              parsed = NormalizeKeep(std::move(*a_keep), diags);
		home::Store::LogDiagnostics(diags, "Keep rules");
		if (!parsed) {
			Notify("Dova Stash: keep rules not saved (see log)");
			return;
		}
		home->layout->keep = std::move(*parsed);
		if (!SaveLayout(*home)) {
			Notify("Dova Stash: keep rules not saved (see log)");
			return;
		}
		core::Diagnostics catDiags;
		core::ValidateLayoutCategories(*home->layout, home::Store::Get().Categories(), home->name, catDiags);
		home::Store::LogDiagnostics(catDiags, "Keep rules");
		const auto warnings = diags.Items().size() + catDiags.Items().size();
		Notify(warnings == 0 ? "Dova Stash: keep rules saved"s :
									std::format("Dova Stash: keep rules saved ({} warning{}, see log)", warnings, warnings == 1 ? "" : "s"));
	}

	void Controller::SetGlobalKeep(core::KeepConfig a_keep)
	{
		core::Diagnostics diags;
		auto              parsed = NormalizeKeep(std::move(a_keep), diags);
		home::Store::LogDiagnostics(diags, "Keep defaults");
		if (!parsed || !home::Store::Get().SaveGlobalKeep(std::move(*parsed))) {
			Notify("Dova Stash: default keep rules not saved (see log)");
			return;
		}
		Notify("Dova Stash: default keep rules saved (all homes)");
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
		Notify(std::format("Dova Stash: exported template \"{}\"", tpl.name));
		return result;
	}
}
