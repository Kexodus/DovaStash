#include "dova/core/Crafting.h"

#include <algorithm>

namespace dova::core::crafting
{
	namespace
	{
		bool IsGear(ItemType a_type) noexcept
		{
			return a_type == ItemType::Weapon || a_type == ItemType::Armor || a_type == ItemType::Ammo;
		}

		// Could this stack move at all (independent of the station)? nullopt = yes.
		std::optional<Verdict> Immovable(const Stack& a_stack)
		{
			const auto& item = a_stack.item;
			if (!SortPlanner::CanMove(item)) {
				return Verdict::Unmovable;
			}
			if (item.equipped || item.favorited) {
				return Verdict::Protected;
			}
			if (item.customized || !a_stack.key) {
				return Verdict::Customized;
			}
			if (!item.form.IsValid()) {
				return Verdict::Dynamic;
			}
			return std::nullopt;
		}

		std::int32_t CountOf(const Counts& a_counts, const StackKey& a_key)
		{
			const auto it = a_counts.find(a_key);
			return it != a_counts.end() ? it->second : 0;
		}

		// Copies with this key in one container, whatever their flags (what left it).
		std::int32_t InContainer(std::span<const Stored> a_stored, const FormRef& a_container, const StackKey& a_key)
		{
			std::int32_t n = 0;
			for (const auto& s : a_stored) {
				if (!(s.container == a_container)) {
					continue;
				}
				for (const auto& st : s.stacks) {
					if (st.key && *st.key == a_key.key && st.item.form == a_key.form && st.item.count > 0) {
						n += st.item.count;
					}
				}
			}
			return n;
		}
	}

	// --- RecipeIndex -----------------------------------------------------------------------------------

	void RecipeIndex::Add(const Recipe& a_recipe)
	{
		if (a_recipe.bench.empty()) {
			++_withoutBench;
			return;
		}
		auto& bench = _byBench[ToLower(a_recipe.bench)];
		++bench.recipes;
		++_recipes;
		for (const auto& c : a_recipe.components) {
			if (c.IsValid()) {
				bench.materials.insert(c);
			}
		}
	}

	std::unordered_set<FormRef> RecipeIndex::MaterialsFor(std::span<const std::string> a_benchKeywords) const
	{
		std::unordered_set<FormRef> out;
		for (const auto& k : a_benchKeywords) {
			if (const auto it = _byBench.find(ToLower(k)); it != _byBench.end()) {
				out.insert(it->second.materials.begin(), it->second.materials.end());
			}
		}
		return out;
	}

	std::size_t RecipeIndex::RecipesAt(std::string_view a_benchKeyword) const
	{
		const auto it = _byBench.find(ToLower(a_benchKeyword));
		return it != _byBench.end() ? it->second.recipes : 0;
	}

	std::size_t RecipeIndex::Materials() const
	{
		std::unordered_set<FormRef> all;
		for (const auto& [_, bench] : _byBench) {
			all.insert(bench.materials.begin(), bench.materials.end());
		}
		return all.size();
	}

	// --- pulling ---------------------------------------------------------------------------------------

	std::string_view VerdictName(Verdict a_verdict) noexcept
	{
		switch (a_verdict) {
		case Verdict::Bring:
			return "bring";
		case Verdict::NotMaterial:
			return "not a material here";
		case Verdict::Gear:
			return "weapon, armor or ammo";
		case Verdict::Unmovable:
			return "never moved";
		case Verdict::Protected:
			return "worn or favorited";
		case Verdict::Customized:
			return "customized";
		case Verdict::Dynamic:
			return "no load-order identity";
		default:
			return "?";
		}
	}

	Verdict Judge(const SupplyRequest& a_request, const Stack& a_stack)
	{
		if (const auto why = Immovable(a_stack)) {
			return *why;
		}
		const auto& item = a_stack.item;
		if (a_request.station == Station::Alchemy && item.type == ItemType::Ingredient) {
			return Verdict::Bring;
		}
		if (a_request.station == Station::Enchanting && item.type == ItemType::SoulGem && item.soulGemState == SoulGemState::Filled) {
			return Verdict::Bring;
		}
		if (!a_request.materials.contains(item.form)) {
			return Verdict::NotMaterial;
		}
		if (IsGear(item.type) && !a_request.gear) {
			return Verdict::Gear;
		}
		return Verdict::Bring;
	}

	std::vector<Pull> PlanPull(const SupplyRequest& a_request, std::span<const Stored> a_stored)
	{
		std::vector<Pull> out;
		if (a_request.Empty()) {
			return out;
		}
		for (const auto& s : a_stored) {
			for (const auto& st : s.stacks) {
				if (Judge(a_request, st) != Verdict::Bring) {
					continue;
				}
				out.push_back(Pull{ Move{ st.item.stackId, st.item.form, st.item.count, ContainerRef(s.container), ContainerRef::Player() }, *st.key });
			}
		}
		return out;
	}

	// --- ledger ----------------------------------------------------------------------------------------

