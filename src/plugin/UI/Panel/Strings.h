#pragma once

// User-visible text of the Menu Framework panel, kept in one place so it can be translated later.
// Strings with "{}" are std::format patterns (positional arguments in the order documented).

namespace dova::ui::panel::text
{
	// Window / section
	inline constexpr auto kWindowTitle = "Dova Stash - This home##DovaStash";
	inline constexpr auto kSection = "Dova Stash";
	inline constexpr auto kItemHome = "This home";  // same page as hotkey > Advanced...
	inline constexpr auto kItemSettings = "Settings (all homes)";
	inline constexpr auto kItemTemplates = "Templates";
	inline constexpr auto kItemDiagnostics = "Diagnostics";
	inline constexpr auto kItemAbout = "About";

	// Common
	inline constexpr auto kLoading = "Loading...";
	inline constexpr auto kYes = "Yes";
	inline constexpr auto kNo = "No";
	inline constexpr auto kClose = "Close";
	inline constexpr auto kRemove = "Remove";
	inline constexpr auto kAdd = "Add";
	inline constexpr auto kNone = "-";
	inline constexpr std::string_view kNotInHome = "You are not in a player home ({}).";  // reason

	// Header
	inline constexpr std::string_view kHeaderHome = "{}";                // home name
	inline constexpr std::string_view kHeaderLayout = "Setup: {} v{} by {}";  // name, version, author
	inline constexpr std::string_view kHeaderBasedOn = "Based on template: {}";
	inline constexpr auto             kHeaderCustom = "Custom setup (not from a template)";
	inline constexpr std::string_view kHeaderMaster = "Master: {}";
	inline constexpr auto             kHeaderNoMaster = "Master: none (items without a destination stay in your inventory)";
	inline constexpr std::string_view kUnavailable = "unavailable: {}";

	// Setup (home without a layout)
	inline constexpr auto             kSetupIntro = "This home is not set up yet.";
	inline constexpr std::string_view kSetupMakeMaster = "Make {} the master";
	inline constexpr std::string_view kSetupLookAt = "Look at a container and press {} to make it the master.";
	inline constexpr auto             kSetupTemplates = "Templates for this home";
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
	inline constexpr auto             kDepositTip = "Move eligible items from your inventory into their containers (keep rules apply).";
	inline constexpr auto             kSortAll = "Sort all";
	inline constexpr auto             kSortAllTip = "Move every item in the master and in containers with category bindings to the container it belongs in. Containers without categories (e.g. added by Learn) are never emptied.";
	inline constexpr auto             kRefreshCounts = "Refresh counts";
	inline constexpr auto             kRetrieveHeader = "Retrieve";
	inline constexpr auto             kCounting = "Counting items...";
	inline constexpr auto             kNothingStored = "Nothing to retrieve from the managed containers.";
	inline constexpr std::string_view kRetrieveEverything = "Everything ({})";
	inline constexpr std::string_view kNodeCount = "{} ({})";  // name, count
	inline constexpr auto             kRetrieve = "Retrieve";
	inline constexpr auto             kTemplateHeader = "Template";
	inline constexpr auto             kNoTemplate = "No template matches this home.";
	inline constexpr std::string_view kTemplateLine = "{} v{} by {}";  // name, version, author
	inline constexpr auto             kTemplateBest = "(recommended)";
	inline constexpr auto             kTemplateApplied = "(in use)";
	inline constexpr auto             kUseTemplate = "Use";
	inline constexpr auto             kReapplyTemplate = "Re-apply";
	inline constexpr std::string_view kConfirmTemplate = "Use the template \"{}\"? Your learned destinations, keep rules and ignored containers are kept; the master and category bindings are replaced by the template's. A backup is saved; no items are moved.";
	inline constexpr auto             kResetHeader = "Reset";
	inline constexpr auto             kResetHome = "Reset home...";
	inline constexpr std::string_view kConfirmReset = "Reset Dova Stash for {}? Your setup is backed up (.bak); no items are moved.";
	inline constexpr auto             kBackupsHeader = "Saved setups";
	inline constexpr auto             kBackupsTip = "Each time this home's setup changes, Dova Stash also keeps a copy outside the Data folder: in Documents/My Games/Skyrim Special Edition/SKSE/DovaStash/Backups (Skyrim VR: My Games/Skyrim VR). MO2 never puts it in its overwrite folder, so cleaning that folder does not lose it. The latest copy, plus the copies from your last sessions.";
	inline constexpr std::string_view kRestoreLast = "Restore last setup ({})...";  // date
	inline constexpr std::string_view kRestoreOlder = "Restore the setup from {}...";  // date
	inline constexpr std::string_view kOlderBackups = "Older copies ({})";  // count
	inline constexpr auto             kNoBackups = "No saved copy of this home's setup yet.";
	inline constexpr std::string_view kBackupSame = "The latest copy ({}) is this setup as it is now.";  // date
	// Multi-cell homes (DESIGN §6.7)
	inline constexpr auto             kAreasHeader = "Areas";
	inline constexpr auto             kAreasTip = "The cells this home spans: the main one, a cellar, the grounds outside... You are in the home in any of them. From one area, containers in another are used when their reference is persistent (the panel says which); displays are filled only in the area you are in. To add an area, walk into it from the home and press the hotkey.";
	inline constexpr auto             kAreaMain = "(main)";
	inline constexpr auto             kAreaHere = "(you are here)";
	inline constexpr std::string_view kAreaNameHint = "name, e.g. Grounds (optional: {})";  // the cell's own name
	inline constexpr auto             kAreaRenameTip = "The name this area goes by in the prompts and the panel. Empty: the cell's own name.";
	inline constexpr std::string_view kAreaExcluded = "{} is listed but has a setup of its own, so it is not part of this home.";  // cell
	inline constexpr std::string_view kHereArea = "in {}";  // area name, after the home's name
	inline constexpr std::string_view kInArea = "In {}";  // area name
	inline constexpr auto             kRemoteTip = "In another area of this home, used from here: its reference is persistent (or its area is loaded).";

