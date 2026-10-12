#pragma once

// Player-facing names for categories, patterns and container roles, shared by every UI surface
// (message boxes, UIExtensions list menu, QuickLoot info bar, activate prompt). Any thread.

#include "Home/HomeRegistry.h"

namespace dova::ui
{
	// The confirmation under "Remove <container> from Dova Stash?" (every UI surface says the same).
	inline constexpr auto kRemoveExplained = "Its categories and learned items are forgotten; what's inside stays. Put items in by hand later and it is learned again; to keep it out for good, use Ignore.";

	// Category display name ("Swords"), falling back to the capitalized id.
	[[nodiscard]] std::string CategoryName(std::string_view a_id);
	// "Everything" for "*", "All Weapons" for "weapons.*", else the category name.
	[[nodiscard]] std::string PatternLabel(std::string_view a_pattern);
	// Short form for one-line summaries: "Weapons" for "weapons.*".
	[[nodiscard]] std::string PatternShortLabel(std::string_view a_pattern);

	// Top-level category ids in definition order.
	[[nodiscard]] std::vector<std::string> RootCategories();
	// Category ids below `a_root` (excluding the root itself), definition order.
	[[nodiscard]] std::vector<std::string> ChildCategories(const std::string& a_root);
	[[nodiscard]] std::string              RootOf(std::string_view a_id);

	// Sum of `a_counts` (primary category id -> count) over ids matching `a_pattern`.
	[[nodiscard]] std::int32_t CountMatching(const std::map<std::string, std::int32_t>& a_counts, std::string_view a_pattern);

	// " (2 learned)" when `a_learned` (LearnedByCategory) has items matching `a_pattern`, else "". The
	// category picker shows where the container's learned items belong, though they are not bound.
	[[nodiscard]] std::string LearnedSuffix(const std::map<std::string, std::int32_t>& a_learned, std::string_view a_pattern);
	// The learned items of `a_container` per category in the current layout of `a_cellId` (empty if none).
	// Main thread only (live layout, classifies forms): for menus, not per-frame display.
	[[nodiscard]] std::map<std::string, std::int32_t> LearnedOf(RE::FormID a_cellId, const core::FormRef& a_container);

	// Exactly this pattern is bound to the container.
	[[nodiscard]] bool HasPattern(const core::ContainerBinding* a_binding, std::string_view a_pattern);
	// `a_id` is routed to this container by any of its patterns.
	[[nodiscard]] bool Covers(const core::ContainerBinding* a_binding, std::string_view a_id);

	// What a container is to Dova Stash, for display. Built from the published home copy
	// (HomeRegistry::Display), so it is safe and cheap from any thread, every frame.
	struct ContainerRole
	{
		enum class Kind : std::uint8_t
		{
			None,       // not in an enabled home, or not a container there
			Unmanaged,  // in an enabled home, not part of the layout
			Master,
			Bound,      // managed (categories and/or learned items)
			Ignored
		};
		Kind                                     kind{ Kind::None };
		std::shared_ptr<const home::DisplayHome> home;               // keeps `binding` alive
		const core::ContainerBinding*            binding{ nullptr };  // into home->layout
		std::size_t                              learned{ 0 };        // learned destinations pointing here
		// Their categories, most items first (DisplayHome::learnedCategories; null if none).
		const std::vector<std::pair<std::string, std::int32_t>>* learnedCategories{ nullptr };
	};
	[[nodiscard]] ContainerRole DescribeContainer(RE::TESObjectREFR* a_ref);

	// One-line summary of what goes in a container: "Weapons, Armor +2", "Master", "" (nothing to say).
	// A container with learned items only names their categories: "Books, learned".
	[[nodiscard]] std::string RoleSummary(const ContainerRole& a_role, std::size_t a_maxPatterns = 2);
}
