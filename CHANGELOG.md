# Changelog

## [Unreleased]

### Added

- Restock: tick *Restock* on a keep quota ("10 health potions", "50 steel arrows", "5 lockpicks") and every Deposit tops you back up to that number from the home's containers, the most valuable copies first, never more. *Restock* is also in the hotkey menu and the panel on its own. HUD: "Dova Stash: deposited 12 items into 3 containers, restocked 10". Setting: "Restock after Deposit" (on).
- Homes with their own sorting system are left alone, so the two never move the same items: Clockwork, Legacy of the Dragonborn, Mirele Bismath Reborn, LUFTSHIP K747, Skyfall Estate, Riverside Lodge, The Hoarder's Chest - Rebuilt, The Dovah Den, the five city homes with Player houses overhaul, and Tundra Homestead with Tundra Homestead - LotD Storage. With Carpenter Storage Chests for Hearthfire only its building-supply chests are left out.
- Homes another mod rebuilt: the Breezehome template steps aside for Wynter's Breezehome Redux, the city homes' for Player houses overhaul. For other mods a template is set up on its own only when most of what it names is still there; otherwise the HUD says "Breezehome setup may not fit (press K)" and the hotkey menu still offers it.
- A display's Categories only offer what it can hold: a weapon rack the weapon categories, a mannequin armor, a bookshelf books; never soul gems, potions or misc items. A category that can't go on a display puts nothing on it (the log says so once).
- Sell chest: press the hotkey at a container and choose "Make sell chest..." to turn your junk into gold (Clutter goes there to start with). Nothing is sold until you choose "Sell now (862 gold)..." and confirm the total, at a merchant's price; stolen, improved (tempered, enchanted by you, renamed), quest, worn or favorited items, soul gems you filled yourself, potions you made and what your keep rules keep never sell. "Buy back" returns your last sale of the session. Settings: "Price" (100%) and "Sell by itself after" (off).
- Displays: look at a weapon rack, mannequin or item plaque (dragon priest masks, claws, Elder Scrolls...) and press the hotkey to use it and pick its Categories. Deposit and Sort all then keep it filled as if placed by hand: racks get your best weapons, mannequins a matching armor set, a plaque its own item, the Creation Club homes' plaques included. Nothing is taken off a display. Setting: "Put gear on the home's displays".
- Bookshelves are displays: Deposit puts your books on one, as many as it holds and never more: skill books first, then spell tomes, then the most valuable, one copy of each title; never an Elder Scroll, notes only when you pick them. A book that drops off a shelf whose marker lies outside it (Niflholm) goes back to its container, and that shelf is left alone for the session.
- Materials at the workbench: at a forge, smelter, tanning rack, cooking pot, alchemy lab or modded station in a home you set up, you craft from what the home's containers hold (`Iron Ingot (12/1)`); each craft takes only what you lack, and nothing moves when you sit down. Enchanters bring your filled soul gems. Tempered, enchanted, favorited and quest items are never used. Setting: "Materials at the workbench": Auto, Off, Move or Craft from storage; Auto steps aside for another crafting-from-storage mod and says why (on VR it means Move).
- Mannequins wear your most complete matching armor set, modded armor included. To pick one, press the hotkey at a mannequin: "Armor set... (Auto)" lists the sets you have ("Ebony (3/4)"); the panel's Armor set searches every set in your load order.
- SKSE Menu Framework "Displays" page: every rack, mannequin, plaque and display case of the home, with what it takes and holds. Filter, search and sort; Use, Categories, Armor set, Fill now, or "Use all 12 racks..." in one go.
- Homes that span several cells: walk out of the home into the next area (cellar, grounds) and press the hotkey: "Add this area to Lakeview Manor", with a name if you like. The forge outside then crafts from what is stored inside, and Deposit, Sort all and Retrieve use the containers of every area the game keeps loaded; displays are filled in the area you are in. Lakeview Manor, Windstad Manor, Heljarchen Hall, Tundra Homestead and Myrwatch now have one template for the whole home, cellar, gallery and grounds included; the separate cellar and gallery templates are gone. A cellar set up by its old template joins the home when you apply the new one. In the Tundra Homestead cellar and the Myrwatch gallery, Deposit puts what has no container there in that area's own chest.
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
- A hotkey list could stay open but invisible (input locked until Esc) when an entry that keeps the list open was chosen within the first half second, while the list was still being centred.
- Pressing the hotkey again while a list is opening no longer opens a message box on top of it.
- A container or item name ending in `;` dropped its row from the hotkey lists.
- Hand-edited setups and templates: a container listed twice uses the first entry only, and a mistyped field is kept as written instead of being reset.
- With "Set up homes from a template on first visit" on, the notice on entering a home says what to do: "Dova Stash: Breezehome is set up (press K)" (it used to say only that the setup was enabled).
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
- Deposit says how many items stay with you because their area is not loaded ("(3 items left: master not here)"). When none moved it says "nothing deposited (3 items left: master not here)" (or "no container here takes them") instead of "nothing to deposit".
- A hotkey list that never comes up (UIExtensions not loading, another menu in the way) now says "Dova Stash: the list menu did not open (see DovaStash.log)" instead of nothing.
- A setting that cannot be saved says so in red on the Settings page ("Settings not saved (see DovaStash.log)").
- The "E is also Activate." warning under a hotkey also shows after a restart.
- Owners are shown by name, never by form ID ("not a player home (owned by WLightHouseFaction)", else "a faction").
- A typo in a keep rule ("restok"), Settings.json's default keep rules included, a category's match field or an enum value is reported with the one it may be ("did you mean"); a wrong category value (`"weaponTypes": "Sword"`) lists the valid ones.
- Home backups retry when a sync client such as OneDrive holds the file.