	Counts CountStacks(std::span<const Stack> a_stacks)
	{
		Counts out;
		for (const auto& st : a_stacks) {
			if (Immovable(st)) {
				continue;
			}
			out[StackKey{ st.item.form, *st.key }] += st.item.count;
		}
		return out;
	}

	std::int32_t Ledger::Total() const noexcept
	{
		std::int32_t n = 0;
		for (const auto& p : pulled) {
			n += p.count;
		}
		return n;
	}

	std::vector<StackKey> Ledger::Keys() const
	{
		std::vector<StackKey> out;
		for (const auto& p : pulled) {
			StackKey k{ p.form, p.key };
			if (std::ranges::find(out, k) == out.end()) {
				out.push_back(std::move(k));
			}
		}
		return out;
	}

	Ledger MakeLedger(std::span<const Pull> a_planned, std::span<const Stored> a_storedBefore, std::span<const Stored> a_storedAfter,
		const Counts& a_playerBefore)
	{
		// Planned copies per (container, form, key), in plan order.
		struct Planned
		{
			FormRef      container;
			StackKey     stack;
			std::int32_t count{ 0 };
		};
		std::vector<Planned> planned;
		for (const auto& p : a_planned) {
			if (p.move.from.IsPlayer() || p.move.count <= 0) {
				continue;
			}
			const auto& container = p.move.from.Ref();
			StackKey    key{ p.move.form, p.key };
			auto        it = std::ranges::find_if(planned, [&](const Planned& x) { return x.container == container && x.stack == key; });
			if (it == planned.end()) {
				planned.push_back(Planned{ container, std::move(key), p.move.count });
			} else {
				it->count += p.move.count;
			}
		}

		Ledger ledger;
		for (const auto& p : planned) {
			const auto left = InContainer(a_storedBefore, p.container, p.stack) - InContainer(a_storedAfter, p.container, p.stack);
			const auto moved = std::clamp(left, 0, p.count);
			if (moved <= 0) {
				continue;
			}
			ledger.pulled.push_back(Pulled{ p.stack.form, p.stack.key, p.container, moved });
			ledger.before.try_emplace(p.stack, CountOf(a_playerBefore, p.stack));
		}
		return ledger;
	}

	// --- returning -------------------------------------------------------------------------------------

	ReturnPlan PlanReturn(const Ledger& a_ledger, const Counts& a_now, const std::map<FormRef, std::int32_t>& a_crafted)
	{
		ReturnPlan plan;

		// Pulled per (form, key); the map orders each form's keys with the plain stack ("") first.
		std::map<StackKey, std::int32_t> pulled;
		for (const auto& p : a_ledger.pulled) {
			pulled[StackKey{ p.form, p.key }] += p.count;
			plan.pulled += p.count;
			// Crafted copies land in the plain stack: every pulled form gets a plain line (pulled 0 when only
			// keyed copies came), so they are counted there before spilling over to the keyed copies.
			pulled.try_emplace(StackKey{ p.form, std::string{} }, 0);
		}

		std::map<FormRef, std::int32_t> craftedLeft;
		for (const auto& [form, n] : a_crafted) {
			if (n > 0) {
				craftedLeft[form] = n;
			}
		}

		std::map<StackKey, std::int32_t> backPer;
		for (const auto& [key, n] : pulled) {
			ReturnLine line{ key };
			line.before = CountOf(a_ledger.before, key);
			line.pulled = n;
			line.now = std::max(0, CountOf(a_now, key));

			std::int32_t available = line.now;
			if (auto it = craftedLeft.find(key.form); it != craftedLeft.end()) {
				line.crafted = std::min(it->second, available);
				it->second -= line.crafted;
				available -= line.crafted;
			}
			line.back = std::clamp(available - line.before, 0, n);
			backPer[key] = line.back;
			plan.returned += line.back;
			if (n > 0 || line.crafted > 0) {
				plan.lines.push_back(line);
			}
		}

		// Spread each (form, key) over the containers it came from, in pull order.
		for (const auto& p : a_ledger.pulled) {
			auto& left = backPer[StackKey{ p.form, p.key }];
			const auto n = std::min(left, p.count);
			if (n <= 0) {
				continue;
			}
			left -= n;
			plan.back.push_back(Back{ p.form, p.key, p.from, n });
		}
		return plan;
	}

	std::vector<Move> ReturnMoves(std::span<const Back> a_back, std::span<const Stack> a_playerStacks)
	{
		std::vector<Move>         out;
		std::vector<std::int32_t> used(a_playerStacks.size(), 0);
		for (const auto& b : a_back) {
			auto need = b.count;
			for (std::size_t i = 0; i < a_playerStacks.size() && need > 0; ++i) {
				const auto& st = a_playerStacks[i];
				if (Immovable(st) || !(st.item.form == b.form) || *st.key != b.key) {
					continue;
				}
				const auto n = std::min(need, st.item.count - used[i]);
				if (n <= 0) {
					continue;
				}
				used[i] += n;
				need -= n;
				out.push_back(Move{ st.item.stackId, b.form, n, ContainerRef::Player(), ContainerRef(b.to) });
			}
		}
		return out;
	}
}
