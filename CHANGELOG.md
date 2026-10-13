# Changelog

## [Unreleased]

### Added

- Displays: look at a weapon rack, mannequin, or a plaque made for one item (dragon priest masks, dragon claws, Elder Scrolls...) and press the hotkey to use it, and pick what it takes under Categories, like a container (a rack for bows only, a mannequin for light armor). Deposit and Sort all then keep it filled, as if you had placed it by hand: racks get your best weapons (one of each), coat-of-arms racks a shield, the Unofficial Patch's glass display cases a weapon, mannequins a matching armor set, and a plaque only its own item (written into the home's setup when you turn it on). Weapons go up the way the game's own racks place them, on the same spot (the Unofficial Patch's racks too, with their crossbow, staff and shield spots; what they turn away, such as a staff too long for a display case, stays off), so you take them down as usual; they just appear on the rack, never seen falling or floating on the way. Works in any home from the displays' own scripts; templates can turn displays on and pick their items (`displays`). Nothing is taken off a display. Setting: "Put gear on the home's displays".
- Materials at the workbench: sit down at a forge, smelter, tanning rack, sharpening wheel, armor table, cooking pot, alchemy lab, enchanter or any modded station in a home you set up, and the materials stored there that its recipes use come to you (an alchemy lab brings your ingredients, an enchanter your filled soul gems). When you leave the menu, what you didn't use goes back to the container it came from. What you just made stays with you, even ingots you smelted while ingots were brought, and so do the copies you carried before. The recipes are read from your load order, so modded materials and recipes work without a patch. Tempered, enchanted, favorited and quest items never move; weapons and armor that recipes use up (breakdown recipes) only with "Weapons and armor used as materials too". Setting: "Materials at the workbench" (on).
- Mannequins: by default each wears your most complete matching armor set, modded armor included: sets are worked out from the plugin, material keyword and piece names ("Valkyrie Cuirass", "Valkyrie Boots"), and for odd mods from what tempers the pieces. To pick one, press the hotkey at a mannequin and open "Armor set... (Auto)": the sets you have, with how much of each, e.g. "Ebony (3/4)". The mannequin then wears only that set and fills up as you get the pieces; nothing is taken off it. The SKSE Menu Framework has an "Armor sets" page: search every armor set in your load order and assign one to a mannequin, even before you own a piece. Layouts store it as `set` (any piece of the set); `esm_dump.py check` validates it.
- Templates pre-define displays: a template can list exactly which racks, mannequins, plaques and display cases a home uses, and for racks and mannequins what goes on them: categories, preferred `items`, a mannequin's armor `set`, and the new `"only": true` (nothing but those items, which no other display takes, e.g. a rack kept for Dawnbreaker). Items and sets may come from other mods without listing them in `requires`: in a game without the mod they are skipped with one line in the log ("Template My Home: Sword rack: 1 item doesn't exist in this load order (...); skipped") and the rest of the template works. Plaques and cases (dragon priest masks, claws, Elder Scrolls) always keep what the home's own script made them for; a template's items for them are not used. One documented entry shape for layouts and templates, checked by the template lint and `esm_dump.py check` (fields a display doesn't use, `only` without items). Existing layouts and templates load and work exactly as before.
- Homes that span several cells: a cellar, an upper floor or the grounds outside (the forge, smelter and tanning rack of a Hearthfire home) can be part of the same home. Walk out of the home into the next area and press the hotkey: "Add this area to Lakeview Manor". From then on you are in the home there too: the forge outside brings the materials stored inside and puts the rest back, and Deposit, Sort all and Retrieve use the containers of every area. Containers in another area are used only when the game keeps them in memory (persistent references; most Hearthfire chests, wardrobes, cupboards and safes are), others only from their own area; displays are filled in the area you are in. Outside, the hotkey also works on a container just across the border of the next area while both are loaded. The master menu (and the panel's Areas list) removes an area again; no items are moved either way. Templates can list the cells too (optional `cells`, next to `cell`); `esm_dump.py cell` shows which containers the other cells reach (`reachableFromOtherCells`) and `check` validates `cells`. Every existing setup and template, including the separate cellar templates, loads and works exactly as before; a cellar with its own setup stays its own home.
- Remove: the container menu (and the panel) can take a container out of the home's setup, forgetting its categories and learned items; what's inside stays. Ignore still keeps one out for good.
- A container with learned items only names their categories: the activate prompt reads "End Table (Books, learned)", and the category picker and the panel show "Books (1 learned)".
- Save as template...: the hotkey menu (lists and message boxes) saves this home's setup as a template in `Templates/User/`, under a name you type (the home's name to start with), with your character as author. It is offered at once, so after Reset home (or a lost `Layouts` folder) Use template brings the setup back: master, categories, ignored containers, displays, mannequin sets, keep rules and learned items (kept as fixed item bindings, so they go to the same containers; HUD: "incl. 12 learned items"; the panel's Template tab has a "Keep learned items" box to leave them out of a template you share). Saving over the same name asks first, backs up the old file and raises its version (1.0.0, 1.0.1...).
- Setups are backed up outside the Data folder: every time a home's setup changes, a copy goes to `Documents/My Games/Skyrim Special Edition/SKSE/DovaStash/Backups` (Skyrim VR: `My Games/Skyrim VR`), with copies from your last 5 sessions. MO2 does not put it in its overwrite folder, so cleaning that folder no longer loses your setups. Setups you already have are copied there at the next game start.
- Restore your last setup: entering a home whose setup file is gone shows "Dova Stash: a saved setup for Whitepeak Tower can be restored (press K)", and the hotkey's setup menu offers "Restore your last setup (2026-10-12 14:03)" first. The panel lists the saved copies under Actions > Saved setups, each with a Restore button (asks first and backs up the current setup). A broken copy is never used. While a copy can be restored, no template is turned on by itself.

### Fixed

- Saved templates list the plugins of the displays they use in `requires`, so they pass the template check (not the plugins of display items or mannequin sets: those are optional, see Templates pre-define displays).
- The panel's Template tab starts from the current home's name after you move to another home, instead of the first home's.

- Spare copies of worn gear are deposited now. Before, a second Iron Shield stayed on you while you wore one. Each such move checks that the worn, tempered or enchanted copy stayed behind.
- Shelves, racks and cupboards that open a hidden chest (an activator linked to it, like Whitepeak Tower's Food shelf or a vanilla bookshelf) can be set up with the hotkey now. They show the chest's categories in the prompt and as floating icons.
- Items that a script hands straight back when stored, such as the Curators Storage Token, stay on you after the first try instead of coming back (and opening their prompt) on every Deposit.
- The hotkey lists and the rename box appear already in the middle of the screen, instead of showing at the skin's position and then jumping there.

### Changed

- Plainer words in the game: the HUD, menus and panel say "setup" instead of "layout" ("Breezehome setup enabled", "Custom setup (not from a template)", "this home's setup file is broken"). File names and the template format are unchanged (`Layouts/`, `dovastash-layout-1`).
- The panel's Template tab saves the same way as the hotkey: the template is offered right away (no restart), the author defaults to your character and the version is automatic, and overwriting backs up the old file.
- Your own saved templates come first for their home (ahead of shipped ones of the same priority), so the hotkey and the first-visit notice offer them.
- The hotkey menu says Re-apply template only for the template the home's setup came from; another template reads Use template.
- `esm_dump.py check` accepts `//` and `/* */` comments in templates, as the game does.
- Templates and saved layouts are easier to read and edit by hand: one line per container with the columns lined up, and short names. The reason a container is left alone is in a separate `note` (shown in the panel under the container's name) instead of filling its name.
- Floating icons show a container's full category row, like QuickLoot's bottom-right row: up to five icons, then "+N". QuickLoot and the overlay now share one icon-row function.
- Every hotkey list starts with a header naming the menu ("-- Whitepeak Tower --", "-- Chest: Categories --"), with the Dova Stash logo above it in your list skin's text colour. Choosing the header does nothing.
- Hotkey lists show category icons from your own SkyUI icons: the category picker, the display picker, Retrieve, Browse containers (each container's icon) and a container's Categories... entry.
- Ticking a category in the hotkey lists updates the list in place, keeping your place in it, instead of closing and reopening the menu.

### Docs

- Dova Stash Editor (in development, `editor/`, design in `docs/EDITOR.md`): a standalone program that reads your MO2 mod list (names, Nexus mod ids, plugins), finds the player homes in a mod's plugins, draws each cell's floor plan with its containers, racks, mannequins, plaques and doors, and drafts a template; preset `editor`.
- Nexus description rewritten in the cmc structure: shorter, plain lists, and current for 0.2.x. The file description and pinned post are updated too.
- Section headers are now 1200×112, left-aligned, with the emblem as a bullet (like cmc and iharvest-ng).
- Press kit gallery: "Never lose an item" is replaced by "One key" (the hotkey lists for the home, a container and a rack) and "Beyond the chests" (displays, mannequins, the workbench). How it works and the homes image say a template is turned on with one choice.

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
