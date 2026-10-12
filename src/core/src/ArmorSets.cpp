#include "dova/core/ArmorSets.h"

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <map>

namespace dova::core
{
	namespace
	{
		// Words that name the piece rather than the set ("Ebony Cuirass" -> "Ebony").
		constexpr std::string_view kPieceWords[]{ "armor", "armour", "cuirass", "breastplate", "chestplate", "mail",
			"helmet", "helm", "hood", "cowl", "mask", "hat", "cap", "crown", "circlet", "diadem", "sallet", "headgear", "headdress", "veil",
			"boots", "shoes", "sandals", "sabatons", "greaves", "leggings", "gauntlets", "gloves", "bracers", "vambraces", "wraps", "handwraps",
			"hand", "robes", "robe", "clothes", "clothing", "outfit", "garb", "tunic", "cloak", "cape", "mantle", "pauldrons", "shirt", "pants",
			"trousers", "coat", "jerkin" };

		bool IsPieceWord(std::string_view a_lower)
		{
			return std::ranges::find(kPieceWords, a_lower) != std::end(kPieceWords);
		}

		struct Word
		{
			std::string text;   // as written
			std::string lower;  // for comparing
		};

		// Words of a name, without bracketed parts; '-', '_', ',', ':', '/' and '|' separate words.
		std::vector<Word> Words(std::string_view a_name)
		{
			std::string clean;
			int         depth = 0;
			for (const char c : a_name) {
				if (c == '(' || c == '[' || c == '{' || c == '<') {
					++depth;
				} else if (c == ')' || c == ']' || c == '}' || c == '>') {
					depth = std::max(0, depth - 1);
					clean += ' ';
				} else if (depth == 0) {
					clean += (c == '-' || c == '_' || c == ',' || c == ':' || c == '/' || c == '|' || c == '\t') ? ' ' : c;
				}
			}
			std::vector<Word> out;
			std::size_t       i = 0;
			while (i < clean.size()) {
				while (i < clean.size() && clean[i] == ' ') {
					++i;
				}
				const auto start = i;
				while (i < clean.size() && clean[i] != ' ') {
					++i;
				}
				if (i > start) {
					std::string text = clean.substr(start, i - start);
					out.push_back(Word{ text, ToLower(text) });
				}
			}
			return out;
		}

		std::vector<Word> StemWords(std::string_view a_name)
		{
			const auto words = Words(a_name);
			const auto withoutPieces = [](auto a_begin, auto a_end) {
				std::vector<Word> out;
				for (auto it = a_begin; it != a_end; ++it) {
					if (!IsPieceWord(it->lower)) {
						out.push_back(*it);
					}
				}
				return out;
			};
			// "Armor of the Old Gods" -> "Old Gods"; "Ebony Armor of Major Health" -> "Ebony".
			const auto of = std::ranges::find_if(words, [](const Word& w) { return w.lower == "of"; });
			if (of != words.begin() && of != words.end() && of + 1 != words.end()) {
				const bool allPieces = std::all_of(words.begin(), of, [](const Word& w) { return IsPieceWord(w.lower); });
				if (allPieces) {
					auto after = of + 1;
					if (after != words.end() && after->lower == "the" && after + 1 != words.end()) {
						++after;
					}
					return withoutPieces(after, words.end());
				}
				if (IsPieceWord((of - 1)->lower)) {
					return withoutPieces(words.begin(), of);
				}
			}
			return withoutPieces(words.begin(), words.end());
		}

		std::vector<std::string> LowerStem(std::string_view a_name)
		{
			std::vector<std::string> out;
			for (auto& w : StemWords(a_name)) {
				out.push_back(std::move(w.lower));
			}
			return out;
		}

		bool StartsWith(const std::vector<std::string>& a_words, const std::vector<std::string>& a_prefix)
		{
			return a_prefix.size() <= a_words.size() && std::equal(a_prefix.begin(), a_prefix.end(), a_words.begin());
		}

		// Bit per slot group: head 1, body 2, hands 4, feet 8.
		std::uint32_t GroupBits(std::uint32_t a_slots)
		{
			return ((a_slots & kSetSlotHead) ? 1u : 0u) | ((a_slots & kSetSlotBody) ? 2u : 0u) | ((a_slots & kSetSlotHands) ? 4u : 0u) |
			       ((a_slots & kSetSlotFeet) ? 8u : 0u);
		}

		std::size_t DistinctForms(std::span<const ArmorPiece> a_pieces, const std::vector<std::size_t>& a_members)
		{
			std::set<FormRef> forms;
			for (const auto i : a_members) {
				forms.insert(a_pieces[i].form);
			}
			return forms.size();
		}

