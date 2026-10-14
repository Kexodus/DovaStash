#include "UI/DisplayMenu.h"

#include "Controller.h"
#include "Home/Store.h"
#include "Icons/IconService.h"
#include "UI/ArmorSetMenu.h"
#include "UI/Labels.h"
#include "UI/MessageBox.h"

namespace dova::ui
{
	namespace
	{
		constexpr std::size_t kPerPage = 5;  // + More / Back = at most 7 buttons

		std::string_view Noun(core::DisplayKind a_kind)
		{
			switch (a_kind) {
			case core::DisplayKind::Rack:
				return "rack";
			case core::DisplayKind::Mannequin:
				return "mannequin";
			case core::DisplayKind::Case:
				return "display case";
			case core::DisplayKind::Bookshelf:
				return "bookshelf";
			default:
				return "display";
			}
		}

		// The category root a display's picker starts from: weapons, armor, books.
		std::string_view Root(core::DisplayKind a_kind)
		{
			switch (a_kind) {
			case core::DisplayKind::Mannequin:
				return "armor";
			case core::DisplayKind::Bookshelf:
				return "books";
			default:
				return "weapons";
			}
		}

		// "Bows, Crossbows", "any weapon".
		std::string Summary(const DisplayMenu& a_menu)
		{
			if (a_menu.categories.empty()) {
				switch (a_menu.kind) {
				case core::DisplayKind::Mannequin:
					return "any armor";
				case core::DisplayKind::Bookshelf:
					return "any book";
				default:
					return "any weapon";
				}
			}
			std::string out;
			for (std::size_t i = 0; i < a_menu.categories.size(); ++i) {
				if (i == 3) {
					out += std::format(" +{}", a_menu.categories.size() - i);
					break;
				}
				out += (i ? ", " : "") + PatternShortLabel(a_menu.categories[i]);
			}
			return out;
		}

		// The message-box picker: a page of choices, More..., Back (to the display menu).
		void ShowCategoryBox(RE::FormID a_cellId, core::FormRef a_display, core::DisplayFit a_fit, std::string a_name, std::size_t a_page)
		{
			const auto choices = DisplayCategoryChoices(a_fit);
			const auto patterns = DisplayCategories(a_cellId, a_display);
			const auto pages = std::max<std::size_t>(1, (choices.size() + kPerPage - 1) / kPerPage);
			a_page = std::min(a_page, pages - 1);

			std::vector<std::string>           labels;
			std::vector<std::function<void()>> actions;
			for (auto i = a_page * kPerPage; i < std::min(choices.size(), (a_page + 1) * kPerPage); ++i) {
				labels.push_back(DisplayCategoryEntry(patterns, choices[i]));
				actions.emplace_back([=, choice = choices[i]]() {
					Controller::Get().ToggleDisplayCategory(a_cellId, a_display, choice);
					ShowCategoryBox(a_cellId, a_display, a_fit, a_name, a_page);
				});
			}
			if (a_page + 1 < pages) {
				labels.emplace_back("More...");
				actions.emplace_back([=]() { ShowCategoryBox(a_cellId, a_display, a_fit, a_name, a_page + 1); });
			}
			labels.emplace_back("Back");
			actions.emplace_back([=]() { Controller::Get().ReopenDisplayMenu(a_cellId, a_display); });
			auto body = std::format("Dova Stash: what goes on {}\n(nothing chosen: anything that fits)", a_name);
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

	std::vector<std::string> DisplayCategoryChoices(const core::DisplayFit& a_fit)
	{
		return core::DisplayCategoryChoices(home::Store::Get().Categories(), a_fit);
	}

	std::vector<std::string> DisplayCategories(RE::FormID a_cellId, const core::FormRef& a_display)
	{
		auto*       home = home::HomeRegistry::Get().CurrentIf(a_cellId);
		const auto* binding = home && home->layout ? home->layout->FindDisplay(a_display) : nullptr;
		return binding ? binding->categories : std::vector<std::string>{};
	}

	std::string DisplayCategoryEntry(const std::vector<std::string>& a_patterns, const std::string& a_choice)
	{
		const bool exact = std::ranges::any_of(a_patterns, [&](const std::string& p) { return core::IEquals(p, a_choice); });
		// A choice is covered when a wider chosen pattern matches it ("weapons.*" covers "weapons.bows").
		const auto id = a_choice.ends_with(".*") ? a_choice.substr(0, a_choice.size() - 2) : a_choice;
		const bool covered = !exact && std::ranges::any_of(a_patterns, [&](const std::string& p) { return core::CategoryPatternMatches(p, id); });
		return std::format("{} {}", exact ? "[x]" : covered ? "[+]" : "[ ]", PatternLabel(a_choice));
	}

	std::vector<DisplayOption> DisplayMenuOptions(const DisplayMenu& a_menu, std::function<void()> a_openCategories)
	{
		const auto                 cellId = a_menu.cellId;
		const auto                 ref = a_menu.ref;
		std::vector<DisplayOption> out;
		if (!a_menu.used) {
			// Then its options right away (categories, armor set, fill now), not another press of the hotkey.
			out.push_back({ std::format("Use this {}", Noun(a_menu.kind)), [cellId, ref]() {
							   Controller::Get().SetDisplayUsed(cellId, ref, true);
							   Controller::Get().ReopenDisplayMenu(cellId, ref);
						   } });
			return out;
		}
		if (!DisplayCategoryChoices(a_menu.fit).empty()) {
			auto icon = a_menu.categories.empty() ? icons::ForCategory(std::string(Root(a_menu.kind))) : icons::ForPattern(a_menu.categories.front());
			out.push_back({ std::format("Categories... ({})", Summary(a_menu)), std::move(a_openCategories), std::move(icon) });
		}
		if (a_menu.kind == core::DisplayKind::Mannequin) {
			out.push_back({ std::format("Armor set... ({})", ArmorSetSummary(cellId, ref)), [cellId, ref]() { OpenArmorSets(cellId, ref); } });
		}
		if (!a_menu.full) {
			out.push_back({ "Fill now", [cellId, ref]() { Controller::Get().FillDisplay(cellId, ref); } });
		}
		out.push_back({ std::format("Stop using this {}", Noun(a_menu.kind)), [cellId, ref]() { Controller::Get().SetDisplayUsed(cellId, ref, false); } });
		return out;
	}

	void IStashUI::OpenDisplayMenu(const DisplayMenu& a_menu)
	{
		std::vector<std::string>           labels;
		std::vector<std::function<void()>> actions;
		const auto openCategories = [cellId = a_menu.cellId, ref = a_menu.ref, fit = a_menu.fit, name = a_menu.name]() {
			ShowCategoryBox(cellId, ref, fit, name, 0);
		};
		for (auto& o : DisplayMenuOptions(a_menu, openCategories)) {
			labels.push_back(std::move(o.text));
			actions.push_back(std::move(o.action));
		}
		labels.emplace_back("Cancel");
		actions.emplace_back(nullptr);
		const auto body = std::format("Dova Stash: {}\n{}", a_menu.name, a_menu.used ? "Used" : "Not used");
		ShowMessageBox(body, labels, [actions = std::move(actions)](std::size_t a_index) {
			if (a_index < actions.size() && actions[a_index]) {
				actions[a_index]();
			}
		});
	}
}
