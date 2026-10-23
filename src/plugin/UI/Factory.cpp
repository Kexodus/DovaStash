#include "UI/IStashUI.h"

#include "Settings.h"
#include "UI/MenuFramework.h"
#include "UI/MenuFrameworkUI.h"
#include "UI/MessageBoxUI.h"
#include "UI/ListUI.h"
#include "UI/UIExtensions.h"

namespace dova::ui
{
	StashUIs CreateStashUIs()
	{
		const auto mode = Settings::GetSingleton().GetUI().mode;

		// Editor: the Menu Framework panel registers its pages in the framework's own menu whenever the
		// framework is loaded, whatever the hotkey opens.
		// Created once: the framework has no way to unregister pages, and UIs are rebuilt when the UI
		// mode changes from the settings page.
		static std::shared_ptr<IStashUI> panel;
		static bool                      panelTried = false;
		const bool                       frameworkLoaded = menu_framework::IsInstalled() && menu_framework::IsLoaded();
		if (!panelTried && frameworkLoaded && (mode == 2 || !REL::Module::IsVR())) {
			panelTried = true;
			panel = MenuFrameworkUI::Create();
			if (!panel) {
				logger::warn("UI: SKSE Menu Framework did not accept the panel");
			}
		}
		const auto messageBoxes = std::make_shared<MessageBoxUI>(panel);  // "Open panel..." under Advanced...

		StashUIs uis;
		uis.editor = panel ? panel : messageBoxes;

		switch (mode) {
		case 1:
			uis.world = messageBoxes;
			logger::info("UI: in-world menus are native message boxes (iMode = 1)");
			break;
		case 2:
			if (panel) {
				uis.world = panel;
				logger::info("UI: in-world menus are the SKSE Menu Framework panel (iMode = 2)");
			} else {
				uis.world = messageBoxes;
				logger::warn("UI: iMode = 2 but SKSE Menu Framework is not loaded; using native message boxes");
			}
			break;
		default:  // 0 auto, 3 UIExtensions
			if (uiext::IsInstalled()) {
				uis.world = std::make_shared<ListUI>(uis.editor, messageBoxes);
				logger::info("UI: in-world menus are UIExtensions list menus");
			} else {
				uis.world = messageBoxes;
				if (mode == 3) {
					logger::warn("UI: iMode = 3 but UIExtensions is not installed; using native message boxes");
				} else {
					logger::info("UI: in-world menus are native message boxes (UIExtensions not installed)");
				}
			}
			break;
		}
		logger::info("UI: editor is {}", panel ? "the SKSE Menu Framework panel" : "native message boxes");
		return uis;
	}
}
