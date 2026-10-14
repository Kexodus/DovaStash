#include "Game/ArmorIndex.h"

#include "Game/FormUtil.h"
#include "Game/ItemAdapter.h"
#include "Log.h"

namespace dova::game
{
	namespace
	{
		std::atomic<std::shared_ptr<const ArmorIndex>> g_index;

		constexpr std::string_view kTemperBench = "CraftingSmithingArmorTable";

		// Armor form id -> the first ingredient of its tempering recipe ("Skyrim.esm|0x0005AD9D").
		std::unordered_map<RE::FormID, std::string> TemperFamilies(RE::TESDataHandler& a_data)
		{
			std::unordered_map<RE::FormID, std::string> out;
			for (auto* cobj : a_data.GetFormArray<RE::BGSConstructibleObject>()) {
				if (!cobj || !cobj->createdItem || !cobj->benchKeyword) {
					continue;
				}
				const char* bench = cobj->benchKeyword->GetFormEditorID();
				auto*       armor = cobj->createdItem->As<RE::TESObjectARMO>();
				if (!armor || !bench || !core::IEquals(bench, kTemperBench) || out.contains(armor->GetFormID())) {
					continue;
				}
				RE::TESBoundObject* first = nullptr;
				cobj->requiredItems.ForEachContainerObject([&](RE::ContainerObject& a_entry) -> RE::BSContainer::ForEachResult {
					if (a_entry.obj) {
						first = a_entry.obj;
						return RE::BSContainer::ForEachResult::kStop;
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
				if (first) {
					if (auto ref = ToFormRef(first)) {
						out.emplace(armor->GetFormID(), ref->ToString());
					}
				}
			}
			return out;
		}

		std::shared_ptr<const ArmorIndex> Build()
		{
			const auto start = std::chrono::steady_clock::now();
			auto       index = std::make_shared<ArmorIndex>();
			auto*      data = RE::TESDataHandler::GetSingleton();
			if (!data) {
				return index;
			}
			const auto families = TemperFamilies(*data);
			for (auto* armor : data->GetFormArray<RE::TESObjectARMO>()) {
				if (!armor || armor->templateArmor) {
					continue;  // enchanted copies count as the armor they are made from (IndexedForm)
				}
				core::ItemInfo info;
				if (!FillBaseInfo(info, armor) || !info.playable || !info.form.IsValid() || info.name.empty() || !core::MannequinTakes(info)) {
					continue;
				}
				auto piece = core::ArmorPieceOf(info);
				if (const auto it = families.find(armor->GetFormID()); it != families.end()) {
					piece.family = it->second;
				}
				index->pieces.push_back(std::move(piece));
			}
			index->sets = core::BuildArmorSets(index->pieces);
			for (std::size_t s = 0; s < index->sets.size(); ++s) {
				for (const auto i : index->sets[s].pieces) {
					index->setOf.emplace(index->pieces[i].form, s);
				}
			}
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
			logger::debug("Armor sets: {} sets from {} armor pieces in the load order ({} ms)", index->sets.size(), index->pieces.size(), ms);
			return index;
		}

		RE::TESObjectARMO* ArmorForm(const core::FormRef& a_form)
		{
			auto* form = LookupForm(a_form);
			return form ? form->As<RE::TESObjectARMO>() : nullptr;
		}
	}

	std::shared_ptr<const ArmorIndex> GetArmorIndex()
	{
		if (auto index = g_index.load()) {
			return index;
		}
		auto index = Build();
		g_index.store(index);
		return index;
	}

	std::shared_ptr<const ArmorIndex> PeekArmorIndex()
	{
		return g_index.load();
	}

	std::optional<core::FormRef> IndexedForm(const ArmorIndex& a_index, const core::FormRef& a_form)
	{
		if (a_index.setOf.contains(a_form)) {
			return a_form;
		}
		auto* armor = ArmorForm(a_form);
		for (int depth = 0; armor && armor->templateArmor && depth < 4; ++depth) {
			armor = armor->templateArmor;
			if (auto ref = ToFormRef(armor); ref && a_index.setOf.contains(*ref)) {
				return ref;
			}
		}
		return std::nullopt;
	}

	std::optional<std::size_t> SetOf(const ArmorIndex& a_index, const core::FormRef& a_form)
	{
		const auto form = IndexedForm(a_index, a_form);
		if (!form) {
			return std::nullopt;
		}
		const auto it = a_index.setOf.find(*form);
		return it != a_index.setOf.end() ? std::optional(it->second) : std::nullopt;
	}

	std::set<core::FormRef> HaveArmor(const ArmorIndex& a_index, const home::Home& a_home, const core::FormRef* a_mannequin)
	{
		std::set<core::FormRef> out;
		StackTable              stacks;
		const auto              add = [&](RE::TESObjectREFR* a_ref) {
			if (!a_ref || a_ref->IsDisabled()) {
				return;
			}
			for (const auto& item : SnapshotInventory(a_ref, stacks)) {
				// Worn and favorited pieces never leave the player (keep rules), so a set made of them could not fill.
				if (item.type == core::ItemType::Armor && item.count > 0 && core::SortPlanner::CanMove(item) && !item.equipped && !item.favorited) {
					if (auto form = IndexedForm(a_index, item.form)) {
						out.insert(std::move(*form));
					}
				}
			}
		};
		add(Player());
		if (a_home.layout) {
			if (a_home.layout->master) {
				add(LookupRef(*a_home.layout->master));
			}
			for (const auto& c : a_home.layout->containers) {
				if (!c.ignored) {
					add(LookupRef(c.ref));
				}
			}
		}
		if (a_mannequin) {
			if (auto* ref = LookupRef(*a_mannequin)) {
				for (const auto& [object, data] : ref->GetInventory()) {
					if (object && data.first > 0 && object->Is(RE::FormType::Armor)) {
						if (auto piece = ToFormRef(object)) {
							if (auto form = IndexedForm(a_index, *piece)) {
								out.insert(std::move(*form));
							}
						}
					}
				}
			}
		}
		return out;
	}

	void ApplyChosenSet(core::DisplaySpot& a_spot, const core::DisplayBinding& a_binding, std::span<const core::DisplayStock> a_stocks,
		const std::vector<RE::TESBoundObject*>& a_holds)
	{
		if (!a_binding.set) {
			return;
		}
		a_spot.chosen = true;
		a_spot.worn = 0;
		for (auto* h : a_holds) {
			if (const auto* armor = h ? h->As<RE::TESObjectARMO>() : nullptr) {
				a_spot.worn |= armor->GetSlotMask().underlying();
			}
		}
		const auto index = GetArmorIndex();
		const auto set = SetOf(*index, *a_binding.set);
		if (!set) {
			if (log::FirstTime("armor-set:" + a_binding.set->ToString())) {
				logger::info("Mannequin {}: its set {} is not loaded (plugin missing?); it gets nothing until it is", a_binding.ref.ToString(),
					a_binding.set->ToString());
			}
			return;
		}
		for (const auto i : index->sets[*set].pieces) {
			a_spot.set.push_back(index->pieces[i].form);
		}
		// Enchanted copies of its pieces belong to it too.
		const auto add = [&](const core::FormRef& a_form) {
			if (std::ranges::find(a_spot.set, a_form) == a_spot.set.end() && SetOf(*index, a_form) == set) {
				a_spot.set.push_back(a_form);
			}
		};
		for (const auto& stock : a_stocks) {
			if (stock.item.type == core::ItemType::Armor && stock.item.form.IsValid()) {
				add(stock.item.form);
			}
		}
		for (auto* h : a_holds) {
			if (auto ref = ToFormRef(h)) {
				add(*ref);
			}
		}
	}

	bool DisplayFullFor(const FoundDisplay& a_display, const core::DisplayBinding* a_binding)
	{
		if (a_display.kind != core::DisplayKind::Mannequin || !a_binding || !a_binding->set) {
			return DisplayFull(a_display);
		}
		const auto index = GetArmorIndex();
		const auto set = SetOf(*index, *a_binding->set);
		if (!set) {
			return DisplayFull(a_display);
		}
		std::vector<core::ArmorPiece> pieces;
		for (const auto i : index->sets[*set].pieces) {
			pieces.push_back(index->pieces[i]);
		}
		std::vector<core::FormRef> holds;
		std::uint32_t              worn = 0;
		for (auto* h : DisplayHolds(a_display)) {
			if (const auto* armor = h ? h->As<RE::TESObjectARMO>() : nullptr) {
				worn |= armor->GetSlotMask().underlying();
			}
			if (auto ref = ToFormRef(h)) {
				auto form = IndexedForm(*index, *ref);
				holds.push_back(form ? *form : *ref);
			}
		}
		return !core::SetMannequinOpen(pieces, holds, worn);
	}

	std::string SetName(const ArmorIndex& a_index, const core::FormRef& a_set)
	{
		const auto set = SetOf(a_index, a_set);
		return set ? a_index.sets[*set].name : a_set.ToString();
	}
}
