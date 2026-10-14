#include "dova/core/CraftingStorage.h"

#include <algorithm>
#include <array>

namespace dova::core::crafting
{
	namespace
	{
		bool SameFile(std::string_view a_lhs, std::string_view a_rhs) noexcept
		{
			return a_lhs.size() == a_rhs.size() && std::ranges::equal(a_lhs, a_rhs, [](char a, char b) {
				const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
				return lower(a) == lower(b);
			});
		}

		// Where `a_container` stands in `a_order` (missing: after every listed one).
		std::size_t Rank(std::span<const FormRef> a_order, const FormRef& a_container)
		{
			const auto it = std::ranges::find(a_order, a_container);
			return it != a_order.end() ? static_cast<std::size_t>(it - a_order.begin()) : a_order.size();
		}

		// Names checked against the mods' own files: CraftingPullFromContainer.dll (the project name in
		// JerryYOJ/CraftingPullFromContainer-SKSE), CraftingInventoryExtender.dll (ohfor/scie's release
		// workflow); the others from their Nexus pages (docs/research/SIMILAR-MODS.md).
		constexpr std::array kKnown{
			KnownMod{ "CraftingPullFromContainer.dll", "Crafting Pulls Resources From Containers" },
			KnownMod{ "CraftingInventoryExtender.dll", "Skyrim Crafting Inventory Extender" },
			KnownMod{ "SmartCraftingStorage.dll", "Smart Crafting Storage" },
			KnownMod{ "LinkedCraftingStorage.esp", "Linked Crafting Storage" },
			KnownMod{ "Workbench Containers.esp", "Workbench Containers" },
		};
	}

	std::string_view SupplyModeName(SupplyMode a_mode) noexcept
	{
		switch (a_mode) {
		case SupplyMode::Off:
			return "off";
		case SupplyMode::Move:
			return "move";
		case SupplyMode::Storage:
			return "craft from storage";
		default:
			return "?";
		}
	}

	SupplySetting ToSupplySetting(std::int32_t a_value) noexcept
	{
		switch (a_value) {
		case 1:
			return SupplySetting::Off;
		case 2:
			return SupplySetting::Move;
		case 3:
			return SupplySetting::Storage;
		default:
			return SupplySetting::Auto;
		}
	}

	ModeResolution ResolveMode(SupplySetting a_setting, bool a_hooksReady, std::span<const std::string> a_otherMods)
	{
		ModeResolution r;
		switch (a_setting) {
		case SupplySetting::Off:
			r.mode = SupplyMode::Off;
			r.chosen = true;
			break;
		case SupplySetting::Move:
			r.mode = SupplyMode::Move;
			r.chosen = true;
			break;
		case SupplySetting::Storage:
			r.chosen = true;
			if (a_hooksReady) {
				r.mode = SupplyMode::Storage;
			} else {
				r.mode = SupplyMode::Move;
				r.why = "crafting from storage is not available on this game version, so the materials are brought instead";
			}
			break;
		case SupplySetting::Auto:
		default:
			if (!a_otherMods.empty()) {
				std::string names;
				for (const auto& m : a_otherMods) {
					names += (names.empty() ? "" : ", ") + m;
				}
				r.mode = SupplyMode::Off;
				r.why = names + (a_otherMods.size() == 1 ? " is loaded and already does this" : " are loaded and already do this");
			} else if (a_hooksReady) {
				r.mode = SupplyMode::Storage;
			} else {
				r.mode = SupplyMode::Move;
				r.why = "crafting from storage is not available on this game version, so the materials are brought instead";
			}
			break;
		}
		return r;
	}

	SupplyMode ModeFor(SupplyMode a_mode, Station a_station) noexcept
	{
		if (a_mode == SupplyMode::Storage && a_station == Station::Enchanting) {
			return SupplyMode::Move;
		}
		return a_mode;
	}

	std::span<const KnownMod> KnownCraftingStorageMods() noexcept
	{
		return kKnown;
	}

	std::vector<std::string> DetectOtherMods(std::span<const std::string> a_loadedFiles)
	{
		std::vector<std::string> out;
		for (const auto& known : kKnown) {
			if (std::ranges::any_of(a_loadedFiles, [&](const std::string& f) { return SameFile(f, known.file); })) {
				out.emplace_back(known.name);
			}
		}
		return out;
	}

	Supply MakeSupply(const SupplyRequest& a_request, std::span<const Stored> a_stored, const BoundContainer& a_bound)
	{
		Supply                         out;
		std::map<FormRef, ItemInfo>    sample;  // one stack per form: what the bound container is asked about
		for (const auto& s : a_stored) {
			for (const auto& st : s.stacks) {
				if (Judge(a_request, st) != Verdict::Bring) {
					continue;
				}
				auto& f = out[st.item.form];
				f.count += st.item.count;
				if (std::ranges::find(f.containers, s.container) == f.containers.end()) {
					f.containers.push_back(s.container);
				}
				sample.try_emplace(st.item.form, st.item);
			}
		}
		if (a_bound) {
			for (auto& [form, f] : out) {
				const auto bound = a_bound(sample.at(form));
				if (!bound) {
					continue;
				}
				if (const auto it = std::ranges::find(f.containers, *bound); it != f.containers.end()) {
					std::rotate(f.containers.begin(), it, it + 1);  // to the front, the others keep their order
				}
			}
		}
		return out;
	}

	std::vector<Pull> PlanTake(const SupplyRequest& a_request, std::span<const Stored> a_stored, const FormRef& a_form,
		std::span<const FormRef> a_order, std::int32_t a_need)
	{
		std::vector<Pull> out;
		if (a_need <= 0) {
			return out;
		}
		std::vector<const Stored*> containers;
		for (const auto& s : a_stored) {
			containers.push_back(&s);
		}
		std::ranges::stable_sort(containers, {}, [&](const Stored* s) { return Rank(a_order, s->container); });

		for (const auto* s : containers) {
			// Plain copies first, then keyed ones, each group in inventory order.
			std::vector<const Stack*> stacks;
			for (const auto& st : s->stacks) {
				if (st.item.form == a_form && Judge(a_request, st) == Verdict::Bring) {
					stacks.push_back(&st);
				}
			}
			std::ranges::stable_partition(stacks, [](const Stack* st) { return st->key->empty(); });
			for (const auto* st : stacks) {
				const auto n = std::min(a_need, st->item.count);
				if (n <= 0) {
					continue;
				}
				out.push_back(Pull{ Move{ st->item.stackId, a_form, n, ContainerRef(s->container), ContainerRef::Player() }, *st->key });
				a_need -= n;
				if (a_need == 0) {
					return out;
				}
			}
		}
		return out;
	}
}
