#pragma once

// In-world UI on UIExtensions' list menu (DESIGN §10.3): plain top-down, scrollable lists.
//
// Skin-agnostic by construction: we only use UIExtensions' own list-menu contract (see
// UI/UIExtensions.h), never anything a particular skin adds, and entries are plain text (no colours,
// fonts or HTML). Whatever replaces UIExtensions' listmenu.swf (any skin, or none) draws the lists.
// Where the menu allows it (UI/UIExtensions.h, "optional extras"), category entries also show their
// category's icon from the player's SkyUI icon SWFs (Icons/IconService: the same icons as the overlay
// and QuickLoot), containers their container icon, and the Dova Stash logo sits above every list.
//
//   setup prompt:    Restore your last setup, Make <container> the master, Use template "...",
//                    Add this area to <home>, Open panel...
//   master list:     Deposit, Restock, Sort all, Retrieve..., Advanced..., Sell now / Buy back..., Rename <master>...,
//                    Restore original name, (in an extra area: Rename <area>..., Remove this area from <home>...)
//                    Also opened at an area master (cells[].master). Titled "<home> (master not here)" only when
//                    no master takes the leftovers from here (home::CatchAllHere).
//   advanced:        Browse containers..., Use / Re-apply template, Save as template..., Reset home..., Open panel...
//   container list:  Categories... (summary), Open <container>, Sell now / Buy back..., Rename...,
//                    Restore original name, Learned items (N)..., Clear categories..., Ignore / Stop ignoring,
//                    Make sell chest... / Stop selling from it, Remove..., Make master..., Open panel...
//                    (ui::ContainerMenuEntries: the message boxes list the same, in the same order)
//   learned items:   Forget all N learned items... (asks), Forget <item>
//   categories:      list of category groups -> list of that group's categories ([x] toggles)
//   retrieve/browse: lists; Browse opens the chosen container's real ContainerMenu
//
// "Open panel..." (the SKSE Menu Framework panel) is listed only when the panel is loaded; message boxes
// would only repeat these lists. Optional entries (Restock, selling, Restore original name, Learned items,
// Clear categories, areas) show only when they apply.
//
// Every list starts with a header naming the menu ("-- Whitepeak Tower --", "-- Chest: Categories --");
// UIListMenu has no unselectable rows, so choosing the header does nothing.
//
// Lists stay flat (one level). A toggle ([x]/[ ]) keeps the menu open and updates its entries in
// place; if the player's listmenu.swf cannot do that, the list closes and reopens on the same entry.
// Confirmations use native message boxes.

#include "UI/IStashUI.h"

namespace dova::ui
{
	class ListUI final : public IStashUI
	{
	public:
		// `a_editor` handles "Open panel..." (offered only when it is the panel); `a_fallback` is used when a
		// list cannot open (another custom menu is up).
		ListUI(std::shared_ptr<IStashUI> a_editor, std::shared_ptr<IStashUI> a_fallback);

		void OpenMasterMenu(home::Home& a_home, RE::TESObjectREFR* a_master) override;
		void OpenContainerMenu(home::Home& a_home, RE::TESObjectREFR* a_container) override;
		void OpenSetupPrompt(home::Home& a_home, RE::TESObjectREFR* a_container) override;
		void OpenPartOfPrompt(home::Home& a_home, std::string_view a_template) override;
		void OpenDisplayMenu(const DisplayMenu& a_menu) override;
		void OpenArmorSetMenu(const ArmorSetMenu& a_menu) override;
		void Notify(std::string_view a_message, bool a_failed = false) override;
		void OnHomeChanged() override;

	private:
		void OpenAssignGroups(RE::FormID a_cellId, const core::FormRef& a_container);
		void OpenDisplayCategories(RE::FormID a_cellId, const core::FormRef& a_display, const core::DisplayFit& a_fit, const std::string& a_name);
		void OpenAssignGroup(RE::FormID a_cellId, const core::FormRef& a_container, const std::string& a_root);
		void OpenAdvanced(RE::FormID a_cellId);
		void OpenRetrieve(RE::FormID a_cellId);
		void OpenRetrieveGroup(RE::FormID a_cellId, const std::string& a_root);
		void OpenBrowse(RE::FormID a_cellId);
		void OpenRename(RE::FormID a_cellId, const core::FormRef& a_container);
		void OpenLearned(RE::FormID a_cellId, const core::FormRef& a_container);
		void OpenEditor(RE::FormID a_cellId, std::optional<core::FormRef> a_container);
		void ReopenContainer(RE::FormID a_cellId, const core::FormRef& a_container);
		void ReopenMaster(RE::FormID a_cellId);

		std::shared_ptr<IStashUI> _editor;
		std::shared_ptr<IStashUI> _fallback;
		// The editor is the Menu Framework panel, not the message-box fallback (CreateStashUIs passes the same
		// object for both when the framework is not loaded): only then do the lists offer "Open panel...".
		bool _editorIsPanel;
	};
}
