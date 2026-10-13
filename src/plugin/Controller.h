#pragma once

// "What does the hotkey mean here?" (DESIGN §5) plus every player-facing action.
//
// Threading: every method runs on the main thread. Event sinks and the input sink only enqueue
// (SKSE::GetTaskInterface()->AddTask) calls into the Controller; message-box callbacks arrive through
// the same queue. Actions take the cell id captured when the menu was opened and do nothing if the
// player has since left that cell.

#include "Settings.h"
#include "Sort/SortEngine.h"
#include "UI/IStashUI.h"

namespace dova
{
	class Controller
	{
	public:
		[[nodiscard]] static Controller& Get();

		// kDataLoaded: data, UI choice.
		void Init();
		// What the hotkey opens in the world (UIExtensions list / message boxes / panel, DESIGN §10.3).
		[[nodiscard]] ui::IStashUI& UI();
		// Full editor ("Advanced..."): the Menu Framework panel when loaded, else message boxes.
		[[nodiscard]] ui::IStashUI& Editor();

		// --- triggers (main thread) ------------------------------------------------------------------
		void OnHotkey(RE::RefHandle a_crosshairTarget);
		void OnCellMaybeChanged();
		void OnContainerMenuOpened(RE::FormID a_target);
		void OnContainerMenuClosed();
		// Player -> container transfer (TESContainerChangedEvent) — candidate for learning.
		void OnItemStored(RE::FormID a_container, RE::FormID a_baseObject);
		// Container -> player transfer: an item a script hands back right after Dova Stash stored it.
		// `a_byHand`: the player took it through a loot menu (QuickLoot IE), which is never a script.
		void OnItemReturned(RE::FormID a_container, RE::FormID a_baseObject, bool a_byHand = false);
		void OnSaveGame();

		// --- actions (main thread; all validate a_cellId against the current cell) --------------------
		void RunOperation(RE::FormID a_cellId, sort::Op a_op, std::string a_pattern = {});
		void MakeMaster(RE::FormID a_cellId, const core::FormRef& a_container);
		void UseTemplate(RE::FormID a_cellId);
		void ResetHome(RE::FormID a_cellId);
		// Brings back a copy of this home's setup kept outside Data (DESIGN §6.6): `a_backup` is one of
		// Store::HomeBackups (anything else is refused); empty means the latest copy. The current setup file,
		// if any, is backed up first. A copy that does not load is never used (the HUD says so).
		void RestoreBackup(RE::FormID a_cellId, std::filesystem::path a_backup = {});
		void ToggleCategory(RE::FormID a_cellId, const core::FormRef& a_container, const std::string& a_pattern);
		void ClearCategories(RE::FormID a_cellId, const core::FormRef& a_container);
		void SetIgnored(RE::FormID a_cellId, const core::FormRef& a_container, bool a_ignored);
		// Takes the container out of the layout: its entry (categories, explicit items, label) and the
		// learned destinations pointing at it go; what is inside stays. Not the master. Unlike Ignore,
		// nothing is kept, so Learn may add it again when items are put in by hand.
		void RemoveContainer(RE::FormID a_cellId, const core::FormRef& a_container);
		// Displays (DESIGN §8.4): only the ones the layout uses are filled, by Deposit and Sort all (or Fill
		// now). Turning one off keeps it in the layout as ignored (nothing is taken down).
		void SetDisplayUsed(RE::FormID a_cellId, const core::FormRef& a_display, bool a_used);
		// Racks and mannequins: the categories they take, like a container's (none: anything that fits).
		void ToggleDisplayCategory(RE::FormID a_cellId, const core::FormRef& a_display, const std::string& a_pattern);
		void FillDisplay(RE::FormID a_cellId, const core::FormRef& a_display);
		// Shows a display's menu again (after its category picker).
		void ReopenDisplayMenu(RE::FormID a_cellId, const core::FormRef& a_display);
		// Opens the container's real ContainerMenu (SkyUI and the player's icon mods draw it), from
		// anywhere in the home.
		void Browse(RE::FormID a_cellId, const core::FormRef& a_container);
		// Multi-cell homes (DESIGN §6.7). Adds the player's cell (`a_cellId`) to the home they just came from
		// (HomeRegistry::OfferArea, checked again here); no items are moved. `a_label`: the name the player gave
		// the area (ui::AddAreaNamed asks with the UIExtensions text box); empty keeps the cell's own name.
		void AddArea(RE::FormID a_cellId, std::string a_label = {});
		// Names an extra area of the home ("Grounds"); empty brings the cell's own name back. Not the main cell.
		void RenameArea(RE::FormID a_cellId, const core::FormRef& a_area, std::string a_label);
		// Takes an extra cell out of the home's layout (its containers stay listed, out of reach); not the main cell.
		void RemoveArea(RE::FormID a_cellId, const core::FormRef& a_area);

