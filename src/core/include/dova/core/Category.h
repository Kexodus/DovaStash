#pragma once

// Category engine (DESIGN §7): hierarchical ids, compiled predicates, wildcard binding patterns.
//
// File format: { "$schema": "dovastash-categories-1", "categories": [ { "id", "name", "priority",
// "match": { ...predicates... } } ] }. JSON comments are tolerated. Files are merged in filename order
// (case-insensitive); a later definition with the same id replaces the earlier one *in place* (it keeps
// the earlier definition's position for tie-breaking).
//
// Decisions:
// - Ids are lower-cased on load and must be dot-separated segments of [a-z0-9_-]. Invalid id -> error,
//   category skipped.
// - Predicates AND together; values inside one predicate OR together (DESIGN §7.1). A predicate that is
//   present with an empty list matches nothing (OR over nothing), except keywordsAll / keywordsNone
//   which are vacuously true. Absent predicates do not constrain.
// - `forms` and `formLists` form ONE membership predicate: item.form in forms ∪ resolved(formLists).
//   ANDing them would make using both at once useless. An unresolvable list contributes nothing
//   (warning); if nothing resolves, the predicate matches nothing (fail closed).
// - A category with no `match` (or `"match": {}`) is a pure grouping node (e.g. "weapons" carrying a
//   display name) and matches no item directly.
// - Unknown predicate names and unparseable enum values are errors and skip the whole category (a
//   typo must not silently widen a match). Unknown category-level keys are warnings ("description"
//   and keys starting with '_' or '$' are allowed silently). An invalid FormRef inside `forms` /
//   `formLists` is a warning and only that entry is dropped.
// - Keyword EditorIDs and plugin names compare case-insensitively.
// - Ordering: priority (desc), then specificity = number of id segments (desc), then definition order.
//   Classify() returns the first match; MatchAll() returns every match in that order.
// - Optional "icon" (DESIGN §7.1, §10.3): a SkyUI icon label or {"label","source","color"} (see
//   ParseIconRef in Icons.h). It is cosmetic, so a malformed icon is a warning and is dropped; the
//   category itself still loads.
//
// Patterns (used by container bindings, keep rules and retrieve):
//   "*"        any category
//   "a.b.*"    "a.b" itself and every descendant ("a.b.c", "a.b.c.d")
//   "a.b"      exactly "a.b"
// Any other use of '*' is invalid and matches nothing. Patterns are compared case-insensitively.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "dova/core/Diagnostics.h"
#include "dova/core/FormRef.h"
#include "dova/core/Icons.h"
#include "dova/core/Item.h"

namespace dova::core
{
	inline constexpr std::string_view kCategoriesSchema = "dovastash-categories-1";

	// Raw predicate set as written in JSON (after parsing/validation). nullopt = predicate absent.
	struct CategoryMatch
	{
		std::optional<std::vector<ItemType>>     formTypes;
		std::optional<std::vector<std::string>>  keywordsAny;
		std::optional<std::vector<std::string>>  keywordsAll;
		std::optional<std::vector<std::string>>  keywordsNone;
		std::optional<std::vector<FormRef>>      forms;
		std::optional<std::vector<FormRef>>      formLists;
		std::optional<std::vector<std::string>>  plugins;
		std::optional<std::vector<WeaponType>>   weaponTypes;
		std::optional<std::vector<ArmorType>>    armorTypes;
		std::optional<std::vector<BookTeaches>>  bookTeaches;
		std::optional<std::vector<AlchemyKind>>  alchemy;
		std::optional<std::vector<SoulGemState>> soulGem;
		std::optional<bool>                      enchanted;
		std::optional<std::vector<AmmoKind>>     ammo;

		[[nodiscard]] bool Empty() const noexcept;
	};

	struct CategoryDef
	{
		std::string   id;  // lower-cased, validated
		std::string   name;
		std::int32_t  priority{ 0 };
		CategoryMatch match;
		std::string   source;  // file it came from (for diagnostics/UI)

		std::optional<IconRef> icon;  // container icon for this category (SkyUI icon SWFs by default)
	};

	// Called once per formList reference at compile time; returns the list's member forms.
	// Return an empty vector when the list cannot be resolved (missing plugin, wrong form type).
	using FormListResolver = std::function<std::vector<FormRef>(const FormRef&)>;

	// --- Patterns / ids -------------------------------------------------------------------------

	[[nodiscard]] bool IsValidCategoryId(std::string_view a_id);
	[[nodiscard]] bool IsValidCategoryPattern(std::string_view a_pattern);
	[[nodiscard]] bool CategoryPatternMatches(std::string_view a_pattern, std::string_view a_categoryId);
	// Higher = more specific. Exact patterns beat any wildcard; deeper wildcards beat shallower ones;
	// "*" is 0. Returns -1 for an invalid pattern.
	[[nodiscard]] int CategoryPatternSpecificity(std::string_view a_pattern);
	[[nodiscard]] int CategoryDepth(std::string_view a_id);

