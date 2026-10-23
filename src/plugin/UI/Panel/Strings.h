#pragma once

// User-visible text of the Menu Framework panel, kept in one place so it can be translated later.
// Strings with "{}" are std::format patterns (positional arguments in the order documented).
//
// Conventions for every Menu Framework page (new text and controls follow them; helpers in Widgets.h):
// - Words: the game's US English, in players' words ("setup", not "layout"; "name", not "label"). Terse:
//   no "Dova Stash:" prefixes inside our own window, nothing the control already says.
// - Labels: Sentence case, no colon, no trailing period. A checkbox says what happens when it is ticked
//   ("Show icons above containers") or names its feature ("Materials at the workbench"); a combo, slider or
//   text box names its value ("Icon size"), with units, "off" and "never" in the value, not in the label.
//   Section headers are one to three words.
// - Buttons: a verb ("Deposit", "Rename", "Fill now"). Only actions that are hard to undo ask first: their
//   label ends in "...", and the button turns into a yellow question with Yes / No (ConfirmButton):
//   "<Verb> <what>? <What happens>. No items are moved." Buttons on a row of a list, table or tree are small;
//   page and section buttons are normal. In a row of buttons: everyday actions first, the ones that ask last.
// - Tooltips: full sentences ending with a period, at most two short ones. A control's tooltip starts with
//   what it does, as a verb ("Moves...", "Shows..."); a status's tooltip says what it means. Settings add the
//   INI key they write. A section's own explanation is the tooltip of its header.
// - Help: a page or tab starts with at most one dimmed paragraph (Hint) saying what it is for.
// - States: an empty or waiting state is one dimmed sentence ("Nothing learned yet."); an empty table cell is
//   "-"; status words in tables are lower case ("used", "not managed yet"), any detail in parentheses after.
//   Colours: red error or unusable, yellow warning / unsaved / question, green fine or in use.
// - Saving: settings apply the moment they change (sliders on release) and are written to DovaStash.ini; no
//   Save button, no restart. Lists edited as a draft (keep rules) end with Save, Revert and a yellow
//   "Unsaved changes". A text box with a button saves with the button or Enter.
// - Order: a page goes from everyday actions to setup, with the actions that reset or remove things last.
//   What an action did shows in the status line at the top of the page, under its header (red when it failed).

#include "UI/Labels.h"

namespace dova::ui::panel::text
{
	// Window / section. Four pages, in menu order: the current home's two, then the global ones.
	// - This home: the stash window's page (tabs Actions, Containers, Learned, Keep rules, Template; the Template
	//   tab ends with All templates, the catalogue of every loaded template; outside a home, that catalogue).
	// - Displays: the home's racks, mannequins, plaques and cases; a mannequin's set picker searches every armor
	//   set in the load order.
	// - Settings: every INI setting, then the default keep rules.
	// - About: version, the editor's location, Diagnostics (load problems), then For bug reports (collapsed).
	inline constexpr auto kWindowTitle = "Dova Stash - This home##DovaStash";
	inline constexpr auto kSection = "Dova Stash";
	inline constexpr auto kItemHome = "This home";  // same page as hotkey > Advanced... > Open panel...
	inline constexpr auto kItemDisplays = "Displays";
	inline constexpr auto kItemSettings = "Settings";
	inline constexpr auto kItemAbout = "About";

	// Common
	inline constexpr auto             kLoading = "Loading...";
	inline constexpr auto             kYes = "Yes";
	inline constexpr auto             kNo = "No";
	inline constexpr auto             kClose = "Close";
	inline constexpr auto             kCloseKey = "(Esc)";
	inline constexpr auto             kRemove = "Remove";
	inline constexpr auto             kRemoveAsk = "Remove...";
	inline constexpr auto             kAdd = "Add";
	inline constexpr auto             kSave = "Save";
	inline constexpr auto             kRevert = "Revert";
	inline constexpr auto             kCancel = "Cancel";
	inline constexpr auto             kRename = "Rename";
	inline constexpr auto             kRefresh = "Refresh";
	inline constexpr auto             kUnsaved = "Unsaved changes";
	inline constexpr auto             kNone = "-";
	inline constexpr std::string_view kNameVersion = "{} v{}";  // name, version
	inline constexpr std::string_view kByAuthor = "{} by {}";   // "<name> v<version>", author
	inline constexpr std::string_view kNotInHome = "You are not in a player home ({}).";  // reason
	inline constexpr auto             kNeedsSetup = "Set this home up first (This home page).";
	inline constexpr auto             kViaWider = "(via a wider choice)";
	inline constexpr auto             kAuto = "Auto";
	inline constexpr auto             kAutoTip = "Wears your most complete matching set.";
	inline constexpr std::string_view kUnavailable = "unavailable: {}";  // reason, in a table cell or after a name
	inline constexpr std::string_view kUnavailableLine = "Unavailable: {}";  // reason, on a line of its own

	// Header
	inline constexpr std::string_view kHeaderHome = "{}";            // home name
	inline constexpr std::string_view kHeaderLayout = "Setup: {}";   // "<name> v<version> by <author>"
	inline constexpr std::string_view kHeaderBasedOn = "Based on template: {}";
	inline constexpr auto             kHeaderCustom = "Custom setup (not from a template)";
	inline constexpr std::string_view kHeaderMaster = "Master: {}";
	inline constexpr auto             kHeaderNoMaster = "Master: none (items without a destination stay in your inventory)";

	// Setup (home without a setup)
	inline constexpr auto             kSetupIntro = "This home is not set up yet.";
	inline constexpr std::string_view kSetupMakeMaster = "Make {} the master";
	inline constexpr auto             kSetupMakeMasterTip = "Sets this home up with it as the master: items without a destination go there. No items are moved.";
	inline constexpr std::string_view kSetupLookAt = "Look at a container and press {} to make it the master.";
	inline constexpr auto             kSetupHotkeyFallback = "the hotkey";
	inline constexpr auto             kSetupTemplates = "Templates";  // its tooltip (kTemplatesTip) says which home
	inline constexpr auto             kSetupBroken = "This home's setup file is broken and was left untouched. Fix it by hand (see DovaStash.log), restore a saved setup below, or reset the home (the file is renamed to .bak).";
	inline constexpr auto             kSetupBrokenNoBackup = "This home's setup file is broken and was left untouched. Fix it by hand (see DovaStash.log) or reset the home (the file is renamed to .bak).";

