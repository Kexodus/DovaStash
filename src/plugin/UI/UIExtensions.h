#pragma once

// UIExtensions bridge (DESIGN §10.3): opens UIExtensions' list and text-entry menus from the DLL, with no ESP and no
// Papyrus scripts of ours.
//
// Skin-agnostic: we only use UIExtensions' own list-menu contract, the exact interface its
// UIListMenu.psc drives, so anything that replaces listmenu.swf (any skin, or none) and still works
// with UIExtensions works with Dova Stash:
//   - UI.OpenCustomMenu("listmenu")                  (SKSE's Papyrus UI script)
//   - mod event UIListMenu_LoadMenu                  -> we invoke _root.listMenu.LM_AddTreeEntries and
//                                                       _root.listMenu.LM_SetSortingEnabled
//   - mod events UIListMenu_SelectItem / _CloseMenu  -> the choice / a cancel
// Entries are plain text: no colours, fonts, HTML or anything a particular skin adds. Verified: the
// stock UIExtensions listmenu.swf and the Nordic UI replacement both expose exactly these names.
// UIExtensions' own Papyrus quests only listen while *they* open a menu, so they stay out of it.
// With [UI] bCenterMenus the movie's _root stays transparent while the menu is filled and laid out,
// then moves so the menu sits mid-screen and is shown there.
//
// Optional extras on top of the contract, each skipped (the list stays plain text, and choosing an entry
// closes it) when the menu's ActionScript does not have what it needs. They use the list classes that
// listmenu.swf itself is built from (identical in the stock SWF and in Nordic UI's): ListMenu
// (_root.listMenu: itemList, onItemPressed, LM_AddTreeEntries) and SkyUI's ScrollingList (itemList:
// entryList, UpdateList, getClipByIndex, doSetSelectedIndex, entryHeight; each row clip's textField):
//   - icons: a clip per visible row, left of its text, showing the entry's icon frame from the player's
//     SkyUI icon SWFs (Icons/IconService). Rows are re-dressed whenever the list redraws them
//     (UpdateList: scrolling, refills), so pooled row clips always show their current entry's icon.
//   - stay-open entries: choosing one keeps the menu open and updates the entries in place (text and
//     icons; selection and scroll position stay), instead of closing and reopening the list.
//   - the Dova Stash logo (Interface/DovaStash/logo.dds) above the list, tinted with the list's own
//     text colour. The one piece of art of ours in game (DESIGN §1 principle 6).
// Both hooks are plain members set on the menu's own objects (onItemPressed on _root.listMenu,
// UpdateList on its itemList); each calls the SWF's original function for everything we do not handle.

namespace dova::ui::uiext
{
	// UIExtensions.esp is loaded (its SWFs ship with it). Never true on VR (untested there).
	[[nodiscard]] bool IsInstalled();

	// kDataLoaded: the mod-event sink.
	void RegisterSinks();

	// Index of the chosen entry, -1 when the menu was cancelled, or kNotShown when it never came up (the SWF did
	// not load, OpenCustomMenu failed, another mod's custom menu stayed open). Always invoked once, on the main
	// thread (SKSE task queue).
	using ChoiceCallback = std::function<void(std::int32_t a_index)>;
	inline constexpr std::int32_t kNotShown = -2;

	struct ListEntry
	{
		std::string   text;
		std::int32_t  parent{ -1 };  // index of the parent entry in the same vector (tree), -1 = top level
		bool          hasChildren{ false };
		core::IconRef icon;              // shown left of the text when the menu allows it; empty = none
		bool          stayOpen{ false };  // choosing it calls ListOptions::onStay with the menu kept open
	};

	// A stay-open entry was chosen (index) and the menu is still open: do what it does and return the
	// list's entries now. Same count: texts and icons are updated in place; another count refills the
	// list in place. Main thread.
	using StayCallback = std::function<std::vector<ListEntry>(std::int32_t a_index)>;

	struct ListOptions
	{
		bool         logo{ false };  // the Dova Stash logo above the list
		std::int32_t select{ -1 };   // entry selected (and scrolled into view) when the list opens
		StayCallback onStay;         // without it, stay-open entries close the menu like any other
	};

	// Opens the list (once any custom menu that is still closing has gone). Selecting an entry without
	// children closes the menu and reports its index, except a stay-open entry while the menu supports
	// it (see above): that one goes to `a_options.onStay` instead. Callers handle both. Returns false
	// (and never calls back) when UIExtensions is not installed or a Dova Stash list is already pending;
	// otherwise the callback always runs, with kNotShown if the list could not be shown.
	bool OpenList(std::vector<ListEntry> a_entries, ChoiceCallback a_callback, ListOptions a_options = {});
	// A Dova Stash list or text box is requested and not closed yet (also before its menu has opened). Any thread.
	[[nodiscard]] bool IsBusy();

	// AutoTest: the Dova Stash list that is pending (requested or on screen): its entry texts, its request serial
	// (0 = none) and whether its menu has loaded.
	struct ListState
	{
		std::uint64_t            serial{ 0 };
		bool                     shown{ false };
		std::vector<std::string> entries;
	};
	[[nodiscard]] ListState PendingList();
	// AutoTest: chooses entry `a_index` of the list on screen the way the menu would (a stay-open entry updates
	// the list in place; any other closes it and reports the choice). False when no list is on screen.
	bool ChooseForTest(std::int32_t a_index);

	// Entered text, or nullopt when cancelled. Main thread.
	using TextCallback = std::function<void(std::optional<std::string> a_text)>;

	// UITextEntryMenu (UITextEntryMenu.psc contract: "textentrymenu", _root.textEntry.setTextEntryMenuText,
	// UITextEntryMenu_LoadMenu / _TextChanged / _CloseMenu), pre-filled with `a_initial`. Same rules as
	// OpenList.
	bool OpenTextEntry(std::string a_initial, TextCallback a_callback);
}
