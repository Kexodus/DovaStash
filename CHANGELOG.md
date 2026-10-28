# Changelog

## [Unreleased]

### Added

- Restock: tick *Restock* on a keep quota ("10 health potions", "50 steel arrows", "5 lockpicks") and every Deposit tops you back up to that number from the home's containers, the most valuable copies first, never more. *Restock* is also in the hotkey menu and the panel on its own. HUD: "Dova Stash: deposited 12 items into 3 containers, restocked 10". Setting: "Restock after Deposit" (on).
- Homes with their own sorting system are left alone, so the two never move the same items: Clockwork, Legacy of the Dragonborn, Mirele Bismath Reborn, LUFTSHIP K747, Skyfall Estate, Riverside Lodge, The Hoarder's Chest - Rebuilt, The Dovah Den, the five city homes with Player houses overhaul, and Tundra Homestead with Tundra Homestead - LotD Storage. With Carpenter Storage Chests for Hearthfire only its building-supply chests are left out.
- Homes another mod rebuilt: the Breezehome template steps aside for Wynter's Breezehome Redux, the city homes' for Player houses overhaul. For other mods a template is set up on its own only when most of what it names is still there; otherwise the HUD says "Breezehome setup may not fit (press K)" and the hotkey menu still offers it.
- A display's Categories only offer what it can hold: a weapon rack the weapon categories, a mannequin armor, a bookshelf books; never soul gems, potions or misc items. A category that can't go on a display puts nothing on it (the log says so once).
- Sell chest: press the hotkey at a container and choose "Make sell chest..." to turn your junk into gold (Clutter goes there to start with). Nothing is sold until you choose "Sell now (862 gold)..." and confirm the total, at a merchant's price; stolen, improved (tempered, enchanted by you, renamed), quest, worn or favorited items, soul gems you filled yourself, potions you made and what your keep rules keep never sell. "Buy back" returns your last sale of the session. Settings: "Price" (100%) and "Sell by itself after" (off).
- Displays: look at a weapon rack, mannequin or item plaque (dragon priest masks, claws, Elder Scrolls...) and press the hotkey to use it and pick its Categories. Deposit and Sort all then keep it filled as if placed by hand: racks get your best weapons, mannequins a matching armor set, a plaque its own item, the Creation Club homes' plaques included. Only what you stored goes on a display, never what a container held before you used it (a mod home's pre-filled chest, an alchemy sack's salt). Nothing is taken off a display; a full plaque's menu has "Take down", which hands its item back. Setting: "Put gear on the home's displays".
- Bookshelves are displays: Deposit puts your books on one, as many as it holds and never more: skill books first, then spell tomes, then the most valuable, one copy of each title; never an Elder Scroll, notes only when you pick them. A book that drops off a shelf whose marker lies outside it (Niflholm) goes back to its container, and that shelf is left alone for the session; a book missing from a shelf is looked for in the home's containers and moved back, and only one found nowhere is replaced by a new copy.
- Materials at the workbench: at a forge, smelter, tanning rack, cooking pot, alchemy lab or modded station in a home you set up, you craft from what the home's containers hold (`Iron Ingot (12/1)`); each craft takes only what you lack, and nothing moves when you sit down. Enchanters bring your filled soul gems. Tempered, enchanted, favorited and quest items are never used. Setting: "Materials at the workbench": Auto, Off, Move or Craft from storage; Auto steps aside for another crafting-from-storage mod and says why (on VR it means Move).
- Mannequins wear your most complete matching armor set, modded armor included. To pick one, press the hotkey at a mannequin: "Armor set... (Auto)" lists the sets you have ("Ebony (3/4)"); the panel's Armor set searches every set in your load order.
- SKSE Menu Framework "Displays" page: every rack, mannequin, plaque and display case of the home, with what it takes and holds. Filter, search and sort; Use, Categories, Armor set, Fill now, or "Use all 12 racks..." in one go.
- Homes that span several cells: walk out of the home into the next area (cellar, grounds) and press the hotkey: "Add this area to Lakeview Manor", with a name if you like. The forge outside then crafts from what is stored inside, and Deposit, Sort all and Retrieve use the containers of every area the game keeps loaded; displays are filled in the area you are in.
- One template per multi-cell home: Lakeview Manor, Windstad Manor, Heljarchen Hall, Tundra Homestead and Myrwatch cover the whole home, cellar, gallery and grounds included; the separate cellar and gallery templates are gone, and a cellar set up by its old template joins the home when you apply the new one. In a cellar or gallery of a home with no setup yet, the first visit says "Dova Stash: part of Lakeview Manor; set it up in its main room", and the hotkey offers Use template "Lakeview Manor", which sets up the whole home from there.
- Area masters: an area can have its own master (the Tundra Homestead cellar, the Myrwatch gallery). There Deposit and Sort all put what has no container in reach into it, and the hotkey at it opens the home's menu. A template that makes a container an area master forgets its categories and learned items, as "Make master of Cellar..." does.
- Remove: the container menu (and the panel) takes a container out of the setup, forgetting its categories and learned items; what's inside stays.
- A container with learned items only names their categories: "End Table (Books, learned)", and "Books (1 learned)" in the picker and the panel.
- Save as template...: the hotkey menu saves this home's setup as a template under a name you type, with your character as author and your learned items ("incl. 12 learned items"). It is offered at once, so after Reset home, Use template brings the setup back.
- Setups are backed up outside the Data folder (`Documents/My Games/Skyrim Special Edition/SKSE/DovaStash/Backups`, copies from your last 5 sessions), so cleaning MO2's overwrite folder no longer loses them.
- Restore your last setup: entering a home whose setup file is gone shows "Dova Stash: a saved setup for Whitepeak Tower can be restored (press K)", and the setup menu offers "Restore your last setup (2026-10-12 14:03)" first. The panel lists every saved copy (Actions > Saved setups).
- Learning: the HUD line for a learn offers an undo ("Iron Sword now goes to End Table. K: undo"; setting "Undo time for a learn"). After three learned items of one category go to one container, Dova Stash asks once: "Send all Books to End Table?" (setting "Suggest a category after"). Enchanted variants count as the item they are made from. The panel's Learned tab forgets entries that do nothing any more in one go.
- Switch templates in the SKSE Menu Framework: This home > Template lists "Templates" (the tooltip says which home), the one in use marked. Use switches in one click, and asks first when it would replace changes of your own (they are backed up). Learned items, keep rules and ignored containers stay. "All templates" lists every template with its status.
- Remove a template from a home: "Remove..." on its row stops it being offered there; "Add back" undoes it.
- The panel works as an in-game editor: This home > Containers marks the container you look at "(looking at it)" after its status ("managed (looking at it)") and scrolls to it.
- Settings: Reset settings to defaults... puts every option back (your hotkeys and default keep rules stay).
- Templates for the Arch-Mage's Quarters and Dead Man's Dread, so every vanilla and Creation Club home has one.
- Templates for 13 mod homes, offered only when the home's mod is loaded: Pinegrove Lodge, Bal Ruhn, Wuth Rein, Ebongrove, Hydda, Black Fur Refuge, Sicarius' Refuge, Niflholm, Tel Jerdein, Antennaria, Winter Cove and Unique Vampire Dens (Lost Hall, Secret Cellar). 32 template files in all, one per home.
- Most shipped templates also fill the home's racks, mannequins and bookshelves (each mannequin on Auto); turn on any others with K. Item plaques and display cases are left to you. A template can name items and armor sets from other mods: without the mod they are skipped with one log line and the rest works.

