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

		// The categories a used display can't take (core::ValidateDisplayCategories): kept in the layout, never put
		// up; logged once per session per display and category.
		void ReportUnfitCategories(const core::Layout& a_layout)
		{
			const auto& categories = Store::Get().Categories();
			for (const auto& b : a_layout.displays) {
				if (b.ignored || b.categories.empty()) {
					continue;
				}
				auto*      ref = game::LookupRef(b.ref);
				const auto found = ref ? game::InspectDisplay(ref) : std::nullopt;
				if (!found || found->kind == core::DisplayKind::Item || found->kind == core::DisplayKind::Case) {
					continue;  // not loaded or not recognised; plaques and cases: the template lint's
				}
				// Whether a rack has a crossbow node needs its trigger's 3D, which may not be loaded yet: not judged here.
				auto fit = game::FitOf(*found);
				fit.crossbows = true;
				const auto unfit = core::UnfitDisplayCategories(categories, b.categories, fit);
				for (const auto& p : unfit) {
					if (log::FirstTime("display-category:" + b.ref.ToString() + ":" + p)) {
						logger::warn("Displays: {}: {}; ignored{}", game::Describe(ref), core::UnfitCategoryMessage(categories, p, fit),
							unfit.size() == b.categories.size() ? " (nothing goes on it)" : "");
					}
				}
			}
		}
	}

	bool RecordDisplayItems(core::Layout& a_layout, bool a_log)
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
			if (a_log && templateCopy) {
				logger::debug("Display {}: made for {} item(s) by its script; the template's list ({}) is not used", game::Describe(ref), items.size(),
					b.items.size() + b.unparsedItems.size());
			} else if (a_log) {
				logger::debug("Display {}: made for {} item(s); written to its layout entry", game::Describe(ref), items.size());
			}
			b.items = std::move(items);
			b.unparsedItems.clear();  // only ever the template's copy here (the template file keeps its list)
			changed = true;
		}
		if (tmpl && a_log) {
			ReportMissingTemplateItems(a_layout, *tmpl);
		}
		if (a_log) {
			ReportUnfitCategories(a_layout);
		}
		return changed;
	}
}
