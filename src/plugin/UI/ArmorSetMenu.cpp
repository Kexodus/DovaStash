#include "UI/ArmorSetMenu.h"

#include "Controller.h"
#include "Game/ArmorIndex.h"
#include "Game/Displays.h"
#include "Game/FormUtil.h"
#include "UI/MessageBox.h"

namespace dova::ui
{
	namespace
	{
		constexpr std::size_t kMaxChoices = 100;  // UIListMenu takes at most 128 entries
		constexpr std::size_t kPerPage = 5;       // + More / Back = at most 7 buttons

		const core::DisplayBinding* FindBinding(RE::FormID a_cellId, const core::FormRef& a_mannequin)
		{
			auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
			return home && home->layout ? home->layout->FindDisplay(a_mannequin) : nullptr;
		}

		// The message-box picker: a page of choices, More..., Back (to the display menu).
		void ShowSetBox(std::shared_ptr<const ArmorSetMenu> a_menu, std::size_t a_page)
		{
			const auto& choices = a_menu->choices;
			const auto  pages = std::max<std::size_t>(1, (choices.size() + kPerPage - 1) / kPerPage);
			a_page = std::min(a_page, pages - 1);

			std::vector<std::string>           labels;
			std::vector<std::function<void()>> actions;
			for (auto i = a_page * kPerPage; i < std::min(choices.size(), (a_page + 1) * kPerPage); ++i) {
				labels.push_back(choices[i].text);
				actions.emplace_back([a_menu, set = choices[i].set]() {
					ChooseArmorSet(a_menu->cellId, a_menu->ref, set);
					Controller::Get().ReopenDisplayMenu(a_menu->cellId, a_menu->ref);
				});
			}
			if (a_page + 1 < pages) {
				labels.emplace_back("More...");
				actions.emplace_back([a_menu, a_page]() { ShowSetBox(a_menu, a_page + 1); });
			}
			labels.emplace_back("Back");
			actions.emplace_back([a_menu]() { Controller::Get().ReopenDisplayMenu(a_menu->cellId, a_menu->ref); });
			auto body = std::format("Dova Stash: what {} wears", a_menu->name);
			if (pages > 1) {
				body += std::format("\n(page {}/{})", a_page + 1, pages);
			}
			ShowMessageBox(body, labels, [actions = std::move(actions)](std::size_t a_index) {
				if (a_index < actions.size() && actions[a_index]) {
					actions[a_index]();
				}
			});
		}
	}

	std::string ArmorSetSummary(RE::FormID a_cellId, const core::FormRef& a_mannequin)
	{
		const auto* binding = FindBinding(a_cellId, a_mannequin);
		if (!binding || !binding->set) {
			return "Auto";
		}
		return game::SetName(*game::GetArmorIndex(), *binding->set);
	}

	void OpenArmorSets(RE::FormID a_cellId, const core::FormRef& a_mannequin)
	{
		auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		const auto* binding = home->layout->FindDisplay(a_mannequin);
		const auto  index = game::GetArmorIndex();
		const auto  have = game::HaveArmor(*index, *home, &a_mannequin);
		const auto  current = binding && binding->set ? game::SetOf(*index, *binding->set) : std::nullopt;

		struct Row
		{
			std::size_t       set;
			core::SetCoverage coverage;
		};
		std::vector<Row> rows;
		for (std::size_t s = 0; s < index->sets.size(); ++s) {
			const auto coverage = core::CoverageOf(index->sets[s], index->pieces, have);
			if (coverage.pieces > 0 || current == s) {
				rows.push_back(Row{ s, coverage });
			}
		}
		// Most complete first, then most pieces, then by name (the index order).
		std::ranges::stable_sort(rows, [](const Row& a_lhs, const Row& a_rhs) {
			if (a_lhs.coverage.Complete() != a_rhs.coverage.Complete()) {
				return a_lhs.coverage.Complete();
			}
			return a_lhs.coverage.have > a_rhs.coverage.have;
		});

		auto menu = std::make_shared<ArmorSetMenu>();
		menu->cellId = a_cellId;
		menu->ref = a_mannequin;
		menu->name = game::RefName(game::LookupRef(a_mannequin));
		const bool automatic = !binding || !binding->set;
		menu->choices.push_back({ std::format("{} Auto", automatic ? "[x]" : "[ ]"), std::nullopt });
		for (const auto& r : rows) {
			if (menu->choices.size() > kMaxChoices) {
				break;
			}
			const auto& set = index->sets[r.set];
			menu->choices.push_back({ std::format("{} {} ({}/{})", current == r.set ? "[x]" : "[ ]", set.name, r.coverage.have, r.coverage.total), set.id });
		}
		logger::debug("Armor sets for {}: {} of {} sets owned", game::Describe(game::LookupRef(a_mannequin)), rows.size(), index->sets.size());
		Controller::Get().UI().OpenArmorSetMenu(*menu);
	}

	void ChooseArmorSet(RE::FormID a_cellId, const core::FormRef& a_mannequin, std::optional<core::FormRef> a_set)
	{
		auto& registry = home::HomeRegistry::Get();
		auto* home = registry.CurrentIf(a_cellId);
		if (!home || !home->layout) {
			return;
		}
		if (home->layoutBroken) {
			Controller::Get().Notify("Dova Stash: this home's layout file is broken (see DovaStash.log)");
			return;
		}
		auto*      ref = game::LookupRef(a_mannequin);
		const auto name = game::RefName(ref);
		auto*      binding = home->layout->FindDisplay(a_mannequin);
		if (!binding) {
			core::DisplayBinding entry;
			entry.ref = a_mannequin;
			entry.label = name;
			home->layout->displays.push_back(std::move(entry));
			binding = &home->layout->displays.back();
		}
		const auto index = game::GetArmorIndex();
		const auto set = a_set ? game::SetOf(*index, *a_set) : std::nullopt;
		if (set) {
			binding->set = index->sets[*set].id;  // a set is written as its id (its first body piece)
		} else {
			binding->set = a_set;  // Auto, or a set that is not loaded (kept as it is)
		}
		binding->ignored = false;
		registry.Save(*home);

		if (!binding->set) {
			logger::info("Mannequin {} in {}: picks its own set", game::Describe(ref), home->name);
			Controller::Get().Notify(std::format("Dova Stash: {} picks its own set", name));
			return;
		}
		const auto setName = game::SetName(*index, *binding->set);
		logger::info("Mannequin {} in {}: wears {} ({})", game::Describe(ref), home->name, setName, binding->set->ToString());
		// Nothing is taken off a mannequin: armor of another set stays until the player takes it off.
		bool other = false;
		if (const auto found = ref ? game::DisplayAt(ref) : std::nullopt) {
			for (auto* h : game::DisplayHolds(*found)) {
				const auto form = game::ToFormRef(h);
				if (!form || game::SetOf(*index, *form) != set) {
					other = true;
				}
			}
		}
		Controller::Get().Notify(other ? std::format("Dova Stash: {} wears {} once you take its armor off", name, setName) :
		                                 std::format("Dova Stash: {} wears {}", name, setName));
	}

	void IStashUI::OpenArmorSetMenu(const ArmorSetMenu& a_menu)
	{
		ShowSetBox(std::make_shared<const ArmorSetMenu>(a_menu), 0);
	}
}
