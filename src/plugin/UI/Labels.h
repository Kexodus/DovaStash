#pragma once

// Player-facing names for categories, patterns and container roles, shared by every UI surface
// (message boxes, UIExtensions list menu, QuickLoot info bar, activate prompt). Any thread.

#include "Home/HomeRegistry.h"

namespace dova::ui
{
	// The confirmation under "Remove <container> from Dova Stash?" (every UI surface says the same). What to do
	// instead to keep it out for good is the panel's Remove... tooltip.
	inline constexpr auto             kRemoveExplained = "Its categories and learned items are forgotten. No items are moved.";
	inline constexpr std::string_view kConfirmRemove = "Remove {} from Dova Stash?\n{}";  // name, kRemoveExplained
	// A container's questions, the same in the panel and both hotkey menus ("{}": the container's name).
	inline constexpr auto             kMakeMasterEntry = "Make master...";
	inline constexpr std::string_view kConfirmMakeMaster = "Make {} the master?\nThe current master becomes an ordinary container. No items are moved.";
	inline constexpr std::string_view kConfirmMakeMasterForgets = "Make {} the master?\nIts categories and learned items are forgotten; the current master becomes an ordinary container. No items are moved.";
	inline constexpr auto             kClearCategoriesEntry = "Clear categories...";
	inline constexpr std::string_view kConfirmClearCategories = "Clear the categories of {}?\nIts learned items stay. No items are moved.";
	// Home backups outside Data (DESIGN §6.6). The setup prompt's first entry ("{}": the copy's date) and the
	// confirmations, the same on every UI surface.
	inline constexpr std::string_view kRestoreLastSetup = "Restore your last setup ({})";
	inline constexpr std::string_view kConfirmRestore = "Restore the setup saved {}? This home's current setup is backed up first (.bak); no items are moved.";
	// Category suggestion after a few learns (DESIGN §8.6): the message box after the container closes and the
	// panel's container page ask the same ("{}": the category's name, the container's label).
	inline constexpr std::string_view kSuggestCategory = "Dova Stash: send all {} to {}?";
	// Multi-cell homes (DESIGN §6.7), the same on every UI surface ("{}": the home's name; the area's name).
	inline constexpr std::string_view kAddArea = "Add this area to {}";
	inline constexpr std::string_view kConfirmAddArea = "Dova Stash: add this area to {}?\nIts containers and crafting stations then work with the rest of the home. No items are moved.";
	inline constexpr std::string_view kRenameArea = "Rename {}...";  // the area's name
	inline constexpr std::string_view kRemoveArea = "Remove this area from {}...";
	inline constexpr std::string_view kConfirmRemoveArea = "Remove {} from {}?\nIts containers stay in the setup but are no longer used. No items are moved.";
	// Hotkey menu entries both in-world UIs show (UIExtensions lists and message boxes) word the same.
	inline constexpr std::string_view kMakeMasterSetup = "Make {} the master";  // the setup prompt; the container's name
	inline constexpr auto             kCategoriesEntry = "Categories...";       // a container's category picker
	inline constexpr std::string_view kLearnedEntry = "Learned items ({})...";  // how many
	inline constexpr std::string_view kConfirmForgetAll = "Forget {} for {}?\nThey go by category again. No items are moved.";  // "3 learned items", container
	inline constexpr auto             kOpenPanel = "Open panel...";  // the Menu Framework panel, only when it is loaded
	// The questions before Reset home... and Use template (the hotkey menus and the panel ask the same; "{}": the
	// home's name, the template's name). Use template asks only when the setup has changes of the player's own
	// (core::HasOwnChanges) or the template may not fit (kMayNotFit is then added).
	inline constexpr std::string_view kConfirmResetHome = "Reset {}?\nIts setup is backed up (.bak). No items are moved.";
	inline constexpr std::string_view kConfirmUseTemplate = "Use the template \"{}\"?\nIt replaces your changes: the master, sell chest and categories come from it; learned items, keep rules and ignored containers stay. Your setup is backed up first. No items are moved.";
	// The same question when the setup is already based on that template (Re-apply...).
	inline constexpr std::string_view kConfirmReapplyTemplate = "Re-apply the template \"{}\"?\nIt undoes your changes to the master, sell chest and categories; learned items, keep rules and ignored containers stay. Your setup is backed up first. No items are moved.";
	inline constexpr auto             kMayNotFit = "may not fit";  // a template the auto-enable guard doubts (DESIGN §9.1)
	inline constexpr auto             kMayNotFitNote = "\nIt may not fit this home.";
	// The home menu's title when the master is in another area, and a container nothing is assigned to (both UIs).
	inline constexpr auto kMasterNotHere = "master not here";
	inline constexpr auto kNotManaged = "not managed yet";
	// Wants a question before Use template (see kConfirmUseTemplate): the setup has changes of the player's own or
	// its best template may not fit. Main thread.
	[[nodiscard]] bool AskBeforeTemplate(home::Home& a_home);
	// "Use template \"X\"" or "Re-apply template \"X\"", with " (may not fit)" when the guard doubts it (Advanced...).
	[[nodiscard]] std::string TemplateEntry(home::Home& a_home);
	// kConfirmUseTemplate (kConfirmReapplyTemplate when the setup is based on it) for the home's best template, plus
	// kMayNotFitNote when it may not fit.
	[[nodiscard]] std::string TemplateQuestion(home::Home& a_home);
	// kConfirmForgetAll for `a_count` learned items of `a_name` ("1 learned item", "3 learned items").
	[[nodiscard]] std::string ForgetLearnedQuestion(std::size_t a_count, std::string_view a_name);
	// The learned-items menu's first entry: "Forget 1 learned item...", "Forget all 3 learned items...".
	[[nodiscard]] std::string ForgetAllEntry(std::size_t a_count);
	// A Browse containers entry, the same in both hotkey UIs: "Chest - Weapons (12 items)", or "Chest (12 items)"
	// when `a_what` (its role) is empty or repeats the name. Main thread (counts the inventory).
	[[nodiscard]] std::string BrowseEntryText(RE::TESObjectREFR* a_ref, std::string_view a_what);
	// kConfirmMakeMaster, or kConfirmMakeMasterForgets when the container has a binding or learned items to lose.
	[[nodiscard]] std::string MakeMasterQuestion(std::string_view a_name, bool a_forgetsSetup);