	// Tabs
	inline constexpr auto kTabActions = "Actions";
	inline constexpr auto kTabContainers = "Containers";
	inline constexpr auto kTabLearned = "Learned";
	inline constexpr auto kTabKeep = "Keep rules";
	inline constexpr auto kTabTemplate = "Template";

	// Actions tab
	inline constexpr auto             kSortHeader = "Sort";
	inline constexpr auto             kDeposit = "Deposit";
	inline constexpr auto             kDepositTip = "Moves your items into their containers. Keep rules decide what stays with you.";
	inline constexpr auto             kRestock = "Restock";
	inline constexpr auto             kRestockTip = "Takes from the containers what you are short of for every keep quota marked Restock, never more than the quota. The most valuable copies come first.";
	inline constexpr auto             kRestockNoneTip = "Needs a keep quota marked Restock (Keep rules, here or under Settings).";
	inline constexpr auto             kSortAll = "Sort all";
	inline constexpr auto             kSortAllTip = "Moves every item in the master and in containers with categories to where it belongs. Containers without categories are never emptied.";
	inline constexpr auto             kRetrieveHeader = "Retrieve";
	inline constexpr auto             kRefreshCounts = "Refresh counts";
	inline constexpr auto             kRefreshCountsTip = "Counts the stored items again, after you moved some by hand.";
	inline constexpr auto             kCounting = "Counting items...";
	inline constexpr auto             kNothingStored = "Nothing to retrieve from the managed containers.";
	inline constexpr std::string_view kRetrieveEverything = "Everything ({})";  // count
	inline constexpr std::string_view kNodeCount = "{} ({})";  // name, count
	inline constexpr auto             kRetrieve = "Retrieve";
	inline constexpr auto             kRetrieveTip = "Moves these items from the containers to you.";
	inline constexpr auto             kActionsTemplates = "Templates";
	inline constexpr auto             kOpenTemplateTab = "Use or save a template";
	inline constexpr auto             kOpenTemplateTabTip = "Opens the Template tab: the templates for this home, and saving this setup as one.";
	// Templates: the Template tab and the setup page draw the same list.
	inline constexpr auto             kNoTemplate = "No template fits this home.";
	inline constexpr auto             kTemplatesTip = "Every template made for this home whose plugins are loaded, the one offered first on top, then the ones you removed from this home.";
	inline constexpr auto             kTemplateBest = "(offered first)";
	inline constexpr auto             kTemplateBestTip = "The one the hotkey offers, and a first visit sets up.";
	inline constexpr auto             kTemplateApplied = "(in use)";
	inline constexpr auto             kTemplateRemoved = "(removed)";
	inline constexpr auto             kTemplateRemovedTip = "Removed from this home: never offered or set up here. The file stays, and other homes still offer it.";
	inline constexpr auto             kRemoveTemplateTip = "Stops offering this template in this home. The file stays; Add back undoes it.";
	inline constexpr std::string_view kConfirmRemoveTemplate = "Remove {} from this home? It is no longer offered here; Add back undoes it. No items are moved.";
	inline constexpr auto             kAddBackTemplate = "Add back";
	inline constexpr auto             kAddBackTemplateTip = "Offers this template in this home again.";
	inline constexpr auto             kTemplateDraft = "(draft)";
	inline constexpr auto             kTemplateDraftTip = "Drafted by the Dova Stash Editor for many homes at once. A template someone made comes first.";
	inline constexpr auto             kUseTemplate = "Use";
	inline constexpr auto             kUseTemplateAsk = "Use...";
	inline constexpr auto             kReapplyTemplate = "Re-apply";
	inline constexpr auto             kReapplyTemplateAsk = "Re-apply...";
	inline constexpr auto             kUseTemplateTip = "Switches this home's setup to this template: the master, sell chest and categories come from it; learned items, keep rules and ignored containers stay. Nothing is moved.";
	inline constexpr auto             kReapplyTemplateTip = "Puts this home's setup back to this template, undoing your changes to the master, sell chest and categories. Nothing is moved.";
	// Asked only when the setup has changes of the player's own (HomeSnapshot::ownChanges).
	inline constexpr std::string_view kConfirmTemplate = ui::kConfirmUseTemplate;  // the hotkey menus ask the same
	inline constexpr std::string_view kConfirmReapplyTemplate = ui::kConfirmReapplyTemplate;  // Re-apply...
	// Sell chest (DESIGN §8.8; the hotkey menus and the buttons use the ui:: strings in UI/Labels.h)
	inline constexpr auto             kSellHeader = "Sell chest";
	inline constexpr auto             kSellTip = "Sells what is in the sell chest at a merchant's price, only when you choose Sell now and confirm (or by timed selling). Stolen, improved, quest and kept items stay.";
	inline constexpr auto             kSellNotHere = "No sell chest can be reached from here.";
	inline constexpr auto             kSellNothing = "Nothing to sell.";
	inline constexpr std::string_view kSellQuote = "{} for {} gold: {}";  // "14 items", gold, what
	inline constexpr std::string_view kBuyBackInfo = "Last sale: {} for {} gold ({}).";  // "14 items", gold, what
	inline constexpr std::string_view kItemCount = "{} item";   // count (singular)
	inline constexpr std::string_view kItemsCount = "{} items";  // count (plural)
	inline constexpr auto             kSalesHeader = "Sales this session";
	inline constexpr std::string_view kSaleLine = "{}  {}: {} for {} gold{}";  // time, chests, "14 items", gold, tags
	inline constexpr auto             kSaleTimed = " (timed)";
	inline constexpr auto             kSaleBoughtBack = " (bought back)";
	inline constexpr std::string_view kSaleItem = "{} x{}: {} gold";  // name, count, gold
	inline constexpr auto             kSellTag = "[Sell] ";
	inline constexpr auto             kSellChestInfo = "This is the sell chest. Items reach it like any container; only selling takes them out (Actions > Sell chest).";
	inline constexpr auto             kNotSellChestTip = "Makes it an ordinary container again. Nothing is sold or moved.";
	inline constexpr auto             kResetHeader = "Reset";
	inline constexpr auto             kResetHome = "Reset home...";
	inline constexpr std::string_view kConfirmReset = ui::kConfirmResetHome;  // the hotkey menus ask the same
	inline constexpr auto             kBackupsHeader = "Saved setups";
	inline constexpr auto             kBackupsTip = "Copies of this home's setup in Documents/My Games/Skyrim Special Edition/SKSE/DovaStash/Backups (VR: Skyrim VR), one per recent session. Cleaning MO2's overwrite folder does not touch them.";
	inline constexpr std::string_view kRestoreLast = "Restore last setup ({})...";  // date
	inline constexpr std::string_view kRestoreOlder = "Restore the setup from {}...";  // date
	inline constexpr std::string_view kRestoreOlderNow = "Restore the setup from {}";  // date; the setup page (no setup to lose: no question)
	inline constexpr std::string_view kOlderBackups = "Older copies ({})";  // count
	inline constexpr auto             kNoBackups = "No saved copy of this home's setup yet.";
	inline constexpr std::string_view kBackupSame = "The latest copy ({}) is this setup as it is now.";  // date
	// Multi-cell homes (DESIGN §6.7)
	inline constexpr auto             kAreasHeader = "Areas";
	inline constexpr auto             kAreasTip = "The cells this home spans. Containers in another area are used while it is loaded or when they are persistent; displays fill only in the area you are in.";
	inline constexpr auto             kAreaMain = "(main)";
	inline constexpr auto             kAreaHere = "(you are here)";
	inline constexpr std::string_view kAreaNameHint = "name, e.g. Grounds (optional: {})";  // the cell's own name
	inline constexpr auto             kAreaRenameTip = "The name prompts and this window show for the area. Empty: the cell's own name.";
	inline constexpr auto             kAreaRenameButtonTip = "Opens a box to name this area.";
	inline constexpr auto             kAreaRemoveTip = "Takes this area out of the home: its containers are no longer used from here. No items are moved.";
	inline constexpr auto             kAddAreaTip = "Makes this cell part of that home: its containers and crafting stations work with the rest. No items are moved.";
	inline constexpr std::string_view kAreaExcluded = "{} has its own setup; Reset it there to make it part of this home.";  // area name
	inline constexpr std::string_view kHereArea = "in {}";  // area name, after the home's name
	inline constexpr std::string_view kInArea = "In {}";  // area name
	inline constexpr auto             kRemoteTip = "In another area of this home, used from here: it is persistent (or its area is loaded).";

