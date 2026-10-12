#include "UI/SaveTemplate.h"

#include "Controller.h"
#include "Home/HomeRegistry.h"
#include "UI/MessageBox.h"
#include "UI/UIExtensions.h"

namespace dova::ui
{
	namespace
	{
		void Save(RE::FormID a_cellId, std::string a_name, bool a_overwrite)
		{
			Controller::ExportRequest request;
			request.name = std::move(a_name);
			request.overwrite = a_overwrite;
			const auto result = Controller::Get().ExportTemplate(a_cellId, request);
			switch (result.status) {
			case Controller::ExportResult::Status::Ok:
				return;  // the Controller reported it on the HUD
			case Controller::ExportResult::Status::Exists:
				{
					auto question = std::format("Replace the template \"{}\"?\n{}{} is saved as v{}; the old file is backed up.", result.name, result.source,
						result.existingVersion.empty() ? ""s : std::format(" (v{})", result.existingVersion), result.version);
					if (result.existingOtherHome) {
						question += "\nIt is the template of another home.";
					}
					ShowMessageBox(question, { "Yes", "No" }, [a_cellId, name = result.name](std::size_t a_button) {
						if (a_button == 0) {
							Save(a_cellId, name, true);
						}
					});
					return;
				}
			default:
				logger::warn("Save as template: not saved: {}", result.message);
				Controller::Get().Notify("Dova Stash: template not saved (see log)");
				return;
			}
		}
	}

	void SaveAsTemplate(RE::FormID a_cellId)
	{
		auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		auto name = home->name;
		if (!uiext::IsInstalled()) {
			Save(a_cellId, std::move(name), false);
			return;
		}
		const bool opened = uiext::OpenTextEntry(std::move(name), [a_cellId](std::optional<std::string> a_text) {
			// The text box reports an empty text when it is cancelled: nothing is saved then.
			if (a_text && a_text->find_first_not_of(" \t") != std::string::npos) {
				Save(a_cellId, *a_text, false);
			} else {
				logger::debug("Save as template: cancelled");
			}
		});
		if (!opened) {
			Controller::Get().Notify("Dova Stash: close the open menu first");
		}
	}
}
