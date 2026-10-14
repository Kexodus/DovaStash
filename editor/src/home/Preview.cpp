#include "home/Preview.h"

#include <algorithm>

namespace dova::editor::home
{
	namespace
	{
		bool BaseGame(const core::FormRef& a_form)
		{
			static constexpr const char* base[] = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" };
			return std::ranges::any_of(base, [&](const char* p) { return core::IEquals(a_form.plugin, p); });
		}

		// Up to `a_count` items matching the pattern, spread evenly over the catalog (deterministic), base game first.
		void Pick(const esp::ItemCatalog& a_catalog, const core::CategorySet& a_categories, const char* a_pattern, std::size_t a_count, std::int32_t a_stack,
			std::vector<core::ItemInfo>& a_out)
		{
			std::vector<std::size_t> base, other;
			for (std::size_t i = 0; i < a_catalog.items.size(); ++i) {
				const auto& item = a_catalog.items[i];
				const auto  cat = a_categories.Classify(item);
				if (!cat || !core::CategoryPatternMatches(a_pattern, *cat) || item.value <= 0) {
					continue;
				}
				(BaseGame(item.form) ? base : other).push_back(i);
			}
			auto& pool = base.size() >= a_count ? base : (base.empty() ? other : base);
			if (pool.empty()) {
				return;
			}
			const std::size_t n = std::min(a_count, pool.size());
			for (std::size_t k = 0; k < n; ++k) {
				const auto idx = pool[std::min(pool.size() - 1, (k * pool.size()) / n + pool.size() / (2 * n))];
				if (std::ranges::any_of(a_out, [&](const core::ItemInfo& i) { return i.stackId == idx; })) {
					continue;
				}
				auto item = a_catalog.items[idx];
				item.count = a_stack;
				item.stackId = static_cast<std::uint32_t>(idx);
				a_out.push_back(std::move(item));
			}
		}
	}

	const char* SampleName(SampleKind a_kind)
	{
		switch (a_kind) {
		case SampleKind::Haul:
			return "Typical haul";
		case SampleKind::Category:
			return "One category";
		case SampleKind::All:
			return "Every item in the load order";
		}
		return "?";
	}

	std::vector<core::ItemInfo> MakeSample(const esp::ItemCatalog& a_catalog, const core::CategorySet& a_categories, SampleKind a_kind, const std::string& a_pattern)
	{
		std::vector<core::ItemInfo> out;
		if (a_kind == SampleKind::Haul) {
			struct Part
			{
				const char*  pattern;
				std::size_t  items;
				std::int32_t stack;
			};
			// What a few dungeons leave in the player's bags.
			static constexpr Part parts[] = { { "weapons.*", 8, 1 }, { "armor.heavy", 4, 1 }, { "armor.light", 4, 1 }, { "armor.shields", 2, 1 },
				{ "armor.clothing", 4, 1 }, { "armor.jewelry", 4, 1 }, { "ammo.*", 2, 24 }, { "alchemy.potions", 8, 3 }, { "alchemy.poisons", 3, 2 },
				{ "alchemy.food", 8, 2 }, { "alchemy.ingredients", 20, 3 }, { "books.*", 6, 1 }, { "scrolls", 3, 1 }, { "soulgems.*", 4, 2 },
				{ "misc.gems", 5, 1 }, { "misc.ores_ingots", 6, 4 }, { "misc.hides_pelts", 4, 2 }, { "misc.animal_parts", 3, 2 }, { "misc.dwemer", 2, 1 },
				{ "misc.building_materials", 3, 5 }, { "misc.tools", 2, 1 }, { "misc.clutter", 6, 1 }, { "keys", 1, 1 } };
			for (const auto& p : parts) {
				if (a_categories.AnyMatches(p.pattern)) {
					Pick(a_catalog, a_categories, p.pattern, p.items, p.stack, out);
				}
			}
			return out;
		}
		for (std::size_t i = 0; i < a_catalog.items.size(); ++i) {
			const auto& item = a_catalog.items[i];
			if (a_kind == SampleKind::Category) {
				const auto cats = a_categories.MatchAll(item);
				if (std::ranges::none_of(cats, [&](const std::string& c) { return core::CategoryPatternMatches(a_pattern, c); })) {
					continue;
				}
			}
			auto copy = item;
			copy.count = 1;
			copy.stackId = static_cast<std::uint32_t>(i);
			out.push_back(std::move(copy));
		}
		return out;
	}

	std::vector<core::DisplaySpot> DisplaySpots(esp::LoadOrder& a_lo, std::span<const CellInfo> a_cells, const core::Layout& a_layout, const ArmorSetIndex* a_sets)
	{
		std::vector<core::DisplaySpot> out;
		for (const auto& d : a_layout.displays) {
			if (d.ignored) {
				continue;
			}
			const PlacedRef* ref = nullptr;
			for (const auto& cell : a_cells) {
				for (const auto& r : cell.refs) {
					if (r.display && a_lo.ToFormRef(r.key) == d.ref) {
						ref = &r;
					}
				}
			}
			const auto kind = ref ? ref->display : d.kind;
			if (!kind) {
				continue;
			}
			core::DisplaySpot s;
			s.ref = d.ref;
			s.kind = *kind;
			if (ref && ref->rackType) {
				s.rack = *ref->rackType;
			}
			s.items = d.items;
			if (ref && (s.kind == core::DisplayKind::Item || s.kind == core::DisplayKind::Case)) {
				s.items.clear();  // what its script is made for
				for (const auto& item : ref->displayItems) {
					s.items.push_back(a_lo.ToFormRef(item));
				}
			}
			s.categories = d.categories;
			s.only = d.only;
			if (s.kind == core::DisplayKind::Mannequin && d.set) {
				s.chosen = true;
				if (const auto set = a_sets ? a_sets->SetOf(*d.set) : std::nullopt) {
					for (const auto p : a_sets->sets[*set].pieces) {
						s.set.push_back(a_sets->pieces[p].form);
					}
				}
			}
			out.push_back(std::move(s));
		}
		return out;
	}

	Fill FillOf(const core::PreviewResult& a_result, const core::FormRef& a_ref, std::int32_t a_heavyAt)
	{
		const auto* c = a_result.Find(a_ref);
		if (!c) {
			return Fill::None;
		}
		if (c->master) {
			return Fill::Master;
		}
		if (c->total == 0) {
			return Fill::Empty;
		}
		return c->total >= a_heavyAt ? Fill::Heavy : Fill::Light;
	}
}
