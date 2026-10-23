#include "dova/core/DraftSuggest.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <map>
#include <set>

#include "JsonUtil.h"
#include "dova/core/Category.h"

namespace dova::core
{
	namespace
	{
		using json = nlohmann::json;

		// The default rules. Words are tokens (see DraftSuggest.h); keywords are EditorIDs; radius in game
		// units (about 70 per metre). Confidence 0.8+ : the name says it outright; 0.5-0.7: a station next to
		// it or a typical use of the piece of furniture; below 0.5: a hint, not accepted on its own.
		constexpr std::string_view kDefaultRules = R"json({
	"$schema": "dovastash-draftrules-1",
	"acceptAt": 0.5,
	// The fill pass (below) gives a group to a container on weaker evidence, but not below this, and never on loose
	// items alone: a cupboard three metres from a mannequin is no armory, bones on an end table are decoration.
	"fillAt": 0.3,

	// First match wins. "when": respawns | upgrade | owned | match (editorIds, scripts, words).
	"ignore": [
		{ "when": "respawns", "note": "Respawns: what you store here can disappear." },
		{ "when": "match", "editorIds": ["PlayerBookShelfContainer"], "scripts": ["PlayerBookShelfContainerScript"],
		  "note": "Scripted bookshelf: it only holds books and hands everything else back." },
		{ "when": "upgrade", "unlessScripts": ["BYOHDivertItemScript"], "note": "Goes away with a house upgrade: anything inside would be lost." },
		{ "when": "owned", "note": "Owned by someone else: taking from it would be stealing." },
		{ "when": "match", "words": ["display case"], "note": "A display case: it shows what is put in it." }
	],

	"harmlessScripts": ["BYOHDivertItemScript", "defaultsetstagecontainerscript"],

	// Words that say nothing about what a container holds. A container whose name has any other word ("Dragonpriest
	// Masks", "Food & Drinks") is named for its contents: only its name counts, never its EditorID, and it is no master.
	// The master's "words" and "avoid" count as generic too.
	"genericWords": ["large", "small", "big", "little", "tiny", "huge", "old", "new", "wooden", "iron", "steel", "noble", "common",
	                 "rustic", "fancy", "ornate", "plain", "simple", "sturdy", "upper", "lower", "left", "right", "player", "house", "home", "my",
	                 "personal", "private", "hidden", "secret", "empty", "open", "nordic", "nord", "imperial", "dwemer", "dwarven", "elven",
	                 "orcish", "falmer", "dunmer", "telvanni", "redoran", "hlaalu", "upstairs", "downstairs", "bedroom", "container", "furniture",
	                 "a", "an", "the", "of", "and"],
	// A match found only in the EditorID (the name is generic) counts this much of the rule's confidence.
	"editorIdFactor": 0.8,

	"master": {
		"words": ["chest", "trunk", "coffer", "storage", "stash", "master", "drop off", "dropoff", "deposit", "unsorted", "overflow"],
		"avoid": ["strongbox", "strong box", "sack", "satchel", "knapsack", "backpack", "pack", "barrel", "basket", "urn", "jar", "pot", "pouch", "bag", "bucket", "end table",
		          "night table", "nightstand", "cupboard", "dresser", "drawer", "wardrobe", "box", "crate", "safe", "case", "cabinet", "shelf",
		          "bookcase", "bookshelf", "desk", "rack", "font", "pedestal", "planter", "coffin", "altar", "shrine", "display", "mount"],
		// Size counts up to "fullSize" cubic units (a small chest is about 70000, a large one 230000): beyond that, bigger is not better.
		"sizeWeight": 1.0, "fullSize": 100000, "wordWeight": 1.5, "avoidWeight": 1.0, "entranceWeight": 1.0, "centralWeight": 0.5,
		"entranceRange": 1500, "namedPenalty": 3.0
	},

	"names": [
		{ "label": "alchemy", "words": ["alchem*", "apothecary", "reagent"],
		  "categories": ["alchemy.ingredients", "alchemy.potions", "alchemy.poisons"], "confidence": 0.85 },
		{ "label": "ingredients", "words": ["ingredient", "herb"], "notWords": ["cooking", "cook", "food", "kitchen"],
		  "categories": ["alchemy.ingredients"], "confidence": 0.85 },
		{ "label": "potions", "words": ["potion", "elixir", "tonic"], "categories": ["alchemy.potions", "alchemy.poisons"], "confidence": 0.85 },
		{ "label": "poisons", "words": ["poison"], "categories": ["alchemy.poisons"], "confidence": 0.85 },
		{ "label": "food", "words": ["food", "pantry", "larder", "provision", "produce", "vegetable", "fruit", "meat", "fish", "grain", "flour", "kitchen",
		                             "foodbarrel", "cheese", "bread", "cooking", "drink", "beverage", "wine", "mead", "ale", "spice", "dumpling",
		                             "soup", "stew", "egg"],
		  "categories": ["alchemy.food"], "confidence": 0.8 },
		{ "label": "weapons", "words": ["weapon", "armory", "armoury", "arsenal", "sword", "blade"], "categories": ["weapons.*"], "confidence": 0.85 },
		{ "label": "staves", "words": ["staff", "staves", "stave"], "categories": ["weapons.staves"], "confidence": 0.85 },
		{ "label": "armor", "words": ["armor", "armour", "cuirass"], "categories": ["armor.heavy", "armor.light", "armor.shields"], "confidence": 0.85 },
		{ "label": "shields", "words": ["shield"], "categories": ["armor.shields"], "confidence": 0.8 },
		{ "label": "ammunition", "words": ["arrow", "quiver", "ammo", "ammunition", "bolt"], "categories": ["ammo.*"], "confidence": 0.85 },
		{ "label": "clothes", "words": ["clothes", "clothing", "garment", "apparel", "outfit"], "categories": ["armor.clothing"], "confidence": 0.85 },
		{ "label": "clothes", "words": ["wardrobe", "closet", "armoire"], "categories": ["armor.clothing"], "confidence": 0.6 },
		{ "label": "clothes", "words": ["dresser"], "categories": ["armor.clothing"], "confidence": 0.45 },
		{ "label": "jewelry", "words": ["jewel*", "ring", "necklace", "amulet", "valuables", "treasure", "jewelrybox"],
		  "categories": ["armor.jewelry", "misc.gems"], "confidence": 0.8 },
		{ "label": "gems", "words": ["gem", "gemstone", "jewel stone"], "notWords": ["soul"], "categories": ["misc.gems"], "confidence": 0.8 },
		{ "label": "valuables", "words": ["strongbox", "strong box", "safe", "coffer"], "categories": ["armor.jewelry", "misc.gems"], "confidence": 0.45 },
		{ "label": "soul gems", "words": ["soul gem", "soulgem*", "soul"], "categories": ["soulgems.*"], "confidence": 0.85 },
		{ "label": "enchanting", "words": ["enchant*", "arcane"], "categories": ["soulgems.*"], "confidence": 0.6 },
		{ "label": "books", "words": ["book", "library", "tome", "journal", "literature"], "notWords": ["spell"], "categories": ["books.*"], "confidence": 0.8 },
		{ "label": "notes", "words": ["note", "letter", "recipe", "bounty", "bounties"], "categories": ["books.notes"], "confidence": 0.8 },
		{ "label": "spell tomes", "words": ["spell tome", "spelltome*", "spellbook*"], "categories": ["books.spelltomes"], "confidence": 0.85 },
		{ "label": "scrolls", "words": ["scroll"], "categories": ["scrolls"], "confidence": 0.8 },
		{ "label": "ingots and ores", "words": ["ingot", "ore", "metal", "smith*", "smelt*", "mining", "mineral", "blacksmith"], "categories": ["misc.ores_ingots"], "confidence": 0.8 },
		{ "label": "hides", "words": ["hide", "pelt", "leather", "fur", "tanning", "tanner"], "categories": ["misc.hides_pelts"], "confidence": 0.8 },
		{ "label": "animal parts", "words": ["trophy", "bone", "claw", "horn", "tusk"], "categories": ["misc.animal_parts"], "confidence": 0.6 },
		{ "label": "Dwemer parts", "words": ["scrap", "dwemer part", "dwarven part", "dwemer metal", "dwarven metal", "dwemer material", "dwarven material",
		                                     "dwemer component", "dwarven component", "dwemer curio", "dwarven curio"],
		  "categories": ["misc.dwemer"], "confidence": 0.75 },
		{ "label": "building materials", "words": ["building", "construction", "lumber", "carpent*", "nail", "hinge"],
		  "categories": ["misc.building_materials"], "confidence": 0.75 },
		{ "label": "tools", "words": ["tool", "toolbox"], "categories": ["misc.tools"], "confidence": 0.75 },
		{ "label": "keys", "words": ["key"], "categories": ["keys"], "confidence": 0.75 },
		{ "label": "odds and ends", "words": ["junk", "clutter", "misc*", "sundries", "trinket", "odds"], "categories": ["misc.clutter"], "confidence": 0.75 },
		{ "label": "food (barrels and cupboards often hold it)", "words": ["barrel", "cupboard"], "categories": ["alchemy.food"], "confidence": 0.35 },
		{ "label": "jewelry (a night table)", "words": ["night table", "nightstand", "night stand", "nighttable"], "categories": ["armor.jewelry", "misc.gems"], "confidence": 0.35 },
		{ "label": "books (an end table)", "words": ["end table", "endtable"], "categories": ["books.*"], "confidence": 0.3 },
		{ "label": "ingredients (a satchel)", "words": ["satchel"], "categories": ["alchemy.ingredients"], "confidence": 0.35 },
		{ "label": "odds and ends (a sack)", "words": ["sack", "knapsack"], "categories": ["misc.clutter"], "confidence": 0.3 }
	],

