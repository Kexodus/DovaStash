#include "dova/core/TemplateDisplays.h"

#include <algorithm>

#include "dova/core/Template.h"

namespace dova::core
{
	namespace
	{
		std::string Where(const DisplayBinding& a_d)
		{
			return "display " + a_d.ref.ToString() + (a_d.label.empty() ? std::string() : " (" + a_d.label + ")");
		}
	}

	bool ItemsChosenByFile(std::optional<DisplayKind> a_kind) noexcept
	{
		return !a_kind || *a_kind == DisplayKind::Rack || *a_kind == DisplayKind::Mannequin;
	}

	void ValidateDisplays(const Layout& a_layout, std::string_view a_source, Diagnostics& a_diags)
	{
		const std::string source(a_source);
		for (const auto& d : a_layout.displays) {
			const auto where = Where(d);
			if (d.set && d.kind && *d.kind != DisplayKind::Mannequin) {
				a_diags.Warn(source, where + ": \"set\" is only used by mannequins (ignored)");
			}
			if (!d.categories.empty() && !ItemsChosenByFile(d.kind)) {
				a_diags.Warn(source, where + ": \"categories\" are only used by racks and mannequins (ignored)");
			}
			if (d.only) {
				if (!ItemsChosenByFile(d.kind)) {
					a_diags.Warn(source, where + ": \"only\" is for racks and mannequins; an item plaque or case always takes just what it is made for");
				} else if (d.items.empty()) {
					a_diags.Warn(source, where + ": \"only\" without \"items\" does nothing");
				} else if (d.set) {
					a_diags.Warn(source, where + ": \"only\" is ignored next to \"set\" (the set decides what the mannequin wears)");
				}
			}
			const auto* container = a_layout.FindContainer(d.ref);
			if (a_layout.IsMaster(d.ref) || (container && !container->ignored)) {
				a_diags.Warn(source, where + ": also listed as " + (a_layout.IsMaster(d.ref) ? "the master" : "a container") +
										 "; a container the layout stores items in is never filled as a display");
			}
		}
	}

	const TemplateCandidate* BasedOnTemplate(const Layout& a_layout, std::span<const TemplateCandidate> a_templates)
	{
		if (!a_layout.basedOn) {
			return nullptr;
		}
		const std::string_view basedOn = *a_layout.basedOn;
		for (const auto& t : a_templates) {
			if (basedOn.size() > t.source.size() && basedOn.starts_with(t.source) && basedOn[t.source.size()] == '@') {
				return &t;
			}
		}
		return nullptr;
	}

	bool ItemsCopiedFromTemplate(const DisplayBinding& a_entry, const DisplayBinding* a_templateEntry)
	{
		if (!a_templateEntry || (a_templateEntry->items.empty() && a_templateEntry->unparsedItems.empty())) {
			return false;
		}
		return a_entry.items == a_templateEntry->items && a_entry.unparsedItems == a_templateEntry->unparsedItems;
	}

	std::vector<MissingDisplayItems> MissingTemplateItems(const Layout& a_layout, const Layout& a_template, const DisplayKindOf& a_kindOf,
		const FormExists& a_exists)
	{
		std::vector<MissingDisplayItems> out;
		for (const auto& d : a_layout.displays) {
			const auto* t = a_template.FindDisplay(d.ref);
			if (d.ignored || !t) {
				continue;
			}
			auto kind = a_kindOf ? a_kindOf(d.ref) : std::nullopt;
			if (!kind) {
				kind = d.kind ? d.kind : t->kind;
			}
			if (!kind || !ItemsChosenByFile(kind)) {
				continue;  // not known yet, or an item plaque / case: its script decides, never reported
			}
			MissingDisplayItems missing;
			missing.display = d.ref;
			missing.label = !d.label.empty() ? d.label : !t->label.empty() ? t->label : d.ref.ToString();
			for (const auto& item : d.items) {
				const bool chosen = std::ranges::find(t->items, item) != t->items.end();
				if (chosen && std::ranges::find(missing.items, item) == missing.items.end() && !a_exists(item)) {
					missing.items.push_back(item);
				}
			}
			const bool mannequin = *kind == DisplayKind::Mannequin;
			if (mannequin && d.set && t->set && *d.set == *t->set && !a_exists(*d.set)) {
				missing.set = d.set;
			}
			if (missing.items.empty() && !missing.set) {
				continue;
			}
			const bool onlyApplies = d.only && !d.items.empty() && !(mannequin && d.set);
			missing.waits = missing.set.has_value() || (onlyApplies && std::ranges::none_of(d.items, [&](const FormRef& a_item) { return a_exists(a_item); }));
			out.push_back(std::move(missing));
		}
		return out;
	}

	std::string MissingItemsMessage(std::string_view a_template, const MissingDisplayItems& a_missing)
	{
		std::string what;
		const auto  n = a_missing.items.size();
		if (n > 0) {
			what = std::to_string(n) + (n == 1 ? " item" : " items");
		}
		if (a_missing.set) {
			what += what.empty() ? "its armor set" : " and its armor set";
		}
		const bool plural = n > 1 || (n == 1 && a_missing.set);
		std::string refs;
		for (const auto& item : a_missing.items) {
			refs += (refs.empty() ? "" : ", ") + item.ToString();
		}
		if (a_missing.set) {
			refs += (refs.empty() ? "" : ", ") + a_missing.set->ToString();
		}
		std::string out = "Template " + std::string(a_template) + ": " + a_missing.label + ": " + what + (plural ? " don't" : " doesn't") +
		                  " exist in this load order (" + refs + "); skipped";
		if (a_missing.waits) {
			out += ", nothing else goes on it until " + std::string(plural ? "they do" : "it does");
		}
		return out;
	}
}