	// --- Loading ---------------------------------------------------------------------------------

	// Parses one categories document. Categories with errors are skipped (and reported).
	[[nodiscard]] std::vector<CategoryDef> ParseCategoryDocument(const nlohmann::json& a_doc, std::string_view a_source, Diagnostics& a_diags);
	[[nodiscard]] std::vector<CategoryDef> ParseCategoryText(std::string_view a_text, std::string_view a_source, Diagnostics& a_diags);

	// One category entry as written in a categories file (inverse of ParseCategoryDocument; keys in
	// the order id, name, priority, icon, match). Comment keys and "description" are not kept.
	[[nodiscard]] nlohmann::ordered_json CategoryToJson(const CategoryDef& a_def);

	// Merges files in the given order; later ids replace earlier ones in place.
	[[nodiscard]] std::vector<CategoryDef> MergeCategoryFiles(std::span<const std::vector<CategoryDef>> a_files, Diagnostics& a_diags);

	// Reads every *.json directly inside `a_dir` (non-recursive), sorted by filename (case-insensitive),
	// and merges them. A missing directory yields an empty list and a warning.
	[[nodiscard]] std::vector<CategoryDef> LoadCategoryDirectory(const std::filesystem::path& a_dir, Diagnostics& a_diags);

	// --- Compiled set ------------------------------------------------------------------------------

	class CategorySet
	{
	public:
		struct Category
		{
			std::string  id;
			std::string  name;
			std::int32_t priority{ 0 };
			int          depth{ 1 };
			std::size_t  order{ 0 };  // definition order after merging
			std::string  source;

			std::optional<IconRef> icon;
		};

		CategorySet() = default;

		// `a_resolver` may be empty; formLists are then unresolvable (warning per reference).
		[[nodiscard]] static CategorySet Compile(std::vector<CategoryDef> a_defs, const FormListResolver& a_resolver, Diagnostics& a_diags);

		// Highest-ranked matching category id, or nullopt for "homeless".
		[[nodiscard]] std::optional<std::string> Classify(const ItemInfo& a_item) const;
		// Every matching category id, best first.
		[[nodiscard]] std::vector<std::string> MatchAll(const ItemInfo& a_item) const;
		// Does the item satisfy this specific category's predicates?
		[[nodiscard]] bool ItemMatches(std::string_view a_categoryId, const ItemInfo& a_item) const;

		[[nodiscard]] static bool Matches(std::string_view a_pattern, std::string_view a_categoryId)
		{
			return CategoryPatternMatches(a_pattern, a_categoryId);
		}

		// Categories in definition order (for UI trees).
		[[nodiscard]] std::span<const Category> Categories() const noexcept { return _info; }
		[[nodiscard]] const Category*           Find(std::string_view a_id) const;
		// True if at least one known category matches the pattern.
		[[nodiscard]] bool AnyMatches(std::string_view a_pattern) const;
		[[nodiscard]] std::size_t Size() const noexcept { return _info.size(); }

	private:
		struct Compiled
		{
			std::size_t                              info{ 0 };  // index into _info
			bool                                     groupOnly{ false };
			std::optional<std::vector<ItemType>>     formTypes;
			std::optional<std::vector<std::string>>  keywordsAny;  // lower-cased
			std::optional<std::vector<std::string>>  keywordsAll;  // lower-cased
			std::optional<std::vector<std::string>>  keywordsNone; // lower-cased
			std::optional<std::unordered_set<FormRef>> membership;  // forms ∪ resolved formLists
			std::optional<std::vector<std::string>>  plugins;      // lower-cased
			std::optional<std::vector<WeaponType>>   weaponTypes;
			std::optional<std::vector<ArmorType>>    armorTypes;
			std::optional<std::vector<BookTeaches>>  bookTeaches;
			std::optional<std::vector<AlchemyKind>>  alchemy;
			std::optional<std::vector<SoulGemState>> soulGem;
			std::optional<bool>                      enchanted;
			std::optional<std::vector<AmmoKind>>     ammo;
		};

		[[nodiscard]] static bool Evaluate(const Compiled& a_category, const ItemInfo& a_item, std::span<const std::string> a_lowerKeywords);

		std::vector<Category>                        _info;      // definition order
		std::vector<Compiled>                        _ranked;    // evaluation order
		std::unordered_map<std::string, std::size_t> _byId;      // id -> index into _info
		std::vector<std::size_t>                     _rankOfInfo;  // _info index -> _ranked index
	};
}
