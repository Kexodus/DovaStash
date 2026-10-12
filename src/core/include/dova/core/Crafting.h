#pragma once

// Materials at the workbench (DESIGN §8.5): pure logic. The plugin indexes every constructible-object
// recipe in the load order (RecipeIndex), asks which stored stacks to bring when the player uses a crafting
// station in an enabled home (Judge / PlanPull), records what really arrived (Ledger), and when the station
// is left asks what goes back (PlanReturn / ReturnMoves). Core never touches the game.
//
// Rules:
// - Relevant for a station: every component of every recipe whose bench keyword the station's furniture
//   carries (so modded materials and recipes that use odd items are covered by the data itself), plus every
//   ingredient at an alchemy lab and every filled soul gem at an enchanter. A station with none of these
//   gets nothing.
// - Never brought: quest, non-playable, nameless items, items that come back when stored, worn or favorited
//   copies, customized copies (tempered, renamed, enchanted by the player, poisoned), copies with extra data
//   the plugin does not recognise (Stack::key nullopt), forms without a load-order identity (dynamic), and
//   weapons, armor and ammo unless SupplyRequest::gear is set (breakdown recipes would pull a whole armory).
// - Stack identity: within one form, `key` tells stacks apart by the extra data the plugin allows to travel
//   (a soul, a stolen item's owner...). "" is the plain stack. Counting and returning go per (form, key), so
//   a stolen or soul-filled copy goes back as that same kind of copy.
// - Going back, per (form, key): the copies that are not new (now minus what was crafted, crafted copies
//   counted against the plain stack first), minus the player's own copies from before, at most what was
//   brought, never below zero. So what was used up is not "returned", the player keeps every copy they
//   had (used-up copies count against the brought ones first), and what was just crafted always stays with
//   the player, even when it is the same form as a brought material (ingots smelted at the smelter).

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "dova/core/FormRef.h"
#include "dova/core/Item.h"
#include "dova/core/SortPlanner.h"

namespace dova::core::crafting
{
	// What the station does besides its recipes.
	enum class Station : std::uint8_t
	{
		Recipes,    // forge, smelter, tanning rack, cooking pot, sharpening wheel, armor table, modded benches
		Alchemy,    // alchemy lab: every ingredient (no recipes)
		Enchanting  // arcane enchanter: every filled soul gem (no recipes)
	};

	// One constructible-object recipe (COBJ), as plain data.
	struct Recipe
	{
		std::string            bench;       // bench keyword EditorID (BNAM); case-insensitive
		std::vector<FormRef>   components;  // required items (form lists expanded by the plugin)
		std::optional<FormRef> created;     // created object (CNAM), informational
	};

	// Every recipe of the load order, grouped by bench keyword.
	class RecipeIndex
	{
	public:
		// Recipes without a bench keyword are counted and ignored; invalid component refs are ignored.
		void Add(const Recipe& a_recipe);

		// Every component of every recipe made at a bench carrying any of these keywords.
		[[nodiscard]] std::unordered_set<FormRef> MaterialsFor(std::span<const std::string> a_benchKeywords) const;
		// Recipes made at a bench with this keyword.
		[[nodiscard]] std::size_t RecipesAt(std::string_view a_benchKeyword) const;

		[[nodiscard]] std::size_t Recipes() const noexcept { return _recipes; }
		[[nodiscard]] std::size_t Benches() const noexcept { return _byBench.size(); }
		[[nodiscard]] std::size_t Materials() const;  // distinct components over all benches
		[[nodiscard]] std::size_t WithoutBench() const noexcept { return _withoutBench; }

	private:
		struct Bench
		{
			std::size_t                 recipes{ 0 };
			std::unordered_set<FormRef> materials;
		};
		std::unordered_map<std::string, Bench> _byBench;  // lower-case keyword
		std::size_t                            _recipes{ 0 };
		std::size_t                            _withoutBench{ 0 };
	};

	struct SupplyRequest
	{
		Station                     station{ Station::Recipes };
		std::unordered_set<FormRef> materials;  // RecipeIndex::MaterialsFor the station's keywords
		bool                        gear{ false };  // also weapons, armor and ammo used as materials

		// Nothing could ever be brought.
		[[nodiscard]] bool Empty() const noexcept { return station == Station::Recipes && materials.empty(); }
	};

	// One inventory stack with its identity within the form.
	struct Stack
	{
		ItemInfo                   item;
		std::optional<std::string> key;  // nullopt: extra data that never travels (see the header comment)
	};

