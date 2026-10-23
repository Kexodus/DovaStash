#include "UI/AreaPrompt.h"

#include "Controller.h"
#include "Game/FormUtil.h"
#include "Home/Areas.h"
#include "Home/HomeRegistry.h"
#include "UI/UIExtensions.h"

namespace dova::ui
{
	namespace
	{
		bool Blank(const std::optional<std::string>& a_text)
		{
			return !a_text || a_text->find_first_not_of(" \t") == std::string::npos;
		}
	}

	void AddAreaNamed(RE::FormID a_cellId)
	{
		const auto offer = home::HomeRegistry::Get().OfferArea();
		if (!offer || offer->cellId != a_cellId) {
			Controller::Get().AddArea(a_cellId);  // reports why not
			return;
		}
		auto*      cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(a_cellId);
		const auto cellName = home::AreaName(cell, offer->cell);
		if (!uiext::IsInstalled()) {
			Controller::Get().AddArea(a_cellId);
			return;
		}
		const bool opened = uiext::OpenTextEntry(cellName, [a_cellId, cellName](std::optional<std::string> a_text) {
			// The text box reports an empty text when it is cancelled: the area is added all the same, unnamed.
			// The cell's own name typed back is not stored either (the area shows it anyway).
			if (Blank(a_text) || *a_text == cellName) {
				logger::debug("Add area: no name given");
				Controller::Get().AddArea(a_cellId);
			} else {
				Controller::Get().AddArea(a_cellId, *a_text);
			}
		});
		if (!opened) {
			Controller::Get().AddArea(a_cellId);  // another menu is up: add it now rather than lose the add
		}
	}

	bool RenameAreaNamed(RE::FormID a_cellId, const core::FormRef& a_area, std::string a_current)
	{
		if (!uiext::IsInstalled()) {
			return false;
		}
		return uiext::OpenTextEntry(std::move(a_current), [a_cellId, a_area](std::optional<std::string> a_text) {
			// Cancelled (empty) never clears a name; the panel's Rename with an empty box does that.
			if (!Blank(a_text)) {
				Controller::Get().RenameArea(a_cellId, a_area, *a_text);
			} else {
				logger::debug("Rename area: cancelled");
			}
		});
	}

	bool RenameContainerNamed(RE::FormID a_cellId, const core::FormRef& a_container)
	{
		auto* ref = game::LookupRef(a_container);
		if (!ref || !uiext::IsInstalled()) {
			return false;
		}
		return uiext::OpenTextEntry(game::RefName(ref), [a_cellId, a_container](std::optional<std::string> a_text) {
			// The text box reports an empty text when it is cancelled: that must never clear a name.
			if (!Blank(a_text)) {
				Controller::Get().SetLabel(a_cellId, a_container, *a_text);
			}
		});
	}

	bool HasCustomName(RE::TESObjectREFR* a_ref)
	{
		return a_ref && a_ref->extraList.HasType(RE::ExtraDataType::kTextDisplayData);
	}
}
