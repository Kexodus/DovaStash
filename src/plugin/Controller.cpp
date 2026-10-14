#include "Controller.h"

#include "Game/ArmorIndex.h"
#include "Game/Displays.h"
#include "Game/Eligibility.h"
#include "Game/FormUtil.h"
#include "Game/ItemAdapter.h"
#include "Game/ItemMover.h"
#include "Home/Store.h"
#include "Home/TemplateDisplays.h"
#include "Input/InputHandler.h"
#include "Integrations/QuickLoot.h"
#include "Log.h"
#include "Settings.h"
#include "Sort/SellChest.h"
#include "Sort/CraftingStorage.h"
#include "UI/AreaPrompt.h"
#include "UI/Labels.h"
#include "UI/MessageBox.h"

#include <thread>

namespace dova
{
	namespace
	{
		// How far in front of the player a display looked at counts when the crosshair names nothing (~3.5 m;
		// the game's own activation reach is about 2.5 m).
		constexpr float kDisplayReach = 250.0f;

		// A display's name for menus and notices: its own, else what it is ("Mannequin": Whitepeak Tower's and
		// many home mods' mannequins have no name, and the form id means nothing to a player).
		std::string DisplayName(RE::TESObjectREFR* a_ref, core::DisplayKind a_kind)
		{
			const char* full = a_ref ? a_ref->GetDisplayFullName() : nullptr;
			const auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
			const char* baseName = base ? base->GetName() : nullptr;
			if ((full && *full) || (baseName && *baseName)) {
				return game::RefName(a_ref);
			}
			switch (a_kind) {
			case core::DisplayKind::Mannequin:
				return "Mannequin";
			case core::DisplayKind::Rack:
				return "Weapon rack";
			case core::DisplayKind::Bookshelf:
				return "Bookshelf";
			case core::DisplayKind::Case:
				return "Display case";
			default:
				return "Display";
			}
		}

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

		// Writes what each used item plaque and display case is made for (its script's items) into its layout
		// entry, so the layout shows and keeps exactly what goes on it, and reports the template's display items
		// that don't exist in this load order (DESIGN §8.4; home::RecordDisplayItems). True if any changed.
		bool RecordDisplayItems(core::Layout& a_layout)
		{
			return home::RecordDisplayItems(a_layout);
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

		// Text that can go into a JSON file: nlohmann::json refuses to write invalid UTF-8 (a name typed in
		// a legacy code page), which would fail the whole export.
		bool IsValidUtf8(std::string_view a_text)
		{
			for (std::size_t i = 0; i < a_text.size();) {
				const auto  c = static_cast<unsigned char>(a_text[i]);
				std::size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
				if (n == 0 || (n == 2 && c < 0xC2) || i + n > a_text.size()) {
					return false;
				}
				for (std::size_t k = 1; k < n; ++k) {
					if ((static_cast<unsigned char>(a_text[i + k]) & 0xC0) != 0x80) {
						return false;
					}
				}
				i += n;
			}
			return true;
		}

		// The player character's name, the default author of an exported template.
		std::string PlayerName()
		{
			auto*       player = game::Player();
			const char* name = player ? player->GetName() : nullptr;
			std::string text = name ? Trim(name) : std::string{};
			return !text.empty() && IsValidUtf8(text) ? text : "Player"s;
		}

		// Bindings of the home's layout that fail the safety checks right now, or are out of reach from the
		// player's area of a multi-cell home (logged).
		int CountUnavailable(const home::Home& a_home)
		{
			const auto& layout = *a_home.layout;
			int         unavailable = 0;
			auto        check = [&](const core::FormRef& a_ref) {
				const auto verdict = home::CheckHomeContainer(game::LookupRef(a_ref), a_home);
				if (!verdict.ok) {
					++unavailable;
					logger::warn("Template container {} unavailable: {}", a_ref.ToString(), verdict.reason);
				}
			};
			if (layout.master) {
				check(*layout.master);
			}
			for (const auto& c : layout.containers) {
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
		if (now.GetSorting().craftingSupply != before.sorting.craftingSupply) {
			sort::crafting::ResolveMode("setting changed");  // the next station visit uses it
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
		if (TryUndoLearn()) {
			return;  // within the undo window after a learn: the press means "undo" (DESIGN §8.6)
		}
		std::string reason;
		auto*       home = Registry().Current(&reason);
		if (!home) {
			// Just outside a home (its door, the next exterior cell): this area can become part of it (DESIGN §6.7).
			if (const auto offer = Registry().OfferArea()) {
				OpenAreaPrompt(*offer);
				return;
			}
			Notify(std::format("Dova Stash: not a player home ({})", reason));
			return;
		}
		auto* cell = PlayerCell();

		auto  targetHold = game::RefFromHandle(a_crosshairTarget);  // keeps the ref alive for this call
		auto* target = targetHold.get();
		const bool targetHere = target && target->GetParentCell() == cell;
		if (targetHere) {
			if (OpenDisplayMenu(*home, target)) {
				return;
			}
			// A shelf or rack that opens a hidden chest stands for that chest.
			if (auto* behind = game::ContainerBehind(target); behind && behind != target) {
				logger::debug("Hotkey: {} opens {}", game::Describe(target), game::Describe(behind));
				target = behind;
			} else if (auto* shelf = game::BookshelfAt(target); shelf && shelf != target) {
				logger::debug("Hotkey: {} stands for the bookshelf {} (used for storage)", game::Describe(target), game::Describe(shelf));
				target = shelf;
			}
		}
		// A container across the border of the next exterior cell counts when that cell is a loaded area of this
		// home (DESIGN §6.7); displays and fronts only in the player's own cell. A single-cell home: as before.
		const bool targetInHome = target && game::IsContainerRef(target) &&
		                          (targetHere || (home->MultiCell() && home::AreaOf(*home, target) && target->GetParentCell() && target->GetParentCell()->IsAttached()));
		if (target && !targetInHome) {
			target = nullptr;  // NPCs, doors, items, containers elsewhere: behave as if nothing was targeted
		}

		logger::debug("Hotkey in {} (layout: {}), target {}", home->name, home->layout ? "yes" : "no", target ? game::Describe(target) : "none"s);
		if (!target && cell) {
			// Nothing in the crosshair: an unnamed mannequin shows no prompt, so the crosshair never names it. The
			// display the player looks at, within reach, gets its menu instead of the master's.
			if (auto* seen = game::DisplayInView(cell, kDisplayReach)) {
				logger::debug("Hotkey: looking at {}", game::Describe(seen));
				if (OpenDisplayMenu(*home, seen)) {
					return;
				}
			}
		}
		if (target) {
			const auto ref = game::ToFormRef(target);
			if (home->layout && ref && home->layout->IsMaster(*ref)) {
				logger::debug("Hotkey -> master menu");
				UI().OpenMasterMenu(*home, target);
				return;
			}
			const auto verdict = targetHere ? game::CheckContainer(target, cell) : home::CheckHomeContainer(target, *home);
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
				Notify("Dova Stash: this home's setup file is broken (see DovaStash.log)");
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
				if (master && !home::CheckHomeContainer(master, *home).ok) {  // in another area of the home: when reachable
					master = nullptr;
				}
			}
			UI().OpenMasterMenu(*home, master);
			return;
		}
		if (home->layoutBroken) {
			Notify("Dova Stash: this home's setup file is broken (see DovaStash.log)");
			return;
		}
		if (home->BestTemplate() || home->backup || Registry().OfferArea()) {
			UI().OpenSetupPrompt(*home, nullptr);
			return;
		}
		Notify(std::format("Dova Stash: look at a container and press {} to set up {}", HotkeyName(), home->name));
	}

	void Controller::OpenAreaPrompt(const home::HomeRegistry::AreaOffer& a_offer)
	{
		logger::debug("Hotkey -> add {} to {}?", a_offer.cell.ToString(), a_offer.homeName);
		const auto cellId = a_offer.cellId;
		ui::ShowMessageBox(std::format(ui::kConfirmAddArea, a_offer.homeName), { std::format(ui::kAddArea, a_offer.homeName), "Cancel" },
			[cellId](std::size_t a_button) {
				if (a_button == 0) {
					ui::AddAreaNamed(cellId);  // asks for a name first (UIExtensions text box), then AddArea
				}
			});
	}

	void Controller::AddArea(RE::FormID a_cellId, std::string a_label)
	{
		const auto offer = Registry().OfferArea();
		if (!offer || offer->cellId != a_cellId) {
			Notify("Dova Stash: this area can't be added now");
			return;
		}
		if (!Registry().AddArea(*offer, a_label)) {
			Notify("Dova Stash: could not add this area (see log)");
			return;
		}
		integrations::quickloot::Refresh();
		UI().OnHomeChanged();
		if (&Editor() != &UI()) {
			Editor().OnHomeChanged();
		}
		auto* home = Registry().CurrentIf(a_cellId);  // loads the cell as part of that home: its area has the name
		const auto* area = home ? home->HereArea() : nullptr;
		Notify(area && !Trim(a_label).empty() ? std::format("Dova Stash: {} is now part of {}", area->name, offer->homeName) :
		                                         std::format("Dova Stash: this area is now part of {}", offer->homeName));
	}

	void Controller::RenameArea(RE::FormID a_cellId, const core::FormRef& a_area, std::string a_label)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home) {
			return;
		}
		if (home->layoutBroken) {
			Notify("Dova Stash: this home's setup file is broken (see DovaStash.log)");
			return;
		}
		if (!Registry().RenameArea(*home, a_area, a_label)) {
			Notify("Dova Stash: could not rename that area (see log)");
			return;
		}
		integrations::quickloot::Refresh();
		UI().OnHomeChanged();
		if (&Editor() != &UI()) {
			Editor().OnHomeChanged();
		}
		const auto area = std::ranges::find(home->areas, a_area, &home::Area::cell);
		Notify(std::format("Dova Stash: area renamed to \"{}\"", area != home->areas.end() ? area->name : Trim(a_label)));
	}

