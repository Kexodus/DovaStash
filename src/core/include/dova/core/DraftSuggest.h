#pragma once

// Draft suggestions (docs/EDITOR.md §3.4): a starting template for a home nobody has written one for yet,
// worked out from what stands in its cells. Pure functions over plain facts the caller extracts (the editor
// from the plugins; the DLL could from the loaded cell), driven by a rules file, so the heuristics can be
// tuned without code and checked by unit tests.
//
// What is suggested:
// - Ignored containers, with a note: the first `ignore` rule that applies (respawns, goes away with a house
//   upgrade, owned by someone else, a scripted bookshelf...). They are never the master and get nothing.
// - The master: the usable container (not ignored, no script beyond the harmless ones, present from the
//   start) with the best score: a chest-like name, its size (up to a usual chest's: bigger is not better),
//   how close it is to the home's entrance and to the middle of the containers. A container whose name says
//   what it holds ("Soul Gems", "Dragonpriest Masks") is only the master when nothing else is usable. The
//   reason names what decided it.
// - Categories per container, each with a confidence (0..1) and a reason a person can check:
//     names    the container's display name ("Alchemy Satchel", "Weapon Chest"); its EditorID ("FoodBarrel01")
//              only when the display name is generic ("Barrel", "Chest"), at `editorIdFactor`;
//     stations crafting stations and displays near it (forge -> ores and ingots, alchemy lab -> ingredients
//              and potions, enchanter -> soul gems, cooking pot -> food, bookshelves -> books), weaker with
//              distance and across floors;
//     nearby   loose items placed on or next to it, by the categories the caller classified them into (a hint).
//   A container named for its contents (its display name has a word that is not generic, or a name rule says
//   so outright) gets only what its name says: stations and nearby items add to those categories, never others.
//   Evidence for the same category on the same container adds up (1 - product of (1 - c)).
//   Each category goes to one container: one named for exactly that category first, then one whose display
//   name says it as part of a group ("Alchemy Satchel" before a barrel whose EditorID says "Ingredient"), then
//   the highest confidence (ties: the container's own name over stations and items nearby, then the container sure of
//   fewer other categories, then the nearer station, then the earlier container). It is accepted when its confidence
//   reaches `acceptAt`; the others keep it as a rejected alternative, so the author sees why.
//   Then the fill pass: each group of `fill` (weapons, armor, clothes, alchemy...) that no container was given
//   goes to the free container with the strongest weaker evidence for it (a dresser for clothes, an end table
//   for books), greedily, strongest first; the container takes the group's categories it has evidence for
//   itself. Groups nothing hints at stay with the master.
//
// Text matching: a container's text is its EditorID split at case changes, digits and punctuation, plus its
// name split at spaces and punctuation, lower-cased ("PlayerHouseChestAlchemy01" -> player house chest
// alchemy). A rule word matches a token exactly, or with a plural "s"/"es"; "alchem*" matches any token
// starting with "alchem". A rule entry of several words ("soul gem") needs all of them. Keywords compare
// case-insensitively and whole.
//
// Rules file (JSON, comments allowed): { "$schema": "dovastash-draftrules-1", "acceptAt", "ignore": [...],
// "harmlessScripts": [...], "genericWords": [...], "editorIdFactor", "master": {...}, "names": [...],
// "stations": [...], "nearby": {...}, "fill": [...] }; see
// DefaultDraftRulesText() for every field. Unknown keys are warnings, malformed rules errors (skipped).

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dova/core/Category.h"
#include "dova/core/Diagnostics.h"
#include "dova/core/Layout.h"

namespace dova::core
{
	inline constexpr std::string_view kDraftRulesSchema = "dovastash-draftrules-1";

	// One object of the home, as the caller sees it.
	struct DraftObject
	{
		enum class Kind : std::uint8_t
		{
			Container,
			Item,     // a loose item lying in the cell (categories: what it matches)
			Display,  // rack, mannequin, plaque, case (add "display:rack" etc. to keywords for station rules)
			Door,     // a load door
			Other     // furniture, activators, statics: checked against the station rules
		};

