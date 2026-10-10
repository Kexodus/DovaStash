#pragma once

// Native MessageBox implementation of the UI facade (DESIGN §10.1): the fallback and the VR path.
// Menus are chains of message boxes; long lists are paginated ("More..." / "Back").

#include "UI/IStashUI.h"

namespace dova::ui
{
	class MessageBoxUI final : public IStashUI
	{
	public:
		void OpenMasterMenu(home::Home& a_home, RE::TESObjectREFR* a_master) override;
		void OpenContainerMenu(home::Home& a_home, RE::TESObjectREFR* a_container) override;
		void OpenSetupPrompt(home::Home& a_home, RE::TESObjectREFR* a_container) override;
		void Notify(std::string_view a_message) override;

	private:
		void OpenRetrieveMenu(RE::FormID a_cellId);
		void OpenRetrieveRoot(RE::FormID a_cellId, const std::string& a_root);
		void OpenAssignRoots(RE::FormID a_cellId, const core::FormRef& a_container, std::size_t a_page = 0);
		void OpenAssignChildren(RE::FormID a_cellId, const core::FormRef& a_container, const std::string& a_root, std::size_t a_page = 0);
		void ReopenContainerMenu(RE::FormID a_cellId, const core::FormRef& a_container);
		void Confirm(std::string a_question, std::function<void()> a_onYes);
	};
}