	// Containers tab
	inline constexpr auto             kColContainer = "Container";
	inline constexpr auto             kColCategories = "Categories";
	inline constexpr auto             kColLearned = "Learned";
	inline constexpr auto             kColStatus = "Status";
	inline constexpr auto             kMasterTag = "[Master] ";
	inline constexpr auto             kHomeless = "(items without a destination)";
	inline constexpr auto             kStatusAimed = "(looking at it)";
	inline constexpr auto             kStatusAimedTip = "The container you are looking at.";
	inline constexpr auto             kDispAimedTip = "The display you are looking at.";
	inline constexpr auto             kStatusIgnored = "ignored";
	inline constexpr auto             kNotManaged = "not managed yet";
	inline constexpr auto             kManaged = "managed";
	inline constexpr std::string_view kLearnedOnly = "{}, learned";  // category names of the learned items
	inline constexpr auto             kSelectContainer = "Select a container above, or look at one in the world and press the hotkey.";
	inline constexpr auto             kMasterInfo = "This is the master container. Items without a destination stay here, and closing it sorts its contents into the other containers.";
	inline constexpr auto             kNameTip = "The name prompts, menus and QuickLoot show. Empty: the game's name.";
	inline constexpr auto             kOpenContainer = "Open";
	inline constexpr auto             kOpenContainerTip = "Opens this container's own menu. This window closes first.";
	inline constexpr auto             kIgnore = "Ignore";
	inline constexpr auto             kIgnoreTip = "Keeps it out of sorting and learning for good. Its categories are kept.";
	inline constexpr auto             kStopIgnoring = "Stop ignoring";
	inline constexpr auto             kStopIgnoringTip = "Lets it receive items and learn again, with its categories.";
	inline constexpr auto             kRenameTip = "Saves the name. Empty: the game's name.";
	// The container's questions are the hotkey menus' (UI/Labels.h): ui::kConfirmRemove, ui::MakeMasterQuestion,
	// ui::kConfirmClearCategories, ui::ForgetLearnedQuestion.
	inline constexpr auto             kMakeMaster = ui::kMakeMasterEntry;
	inline constexpr auto             kMakeMasterTip = "Makes it the master: items without a destination go here.";
	inline constexpr auto             kMakeSellChestTip = "Makes it the sell chest: what you put in is sold when you choose Sell now.";
	inline constexpr auto             kClearCategories = ui::kClearCategoriesEntry;
	inline constexpr auto             kClearCategoriesTip = "Unticks every category of this container. Its learned items stay.";
	inline constexpr std::string_view kForgetLearned = "Forget learned ({})...";
	inline constexpr auto             kForgetOne = "Forget";
	inline constexpr auto             kForgetOneTip = "Forgets where this item goes: it goes by category again. No items are moved.";
	inline constexpr auto             kRemovePatternTip = "Unticks this pattern: its items no longer go here. No items are moved.";
	inline constexpr auto             kForgetLearnedTip = "Forgets the items learned for this container: they go by category again. No items are moved.";
	inline constexpr auto             kRemoveContainerTip = "Takes this container out of the setup; what is inside stays. Put items in by hand and it is learned again; to keep it out for good, use Ignore.";
	inline constexpr std::string_view kExplicitForms = "{} (edit them in this home's file under Layouts/).";  // kExplicitForm1 / kExplicitFormN
	inline constexpr std::string_view kExplicitForm1 = "{} fixed item binding";
	inline constexpr std::string_view kExplicitFormN = "{} fixed item bindings";
	inline constexpr auto             kCategoriesHeader = "Categories sent here";
	inline constexpr std::string_view kLearnedInfo = "Learned here: {}. Only those exact items come here; tick a category to send all of it.";
	inline constexpr auto             kIgnoredInfo = "Ignored containers receive nothing and teach nothing, and Dova Stash never adds them back. Their categories are kept for when you stop ignoring them. To forget a container instead, use Remove.";
	inline constexpr auto             kEverything = "Everything (*)";
	inline constexpr std::string_view kAllOf = "All {} ({}.*)";  // name, id
	inline constexpr auto             kOtherPatterns = "Other patterns (unknown categories)";