	// Containers tab
	inline constexpr auto             kColContainer = "Container";
	inline constexpr auto             kColCategories = "Categories";
	inline constexpr auto             kColLearned = "Learned";
	inline constexpr auto             kColStatus = "Status";
	inline constexpr auto             kMasterTag = "[Master] ";
	inline constexpr auto             kHomeless = "(items without a destination)";
	inline constexpr auto             kIgnoredTag = "(ignored)";
	inline constexpr auto             kNotManaged = "not managed yet";
	inline constexpr std::string_view kLearnedOnly = "{}, learned";  // category names of the learned items
	inline constexpr auto             kAvailable = "available";
	inline constexpr auto             kSelectContainer = "Select a container above, or look at one in the world and press the hotkey.";
	inline constexpr auto             kMasterInfo = "This is the master container. Items without a destination stay here, and closing it sorts its contents into the other containers.";
	inline constexpr auto             kLabel = "Label";
	inline constexpr auto             kRename = "Rename";
	inline constexpr auto             kOpenContainer = "Open";
	inline constexpr auto             kOpenContainerHelp = "Opens this container's own menu. The Dova Stash window closes first.";
	inline constexpr auto             kIgnore = "Ignore";
	inline constexpr auto             kStopIgnoring = "Stop ignoring";
	inline constexpr auto             kRemoveContainer = "Remove...";
	inline constexpr std::string_view kConfirmRemove = "Remove {} from Dova Stash? {}";  // label, ui::kRemoveExplained
	inline constexpr auto             kMakeMaster = "Make master...";
	inline constexpr std::string_view kConfirmMakeMaster = "Make {} the master? The current master becomes an ordinary container; no items are moved.";
	inline constexpr auto             kClearCategories = "Clear categories";
	inline constexpr std::string_view kForgetLearned = "Forget learned ({})...";
	inline constexpr std::string_view kConfirmForget = "Forget the {} learned item destination(s) of {}?";
	inline constexpr std::string_view kExplicitForms = "{} fixed item binding(s) (edit them in this home's file under Layouts/).";
	inline constexpr auto             kCategoriesHeader = "Categories sent here";
	inline constexpr std::string_view kLearnedInfo = "Learned here: {}. Only those exact items come here; tick a category to send all of it.";
	inline constexpr auto             kIgnoredInfo = "Ignored containers receive nothing and teach nothing, and Dova Stash never adds them back. Their categories are kept for when you stop ignoring them. To forget a container instead, use Remove.";
	inline constexpr auto             kEverything = "Everything (*)";
	inline constexpr std::string_view kAllOf = "All {} ({}.*)";  // name, id
	inline constexpr auto             kViaWider = "(via a wider pattern)";
	inline constexpr auto             kOtherPatterns = "Other patterns (unknown categories):";

