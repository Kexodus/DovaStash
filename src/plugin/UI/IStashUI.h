#pragma once

// UI facade (DESIGN §10.1). Implementations: MessageBoxUI (native, every runtime), ListUI
// (UIExtensions list menus) and MenuFrameworkUI (SKSE Menu Framework panel, M4). Called on the
// main thread by the Controller. Implementations must not keep Home& or REFR pointers across frames:
// menu callbacks capture the cell id / FormRefs and go back through the Controller, which re-validates
// them.

#include "Home/HomeRegistry.h"

namespace dova::ui
{
	// What the hotkey found when aimed at a display (DESIGN §8.4).
	struct DisplayMenu
	{
		RE::FormID               cellId{ 0 };
		core::FormRef            ref;   // rack activator, plaque, mannequin actor or case
		std::string              name;  // "Weapon Rack"
		core::DisplayKind        kind{ core::DisplayKind::Rack };
		core::DisplayFit         fit;  // what it holds (kind, rack type, crossbow node): its category picker
		bool                     used{ false };  // the layout uses it (Deposit and Sort all fill it)
		bool                     full{ false };  // it holds something now
		std::vector<std::string> categories;     // racks, mannequins, bookshelves: what they take (empty: anything that fits)
	};

	struct ArmorSetMenu;

	class IStashUI
	{
	public:
		virtual ~IStashUI() = default;

		// `a_master` may be null (pressed the hotkey at nothing, or the master is not loaded/available).
		virtual void OpenMasterMenu(home::Home& a_home, RE::TESObjectREFR* a_master) = 0;
		virtual void OpenContainerMenu(home::Home& a_home, RE::TESObjectREFR* a_container) = 0;
		// Home without a player layout: "Restore your last setup (date)" first when Home::backup is set, then
		// make the container the master, then the template. `a_container` may be null (then only the restore
		// and template options are shown).
		virtual void OpenSetupPrompt(home::Home& a_home, RE::TESObjectREFR* a_container) = 0;
		// A cell another home's template lists (Home::PartOfTemplate, DESIGN §6.7), no setup anywhere: one entry,
		// Use template "<name>" (Controller::UsePartOfTemplate), which sets up the whole home from here. The default
		// is a native message box (UI/AreaPrompt.cpp).
		virtual void OpenPartOfPrompt(home::Home& a_home, std::string_view a_template);
		// Turn a display on or off, pick its categories, or fill it now. The default is native message boxes
		// (UI/DisplayMenu.cpp).
		virtual void OpenDisplayMenu(const DisplayMenu& a_menu);
		// A mannequin's armor set picker (UI/ArmorSetMenu.h). The default is native message boxes.
		virtual void OpenArmorSetMenu(const ArmorSetMenu& a_menu);
		// `a_failed`: the action did not happen (the panel shows it red); the HUD shows both alike.
		virtual void Notify(std::string_view a_message, bool a_failed = false) = 0;
		// The current home or its layout may have changed outside the UI (cell change, learned items).
		virtual void OnHomeChanged() {}
		// The template list was scanned again (a template was saved): pages that list templates rebuild.
		virtual void OnTemplatesChanged() {}
		// True while this UI is on screen (status messages then go to it rather than the world UI).
		[[nodiscard]] virtual bool IsActive() const { return false; }
	};

	// The two UI roles (DESIGN §10.3, [UI] iMode):
	//   world  - what the hotkey opens in the world: UIExtensions lists (ListUI), native message
	//            boxes (MessageBoxUI), or the Menu Framework panel when iMode = 2.
	//   editor - the SKSE Menu Framework panel, reached by "Open panel..." in the hotkey menus (when loaded,
	//            not VR), else message boxes. May be the same object as `world`.
	struct StashUIs
	{
		std::shared_ptr<IStashUI> world;
		std::shared_ptr<IStashUI> editor;
	};
	[[nodiscard]] StashUIs CreateStashUIs();
}
