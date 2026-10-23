#include "UI/MenuFrameworkUI.h"

#include "Game/FormUtil.h"
#include "Home/HomeRegistry.h"
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

	void MenuFrameworkUI::Notify(std::string_view a_message, bool a_failed)
	{
		logger::info("Notify: {}", a_message);
		ShowNotification(a_message);  // queued by the HUD while the (pausing) window is open
		// Our own window needs no "Dova Stash:" prefix (UI/Panel/Strings.h); the HUD keeps it.
		std::string status(a_message);
		if (constexpr std::string_view prefix = "Dova Stash: "; status.starts_with(prefix)) {
			status.erase(0, prefix.size());
			if (!status.empty() && status[0] >= 'a' && status[0] <= 'z') {
				status[0] = static_cast<char>(status[0] - 'a' + 'A');
			}
		}
		// A failure shows in red: the caller says which (Controller::Notify's a_failed).
		const auto here = home::HomeRegistry::Get().Display();  // published copy: any thread
		panel::PanelModel::Get().SetStatus(std::move(status), a_failed, here ? here->cellId : 0);
	}

	void MenuFrameworkUI::OnHomeChanged()
	{
		if (panel::PanelModel::Get().RecentlyRendered()) {
			panel::PanelModel::Get().RequestRefresh(panel::Refresh::Light);
		}
	}

	void MenuFrameworkUI::OnTemplatesChanged()
	{
		// All templates (This home) and About's Diagnostics read the global snapshot; the home's template choices come
		// with the next home rebuild.
		panel::PanelModel::Get().BuildGlobal();
		panel::PanelModel::Get().RequestRefresh(panel::Refresh::Light);
	}

	bool MenuFrameworkUI::IsActive() const
	{
		return panel::PanelModel::Get().RecentlyRendered();
	}
}
