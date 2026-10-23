#include "UI/Labels.h"

#include "Controller.h"
#include "Game/FormUtil.h"
#include "Home/Store.h"
#include "Home/TemplateDisplays.h"
#include "Settings.h"
#include "Sort/SellChest.h"
#include "Sort/SortEngine.h"
#include "UI/AreaPrompt.h"

namespace dova::ui
{
	std::vector<std::pair<std::string, std::function<void()>>> SellEntries(const home::Home& a_home, const std::optional<core::FormRef>& a_chest,
		const std::function<void()>& a_onBack)
	{
		std::vector<std::pair<std::string, std::function<void()>>> out;
		const auto cellId = a_home.cellId;
		if (const auto quote = sort::selling::QuoteSale(a_home, a_chest); !quote.Empty()) {
			out.emplace_back(std::format(kSellNow, quote.gold), [cellId, a_chest, a_onBack]() { Controller::Get().SellNow(cellId, a_chest, a_onBack); });
		}
		if (const auto offer = sort::selling::BuyBackOffer(a_home)) {
			out.emplace_back(std::format(kBuyBack, offer->gold), [cellId, a_onBack]() { Controller::Get().BuyBack(cellId, a_onBack); });
		}
		return out;
	}

	namespace
	{
		bool MayNotFit(home::Home& a_home)
		{
			const auto* fit = home::HomeRegistry::Get().BestTemplateFit(a_home);  // the auto-enable guard (DESIGN §9.1)
			return fit && !fit->fits;
		}
	}

	bool AskBeforeTemplate(home::Home& a_home)
	{
		if (MayNotFit(a_home)) {
			return true;
		}
		if (!a_home.layout || a_home.layoutBroken) {
			return false;
		}
		const auto templates = home::Store::Get().Templates();
		return core::HasOwnChanges(*a_home.layout, core::FindBasedOn(*a_home.layout, templates), [](core::Layout& a_copy) { home::RecordDisplayItems(a_copy, false); });
	}

	std::string TemplateEntry(home::Home& a_home)
	{
		const auto* best = a_home.BestTemplate();
		if (!best) {
			return {};
		}
		const bool basedOn = a_home.layout && a_home.layout->basedOn && a_home.layout->basedOn->starts_with(best->source + "@");
		auto       entry = std::format("{} template \"{}\"", basedOn ? "Re-apply" : "Use", best->layout.name);
		if (MayNotFit(a_home)) {
			entry += std::format(" ({})", kMayNotFit);
		}
		return entry;
	}

	std::string TemplateQuestion(home::Home& a_home)
	{
		const auto* best = a_home.BestTemplate();
		const bool  basedOn = best && a_home.layout && a_home.layout->basedOn && a_home.layout->basedOn->starts_with(best->source + "@");
		const auto  name = best ? best->layout.name : ""s;
		auto        text = basedOn ? std::format(kConfirmReapplyTemplate, name) : std::format(kConfirmUseTemplate, name);
		if (MayNotFit(a_home)) {
			text += kMayNotFitNote;
		}
		return text;
	}

	std::string ForgetLearnedQuestion(std::size_t a_count, std::string_view a_name)
	{
		return std::format(kConfirmForgetAll, std::format("{} learned item{}", a_count, a_count == 1 ? "" : "s"), a_name);
	}

	std::string ForgetAllEntry(std::size_t a_count)
	{
		return a_count == 1 ? "Forget 1 learned item..."s : std::format("Forget all {} learned items...", a_count);
	}

	std::string BrowseEntryText(RE::TESObjectREFR* a_ref, std::string_view a_what)
	{
		std::int64_t items = 0;
		if (a_ref) {
			for (const auto& [object, data] : a_ref->GetInventory()) {
				if (object && data.first > 0) {
					items += data.first;
				}
			}
		}
		const auto name = game::RefName(a_ref);
		const auto count = std::format("{} item{}", items, items == 1 ? "" : "s");
		return a_what.empty() || core::IEquals(a_what, name) ? std::format("{} ({})", name, count) : std::format("{} - {} ({})", name, a_what, count);
	}

	std::string MakeMasterQuestion(std::string_view a_name, bool a_forgetsSetup)
	{
		return a_forgetsSetup ? std::format(kConfirmMakeMasterForgets, a_name) : std::format(kConfirmMakeMaster, a_name);
	}

