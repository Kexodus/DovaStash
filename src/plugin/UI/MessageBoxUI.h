#pragma once

// Native MessageBox implementation of the UI facade (DESIGN §10.1): the fallback and the VR path.
// Menus are chains of message boxes; long lists are paginated ("More..." / "Back").

#include "UI/IStashUI.h"

namespace dova::ui
{
	class MessageBoxUI final : public IStashUI
	{
	public:
		// `a_panel`: the Menu Framework panel, or null; "Open panel..." (under Advanced..., on the setup prompt and on
		// the container menu) opens it.
		explicit MessageBoxUI(std::shared_ptr<IStashUI> a_panel = {}) :
			_panel(std::move(a_panel))
		{}

		void OpenMasterMenu(home::Home& a_home, RE::TESObjectREFR* a_master) override;
		void OpenContainerMenu(home::Home& a_home, RE::TESObjectREFR* a_container) override;
		void OpenSetupPrompt(home::Home& a_home, RE::TESObjectREFR* a_container) override;
		void Notify(std::string_view a_message, bool a_failed = false) override;

	private:
		// Retrieve..., Browse containers..., Use / Re-apply template, Save as template..., Reset home..., Open panel...
		void OpenAdvancedMenu(RE::FormID a_cellId);
		void ReopenMasterMenu(RE::FormID a_cellId);
		void OpenRetrieveMenu(RE::FormID a_cellId);
		void OpenRetrieveRoot(RE::FormID a_cellId, const std::string& a_root);
		void OpenBrowseMenu(RE::FormID a_cellId);
		void OpenAssignRoots(RE::FormID a_cellId, const core::FormRef& a_container, std::size_t a_page = 0);
		// `a_rootPage`: the page of the root picker Back returns to.
		void OpenAssignChildren(RE::FormID a_cellId, const core::FormRef& a_container, const std::string& a_root, std::size_t a_rootPage, std::size_t a_page = 0);
		void OpenLearnedMenu(RE::FormID a_cellId, const core::FormRef& a_container, std::size_t a_page = 0);
		void ReopenContainerMenu(RE::FormID a_cellId, const core::FormRef& a_container);
		// Yes runs `a_onYes`; No runs `a_onNo` (the menu the question came from).
		void Confirm(std::string a_question, std::function<void()> a_onYes, std::function<void()> a_onNo = {});

		std::shared_ptr<IStashUI> _panel;
	};
}