	// Category suggestion on a container's page (DESIGN §8.6; the HUD asks the same, ui::kSuggestCategory)
	inline constexpr std::string_view kSuggestion = "Send all {} to {}? ({} learned items)";  // category, container, count
	inline constexpr auto             kSuggestionNo = "No, don't ask again";
	inline constexpr auto             kSuggestionYesTip = "Sends the whole category to this container. No items are moved.";
	inline constexpr auto             kSuggestionNoTip = "Stops asking about this category here; Ask again undoes it.";
	inline constexpr std::string_view kDeclinedInfo = "Not asked again for: {}";  // category names
	inline constexpr auto             kAskAgain = "Ask again";
	inline constexpr auto             kAskAgainTip = "Lets Dova Stash suggest these categories for this container again.";

	// Learned tab
	inline constexpr auto             kLearnedEmpty = "Nothing learned yet. Put an item into a managed container by hand to teach Dova Stash where it belongs.";
	inline constexpr auto             kLearnedIntro = "Items you put away by hand go back to the same container. An enchanted or otherwise variant item counts as the item it is made from.";
	inline constexpr auto             kColItem = "Item";
	inline constexpr auto             kNotManagedSuffix = " (not managed)";
	inline constexpr std::string_view kLearnedUnusedInfo = "{} of these point at a container that is no longer used, so they do nothing.";
	inline constexpr std::string_view kForgetUnused = "Forget those ({})...";
	inline constexpr std::string_view kConfirmForgetUnused = "Forget {} learned for containers no longer used? They go by category again. No items are moved.";  // "3 items"
	inline constexpr auto             kStaleMaster = "the master";
	inline constexpr auto             kStaleNotInSetup = "not in the setup";
	inline constexpr auto             kStaleIgnored = "ignored";
	inline constexpr auto             kStaleNotLoaded = "not loaded (plugin missing?)";

	// Keep rules (this home's tab, and the defaults on the Settings page)
	inline constexpr auto             kKeepIntro = "Keep rules decide what Deposit leaves in your inventory. Quest items and non-playable items are always kept. A quota marked Restock also tops you up to its number from the containers.";
	inline constexpr auto             kKeepNoBlock = "This home uses the default keep rules only. Saving gives it its own.";
	inline constexpr auto             kKeepInherit = "Also apply the default keep rules";
	inline constexpr auto             kKeepInheritTip = "Keeps what the defaults keep, plus this home's rules. Off: only this home's rules.";
	inline constexpr auto             kKeepAlways = "Always keep";
	inline constexpr auto             kKeepNever = "Never keep";
	inline constexpr auto             kKeepQuotas = "Quotas";
	inline constexpr auto             kKeepQuotasTip = "Keeps up to N items of a category on you; Deposit takes the rest.";
	inline constexpr auto             kKeepAddHint = "or type a selector";
	inline constexpr auto             kKeepAddTip = "Types a rule, a category id or Plugin.esm|0x000ABC. Enter adds it.";
	inline constexpr auto             kKeepPickCategory = "Add category";
	inline constexpr auto             kKeepPickRule = "Add rule";
	inline constexpr auto             kKeepPickRuleTip = "Adds a rule: what you wear, what you favorited, or quest items.";
	inline constexpr auto             kKeepPickCategoryTip = "Adds every item of a category.";
	inline constexpr auto             kKeepRuleEquipped = "Equipped";
	inline constexpr auto             kKeepRuleFavorited = "Favorited";
	inline constexpr auto             kKeepRuleQuest = "Quest items";
	inline constexpr std::string_view kKeepNotLoaded = "{} (not loaded)";  // form ref of an item whose plugin is missing
	inline constexpr auto             kKeepPickItem = "Add item";
	inline constexpr auto             kKeepItemSearchHint = "Search";
	inline constexpr auto             kKeepPickItemTip = "Adds an item you carry: that exact item, whatever its category.";
	inline constexpr auto             kKeepNoItems = "Nothing in your inventory.";
	inline constexpr auto             kKeepNoItemMatch = "No item matches.";
	inline constexpr auto             kKeepNotSelector = "Not a keep selector.";
	inline constexpr auto             kRemoveKeepEntryTip = "Removes it from this list. Save to keep the change.";
	inline constexpr auto             kColCategory = "Category";
	inline constexpr auto             kColQuota = "Quota";
	inline constexpr auto             kColRestock = "Restock";
	inline constexpr auto             kRestockColTip = "Tops you up to this many from the home's containers (Restock, and after Deposit when that setting is on). Off: the quota only limits what Deposit leaves on you.";
	inline constexpr auto             kRestockMark = " (restock)";
	inline constexpr auto             kColOther = "Other selectors";
	inline constexpr auto             kAddQuota = "Add quota";
	inline constexpr auto             kAddQuotaTip = "Adds a quota: keep up to this many of a category.";
	inline constexpr auto             kRemoveQuotaTip = "Removes this quota from the list.";
	inline constexpr auto             kQuotaNeedsCategory = "Pick a category for every quota (rules without any selector are dropped).";
	inline constexpr auto             kKeepUseGlobal = "Use the defaults only...";
	inline constexpr auto             kConfirmKeepGlobal = "Remove this home's keep rules and use only the defaults? No items are moved.";
	inline constexpr auto             kKeepGlobalHeader = "Default keep rules";
	inline constexpr auto             kKeepGlobalWhere = "Edit them under Dova Stash > Settings.";
	inline constexpr auto             kKeepGlobalIntro = "What Deposit leaves in your inventory in every home. A home's own keep rules add to these.";
	inline constexpr auto             kNoCategory = "(none)";
	inline constexpr std::string_view kKeywordsAny = "keywords: {}";
	inline constexpr std::string_view kFormsCount = "{} forms";
	inline constexpr std::string_view kFormCount = "{} form";
	inline constexpr std::string_view kQuotaLine = "{} x {}{}";  // quota, category, kRestockMark or ""

