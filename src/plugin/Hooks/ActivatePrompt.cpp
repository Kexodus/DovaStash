#include "Hooks/ActivatePrompt.h"

#include "Events/EventSinks.h"
#include "Game/FormUtil.h"
#include "Settings.h"
#include "UI/Labels.h"

namespace dova::hooks
{
	namespace
	{
		struct GetActivateText
		{
			static bool Thunk(RE::TESObjectCONT* a_this, RE::TESObjectREFR* a_ref, RE::BSString& a_dst)
			{
				const bool result = _original(a_this, a_ref, a_dst);
				if (result && Settings::GetSingleton().GetIntegrations().activatePrompt) {
					Decorate(a_this, a_ref, a_dst);
				}
				return result;
			}

			static void Decorate(RE::TESObjectCONT* a_this, RE::TESObjectREFR* a_ref, RE::BSString& a_dst)
			{
				// The engine passes the reference being looked at. Verify that rather than trust it, and
				// fall back to the crosshair target (same base object) if it ever is not.
				RE::NiPointer<RE::TESObjectREFR> hold;
				if (!a_ref || a_ref->GetBaseObject() != a_this) {
					hold = game::RefFromHandle(events::CrosshairTarget());
					a_ref = hold.get();
					if (!a_ref || a_ref->GetBaseObject() != a_this) {
						return;
					}
				}
				const auto summary = ui::RoleSummary(ui::DescribeContainer(a_ref));
				if (summary.empty()) {
					return;
				}
				std::string text{ a_dst.c_str() ? a_dst.c_str() : "" };
				if (text.empty()) {
					return;
				}
				text += std::format(" ({})", summary);
				a_dst = text.c_str();

				static RE::FormID lastRef{ 0 };
				static std::string lastText;
				if (lastRef != a_ref->GetFormID() || lastText != text) {
					lastRef = a_ref->GetFormID();
					lastText = text;
					logger::debug("Activate prompt: {} -> \"{}\"", game::Describe(a_ref), text);
				}
			}

			static inline REL::Relocation<decltype(Thunk)> _original;
		};
	}

	void InstallActivatePrompt()
	{
		// Always hooked; [Integrations] bActivatePrompt is checked per call so it can be toggled live.
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_TESObjectCONT[0] };
		GetActivateText::_original = vtbl.write_vfunc(0x4C, GetActivateText::Thunk);
		logger::info("Activate prompt: TESObjectCONT::GetActivateText hooked");
	}
}