	std::vector<MenuEntry> ContainerMenuEntries(home::Home& a_home, RE::TESObjectREFR* a_container, const ContainerMenuHooks& a_hooks)
	{
		std::vector<MenuEntry> out;
		const auto             ref = game::ToFormRef(a_container);
		if (!ref || !a_home.layout) {
			return out;
		}
		const auto  cellId = a_home.cellId;
		const auto& layout = *a_home.layout;
		const auto* binding = layout.FindContainer(*ref);
		const auto  name = game::RefName(a_container);
		const bool  sellChest = layout.IsSellChest(*ref);
		const bool  ignored = binding && binding->ignored;
		const auto  learned = static_cast<std::size_t>(std::ranges::count_if(layout.learned, [&](const auto& a_pair) { return a_pair.second == *ref; }));

		out.push_back({ a_hooks.categoriesText, a_hooks.openCategories });
		out.push_back({ std::format("Open {}", name), [cellId, ref = *ref]() { Controller::Get().Browse(cellId, ref); } });
		if (sellChest) {
			for (auto& [text, action] : SellEntries(a_home, *ref, a_hooks.back)) {
				out.push_back({ std::move(text), std::move(action) });
			}
		}
		if (a_hooks.rename) {
			out.push_back({ "Rename...", a_hooks.rename });
			if (HasCustomName(a_container)) {
				out.push_back({ "Restore original name", [cellId, ref = *ref]() { Controller::Get().SetLabel(cellId, ref, {}); } });
			}
		}
		if (learned > 0) {
			out.push_back({ std::format(kLearnedEntry, learned), a_hooks.openLearned });
		}
		const auto confirm = a_hooks.confirm;
		if (binding && !binding->categories.empty()) {
			out.push_back({ kClearCategoriesEntry, [confirm, cellId, ref = *ref, question = std::format(kConfirmClearCategories, name)]() {
							   confirm(question, [cellId, ref]() { Controller::Get().ClearCategories(cellId, ref); });
						   } });
		}
		out.push_back({ ignored ? "Stop ignoring" : "Ignore", [cellId, ref = *ref, ignored]() { Controller::Get().SetIgnored(cellId, ref, !ignored); } });
		if (sellChest) {
			out.push_back({ kNotSellChest, [cellId, ref = *ref]() { Controller::Get().SetSellChest(cellId, ref, false); } });
		} else if (!ignored) {
			out.push_back({ kMakeSellChest, [confirm, cellId, ref = *ref, question = SellChestQuestion(name)]() {
							   confirm(question, [cellId, ref]() { Controller::Get().SetSellChest(cellId, ref, true); });
						   } });
		}
		if (binding || learned > 0) {
			out.push_back({ "Remove...", [confirm, cellId, ref = *ref, question = std::format(kConfirmRemove, name, kRemoveExplained)]() {
							   confirm(question, [cellId, ref]() { Controller::Get().RemoveContainer(cellId, ref); });
						   } });
		}
		// What Make master forgets (DESIGN §10.3): the container's binding (categories, sell chest, ignored) and the
		// items learned for it.
		out.push_back({ kMakeMasterEntry, [confirm, cellId, ref = *ref, question = MakeMasterQuestion(name, binding || learned > 0)]() {
						   confirm(question, [cellId, ref]() { Controller::Get().MakeMaster(cellId, ref); });
					   } });
		if (a_hooks.openPanel) {
			out.push_back({ kOpenPanel, a_hooks.openPanel });
		}
		return out;
	}

	std::string SellChestQuestion(std::string_view a_name)
	{
		auto       text = std::format(kConfirmSellChest, a_name);
		const auto hours = Settings::GetSingleton().GetSelling().autoSellHours;
		if (hours > 0) {
			text += std::format(kTimedSellNote, hours);
		}
		return text;
	}

	namespace
	{
		std::string Capitalize(std::string a_text)
		{
			if (!a_text.empty() && a_text[0] >= 'a' && a_text[0] <= 'z') {
				a_text[0] = static_cast<char>(a_text[0] - 'a' + 'A');
			}
			return a_text;
		}
	}

	std::string RootOf(std::string_view a_id)
	{
		const auto dot = a_id.find('.');
		return std::string(a_id.substr(0, dot));
	}

	std::string CategoryName(std::string_view a_id)
	{
		const auto* cat = home::Store::Get().Categories().Find(a_id);
		return cat && !cat->name.empty() ? cat->name : Capitalize(std::string(a_id));
	}

	std::string PatternLabel(std::string_view a_pattern)
	{
		if (a_pattern == "*") {
			return "Everything";
		}
		if (a_pattern.ends_with(".*")) {
			return "All " + CategoryName(a_pattern.substr(0, a_pattern.size() - 2));
		}
		return CategoryName(a_pattern);
	}

	std::string PatternShortLabel(std::string_view a_pattern)
	{
		if (a_pattern == "*") {
			return "Everything";
		}
		if (a_pattern.ends_with(".*")) {
			a_pattern.remove_suffix(2);
		}
		return CategoryName(a_pattern);
	}