	// A container's stacks.
	struct Stored
	{
		FormRef            container;
		std::vector<Stack> stacks;
	};

	enum class Verdict : std::uint8_t
	{
		Bring,
		NotMaterial,  // no recipe of this station uses it
		Gear,         // weapon, armor or ammo, and SupplyRequest::gear is off
		Unmovable,    // quest, non-playable, nameless, comes back when stored, empty
		Protected,    // worn or favorited
		Customized,   // tempered, renamed, enchanted by the player, poisoned, or unknown extra data
		Dynamic       // no load-order identity
	};
	[[nodiscard]] std::string_view VerdictName(Verdict a_verdict) noexcept;

	[[nodiscard]] Verdict Judge(const SupplyRequest& a_request, const Stack& a_stack);

	// A planned pull: the move (container -> player, whole stack) and the stack's key.
	struct Pull
	{
		Move        move;
		std::string key;
	};
	[[nodiscard]] std::vector<Pull> PlanPull(const SupplyRequest& a_request, std::span<const Stored> a_stored);

	// (form, key)
	struct StackKey
	{
		FormRef     form;
		std::string key;

		friend bool                 operator==(const StackKey&, const StackKey&) = default;
		friend std::strong_ordering operator<=>(const StackKey& a_lhs, const StackKey& a_rhs) noexcept
		{
			if (const auto c = a_lhs.form <=> a_rhs.form; c != 0) {
				return c;
			}
			return a_lhs.key <=> a_rhs.key;
		}
	};
	using Counts = std::map<StackKey, std::int32_t>;

	// Copies per (form, key) in `a_stacks` that could move: a key, valid form, SortPlanner::CanMove, not worn
	// or favorited, not customized. The player's counts before and after use this, so both sides agree.
	[[nodiscard]] Counts CountStacks(std::span<const Stack> a_stacks);

	// What one container gave.
	struct Pulled
	{
		FormRef      form;
		std::string  key;
		FormRef      from;
		std::int32_t count{ 0 };
	};

	// What was brought to the player, and how many of each the player had before.
	struct Ledger
	{
		std::vector<Pulled> pulled;  // one entry per (form, key, container), in pull order
		Counts              before;  // the player's copies before the pull, for every pulled (form, key)

		[[nodiscard]] bool         Empty() const noexcept { return pulled.empty(); }
		[[nodiscard]] std::int32_t Total() const noexcept;
		// Every (form, key) the ledger holds: what the plugin counts again at the end.
		[[nodiscard]] std::vector<StackKey> Keys() const;
	};

	// Builds the ledger from what really left each container: per planned (container, form, key), the
	// container's drop in copies (before minus after, CountStacks over every stack with that key), at most
	// what was planned. `a_playerBefore` is the player's CountStacks before the pull.
	[[nodiscard]] Ledger MakeLedger(std::span<const Pull> a_planned, std::span<const Stored> a_storedBefore,
		std::span<const Stored> a_storedAfter, const Counts& a_playerBefore);

	// One (form, key) at the end, for the log.
	struct ReturnLine
	{
		StackKey     stack;
		std::int32_t before{ 0 };
		std::int32_t pulled{ 0 };
		std::int32_t crafted{ 0 };  // crafted copies counted against this key
		std::int32_t now{ 0 };
		std::int32_t back{ 0 };
	};

	// Copies going back to one container.
	struct Back
	{
		FormRef      form;
		std::string  key;
		FormRef      to;
		std::int32_t count{ 0 };
	};

	struct ReturnPlan
	{
		std::vector<Back>       back;   // in ledger order
		std::vector<ReturnLine> lines;  // per (form, key)
		std::int32_t            pulled{ 0 };
		std::int32_t            returned{ 0 };
	};

	// `a_now`: the player's CountStacks now. `a_crafted`: copies per form that appeared in the player's
	// inventory from nowhere while the station was in use (crafted, or added by a script).
	[[nodiscard]] ReturnPlan PlanReturn(const Ledger& a_ledger, const Counts& a_now, const std::map<FormRef, std::int32_t>& a_crafted);

	// Moves of the player's stacks (player -> container) carrying out `a_back`: stacks of the same form and
	// key that could move (as CountStacks), plain or keyed alike, in inventory order. A return with too few
	// matching copies moves what there is.
	[[nodiscard]] std::vector<Move> ReturnMoves(std::span<const Back> a_back, std::span<const Stack> a_playerStacks);
}