		Kind                     kind{ Kind::Other };
		std::string              id;        // the reference ("Plugin.esm|0x..."): ApplyDraftPlan parses it
		std::string              cell;      // the cell it is in (distances only count within one cell)
		std::string              editorId;  // base object's EditorID
		std::string              name;      // base object's name
		std::vector<std::string> keywords;  // base object's keyword EditorIDs
		std::vector<std::string> scripts;   // base + reference scripts
		std::array<float, 3>     center{};  // world position of the middle of its bounds
		std::array<float, 3>     size{};    // extent of its bounds (scaled)

		// Containers
		bool respawns{ false };
		bool removedByUpgrade{ false };  // disappears when its enable parent appears (a house upgrade)
		bool appearsLater{ false };      // initially disabled or enabled by a parent: not there from the start
		bool ownedByOther{ false };      // owned by an NPC or a faction other than the player's

		// Doors
		bool exteriorDoor{ false };  // leads outside: the home's front door
		std::string leadsTo;         // label of the other side (reasons)

		// Items
		std::vector<std::string> categories;  // category ids it matches, best first
	};

	struct DraftNameRule
	{
		std::vector<std::vector<std::string>> words;  // any entry; every word of an entry
		std::vector<std::string>              notWords;  // none of these may be present
		std::vector<std::string>              categories;
		float                                 confidence{ 0.5f };
		std::string                           label;  // what the name says: "alchemy"
	};

	struct DraftStationRule
	{
		std::string                           label;  // "alchemy lab"
		std::vector<std::vector<std::string>> words;
		std::vector<std::string>              keywords;
		std::vector<std::string>              categories;
		float                                 confidence{ 0.6f };
		float                                 radius{ 600.0f };  // game units (about 70 per metre)
	};

	struct DraftIgnoreRule
	{
		enum class When : std::uint8_t
		{
			Respawns,
			Upgrade,
			Owned,
			Match  // editorIds / scripts / words
		};
		When                                  when{ When::Match };
		std::vector<std::string>              editorIds;
		std::vector<std::string>              scripts;
		std::vector<std::vector<std::string>> words;
		std::vector<std::string>              unlessScripts;
		std::string                           note;
	};

	struct DraftRules
	{
		float                         acceptAt{ 0.5f };
		float                         keepAt{ 0.25f };   // weaker suggestions are not listed at all
		float                         fillAt{ 0.3f };    // the fill pass gives nothing on weaker evidence
		std::size_t                   maxPerContainer{ 6 };  // listed suggestions per container (accepted ones always)
		float                         categoryNameConfidence{ 0.85f };  // named after a category of the set
		float                         itemNameConfidence{ 0.6f };       // named after an item
		float                         editorIdFactor{ 0.8f };           // a name rule matched only by the EditorID counts this much
		std::vector<std::string>      genericWords;  // words that say nothing about the contents ("large", "wooden"); see SuggestDraft
		float                         masterNamedPenalty{ 3.0f };  // a container whose name says what it holds is no master
		std::vector<DraftIgnoreRule>  ignore;
		std::vector<std::string>      harmlessScripts;  // scripts that do not stop a container from being the master
		std::vector<std::vector<std::string>> masterWords;  // chest-like names
		std::vector<std::vector<std::string>> masterAvoid;  // small or odd ones: sacks, satchels, urns
		float                         masterSizeWeight{ 1.0f };
		float                         masterFullSize{ 100000.0f };  // cubic units at which size stops counting (about a small chest)
		float                         masterWordWeight{ 1.5f };
		float                         masterAvoidWeight{ 1.0f };
		float                         masterEntranceWeight{ 1.0f };
		float                         masterCentralWeight{ 0.5f };
		float                         masterEntranceRange{ 1500.0f };
		std::vector<DraftNameRule>    names;
		std::vector<DraftStationRule> stations;
		float                         nearbyRadius{ 96.0f };
		float                         nearbyConfidence{ 0.25f };   // one item
		float                         nearbyPerItem{ 0.05f };      // each further item
		float                         nearbyMax{ 0.45f };
		std::vector<std::string>      nearbySkip;    // category patterns that say nothing (clutter)
		std::vector<std::string>      nearbyGroups;  // top-level ids suggested as "<id>.*" ("weapons" -> "weapons.*")

