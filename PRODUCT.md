# Product

<!-- impeccable:product-schema 1 -->

Context file for the `impeccable` design skill (`.claude/skills/impeccable`). Written from the repository on
2026-10-28 without an interview: every fact below is taken from `docs/DESIGN.md`, `docs/EDITOR.md`,
`docs/EDITOR-UX.md`, `presskit/` and the code, and the open points are marked as such.

## Platform

web

(The editor is a Win32 desktop app drawn with Dear ImGui, not a web page; the in-game side draws through the
player's own Skyrim UI mods. `web` is the nearest value the skill accepts. Its live mode, browser checks and
hook do not apply here and are not used.)

## Users

Skyrim Special Edition players who mod their game through Mod Organizer 2 and want the loot they bring home
sorted into their player home's containers without doing it by hand. A smaller group, template authors, open
the standalone Dova Stash Editor on the desktop to say which chest in a home takes which categories, then
share the result as a JSON template.

## Product Purpose

Dova Stash is an SKSE plugin (one DLL for SE, AE and VR) that auto-sorts items into containers in any player
home, driven by JSON layouts and templates. The editor exists so a template can be made in minutes by looking
at the home in 3D or on a plan instead of reading form IDs. Success for a player: drop everything in one
chest, find it in the right one. Success for an author: open a home, click a chest, pick a category, save,
without reading anything.

## Positioning

Any home, not a fixed list: templates describe a home's containers by form reference, so a mod home works like
a vanilla one. The in-game UI borrows the player's own UI mods instead of shipping a skin. The editor reads the
MO2 instance and the plugins itself and shows the real meshes of the home.

## Operating Context

- In game: SkyUI-style menus, QuickLoot IE prompts, UIExtensions lists, the SKSE Menu Framework settings page.
  The player is in a game session, mouse or controller, dark surroundings, reading at TV or monitor distance.
- On the desktop: the editor at 1280x720 and up, DPI 100 to 200 %, usually beside MO2. Operate mode: the
  author is in a task (find a home, see it, give its chests jobs, check, save).
- Evaluated by playtests over the homes Lux's patch hub supports, an unattended in-game test suite, and the
  guide's screenshots.

## Capabilities and Constraints

- Core logic (`src/core`) is engine-independent C++23; the plugin and the editor are the only layers that
  touch the game or Dear ImGui. The editor never duplicates a core rule.
- Never lose an item: when in doubt the plugin does not move it; unresolvable form refs are kept, never
  deleted.
- Terminology: home, cell, container, display (rack, mannequin, case, bookshelf), master chest, template,
  draft, auto-draft, category, keep rule, sell chest.
- The editor's UI is Dear ImGui with the editor's own widget set (`editor/src/ui/Widgets.h`); there is no
  CSS, no DOM, no browser. Design rules written for the web are applied by hand in that widget set.
- Open: no decision yet on a multi-cell home schema change before the first public release (see
  `docs/DESIGN.md`).

## Brand Commitments

- Name: Dova Stash; the editor is Dova Stash Editor.
- The art set (`presskit/README.md`, Style): black, white and neutral grays only (every gray has R = G = B),
  Cinzel capitals tracked wide for the wordmark and titles, Jost for text, the emblem (a longhouse gable over
  a three-bay chest) as the only ornament, no game assets, no taglines, no version tags.
- In-game text is terse: one short entry, no prefixes, nothing the control already shows.
- The in-game UI never targets one skin; it reuses whatever UI mods the player has.

## Evidence on Hand

- Nexus page copy and images: `presskit/nexus-*.txt`, `presskit/nexus/`, `presskit/logo/`.
- The user guide and its figures: `presskit/guide/`.
- Before/after screenshots of the 2026-10-28 design pass: `docs/design-pass/`.
- No testimonials, download counts or reviews are recorded here; do not invent any.

## Product Principles

1. The home is the hero: the 3D view or the plan takes the largest area; chrome is one title row and one
   status row.
2. Every suggestion and every problem carries its reason in one line.
3. Keyboard parity: everything in the toolbar has a key.
4. Looks like the art set, on the desktop as in the press kit.
5. Never lose work: unsaved changes are visible, destructive actions ask once, undo covers every edit.

## Accessibility & Inclusion

Body text at least 7:1 on the editor's surfaces, captions and status hues at least 4.5:1, in every built-in
theme; a UI size setting from 100 to 250 %; every control reachable by keyboard.