	// Template tab: Templates (above), Save as template, then All templates (below, collapsed)
	inline constexpr auto             kTemplateTabIntro = "Use a template for this home, or save this setup as one to bring back after a reset or to share.";
	inline constexpr auto             kSaveTemplateHeader = "Save as template";
	inline constexpr auto             kSaveTemplateTip = "Saves this home's setup as a template, to bring back after a reset or to share. The hotkey's Save as template... does the same.";
	inline constexpr auto             kExportLearned = "Include where you put things by hand";
	inline constexpr auto             kExportLearnedTip = "Saves the items you put away by hand, so they go to the same containers for anyone using the template. Untick it to share a template without your habits.";
	inline constexpr auto             kExportName = "Name";
	inline constexpr auto             kExportAuthor = "Author";
	inline constexpr auto             kExportAuthorHint = "your character's name";
	inline constexpr auto             kExportVersion = "Version";
	inline constexpr auto             kExportVersionHint = "automatic";
	inline constexpr std::string_view kExportFile = "Saved as Data/SKSE/Plugins/DovaStash/Templates/User/{}.json (under MO2: in overwrite). This home offers it right away.";
	inline constexpr auto             kExport = "Save";
	inline constexpr auto             kExporting = "Saving...";
	inline constexpr auto             kExportWarnings = "Template warnings";