		// --- editor actions (M4 panel; main thread, validate a_cellId like the actions above) ---------
		// Applies a specific eligible template for this cell (by TemplateCandidate::source).
		void UseTemplate(RE::FormID a_cellId, std::string_view a_source);
		void SetLabel(RE::FormID a_cellId, const core::FormRef& a_container, std::string a_label);
		// Drops every learned destination that points at `a_container`.
		void ForgetLearned(RE::FormID a_cellId, const core::FormRef& a_container);
		// Drops the learned destination of one base form.
		void RemoveLearned(RE::FormID a_cellId, const core::FormRef& a_item);
		// Drops the learned destinations whose container is no longer used: not in the setup, ignored, or the
		// master (the panel's Learned tab, DESIGN §8.6). Entries for containers that are merely unavailable
		// right now (unbuilt, out of reach) stay.
		void ForgetStaleLearned(RE::FormID a_cellId);
		// Category suggestions (DESIGN §8.6, core/Learning.h). Accept binds the category to the container and
		// drops the learned entries it covers; Decline remembers the No in the layout (never asked again);
		// ClearDeclined forgets a No (the panel's "Ask again"; empty category: every No for the container).
		void AcceptSuggestion(RE::FormID a_cellId, const core::FormRef& a_container, const std::string& a_category);
		void DeclineSuggestion(RE::FormID a_cellId, const core::FormRef& a_container, const std::string& a_category);
		void ClearDeclined(RE::FormID a_cellId, const core::FormRef& a_container, const std::string& a_category);
		// The suggestion the panel shows for a container, if any (main thread; the live layout).
		[[nodiscard]] std::optional<core::CategorySuggestion> SuggestionFor(home::Home& a_home, const core::FormRef& a_container);
		// Replaces the layout's keep block (nullopt = remove it, i.e. use the global defaults only).
		void SetKeep(RE::FormID a_cellId, std::optional<core::KeepConfig> a_keep);
		// Keep defaults for every home (Data/SKSE/Plugins/DovaStash/Settings.json).
		void SetGlobalKeep(core::KeepConfig a_keep);

		struct ExportRequest
		{
			std::string name;
			std::string author;   // empty: the player character's name ("Player" if it has none)
			std::string version;  // empty: 1.0.0, or the replaced file's version with the patch bumped
			bool        overwrite{ false };
			// Learned destinations become fixed item bindings (core::ExportOptions::learnedAsForms), so the
			// items go to the same containers for anyone using the template. On in every UI by default.
			bool learnedAsForms{ true };
		};
		struct ExportResult
		{
			enum class Status : std::uint8_t
			{
				Ok,
				Exists,  // the file exists and overwrite was false: ask, then retry with overwrite
				Failed
			};
			Status                   status{ Status::Failed };
			std::string              name;      // the template's name (trimmed)
			std::string              path;      // Data-relative path of the template file
			std::string              source;    // "Templates/User/<stem>.json"
			std::string              version;   // written, or (Exists) the version it would get
			std::string              existingVersion;           // Exists: the file's version ("" if unreadable)
			bool                     existingOtherHome{ false };  // Exists: the file is a template for another home
			std::size_t              learnedKept{ 0 };  // Ok: learned items written as item bindings
			std::string              message;   // one line for the UI
			std::vector<std::string> warnings;  // template lint (ValidateTemplate / categories), plugins not loaded
		};
		// "Save as template" (hotkey menus and the panel's Template tab share this): writes this home's
		// layout as a shareable template to Data/SKSE/Plugins/DovaStash/Templates/User/<stem>.json
		// (core::ExportAsTemplate: learned destinations as item bindings or dropped, see learnedAsForms;
		// requires = every referenced plugin). A file
		// that is replaced is backed up first (timestamped .bak). The templates are then scanned again and
		// the home re-resolved, so "Use template" offers it at once. Notifies the HUD on success.
		[[nodiscard]] ExportResult ExportTemplate(RE::FormID a_cellId, const ExportRequest& a_request);

