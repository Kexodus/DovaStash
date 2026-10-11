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

namespace dova::ui::uiext
{
	// UIExtensions.esp is loaded (its SWFs ship with it). Never true on VR (untested there).
	[[nodiscard]] bool IsInstalled();

	// kDataLoaded: the mod-event sink.
	void RegisterSinks();

	// Index of the chosen entry, or -1 when the menu was cancelled. Always invoked once, on the main
	// thread (SKSE task queue).
	using ChoiceCallback = std::function<void(std::int32_t a_index)>;

	struct ListEntry
	{
		std::string  text;
		std::int32_t parent{ -1 };  // index of the parent entry in the same vector (tree), -1 = top level
		bool         hasChildren{ false };
	};

	// Opens the list (once any custom menu that is still closing has gone). Selecting an entry without
	// children closes the menu and reports its index. Returns false (and never calls back) when
	// UIExtensions is not installed or a Dova Stash list is already pending; otherwise the callback
	// always runs, with -1 if the list could not be shown.
	bool OpenList(std::vector<ListEntry> a_entries, ChoiceCallback a_callback);

	// Entered text, or nullopt when cancelled. Main thread.
	using TextCallback = std::function<void(std::optional<std::string> a_text)>;

	// UITextEntryMenu (UITextEntryMenu.psc contract: "textentrymenu", _root.textEntry.setTextEntryMenuText,
	// UITextEntryMenu_LoadMenu / _TextChanged / _CloseMenu), pre-filled with `a_initial`. Same rules as
	// OpenList.
	bool OpenTextEntry(std::string a_initial, TextCallback a_callback);
}
