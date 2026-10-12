#include "UI/MenuFrameworkUI.h"

#include "Game/FormUtil.h"
#include "UI/MessageBox.h"
#include "UI/Panel/PanelModel.h"
#include "UI/Panel/PanelRender.h"

namespace dova::ui
{
	namespace
	{
		void Open(const home::Home& a_home, RE::TESObjectREFR* a_container, panel::Tab a_tab)
		{
			std::optional<core::FormRef> focus;
			if (a_container) {
				focus = game::ToFormRef(a_container);
			}
			// Snapshot first (we are on the main thread), so the first rendered frame is already current.
			panel::PanelModel::Get().Focus(a_home.cellId, std::move(focus), a_tab);
			panel::OpenWindow();
		}
	}

	std::unique_ptr<IStashUI> MenuFrameworkUI::Create()
	{
		if (!panel::Register()) {
			return nullptr;
		}
		panel::PanelModel::Get().BuildGlobal();
		return std::unique_ptr<IStashUI>(new MenuFrameworkUI());
	}

	void MenuFrameworkUI::OpenMasterMenu(home::Home& a_home, RE::TESObjectREFR* a_master)
	{
		Open(a_home, a_master, panel::Tab::Actions);
	}

	void MenuFrameworkUI::OpenContainerMenu(home::Home& a_home, RE::TESObjectREFR* a_container)
	{
		Open(a_home, a_container, panel::Tab::Containers);
	}

	void MenuFrameworkUI::OpenSetupPrompt(home::Home& a_home, RE::TESObjectREFR* a_container)
	{
		Open(a_home, a_container, panel::Tab::Actions);
	}

	void MenuFrameworkUI::Notify(std::string_view a_message)
	{
		logger::info("Notify: {}", a_message);
		ShowNotification(a_message);  // queued by the HUD while the (pausing) window is open
		panel::PanelModel::Get().SetStatus(std::string(a_message));
	}

	void MenuFrameworkUI::OnHomeChanged()
	{
		if (panel::PanelModel::Get().RecentlyRendered()) {
			panel::PanelModel::Get().RequestRefresh(panel::Refresh::Light);
		}
	}

	void MenuFrameworkUI::OnTemplatesChanged()
	{
		// The Templates and Diagnostics pages read the global snapshot; the home's template choices come
		// with the next home rebuild.
		panel::PanelModel::Get().BuildGlobal();
		panel::PanelModel::Get().RequestRefresh(panel::Refresh::Light);
	}

	bool MenuFrameworkUI::IsActive() const
	{
		return panel::PanelModel::Get().RecentlyRendered();
	}
}
