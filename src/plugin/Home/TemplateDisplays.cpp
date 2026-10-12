#include "Home/TemplateDisplays.h"

#include "Game/Displays.h"
#include "Game/FormUtil.h"
#include "Home/Store.h"
#include "Log.h"

namespace dova::home
{
	namespace
	{
		// The display's kind as its script makes it (nullopt: not loaded, or no script Dova Stash reads).
		std::optional<core::DisplayKind> ScriptKind(const core::FormRef& a_display)
		{
			auto*      ref = game::LookupRef(a_display);
			const auto found = ref ? game::InspectDisplay(ref) : std::nullopt;
			return found ? std::optional(found->kind) : std::nullopt;
		}

		bool Exists(const core::FormRef& a_form)
		{
			const auto* form = game::LookupForm(a_form);
			return form && form->As<RE::TESBoundObject>();
		}

		void ReportMissingTemplateItems(const core::Layout& a_layout, const core::TemplateCandidate& a_template)
		{
			const auto name = a_template.layout.name.empty() ? a_template.source : a_template.layout.name;
			for (const auto& missing : core::MissingTemplateItems(a_layout, a_template.layout, ScriptKind, Exists)) {
				if (log::FirstTime("template-display-items:" + a_template.source + ":" + missing.display.ToString())) {
					logger::info("{}", core::MissingItemsMessage(name, missing));
				}
			}
		}
	}

	bool RecordDisplayItems(core::Layout& a_layout)
	{
		const auto* tmpl = core::BasedOnTemplate(a_layout, Store::Get().Templates());
		bool        changed = false;
		for (auto& b : a_layout.displays) {
			if (b.ignored) {
				continue;
			}
			const bool hasItems = !b.items.empty() || !b.unparsedItems.empty();
			const bool templateCopy = hasItems && tmpl && core::ItemsCopiedFromTemplate(b, tmpl->layout.FindDisplay(b.ref));
			if (hasItems && !templateCopy) {
				continue;  // the player's own list (or the script's, written before): kept
			}
			auto*      ref = game::LookupRef(b.ref);
			const auto found = ref ? game::InspectDisplay(ref) : std::nullopt;
			if (!found || (found->kind != core::DisplayKind::Item && found->kind != core::DisplayKind::Case)) {
				continue;
			}
			std::vector<core::FormRef> items;
			for (const auto* item : found->items) {
				if (auto r = game::ToFormRef(item); r && std::ranges::find(items, *r) == items.end()) {
					items.push_back(std::move(*r));
				}
			}
			if (items.empty() || (templateCopy && items == b.items && b.unparsedItems.empty())) {
				continue;
			}
			if (templateCopy) {
				logger::debug("Display {}: made for {} item(s) by its script; the template's list ({}) is not used", game::Describe(ref), items.size(),
					b.items.size() + b.unparsedItems.size());
			} else {
				logger::debug("Display {}: made for {} item(s); written to its layout entry", game::Describe(ref), items.size());
			}
			b.items = std::move(items);
			b.unparsedItems.clear();  // only ever the template's copy here (the template file keeps its list)
			changed = true;
		}
		if (tmpl) {
			ReportMissingTemplateItems(a_layout, *tmpl);
		}
		return changed;
	}
}
