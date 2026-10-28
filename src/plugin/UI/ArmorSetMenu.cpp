#include "UI/ArmorSetMenu.h"

#include "Controller.h"
#include "Game/ArmorIndex.h"
#include "Game/Displays.h"
#include "Game/FormUtil.h"
#include "UI/MenuFramework.h"
#include "UI/MessageBox.h"

namespace dova::ui
{
	namespace
	{
		constexpr std::size_t kMaxChoices = 100;  // UIListMenu takes at most 128 entries
		constexpr std::size_t kMaxBoxChoices = 15;  // message boxes: 3 pages at most

		// Many mannequins have no name (Whitepeak Tower's, home mods' copies): menus and labels call one "Mannequin",
		// as the display menu does (Controller's DisplayName).
		std::string MannequinName(RE::TESObjectREFR* a_ref)
		{
			auto name = game::RefName(a_ref);
			return name.empty() ? std::string("Mannequin") : name;
		}

		const core::DisplayBinding* FindBinding(RE::FormID a_cellId, const core::FormRef& a_mannequin)
		{
			auto* home = home::HomeRegistry::Get().CurrentIf(a_cellId);
			return home && home->layout ? home->layout->FindDisplay(a_mannequin) : nullptr;
		}

		// The message-box picker, paged like every message-box menu (ShowOptions); Back on the first page goes to
		// the display menu. At most 15 entries (3 pages): Auto, the current set, then the complete sets, then the most
		// complete others (the order of `choices`); the panel's Displays page lists every set.
		void ShowSetBox(const ArmorSetMenu& a_menu)
		{
			std::vector<const ArmorSetChoice*> picked;
			for (const auto& c : a_menu.choices) {
				if (!c.set || c.current) {
					picked.push_back(&c);  // Auto (first), and the current set right after it
				}
			}
			std::ranges::stable_sort(picked, [](const auto* a_lhs, const auto* a_rhs) { return !a_lhs->set && a_rhs->set; });
			std::size_t notShown = a_menu.notShown;
			for (const auto& c : a_menu.choices) {
				if (c.set && !c.current) {
					if (picked.size() < kMaxBoxChoices) {
						picked.push_back(&c);
					} else {
						++notShown;
					}
				}
			}
			std::vector<MenuOption> options;
			for (const auto* c : picked) {
				options.push_back({ c->text, [cellId = a_menu.cellId, ref = a_menu.ref, set = c->set]() {
									   ChooseArmorSet(cellId, ref, set);
									   Controller::Get().ReopenDisplayMenu(cellId, ref);
								   } });
			}
			std::string more;
			if (notShown > 0) {
				more = menu_framework::IsLoaded() ? "\nMore sets: Dova Stash > Displays"s : std::format("\n({} more sets not shown)", notShown);
			}
			ShowOptions(std::format("Dova Stash: {}: Armor set{}", a_menu.name, more), std::move(options),
				[cellId = a_menu.cellId, ref = a_menu.ref]() { Controller::Get().ReopenDisplayMenu(cellId, ref); });
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
		menu->name = MannequinName(game::LookupRef(a_mannequin));
		const bool automatic = !binding || !binding->set;
		menu->choices.push_back({ std::format("{} Auto", automatic ? "[x]" : "[ ]"), std::nullopt, false, automatic });
		for (const auto& r : rows) {
			if (menu->choices.size() > kMaxChoices) {
				++menu->notShown;
				continue;
			}
			const auto& set = index->sets[r.set];
			menu->choices.push_back({ std::format("{} {} ({}/{})", current == r.set ? "[x]" : "[ ]", set.name, r.coverage.have, r.coverage.total), set.id,
				r.coverage.Complete(), current == r.set });
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
			Controller::Get().Notify("Dova Stash: this home's setup file is broken (see DovaStash.log)", true);
			return;
		}
		auto*      ref = game::LookupRef(a_mannequin);
		const auto name = MannequinName(ref);
		auto*      binding = home->layout->FindDisplay(a_mannequin);
		if (binding && binding->ignored) {
			// Turned off by the player (Stop using): a set does not turn it back on.
			logger::info("Mannequin {} in {}: set not chosen (not used)", game::Describe(ref), home->name);
			Controller::Get().Notify(std::format("Dova Stash: {} isn't used", name), true);
			return;
		}
		if (!binding) {
			// Not in the layout yet: used first, with every check Use makes (a mannequin script, alive, built).
			Controller::Get().SetDisplayUsed(a_cellId, a_mannequin, true);
			home = registry.CurrentIf(a_cellId);
			binding = home && home->layout ? home->layout->FindDisplay(a_mannequin) : nullptr;
			if (!binding || binding->ignored) {
				return;  // refused: SetDisplayUsed said why
			}
		}
		const auto index = game::GetArmorIndex();
		const auto set = a_set ? game::SetOf(*index, *a_set) : std::nullopt;
		if (set) {
			binding->set = index->sets[*set].id;  // a set is written as its id (its first body piece)
		} else {
			binding->set = a_set;  // Auto, or a set that is not loaded (kept as it is)
		}
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
		ShowSetBox(a_menu);
	}
}
