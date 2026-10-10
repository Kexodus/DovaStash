#pragma once

// "What does the hotkey mean here?" (DESIGN §5) plus every player-facing action.
//
// Threading: every method runs on the main thread. Event sinks and the input sink only enqueue
// (SKSE::GetTaskInterface()->AddTask) calls into the Controller; message-box callbacks arrive through
// the same queue. Actions take the cell id captured when the menu was opened and do nothing if the
// player has since left that cell.

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
		[[nodiscard]] ui::IStashUI& UI();

		// --- triggers (main thread) ------------------------------------------------------------------
		void OnHotkey(RE::RefHandle a_crosshairTarget);
		void OnCellMaybeChanged();
		void OnContainerMenuOpened(RE::FormID a_target);
		void OnContainerMenuClosed();
		// Player -> container transfer (TESContainerChangedEvent) — candidate for learning.
		void OnItemStored(RE::FormID a_container, RE::FormID a_baseObject);
		void OnSaveGame();

		// --- actions (main thread; all validate a_cellId against the current cell) --------------------
		void RunOperation(RE::FormID a_cellId, sort::Op a_op, std::string a_pattern = {});
		void MakeMaster(RE::FormID a_cellId, const core::FormRef& a_container);
		void UseTemplate(RE::FormID a_cellId);
		void ResetHome(RE::FormID a_cellId);
		void ToggleCategory(RE::FormID a_cellId, const core::FormRef& a_container, const std::string& a_pattern);
		void ClearCategories(RE::FormID a_cellId, const core::FormRef& a_container);
		void SetIgnored(RE::FormID a_cellId, const core::FormRef& a_container, bool a_ignored);

		// --- editor actions (M4 panel; main thread, validate a_cellId like the actions above) ---------
		// Applies a specific eligible template for this cell (by TemplateCandidate::source).
		void UseTemplate(RE::FormID a_cellId, std::string_view a_source);
		void SetLabel(RE::FormID a_cellId, const core::FormRef& a_container, std::string a_label);
		// Drops every learned destination that points at `a_container`.
		void ForgetLearned(RE::FormID a_cellId, const core::FormRef& a_container);
		// Drops the learned destination of one base form.
		void RemoveLearned(RE::FormID a_cellId, const core::FormRef& a_item);
		// Replaces the layout's keep block (nullopt = remove it, i.e. use the global defaults only).
		void SetKeep(RE::FormID a_cellId, std::optional<core::KeepConfig> a_keep);

		struct ExportRequest
		{
			std::string name;
			std::string author;
			std::string version;
			bool        overwrite{ false };
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
			std::string              path;      // Data-relative path of the template file
			std::string              message;   // one line for the UI
			std::vector<std::string> warnings;  // template lint (ValidateTemplate / categories)
		};
		// Writes this home's layout as a shareable template to
		// Data/SKSE/Plugins/DovaStash/Templates/User/<name>.json (core::ExportAsTemplate).
		[[nodiscard]] ExportResult ExportTemplate(RE::FormID a_cellId, const ExportRequest& a_request);

		[[nodiscard]] static std::string HotkeyName();

	private:
		Controller() = default;

		// Runs `a_fn` once no ContainerMenu is open (DESIGN §8.2: never sort while one is open), polling
		// every 100 ms for up to ~5 s.
		void RunWhenContainerMenuClosed(std::function<void()> a_fn, int a_retries = 50);

		// Binding for `a_container`, created (as a plain managed container) if needed.
		core::ContainerBinding* EnsureBinding(home::Home& a_home, const core::FormRef& a_container);
		// Container ref by FormRef if it is currently usable in this home; notifies the reason otherwise.
		RE::TESObjectREFR* UsableContainer(home::Home& a_home, const core::FormRef& a_container);

		std::unique_ptr<ui::IStashUI> _ui;
		RE::FormID                    _openContainer{ 0 };  // target of the open ContainerMenu (0 = none)
	};
}
