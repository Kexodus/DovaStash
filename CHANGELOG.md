# Changelog

## [Unreleased]

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