	// Settings page. Section headers, then each control's label and tooltip (Help() adds the INI key).
	inline constexpr auto kSettingsNote = "Changes apply at once and are saved to Data/SKSE/Plugins/DovaStash.ini. Hover an option for details.";
	inline constexpr auto kSetHotkeyHeader = "Hotkey";
	inline constexpr auto kSetHotkey = "Keyboard / mouse";
	inline constexpr auto kSetHotkeyTip = "Click, then press a key or mouse button to make it the hotkey. The hotkey opens Dova Stash: at a container, its menu; at nothing, the home's menu.";
	inline constexpr auto kSetGamepad = "Gamepad";
	inline constexpr auto kSetGamepadTip = "Click, then press a controller button to make it the hotkey. Off by default: every button already has a game action.";
	inline constexpr auto kSetKeyOff = "Off";
	// Press-to-bind (UI/Panel/KeyCapture.h): the hotkey's button while it waits for a key, in yellow.
	inline constexpr auto kSetKeyWaitKey = "Press a key... (click to cancel)";
	inline constexpr auto kSetKeyWaitButton = "Press a button... (click to cancel)";
	inline constexpr auto kSetKeyWaitTip = "Waiting for the new hotkey. Click to cancel; Esc closes the window and B leaves the page, binding nothing.";
	inline constexpr auto kSetKeyClear = "Clear";
	inline constexpr auto kSetKeyClearTip = "Turns the gamepad hotkey off.";
	inline constexpr std::string_view kSetKeyClash = "{} is also {}.";  // key, the game control it also has ("E is also Activate.")
	inline constexpr auto kSetMenusHeader = "Menus";
	inline constexpr auto kSetMode = "What the hotkey opens";
	inline constexpr auto kSetModeTip = "Chooses the hotkey's menus. Auto: UIExtensions' lists when it is installed (any skin), else the game's message boxes.";
	inline constexpr auto kSetModeAuto = "Auto";
	inline constexpr auto kSetModeMessageBoxes = "Message boxes";
	inline constexpr auto kSetModePanel = "SKSE Menu Framework window";
	inline constexpr auto kSetModeLists = "UIExtensions lists";
	inline constexpr auto kSetCenter = "Open the lists in the middle of the screen";
	inline constexpr auto kSetCenterTip = "Moves UIExtensions' lists and the rename box to the middle of the screen, whatever your skin does.";
	inline constexpr auto kSetNoUIExtensions = "UIExtensions is not installed.";
	inline constexpr auto kSetNoUIExtensionsVR = "UIExtensions lists are not used on VR.";
	inline constexpr auto kSetSortingHeader = "Sorting";
	inline constexpr auto kSetSortOnArrival = "Sort items as they go into the master";
	inline constexpr auto kSetSortOnArrivalTip = "Moves anything you put into the master to its container about half a second later, even with the menu still open.";
	inline constexpr auto kSetSortOnClose = "Sort the master when it closes";
	inline constexpr auto kSetSortOnCloseTip = "Sorts whatever is left in the master when you close its menu.";
	inline constexpr auto kSetFillDisplays = "Put gear on the home's displays";
	inline constexpr auto kSetRestock = "Restock after Deposit";
	inline constexpr auto kSetRestockTip = "Tops you up from the home's containers to every keep quota marked Restock after a Deposit, never more. Without such a quota nothing happens.";
	inline constexpr auto kSetFillDisplaysTip = "Makes Deposit and Sort all fill the displays you use (racks, mannequins, bookshelves, plaques) with your best gear and books, one of each. Nothing is taken off a display.";
	inline constexpr auto kSetLearningHeader = "Learning";
	inline constexpr auto kSetLearn = "Learn where you put things by hand";
	inline constexpr auto kSetLearnTip = "Sends an item you put into a container yourself to that container from then on. A variant (enchanted, for example) counts as the item it is made from.";
	inline constexpr auto kSetSuggest = "Suggest a category after";
	inline constexpr auto kSetSuggestFormat = "%d learned items";
	inline constexpr auto kSetSuggestNever = "never";
	inline constexpr auto kSetSuggestTip = "Asks once whether to send the whole category to a container when this many of its items were learned there. No is remembered; the container's page can ask again.";
	inline constexpr auto kSetUndo = "Undo time for a learn";
	inline constexpr auto kSetUndoFormat = "%d s";
	inline constexpr auto kSetUndoOff = "off";
	inline constexpr auto kSetUndoTip = "Lets the hotkey take a learn back for this long after \"... now goes to ...\".";
	inline constexpr auto kSetCraftingHeader = "Crafting";
	inline constexpr auto             kSetCrafting = "Materials at the workbench";
	inline constexpr auto             kSetCraftingTip = "Lets a crafting station in your home use what your containers hold. Enchanters always bring your filled soul gems.";
	// Under the combo: what its chosen value does (Auto, Off, Move, Craft from storage).
	inline constexpr auto             kSetCraftAutoHint = "Craft from storage, or Off when another crafting-from-storage mod is installed.";
	inline constexpr auto             kSetCraftOffHint = "Stations use only what you carry.";
	inline constexpr auto             kSetCraftMoveHint = "The materials come to you and the rest goes back when you get up; what you made stays with you.";
	inline constexpr auto             kSetCraftStorageHint = "The menu counts your containers and crafting takes from them.";
	inline constexpr auto             kSetCraftAuto = "Auto";
	inline constexpr std::string_view kSetCraftAutoNow = "Auto (now: {})";  // the mode Auto resolves to
	inline constexpr auto             kSetCraftOff = "Off";
	inline constexpr auto             kSetCraftMove = "Move: bring them, put the rest back";
	inline constexpr auto             kSetCraftStorage = "Craft from storage: nothing moves";
	inline constexpr std::string_view kSetCraftNow = "Now {}: {}.";  // mode, why
	inline constexpr auto             kSetCraftingGear = "Weapons and armor used as materials too";
	inline constexpr auto             kSetCraftingGearTip = "Counts stored gear that recipes use up too, such as breakdown recipes (with a breakdown mod, every piece you store). Tempered or enchanted copies never count.";
	inline constexpr auto kSetTemplatesHeader = "Templates";
	inline constexpr auto kSetAutoEnable = "Set up homes from a template on first visit";
	inline constexpr auto kSetAutoEnableTip = "Sets up a home with a template as soon as you walk in, instead of asking. Not while a saved setup of yours can be restored there.";
	inline constexpr auto kSetSellHeader = "Sell chest";
	inline constexpr auto kSetSellPrice = "Price";
	inline constexpr auto kSetSellPriceFormat = "%d%% of the game's";
	inline constexpr auto kSetSellPriceTip = "Sets what a sale pays, in percent of what a merchant would pay you (your Speech, Haggling and Fortify Barter).";
	inline constexpr auto kSetAutoSell = "Sell by itself after";
	inline constexpr auto kSetAutoSellFormat = "%d game hours";
	inline constexpr auto kSetAutoSellOff = "off";
	inline constexpr auto kSetAutoSellTip = "Sells what has been in the sell chest this long when you come home, close a container or wake up there, with one HUD line. Off: only Sell now sells, after you confirm.";
	inline constexpr auto kSetQuickLootHeader = "QuickLoot IE";
	inline constexpr auto kSetQuickLoot = "Use QuickLoot IE";
	inline constexpr auto kSetQuickLootTip = "Shows Dova Stash in QuickLoot IE. Off: QuickLoot looks as it does without Dova Stash.";
	inline constexpr auto kSetItemTags = "Show where each item goes";
	inline constexpr auto kSetItemTagsTip = "Shows items that belong elsewhere as \"Iron Sword  >  Weapon Rack\" in the list.";
	inline constexpr auto kSetQuickLootIcons = "Show category icons instead of names";
	inline constexpr auto kSetQuickLootIconsTip = "Shows one icon per category the container holds (your SkyUI/I4 icons) bottom-right, instead of their names.";
	inline constexpr auto kSetNoQuickLoot = "QuickLoot IE is not installed.";
	inline constexpr auto kSetWorldHeader = "In the world";
	inline constexpr auto kSetPrompt = "Show a container's role in the activate prompt";
	inline constexpr auto kSetPromptTip = "Shows \"Chest (Weapons)\" when you look at a container. Nothing is written to your save.";
	inline constexpr auto kSetOverlay = "Show icons above containers";
	inline constexpr auto kSetOverlayTip = "Shows category icons above the containers around you, in your homes.";
	inline constexpr auto kSetLineOfSight = "Hide icons behind walls";
	inline constexpr auto kSetLineOfSightTip = "Shows icons only for containers you can see.";
	inline constexpr auto kSetRange = "Range";
	inline constexpr auto kSetRangeFormat = "%.0f units";
	inline constexpr auto kSetRangeTip = "Shows icons up to this far away (about 70 units per meter).";
	inline constexpr auto kSetMaxIcons = "Icon limit";
	inline constexpr auto kSetMaxIconsTip = "Shows only this many icons, the nearest ones.";
	inline constexpr auto kSetIconSize = "Icon size";
	inline constexpr auto kSetIconSizeFormat = "%.0f px";
	inline constexpr auto kSetIconSizeTip = "Sets the size up close; icons shrink with distance.";
	inline constexpr auto kSetNoOverlayVR = "Floating icons are not available on VR.";
	inline constexpr auto kSetLogHeader = "Log";
	inline constexpr auto kSetLogLevel = "Log detail";
	inline constexpr auto kSetLogLevelTip = "Sets how much goes into Documents/My Games/Skyrim Special Edition/SKSE/DovaStash.log.";
	inline constexpr auto kSetLogTrace = "Trace (everything)";
	inline constexpr auto kSetLogDebug = "Debug";
	inline constexpr auto kSetLogInfo = "Info";
	inline constexpr auto kSetLogWarnings = "Warnings";
	inline constexpr auto kSetLogErrors = "Errors";
	inline constexpr auto kSetLogCritical = "Critical";
	inline constexpr auto kSetLogOff = "Off";
	inline constexpr auto kSetResetHeader = "Reset";
	inline constexpr auto kSetReset = "Reset settings to defaults...";
	inline constexpr auto kSetConfirmReset = "Reset every setting above to its default? Your hotkeys and the default keep rules are kept. No items are moved.";