### Changed

- The hotkey is set by pressing it: on the Settings page, click the keyboard or gamepad hotkey and press the key, mouse button or controller button you want; it is saved at once. Clear turns the gamepad hotkey off. Esc closes the window without changing anything; the Menu Framework's own key (F1, LB) can be bound too. A key that is also a game control or the Menu Framework's own key gets a warning ("E is also Activate.").
- The SKSE Menu Framework panel has four pages, This home (with templates), Displays (with armor sets), Settings and About (with diagnostics), and they read and behave the same everywhere: one dimmed intro line per page, tooltips that say what a control does (also on greyed-out ones), "..." on every button that asks first, small buttons on list rows, empty states in the same dimmed style, and the actions that reset or remove things at the bottom; Sell now, Buy back and Make sell chest... ask first like every other such button. Settings are grouped into Hotkey, Menus, Sorting, Learning, Crafting, Templates, Sell chest, QuickLoot IE, In the world and Log, with shorter labels; the global keep rules are called "Default keep rules" everywhere.
- The panel's Template tab button is now "Save" (was "Export"), under a "Save as template" heading.
- Homes with a template are set up as soon as you walk in: "Set up homes from a template on first visit" (`bAutoEnableTemplates`) is now on by default. Turn it off to get the "setup available" notice instead. An INI you already have keeps its value.
- The hotkey at a bookshelf opens its display menu instead of the container menu; one your setup already stores items in keeps its container menu.
- Plainer words in the game: the HUD, menus and panel say "setup" instead of "layout" ("Breezehome setup enabled", "Custom setup (not from a template)", "this home's setup file is broken"). File names and the template format are unchanged (`Layouts/`, `dovastash-layout-1`).
- The panel's Template tab saves the same way as the hotkey: the template is offered right away (no restart), the author defaults to your character and the version is automatic, and overwriting backs up the old file.
- Your own saved templates come first for their home (ahead of shipped ones of the same priority), so the hotkey and the first-visit notice offer them.
- A template the editor's *Draft all homes* saved, that nobody has saved since, comes after every template someone made for the same home, whatever its priority or version: the hotkey, the first-visit notice and "Set up homes from a template on first visit" use it only in a home that has no other template. Applying one gives a normal setup.
- The hotkey menu says Re-apply template only for the template the home's setup came from; another template reads Use template.
- Floating icons show a container's full category row, like QuickLoot's bottom-right row: up to five icons, then "+N".
- Every hotkey list starts with a header naming the menu ("-- Whitepeak Tower --", "-- Chest: Categories --"), with the Dova Stash logo above it in your list skin's text colour. Choosing the header does nothing.
- Hotkey lists show category icons from your own SkyUI icons: the category picker, the display picker, Retrieve, Browse containers (each container's icon) and a container's Categories... entry.
- Ticking a category in the hotkey lists updates the list in place, keeping your place in it, instead of closing and reopening the menu.
- A shorter hotkey menu for the home: Deposit, Restock, Sort all, then Advanced..., which holds Retrieve, Browse containers, Use / Re-apply template, Save as template... and Reset home..., with Open panel... for the SKSE Menu Framework panel. Message boxes offer Rename and Restore original name too (with UIExtensions installed).
- In the hotkey menus, Clear categories... and Make master... ask first too, with the panel's questions (each ends "No items are moved."), and No on any question (Sell now's and Buy back's included) goes back to the menu it came from; cancelling the Save as template name goes back to Advanced.... Message boxes name their menu ("Dova Stash: set up Breezehome?", "Dova Stash: Chest: Categories").
- Template labels never name a display after itself and spread repeats over nearby landmarks.
- Use template asks first only when it would replace changes of your own, or when the template may not fit (the entry then ends "(may not fit)"). The question is the same in the hotkey menus and the panel.
- The setup prompt and a container's list end with Open panel... (with the SKSE Menu Framework) instead of an Advanced... that did nothing.
- Shorter notices: "Breezehome is set up (press K)", "setup available (press K)", "setup may not fit (press K)"; a learn reads "Iron Sword now goes to End Table. K: undo"; every hint at the log reads "(see DovaStash.log)". Refusals are short too ("can't use Barrel (respawns, items would be lost)", "owned by X", "only reachable in X"); the log gives the long form. Reset and area notices name the home by its setup ("Tel Jerdein reset (setup backed up)").
- The panel's status line is at the top of the page and belongs to the page whose action posted it (This home, Displays or Settings) and the home it was set in; it is red only when the action failed, and stays in view while the page scrolls (on Settings too).
- Keep rules have Add rule, Add category and Add item pickers (Add item with a search box); text that is not a keep selector is not added. They show item names instead of form refs, and the third list is now "Quotas".
- Panel: the Displays page marks and selects the display you look at; the "Templates" header is shorter; Save as template asks Yes / No before replacing a template. The message-box armor-set picker shows at most 3 pages.
- Hearthfire homes use the furniture of whichever wing you build. Breezehome and Vlindrel Hall: the child's bedroom takes the alchemy items when it replaces the alchemy corner. Arch-Mage's Quarters uses every safe container. Labels in one form: containers and displays are named after where they stand ("Bookshelf (by the master chest, 2)"). Myrwatch's gallery armory cupboard takes all weapons.
- A Welkynd Stone is a curio (new category Curios), not a potion Restock hands out. Curios go with the gems. Torches and modded lanterns have their own category, Torches (the default keep rules still keep your torches); the second "Books" category is now "Other books".
- The Displays page says "No display matches." when its filters match nothing, and the set picker "No set matches.".
- The learned-items menu's first entry reads "Forget 1 learned item..." when there is one ("Forget all 3 learned items..." otherwise), in both hotkey UIs.
- Wuth Rein template 1.0.2: its scripted bookshelves are no longer displays (a book put on one could vanish); books go to the chest by the shelves.
- Sicarius' Refuge template 1.0.2: the mannequins `0x0016CC2D` and `0x0016CC2E`, which the mod keeps disabled, are no longer listed; "Armour storage" is now "Armor storage".
- Antennaria, Tel Jerdein, Lost Hall and Secret Cellar templates 1.0.1 bind more of their furniture (Antennaria 16 containers, Tel Jerdein 15, Lost Hall 8, Secret Cellar 8); Lost Hall and Secret Cellar say in their note what still goes to the master.
- Clearer template labels: Antennaria ("Jewelry chest", the respawning pack is "Large sack"), Tel Jerdein's root cellar shelves, Tundra Homestead's cellar bookcases and Niflholm's ignored containers named by landmarks (no clutter on the cellar's jewelry bookcase), "Armor" spelling in Pinegrove Lodge and Ebongrove.
- Winter Cove binds every category (ammo, soul gems, gems, tools, Dwemer parts, animal parts, clutter) and uses the main hall's soul gem container, wardrobe and end table while you are there. Notes in Goldenhills, Honeyside, Myrwatch and its gallery say why some categories go to the master.
- Unbuilt furniture is listed once when a template is used, no longer as a warning on every Deposit.
- New panel tooltips; a learned row's button reads Forget; the Icon size reads in px ("32 px").