	// A station counts for containers within "radius" (game units, about 70 per metre; height counts double), at full
	// confidence next to it and half at the edge. Racks and mannequins are hints only: below fillAt, they add up with
	// a name or items, never decide alone.
	"stations": [
		{ "label": "forge", "keywords": ["CraftingSmithingForge", "CraftingSmithingSkyforge", "CraftingSmithingArmorTable", "CraftingSmithingSharpeningWheel"],
		  "words": ["forge", "anvil", "grindstone", "sharpening wheel", "armor table"], "categories": ["misc.ores_ingots"], "confidence": 0.65, "radius": 400 },
		{ "label": "smelter", "keywords": ["CraftingSmelter"], "words": ["smelter"], "categories": ["misc.ores_ingots"], "confidence": 0.7, "radius": 400 },
		{ "label": "tanning rack", "keywords": ["CraftingTanningRack"], "words": ["tanning rack", "tanningrack"], "categories": ["misc.hides_pelts"], "confidence": 0.65, "radius": 400 },
		{ "label": "alchemy lab", "keywords": ["WICraftingAlchemy", "CraftingAlchemy"], "words": ["alchemy lab", "alchemylab*", "alchemy table", "alchemy station"],
		  "categories": ["alchemy.ingredients", "alchemy.potions", "alchemy.poisons"], "confidence": 0.7, "radius": 350 },
		{ "label": "enchanter", "keywords": ["WICraftingEnchanting", "CraftingEnchanting"], "words": ["arcane enchanter", "enchanter", "enchanting table"],
		  "categories": ["soulgems.*"], "confidence": 0.7, "radius": 350 },
		{ "label": "cooking pot", "keywords": ["CraftingCookpot", "isCookingSpit", "BYOHCraftingOven", "CraftingOven"], "words": ["cookpot", "cooking pot", "oven", "cooking spit", "spit"],
		  "categories": ["alchemy.food"], "confidence": 0.65, "radius": 350 },
		{ "label": "bookshelf", "keywords": ["display:bookshelf"], "words": ["bookshelf", "bookcase", "bookshelves", "shelf book", "book shelf"], "categories": ["books.*"],
		  "confidence": 0.55, "radius": 250 },
		{ "label": "carpenter's workbench", "keywords": ["BYOHCraftingDraftingTable", "BYOHCarpenterTableKeyword"], "words": ["drafting table", "carpenter*"],
		  "categories": ["misc.building_materials"], "confidence": 0.6, "radius": 400 },
		{ "label": "weapon rack", "keywords": ["display:rack"], "categories": ["weapons.*"], "confidence": 0.3, "radius": 250 },
		{ "label": "mannequin", "keywords": ["display:mannequin"], "categories": ["armor.heavy", "armor.light"], "confidence": 0.3, "radius": 250 }
	],

	// Loose items on or next to a container: a hint (at most "max", below acceptAt), adding up with a name or a station.
	// Weapons, armor and ammunition lying about are decoration (a shield on the wall, a sword on a table): skipped.
	"nearby": {
		"radius": 96, "confidence": 0.25, "perItem": 0.05, "max": 0.45,
		"skip": ["misc.clutter", "weapons.*", "armor.heavy", "armor.light", "armor.shields", "ammo.*"],
		"groups": ["soulgems", "books"]
	},

