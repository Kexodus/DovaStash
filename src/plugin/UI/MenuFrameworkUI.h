#pragma once

// SKSE Menu Framework implementation of the UI facade (DESIGN §10.1): the rich editor.
//
// The hotkey opens one contextual stash window (AddWindow) focused on the targeted container; the
// "Dova Stash" section of the framework's own menu adds This home / Displays / Settings / About pages. Rendering happens on the render thread from snapshots (UI/Panel/*);
// this class only runs on the main thread (called by the Controller).

#include "UI/IStashUI.h"

namespace dova::ui
{
	class MenuFrameworkUI final : public IStashUI
	{
	public:
		// Registers the window and section pages. nullptr if the framework is not usable (caller falls
		// back to MessageBoxUI). Main thread, kDataLoaded or later.
		[[nodiscard]] static std::unique_ptr<IStashUI> Create();

		void OpenMasterMenu(home::Home& a_home, RE::TESObjectREFR* a_master) override;
		void OpenContainerMenu(home::Home& a_home, RE::TESObjectREFR* a_container) override;
		void OpenSetupPrompt(home::Home& a_home, RE::TESObjectREFR* a_container) override;
		void Notify(std::string_view a_message, bool a_failed = false) override;
		void OnHomeChanged() override;
		void OnTemplatesChanged() override;
		[[nodiscard]] bool IsActive() const override;

	private:
		MenuFrameworkUI() = default;
	};
}