	// One entry of a hotkey menu (UIExtensions list or message box).
	struct MenuEntry
	{
		std::string           text;
		std::function<void()> action;
	};
	// What a container's hotkey menu does that differs between the two UIs.
	struct ContainerMenuHooks
	{
		std::string           categoriesText;  // the first entry ("Categories...", with a summary in the lists)
		std::function<void()> openCategories;
		std::function<void()> rename;       // null: no Rename... entry (no text box)
		std::function<void()> openLearned;  // the container's learned items
		std::function<void()> openPanel;    // null: the panel is not loaded
		// Asks `question`; Yes runs the action, No goes back to this container's menu.
		std::function<void(std::string, std::function<void()>)> confirm;
		std::function<void()> back;  // this container's menu again (No on Sell now / Buy back)
	};
	// A container's hotkey menu, the same order in both UIs (DESIGN §10.3), everyday entries first and the ones that
	// ask last: Categories..., Open, the sell entries, Rename... / Restore original name, Learned items (N)...,
	// Clear categories..., Ignore / Stop ignoring, Make sell chest... / Stop selling from it, Remove...,
	// Make master..., Open panel.... Optional entries show only when they apply. Main thread.
	[[nodiscard]] std::vector<MenuEntry> ContainerMenuEntries(home::Home& a_home, RE::TESObjectREFR* a_container, const ContainerMenuHooks& a_hooks);
	// A template's author for the setup prompt ("unknown" when the file names none).
	[[nodiscard]] inline std::string AuthorOf(const core::Layout& a_layout) { return a_layout.author.empty() ? std::string("unknown") : a_layout.author; }

	// The sell chest (DESIGN §8.8), the same on every UI surface.
	inline constexpr auto             kMakeSellChest = "Make sell chest...";
	inline constexpr auto             kNotSellChest = "Stop selling from it";
	inline constexpr std::string_view kConfirmSellChest = "Make {} the sell chest?\nWhat you put in it is sold for gold when you choose Sell now and confirm the total. Stolen, improved, quest and kept items stay. Nothing is sold now.";
	inline constexpr std::string_view kTimedSellNote = "\nTimed selling is on: what stays in it {} hours is sold by itself.";  // hours
	inline constexpr std::string_view kSellNow = "Sell now ({} gold)...";  // gold
	inline constexpr std::string_view kConfirmSell = "Sell {} for {} gold?\n{}";  // "14 items", gold, what goes (+ what stays)
	inline constexpr std::string_view kBuyBack = "Buy back ({} gold)...";  // gold
	inline constexpr std::string_view kConfirmBuyBack = "Buy back {} for {} gold?\n{}";  // "14 items", gold, what

	// The sell chest's entries in the hotkey menus (main thread: works out the sale): "Sell now (862 gold)..." when
	// there is something to sell, and "Buy back (862 gold)..." after a sale in this home. `a_chest`: that sell chest;
	// nullopt: every one of the home (the master menu). No on either question runs `a_onBack` (the menu it came from).
	[[nodiscard]] std::vector<std::pair<std::string, std::function<void()>>> SellEntries(const home::Home& a_home, const std::optional<core::FormRef>& a_chest,
		const std::function<void()>& a_onBack = {});
	// kConfirmSellChest for `a_name`, plus the timed-selling note when that is on.
	[[nodiscard]] std::string SellChestQuestion(std::string_view a_name);

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