		std::string_view TypeName(ArmorType a_type)
		{
			switch (a_type) {
			case ArmorType::Light:
				return "Light";
			case ArmorType::Heavy:
				return "Heavy";
			default:
				return "Clothing";
			}
		}

		std::string Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t");
			if (first == std::string_view::npos) {
				return {};
			}
			const auto last = a_text.find_last_not_of(" \t");
			return std::string(a_text.substr(first, last - first + 1));
		}
	}

	int SlotGroupCount(std::uint32_t a_slots) noexcept
	{
		return ((a_slots & kSetSlotHead) ? 1 : 0) + ((a_slots & kSetSlotBody) ? 1 : 0) + ((a_slots & kSetSlotHands) ? 1 : 0) + ((a_slots & kSetSlotFeet) ? 1 : 0);
	}

	ArmorPiece ArmorPieceOf(const ItemInfo& a_item)
	{
		ArmorPiece p;
		p.form = a_item.form;
		p.name = a_item.name;
		p.keywords = a_item.keywords;
		p.armorType = a_item.armorType;
		p.slots = a_item.armorSlots;
		p.value = a_item.value;
		return p;
	}

	std::string ArmorNameStem(std::string_view a_name)
	{
		std::string out;
		for (const auto& w : StemWords(a_name)) {
			out += (out.empty() ? "" : " ") + w.text;
		}
		return out;
	}

	std::string ArmorMaterialKeyword(const ArmorPiece& a_piece)
	{
		for (const auto& k : a_piece.keywords) {
			if (ToLower(k).find("armormaterial") != std::string::npos) {
				return k;
			}
		}
		return {};
	}

	std::vector<std::vector<std::size_t>> GroupArmorPieces(std::span<const ArmorPiece> a_pieces)
	{
		std::vector<std::vector<std::string>> stems(a_pieces.size());
		std::vector<std::string>              keys;  // plugin + material, lower case, in order of appearance
		std::map<std::string, std::vector<std::size_t>> buckets;
		for (std::size_t i = 0; i < a_pieces.size(); ++i) {
			stems[i] = LowerStem(a_pieces[i].name);
			auto key = ToLower(a_pieces[i].form.plugin) + '\n' + ToLower(ArmorMaterialKeyword(a_pieces[i]));
			auto& members = buckets[key];
			if (members.empty()) {
				keys.push_back(key);
			}
			members.push_back(i);
		}

		std::vector<std::vector<std::size_t>> out;
		for (const auto& key : keys) {
			const auto& members = buckets[key];
			const bool  material = !key.ends_with('\n');

			struct Cluster
			{
				std::vector<std::string> root;
				std::vector<std::size_t> members;
			};
			std::vector<Cluster>     clusters;
			std::vector<std::size_t> unstemmed;
			auto                     byLength = members;
			std::ranges::stable_sort(byLength, {}, [&](std::size_t i) { return stems[i].size(); });
			for (const auto i : byLength) {
				if (stems[i].empty()) {
					unstemmed.push_back(i);
					continue;
				}
				Cluster*    best = nullptr;
				std::size_t bestLength = 0;
				for (auto& c : clusters) {
					if (StartsWith(stems[i], c.root) && c.root.size() > bestLength) {
						best = &c;
						bestLength = c.root.size();
					}
				}
				if (best) {
					best->members.push_back(i);
				} else {
					clusters.push_back(Cluster{ stems[i], { i } });
				}
			}

			if (material) {
				// The keyword already ties them: stems sharing their first word are one set.
				for (std::size_t a = 0; a < clusters.size(); ++a) {
					for (std::size_t b = a + 1; b < clusters.size(); ++b) {
						if (!clusters[b].members.empty() && !clusters[a].members.empty() && clusters[a].root.front() == clusters[b].root.front()) {
							clusters[a].members.insert(clusters[a].members.end(), clusters[b].members.begin(), clusters[b].members.end());
							clusters[b].members.clear();
						}
					}
				}
				std::erase_if(clusters, [](const Cluster& c) { return c.members.empty(); });
				if (clusters.size() == 1) {
					clusters.front().members.insert(clusters.front().members.end(), unstemmed.begin(), unstemmed.end());
				} else {
					for (const auto i : unstemmed) {
						clusters.push_back(Cluster{ {}, { i } });
					}
				}
			} else {
				for (const auto i : unstemmed) {
					// Copies of one form stay together.
					const auto same = std::ranges::find_if(clusters, [&](const Cluster& c) {
						return c.root.empty() && a_pieces[c.members.front()].form == a_pieces[i].form;
					});
					if (same != clusters.end()) {
						same->members.push_back(i);
					} else {
						clusters.push_back(Cluster{ {}, { i } });
					}
				}
				// Pieces still alone: by the family the plugin knows (what tempers them), one per slot group.
				struct Family
				{
					std::string              family;
					std::uint32_t            groups{ 0 };
					std::vector<std::size_t> clusters;
				};
				std::vector<Family> families;
				for (std::size_t c = 0; c < clusters.size(); ++c) {
					const auto& first = a_pieces[clusters[c].members.front()];
					if (first.family.empty() || DistinctForms(a_pieces, clusters[c].members) != 1) {
						continue;
					}
					const auto bits = GroupBits(first.slots);
					auto       it = std::ranges::find_if(families, [&](const Family& f) { return f.family == first.family && (f.groups & bits) == 0; });
					if (it == families.end()) {
						families.push_back(Family{ first.family, 0, {} });
						it = families.end() - 1;
					}
					it->groups |= bits;
					it->clusters.push_back(c);
				}
				for (const auto& f : families) {
					if (f.clusters.size() < 2) {
						continue;
					}
					auto& into = clusters[f.clusters.front()];
					for (std::size_t k = 1; k < f.clusters.size(); ++k) {
						auto& from = clusters[f.clusters[k]];
						into.members.insert(into.members.end(), from.members.begin(), from.members.end());
						from.members.clear();
					}
				}
				std::erase_if(clusters, [](const Cluster& c) { return c.members.empty(); });
			}

			for (auto& c : clusters) {
				std::ranges::sort(c.members);
				out.push_back(std::move(c.members));
			}
		}
		std::ranges::sort(out, {}, [](const std::vector<std::size_t>& a_set) { return a_set.front(); });
		return out;
	}

	std::vector<ArmorSet> BuildArmorSets(std::span<const ArmorPiece> a_pieces)
	{
		std::vector<ArmorSet> sets;
		for (auto& members : GroupArmorPieces(a_pieces)) {
			ArmorSet s;
			const auto& first = a_pieces[members.front()];
			s.plugin = first.form.plugin;
			s.material = ArmorMaterialKeyword(first);
			std::optional<FormRef> body;
			std::optional<FormRef> any;
			std::array<int, 3>     types{};
			for (const auto i : members) {
				const auto& p = a_pieces[i];
				s.slots |= p.slots;
				if ((p.slots & kSetSlotBody) && (!body || p.form < *body)) {
					body = p.form;
				}
				if (!any || p.form < *any) {
					any = p.form;
				}
				if (p.armorType) {
					++types[static_cast<std::size_t>(*p.armorType)];
				}
			}
			s.id = body ? *body : *any;
			const auto most = std::ranges::max_element(types);
			if (*most > 0) {
				s.armorType = static_cast<ArmorType>(most - types.begin());
			}
			if (DistinctForms(a_pieces, members) == 1) {
				s.name = Trim(first.name);
			} else {
				// The shortest stem; on a tie a body piece's, then the first.
				std::optional<std::size_t> best;
				std::size_t                bestWords = 0;
				for (const auto i : members) {
					const auto words = LowerStem(a_pieces[i].name).size();
					if (words == 0) {
						continue;
					}
					const bool isBody = (a_pieces[i].slots & kSetSlotBody) != 0;
					if (!best || words < bestWords || (words == bestWords && isBody && !(a_pieces[*best].slots & kSetSlotBody))) {
						best = i;
						bestWords = words;
					}
				}
				s.name = best ? ArmorNameStem(a_pieces[*best].name) : Trim(first.name);
			}
			if (s.name.empty()) {
				s.name = s.id.ToString();
			}
			s.pieces = std::move(members);
			sets.push_back(std::move(s));
		}

		// Unique names: armor type, then plugin, then a number.
		const auto dedupe = [&](auto&& a_suffix) {
			std::map<std::string, std::vector<std::size_t>> byName;
			for (std::size_t i = 0; i < sets.size(); ++i) {
				byName[ToLower(sets[i].name)].push_back(i);
			}
			for (const auto& entry : byName) {
				const auto& same = entry.second;
				if (same.size() < 2) {
					continue;
				}
				for (std::size_t k = 0; k < same.size(); ++k) {
					a_suffix(sets[same[k]], k, same);
				}
			}
		};
		dedupe([&](ArmorSet& a_set, std::size_t, const std::vector<std::size_t>& a_same) {
			const bool typesDiffer = std::ranges::any_of(a_same, [&](std::size_t j) { return sets[j].armorType != sets[a_same.front()].armorType; });
			if (typesDiffer && a_set.armorType) {
				a_set.name += std::format(" ({})", TypeName(*a_set.armorType));
			}
		});
		dedupe([&](ArmorSet& a_set, std::size_t, const std::vector<std::size_t>& a_same) {
			const bool pluginsDiffer = std::ranges::any_of(a_same, [&](std::size_t j) { return !IEquals(sets[j].plugin, sets[a_same.front()].plugin); });
			if (pluginsDiffer) {
				a_set.name += std::format(" ({})", a_set.plugin);
			}
		});
		std::ranges::sort(sets, [](const ArmorSet& a_lhs, const ArmorSet& a_rhs) { return a_lhs.id < a_rhs.id; });
		dedupe([&](ArmorSet& a_set, std::size_t a_k, const std::vector<std::size_t>&) {
			if (a_k > 0) {
				a_set.name += std::format(" {}", a_k + 1);
			}
		});

		std::ranges::sort(sets, [](const ArmorSet& a_lhs, const ArmorSet& a_rhs) {
			if (const int c = ICompare(a_lhs.name, a_rhs.name); c != 0) {
				return c < 0;
			}
			return a_lhs.id < a_rhs.id;
		});
		return sets;
	}

	std::vector<std::size_t> PickOutfit(std::span<const ArmorPiece> a_pieces, std::span<const std::size_t> a_order, std::uint32_t a_worn, bool a_needBody)
	{
		std::vector<std::size_t> out;
		std::uint32_t            slots = a_worn;
		std::set<FormRef>        forms;
		const auto               fits = [&](std::size_t a_i) {
			const auto& p = a_pieces[a_i];
			return (p.slots & kSetSlots) != 0 && (slots & p.slots) == 0 && !forms.contains(p.form);
		};
		const auto add = [&](std::size_t a_i) {
			slots |= a_pieces[a_i].slots;
			forms.insert(a_pieces[a_i].form);
			out.push_back(a_i);
		};

		std::optional<ArmorType> bodyType;
		for (const auto i : a_order) {
			if ((a_pieces[i].slots & kSetSlotBody) && fits(i)) {
				add(i);
				bodyType = a_pieces[i].armorType;
				break;
			}
		}
		if (out.empty() && a_needBody) {
			return out;
		}
		std::vector<std::size_t> rest(a_order.begin(), a_order.end());
		if (bodyType) {
			std::ranges::stable_partition(rest, [&](std::size_t i) { return a_pieces[i].armorType == bodyType; });
		}
		for (const auto i : rest) {
			if (fits(i)) {
				add(i);
			}
		}
		return out;
	}

	SetCoverage CoverageOf(const ArmorSet& a_set, std::span<const ArmorPiece> a_pieces, const std::set<FormRef>& a_have)
	{
		SetCoverage       out;
		std::uint32_t     have = 0;
		std::set<FormRef> counted;
		for (const auto i : a_set.pieces) {
			const auto& p = a_pieces[i];
			if (a_have.contains(p.form)) {
				have |= p.slots;
				counted.insert(p.form);
			}
		}
		out.total = SlotGroupCount(a_set.slots & kSetSlots);
		out.have = SlotGroupCount(have & a_set.slots & kSetSlots);
		out.pieces = static_cast<int>(counted.size());
		return out;
	}

	bool ArmorSetMatches(const ArmorSet& a_set, std::span<const ArmorPiece> a_pieces, std::string_view a_query)
	{
		const auto words = Words(a_query);
		if (words.empty()) {
			return true;
		}
		std::string hay = ToLower(a_set.name) + '\n' + ToLower(a_set.plugin);
		for (const auto i : a_set.pieces) {
			hay += '\n' + ToLower(a_pieces[i].name);
		}
		return std::ranges::all_of(words, [&](const Word& w) { return hay.find(w.lower) != std::string::npos; });
	}

	bool SetMannequinOpen(std::span<const ArmorPiece> a_set, std::span<const FormRef> a_holds, std::uint32_t a_worn)
	{
		const auto inSet = [&](const FormRef& a_form) { return std::ranges::any_of(a_set, [&](const ArmorPiece& p) { return p.form == a_form; }); };
		if (!std::ranges::all_of(a_holds, inSet)) {
			return false;
		}
		return std::ranges::any_of(a_set, [&](const ArmorPiece& p) {
			return (p.slots & kSetSlots) != 0 && (p.slots & a_worn) == 0 && std::ranges::find(a_holds, p.form) == a_holds.end();
		});
	}
}
