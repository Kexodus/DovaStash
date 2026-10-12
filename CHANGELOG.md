# Changelog

## [Unreleased]

### Added

- Displays: look at a weapon rack, mannequin, or a plaque made for one item (dragon priest masks, dragon claws, Elder Scrolls...) and press the hotkey to use it, and pick what it takes under Categories, like a container (a rack for bows only, a mannequin for light armor). Deposit and Sort all then keep it filled, as if you had placed it by hand: racks get your best weapons (one of each), coat-of-arms racks a shield, the Unofficial Patch's glass display cases a weapon, mannequins a matching armor set, and a plaque only its own item (written into the layout when you turn it on). Weapons go up the way the game's own racks place them, on the same spot (the Unofficial Patch's racks too, with their crossbow, staff and shield spots; what they turn away, such as a staff too long for a display case, stays off), so you take them down as usual; they just appear on the rack, never seen falling or floating on the way. Works in any home from the displays' own scripts; templates can turn displays on and pick their items (`displays`). Nothing is taken off a display. Setting: "Put gear on the home's displays".
- Materials at the workbench: sit down at a forge, smelter, tanning rack, sharpening wheel, armor table, cooking pot, alchemy lab, enchanter or any modded station in a home you set up, and the materials stored there that its recipes use come to you (an alchemy lab brings your ingredients, an enchanter your filled soul gems). When you leave the menu, what you didn't use goes back to the container it came from. What you just made stays with you, even ingots you smelted while ingots were brought, and so do the copies you carried before. The recipes are read from your load order, so modded materials and recipes work without a patch. Tempered, enchanted, favorited and quest items never move; weapons and armor that recipes use up (breakdown recipes) only with "Weapons and armor used as materials too". Setting: "Materials at the workbench" (on).
- Mannequins: by default each wears your most complete matching armor set, modded armor included: sets are worked out from the plugin, material keyword and piece names ("Valkyrie Cuirass", "Valkyrie Boots"), and for odd mods from what tempers the pieces. To pick one, press the hotkey at a mannequin and open "Armor set... (Auto)": the sets you have, with how much of each, e.g. "Ebony (3/4)". The mannequin then wears only that set and fills up as you get the pieces; nothing is taken off it. The SKSE Menu Framework has an "Armor sets" page: search every armor set in your load order and assign one to a mannequin, even before you own a piece. Layouts store it as `set` (any piece of the set); `esm_dump.py check` validates it.
- Remove: the container menu (and the panel) can take a container out of the layout, forgetting its categories and learned items; what's inside stays. Ignore still keeps one out for good.
- A container with learned items only names their categories: the activate prompt reads "End Table (Books, learned)", and the category picker and the panel show "Books (1 learned)".

### Fixed

- Spare copies of worn gear are deposited now. Before, a second Iron Shield stayed on you while you wore one. Each such move checks that the worn, tempered or enchanted copy stayed behind.
- Shelves, racks and cupboards that open a hidden chest (an activator linked to it, like Whitepeak Tower's Food shelf or a vanilla bookshelf) can be set up with the hotkey now. They show the chest's categories in the prompt and as floating icons.
- Items that a script hands straight back when stored, such as the Curators Storage Token, stay on you after the first try instead of coming back (and opening their prompt) on every Deposit.
- The hotkey lists and the rename box appear already in the middle of the screen, instead of showing at the skin's position and then jumping there.

### Changed

- `esm_dump.py check` accepts `//` and `/* */` comments in templates, as the game does.
- Templates and saved layouts are easier to read and edit by hand: one line per container with the columns lined up, and short names. The reason a container is left alone is in a separate `note` (shown in the panel under the container's name) instead of filling its name.
- Floating icons show a container's full category row, like QuickLoot's bottom-right row: up to five icons, then "+N". QuickLoot and the overlay now share one icon-row function.
- Every hotkey list starts with a header naming the menu ("-- Whitepeak Tower --", "-- Chest: Categories --"), with the Dova Stash logo above it in your list skin's text colour. Choosing the header does nothing.
- Hotkey lists show category icons from your own SkyUI icons: the category picker, the display picker, Retrieve, Browse containers (each container's icon) and a container's Categories... entry.
- Ticking a category in the hotkey lists updates the list in place, keeping your place in it, instead of closing and reopening the menu.

### Docs

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
