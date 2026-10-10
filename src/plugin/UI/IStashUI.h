#pragma once

// UI facade (DESIGN §10.1). Implementations: MessageBoxUI (native, every runtime) and MenuFrameworkUI
// (SKSE Menu Framework panel, M4). Called on the main thread by the Controller. Implementations must not keep
// Home& or REFR pointers across frames: menu callbacks capture the cell id / FormRefs and go back
// through the Controller, which re-validates them.

#include "Home/HomeRegistry.h"

namespace dova::ui
{
	class IStashUI
	{
	public:
		virtual ~IStashUI() = default;

		// `a_master` may be null (pressed the hotkey at nothing, or the master is not loaded/available).
		virtual void OpenMasterMenu(home::Home& a_home, RE::TESObjectREFR* a_master) = 0;
		virtual void OpenContainerMenu(home::Home& a_home, RE::TESObjectREFR* a_container) = 0;
		// Home without a player layout. `a_container` may be null (then only template options are shown).
		virtual void OpenSetupPrompt(home::Home& a_home, RE::TESObjectREFR* a_container) = 0;
		virtual void Notify(std::string_view a_message) = 0;
		// The current home or its layout may have changed outside the UI (cell change, learned items).
		virtual void OnHomeChanged() {}
	};

	// [UI] iMode (Settings): 0 = MenuFrameworkUI when the framework is loaded and not VR, else
	// MessageBoxUI; 1 = MessageBoxUI; 2 = MenuFrameworkUI whenever the framework is loaded.
	[[nodiscard]] std::unique_ptr<IStashUI> CreateStashUI();
}