	std::vector<std::string> RootCategories()
	{
		std::vector<std::string> roots;
		for (const auto& c : home::Store::Get().Categories().Categories()) {
			auto root = RootOf(c.id);
			if (std::ranges::find(roots, root) == roots.end()) {
				roots.push_back(std::move(root));
			}
		}
		return roots;
	}

	std::vector<std::string> ChildCategories(const std::string& a_root)
	{
		std::vector<std::string> out;
		for (const auto& c : home::Store::Get().Categories().Categories()) {
			if (c.id != a_root && RootOf(c.id) == a_root) {
				out.push_back(c.id);
			}
		}
		return out;
	}

	std::int32_t CountMatching(const std::map<std::string, std::int32_t>& a_counts, std::string_view a_pattern)
	{
		std::int32_t n = 0;
		for (const auto& [id, count] : a_counts) {
			if (!id.empty() && core::CategoryPatternMatches(a_pattern, id)) {
				n += count;
			}
		}
		return n;
	}

	std::string LearnedSuffix(const std::map<std::string, std::int32_t>& a_learned, std::string_view a_pattern)
	{
		const auto n = CountMatching(a_learned, a_pattern);
		return n > 0 ? std::format(" ({} learned)", n) : std::string{};
	}

	std::map<std::string, std::int32_t> LearnedOf(RE::FormID a_cellId, const core::FormRef& a_container)
	{
		auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
		return home && home->layout ? sort::LearnedByCategory(*home->layout, a_container) : std::map<std::string, std::int32_t>{};
	}

	bool HasPattern(const core::ContainerBinding* a_binding, std::string_view a_pattern)
	{
		return a_binding && std::ranges::any_of(a_binding->categories, [&](const std::string& p) { return core::IEquals(p, a_pattern); });
	}

	bool Covers(const core::ContainerBinding* a_binding, std::string_view a_id)
	{
		return a_binding && std::ranges::any_of(a_binding->categories, [&](const std::string& p) { return core::CategoryPatternMatches(p, a_id); });
	}

	ContainerRole DescribeContainer(RE::TESObjectREFR* a_ref)
	{
		ContainerRole role;
		auto          home = home::HomeRegistry::Get().Display();
		if (!a_ref || !home || !home->layout || a_ref->GetParentCell() == nullptr || a_ref->GetParentCell()->GetFormID() != home->cellId) {
			return role;
		}
		role.home = home;
		const auto ref = game::ToFormRef(a_ref);
		if (!ref) {
			return role;
		}
		const auto& layout = *home->layout;
		if (layout.IsMaster(*ref)) {
			role.kind = ContainerRole::Kind::Master;
			return role;
		}
		for (const auto& [item, container] : layout.learned) {
			if (container == *ref) {
				++role.learned;
			}
		}
		if (const auto it = home->learnedCategories.find(*ref); it != home->learnedCategories.end()) {
			role.learnedCategories = &it->second;
		}
		role.binding = layout.FindContainer(*ref);
		if (!role.binding) {
			role.kind = ContainerRole::Kind::Unmanaged;
		} else if (role.binding->ignored) {
			role.kind = ContainerRole::Kind::Ignored;
		} else {
			role.kind = ContainerRole::Kind::Bound;
		}
		return role;
	}

	std::string RoleSummary(const ContainerRole& a_role, std::size_t a_maxPatterns)
	{
		switch (a_role.kind) {
		case ContainerRole::Kind::Master:
			return "Master";
		case ContainerRole::Kind::Bound:
			{
				const auto& patterns = a_role.binding->categories;
				std::string out;
				const auto  shown = std::min(patterns.size(), a_maxPatterns);
				for (std::size_t i = 0; i < shown; ++i) {
					out += (i == 0 ? "" : ", ") + PatternShortLabel(patterns[i]);
				}
				if (patterns.size() > shown) {
					out += std::format(" +{}", patterns.size() - shown);
				}
				if (a_role.binding->sell) {
					return out.empty() ? "Sell"s : "Sell: " + out;  // the sell chest says so first ("Chest (Sell: Clutter)")
				}
				if (out.empty() && a_role.learned > 0) {
					// Learned items only: where they belong ("Books, learned"), else how many there are.
					const auto* learned = a_role.learnedCategories;
					if (learned && !learned->empty()) {
						const auto shownLearned = std::min(learned->size(), a_maxPatterns);
						for (std::size_t i = 0; i < shownLearned; ++i) {
							out += (i == 0 ? "" : ", ") + CategoryName((*learned)[i].first);
						}
						if (learned->size() > shownLearned) {
							out += std::format(" +{}", learned->size() - shownLearned);
						}
						out += ", learned";
					} else {
						out = std::format("{} learned item{}", a_role.learned, a_role.learned == 1 ? "" : "s");
					}
				}
				return out;
			}
		default:
			return {};
		}
	}
}
