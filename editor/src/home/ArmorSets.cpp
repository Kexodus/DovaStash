#include "home/ArmorSets.h"

#include <unordered_set>

#include "dova/core/Display.h"

namespace dova::editor::home
{
	using esp::FormKey;
	using esp::MakeSig;

	namespace
	{
		constexpr std::string_view kTemperBench = "CraftingSmithingArmorTable";

		// Armor -> the first ingredient of its tempering recipe, as "Plugin.esm|0x..." (game::TemperFamilies).
		esp::FormKeyMap<std::string> TemperFamilies(esp::LoadOrder& a_lo)
		{
			esp::FormKeyMap<std::string> out;
			a_lo.ForEachRecord(MakeSig("COBJ"), [&](const FormKey&, const esp::RecordRef& a_rec) {
				const auto subs = a_lo.Read(a_rec);
				const auto cnam = subs.Get(MakeSig("CNAM"));
				const auto bnam = subs.Get(MakeSig("BNAM"));
				const auto cnto = subs.Get(MakeSig("CNTO"));
				if (!cnam || cnam->size() < 4 || !bnam || bnam->size() < 4 || !cnto || cnto->size() < 4) {
					return;
				}
				const auto bench = a_lo.Resolve(a_rec.plugin, esp::Read<std::uint32_t>(*bnam, 0));
				if (!core::IEquals(a_lo.KeywordId(bench), kTemperBench)) {
					return;
				}
				const auto armor = a_lo.Resolve(a_rec.plugin, esp::Read<std::uint32_t>(*cnam, 0));
				if (out.contains(armor) || !a_lo.Find(armor, MakeSig("ARMO"))) {
					return;
				}
				const auto first = a_lo.Resolve(a_rec.plugin, esp::Read<std::uint32_t>(*cnto, 0));
				out.emplace(armor, a_lo.ToFormRef(first).ToString());
			});
			return out;
		}
	}

	std::optional<std::size_t> ArmorSetIndex::SetOf(const core::FormRef& a_piece) const
	{
		const auto it = setOf.find(a_piece);
		return it == setOf.end() ? std::nullopt : std::optional(it->second);
	}

	ArmorSetIndex BuildArmorSetIndex(esp::LoadOrder& a_lo, const esp::ItemCatalog& a_catalog)
	{
		ArmorSetIndex index;
		const auto    families = TemperFamilies(a_lo);
		std::unordered_set<std::size_t> seen;
		for (const auto& [key, i] : a_catalog.index) {
			if (i >= a_catalog.items.size() || !seen.insert(i).second) {
				continue;
			}
			const auto& info = a_catalog.items[i];
			if (info.type != core::ItemType::Armor || !info.playable || !info.form.IsValid() || info.name.empty() || !core::MannequinTakes(info)) {
				continue;
			}
			// Enchanted copies ("Ebony Armor of Health") count as the armor they are made from.
			const auto* rec = a_lo.Find(key, MakeSig("ARMO"));
			if (!rec || a_lo.Read(*rec).Get(MakeSig("TNAM"))) {
				continue;
			}
			auto piece = core::ArmorPieceOf(info);
			if (const auto f = families.find(key); f != families.end()) {
				piece.family = f->second;
			}
			index.pieces.push_back(std::move(piece));
		}
		// The game walks the forms in load order; FormRef order keeps the sets stable here.
		std::ranges::sort(index.pieces, [](const core::ArmorPiece& a, const core::ArmorPiece& b) { return a.form < b.form; });
		index.sets = core::BuildArmorSets(index.pieces);
		for (std::size_t s = 0; s < index.sets.size(); ++s) {
			for (const auto p : index.sets[s].pieces) {
				index.setOf.emplace(index.pieces[p].form, s);
			}
		}
		return index;
	}
}
