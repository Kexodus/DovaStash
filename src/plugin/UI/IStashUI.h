#pragma once

// UI facade (DESIGN §10.1). Implementations: MessageBoxUI (native, every runtime), ListUI
// (UIExtensions list menus) and MenuFrameworkUI (SKSE Menu Framework panel, M4). Called on the
// main thread by the Controller. Implementations must not keep Home& or REFR pointers across frames:
// menu callbacks capture the cell id / FormRefs and go back through the Controller, which re-validates
// them.

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
		// True while this UI is on screen (status messages then go to it rather than the world UI).
		[[nodiscard]] virtual bool IsActive() const { return false; }
	};

	// The two UI roles (DESIGN §10.3, [UI] iMode):
	//   world  - what the hotkey opens in the world: UIExtensions lists (ListUI), native message
	//            boxes (MessageBoxUI), or the Menu Framework panel when iMode = 2.
	//   editor - full editing ("Advanced..." in the lists): the SKSE Menu Framework panel when loaded
	//            (not VR), else message boxes. May be the same object as `world`.
	struct StashUIs
	{
		std::shared_ptr<IStashUI> world;
		std::shared_ptr<IStashUI> editor;
	};
	[[nodiscard]] StashUIs CreateStashUIs();
}