	void Controller::RemoveArea(RE::FormID a_cellId, const core::FormRef& a_area)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home) {
			return;
		}
		if (home->layoutBroken) {
			Notify("Dova Stash: this home's setup file is broken (see DovaStash.log)");
			return;
		}
		const auto area = std::ranges::find(home->areas, a_area, &home::Area::cell);
		const auto areaName = area != home->areas.end() ? area->name : a_area.ToString();
		const auto homeName = home->name;
		if (!Registry().RemoveArea(*home, a_area)) {
			Notify("Dova Stash: could not remove that area (see log)");
			return;
		}
		// `home` may be loaded again from here on (the player's cell may have left the home): not used below.
		integrations::quickloot::Refresh();
		UI().OnHomeChanged();
		if (&Editor() != &UI()) {
			Editor().OnHomeChanged();
		}
		Notify(std::format("Dova Stash: {} is no longer part of {}", areaName, homeName));
	}

	bool Controller::OpenDisplayMenu(home::Home& a_home, RE::TESObjectREFR* a_target)
	{
		const auto found = game::DisplayAt(a_target);
		auto*      ref = found ? RE::TESForm::LookupByID<RE::TESObjectREFR>(found->ref) : nullptr;
		const auto display = game::ToFormRef(ref);
		if (!display) {
			return false;
		}
		if ((found->kind == core::DisplayKind::Case || found->kind == core::DisplayKind::Bookshelf) && a_home.layout) {
			// A container the layout uses for storage is a container, not a case or a bookshelf.
			const auto* binding = a_home.layout->FindContainer(*display);
			if (a_home.layout->IsMaster(*display) || (binding && !binding->ignored)) {
				return false;
			}
		}
		if (a_home.layoutBroken) {
			Notify("Dova Stash: this home's setup file is broken (see DovaStash.log)");
			return true;
		}
		if (!a_home.layout) {
			Notify(std::format("Dova Stash: set up {} first: look at a container and press {}", a_home.name, HotkeyName()));
			return true;
		}
		const auto*     binding = a_home.layout->FindDisplay(*display);
		ui::DisplayMenu menu;
		menu.cellId = a_home.cellId;
		menu.ref = *display;
		menu.name = DisplayName(ref, found->kind);
		menu.kind = found->kind;
		menu.fit = game::FitOf(*found);
		menu.used = binding && !binding->ignored;
		menu.full = game::DisplayFullFor(*found, binding);  // a mannequin with a chosen set: while it can take more
		if (binding) {
			menu.categories = binding->categories;
		}
		logger::debug("Hotkey -> display menu: {} ({}, {}, {})", game::Describe(ref), core::DisplayKindName(found->kind), menu.used ? "used" : "not used",
			menu.full ? "full" : "empty");
		UI().OpenDisplayMenu(menu);
		return true;
	}

	void Controller::OnCellMaybeChanged()
	{
		UI().OnHomeChanged();
		if (&Editor() != &UI()) {
			Editor().OnHomeChanged();
		}
		auto* home = Registry().Current();
		if (home && home->layout && !home->layoutBroken && RecordDisplayItems(*home->layout)) {
			SaveLayout(*home);
		}
		if (home && home->layout && !home->layoutBroken) {
			sort::selling::CheckTimed("came home");  // timed selling, when it is on (DESIGN §8.8)
		}
		if (!home || home->layout || home->layoutBroken) {
			return;
		}
		if (home->backup) {
			// The player's own setup comes first: no template is enabled automatically while it can be restored.
			if (Registry().MarkNotified(home->cellId)) {
				logger::debug("{}: no setup file, a backup from {} can be restored", home->name, home->backup->date);
				Notify(std::format("Dova Stash: a saved setup for {} can be restored (press {})", home->name, HotkeyName()));
			}
			return;
		}
		const auto* best = home->BestTemplate();
		if (!best) {
			return;
		}
		// The auto-enable guard (DESIGN §9.1) looks at the template's references, so it waits for them: an interior
		// cell that is not attached yet comes back here with TESCellFullyLoadedEvent.
		const auto* fit = Registry().BestTemplateFit(*home);
		if (!fit && !home->extra) {
			auto* cell = game::Player() ? game::Player()->GetParentCell() : nullptr;
			if (cell && cell->IsInteriorCell() && !cell->IsAttached()) {
				return;
			}
		}
		if (!Registry().MarkNotified(home->cellId)) {
			return;
		}
		const auto name = best->layout.name;
		const auto author = best->layout.author.empty() ? "unknown"s : best->layout.author;

		if (fit && !fit->fits) {
			// Another mod seems to have rebuilt the home: offer the template, never enable it on its own.
			Notify(std::format("Dova Stash: {} setup may not fit this home. Press {} at any container to enable.", name, HotkeyName()));
			return;
		}
		if (Settings::GetSingleton().GetTemplates().autoEnable) {
			if (Registry().ApplyBestTemplate(*home)) {
				if (RecordDisplayItems(*home->layout)) {
					SaveLayout(*home);
				}
				// First visit: the player did nothing yet, so say what to do (the hotkey anywhere opens the master menu).
				Notify(std::format("Dova Stash: {} is set up. Press {} anywhere here to put your loot away.", name, HotkeyName()));
			}
			return;
		}
		Notify(std::format("Dova Stash: {} setup available (by {}). Press {} at any container to enable.", name, author, HotkeyName()));
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
		// The hotkey is blocked while the menu is open: the undo window (re)starts now that it can be pressed,
		// and the category question waits for that window to pass (never during a sort, never over the menu).
		if (_undo && !_undo->learns.empty()) {
			OpenUndoWindow();
		}
		if (_suggestion) {
			ScheduleSuggestion();
		}

		if (const auto deferred = std::exchange(_arrivalDeferredCell, 0)) {
			// The items that arrived while the paused menu was open. The menu can still be in its close
			// animation here, so wait until it is really gone.
			RunWhenContainerMenuClosed([this, deferred]() { SortArrivals(deferred); });
		}
		if (Settings::GetSingleton().GetSelling().autoSellHours > 0) {
			RunWhenContainerMenuClosed([]() { sort::selling::CheckTimed("a container closed"); });
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
			const bool  undo = _undo && !_undo->learns.empty() && Settings::GetSingleton().GetSorting().learnUndoSeconds > 0;
			const auto  suffix = undo ? std::format(" ({}: undo)", HotkeyName()) : std::string{};
			for (const auto& [container, items] : _learnBatch) {
				Notify(items.size() == 1 ?
						   std::format("Dova Stash: {} now goes to {}{}", items.front(), container, suffix) :
						   std::format("Dova Stash: {} items now go to {} ({}, ...){}", items.size(), container, items.front(), suffix));
			}
			_learnBatch.clear();
			if (undo) {
				OpenUndoWindow();
			}
		});
	}

	void Controller::OpenUndoWindow()
	{
		const auto seconds = Settings::GetSingleton().GetSorting().learnUndoSeconds;
		if (!_undo || seconds <= 0) {
			_undo.reset();
			return;
		}
		_undo->until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
		logger::debug("Learn: undo offered for {} s ({} learn(s))", seconds, _undo->learns.size());
	}

	bool Controller::TryUndoLearn()
	{
		if (!_undo || !_undo->until) {
			return false;
		}
		if (std::chrono::steady_clock::now() > *_undo->until) {
			_undo.reset();  // the window has passed: the hotkey means what it always means
			return false;
		}
		auto undo = std::move(*_undo);
		_undo.reset();
		_suggestion.reset();  // a question built on these learns is moot
		auto* home = Registry().CurrentIf(undo.cellId);
		if (!home || !home->layout) {
			return false;
		}
		for (auto it = undo.learns.rbegin(); it != undo.learns.rend(); ++it) {
			core::UndoLearn(*home->layout, *it);
		}
		SaveLayout(*home);
		UI().OnHomeChanged();
		if (&Editor() != &UI()) {
			Editor().OnHomeChanged();
		}
		logger::info("Learn: {} learn(s) undone in {}", undo.learns.size(), home->name);
		Notify(undo.itemNames.empty()     ? "Dova Stash: undone"s :
		       undo.itemNames.size() == 1 ? std::format("Dova Stash: undone, {} is not learned", undo.itemNames.front()) :
		                                    std::format("Dova Stash: undone, {} items are not learned", undo.itemNames.size()));
		return true;
	}

	void Controller::ScheduleSuggestion()
	{
		// After the undo window (the hotkey must stay free for undo while it is open), else right after the
		// menu's close animation.
		const auto undoSeconds = _undo && !_undo->learns.empty() ? Settings::GetSingleton().GetSorting().learnUndoSeconds : 0;
		const auto serial = ++_suggestionSerial;
		Later(std::chrono::milliseconds(400 + 1000 * std::max(0, undoSeconds)), [this, serial]() { AskSuggestion(serial); });
	}

	void Controller::AskSuggestion(std::uint64_t a_serial)
	{
		if (a_serial != _suggestionSerial || !_suggestion) {
			return;
		}
		auto* ui = RE::UI::GetSingleton();
		if (!ui || ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME) || ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME) || ui->GameIsPaused()) {
			// A menu is up: try again in a second, for up to a minute, then let the next learn bring it back.
			if (++_suggestionRetries > 60) {
				_suggestionRetries = 0;
				logger::debug("Learn: suggestion put off (menus stayed open); asked again after the next learn");
				return;
			}
			Later(std::chrono::seconds(1), [this, a_serial]() { AskSuggestion(a_serial); });
			return;
		}
		_suggestionRetries = 0;
		const auto pending = std::exchange(_suggestion, std::nullopt);
		auto*      home = Registry().CurrentIf(pending->cellId);
		if (!home || !home->layout) {
			return;
		}
		// Still worth asking? Learns may have been undone or the container bound meanwhile.
		const auto suggestion = SuggestionFor(*home, pending->container);
		if (!suggestion || !core::IEquals(suggestion->category, pending->category)) {
			logger::debug("Learn: suggestion for {} no longer applies", pending->container.ToString());
			return;
		}
		const auto* binding = home->layout->FindContainer(pending->container);
		const auto  label = binding && !binding->label.empty() ? binding->label : game::RefName(game::LookupRef(pending->container));
		const auto  cellId = pending->cellId;
		const auto  container = pending->container;
		const auto  category = suggestion->category;
		logger::debug("Learn: asking whether all {} go to {} ({} learned)", category, label, suggestion->count);
		ui::ShowMessageBox(std::format(ui::kSuggestCategory, ui::CategoryName(category), label), { "Yes", "No" },
			[this, cellId, container, category](std::size_t a_button) {
				if (a_button == 0) {
					AcceptSuggestion(cellId, container, category);
				} else {
					DeclineSuggestion(cellId, container, category);
				}
			});
	}

	std::optional<core::CategorySuggestion> Controller::SuggestionFor(home::Home& a_home, const core::FormRef& a_container)
	{
		if (!a_home.layout) {
			return std::nullopt;
		}
		const auto threshold = Settings::GetSingleton().GetSorting().suggestCategoryAfter;
		if (threshold <= 0) {
			return std::nullopt;
		}
		return core::SuggestCategory(*a_home.layout, a_container, core::SortLearnedCategories(sort::LearnedByCategory(*a_home.layout, a_container)), threshold);
	}

	void Controller::OnItemReturned(RE::FormID a_container, RE::FormID a_baseObject, bool a_byHand)
	{
		if (_openContainer == a_container || a_byHand) {
			return;  // the player took it out by hand (the container menu, or QuickLoot's loot menu)
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
		const auto verdict = home::CheckHomeContainer(ref, *home);  // also a reachable one in another area (opened from Browse)
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

		const bool      wasManaged = home->layout->FindContainer(*containerRef) != nullptr;
		core::LearnUndo undo;
		const auto      result = core::Learn(*home->layout, info, *containerRef, home::Store::Get().Categories(), &undo);
		logger::info("Learn: {} -> {}: {}{}", game::Describe(object), game::Describe(ref), LearnResultName(result),
			info.family && result != core::LearnResult::RejectedItem ? std::format(" (as {})", game::Describe(game::LookupForm(*info.family))) : "");

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
			// Undo: one batch per home; a learn in another home starts over.
			if (!_undo || _undo->cellId != home->cellId) {
				_undo = PendingUndo{ .cellId = home->cellId };
			}
			_undo->learns.push_back(std::move(undo));
			_undo->until.reset();  // the window opens with the HUD line, and again when the menu closes
			if (result != core::LearnResult::Removed) {
				const auto* binding = home->layout->FindContainer(*containerRef);
				const auto  itemName = info.name.empty() ? game::Describe(object) : info.name;
				_undo->itemNames.push_back(itemName);
				ReportLearned(binding && !binding->label.empty() ? binding->label : game::RefName(ref), itemName);
				// A few learned items of one category here: suggest binding the category once the menu closes.
				if (const auto suggestion = SuggestionFor(*home, *containerRef)) {
					_suggestion = PendingSuggestion{ home->cellId, *containerRef, suggestion->category };
					logger::debug("Learn: {} has {} learned {}; will ask about the category", game::Describe(ref), suggestion->count, suggestion->category);
				}
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

	void Controller::OnPreLoadGame()
	{
		// Learned destinations waiting for the container to close (MarkDirty) belong to the layout file, not
		// to the save: write them before the state below is dropped.
		Registry().Flush();
		// Bumping the serials makes every Later callback already in flight a no-op in the new save.
		++_loadSerial;
		++_arrivalSerial;
		++_learnSerial;
		++_suggestionSerial;
		_arrivalDeferredCell = 0;
		_learnBatch.clear();
		_openContainer = 0;
		_undo.reset();
		_suggestion.reset();
		_suggestionRetries = 0;
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
		// A game load meanwhile (OnPreLoadGame) ends the chain: the operation belonged to the old save.
		const auto load = _loadSerial;
		std::thread([this, fn = std::move(a_fn), a_retries, load]() mutable {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			SKSE::GetTaskInterface()->AddTask([this, fn = std::move(fn), a_retries, load]() mutable {
				if (load != _loadSerial) {
					logger::debug("Operation dropped: a game was loaded while a ContainerMenu was open");
					return;
				}
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
		const auto verdict = home::CheckHomeContainer(ref, a_home);
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
			Notify("Dova Stash: this home's setup file is broken (see DovaStash.log)");
			return;
		}
		auto* ref = UsableContainer(*home, a_container);
		if (!ref) {
			return;
		}
		auto& layout = Registry().EnsureLayout(*home);
		std::erase_if(layout.containers, [&](const core::ContainerBinding& c) { return c.ref == a_container; });
		std::erase_if(layout.learned, [&](const auto& a_pair) { return a_pair.second == a_container; });
		layout.declined.erase(a_container);
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
			Notify("Dova Stash: this home's setup file is broken (see DovaStash.log)");
			return;
		}
		const auto name = home->BestTemplate()->layout.name;
		if (!Registry().ApplyBestTemplate(*home)) {
			Notify("Dova Stash: could not apply the template (see log)");
			return;
		}
		if (RecordDisplayItems(*home->layout)) {
			SaveLayout(*home);
		}

		// Report bindings that fail the safety checks right now (disabled Hearthfire furniture, etc.).
		const auto unavailable = CountUnavailable(*home);
		Notify(unavailable == 0 ?
						std::format("Dova Stash: {} setup enabled", name) :
						std::format("Dova Stash: {} setup enabled ({} containers unavailable for now)", name, unavailable));
	}

	void Controller::ResetHome(RE::FormID a_cellId)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home) {
			return;
		}
		if (Registry().Reset(*home)) {
			Notify(std::format("Dova Stash: {} reset (setup backed up)", home->name));
		} else {
			Notify("Dova Stash: reset failed (see log)");
		}
	}

	void Controller::RestoreBackup(RE::FormID a_cellId, std::filesystem::path a_backup)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home) {
			return;
		}
		// Only a copy Dova Stash keeps for this home (the panel passes a path back from its snapshot).
		const auto backups = home::Store::Get().HomeBackups(home->cell);
		const auto it = a_backup.empty() ? std::ranges::find_if(backups, [](const home::Store::HomeBackup& b) { return !b.history; }) :
		                                   std::ranges::find(backups, a_backup, &home::Store::HomeBackup::path);
		if (it == backups.end()) {
			logger::warn("Restore: no backup {} for {}", a_backup.empty() ? "(latest)"s : home::Store::PathText(a_backup), home->name);
			Notify(std::format("Dova Stash: no saved setup for {}", home->name));
			return;
		}
		switch (Registry().RestoreBackup(*home, it->path)) {
		case home::HomeRegistry::RestoreResult::Restored:
			integrations::quickloot::Refresh();
			UI().OnHomeChanged();
			if (&Editor() != &UI()) {
				Editor().OnHomeChanged();
			}
			Notify(std::format("Dova Stash: {} restored", home->name));
			break;
		case home::HomeRegistry::RestoreResult::Broken:
			Notify("Dova Stash: that saved setup is broken (see DovaStash.log)");
			break;
		default:
			Notify("Dova Stash: could not restore the setup (see log)");
			break;
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

	void Controller::RemoveContainer(RE::FormID a_cellId, const core::FormRef& a_container)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		if (home->layoutBroken) {
			Notify("Dova Stash: this home's setup file is broken (see DovaStash.log)");
			return;
		}
		auto& layout = *home->layout;
		if (layout.IsMaster(a_container)) {
			Notify("Dova Stash: the master can't be removed; make another container the master first");
			return;
		}
		const auto* binding = layout.FindContainer(a_container);
		auto        label = binding && !binding->label.empty() ? binding->label : game::RefName(game::LookupRef(a_container));
		if (label.empty()) {
			label = a_container.ToString();  // not loaded (plugin missing): still removable
		}
		const auto  bindings = std::erase_if(layout.containers, [&](const core::ContainerBinding& b) { return b.ref == a_container; });
		const auto  learned = std::erase_if(layout.learned, [&](const auto& a_pair) { return a_pair.second == a_container; });
		layout.declined.erase(a_container);
		if (bindings == 0 && learned == 0) {
			return;
		}
		SaveLayout(*home);
		logger::debug("{} removed from the layout of {} ({} learned destination(s) forgotten)", a_container.ToString(), home->name, learned);
		Notify(std::format("Dova Stash: {} removed (what's inside stays)", label));
	}

	void Controller::SetSellChest(RE::FormID a_cellId, const core::FormRef& a_container, bool a_sell)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home) {
			return;
		}
		if (home->layoutBroken) {
			Notify("Dova Stash: this home's setup file is broken (see DovaStash.log)");
			return;
		}
		if (!a_sell) {
			if (auto* binding = home->layout ? home->layout->FindContainer(a_container) : nullptr; binding && binding->sell) {
				binding->sell = false;
				SaveLayout(*home);
				logger::info("{}: {} is no longer the sell chest", home->name, game::Describe(game::LookupRef(a_container)));
				Notify(std::format("Dova Stash: {} is no longer the sell chest", binding->label));
			}
			return;
		}
		if (home->layout && home->layout->IsMaster(a_container)) {
			Notify("Dova Stash: the master can't be the sell chest");
			return;
		}
		auto* ref = UsableContainer(*home, a_container);
		if (!ref) {
			return;
		}
		auto* binding = EnsureBinding(*home, a_container);
		if (!binding) {
			return;
		}
		binding->sell = true;
		binding->ignored = false;
		// Something to sell from the start: the junk category, when the container has no other job yet.
		std::string junk;
		const bool  learned = std::ranges::any_of(home->layout->learned, [&](const auto& a_pair) { return a_pair.second == a_container; });
		if (binding->categories.empty() && binding->forms.empty() && !learned && home::Store::Get().Categories().Find("misc.clutter")) {
			binding->categories.push_back("misc.clutter");
			junk = ui::CategoryName("misc.clutter");
		}
		SaveLayout(*home);
		logger::info("{}: {} is now the sell chest{}", home->name, game::Describe(ref), junk.empty() ? "" : " (bound misc.clutter)");
		Notify(junk.empty() ? std::format("Dova Stash: {} is now the sell chest", binding->label) :
		                      std::format("Dova Stash: {} is now the sell chest ({})", binding->label, junk));
	}

	void Controller::SellNow(RE::FormID a_cellId, std::optional<core::FormRef> a_chest)
	{
		RunWhenContainerMenuClosed([this, a_cellId, chest = std::move(a_chest)]() {
			auto* home = Registry().CurrentIf(a_cellId);
			if (!home || !home->layout) {
				Notify("Dova Stash: you are no longer in that home");
				return;
			}
			const auto quote = sort::selling::QuoteSale(*home, chest);
			if (quote.chests.empty()) {
				Notify("Dova Stash: no sell chest can be reached from here");
				return;
			}
			if (quote.Empty()) {
				Notify(quote.stays.empty() ? "Dova Stash: nothing to sell"s : std::format("Dova Stash: nothing to sell ({})", quote.stays));
				return;
			}
			const auto items = std::format("{} item{}", quote.items, quote.items == 1 ? "" : "s");
			const auto details = quote.stays.empty() ? quote.what : std::format("{}\n({})", quote.what, quote.stays);
			logger::debug("Sell chest: asking to sell {} for {} gold", items, quote.gold);
			ui::ShowMessageBox(std::format(ui::kConfirmSell, items, quote.gold, details), { "Yes", "No" },
				[this, a_cellId, chest, count = quote.items, gold = quote.gold](std::size_t a_button) {
					if (a_button == 0) {
						SellConfirmed(a_cellId, chest, count, gold);
					}
				});
		});
	}

	void Controller::SellConfirmed(RE::FormID a_cellId, std::optional<core::FormRef> a_chest, std::int32_t a_items, std::int64_t a_gold)
	{
		RunWhenContainerMenuClosed([this, a_cellId, chest = std::move(a_chest), a_items, a_gold]() {
			auto* home = Registry().CurrentIf(a_cellId);
			if (!home || !home->layout) {
				Notify("Dova Stash: you are no longer in that home");
				return;
			}
			const auto result = sort::selling::Sell(*home, chest, a_items, a_gold);
			integrations::quickloot::Refresh();
			UI().OnHomeChanged();
			if (&Editor() != &UI()) {
				Editor().OnHomeChanged();
			}
			Notify(result.message);
		});
	}

	void Controller::BuyBack(RE::FormID a_cellId)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home) {
			return;
		}
		const auto offer = sort::selling::BuyBackOffer(*home);
		if (!offer) {
			Notify("Dova Stash: nothing to buy back here");
			return;
		}
		const auto items = std::format("{} item{}", offer->items, offer->items == 1 ? "" : "s");
		ui::ShowMessageBox(std::format(ui::kConfirmBuyBack, items, offer->gold, offer->what), { "Yes", "No" }, [this, a_cellId](std::size_t a_button) {
			if (a_button == 0) {
				BuyBackConfirmed(a_cellId);
			}
		});
	}

	void Controller::BuyBackConfirmed(RE::FormID a_cellId)
	{
		RunWhenContainerMenuClosed([this, a_cellId]() {
			auto* home = Registry().CurrentIf(a_cellId);
			if (!home) {
				Notify("Dova Stash: you are no longer in that home");
				return;
			}
			const auto result = sort::selling::BuyBack(*home);
			integrations::quickloot::Refresh();
			Notify(result.message);
		});
	}

	void Controller::ReopenDisplayMenu(RE::FormID a_cellId, const core::FormRef& a_display)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		auto* ref = game::LookupRef(a_display);
		if (home && ref) {
			OpenDisplayMenu(*home, ref);
		}
	}

	void Controller::SetDisplayUsed(RE::FormID a_cellId, const core::FormRef& a_display, bool a_used)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		if (home->layoutBroken) {
			Notify("Dova Stash: this home's setup file is broken (see DovaStash.log)");
			return;
		}
		auto*      ref = game::LookupRef(a_display);
		const auto found = ref ? game::InspectDisplay(ref) : std::nullopt;
		const auto name = found ? DisplayName(ref, found->kind) : game::RefName(ref);
		auto&      displays = home->layout->displays;
		const auto it = std::ranges::find(displays, a_display, &core::DisplayBinding::ref);
		if (a_used) {
			if (it == displays.end()) {
				core::DisplayBinding binding;
				binding.ref = a_display;
				binding.label = name;
				displays.push_back(std::move(binding));
			} else {
				it->ignored = false;
			}
		} else {
			if (it == displays.end() || it->ignored) {
				return;
			}
			// Kept as ignored, so re-applying a template that lists it does not turn it back on.
			it->ignored = true;
		}
		RecordDisplayItems(*home->layout);
		SaveLayout(*home);
		logger::info("Display {} in {}: {}", game::Describe(ref), home->name, a_used ? "used" : "no longer used");
		Notify(a_used ? std::format("Dova Stash: {} is used now", name) : std::format("Dova Stash: {} won't be filled anymore", name));
	}

	void Controller::ToggleDisplayCategory(RE::FormID a_cellId, const core::FormRef& a_display, const std::string& a_pattern)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		auto* binding = home && home->layout ? home->layout->FindDisplay(a_display) : nullptr;
		if (!binding) {
			return;
		}
		auto&      patterns = binding->categories;
		const auto it = std::ranges::find_if(patterns, [&](const std::string& p) { return core::IEquals(p, a_pattern); });
		const bool remove = it != patterns.end();
		if (remove) {
			patterns.erase(it);
		} else {
			patterns.push_back(a_pattern);
		}
		logger::info("Display {}: {} {}", game::Describe(game::LookupRef(a_display)), remove ? "unbound" : "bound", a_pattern);
		SaveLayout(*home);
	}

	void Controller::FillDisplay(RE::FormID a_cellId, const core::FormRef& a_display)
	{
		RunWhenContainerMenuClosed([this, a_cellId, a_display]() {
			auto* home = Registry().CurrentIf(a_cellId);
			if (!home) {
				return;
			}
			const auto result = sort::FillDisplay(*home, a_display);
			if (!result.message.empty()) {
				Notify(result.message);
			}
		});
	}

	void Controller::UseDisplays(RE::FormID a_cellId, const std::vector<core::FormRef>& a_displays)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		if (home->layoutBroken) {
			Notify("Dova Stash: this home's setup file is broken (see DovaStash.log)");
			return;
		}
		auto&       layout = *home->layout;
		std::size_t added = 0;
		for (const auto& display : a_displays) {
			if (layout.FindDisplay(display)) {
				continue;  // listed: used already, or turned off by the player
			}
			const auto* container = layout.FindContainer(display);
			if (layout.IsMaster(display) || (container && !container->ignored)) {
				continue;  // storage, not a display
			}
			core::DisplayBinding binding;
			binding.ref = display;
			binding.label = game::RefName(game::LookupRef(display));
			layout.displays.push_back(std::move(binding));
			++added;
		}
		if (added == 0) {
			return;
		}
		RecordDisplayItems(layout);
		SaveLayout(*home);
		logger::info("{} display(s) in {} used now (panel)", added, home->name);
		Notify(std::format("Dova Stash: {} display{} used now", added, added == 1 ? "" : "s"));
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
			Notify("Dova Stash: this home's setup file is broken (see DovaStash.log)");
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
		if (RecordDisplayItems(*home->layout)) {
			SaveLayout(*home);
		}
		const auto unavailable = CountUnavailable(*home);
		const auto name = chosen->layout.name;
		Notify(unavailable == 0 ?
						std::format("Dova Stash: {} setup enabled", name) :
						std::format("Dova Stash: {} setup enabled ({} containers unavailable for now)", name, unavailable));
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

	void Controller::ForgetStaleLearned(RE::FormID a_cellId)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		auto&      layout = *home->layout;
		const auto removed = std::erase_if(layout.learned, [&](const auto& a_pair) {
			const auto* c = layout.FindContainer(a_pair.second);
			return !c || c->ignored || layout.IsMaster(a_pair.second);
		});
		if (removed == 0) {
			Notify("Dova Stash: nothing to forget");
			return;
		}
		SaveLayout(*home);
		logger::info("{}: forgot {} learned item(s) whose container is no longer used", home->name, removed);
		Notify(std::format("Dova Stash: forgot {} learned item{}", removed, removed == 1 ? "" : "s"));
	}

	void Controller::AcceptSuggestion(RE::FormID a_cellId, const core::FormRef& a_container, const std::string& a_category)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		if (home->layoutBroken) {
			Notify("Dova Stash: this home's setup file is broken (see DovaStash.log)");
			return;
		}
		const core::ItemLookup lookup = [](const core::FormRef& a_form) -> std::optional<core::ItemInfo> {
			auto*          form = game::LookupForm(a_form);
			auto*          object = form ? form->As<RE::TESBoundObject>() : nullptr;
			core::ItemInfo info;
			if (!object || !game::FillBaseInfo(info, object)) {
				return std::nullopt;
			}
			info.count = 1;
			return info;
		};
		const auto pruned = core::AcceptSuggestion(*home->layout, a_container, a_category, home::Store::Get().Categories(), lookup);
		if (!pruned) {
			return;  // master, ignored or unknown: nothing changed
		}
		SaveLayout(*home);
		UI().OnHomeChanged();
		if (&Editor() != &UI()) {
			Editor().OnHomeChanged();
		}
		const auto* binding = home->layout->FindContainer(a_container);
		const auto  label = binding && !binding->label.empty() ? binding->label : game::RefName(game::LookupRef(a_container));
		logger::info("{}: bound {} from a suggestion ({} learned item(s) now covered by it dropped)", label, a_category, *pruned);
		Notify(std::format("Dova Stash: {} now takes {}", label, ui::CategoryName(a_category)));
	}

	void Controller::DeclineSuggestion(RE::FormID a_cellId, const core::FormRef& a_container, const std::string& a_category)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home || !home->layout || home->layoutBroken) {
			return;
		}
		if (core::DeclineSuggestion(*home->layout, a_container, a_category)) {
			SaveLayout(*home);
			UI().OnHomeChanged();
			if (&Editor() != &UI()) {
				Editor().OnHomeChanged();
			}
			logger::info("{}: {} declined for {} (not asked again)", home->name, a_category, a_container.ToString());
		}
	}

	void Controller::ClearDeclined(RE::FormID a_cellId, const core::FormRef& a_container, const std::string& a_category)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home || !home->layout || home->layoutBroken) {
			return;
		}
		if (core::ClearDeclined(*home->layout, a_container, a_category) > 0) {
			SaveLayout(*home);
			UI().OnHomeChanged();
			if (&Editor() != &UI()) {
				Editor().OnHomeChanged();
			}
			logger::info("{}: suggestion(s) for {} may be asked again", home->name, a_container.ToString());
		}
	}

	void Controller::SetKeep(RE::FormID a_cellId, std::optional<core::KeepConfig> a_keep)
	{
		auto* home = Registry().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		if (home->layoutBroken) {
			Notify("Dova Stash: this home's setup file is broken (see DovaStash.log)");
			return;
		}
		if (!a_keep) {
			home->layout->keep.reset();
			SaveLayout(*home);
			Notify("Dova Stash: keep rules reset to the defaults");
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

	void Controller::ReloadTemplates()
	{
		home::Store::Get().ReloadTemplates();
		Registry().RefreshTemplates();
		UI().OnTemplatesChanged();
		UI().OnHomeChanged();
		if (&Editor() != &UI()) {
			Editor().OnTemplatesChanged();
			Editor().OnHomeChanged();
		}
	}

	Controller::ExportResult Controller::ExportTemplate(RE::FormID a_cellId, const ExportRequest& a_request)
	{
		ExportResult result;
		auto*        home = Registry().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			result.message = "You are no longer in that home, or it is not set up.";
			return result;
		}
		result.name = Trim(a_request.name);
		if (result.name.empty()) {
			result.message = "Enter a template name.";
			return result;
		}
		if (!IsValidUtf8(result.name)) {
			result.message = "The name has characters that can't be saved; use another one.";
			return result;
		}
		auto author = Trim(a_request.author);
		if (author.empty() || !IsValidUtf8(author)) {
			author = PlayerName();
		}

		const auto stem = core::TemplateFileStem(result.name);
		result.source = std::format("{}{}.json", core::kUserTemplatePrefix, stem);
		const auto path = std::filesystem::path{ home::Store::kRoot } / "Templates" / "User" / (stem + ".json");
		result.path = path.generic_string();

		// A file of that name: read its version (the new one is one patch higher) and whose home it is.
		std::error_code ec;
		const bool      exists = std::filesystem::exists(path, ec);
		if (ec) {
			logger::error("Template export: cannot check {}: {}", result.path, ec.message());
			result.message = std::format("Could not check {} (see log).", result.path);
			return result;
		}
		std::optional<core::Layout> existing;
		if (exists) {
			core::Diagnostics ignored;  // an unreadable file is still replaced (after its backup)
			try {
				existing = core::LoadLayoutFile(path, result.source, ignored);
			} catch (const std::exception& e) {
				logger::warn("Template export: cannot read {}: {}", result.path, e.what());
			}
			if (existing) {
				result.existingVersion = existing->version;
				result.existingOtherHome = existing->cell && *existing->cell != home->cell;
			}
		}
		result.version = Trim(a_request.version);
		if (result.version.empty()) {
			result.version = existing ? core::NextPatchVersion(existing->version) : "1.0.0"s;
		}
		if (exists && !a_request.overwrite) {
			result.status = ExportResult::Status::Exists;
			result.message = std::format("{} already exists{}{}. Overwrite saves v{} and backs up the old file.", result.path,
				result.existingVersion.empty() ? ""s : std::format(" (v{})", result.existingVersion),
				result.existingOtherHome ? ", for another home"s : ""s, result.version);
			logger::debug("Template export: {} exists, asking before replacing it", result.path);
			return result;
		}
		if (exists && !home::Store::BackupFile(path, "Template export")) {
			result.message = std::format("Could not back up {}; nothing was written (see log).", result.path);
			return result;
		}

		auto layout = *home->layout;
		layout.cell = home->cell;  // the cell being edited decides the home, as for saving
		auto tpl = core::ExportAsTemplate(layout, { .learnedAsForms = a_request.learnedAsForms }, &result.learnedKept);
		tpl.name = result.name;
		tpl.author = author;
		tpl.version = result.version;

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
		core::ValidateTemplate(tpl, result.source, diags);
		core::ValidateLayoutCategories(tpl, home::Store::Get().Categories(), result.source, diags);
		home::Store::LogDiagnostics(diags, "Template export");
		for (const auto& d : diags.Items()) {
			result.warnings.push_back(d.ToString());
		}
		logger::info("Exported {} as template \"{}\" v{} by {} to {} ({} container(s), {} display(s), {} learned item(s) {}, requires {})", home->name, tpl.name,
			tpl.version, tpl.author, result.path, tpl.containers.size(), tpl.displays.size(),
			a_request.learnedAsForms ? result.learnedKept : layout.learned.size(), a_request.learnedAsForms ? "kept as item bindings" : "left out",
			tpl.requiredPlugins.size());

		// Offered right away: scan the templates again and resolve this home against them.
		ReloadTemplates();
		home = Registry().CurrentIf(a_cellId);
		const auto templates = home::Store::Get().Templates();
		const auto offered = [&](std::size_t a_index) { return a_index < templates.size() && core::IEquals(templates[a_index].source, result.source); };
		const bool here = home && ((home->resolution.chosen && offered(*home->resolution.chosen)) || std::ranges::any_of(home->resolution.alternatives, offered));
		const auto missing = core::MissingRequirements(tpl, game::LoadedPlugins());
		std::string missingText;
		for (const auto& p : missing) {
			missingText += missingText.empty() ? p : ", " + p;
		}
		if (!missing.empty()) {
			// The layout still names forms of a plugin that is gone (kept, never deleted): the template
			// requires it, so it is offered where that plugin is loaded, not in this game.
			result.warnings.push_back(std::format("Not offered in this game: it requires {} (not loaded).", missingText));
			logger::warn("Template export: {} requires {}, which is not loaded; it is not offered in this game", result.source, missingText);
		}

		result.status = ExportResult::Status::Ok;
		result.message = here ? std::format("Saved to {}. This home offers it now.", result.path) : std::format("Saved to {}.", result.path);
		if (result.learnedKept > 0) {
			result.message += std::format(" {} learned item{} kept as fixed item bindings.", result.learnedKept, result.learnedKept == 1 ? "" : "s");
		}
		logger::debug("Template export: {} is {}offered in {}", result.source, here ? "" : "not ", home ? home->name : "(left the home)"s);
		const auto  warnings = result.warnings.size();
		std::string extras;
		if (result.learnedKept > 0) {
			extras = std::format("incl. {} learned item{}", result.learnedKept, result.learnedKept == 1 ? "" : "s");
		}
		if (warnings > 0) {
			extras += std::format("{}{} warning{}, see log", extras.empty() ? "" : ", ", warnings, warnings == 1 ? "" : "s");
		}
		Notify(extras.empty() ? std::format("Dova Stash: saved as template \"{}\"", tpl.name) :
		                        std::format("Dova Stash: saved as template \"{}\" ({})", tpl.name, extras));
		return result;
	}
}