### Template authors

- Templates can now name a home's displays, extra areas, sell chest and Restock quotas, and step aside for mods that rebuild the home; a file in `SelfSorting/` adds a home to those left alone (details in `tools/README.md` and the templates article).
- Saved templates list the plugins of the displays they use in `requires`, so they pass the template check (not the plugins of display items or mannequin sets: those are optional).
- The editor's *Draft all homes* marks its files `"autoDraft": true`; saving one by hand takes the mark away.
- `esm_dump.py check` accepts `//` and `/* */` comments in templates, as the game does.
- Templates and saved layouts are easier to read and edit by hand: one line per container with the columns lined up, and short names. The reason a container is left alone is in a separate `note` (shown in the panel under the container's name) instead of filling its name.
- A template's `cells` entry can name the area's own `master`, used there while the home's master is out of reach, and `replaces` lists retired templates it takes over (an untouched setup from one of them joins). `esm_dump.py check` fails a cell that is in two shipped templates.

### Dova Stash Editor (in development)

- Dova Stash Editor (in development, `editor/`, design in `docs/EDITOR.md`): a standalone program that reads your MO2 mod list (names, Nexus mod ids, plugins), finds the player homes in a mod's plugins, draws each cell's floor plan with its containers, racks, mannequins, plaques and doors, and drafts a template. It ships inside the main zip; under MO2 it saves templates into a "Dova Stash Templates" mod by default; a home over several cells is one template file.
- Dova Stash Editor: a Creation Kit style 3D view of the home (meshes and textures from your mods and archives, exterior terrain and the cells around it, fly camera, select mode shared with the floor plan), a role editor (master, categories, ignored, displays) and saving into a "Dova Stash Templates" mod; the home list stays beside the home while you go from one to the next; plaques and display cases are the ones Dova Stash fills, read from the home's compiled scripts (a dining table that sets out a meal is no display); a mannequin's armor set is picked from every set of the load order (searchable, as the in-game picker), and "Where items go" fills it with that set; `--render-cell` draws a cell to a PNG without a window. How to try it: `docs/EDITOR.md` §9.
- Dova Stash Editor: a start page with your homes and their to-do stage, "Draft all homes" (every home of your load order with a draft template: the master and each container's categories suggested from its name, the stations and items around it, with a confidence and the reason; a container its author labelled ("Soul Gems", "Dragonpriest Masks") gets only what its label says and is the master only when nothing plain is usable; accept or reject each, then save), a "Where items go" preview that runs Dova Stash's own planner over a sample haul and colours the containers by fill, paint mode, multi-select and undo/redo; `--draft-all` and `--preview` do the same without a window (`docs/EDITOR.md` §3.4-3.7).
- Dova Stash Editor: the home list shows every player home of your load order under the mod that adds it (filter by home, mod name or Nexus id; each home with its storage and template), one click to open, instead of picking a mod and then its cells. The old way stays as *All cells...* under each mod, for a home the finder misses.
- Dova Stash Editor: one window instead of three modes. Your homes stay in a rail on the left (one search box for home, mod, plugin or Nexus id, a filter, each home with its storage and a chip: yours, shipped, auto-draft, draft, none; *All cells...* under a mod for a home the finder misses), the home in 3D or as a plan in the middle with a small floating toolbar (Select, Paint, Layers, Colours, Camera), and on the right the inspector: Selection, Template, Draft and Checks. The wide tables (Containers, Displays, Doors, Where items go) are a drawer under the home; a status row shows the home, errors and warnings, and what is loading. The start page is now a panel in the middle while no home is open.
- Dova Stash Editor: Ctrl+K opens a command palette: type to find a command, a home, a container of the open home, or (while painting) a category. Every toolbar button has a key (EDITOR-UX.md §4); Ctrl+1/2/3 fold the rail, drawer and inspector, and the window remembers their sizes and the view.
- Dova Stash Editor: a draft is reviewed in the Draft panel of the open home, and it edits the template directly: accepting a suggestion is the category in the Selection panel, the checks and Where items go at once, and undo covers it. Opening a home from the rail starts from its draft, with every cell the draft found.
- Dova Stash Editor: Draft all homes (Ctrl+D) runs in the background with its progress in the status row, then marks each home *drafted* and offers *Save N drafts...* over the rail. It no longer moves every home to "drafted" on the start panel: a home's stage starts when you open it.
- Dova Stash Editor: switching homes keeps each home's unsaved draft (the dot stays on its row); *Save N drafts...* and closing the window cover them.
- Dova Stash Editor: keep rules got an editor in the Template panel (always keep, never keep, quotas, the player's own rules on or off). *Save as...* writes a copy anywhere; Save waits while the checks find an error.
- Dova Stash Editor: the Checks panel lists problems errors first (click one to select the object) and what the template still lacks, such as containers without a role. S in paint mode returns to Select.
- Dova Stash Editor: the window follows the press kit's brand: black, white and neutral grays only, Cinzel and Jost (built in), drawn icons, the selected object's own mesh as a small picture, the wordmark and titles in tracked Cinzel capitals, the emblem as the only ornament, no game texture behind the start panel. Colour is kept for state and data: the pills (drafted, shipped, yours, saved, confidence), the status dots, the viewport's categories, fill and see-through boxes. *Draft all homes* and *Save N drafts...* live in the *Draft* menu only.
- Dova Stash Editor: templates are saved one folder per home, `Templates/<Home>/<Author>.json`, so every template of a home sits together (the game reads `Templates/**`, so nothing changes for it). The Template panel starts with *This home's templates*: every template the home has (shipped, yours, other authors') with author, version and the mod it comes from, the open one marked; click another to open that file. The rail says how many templates a home has.
- Dova Stash Editor: *Save N drafts...* and `--draft-all --save-to` save auto-drafts: each file is marked `"autoDraft": true` and only ever replaces that home's own earlier auto-draft. A template someone made (hand-made, downloaded, or an auto-draft saved since) is never overwritten; the homes left alone are listed by name in the dialog and get a *not saved* line on the command line, as does a second home with the same name. A draft you reviewed first is saved as yours. Open an auto-draft and Save (Ctrl+S) to make it yours: the mark goes and no bulk draft touches it again. The rail, the title row and the Template panel show an *auto-draft* chip (DESIGN §6.8).
- Dova Stash Editor: a *UI size* slider under `⚙` Setup (100 to 250 %; Ctrl+= / Ctrl+- step 10 %, Ctrl+0 is Auto; remembered). *Auto* picks 125 % on a 1440p and 150 % on a 4K monitor at 100 % Windows scaling, where the text was too small to read; moving the window to a monitor with another DPI rescales it.
- Dova Stash Editor: colour themes under `⚙` Setup › *Theme*, for anyone who finds the black and white hard to read. *Dova Stash* (the press kit's look) stays the default; *Dark* and *Light* palettes named after and inspired by xEdit's UI themes (Windows10 Dark, Carbon, Charcoal Dark Slate, Tablet Dark, Cyan Night, Golden Graphite, Ruby Graphite; Windows10, Light, Tablet Light, Aqua Light Slate, Lavender Classico, Emerald Light Slate) apply on pick and are remembered. *Export...* writes the theme to a JSON file in `%LOCALAPPDATA%\DovaStashEditor\Themes` to edit; your files show under *Yours*. Also in the command palette, and `--theme <name>` for one run.
- Dova Stash Editor: the camera popover's sliders match the rest of the window; the inspector keeps a strip for its collapse chevron instead of scrolling under it; a template saved into the mod picked under *Save to* counts as yours in the rail and the title row.
- Dova Stash Editor: the rail's filter button keeps its count badge in view. With a filter on, the badge stood past the rail's edge and only its left half showed; the search box now leaves room for the button and its badge at any rail width and UI size.
- Dova Stash Editor: a double click on the drawer's tabs no longer folds it. A click on the open tab folded the drawer at once, so a double click on it (or on another tab, the first click switching to it) ended folded. The open tab now folds it only when no second click follows, and the second click of a double click that opens or folds the drawer, the rail or the inspector no longer reaches what moved under the cursor (a table row, the 3D view).
- Dova Stash Editor: the drawer's tables sort by any column (click a header; Shift+click adds a column; a third click returns to the table's order) and filter: a *Filter rows* box and quick filters in the tab row (Containers: Master, With categories, No categories, Ignored, Not listed, Respawning, and a new Kind column and list: chest, sack, barrel, satchel...; Displays: their kinds, Used, Unused; Where items go: Master, Receives items, Receives nothing), "N of M" when rows are hidden, kept per table for the session. Doors lost its *Open* column: double-click a row to open that cell. Command line: `--sort table:column[:desc]`, `--filter <text>`, `--quick <chip>`.
- Dova Stash Editor: the plan's *Effects* and *Other* layers work. Fog, mist, light beams and lights were drawn as structure, and trigger volumes (Hearthfire's crafting triggers, `defaultBlankTrigger`) as furniture, so large faint boxes covered the plan whatever was ticked. One rule now sorts references into the layers for the plan and the 3D view; the plan starts with *Effects* and *Other* off, does not fit itself to them, and a hidden layer can no longer be hovered or clicked. `--layers effects,-structure` sets the layers for one run.
- Dova Stash Editor: larger text: body 18 px (was 15), so it reads as large as the form refs beside it; rows, inputs, chips and bars grow with it.
- Dova Stash Editor: the UI size slider in Setup applies again: drag it or click its track and let go, or roll the wheel over it (before, only Ctrl+= / Ctrl+- / Ctrl+0 changed the size).
- Dova Stash Editor: a home's template status is one word everywhere (rail, title row, Template panel): *yours*, *shipped*, *auto-draft*, *draft*, or a quiet *none*; *saved* is now *yours*. The cryptic "+1" is gone: the rail says "2 cells · 3 templates" in words, and the title row has a *2 templates ▾* list to switch between them.
- Dova Stash Editor: a home opens on its template, the one the game offers first, instead of a fresh draft. The draft is in the Draft panel under *Use the draft*; the title chip reads *draft* whenever the draft is open.
- Dova Stash Editor: the 3D view of an interior has a roof cut. Above the highest container (+120) only containers and displays are drawn, so caves such as Wuth Rein show their rooms instead of the rock. *Layers > Roof* brings the roof back.
- Dova Stash Editor: the 3D overview cuts the roof per area, about 2 m above each area's floor, so a cave below the built rooms opens from above too (Wuth Rein); `--render-cell --roof-cut` draws the same.
- Dova Stash Editor: `DovaStashEditor.com` next to the .exe. Typed as `DovaStashEditor` in cmd or PowerShell, it waits for the editor and passes on its output and exit code.
- Dova Stash Editor: a container with the respawn flag in a cell that never resets is usable in drafts and counts, as in game.
- Dova Stash Editor: tables show category names (Swords, Books (all)), scripts as words (bookshelf, Hearthfire build) and *Height* above the lowest container; the ids are in the tooltips.
- Dova Stash Editor: Save asks for your name when no author is set (no more `Unknown.json`).
- Dova Stash Editor: the drawer's height follows the window size, the view keeps its room at 1280x720, and the view toolbar shows icons when the view is narrow.
- Dova Stash Editor: the rail scrolls to the opened home. Long names in *In progress* end in "...". The Draft panel names each template by its word and mod. The missing-file tooltip uses correct plurals and names the mesh and mod behind a missing texture.
- Dova Stash Editor: textures no longer turn gray after a while when another program is started from MO2 (such as a test run with another profile); the status row now tells missing files from unreadable ones.
- Dova Stash Editor: the home overview frames the home itself. Initially disabled prefabs stored far outside the rooms no longer shrink Breezehome to a speck, and the roof cut counts only containers that are there from the start.
- Dova Stash Editor: Lux's shadow planes and other black cards (`textures\black.dds`), and shapes with material alpha 0, are no longer drawn as black slabs over the rooms.
- Dova Stash Editor: the title row says *draft* whenever the editor's draft is open, matching the rail. The Displays table's column is now *Used*.
- Dova Stash Editor: a typo in `--tab`, `--inspector`, `--popover`, `--colours` or `--layers` is an error (exit 2), and a `--home` that matches no home exits 1 after its screenshot.
- Dova Stash Editor: the Containers tab lists the same containers its badge counts (displays such as bookshelves stay in the Displays tab).
- Dova Stash Editor: the command palette offers Accept, Reject and Draft again only while a draft is open, and leaves out "Save 0 drafts".
- Dova Stash Editor: every missing mesh and texture is logged (meshes first), and clicking "N missing" copies the list.
- Dova Stash Editor: *Save N drafts* asks for your name when none is set, instead of saving under "Unknown".
- Dova Stash Editor: Draft panel rows with the same name show their height ("Barrel · 1.3 m"), and hovering a row highlights the container in 3D.
- Dova Stash Editor: the in-game features have their editor side too: a keep quota's Restock, the Template panel's Conflicts field, a sell chest mark, and a display's categories offered as in game. The panel's About page says where the editor is installed.
- Dova Stash Editor: your settings, author, MO2 instance and home progress are kept between runs again (state.json was never written since the last change).
- Dova Stash Editor: a template switched off by a loaded mod that rebuilds its home (`conflicts`) reads *off* in the title and the home list, and Checks says "Not offered in game: <plugin> rebuilds this home".
- Dova Stash Editor: heights count from the home's floor, ignoring a container a home overhaul parked far below (it reads "-", *out of the home*). A container has one name everywhere: its template label ("Barrel (by the grindstone)") first.
- Dova Stash Editor: missing wall and clutter meshes are see-through boxes that clicks pass through (Layers > Missing meshes), so they no longer hide the rooms.
- Dova Stash Editor: the first-run card takes your MO2 instance or a Skyrim Data folder (Vortex or plain installs) and explains itself instead of showing command-line flags in red.
- Dova Stash Editor: Save N drafts skips homes with nothing to sort into and saves a second home of the same name as "Name (CellEditorID)" instead of leaving it out. The banner and the dialog count the same.
- Dova Stash Editor: "N containers not listed" in Checks is a link to the Containers tab's Not listed filter (upgrades counted apart, a quieter heading for shipped templates). Mod groups read "Nexus 65088 · 2 homes", and homes with no containers are hidden behind a filter.
- Dova Stash Editor: the draft's master reason gives sizes in metres ("Chest: named like a chest, 0.6 x 1.1 x 0.6 m, 6.7 m from the door to Skyrim").
- Dova Stash Editor: the Checks panel now runs the game-data checks of `esm_dump.py check`: a reference that is not in the home's cells, is not a container or is deleted, a bound container that respawns, the respawn flag in a cell that never resets, a container an upgrade removes, a scripted bookshelf listed as a container, and a master that starts disabled.
- Dova Stash Editor: heights in the Containers and Displays tables are in metres (*Height (m)*), the same unit as the Draft panel, and a single container left alone below the rest no longer sets the floor.
- Dova Stash Editor: one container count everywhere: the rail, the drawer tabs (`252 of 297` when the tab lists more) and `--draft-all` (*usable containers*) count containers that do not respawn, belong to nobody else and are not removed by an upgrade.
- Dova Stash Editor: `--draft-all` marks templates the game does not offer (`off: <plugin> loaded`), and `--compare` skips them.
- Dova Stash Editor: boxes for missing walls and clutter are off by default. Click the status row's *N missing* to show them; right-click copies the list.
- Dova Stash Editor: `--text-size <percent|auto>` sets the text size for one run, screenshots included.
- Dova Stash Editor: *All cells...* moved to a mod group's right-click menu, plus a link under the open home's group only.
- Dova Stash Editor: the *Save N drafts* dialog uses one slash style in the folder path and names only counts that are not zero.
- Dova Stash Editor: the title row's template count and list match the rail's (every cell of the home); a cellar's templates are listed under *Cellar*.
- Dova Stash Editor: one container count per home: the drawer shows `252 of 253` (this cell of the home) and the Draft panel counts accepted containers out of the home's usable ones.
- Dova Stash Editor: Checks lines name the container (its form ref in the tooltip) and no longer start with `template: containers[N]`; a respawning container reads *respawns: never bind it*.
- Dova Stash Editor: `--compare` counts a template-ignored bookshelf that the draft lists as a display as agreeing. Draft suggestions break ties by the container's own name, then the container sure of fewer other categories, then the nearer station.
- Dova Stash Editor: Save is the highlighted button only when there is something to save; an unchanged shipped template offers *Save a copy*.
- Dova Stash Editor: command-line paths print with Windows separators; search box hints shorten instead of being cut mid-word; dialogs dim the window behind them.
- Dova Stash Editor: a home a template names is always in the home list, even when a later plugin drops its location or name (Myrwatch under the Unofficial Creation Club patch is one home again, tower and gallery, under its own name). A home owned by its mod's own faction, the one you join when you buy it, is found too (White Lighthouse).
- Dova Stash Editor: drafts leave out containers only a quest's script enables and scripted weapon mounts; the plan fits the home, not distant clusters; the Containers table opens with the master and the containers that have categories first; a narrow drawer folds its quick filters into *Filters* ▾; long home names keep their room in the rail; the Template panel's Save matches the title row's; notes wrap; the status row says what is missing ("7 textures missing"); a template another mod switches off can still be saved as a copy (its reference checks become information).
- Dova Stash Editor: console commands and screenshot runs log to `%TEMP%\DovaStashEditor\editor-run.log`, so your own editor.log keeps your last session.

### Docs

- The Nexus description, file description, pinned post and templates article are current for this release; the gallery shows "One key" (the hotkey lists) and "Beyond the chests" (displays, mannequins, the workbench).

### Development

- Dova Stash Editor: preset `editor`; decided (EDITOR.md, Decisions): objects are picked on the 2D floor plan and in a Creation Kit style 3D view, and the whole load order loads by default (toggle: home plugin + masters only). `docs/EDITOR-UX.md` specifies the one-workspace UI; EDITOR.md §2 and §9, the user guide's text, BUILDING.md and the editor checklist (TESTING.md §8) describe it.
- Dova Stash Editor: the editor build (preset `editor-release`) copies `DovaStashEditor.exe` and `.com` into the mod's Tools folder itself, and the plugin build no longer does (it could put an older copy over a newer one).
- Dova Stash Editor: `ctest --preset editor` (and `cmake --workflow --preset editor`) runs `DovaStashEditorTests` (the BSA, Assets, DDS and NIF readers); the golden test `editor/tests/editor_golden.py` compares `--cell` with `esm_dump.py` on every shipped template's cell and skips cells whose plugin is not in `--data`. Screenshot flags `--home`, `--inspector`, `--palette`, `--popover <id>`, `--window WxH`, `--tab`; screenshot runs no longer change `state.json` (`--state <file>`); `--help` lists every `--popover`, `--colours` and `--layers` value.
- Release zip: `cmake --build --preset plugin-release --target DovaStash_package` (BUILDING.md, Release). The plugin deploy warns about mod-folder entries no build writes (`root/`); configuring without `VCPKG_ROOT` says what to set.
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
- New target `DovaStashEditor_deploy` copies the editor into the MO2 mod's Tools folder again: `cmake --build --preset editor-release --target DovaStashEditor_deploy`.
- Test runner: `run.ps1 -Finish` cleans up after a runner that was killed (puts the player's setups and MO2 profile back), and every run starts a watchdog that does it on its own; a guessed player profile stays marked as a guess. Plans left out for a missing plugin run in `<ProfileName> - <suffix>` (`Dova Stash AutoTest - wynter`) right after the main run when that profile exists.
- The plugin build deploys only when the MO2 mods folder (or `DOVASTASH_DEPLOY_DIR`) exists, so it builds without the developer's drive, and mirrors `dist` exactly (files `dist` no longer has are removed).
- Test runner: prints only the tests that did not pass (`-ListAll` prints every test), writes the full summary to `<run>\summary.txt`, sums the main and extra-profile runs on its last line (`all: pass N, ...; exit <code>`) and keeps the newest 20 run folders. Harness: `displayHolds` takes `refs`, `skipIfClosed` applies only to a station the plan's own `enable` turned on, plan 19 checks a template that may not fit, plans 43-46 check display fills, and 3.9.6, 3.9-myrwatch-whole and 3.9g-WinterCove are automated.
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