	// Learned tab
	inline constexpr auto kLearnedEmpty = "Nothing learned yet. Put an item into a managed container by hand to teach Dova Stash where it belongs.";
	inline constexpr auto kColItem = "Item";
	inline constexpr auto kNotManagedSuffix = " (not managed)";

	// Keep tab
	inline constexpr auto kKeepIntro = "Keep rules decide what Deposit leaves in your inventory. Quest items and non-playable items are always kept.";
	inline constexpr auto kKeepNoBlock = "This home uses the global defaults only. Saving creates keep rules for this home.";
	inline constexpr auto kKeepInherit = "Also apply the global defaults (inherit)";
	inline constexpr auto kKeepAlways = "Always keep";
	inline constexpr auto kKeepNever = "Never keep";
	inline constexpr auto kKeepQuotas = "Quotas (keep up to N)";
	inline constexpr auto kKeepAddHint = "equipped / favorited / quest / Plugin.esm|0x00000F / category";
	inline constexpr auto kKeepPickCategory = "Category...";
	inline constexpr auto kColQuota = "Quota";
	inline constexpr auto kColOther = "Other selectors";
	inline constexpr auto kAddQuota = "Add quota";
	inline constexpr auto kQuotaNeedsCategory = "Pick a category for every quota (rules without any selector are dropped).";
	inline constexpr auto kSave = "Save";
	inline constexpr auto kRevert = "Revert";
	inline constexpr auto kUnsaved = "Unsaved changes";
	inline constexpr auto kKeepUseGlobal = "Use global defaults only...";
	inline constexpr auto kConfirmKeepGlobal = "Remove this home's keep rules and use only the global defaults?";
	inline constexpr auto kKeepGlobalHeader = "Defaults for every home";
	inline constexpr auto kKeepGlobalWhere = "Edit these under Dova Stash > Settings (all homes).";
	inline constexpr auto kKeepGlobalIntro = "What Deposit leaves in your inventory in every home. A home's own keep rules add to these.";
	inline constexpr auto kNoCategory = "(none)";
	inline constexpr std::string_view kKeywordsAny = "keywords: {}";
	inline constexpr std::string_view kFormsCount = "{} form(s)";

	// Template tab
	inline constexpr auto             kExportIntro = "Save this home's setup as a template: a copy you can bring back with Use template after a reset, or share (other players drop it into Data/SKSE/Plugins/DovaStash/Templates/). The hotkey menu's Save as template... does the same.";
	inline constexpr auto             kExportLearned = "Keep learned items (as fixed item bindings)";
	inline constexpr auto             kExportLearnedTip = "Items you taught Dova Stash by putting them away by hand are saved as fixed item bindings of their containers, so they go to the same containers for anyone who uses this template. Untick it for a template to share without your personal habits.";
	inline constexpr auto             kExportName = "Name";
	inline constexpr auto             kExportAuthor = "Author";
	inline constexpr auto             kExportAuthorHint = "your character's name";
	inline constexpr auto             kExportVersion = "Version";
	inline constexpr auto             kExportVersionHint = "automatic";
	inline constexpr std::string_view kExportFile = "Writes Data/SKSE/Plugins/DovaStash/Templates/User/{}.json (under MO2: the overwrite folder). This home offers it right away. Replacing a file backs it up and, with an automatic version, raises its version.";
	inline constexpr auto             kExport = "Export";
	inline constexpr auto             kExporting = "Exporting...";
	inline constexpr auto             kOverwrite = "Overwrite";
	inline constexpr auto             kCancel = "Cancel";
	inline constexpr auto             kExportWarnings = "Template warnings:";