		[[nodiscard]] static std::string HotkeyName();

		// Status line for the player: the editor while it is open, else the world UI. Main thread.
		void Notify(std::string_view a_message);

		// Settings page (main thread): validates, applies and saves to the INI. Everything takes effect
		// immediately; a UI mode change rebuilds the UIs. No restart needed.
		void ApplySettings(Settings::Values a_values);

	private:
		Controller() = default;

		// Runs `a_fn` once no ContainerMenu is open (DESIGN §8.2: never sort while one is open), polling
		// every 100 ms for up to ~5 s.
		void RunWhenContainerMenuClosed(std::function<void()> a_fn, int a_retries = 50);

		// The hotkey aimed at a display (game::DisplayAt): opens its menu and returns true.
		bool OpenDisplayMenu(home::Home& a_home, RE::TESObjectREFR* a_target);
		// The hotkey in a cell that is no home but can join the home the player came from: asks (message box).
		void OpenAreaPrompt(const home::HomeRegistry::AreaOffer& a_offer);
		// Binding for `a_container`, created (as a plain managed container) if needed.
		core::ContainerBinding* EnsureBinding(home::Home& a_home, const core::FormRef& a_container);
		// Container ref by FormRef if it is currently usable in this home; notifies the reason otherwise.
		RE::TESObjectREFR* UsableContainer(home::Home& a_home, const core::FormRef& a_container);

		// Sort on arrival (DESIGN §8.1): true when `a_container` is the master of the current home (the
		// item was put into the master); schedules a Distribute once arrivals stop for a moment.
		bool OnMasterArrival(RE::FormID a_container);
		void SortArrivals(RE::FormID a_cellId);
		// Learn feedback: batches "this container now holds X" into one notification per burst (with the
		// undo offer when iLearnUndoSeconds > 0).
		void ReportLearned(const std::string& a_container, const std::string& a_item);
		// Undo (DESIGN §8.6): the hotkey within the window after a learn forgets the whole batch. True when
		// the press was consumed. The window starts with the HUD line and again when the container closes.
		bool TryUndoLearn();
		void OpenUndoWindow();
		// Category suggestion (DESIGN §8.6): asked once the container is closed and the undo window has
		// passed, never during a sort; a No is remembered in the layout.
		void ScheduleSuggestion();
		void AskSuggestion(std::uint64_t a_serial);

		// Saves the layout and lets UI surfaces that show it (QuickLoot info bar) refresh.
		bool SaveLayout(home::Home& a_home);
		// Scans the templates again, re-resolves the current home and tells the UIs (after an export).
		void ReloadTemplates();
		void EnsureUI();

		ui::StashUIs _ui;
		std::uint64_t _arrivalSerial{ 0 };
		RE::FormID    _arrivalDeferredCell{ 0 };  // arrivals waiting for a paused master menu to close
		std::uint64_t _learnSerial{ 0 };
		std::map<std::string, std::vector<std::string>> _learnBatch;  // container label -> item names
		RE::FormID                    _openContainer{ 0 };  // target of the open ContainerMenu (0 = none)

		// Undo: what the current learn batch changed (core::UndoLearn takes it back, newest first), the home it
		// belongs to, and until when the hotkey means "undo" (steady clock; nullopt = no window open).
		struct PendingUndo
		{
			RE::FormID                    cellId{ 0 };
			std::vector<core::LearnUndo>  learns;
			std::vector<std::string>      itemNames;
			std::optional<std::chrono::steady_clock::time_point> until;
		};
		std::optional<PendingUndo> _undo;
		// Category suggestion waiting for the container to close (one at a time: the latest learn's).
		struct PendingSuggestion
		{
			RE::FormID    cellId{ 0 };
			core::FormRef container;
			std::string   category;
		};
		std::optional<PendingSuggestion> _suggestion;
		std::uint64_t                    _suggestionSerial{ 0 };
	};
}
