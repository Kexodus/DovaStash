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
//   master list:     Deposit, Sort all, Retrieve..., Browse..., template, Save as template..., Advanced...,
//                    (in an extra area: Rename <area>..., Remove this area...), Reset...
//   container list:  Categories..., Open, Rename..., Learned items..., Clear categories, Ignore,
//                    Remove..., Make master, Advanced...
//   categories:      list of category groups -> list of that group's categories ([x] toggles)
//   retrieve/browse: lists; Browse opens the chosen container's real ContainerMenu
//
// Every list starts with a header naming the menu ("-- Whitepeak Tower --", "-- Chest: Categories --");
// UIListMenu has no unselectable rows, so choosing the header does nothing.
//
// Lists stay flat (one level). A toggle ([x]/[ ]) keeps the menu open and updates its entries in
// place; if the player's listmenu.swf cannot do that, the list closes and reopens on the same entry.
// Confirmations use native message boxes. "Advanced..." opens the editor UI (the SKSE Menu Framework
// panel, or message boxes without it).

#include "UI/IStashUI.h"

namespace dova::ui
{
	class ListUI final : public IStashUI
	{
	public:
		// `a_editor` handles "Advanced..."; `a_fallback` is used when a list cannot open (another
		// custom menu is up).
		ListUI(std::shared_ptr<IStashUI> a_editor, std::shared_ptr<IStashUI> a_fallback);

		void OpenMasterMenu(home::Home& a_home, RE::TESObjectREFR* a_master) override;
		void OpenContainerMenu(home::Home& a_home, RE::TESObjectREFR* a_container) override;
		void OpenSetupPrompt(home::Home& a_home, RE::TESObjectREFR* a_container) override;
		void OpenDisplayMenu(const DisplayMenu& a_menu) override;
		void OpenArmorSetMenu(const ArmorSetMenu& a_menu) override;
		void Notify(std::string_view a_message) override;
		void OnHomeChanged() override;

	private:
		void OpenAssignGroups(RE::FormID a_cellId, const core::FormRef& a_container);
		void OpenDisplayCategories(RE::FormID a_cellId, const core::FormRef& a_display, core::DisplayKind a_kind, const std::string& a_name);
		void OpenAssignGroup(RE::FormID a_cellId, const core::FormRef& a_container, const std::string& a_root);
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
	};
}