	// All templates: the catalogue at the end of the Template tab and of the setup page (This home); outside a home,
	// This home shows the Templates header with kTemplatesNoHome, then the catalogue open.
	inline constexpr auto             kTemplatesNoHome = "Go into a player home to use a template there or save its setup.";
	inline constexpr auto             kAllTemplates = "All templates";
	inline constexpr auto             kAllTemplatesTip = "Every template loaded from Data/SKSE/Plugins/DovaStash/Templates/. A template is offered in its home when its required plugins are loaded; one someone made comes before a (draft).";
	inline constexpr auto             kTemplatesNone = "No templates loaded.";
	inline constexpr auto             kColTemplate = "Template";
	inline constexpr auto             kColAuthor = "Author";
	inline constexpr auto             kColHome = "Home";
	inline constexpr auto             kColRequires = "Requires";
	inline constexpr auto             kColIssues = "Issues";
	inline constexpr auto             kOk = "ok";
	inline constexpr auto             kStatusInUse = "in use";
	inline constexpr auto             kStatusRemoved = "removed from this home";
	inline constexpr auto             kStatusInUseTip = "This home's setup is based on it.";
	inline constexpr auto             kStatusFits = "fits this home";
	inline constexpr auto             kStatusFitsTip = "Offered in the home you are in: Use it under Templates.";
	inline constexpr auto             kOnlyInstalled = "Only installed homes";
	inline constexpr auto             kOnlyInstalledTip = "Hides templates for homes whose mods are not loaded.";
	inline constexpr auto             kTemplatesNoneInstalled = "No template for a home you have installed.";
	inline constexpr std::string_view kMissing = "missing: {}";
	inline constexpr std::string_view kConflicts = "conflicts: {}";
	inline constexpr auto             kConflictsTip = "Not offered: a mod that rebuilds this home is loaded.";
	inline constexpr std::string_view kIssues = "{} warnings";
	inline constexpr std::string_view kIssue = "{} warning";
	inline constexpr std::string_view kTemplateSourceTip = "{}\npriority {}";  // source, priority

	// About page, Diagnostics section (load problems; the header's tooltip explains it)
	inline constexpr auto             kDiagHeader = "Diagnostics";
	inline constexpr auto             kDiagIntro = "Problems found when the data files were loaded at game start (also in DovaStash.log).";
	inline constexpr std::string_view kDiagLine = "{}: {}";  // section, "No problems." or the counts
	inline constexpr auto             kDiagCategories = "Categories";
	inline constexpr auto             kDiagTemplates = "Templates";
	inline constexpr auto             kDiagSettings = "Settings.json (default keep rules)";
	inline constexpr auto             kDiagSelfSorting = "Homes that sort themselves (SelfSorting)";
	inline constexpr std::string_view kDiagHome = "This home's setup ({})";  // home name
	inline constexpr auto             kDiagNone = "No problems.";
	inline constexpr std::string_view kDiagCounts = "{}, {}";  // kDiagError(s), kIssue(s)
	inline constexpr std::string_view kDiagError = "{} error";
	inline constexpr std::string_view kDiagErrors = "{} errors";

	// About page (the Dova Stash Editor: Tools/DovaStashEditor.exe, deployed next to the plugin). The title, the
	// hotkey, the editor and the license; then Diagnostics (above); then For bug reports, collapsed: the runtime,
	// what is available, the counts and the files.
	inline constexpr std::string_view kAboutTitle = "Dova Stash v{}";
	inline constexpr auto             kAboutText = "Auto-sorting loot storage for any player home. Look at a container in a player home and press the hotkey to open its menu.";
	inline constexpr std::string_view kAboutRuntime = "Runtime: {}";
	inline constexpr std::string_view kAboutFramework = "SKSE Menu Framework: v{:.2f}";
	inline constexpr std::string_view kAboutHotkey = "Hotkey: {}";
	inline constexpr std::string_view kAboutHotkeyPad = "Hotkey: {} (gamepad: {})";  // keyboard, gamepad
	inline constexpr std::string_view kAboutCategories = "Categories: {}";
	inline constexpr std::string_view kAboutTemplates = "Templates: {}";
	inline constexpr std::string_view kAboutQuickLoot = "QuickLoot IE: {}";
	inline constexpr std::string_view kAboutUIExtensions = "UIExtensions: {}";
	inline constexpr std::string_view kAboutEditor = "Dova Stash Editor: installed at {}";
	inline constexpr auto             kAboutEditorMissing = "Dova Stash Editor: not installed";
	inline constexpr auto             kAboutAvailable = "available";
	inline constexpr auto             kAboutNotAvailable = "not available";
	inline constexpr auto             kAboutBugsHeader = "For bug reports";
	inline constexpr auto             kAboutBugsTip = "What to mention in a bug report. Attach DovaStash.log too.";
	inline constexpr auto             kAboutData = "Data: Data/SKSE/Plugins/DovaStash/ (Categories, Templates, Layouts)";
	inline constexpr auto             kAboutLog = "Log: Documents/My Games/Skyrim Special Edition/SKSE/DovaStash.log";
	inline constexpr auto             kAboutBackups = "Copies of your setups: Documents/My Games/Skyrim Special Edition/SKSE/DovaStash/Backups";
	inline constexpr auto             kAboutLicense = "License: GPL-3.0-or-later. Uses the SKSE Menu Framework 3 API header (LGPL-2.1).";