	// Global pages
	inline constexpr auto             kSettingsNote = "Changes apply immediately and are saved to Data/SKSE/Plugins/DovaStash.ini. No restart needed. Hover an option for details.";
	inline constexpr auto             kColSetting = "Setting";
	inline constexpr auto             kColValue = "Value";
	inline constexpr auto             kTemplatesIntro = "Every template loaded from Data/SKSE/Plugins/DovaStash/Templates/. A template is offered in its home when all of its required plugins are loaded.";
	inline constexpr auto             kTemplatesNone = "No templates loaded.";
	inline constexpr auto             kColTemplate = "Template";
	inline constexpr auto             kColAuthor = "Author";
	inline constexpr auto             kColHome = "Home";
	inline constexpr auto             kColRequires = "Requires";
	inline constexpr auto             kColIssues = "Issues";
	inline constexpr auto             kOk = "ok";
	inline constexpr std::string_view kMissing = "missing: {}";
	inline constexpr std::string_view kIssues = "{} warning(s)";
	inline constexpr auto             kDiagIntro = "Problems found when the data files were loaded at game start (also in DovaStash.log).";
	inline constexpr auto             kDiagCategories = "Categories";
	inline constexpr auto             kDiagTemplates = "Templates";
	inline constexpr auto             kDiagSettings = "Settings.json (global keep defaults)";
	inline constexpr auto             kDiagHome = "This home's setup";
	inline constexpr auto             kDiagNone = "No problems.";
	inline constexpr std::string_view kDiagCounts = "{} error(s), {} warning(s)";
	inline constexpr auto             kAboutText = "Auto-sorting loot storage for any player home. Look at a container in a player home and press the hotkey to open the stash window.";

	// Armor sets page (mannequins)
	inline constexpr auto             kItemArmorSets = "Armor sets";
	inline constexpr auto             kSetsIntro = "Every armor set in your load order, modded ones included. Pick a mannequin and a set: it wears that set as you get the pieces (Deposit, Sort all, Fill now). Auto: it wears your most complete matching set.";
	inline constexpr auto             kSetsNotInHome = "Go to one of your homes to dress its mannequins. You can still browse the sets.";
	inline constexpr auto             kSetsNotEnabled = "Set this home up first (This home).";
	inline constexpr auto             kSetsNoMannequins = "No mannequins in this home.";
	inline constexpr auto             kSetsMannequin = "Mannequin";
	inline constexpr std::string_view kSetsMannequinRow = "{}: {}";  // name, set
	inline constexpr auto             kSetsNotUsed = " (not used yet)";
	inline constexpr auto             kSetsSearchHint = "Search sets, mods, pieces";
	inline constexpr auto             kSetsOnlyHave = "Only sets I have";
	inline constexpr std::string_view kSetsCount = "{} of {} sets";
	inline constexpr std::string_view kSetsTooMany = "Showing the first {}; search to narrow it down.";
	inline constexpr auto             kColSet = "Set";
	inline constexpr auto             kColMod = "Mod";
	inline constexpr auto             kColPieces = "Pieces";
	inline constexpr auto             kColHave = "You have";
	inline constexpr std::string_view kSetsAssign = "Assign {} to {}";  // set, mannequin
	inline constexpr auto             kSetsAuto = "Auto";
	inline constexpr auto             kSetsPickSet = "Select a set in the list.";
}