	// What a home usually has a place for, as the shipped templates group it. A group nobody was given goes to the
	// free container with the strongest (weaker) evidence for it.
	"fill": [
		{ "label": "weapons", "categories": ["weapons.*"] },
		{ "label": "armor", "categories": ["armor.heavy", "armor.light", "armor.shields"] },
		{ "label": "clothes", "categories": ["armor.clothing"] },
		{ "label": "jewelry and gems", "categories": ["armor.jewelry", "misc.gems"] },
		{ "label": "alchemy", "categories": ["alchemy.ingredients", "alchemy.potions", "alchemy.poisons"] },
		{ "label": "food", "categories": ["alchemy.food"] },
		{ "label": "books", "categories": ["books.*"] },
		{ "label": "soul gems and scrolls", "categories": ["soulgems.*", "scrolls"] },
		{ "label": "ammunition", "categories": ["ammo.*"] },
		{ "label": "crafting materials", "categories": ["misc.ores_ingots", "misc.hides_pelts", "misc.animal_parts", "misc.dwemer"] },
		{ "label": "building materials and tools", "categories": ["misc.building_materials", "misc.tools"] },
		{ "label": "odds and ends", "categories": ["misc.clutter"] },
		{ "label": "keys", "categories": ["keys"] }
	]
})json";

		std::vector<std::string> SplitWords(std::string_view a_text)
		{
			std::vector<std::string> out;
			std::string              cur;
			for (const char c : a_text) {
				if (c == ' ') {
					if (!cur.empty()) {
						out.push_back(ToLower(cur));
						cur.clear();
					}
				} else {
					cur.push_back(c);
				}
			}
			if (!cur.empty()) {
				out.push_back(ToLower(cur));
			}
			return out;
		}

		bool ReadWordList(const json& a_obj, const char* a_key, std::vector<std::vector<std::string>>& a_out, std::string_view a_source, std::string_view a_where,
			Diagnostics& a_diags)
		{
			const auto it = a_obj.find(a_key);
			if (it == a_obj.end()) {
				return true;
			}
			auto list = detail::ReadStringList(*it, a_source, std::format("{}.{}", a_where, a_key), a_diags);
			if (!list) {
				return false;
			}
			for (const auto& s : *list) {
				auto words = SplitWords(s);
				if (!words.empty()) {
					a_out.push_back(std::move(words));
				}
			}
			return true;
		}

		bool ReadStrings(const json& a_obj, const char* a_key, std::vector<std::string>& a_out, std::string_view a_source, std::string_view a_where, Diagnostics& a_diags)
		{
			const auto it = a_obj.find(a_key);
			if (it == a_obj.end()) {
				return true;
			}
			auto list = detail::ReadStringList(*it, a_source, std::format("{}.{}", a_where, a_key), a_diags);
			if (!list) {
				return false;
			}
			a_out = std::move(*list);
			return true;
		}

		bool ReadFloat(const json& a_obj, const char* a_key, float& a_out, std::string_view a_source, std::string_view a_where, Diagnostics& a_diags)
		{
			const auto it = a_obj.find(a_key);
			if (it == a_obj.end()) {
				return true;
			}
			if (!it->is_number()) {
				a_diags.Error(std::string(a_source), std::format("{}.{}: expected a number, got {}", a_where, a_key, detail::TypeName(*it)));
				return false;
			}
			a_out = it->get<float>();
			return true;
		}

		void WarnUnknown(const json& a_obj, std::initializer_list<std::string_view> a_known, std::string_view a_source, std::string_view a_where, Diagnostics& a_diags)
		{
			for (const auto& [k, v] : a_obj.items()) {
				if (k.starts_with('$') || k.starts_with('_') || std::ranges::find(a_known, std::string_view(k)) != a_known.end()) {
					continue;
				}
				a_diags.Warn(std::string(a_source), std::format("{}: unknown key \"{}\"", a_where, k));
			}
		}

		std::vector<std::string> CategoryList(const json& a_obj, std::string_view a_source, std::string_view a_where, Diagnostics& a_diags, bool& a_ok)
		{
			std::vector<std::string> cats;
			a_ok = ReadStrings(a_obj, "categories", cats, a_source, a_where, a_diags);
			for (auto& c : cats) {
				c = ToLower(c);
				if (!IsValidCategoryPattern(c)) {
					a_diags.Error(std::string(a_source), std::format("{}: \"{}\" is not a category pattern", a_where, c));
					a_ok = false;
				}
			}
			if (a_ok && cats.empty()) {
				a_diags.Error(std::string(a_source), std::format("{}: no categories", a_where));
				a_ok = false;
			}
			return cats;
		}

		bool TokenMatches(std::string_view a_token, std::string_view a_word)
		{
			if (a_word.ends_with('*')) {
				return a_token.starts_with(a_word.substr(0, a_word.size() - 1));
			}
			if (a_token == a_word) {
				return true;
			}
			if (a_token.size() == a_word.size() + 1 && a_token.starts_with(a_word) && a_token.back() == 's') {
				return true;
			}
			return a_token.size() == a_word.size() + 2 && a_token.starts_with(a_word) && a_token.ends_with("es");
		}

		bool AnyWords(std::span<const std::string> a_tokens, const std::vector<std::vector<std::string>>& a_entries, std::string* a_which = nullptr)
		{
			for (const auto& e : a_entries) {
				if (DraftWordsMatch(a_tokens, e)) {
					if (a_which) {
						a_which->clear();
						for (const auto& w : e) {
							*a_which += (a_which->empty() ? "" : " ") + w;
						}
					}
					return true;
				}
			}
			return false;
		}

		bool HasAny(std::span<const std::string> a_have, std::span<const std::string> a_wanted, std::string* a_which = nullptr)
		{
			for (const auto& w : a_wanted) {
				for (const auto& h : a_have) {
					if (IEquals(h, w)) {
						if (a_which) {
							*a_which = h;
						}
						return true;
					}
				}
			}
			return false;
		}

		std::vector<std::string> ObjectTokens(const DraftObject& a_obj)
		{
			auto t = DraftTokens(a_obj.editorId);
			auto n = DraftTokens(a_obj.name);
			t.insert(t.end(), n.begin(), n.end());
			return t;
		}

		std::string Title(const DraftObject& a_obj)
		{
			if (!a_obj.name.empty()) {
				return a_obj.name;
			}
			return a_obj.editorId.empty() ? a_obj.id : a_obj.editorId;
		}

		// Distance between two objects, height counted double (another floor is further than it looks).
		float Distance(const DraftObject& a, const DraftObject& b)
		{
			const float dx = a.center[0] - b.center[0];
			const float dy = a.center[1] - b.center[1];
			const float dz = (a.center[2] - b.center[2]) * 2.0f;
			return std::sqrt(dx * dx + dy * dy + dz * dz);
		}

		// How far a point is from the box of a container (horizontal), and whether it is within its height band.
		float NearDistance(const DraftObject& a_container, const DraftObject& a_item, float a_radius)
		{
			const float hx = a_container.size[0] * 0.5f, hy = a_container.size[1] * 0.5f, hz = a_container.size[2] * 0.5f;
			const float dx = std::max(std::abs(a_item.center[0] - a_container.center[0]) - hx, 0.0f);
			const float dy = std::max(std::abs(a_item.center[1] - a_container.center[1]) - hy, 0.0f);
			const float lo = a_container.center[2] - hz - 32.0f;
			const float hi = a_container.center[2] + hz + a_radius;
			if (a_item.center[2] < lo || a_item.center[2] > hi) {
				return 1e9f;
			}
			return std::sqrt(dx * dx + dy * dy);
		}

		struct Evidence
		{
			float       confidence{ 0.0f };
			std::string reason;
			DraftSource source{ DraftSource::Name };
			float       best{ 0.0f };          // strongest piece
			float       stationDistance{ 1e9f };  // nearest station that supports it (tie-break)
			// How the container's own name claims it: 2 named for exactly this category ("Poison Crate", "Soul Gems"),
			// 1 its display name says it as part of a group ("Potion Basket", "Alchemy Satchel"), 0 no claim (EditorID,
			// stations, items nearby).
			int claim{ 0 };
			bool hinted{ false };  // some piece is not loose items nearby (the fill pass needs one)
		};

		void Add(std::map<std::string, Evidence>& a_into, const std::string& a_pattern, float a_conf, std::string a_reason, DraftSource a_source, float a_dist = 1e9f,
			int a_claim = 0)
		{
			auto& e = a_into[a_pattern];
			e.claim = std::max(e.claim, a_claim);
			e.hinted |= a_source != DraftSource::Nearby;
			e.confidence = 1.0f - (1.0f - e.confidence) * (1.0f - std::clamp(a_conf, 0.0f, 1.0f));
			e.reason += (e.reason.empty() ? "" : "; ") + a_reason;
			if (a_conf > e.best) {
				e.best = a_conf;
				e.source = a_source;
			}
			e.stationDistance = std::min(e.stationDistance, a_dist);
		}

		bool Harmless(const DraftObject& a_obj, const DraftRules& a_rules)
		{
			return std::ranges::all_of(a_obj.scripts, [&](const std::string& s) { return HasAny(std::span(&s, 1), a_rules.harmlessScripts); });
		}

		std::string Percent(float a_conf) { return std::format("{:.0f}%", a_conf * 100.0f); }

		// A word that says nothing about what a container holds: the generic words, and the master's chest-like and
		// small-or-odd furniture words ("chest", "barrel", "drawers").
		bool GenericToken(std::string_view a_token, const DraftRules& a_rules)
		{
			if (std::ranges::any_of(a_rules.genericWords, [&](const std::string& w) { return TokenMatches(a_token, w); })) {
				return true;
			}
			const auto inEntries = [&](const std::vector<std::vector<std::string>>& a_entries) {
				return std::ranges::any_of(a_entries, [&](const std::vector<std::string>& e) {
					return std::ranges::any_of(e, [&](const std::string& w) { return TokenMatches(a_token, w); });
				});
			};
			return inEntries(a_rules.masterWords) || inEntries(a_rules.masterAvoid);
		}

		// What a container's name tells. The display name is what the home's author wrote for players: when it has a
		// word beyond the generic ones it is specific ("Dragonpriest Masks", "Food & Drinks") and is all that counts.
		// A generic or missing display name ("Chest", "Barrel") lets the EditorID speak too ("PlayerHouseChestAlchemy").
		struct NameView
		{
			std::vector<std::string> name;      // display name tokens
			std::vector<std::string> combined;  // what name rules read: the name, plus the EditorID when the name is generic
			bool                     specific{ false };
			std::string              core;      // the display name without its generic words ("Apple Crate" -> "apple")
		};

		NameView ViewName(const DraftObject& a_obj, const DraftRules& a_rules)
		{
			NameView v;
			v.name = DraftTokens(a_obj.name);
			for (const auto& t : v.name) {
				if (!GenericToken(t, a_rules)) {
					v.specific = true;
					v.core += (v.core.empty() ? "" : " ") + t;
				}
			}
			v.combined = v.name;
			if (!v.specific) {
				const auto ed = DraftTokens(a_obj.editorId);
				v.combined.insert(v.combined.end(), ed.begin(), ed.end());
			}
			return v;
		}

		bool AnyToken(std::span<const std::string> a_tokens, std::span<const std::string> a_words)
		{
			return std::ranges::any_of(a_words, [&](const std::string& w) { return std::ranges::any_of(a_tokens, [&](const std::string& t) { return TokenMatches(t, w); }); });
		}
	}

	std::string_view DefaultDraftRulesText() { return kDefaultRules; }

	const DraftRules& DefaultDraftRules()
	{
		static const DraftRules rules = [] {
			Diagnostics d;
			auto        r = ParseDraftRules(kDefaultRules, "default draft rules", d);
			return r ? std::move(*r) : DraftRules{};
		}();
		return rules;
	}

	std::string_view DraftSourceName(DraftSource a_source) noexcept
	{
		switch (a_source) {
		case DraftSource::Name:
			return "name";
		case DraftSource::Station:
			return "station";
		case DraftSource::Nearby:
			return "nearby items";
		case DraftSource::Fill:
			return "best free place";
		}
		return "?";
	}

	std::string DraftMetres(float a_units) { return std::format("{:.1f} m", a_units / 70.0f); }

	std::vector<std::string> DraftTokens(std::string_view a_text)
	{
		std::vector<std::string> out;
		std::string              cur;
		auto                     flush = [&] {
            if (!cur.empty()) {
                out.push_back(ToLower(cur));
                cur.clear();
            }
		};
		for (std::size_t i = 0; i < a_text.size(); ++i) {
			const auto c = static_cast<unsigned char>(a_text[i]);
			if (std::isalpha(c) || c >= 0x80) {
				// A capital after a lower-case letter starts a word ("ChestAlchemy"); in a run of capitals the last one
				// before a lower-case letter does ("DLCChest" -> dlc chest).
				if (std::isupper(c) && !cur.empty()) {
					const auto prev = static_cast<unsigned char>(cur.back());
					const bool nextLower = i + 1 < a_text.size() && std::islower(static_cast<unsigned char>(a_text[i + 1]));
					if (std::islower(prev) || (std::isupper(prev) && nextLower)) {
						flush();
					}
				}
				cur.push_back(static_cast<char>(c));
			} else if (c == '\'') {
				// "Apothecary's": the possessive stays with its word, then is dropped.
				if (i + 1 < a_text.size() && (a_text[i + 1] == 's' || a_text[i + 1] == 'S') && (i + 2 >= a_text.size() || !std::isalpha(static_cast<unsigned char>(a_text[i + 2])))) {
					++i;
				}
				flush();
			} else {
				flush();
			}
		}
		flush();
		return out;
	}

	bool DraftWordsMatch(std::span<const std::string> a_tokens, std::span<const std::string> a_words)
	{
		if (a_words.empty()) {
			return false;
		}
		// A word written as one token in the rule may be glued in an EditorID ("foodbarrel"); a rule of several
		// words also matches their concatenation as one token ("soul gem" -> "soulgem").
		std::string glued;
		for (const auto& w : a_words) {
			glued += w;
		}
		if (a_words.size() > 1 && std::ranges::any_of(a_tokens, [&](const std::string& t) { return TokenMatches(t, glued); })) {
			return true;
		}
		return std::ranges::all_of(a_words, [&](const std::string& w) { return std::ranges::any_of(a_tokens, [&](const std::string& t) { return TokenMatches(t, w); }); });
	}

	std::optional<DraftRules> ParseDraftRules(std::string_view a_text, std::string_view a_source, Diagnostics& a_diags)
	{
		auto doc = detail::ParseJson(a_text, a_source, a_diags);
		if (!doc) {
			return std::nullopt;
		}
		if (!doc->is_object()) {
			a_diags.Error(std::string(a_source), "draft rules must be a JSON object");
			return std::nullopt;
		}
		if (const auto s = doc->find("$schema"); s != doc->end() && (!s->is_string() || s->get<std::string>() != kDraftRulesSchema)) {
			a_diags.Error(std::string(a_source), std::format("$schema must be \"{}\"", kDraftRulesSchema));
			return std::nullopt;
		}
		WarnUnknown(*doc,
			{ "acceptAt", "keepAt", "fillAt", "maxPerContainer", "categoryNameConfidence", "itemNameConfidence", "editorIdFactor", "genericWords", "ignore",
				"harmlessScripts", "master", "names", "stations", "nearby", "fill" },
			a_source, "rules", a_diags);
		DraftRules r;
		ReadFloat(*doc, "acceptAt", r.acceptAt, a_source, "rules", a_diags);
		ReadFloat(*doc, "keepAt", r.keepAt, a_source, "rules", a_diags);
		ReadFloat(*doc, "fillAt", r.fillAt, a_source, "rules", a_diags);
		ReadFloat(*doc, "categoryNameConfidence", r.categoryNameConfidence, a_source, "rules", a_diags);
		ReadFloat(*doc, "itemNameConfidence", r.itemNameConfidence, a_source, "rules", a_diags);
		ReadFloat(*doc, "editorIdFactor", r.editorIdFactor, a_source, "rules", a_diags);
		ReadStrings(*doc, "genericWords", r.genericWords, a_source, "rules", a_diags);
		for (auto& w : r.genericWords) {
			w = ToLower(w);
		}
		{
			float maxPer = static_cast<float>(r.maxPerContainer);
			if (ReadFloat(*doc, "maxPerContainer", maxPer, a_source, "rules", a_diags) && maxPer >= 1.0f) {
				r.maxPerContainer = static_cast<std::size_t>(maxPer);
			}
		}
		ReadStrings(*doc, "harmlessScripts", r.harmlessScripts, a_source, "rules", a_diags);

		if (const auto it = doc->find("ignore"); it != doc->end() && it->is_array()) {
			for (std::size_t i = 0; i < it->size(); ++i) {
				const auto& e = (*it)[i];
				const auto  where = std::format("ignore[{}]", i);
				if (!e.is_object()) {
					a_diags.Error(std::string(a_source), where + ": expected an object");
					continue;
				}
				WarnUnknown(e, { "when", "editorIds", "scripts", "words", "unlessScripts", "note" }, a_source, where, a_diags);
				DraftIgnoreRule rule;
				const auto      when = e.value("when", std::string("match"));
				if (IEquals(when, "respawns")) {
					rule.when = DraftIgnoreRule::When::Respawns;
				} else if (IEquals(when, "upgrade")) {
					rule.when = DraftIgnoreRule::When::Upgrade;
				} else if (IEquals(when, "owned")) {
					rule.when = DraftIgnoreRule::When::Owned;
				} else if (IEquals(when, "match")) {
					rule.when = DraftIgnoreRule::When::Match;
				} else {
					a_diags.Error(std::string(a_source), std::format("{}: unknown \"when\" \"{}\" (respawns, upgrade, owned, match)", where, when));
					continue;
				}
				bool ok = ReadStrings(e, "editorIds", rule.editorIds, a_source, where, a_diags) && ReadStrings(e, "scripts", rule.scripts, a_source, where, a_diags) &&
				          ReadWordList(e, "words", rule.words, a_source, where, a_diags) && ReadStrings(e, "unlessScripts", rule.unlessScripts, a_source, where, a_diags);
				rule.note = e.value("note", std::string{});
				if (ok && rule.when == DraftIgnoreRule::When::Match && rule.editorIds.empty() && rule.scripts.empty() && rule.words.empty()) {
					a_diags.Error(std::string(a_source), where + ": a \"match\" rule needs editorIds, scripts or words");
					ok = false;
				}
				if (ok) {
					r.ignore.push_back(std::move(rule));
				}
			}
		}

		if (const auto it = doc->find("master"); it != doc->end() && it->is_object()) {
			WarnUnknown(*it,
				{ "words", "avoid", "sizeWeight", "fullSize", "wordWeight", "avoidWeight", "entranceWeight", "centralWeight", "entranceRange", "namedPenalty" },
				a_source, "master", a_diags);
			ReadFloat(*it, "namedPenalty", r.masterNamedPenalty, a_source, "master", a_diags);
			ReadWordList(*it, "words", r.masterWords, a_source, "master", a_diags);
			ReadWordList(*it, "avoid", r.masterAvoid, a_source, "master", a_diags);
			ReadFloat(*it, "sizeWeight", r.masterSizeWeight, a_source, "master", a_diags);
			ReadFloat(*it, "fullSize", r.masterFullSize, a_source, "master", a_diags);
			ReadFloat(*it, "wordWeight", r.masterWordWeight, a_source, "master", a_diags);
			ReadFloat(*it, "avoidWeight", r.masterAvoidWeight, a_source, "master", a_diags);
			ReadFloat(*it, "entranceWeight", r.masterEntranceWeight, a_source, "master", a_diags);
			ReadFloat(*it, "centralWeight", r.masterCentralWeight, a_source, "master", a_diags);
			ReadFloat(*it, "entranceRange", r.masterEntranceRange, a_source, "master", a_diags);
		}

		if (const auto it = doc->find("names"); it != doc->end() && it->is_array()) {
			for (std::size_t i = 0; i < it->size(); ++i) {
				const auto& e = (*it)[i];
				const auto  where = std::format("names[{}]", i);
				if (!e.is_object()) {
					a_diags.Error(std::string(a_source), where + ": expected an object");
					continue;
				}
				WarnUnknown(e, { "label", "words", "notWords", "categories", "confidence" }, a_source, where, a_diags);
				DraftNameRule rule;
				rule.label = e.value("label", std::string{});
				bool ok = ReadWordList(e, "words", rule.words, a_source, where, a_diags);
				if (const auto nw = e.find("notWords"); nw != e.end()) {
					std::vector<std::string> list;
					ok = ReadStrings(e, "notWords", list, a_source, where, a_diags) && ok;
					for (auto& w : list) {
						rule.notWords.push_back(ToLower(w));
					}
				}
				bool catsOk = false;
				rule.categories = CategoryList(e, a_source, where, a_diags, catsOk);
				ok = ReadFloat(e, "confidence", rule.confidence, a_source, where, a_diags) && ok && catsOk;
				if (ok && rule.words.empty()) {
					a_diags.Error(std::string(a_source), where + ": no words");
					ok = false;
				}
				if (ok) {
					r.names.push_back(std::move(rule));
				}
			}
		}

		if (const auto it = doc->find("stations"); it != doc->end() && it->is_array()) {
			for (std::size_t i = 0; i < it->size(); ++i) {
				const auto& e = (*it)[i];
				const auto  where = std::format("stations[{}]", i);
				if (!e.is_object()) {
					a_diags.Error(std::string(a_source), where + ": expected an object");
					continue;
				}
				WarnUnknown(e, { "label", "words", "keywords", "categories", "confidence", "radius" }, a_source, where, a_diags);
				DraftStationRule rule;
				rule.label = e.value("label", std::string{});
				bool ok = ReadWordList(e, "words", rule.words, a_source, where, a_diags) && ReadStrings(e, "keywords", rule.keywords, a_source, where, a_diags);
				bool catsOk = false;
				rule.categories = CategoryList(e, a_source, where, a_diags, catsOk);
				ok = ReadFloat(e, "confidence", rule.confidence, a_source, where, a_diags) && ReadFloat(e, "radius", rule.radius, a_source, where, a_diags) && ok && catsOk;
				if (ok && rule.words.empty() && rule.keywords.empty()) {
					a_diags.Error(std::string(a_source), where + ": needs words or keywords");
					ok = false;
				}
				if (ok) {
					r.stations.push_back(std::move(rule));
				}
			}
		}

		if (const auto it = doc->find("nearby"); it != doc->end() && it->is_object()) {
			WarnUnknown(*it, { "radius", "confidence", "perItem", "max", "skip", "groups" }, a_source, "nearby", a_diags);
			ReadFloat(*it, "radius", r.nearbyRadius, a_source, "nearby", a_diags);
			ReadFloat(*it, "confidence", r.nearbyConfidence, a_source, "nearby", a_diags);
			ReadFloat(*it, "perItem", r.nearbyPerItem, a_source, "nearby", a_diags);
			ReadFloat(*it, "max", r.nearbyMax, a_source, "nearby", a_diags);
			ReadStrings(*it, "skip", r.nearbySkip, a_source, "nearby", a_diags);
			ReadStrings(*it, "groups", r.nearbyGroups, a_source, "nearby", a_diags);
			for (auto& s : r.nearbySkip) {
				s = ToLower(s);
			}
			for (auto& s : r.nearbyGroups) {
				s = ToLower(s);
			}
		}

		if (const auto it = doc->find("fill"); it != doc->end() && it->is_array()) {
			for (std::size_t i = 0; i < it->size(); ++i) {
				const auto& e = (*it)[i];
				const auto  where = std::format("fill[{}]", i);
				if (!e.is_object()) {
					a_diags.Error(std::string(a_source), where + ": expected an object");
					continue;
				}
				WarnUnknown(e, { "label", "categories" }, a_source, where, a_diags);
				DraftRules::FillGroup g;
				g.label = e.value("label", std::string{});
				bool ok = false;
				g.categories = CategoryList(e, a_source, where, a_diags, ok);
				if (ok) {
					r.fill.push_back(std::move(g));
				}
			}
		}
		return r;
	}

	const DraftContainer* DraftPlan::Find(std::size_t a_object) const
	{
		const auto it = std::ranges::find_if(containers, [&](const DraftContainer& c) { return c.object == a_object; });
		return it == containers.end() ? nullptr : &*it;
	}

	DraftContainer* DraftPlan::Find(std::size_t a_object)
	{
		const auto it = std::ranges::find_if(containers, [&](const DraftContainer& c) { return c.object == a_object; });
		return it == containers.end() ? nullptr : &*it;
	}

	DraftPlan SuggestDraft(std::span<const DraftObject> a_objects, const DraftRules& a_rules, const DraftOptions& a_options)
	{
		DraftPlan plan;
		using Kind = DraftObject::Kind;

		// ---- Ignored containers ----
		for (std::size_t i = 0; i < a_objects.size(); ++i) {
			const auto& o = a_objects[i];
			if (o.kind != Kind::Container) {
				continue;
			}
			DraftContainer c;
			c.object = i;
			const auto tokens = ObjectTokens(o);
			for (const auto& rule : a_rules.ignore) {
				bool hit = false;
				switch (rule.when) {
				case DraftIgnoreRule::When::Respawns:
					hit = o.respawns;
					break;
				case DraftIgnoreRule::When::Upgrade:
					hit = o.removedByUpgrade && !HasAny(o.scripts, rule.unlessScripts);
					break;
				case DraftIgnoreRule::When::Owned:
					hit = o.ownedByOther;
					break;
				case DraftIgnoreRule::When::Match:
					hit = HasAny(std::span(&o.editorId, 1), rule.editorIds) || HasAny(o.scripts, rule.scripts) || AnyWords(tokens, rule.words);
					break;
				}
				if (hit) {
					c.ignored = true;
					c.note = rule.note;
					break;
				}
			}
			plan.containers.push_back(std::move(c));
		}

		// ---- What the names say (before the master: a container named for what it holds is no master) ----
		auto known = [&](const std::string& p) { return !a_options.knownPattern || a_options.knownPattern(p); };
		std::vector<std::map<std::string, Evidence>> evidence(plan.containers.size());
		std::vector<float>                           named(plan.containers.size(), 0.0f);
		// Category names split into alternatives: "Ores & ingots" -> [ores] | [ingots].
		struct CategoryWords
		{
			std::string                           pattern, displayName;
			std::vector<std::vector<std::string>> alts;
		};
		std::vector<CategoryWords> categoryWords;
		for (const auto& [pattern, displayName] : a_options.categoryNames) {
			std::string display = displayName;
			std::ranges::replace(display, '&', ',');
			std::vector<std::vector<std::string>> alts;
			std::vector<std::string>              cur;
			std::string                           spaced;
			for (const char ch : display) {
				spaced += ch == ',' ? std::string(" and ") : std::string(1, ch);
			}
			for (const auto& t : DraftTokens(spaced)) {
				if (t == "and") {
					if (!cur.empty()) {
						alts.push_back(std::move(cur));
						cur.clear();
					}
					continue;
				}
				if (t == "of" || t == "the" || t == "other" || t == "items" || t == "item") {
					continue;
				}
				cur.push_back(t);
			}
			if (!cur.empty()) {
				alts.push_back(std::move(cur));
			}
			if (!alts.empty()) {
				categoryWords.push_back({ pattern, displayName, std::move(alts) });
			}
		}
		std::vector<NameView> views(plan.containers.size());
		for (std::size_t ci = 0; ci < plan.containers.size(); ++ci) {
			const auto& c = plan.containers[ci];
			const auto& o = a_objects[c.object];
			views[ci] = ViewName(o, a_rules);
			if (c.ignored) {
				continue;
			}
			const auto& view = views[ci];
			auto&       ev = evidence[ci];
			for (const auto& rule : a_rules.names) {
				// The display name first; the EditorID only speaks for a generic name, and less.
				std::string which;
				bool        onName = AnyWords(view.name, rule.words, &which) && !AnyToken(view.name, rule.notWords);
				const bool  onEditorId = !onName && view.combined.size() > view.name.size() && AnyWords(view.combined, rule.words, &which) &&
				                        !AnyToken(view.combined, rule.notWords);
				if (!onName && !onEditorId) {
					continue;
				}
				const float conf = rule.confidence * (onName ? 1.0f : a_rules.editorIdFactor);
				const auto  label = rule.label.empty() ? rule.categories.front() : rule.label;
				const auto  reason = onName ? std::format("name \"{}\" says {} (\"{}\")", o.name, label, which) : std::format("EditorID {} says {} (\"{}\")", o.editorId, label, which);
				for (const auto& cat : rule.categories) {
					if (known(cat)) {
						// Only a rule that is sure on its own makes the container the place for exactly that category.
						const bool sure = rule.confidence + 1e-4f >= a_rules.acceptAt;
						Add(ev, cat, conf, reason, DraftSource::Name, 1e9f, !sure ? 0 : !onName ? (rule.categories.size() == 1 ? 1 : 0) : rule.categories.size() == 1 ? 2 : 1);
						named[ci] = std::max(named[ci], conf);
					}
				}
			}
			for (const auto& [pattern, displayName, alts] : categoryWords) {
				for (const auto& words : alts) {
					// The whole name must be about the category: every word of the container's name is one of the
					// category's words or a generic one ("Soul Gems", "Large Ingot Crate"), so "Chest of Drawers" is not "Chests".
					if (view.name.empty() || !DraftWordsMatch(view.name, words)) {
						continue;
					}
					const bool onlyThose = std::ranges::all_of(view.name, [&](const std::string& t) {
						return std::ranges::any_of(words, [&](const std::string& w) { return TokenMatches(t, w); }) || GenericToken(t, a_rules);
					});
					if (onlyThose && known(pattern)) {
						Add(ev, pattern, a_rules.categoryNameConfidence, std::format("named after the category \"{}\"", displayName), DraftSource::Name, 1e9f, 2);
						named[ci] = std::max(named[ci], a_rules.categoryNameConfidence);
					}
					break;
				}
			}
			if (a_options.itemCategoryByName && view.specific) {
				// "Ash Hopper Jelly"; else the name without its generic words ("Garlic Basket" -> "garlic"). A generic
				// name ("Storage") is no item's, whatever some book happens to be called. One item is a weak claim on
				// its whole category: "Pearls" does not take every ingredient from the "Alchemy Satchel".
				auto cat = a_options.itemCategoryByName(o.name);
				std::string_view item = o.name;
				if (!cat && !view.core.empty() && !IEquals(view.core, o.name)) {
					cat = a_options.itemCategoryByName(view.core);
					item = view.core;
				}
				if (cat && known(*cat)) {
					Add(ev, *cat, a_rules.itemNameConfidence, std::format("named after the item \"{}\"", item), DraftSource::Name, 1e9f);
					named[ci] = std::max(named[ci], a_rules.itemNameConfidence);
				}
			}
		}
		// Named for its contents: it gets what its name says and nothing else (stations and nearby items only add to
		// those). A specific name the rules cannot read ("Dragonpriest Masks") gets nothing: the author knows best.
		std::vector<bool> labelled(plan.containers.size(), false);
		for (std::size_t ci = 0; ci < plan.containers.size(); ++ci) {
			labelled[ci] = views[ci].specific || named[ci] + 1e-4f >= a_rules.acceptAt;
		}

		// ---- Master ----
		struct Candidate
		{
			std::size_t object;
			float       score;
			float       volume;
			bool        word, avoid;
			float       entrance;  // distance to the nearest load door, < 0: none
			std::string door;
			bool        named{ false };  // its name says what it holds
		};
		std::vector<Candidate> candidates;
		auto usable = [&](const DraftContainer& c, bool a_mainOnly) {
			const auto& o = a_objects[c.object];
			return !c.ignored && !o.appearsLater && !o.removedByUpgrade && !o.ownedByOther && Harmless(o, a_rules) &&
			       (!a_mainOnly || a_options.mainCell.empty() || o.cell == a_options.mainCell);
		};
		bool mainOnly = std::ranges::any_of(plan.containers, [&](const DraftContainer& c) { return usable(c, true); });
		std::array<float, 3> centroid{};
		int                  centroidCount = 0;
		for (const auto& c : plan.containers) {
			if (!usable(c, mainOnly)) {
				continue;
			}
			const auto& o = a_objects[c.object];
			for (int k = 0; k < 3; ++k) {
				centroid[static_cast<std::size_t>(k)] += o.center[static_cast<std::size_t>(k)];
			}
			++centroidCount;
		}
		if (centroidCount > 0) {
			for (auto& v : centroid) {
				v /= static_cast<float>(centroidCount);
			}
		}
		float maxSpread = 1.0f;
		for (const auto& c : plan.containers) {
			if (usable(c, mainOnly)) {
				const auto& o = a_objects[c.object];
				maxSpread = std::max(maxSpread, std::hypot(o.center[0] - centroid[0], o.center[1] - centroid[1]));
			}
		}
		const float fullSize = std::max(a_rules.masterFullSize, 1.0f);
		for (std::size_t ci = 0; ci < plan.containers.size(); ++ci) {
			const auto& c = plan.containers[ci];
			if (!usable(c, mainOnly)) {
				continue;
			}
			const auto& o = a_objects[c.object];
			const auto& tokens = views[ci].combined;
			Candidate   cand{ c.object, 0.0f, std::max(o.size[0], 1.0f) * std::max(o.size[1], 1.0f) * std::max(o.size[2], 1.0f), AnyWords(tokens, a_rules.masterWords),
				  AnyWords(tokens, a_rules.masterAvoid), -1.0f, {} };
			for (const auto& d : a_objects) {
				if (d.kind == Kind::Door && d.cell == o.cell) {
					float dist = Distance(o, d);
					if (d.exteriorDoor) {
						dist *= 0.8f;  // the front door counts a little more than a cellar door
					}
					if (cand.entrance < 0.0f || dist < cand.entrance) {
						cand.entrance = dist;
						cand.door = d.leadsTo;
					}
				}
			}
			// Size up to a usual chest's: one oversized shelf or urn must not win on volume alone.
			cand.score = a_rules.masterSizeWeight * std::min(cand.volume / fullSize, 1.0f) + (cand.word ? a_rules.masterWordWeight : 0.0f) -
			             (cand.avoid ? a_rules.masterAvoidWeight : 0.0f);
			if (cand.entrance >= 0.0f) {
				cand.score += a_rules.masterEntranceWeight * std::max(0.0f, 1.0f - cand.entrance / std::max(a_rules.masterEntranceRange, 1.0f));
			}
			cand.score += a_rules.masterCentralWeight * (1.0f - std::min(std::hypot(o.center[0] - centroid[0], o.center[1] - centroid[1]) / maxSpread, 1.0f));
			// Named for what it holds ("Soul Gems", "Firewood", "Dragonpriest Masks"): the master only when nothing else is usable.
			cand.named = labelled[ci];
			if (cand.named) {
				cand.score -= a_rules.masterNamedPenalty;
			}
			candidates.push_back(std::move(cand));
		}
		std::ranges::stable_sort(candidates, [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
		if (!candidates.empty()) {
			const auto& best = candidates.front();
			const auto& o = a_objects[best.object];
			plan.master = best.object;
			std::vector<std::string> parts;
			if (best.word) {
				parts.emplace_back("named like a chest");
			} else if (best.avoid) {
				parts.emplace_back("though its name sounds small or odd (nothing more chest-like is usable)");
			}
			if (best.named) {
				parts.emplace_back(std::ranges::all_of(candidates, [](const Candidate& c) { return c.named; }) ?
				                       "though its name says what it holds (every usable container's does)" :
				                       "though its name says what it holds");
			}
			parts.push_back(std::format("{:.1f} x {:.1f} x {:.1f} m{}", o.size[0] / 70.0f, o.size[1] / 70.0f, o.size[2] / 70.0f, best.volume * 2.0f >= fullSize ? "" : ", small"));  // metres, as DraftMetres
			if (best.entrance >= 0.0f) {
				parts.push_back(std::format("{} from the door{}", DraftMetres(best.entrance), best.door.empty() ? std::string{} : " to " + best.door));
			}
			std::string why = std::format("{}: ", Title(o));
			for (std::size_t i = 0; i < parts.size(); ++i) {
				why += (i ? ", " : "") + parts[i];
			}
			why += std::format("; usable (one of {}): does not respawn, no script, nobody else's, there from the start", candidates.size());
			if (mainOnly && !a_options.mainCell.empty() && std::ranges::any_of(plan.containers, [&](const DraftContainer& c) { return usable(c, false) && !usable(c, true); })) {
				why += ", in the home's main cell";
			}
			plan.masterReason = std::move(why);
			for (std::size_t i = 1; i < candidates.size() && i < 4; ++i) {
				plan.masterRunnersUp.push_back(std::format("{} (score {:.2f} vs {:.2f})", Title(a_objects[candidates[i].object]), candidates[i].score, best.score));
			}
		} else {
			plan.masterReason = "No usable container: every one respawns, has a script, belongs to someone else or appears later. Pick one by hand.";
		}

		// ---- More evidence per container: stations, loose items ----
		// The master holds what has no place: its own evidence is dropped.
		for (std::size_t ci = 0; ci < plan.containers.size(); ++ci) {
			if (plan.master && *plan.master == plan.containers[ci].object) {
				evidence[ci].clear();
			}
		}
		// Stations: every non-container object matching a station rule.
		struct Station
		{
			std::size_t             object;
			const DraftStationRule* rule;
		};
		std::vector<Station> stations;
		for (std::size_t i = 0; i < a_objects.size(); ++i) {
			const auto& o = a_objects[i];
			if (o.kind == Kind::Container || o.kind == Kind::Item || o.kind == Kind::Door) {
				continue;
			}
			// A keyword says what a station is; words only when no rule's keyword matches.
			const DraftStationRule* match = nullptr;
			for (const auto& rule : a_rules.stations) {
				if (HasAny(o.keywords, rule.keywords)) {
					match = &rule;
					break;
				}
			}
			if (!match) {
				const auto tokens = ObjectTokens(o);
				for (const auto& rule : a_rules.stations) {
					if (AnyWords(tokens, rule.words)) {
						match = &rule;
						break;
					}
				}
			}
			if (match) {
				stations.push_back({ i, match });
			}
		}
		for (std::size_t ci = 0; ci < plan.containers.size(); ++ci) {
			const auto& c = plan.containers[ci];
			if (c.ignored || (plan.master && *plan.master == c.object)) {
				continue;
			}
			const auto& o = a_objects[c.object];
			auto&       ev = evidence[ci];
			// A container named for its contents only hears more about what its name says.
			const auto heard = [&](const std::string& a_pattern) { return known(a_pattern) && (!labelled[ci] || ev.contains(a_pattern)); };
			// Each station rule counts once per container (the nearest station of that kind).
			std::map<const DraftStationRule*, std::pair<float, std::size_t>> nearest;
			for (const auto& s : stations) {
				const auto& so = a_objects[s.object];
				if (so.cell != o.cell) {
					continue;
				}
				const float d = Distance(o, so);
				if (d > s.rule->radius) {
					continue;
				}
				auto it = nearest.find(s.rule);
				if (it == nearest.end() || d < it->second.first) {
					nearest[s.rule] = { d, s.object };
				}
			}
			for (const auto& [rule, hit] : nearest) {
				const float conf = rule->confidence * (1.0f - 0.5f * hit.first / std::max(rule->radius, 1.0f));
				for (const auto& cat : rule->categories) {
					if (heard(cat)) {
						Add(ev, cat, conf, std::format("{} from the {} ({})", DraftMetres(hit.first), rule->label, Title(a_objects[hit.second])), DraftSource::Station, hit.first);
					}
				}
			}
			// Loose items on or next to it.
			std::map<std::string, std::vector<std::string>> items;
			for (const auto& it : a_objects) {
				if (it.kind != Kind::Item || it.cell != o.cell || it.categories.empty()) {
					continue;
				}
				if (NearDistance(o, it, a_rules.nearbyRadius) > a_rules.nearbyRadius) {
					continue;
				}
				const auto& cat = it.categories.front();
				if (std::ranges::any_of(a_rules.nearbySkip, [&](const std::string& p) { return CategoryPatternMatches(p, cat); })) {
					continue;
				}
				const auto top = cat.substr(0, cat.find('.'));
				const auto pattern = std::ranges::find(a_rules.nearbyGroups, top) != a_rules.nearbyGroups.end() ? top + ".*" : cat;
				items[pattern].push_back(Title(it));
			}
			for (const auto& [pattern, names] : items) {
				if (!heard(pattern)) {
					continue;
				}
				const float conf = std::min(a_rules.nearbyConfidence + a_rules.nearbyPerItem * static_cast<float>(names.size() - 1), a_rules.nearbyMax);
				std::string list;
				for (std::size_t k = 0; k < names.size() && k < 3; ++k) {
					list += (list.empty() ? "" : ", ") + names[k];
				}
				if (names.size() > 3) {
					list += std::format(" and {} more", names.size() - 3);
				}
				Add(ev, pattern, conf, std::format("{} placed on or next to it ({})", names.size() == 1 ? std::string("1 item") : std::format("{} items", names.size()), list),
					DraftSource::Nearby);
			}
		}

		// ---- One container per category ----
		std::map<std::string, std::size_t> winner;  // pattern -> container index
		// The categories other than `a_pattern` container `a_ci` is sure of: on a tie, the one with fewer is more about it.
		const auto othersSure = [&](std::size_t a_ci, const std::string& a_pattern) {
			return std::ranges::count_if(evidence[a_ci], [&](const auto& a_entry) {
				return a_entry.first != a_pattern && a_entry.second.confidence + 1e-4f >= a_rules.acceptAt;
			});
		};
		for (std::size_t ci = 0; ci < evidence.size(); ++ci) {
			for (const auto& [pattern, e] : evidence[ci]) {
				const auto it = winner.find(pattern);
				if (it == winner.end()) {
					winner[pattern] = ci;
					continue;
				}
				const auto& other = evidence[it->second].at(pattern);
				// The stronger claim by name wins (named for exactly this category, then a display name that says it as part
				// of a group, then the rest), when it is sure enough; then confidence. Ties: the container's own name over
				// stations and items nearby, then the one sure of fewer other categories ("Notes and Letters" over
				// "Alchemy Recipes"), then the nearer station, then the earlier container.
				const bool claimWins = e.claim > other.claim && e.confidence + 1e-4f >= a_rules.acceptAt;
				const bool claimLoses = other.claim > e.claim && other.confidence + 1e-4f >= a_rules.acceptAt;
				const auto tieWins = [&] {
					const bool byName = e.source == DraftSource::Name, otherByName = other.source == DraftSource::Name;
					if (byName != otherByName) {
						return byName;
					}
					if (const auto mine = othersSure(ci, pattern), theirs = othersSure(it->second, pattern); mine != theirs) {
						return mine < theirs;
					}
					return e.stationDistance < other.stationDistance;
				};
				if (claimWins ||
					(!claimLoses && (e.confidence > other.confidence + 1e-4f || (std::abs(e.confidence - other.confidence) <= 1e-4f && tieWins())))) {
					it->second = ci;
				}
			}
		}
		for (std::size_t ci = 0; ci < evidence.size(); ++ci) {
			auto& c = plan.containers[ci];
			for (const auto& [pattern, e] : evidence[ci]) {
				DraftSuggestion s;
				s.pattern = pattern;
				s.confidence = e.confidence;
				s.reason = e.reason;
				s.source = e.source;
				const auto w = winner.at(pattern);
				if (w != ci) {
					s.lostTo = a_objects[plan.containers[w].object].id;
					s.reason += std::format("; {} has it with {}", Title(a_objects[plan.containers[w].object]), Percent(evidence[w].at(pattern).confidence));
				}
				s.accepted = w == ci && e.confidence + 1e-4f >= a_rules.acceptAt;
				c.suggestions.push_back(std::move(s));
			}
		}

		// ---- Fill: groups nobody was given, to the free container with the strongest hint ----
		auto acceptedAnywhere = [&](const std::string& a_pattern) {
			return std::ranges::any_of(plan.containers, [&](const DraftContainer& c) {
				return std::ranges::any_of(c.suggestions, [&](const DraftSuggestion& s) { return s.accepted && s.pattern == a_pattern; });
			});
		};
		auto isFree = [&](std::size_t ci) {
			const auto& c = plan.containers[ci];
			return !c.ignored && !(plan.master && *plan.master == c.object) &&
			       std::ranges::none_of(c.suggestions, [](const DraftSuggestion& s) { return s.accepted; });
		};
		// A container takes the group's categories it has evidence of its own for: a shield hanging next to a cabinet
		// is no reason to send it heavy armor.
		auto open = [&](const std::string& a_pattern) { return known(a_pattern) && !acceptedAnywhere(a_pattern); };
		std::vector<bool> groupDone(a_rules.fill.size(), false);
		for (;;) {
			float       best = 0.0f;
			std::size_t bestGroup = 0, bestContainer = 0;
			for (std::size_t g = 0; g < a_rules.fill.size(); ++g) {
				const auto& group = a_rules.fill[g];
				if (groupDone[g]) {
					continue;
				}
				for (std::size_t ci = 0; ci < plan.containers.size(); ++ci) {
					if (!isFree(ci)) {
						continue;
					}
					for (const auto& [pattern, e] : evidence[ci]) {
						if (std::ranges::find(group.categories, pattern) != group.categories.end() && open(pattern) && e.hinted && e.confidence > best + 1e-4f) {
							best = e.confidence;
							bestGroup = g;
							bestContainer = ci;
						}
					}
				}
			}
			if (best + 1e-4f < a_rules.fillAt) {
				break;
			}
			groupDone[bestGroup] = true;
			auto&       c = plan.containers[bestContainer];
			const auto& group = a_rules.fill[bestGroup];
			for (const auto& p : group.categories) {
				const auto e = evidence[bestContainer].find(p);
				if (e == evidence[bestContainer].end() || e->second.confidence + 1e-4f < a_rules.fillAt || !e->second.hinted || !open(p)) {
					continue;
				}
				auto s = std::ranges::find_if(c.suggestions, [&](const DraftSuggestion& x) { return x.pattern == p; });
				if (s == c.suggestions.end()) {
					c.suggestions.push_back({ p, e->second.confidence, {}, DraftSource::Fill, false, {} });
					s = std::prev(c.suggestions.end());
				}
				s->accepted = true;
				s->source = DraftSource::Fill;
				s->lostTo.clear();
				s->reason = std::format("best free place for {}: {}", group.label.empty() ? p : group.label, e->second.reason);
			}
		}

		for (auto& c : plan.containers) {
			std::ranges::stable_sort(c.suggestions, [](const DraftSuggestion& a, const DraftSuggestion& b) {
				return a.accepted != b.accepted ? a.accepted : a.confidence > b.confidence;
			});
			// Weak alternatives are noise: keep the accepted ones and the strongest few others.
			std::size_t kept = 0;
			std::erase_if(c.suggestions, [&](const DraftSuggestion& s) {
				if (s.accepted) {
					return false;
				}
				if (s.confidence + 1e-4f < a_rules.keepAt || kept >= a_rules.maxPerContainer) {
					return true;
				}
				++kept;
				return false;
			});
		}
		return plan;
	}

	std::vector<std::pair<std::string, std::string>> DraftCategoryNames(const CategorySet& a_categories)
	{
		std::vector<std::pair<std::string, std::string>> out;
		const auto                                       cats = a_categories.Categories();
		for (const auto& c : cats) {
			if (c.name.empty()) {
				continue;
			}
			const bool group = std::ranges::any_of(cats, [&](const CategorySet::Category& o) { return o.id.starts_with(c.id + "."); });
			out.emplace_back(group ? c.id + ".*" : c.id, c.name);
		}
		return out;
	}

	void ApplyDraftPlan(const DraftPlan& a_plan, std::span<const DraftObject> a_objects, Layout& a_layout)
	{
		if (a_plan.master && *a_plan.master < a_objects.size()) {
			if (auto ref = FormRef::Parse(a_objects[*a_plan.master].id)) {
				a_layout.master = *ref;
				std::erase_if(a_layout.containers, [&](const ContainerBinding& b) { return b.ref == *ref; });
			}
		}
		for (const auto& c : a_plan.containers) {
			if (c.object >= a_objects.size() || (a_plan.master && *a_plan.master == c.object)) {
				continue;
			}
			const auto& o = a_objects[c.object];
			const auto  ref = FormRef::Parse(o.id);
			if (!ref) {
				continue;
			}
			ContainerBinding b;
			b.ref = *ref;
			b.label = o.name.empty() ? o.editorId : o.name;
			b.ignored = c.ignored;
			b.note = c.note;
			if (!c.ignored) {
				for (const auto& s : c.suggestions) {
					if (s.accepted) {
						b.categories.push_back(s.pattern);
					}
				}
			}
			if (auto* existing = a_layout.FindContainer(*ref)) {
				*existing = std::move(b);
			} else {
				a_layout.containers.push_back(std::move(b));
			}
		}
	}
}