	// Armor set picker: a mannequin's Armor set on the Displays page (UI/Panel/ArmorSetPicker.cpp)
	inline constexpr auto             kSetsSearchHint = "Search sets, mods, pieces";
	inline constexpr auto             kSetsOnlyHave = "Only sets you have";
	inline constexpr auto             kSetsOnlyHaveTip = "Shows only the sets you own a piece of.";
	inline constexpr std::string_view kSetsCount = "{} of {} sets";
	inline constexpr auto             kSetsNoMatch = "No set matches.";
	inline constexpr std::string_view kSetsTooMany = "Showing the first {}; search to narrow it down.";
	inline constexpr auto             kColSet = "Set";
	inline constexpr auto             kColMod = "Mod";
	inline constexpr auto             kColPieces = "Pieces";
	inline constexpr auto             kColHave = "You have";
	inline constexpr auto             kSetsHavePiece = "  (have)";

	// Displays page (DESIGN §8.4, §10.1; UI/Panel/DisplaysPage.cpp)
	inline constexpr auto             kDispIntro = "The displays Dova Stash recognizes in this home. Used ones get your best gear, one of each, from Deposit, Sort all and Fill now. Nothing is taken off a display.";
	inline constexpr auto             kDispNotInHome = "Go to one of your homes to see its displays.";
	inline constexpr auto             kDispBroken = "This home's setup file is broken (see DovaStash.log).";
	inline constexpr auto             kDispNone = "No displays found here.";
	inline constexpr auto             kDispNoMatch = "No display matches.";
	inline constexpr auto             kDispRefreshTip = "Looks for displays again, for example after building a new wing.";
	inline constexpr auto             kDispKindTip = "Shows only this kind of display.";
	inline constexpr auto             kDispStatusTip = "Shows only the displays that are used, not used or ignored.";
	inline constexpr auto             kDispContentsTip = "Shows only the displays that are empty, hold something or have room.";
	inline constexpr auto             kDispSortTip = "Sorts the table.";
	inline constexpr std::string_view kDispHereArea = "{} ({})";  // home, area the player is in
	inline constexpr auto             kDispKindRack = "Rack";
	inline constexpr auto             kDispKindMannequin = "Mannequin";
	inline constexpr auto             kDispKindItem = "Plaque";
	inline constexpr auto             kDispKindCase = "Display case";
	inline constexpr auto             kDispAnyKind = "Any kind";
	inline constexpr auto             kDispAnyStatus = "Any status";
	inline constexpr auto             kDispUsedOnly = "Used";
	inline constexpr auto             kDispNotUsedOnly = "Not used";
	inline constexpr auto             kDispIgnoredOnly = "Ignored";
	inline constexpr auto             kDispAnyContents = "Any contents";
	inline constexpr auto             kDispEmptyOnly = "Empty";
	inline constexpr auto             kDispHoldsOnly = "Holds something";
	inline constexpr auto             kDispRoomOnly = "Room left";
	inline constexpr auto             kDispSearchHint = "Search names, items, categories";
	inline constexpr auto             kDispSortName = "Sort by name";
	inline constexpr auto             kDispSortKind = "Sort by kind";
	inline constexpr auto             kDispSortArea = "Sort by area";
	inline constexpr auto             kDispSortStatus = "Sort by status";
	inline constexpr std::string_view kDispCount = "{} of {}";
	inline constexpr auto             kColDisplay = "Display";
	inline constexpr auto             kColKind = "Kind";
	inline constexpr auto             kColArea = "Area";
	inline constexpr auto             kColTakes = "Takes";
	inline constexpr auto             kColHolds = "Holds";
	inline constexpr auto             kDispUsed = "used";
	inline constexpr auto             kDispNotUsed = "not used";
	inline constexpr auto             kDispIgnored = "ignored";
	inline constexpr auto             kDispIgnoredTip = "Turned off in the setup (Stop using, or the template). Use all leaves it off.";
	inline constexpr auto             kDispNotBuilt = "not built";
	inline constexpr auto             kDispNotLoaded = "not loaded";
	inline constexpr auto             kDispNotLoadedTip = "In the setup, but not in a loaded area of this home (or its plugin is missing). It is filled when you are in its area.";
	inline constexpr auto             kDispAnyWeapon = "any weapon";
	inline constexpr auto             kDispAnyArmor = "any armor";
	inline constexpr std::string_view kDispSet = "set: {}";
	inline constexpr std::string_view kDispPrefers = "prefers {}";
	inline constexpr std::string_view kDispOnly = "only {}";
	inline constexpr auto             kDispFull = "full";
	inline constexpr auto             kDispEmpty = "empty";
	inline constexpr auto             kDispSelect = "Select a display above.";
	inline constexpr auto             kDispUse = "Use";
	inline constexpr auto             kDispUseTip = "Adds it to the setup: Deposit, Sort all and Fill now fill it.";
	inline constexpr auto             kDispStopUsing = "Stop using";
	inline constexpr auto             kDispStopTip = "Stops filling it; it stays in the setup as ignored. Nothing is taken off it.";
	inline constexpr auto             kDispFill = "Fill now";
	inline constexpr auto             kDispFillTip = "Fills it now with what fits: what Deposit would move, and what you store.";
	inline constexpr auto             kDispFillFull = "It is full.";
	inline constexpr auto             kDispFillNotBuilt = "It is not built yet.";
	inline constexpr auto             kDispFillElsewhere = "Only displays in the area you are in are filled.";
	inline constexpr std::string_view kDispCategories = "Categories ({})";  // summary
	inline constexpr auto             kDispCategoriesTip = "Chooses what goes on it. Nothing ticked: anything that fits.";
	inline constexpr std::string_view kDispArmorSet = "Armor set ({})";  // set name
	inline constexpr auto             kDispSetTip = "Chooses what it wears, from every armor set in your load order, modded ones included. Sets you have come first; a set fills in as you get the pieces.";
	inline constexpr auto             kDispBulkHeader = "Use all";
	inline constexpr std::string_view kDispUseAll = "Use all {} {}...";  // count, plural kind
	inline constexpr std::string_view kDispConfirmUseAll = "Use all {} {}? Deposit and Sort all then fill them. No items are moved now.";  // count, plural kind
	inline constexpr std::string_view kDispIgnoredStayOff = "{} ignored stay off.";  // count
}
