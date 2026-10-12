#include "Hooks/ActivatePrompt.h"

#include "Events/EventSinks.h"
#include "Game/FormUtil.h"
#include "Home/HomeRegistry.h"
#include "Settings.h"
#include "UI/Labels.h"

namespace dova::hooks
{
	namespace
	{
		// Container prompts ("Open Chest (Weapons)"), and activators that open a hidden container
		// ("Search Food (Food & drink)", game::ContainerBehind). One thunk per vtable.
		template <class T>
		struct GetActivateText
		{
			static bool Thunk(T* a_this, RE::TESObjectREFR* a_ref, RE::BSString& a_dst)
			{
				const bool result = _original(a_this, a_ref, a_dst);
				if (result && Settings::GetSingleton().GetIntegrations().activatePrompt) {
					Decorate(a_this, a_ref, a_dst);
				}
				return result;
			}

			// The container a looked-at activator opens, from the published home (UI thread: no game
			// lookups beyond the form table).
			static RE::TESObjectREFR* Behind(RE::TESObjectREFR* a_front)
			{
				const auto home = home::HomeRegistry::Get().Display();
				if (!home) {
					return nullptr;
				}
				const auto it = home->fronts.find(a_front->GetFormID());
				auto*      form = it != home->fronts.end() ? RE::TESForm::LookupByID(it->second) : nullptr;
				return form ? form->AsReference() : nullptr;
			}

			static void Decorate(T* a_this, RE::TESObjectREFR* a_ref, RE::BSString& a_dst)
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
				auto* container = a_ref;
				if constexpr (std::is_same_v<T, RE::TESObjectACTI>) {
					container = Behind(a_ref);
					if (!container) {
						return;
					}
				}
				const auto summary = ui::RoleSummary(ui::DescribeContainer(container));
				if (summary.empty()) {
					return;
				}
				std::string text{ a_dst.c_str() ? a_dst.c_str() : "" };
				if (text.empty()) {
					return;
				}
				// Put the label on the line that holds the container's name. Prompt mods (e.g. Oblivion
				// Interaction Icons) add their own lines around it (an icon glyph above, "Empty" below), so the
				// end of the string is not necessarily the name. No name line found: append at the end.
				const std::string suffix = std::format(" ({})", summary);
				const std::string name = game::RefName(a_ref);
				std::size_t       insertAt = std::string::npos;
				std::size_t       lineStart = 0;
				while (lineStart <= text.size()) {
					auto lineEnd = text.find('\n', lineStart);
					if (lineEnd == std::string::npos) {
						lineEnd = text.size();
					}
					const std::string_view line(text.data() + lineStart, lineEnd - lineStart);
					if (!name.empty() && line.find(name) != std::string_view::npos) {
						insertAt = lineEnd;
						break;
					}
					lineStart = lineEnd + 1;
				}
				text.insert(insertAt == std::string::npos ? text.size() : insertAt, suffix);
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
		REL::Relocation<std::uintptr_t> cont{ RE::VTABLE_TESObjectCONT[0] };
		GetActivateText<RE::TESObjectCONT>::_original = cont.write_vfunc(0x4C, GetActivateText<RE::TESObjectCONT>::Thunk);
		REL::Relocation<std::uintptr_t> acti{ RE::VTABLE_TESObjectACTI[0] };
		GetActivateText<RE::TESObjectACTI>::_original = acti.write_vfunc(0x4C, GetActivateText<RE::TESObjectACTI>::Thunk);
		logger::info("Activate prompt: TESObjectCONT and TESObjectACTI GetActivateText hooked");
	}
}