		// Groups of categories a home usually has a place for. A group nobody was given goes to the free container
		// (no accepted category yet) with the strongest evidence for it, even below `acceptAt`, but at least `fillAt`;
		// it takes the group's categories it has evidence of its own for, never the others on that evidence.
		struct FillGroup
		{
			std::string              label;
			std::vector<std::string> categories;
		};
		std::vector<FillGroup> fill;
	};

	[[nodiscard]] std::string_view DefaultDraftRulesText();
	[[nodiscard]] std::optional<DraftRules> ParseDraftRules(std::string_view a_text, std::string_view a_source, Diagnostics& a_diags);
	[[nodiscard]] const DraftRules& DefaultDraftRules();

	// Lower-cased tokens of an EditorID or a name (see the header comment).
	[[nodiscard]] std::vector<std::string> DraftTokens(std::string_view a_text);
	// Whether every word of `a_words` matches one of `a_tokens`.
	[[nodiscard]] bool DraftWordsMatch(std::span<const std::string> a_tokens, std::span<const std::string> a_words);

	enum class DraftSource : std::uint8_t
	{
		Name,
		Station,
		Nearby,
		Fill  // the best free container for a group nobody took (rules "fill"), on weaker evidence
	};
	[[nodiscard]] std::string_view DraftSourceName(DraftSource a_source) noexcept;

	struct DraftSuggestion
	{
		std::string pattern;
		float       confidence{ 0.0f };
		std::string reason;             // every piece of evidence, "; "-separated
		DraftSource source{ DraftSource::Name };  // the strongest piece
		bool        accepted{ false };
		std::string lostTo;             // rejected because this container (object id) has it with more confidence
	};

	struct DraftContainer
	{
		std::size_t                  object{ 0 };  // index into the objects
		bool                         ignored{ false };
		std::string                  note;         // why it is ignored
		std::vector<DraftSuggestion> suggestions;  // best first
	};

	struct DraftPlan
	{
		std::optional<std::size_t>  master;  // object index
		std::string                 masterReason;
		std::vector<std::string>    masterRunnersUp;  // "Wardrobe (score 2.1)"
		std::vector<DraftContainer> containers;       // every container object, in input order (the master included, without suggestions)

		[[nodiscard]] const DraftContainer* Find(std::size_t a_object) const;
		[[nodiscard]] DraftContainer*       Find(std::size_t a_object);
	};

	struct DraftOptions
	{
		std::string mainCell;  // the master comes from this cell when it has a usable container (empty: any)
		// Patterns the loaded categories know (empty: every pattern is kept). Unknown ones are dropped.
		std::function<bool(std::string_view)> knownPattern;
		// The loaded categories' names, as (pattern, display name): a container named after a category ("Soul
		// gems", "Ores & ingots": either side of an "&" or "and") gets it. Groups come as "<id>.*".
		std::vector<std::pair<std::string, std::string>> categoryNames;
		// A container named exactly like an item of the load order ("Ash Hopper Jelly", "Flour") gets the item's
		// category: the caller looks the name up (case-insensitive; plural names may drop their "s").
		std::function<std::optional<std::string>(std::string_view a_name)> itemCategoryByName;
	};

	// (pattern, name) for every category of the set: leaves by id, groups (ids with children) as "<id>.*".
	[[nodiscard]] std::vector<std::pair<std::string, std::string>> DraftCategoryNames(const CategorySet& a_categories);

	[[nodiscard]] DraftPlan SuggestDraft(std::span<const DraftObject> a_objects, const DraftRules& a_rules, const DraftOptions& a_options = {});

	// Writes the plan into `a_layout`: master, and one container entry per other container object (label: its
	// name, else its EditorID; categories: the accepted suggestions; ignored with the note). Entries the layout
	// already has for those references are replaced; everything else is kept. Ids that are not form references
	// are skipped.
	void ApplyDraftPlan(const DraftPlan& a_plan, std::span<const DraftObject> a_objects, Layout& a_layout);

	// Units -> "3.2 m" (about 70 units per metre), for reasons.
	[[nodiscard]] std::string DraftMetres(float a_units);
}