### Fixed

- Salt in a vanilla alchemy sack or bread in a food barrel no longer stops that item moving for the rest of the session.
- Homes from plugins with non-English names (`Château.esp`) keep their setup after a reload (nothing was lost before; the setup just did nothing).
- Loading another save mid-session: nothing you started in the old save (a deposit, a fill, the undo window) carries over into the new one. Learned destinations not yet written are saved first.
- A hotkey list could stay open but invisible (input locked until Esc) when an entry that keeps the list open was chosen within the first half second, while the list was still being centered.
- Pressing the hotkey again while a list is opening no longer opens a message box on top of it.
- A container or item name ending in `;` dropped its row from the hotkey lists.
- Hotkey lists longer than 128 entries (a container with 130 learned items) are paged with More... instead of cut off.
- Hand-edited setups and templates: a container listed twice uses the first entry only, and a mistyped field is kept as written instead of being reset.
- Taking a just-deposited item back out through QuickLoot IE's loot menu no longer marks it as "comes back when stored" (an item that would never be moved again that session): only a script handing it back counts, as with the normal container menu.
- The panel's Template tab starts from the current home's name after you move to another home, instead of the first home's.
- Arrows or bolts picked up after equipping that type stay on you with the rest of the quiver when you Deposit. Before, Deposit took them out of the equipped stack, and from then on left the spare copies of every worn item on you for the rest of the session (nothing was lost). If the game ever takes a special copy for a plain one again, only that item stays put for the session.
- Spare copies of worn gear are deposited now. Before, a second Iron Shield stayed on you while you wore one. Each such move checks that the worn, tempered or enchanted copy stayed behind.
- Shelves, racks and cupboards that open a hidden chest (an activator linked to it, like Whitepeak Tower's Food shelf or a vanilla bookshelf) can be set up with the hotkey now. They show the chest's categories in the prompt and as floating icons.
- Items that a script hands straight back when stored, such as the Curators Storage Token, stay on you after the first try instead of coming back (and opening their prompt) on every Deposit.
- The hotkey lists and the rename box appear already in the middle of the screen, instead of showing at the skin's position and then jumping there.
- Browse and "Open <container>" right after a container menu closed opened nothing. Browse now waits up to 3 s for the closed menu to settle, opens the container, and if nothing opens in 3 s tries again the console's way; only 8 s later does the HUD say "Dova Stash: Chest did not open".
- Worn and favorited items always stay with you, even with a keep block that drops the defaults.
- Use template from a stale entry, when the home has no template any more, says "Dova Stash: no template for this home".
- A wrong form reference in a template, category file, keep entry or declined key now says why (expected Plugin.esm|0x00ABCDEF, not a plugin file name, or drop the load-order byte).
- Deposit says how many items stay with you because their area is not loaded ("(3 items left: master not here)", or "(3 items left: visit Cellar first)" when their container stands in an area not visited this session). When none moved it says "nothing deposited (3 items left: master not here)" (or "no container here takes them") instead of "nothing to deposit".
- A hotkey list that never comes up (UIExtensions not loading, another menu in the way) now says "Dova Stash: the list menu did not open (see DovaStash.log)" instead of nothing.
- A setting that cannot be saved says so in red on the Settings page ("Settings not saved (see DovaStash.log)").
- The "E is also Activate." warning under a hotkey also shows after a restart.
- Owners are shown by name, never by form ID ("not a player home (owned by WLightHouseFaction)", else "a faction").
- A typo in a keep rule ("restok"), Settings.json's default keep rules included, a category's match field or an enum value is reported with the one it may be ("did you mean"); a wrong category value (`"weaponTypes": "Sword"`) lists the valid ones.
- Home backups retry when a sync client such as OneDrive holds the file.
- A keep rule that fails to parse is saved back in its place, so first-match order holds.

### Changed

- The hotkey is set by pressing it: on the Settings page, click the keyboard or gamepad hotkey and press the key, mouse button or controller button you want; it is saved at once. Clear turns the gamepad hotkey off. Esc closes the window without changing anything; the Menu Framework's own key (F1, LB) can be bound too. A key that is also a game control or the Menu Framework's own key gets a warning ("E is also Activate.").
- The SKSE Menu Framework panel has four pages, This home (with templates), Displays (with armor sets), Settings and About (with diagnostics), and they read and behave the same everywhere: one dimmed intro line per page, tooltips that say what a control does (also on grayed-out ones, every button that asks first and every status cell), "..." on every button that asks first, small buttons on list rows, empty states in the same dimmed style, and the actions that reset or remove things at the bottom; Sell now, Buy back and Make sell chest... ask first like every other such button. Settings are grouped into Hotkey, Menus, Sorting, Learning, Crafting, Templates, Sell chest, QuickLoot IE, In the world and Log, with shorter labels; the global keep rules are called "Default keep rules" everywhere.
- The panel's Template tab button is now "Save" (was "Export"), under a "Save as template" heading.
- Homes with a template are set up as soon as you walk in: "Set up homes from a template on first visit" (`bAutoEnableTemplates`) is now on by default. Turn it off to get the "setup available" notice instead. An INI you already have keeps its value.
- The hotkey at a bookshelf opens its display menu instead of the container menu; one your setup already stores items in keeps its container menu.
- Plainer words in the game: the HUD, menus and panel say "setup" instead of "layout" ("Breezehome is set up", "Custom setup (not from a template)", "this home's setup file is broken"). File names and the template format are unchanged (`Layouts/`, `dovastash-layout-1`).
- The panel's Template tab saves the same way as the hotkey: the template is offered right away (no restart), the author defaults to your character and the version is automatic, and overwriting backs up the old file.
- Your own saved templates come first for their home (ahead of shipped ones of the same priority), so the hotkey and the first-visit notice offer them.
- A template the editor's *Draft all homes* saved, that nobody has saved since, comes after every template someone made for the same home, whatever its priority or version: the hotkey, the first-visit notice and "Set up homes from a template on first visit" use it only in a home that has no other template. Applying one gives a normal setup.
- The hotkey menu says Re-apply template only for the template the home's setup came from; another template reads Use template.
- Floating icons show a container's full category row, like QuickLoot's bottom-right row: up to five icons, then "+N".
- Every hotkey list starts with a header naming the menu ("-- Whitepeak Tower --", "-- Chest: Categories --"), with the Dova Stash logo above it in your list skin's text color. Choosing the header does nothing.
- Hotkey lists show category icons from your own SkyUI icons: the category picker, the display picker, Retrieve, Browse containers (each container's icon) and a container's Categories... entry.
- Ticking a category in the hotkey lists updates the list in place, keeping your place in it, instead of closing and reopening the menu.
- A shorter hotkey menu for the home: Deposit, Restock, Sort all, Retrieve..., Advanced..., then the sell entries and Rename. Advanced... holds Browse containers..., Use / Re-apply template, Save as template..., Reset home... and Open panel... (the SKSE Menu Framework panel). Message boxes show 5 entries a page with More..., Back and Cancel (a lone 6th entry stays on the last page). Message boxes also offer Rename and Restore original name when UIExtensions is installed (it supplies the text box).
- Browse containers... in message boxes, in a big home (over 3 pages): the masters, then one line per area, or per category ("Weapons (4)..."), each opening its containers.
- Retrieve... > Everything asks first above 50 items: "Retrieve 120 items? You may be over-encumbered. Deposit puts them back." No goes back to Retrieve....
- In the hotkey menus, Clear categories... and Make master... ask first too, with the panel's questions (each ends "No items are moved."), and No on any question (Sell now's and Buy back's included) goes back to the menu it came from; cancelling the Save as template name goes back to Advanced.... Message boxes name their menu ("Dova Stash: set up Breezehome?", "Dova Stash: Chest: Categories").
- Use template asks first only when it would replace changes of your own, or when the template may not fit (the entry then ends "(may not fit)"). The question is the same in the hotkey menus and the panel.
- The setup prompt and a container's list end with Open panel... (with the SKSE Menu Framework) instead of an Advanced... that did nothing.
- Shorter notices: with "Set up homes from a template on first visit" on, entering a home says what to do, "Breezehome is set up (press K)" (it used to say only that the setup was enabled), and Use template and Re-apply word it the same way ("Lakeview Manor is set up (3 containers unavailable for now)"); "setup available (press K)", "setup may not fit (press K)"; a learn reads "Iron Sword now goes to End Table. K: undo"; every hint at the log reads "(see DovaStash.log)". Refusals are short too ("can't use Barrel (respawns, items would be lost)", "owned by X", "only reachable in X"); the log gives the long form. HUD notices and the Add / Remove area entries name the home by its setup's name, else the cell's ("Tel Jerdein reset (setup backed up)"); menu titles keep the cell's name.
- The panel's status line is at the top of the page and belongs to the page whose action posted it (This home, Displays or Settings) and the home it was set in; it is red only when the action failed, and stays in view while the page scrolls (on Settings too).
- Keep rules have Add rule, Add category and Add item pickers (Add item with a search box); text that is not a keep selector is not added. They show item names instead of form refs, and the third list is now "Quotas".
- Panel: the Displays page marks and selects the display you look at; the "Templates" header is shorter; Save as template asks Yes / No before replacing a template. The Displays page's Armor set shows only for a mannequin in use. The armor set pickers list at most 100 sets (message boxes 15 entries: Auto, the current set, the complete sets, then the most complete others) and say where the rest are ("More sets: Dova Stash > Displays", or how many without the panel). Categories read by name (All Weapons, Potions) with the id in the tooltip; the header names the template (Breezehome v1.0.2), and cell, container and display form references are tooltips.
- Hearthfire homes use the furniture of whichever wing you build.
- Breezehome and Vlindrel Hall: the child's bedroom takes the alchemy items when it replaces the alchemy corner.
- The Arch-Mage's Quarters uses every safe container.
- Myrwatch: the gallery's armory cupboard takes all weapons.
- A Welkynd Stone is a curio (new category Curios), not a potion Restock hands out. Curios go with the gems. Torches and modded lanterns have their own category, Torches (the default keep rules still keep your torches); the second "Books" category is now "Other books".
- The Displays page says "No display matches." when its filters match nothing, and the set picker "No set matches.".
- The learned-items menu's first entry reads "Forget 1 learned item..." when there is one ("Forget all 3 learned items..." otherwise), in both hotkey UIs.
- Wuth Rein: its scripted bookshelves are no longer displays (a book put on one could vanish); books go to the chest by the shelves.
- Sicarius' Refuge: the mannequins `0x0016CC2D` and `0x0016CC2E`, which the mod keeps disabled, are no longer listed; "Armour storage" is now "Armor storage".
- Antennaria, Tel Jerdein, Lost Hall and Secret Cellar bind more of their furniture (Antennaria 16 containers, Tel Jerdein 15, Lost Hall 8, Secret Cellar 8); Lost Hall and Secret Cellar say in their note what still goes to the master.
- Clearer template labels: Antennaria ("Jewelry chest", the respawning pack is "Large sack"), Tel Jerdein's root cellar shelves, Tundra Homestead's cellar bookcases and Niflholm's ignored containers named by landmarks (no clutter on the cellar's jewelry bookcase), "Armor" spelling in Pinegrove Lodge and Ebongrove.
- Winter Cove binds every category (ammo, soul gems, gems, tools, Dwemer parts, animal parts, clutter) and uses the main hall's soul gem container, wardrobe and end table while you are there.
- Goldenhills, Honeyside and Myrwatch (tower and gallery) explain in a note why some categories go to the master.
- Tel Jerdein, Niflholm and Winter Cove: each area has its own master, so a Deposit there leaves nothing with you; Winter Cove's master is now a main hall chest.
- Dropping an item in an area's master (the Tundra Homestead cellar chest, the Myrwatch gallery's) no longer teaches that chest: like the master, it passes items on when closed. An area master marked as a sell chest is never sold from, and switching templates keeps your items bound to it; Sort all leaves homeless items in an area master, as Deposit does.
- A container in an extra area offers "Make master of Cellar..." in its menu and panel row instead of "Make master...": that container becomes the area's master and the home's master stays. It names the container's area, not the one you stand in (Heljarchen Hall's Grounds chest seen from the Smelter yard: "Make master of Grounds..."). Browse containers lists each area master right after the master ("Chest - Cellar master (4 items)").
- Labels: Lakeview Manor, Windstad Manor and Heljarchen Hall (Storage room chests numbered from the first, "Grounds chest", the second back-room display wardrobe is "Main hall display wardrobe (back room, 2)"), Hendraheim ("Wardrobe"), Severin Manor ("Armory large sack"). The Hearthfire templates' note says that cellar leftovers go to the cellar's chest, and that among containers sharing a category the first one built, in list order, takes it.
- Updating no longer resets your settings. The zip ships `SKSE/Plugins/DovaStash.ini.default`, and Dova Stash creates `DovaStash.ini` from it on the first start (under MO2 it lands in overwrite). Coming from 0.2.4 or earlier: that version kept `DovaStash.ini` in the mod folder, so if your mod manager replaces the folder your settings go back to the defaults once (set the hotkey again). From now on, updates keep them.
- Unbuilt furniture is listed once when a template is used, no longer as a warning on every Deposit, and unbuilt Hearthfire furniture reads "not built yet", not "disabled".
- New panel tooltips (the Everything pattern `*`, a template's "in use", Keep's "use the global setting", Save's Name, Author and Version, a display not built yet); a learned row's button reads Forget; the Icon size reads in px ("32 px").

### Template authors

- Templates can now name a home's displays, extra areas, sell chest and Restock quotas, and step aside for mods that rebuild the home; a file in `SelfSorting/` adds a home to those left alone (details in `tools/README.md` and the templates article).
- Saved templates list the plugins of the displays they use in `requires`, so they pass the template check (not the plugins of display items or mannequin sets: those are optional).
- The editor's *Draft all homes* marks its files `"autoDraft": true`; saving one by hand takes the mark away.
- `esm_dump.py check` accepts `//` and `/* */` comments in templates, as the game does.
- Labels in one form: containers and displays are named after where they stand ("Bookshelf (by the master chest, 2)"), never after themselves, with repeats spread over nearby landmarks; what a container holds comes before its kind, never in the parentheses. Shipped labels follow it (Lost Hall "Food barrel", Severin Manor "Alchemy lab potions sack", Tundra Homestead "Alchemy corner ingredients cupboard"); the Myrwatch gallery's containers start with "Gallery".
- `esm_dump.py check` warns about an area with no master in reach and about labels that put contents in parentheses; its rules have self-tests run by ctest.
- A master or area master also listed in `containers` is a warning (in the game's log and in `esm_dump.py check`): its categories are used, it stays the catch-all.
- Templates and saved layouts are easier to read and edit by hand: one line per container with the columns lined up, and short names. The reason a container is left alone is in a separate `note` (shown in the panel under the container's name) instead of filling its name.
- The data checker warns about a repeat label without its first and about a word ordinal after a landmark ("(back room, second)": use ", 2"). A Hearthfire cellar chest that starts disabled as an area master is reported as built later (info `areaMastersBuiltLater`), not as a warning.
- A template's `cells` entry can name the area's own `master`, used there while the home's master is out of reach, and `replaces` lists retired templates it takes over (an untouched setup from one of them joins). `esm_dump.py check` fails a cell that is in two shipped templates.

### Dova Stash Editor (in development)

- Dova Stash Editor (in development): a template editor that runs outside the game. It ships in the zip as `Tools/DovaStashEditor.exe`, with its guide beside it (`Tools/DovaStashEditor-UserGuide.pdf`); under MO2, add it as an executable. It reads your MO2 instance (none found: the start panel lists the Skyrim instances MO2 registered, one click opens one), or a Skyrim Data folder for Vortex and plain installs, and lists every player home of your load order under the mod that adds it. You don't need it to play; the panel's About page says where it is installed.
- One window: your homes in a rail on the left (one search box for home, mod, plugin or Nexus id; homes that share a name show their cell's EditorID; each home with its cells, templates and one status word: *yours*, *shipped*, *auto-draft*, *draft* or *none*), the home in 3D or as a floor plan in the middle, the inspector on the right (Selection, Template, Draft, Checks), and a drawer of tables under the home (Containers, Displays, Doors, Where items go) that sort by any column and filter. Ctrl+K opens a command palette, and every tool has a key.
- The 3D view looks like the Creation Kit's: meshes and textures from your mods and archives, exterior terrain and the cells around it, a fly camera, and a roof cut so caves such as Wuth Rein show their rooms. The overview frames the home; layers hide effects, trigger volumes and the boxes that stand in for missing meshes.
- Set every role a template has: the master, each container's categories (also by painting), ignored containers, a joined cell's area master (*Make this area's master*), displays (racks, mannequins with an armor set from any set of the load order, plaques, bookshelves), keep rules with quotas and Restock, the sell chest and the mods that rebuild the home. Undo and redo cover every change.
- *Draft all homes* (Ctrl+D) drafts a template for every home in the background: the master and each container's categories suggested from names, nearby stations and items, each with its reason, and an area master for each joined cell. Review a draft in the Draft panel, then *Save N drafts...*. An auto-draft only ever replaces that home's earlier auto-draft; open one and Save to make it yours. Drafts explain each choice ("one of 4 that can be the master"), join a Hearthfire home's grounds and built rooms (a room's name counts: an Armory takes weapons), and tell rows with the same name apart by room, nearest station and side of the room. The Draft panel says what it compares ("The draft and shipped: 1 of 39").
- Checks lists errors first (click one to select it) and what the template still lacks. It runs the game-data checks of `esm_dump.py check` over every cell the template lists: a reference that is not in the home or not a container, a container that respawns or that an upgrade removes, a scripted bookshelf listed as a container, a master that starts disabled. Save waits while there is an error.
- *Where items go* runs Dova Stash's own planner over a sample haul and colors the containers by how full they get.
- Saving: templates go into a "Dova Stash Templates" mod under MO2, one folder per home (`Templates/<Home>/<Author>.json`, the game reads them all); a home over several cells is one file. The Template panel lists every template of the home (shipped, yours, other authors') to switch between. Save asks for your name when no author is set; *Save as...* writes a copy anywhere.
- The look follows the press kit: black, white and grays with Cinzel and Jost; color only for state and data. `⚙` Setup has *Dark* and *Light* themes inspired by xEdit's (*Export...* writes one to edit) and a *UI size* (Auto picks 125 % on a 1440p and 150 % on a 4K monitor; Ctrl+= / Ctrl+- / Ctrl+0). The window remembers its panels, sizes and view.

### Docs

- The Nexus description, file description, pinned post and templates article are current for this release; the gallery shows "One key" (the hotkey lists) and "Beyond the chests" (displays, mannequins, the workbench).

### Development

- Dova Stash Editor: preset `editor`; decided (EDITOR.md, Decisions): objects are picked on the 2D floor plan and in a Creation Kit style 3D view, and the whole load order loads by default (toggle: home plugin + masters only). `docs/EDITOR-UX.md` specifies the one-workspace UI; EDITOR.md §2 and §9, the user guide's text, BUILDING.md and the editor checklist (TESTING.md §8) describe it.
- Dova Stash Editor: the editor build (preset `editor-release`) copies `DovaStashEditor.exe` and `.com` into the mod's Tools folder itself, and the plugin build no longer does (it could put an older copy over a newer one); the target `DovaStashEditor_deploy` copies it again without a rebuild (`cmake --build --preset editor-release --target DovaStashEditor_deploy`).
- Dova Stash Editor: `ctest --preset editor` (and `cmake --workflow --preset editor`) runs `DovaStashEditorTests` (the BSA, Assets, DDS and NIF readers); the golden test `editor/tests/editor_golden.py` compares `--cell` with `esm_dump.py` on every shipped template's cell and skips cells whose plugin is not in `--data`. Screenshot flags `--home`, `--inspector`, `--palette`, `--popover <id>`, `--window WxH`, `--tab`; screenshot runs no longer change `state.json` (`--state <file>`); `--help` lists every `--popover`, `--colours` and `--layers` value.
- Dova Stash Editor command line: `--render-cell` (with `--roof-cut`) draws a cell to a PNG without a window; `--draft-all` (`--save-to`, `--compare`; a template the game does not offer reads `off: <plugin> loaded`) and `--preview` draft and preview without one; `--theme`, `--text-size`, `--layers` and `--sort` / `--filter` / `--quick` set one run; a typo in `--tab`, `--inspector`, `--popover`, `--colours` or `--layers` exits 2; `--home` prefers an exact cell EditorID, and one that matches no home exits 1. `DovaStashEditor.com` beside the .exe waits for the editor in cmd or PowerShell and passes on its output and exit code. Console and screenshot runs log to `%TEMP%\DovaStashEditor\editor-run.log`, so `editor.log` keeps your last session.
- Editor `--screenshot` runs never hang: without an interactive desktop (session 0) they draw off-screen at the `--window` size and save the same image (a window without `--screenshot` exits 3 there); errors go to stderr and the log (exit 1) instead of a message box, also a run that asks for a home (`--home`, `--cell`, `--mod`, `--drafts`) with no MO2 instance (`error: no home open (...)`, not a start-panel image); `--timeout <s>` (default 180) saves what is there and exits 4; `--help` says so (a `--screenshot` run without a desktop draws off-screen).
- Release zip: `cmake --build --preset plugin-release --target DovaStash_package` (BUILDING.md, Release); `SKSE/`, `Interface/` and `Tools/` are at its top, with no wrapping folder, so it maps onto `Data/`. The plugin deploy warns about mod-folder entries no build writes (`root/`); configuring without `VCPKG_ROOT` says what to set.
- Test runner: `run.ps1 -StallMinutes` (default 10) ends a hung game (no write to `results.json` or `DovaStash.log`, or not responding for 2 min) and says so in the summary and `stopped.txt`; the watchdog uses the same rule; pruning keeps every run younger than 48 h and the newest full run.
- Test harness: plans declare their test templates, `run.ps1 -Only`, extra home mods for the test profile, cross-cell "nothing lost" checks, message boxes left open are reported, and 60 new plans over 29 homes (city, Hearthfire, Creation Club, mod homes). `esm_dump.py cell` lists furniture (with crafting keywords) and doors (with the cell they lead to).
- Test runner: one run at a time on the machine (a lock; `-WaitMinutes`), leftover overwrite files set aside by default (`-NoQuarantine` refuses and lists them), cleanup after an interrupted run, the timeout from `_run.json`, blocked plans counted; pwsh 7 required. `-Build` configures `build/plugin` when needed; the player's MO2 profile is put back after the run.
- Test templates are per plan: at each plan start the plugin loads only the `Templates/AutoTest/` files that plan declares, so plans on a shipped template see it in a full run. New per-test `"expectFail"` (status xfail / xpass), panel checks for `aimed`, `ownChanges` and the catalogue, the `chooseArmorSet` step, and home-scoped steps refuse outside the plan's home.
- One-command builds: `cmake --workflow --preset core-tests` (configure, build, test), `--preset plugin` and `--preset editor`.
- Test harness: steps `bindHotkey`, `panelVisible`, `setCategories`, check `displayHolds`, the panel check's `status` and `menuEntries`' `order` and `body`; plans 18 (hotkey message boxes), 24 and 25 (shipped Hearthfire templates); the Hearthfire and mod-home plans run on the shipped templates. `run.ps1` lists plans left out for a missing plugin as blocked (exit 1 unless `-AllowNotRun`) and says when it guesses the player's profile (`-PlayerProfile`).
- The editor's deploy follows the plugin's rules: `DOVASTASH_DEPLOY_DIR` from the environment, and no deploy (nor stray folders) when the default mods folder does not exist.
- A category pack with a `priority` outside the 32-bit range ranked below the defaults instead of above them.
- The startup log's Environment block names the in-world menus your `iMode` picks (it always said UIExtensions lists when UIExtensions was installed).
- `esm_dump.py check` validates keep blocks and container forms and the new template fields (`displays` with `"kind": "bookshelf"`, `items`, `set` and `"only": true`; `cells`, `conflicts`, `"sell": true`, `"restock": true`, `autoDraft`, `hiddenTemplates`), passes the OCF set (`--kid`), and with `--mo2 <instance> --profile <name>` checks templates against an MO2 profile's mods (TESTING §0). It warns about a display category that can't go on that display and names a category that matches nothing with a did-you-mean hint (also for mistyped category and `cells` fields), and also lists the plugins of a container's fixed items and of keep rules under `requires`. `esm_dump.py cell` lists bookshelves (with how many books each holds), furniture and `reachableFromOtherCells`. JSON Schemas for hand editing in `tools/schema/`.
- A template named `con.txt` or another Windows device name gets a `_` so the file can be written.
- `fRadius` and `fIconSize` set to `nan` or `inf` in the INI fall back to the defaults.
- Test runner: `run.ps1 -Finish` cleans up after a runner that was killed (puts the player's setups and MO2 profile back), and every run starts a watchdog that does it on its own; a guessed player profile stays marked as a guess. Plans left out for a missing plugin run in `<ProfileName> - <suffix>` (`Dova Stash AutoTest - wynter`) right after the main run when that profile exists.
- The plugin build deploys only when the MO2 mods folder (or `DOVASTASH_DEPLOY_DIR`) exists, so it builds without the developer's drive, and mirrors `dist` exactly (files `dist` no longer has are removed).
- Test runner: prints only the tests that did not pass (`-ListAll` prints every test), writes the full summary to `<run>\summary.txt`, sums the main and extra-profile runs on its last line (`all: pass N, ...; exit <code>`) and keeps the newest 20 run folders. Harness: `displayHolds` takes `refs`, `skipIfClosed` applies only to a station the plan's own `enable` turned on, plan 19 checks a template that may not fit, plans 43-46 check display fills, and 3.9.6, 3.9-myrwatch-whole and 3.9g-WinterCove are automated.
- Test runner: from session 0, `run.ps1` hands the run to the desktop session (a one-shot scheduled task) and exits with its code; `-NoHandoff` refuses instead (exit 2). A leftover Steam, or Steam in another session, is refused before the run touches anything. It ends the MO2 and Steam its run started, writes `summary.txt` for every run (also exit 3/4), and copies only logs written by this run.
- Test runner: `run.ps1` ends an AutoTest MO2 or a game's Steam that an earlier run left, instead of waiting on it forever, and writes `summary.txt` for a run that died without results.
- Test harness: `require` takes `"plugin"` (skipped when that plugin is not loaded), `menuEntries` takes `includes` (substrings in order); plans 21, 22, 38 and 14 add 3.9.1-unvisited, 3.9.1-visit, 3.9-heljarchen-border, 3.9-tundra-h2, 6.2.5-tundra-browse and 6.2-lists-armorset. To bisect a hang the debug log has `Cell change (<source>): start` / `done ... after N ms`, `AutoTest: wait`, `AutoTest: console: X returned` and `Overlay: first frame` lines, and the trace log `Task #N <tag>: start` / `done` (TESTING §9.5).
- Test runner: a game that exits by itself mid-run is named in `stopped.txt` and `summary.txt` (exit code, crash log, the Windows event's faulting module, a dump), and the plans with no result yet run once more in a run of their own (`-NoResume`: not); only the newest 5 handoff folders are kept. The AutoTest profile enables Crash Logger when installed. Harness: steps `activate`, `displayUseAll` and `learnMany`; a message box a failed test left open is closed with its last button; `sit` never activates a station whose menu is already open.
- `esm_dump.py --mo2` defaults to `DOVASTASH_MO2`, else the developer's instance, so `check --profile "Dova Stash AutoTest"` works in a fresh shell. A keep entry that matches no category gets a near-miss hint in the template lint.

## [0.2.4] - 2026-10-10

### Added

- **Open** for every container in the SKSE Menu Framework panel. The Dova Stash window closes first.
- **Open** and **Browse containers...** in the message-box menus.
- **Restore original name** in the in-world lists, for renamed containers.

### Fixed

- Cancelling the rename box no longer removes a container's custom name.
- Browse no longer repeats a container's name ("Weapons - Weapons").
- Floating icons keep their slot as you move, instead of reloading their icons.

### Changed

- Quieter debug log: the QuickLoot icon row and the floating icons only log changes.

### Docs

- The Nexus press kit moved to `presskit/` (cmc layout). The description loads its images from this repo's `main` branch, so the URL-filling script is gone.

## [0.2.3] - 2026-10-10

### Fixed

- Containers that a template marks as off-limits (respawning barrels) no longer count as "unavailable" when the template is applied.

### Docs

- The plain-copy rule and its test cases are updated.

## [0.2.2] - 2026-10-10

### Fixed

- Sort on arrival: when the menu pauses the game, items put into the master are sorted when you close it. Only unpaused menus (Skyrim Souls) sort while it is open.
- Respawning containers are accepted only where the area never resets. "Player house" is checked on the cell's own location only.
- Plain-copy moves no longer run while a tempered, enchanted, renamed or worn copy of the same item stays behind. The engine could hand over the special copy.
- Thread safety: the HUD, the activate prompt and QuickLoot read a published copy of the home and settings. The UIExtensions bridge is locked.
- Default keep rules are never written over an unreadable Settings.json, and are saved atomically.

### Changed

- Logging defaults to info again.
- The unused I4 rule loading is gone, so the game starts faster.

## [0.2.1] - 2026-10-10

### Added

- QuickLoot IE shows one category icon per bound category in the bottom-right corner. The master shows "Master".
- The settings page has hover help naming each INI key. Unavailable options are greyed out, and there is a reset to defaults.

### Fixed

- The activate prompt label sits on the name line.
- Floating icons no longer blink. They are now off by default.

## [0.2.0] - 2026-10-10

### Added

- QuickLoot IE integration.
- UIExtensions list menus (any skin), with in-game rename.
- Every setting is editable in game without a restart.
- Keep rules for every home.
- Sort on arrival.
- Activate-prompt roles.
- Floating icons.

## [0.1.0] - 2026-10-10

First public release (release candidate).

### Added

- A master container per home, with Deposit, Distribute on master close, Sort all, and Retrieve (everything or by category).
- 37 keyword-driven default categories in JSON (`Categories/00_Default.json`). Add-on category packs load in name order.
- Learned destinations: placing an item in a container by hand makes that container its destination.
- 22 home templates: Breezehome, Honeyside, Hjerim, Proudspire Manor, Vlindrel Hall, Severin Manor, Lakeview Manor, Heljarchen Hall and Windstad Manor (each Hearthfire house with its cellar), plus 10 Creation Club homes.
- A template notice on entering a home, with optional auto-enable (`bAutoEnableTemplates`). Re-applying a template keeps learned destinations, keep rules, ignored containers and containers you added yourself. A timestamped backup is written first.
- Keep rules: built-in defaults (equipped, favorited, quest, gold, lockpicks, torches), an optional global `Settings.json`, and per-home always/never rules and quotas.
- An SKSE Menu Framework panel (SE/AE).
- A native message-box interface, used as the fallback and on VR.
- Safety checks: ownership, respawning and disabled containers, warnings for scripted containers, extra data kept on every move, layout backups, and broken layout files never overwritten.
- One DLL for SE 1.5.97, AE 1.6.x and VR (VR is experimental). No ESP.
