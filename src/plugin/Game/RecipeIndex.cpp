#include "Game/RecipeIndex.h"

#include "Game/FormUtil.h"

namespace dova::game
{
	namespace
	{
		// One component: an item, or a form list of items (expanded). Leveled lists and anything else are
		// not materials. Returns the number of forms added.
		std::size_t AddComponent(std::vector<core::FormRef>& a_out, RE::TESForm* a_form)
		{
			if (!a_form) {
				return 0;
			}
			if (const auto* list = a_form->As<RE::BGSListForm>()) {
				std::size_t n = 0;
				list->ForEachForm([&](RE::TESForm* a_entry) {
					if (a_entry && !a_entry->As<RE::BGSListForm>()) {  // no nested lists: no cycles
						n += AddComponent(a_out, a_entry);
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
				return n;
			}
			if (a_form->Is(RE::FormType::LeveledItem) || !a_form->IsBoundObject()) {
				return 0;
			}
			if (auto ref = ToFormRef(a_form)) {
				a_out.push_back(std::move(*ref));
				return 1;
			}
			return 0;
		}

		core::crafting::RecipeIndex Build()
		{
			core::crafting::RecipeIndex index;
			auto*                       data = RE::TESDataHandler::GetSingleton();
			if (!data) {
				logger::warn("Recipes: no TESDataHandler, crafting stations get nothing");
				return index;
			}
			const auto started = std::chrono::steady_clock::now();
			for (auto* cobj : data->GetFormArray<RE::BGSConstructibleObject>()) {
				if (!cobj) {
					continue;
				}
				core::crafting::Recipe recipe;
				recipe.bench = KeywordName(cobj->benchKeyword);
				cobj->requiredItems.ForEachContainerObject([&](RE::ContainerObject& a_entry) {
					// The array is typed TESBoundObject*, but go through TESForm so a form list is recognised.
					AddComponent(recipe.components, static_cast<RE::TESForm*>(a_entry.obj));
					return RE::BSContainer::ForEachResult::kContinue;
				});
				if (cobj->createdItem) {
					recipe.created = ToFormRef(cobj->createdItem);
				}
				index.Add(recipe);
			}
			const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
			logger::debug("Recipes: {} recipes at {} bench keywords use {} materials ({} without a bench keyword; {:.1f} ms)",
				index.Recipes(), index.Benches(), index.Materials(), index.WithoutBench(), ms);
			return index;
		}
	}

	std::string KeywordName(const RE::BGSKeyword* a_keyword)
	{
		if (!a_keyword) {
			return {};
		}
		if (const char* edid = a_keyword->GetFormEditorID(); edid && *edid) {
			return edid;
		}
		const auto ref = ToFormRef(a_keyword);
		return ref ? ref->ToString() : std::format("{:08X}", a_keyword->GetFormID());
	}

	std::vector<std::string> KeywordNames(const RE::BGSKeywordForm* a_form)
	{
		std::vector<std::string> out;
		if (!a_form) {
			return out;
		}
		for (const auto* kw : a_form->GetKeywords()) {
			if (auto name = KeywordName(kw); !name.empty()) {
				out.push_back(std::move(name));
			}
		}
		return out;
	}

	const core::crafting::RecipeIndex& Recipes()
	{
		static const core::crafting::RecipeIndex index = Build();
		return index;
	}
}
