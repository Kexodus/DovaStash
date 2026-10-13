# Dova Stash — Design Document

> Auto-sorting loot storage for **any** player home in Skyrim — vanilla, Creation Club, or mod-added —
> with shareable per-home templates so most players never have to set anything up.

Status: **Draft v0.1** · Target: SKSE plugin (CommonLibSSE-NG, Address Library) · Runtimes: SE 1.5.97, AE 1.6.x, VR

---

## 1. Goals & principles

**Core goal:** Let any player home have an auto-sorting loot mechanism, and let people author per-home
templates so that, for popular homes, setup is zero-touch.

Design principles, in priority order:

1. **Never lose an item.** Every move is a real `RemoveItem(... moveToRef)` that preserves extra data
   (enchantments, tempering, custom names, stolen flags, soul charge). If anything is ambiguous, the item
   stays where it is.
2. **Zero-config for the common case.** Drop-in install; homes with a template just work.
3. **Works anywhere it's safe.** Not limited to a hardcoded list of homes. Eligibility is decided by
   runtime safety checks (ownership, respawn), not by a whitelist.
4. **Data-driven.** Categories, keep rules, and home layouts are plain JSON. No ESP edits are needed to
   support a new home or a new item category.
5. **Minimal dependencies.** DLL-only. SKSE Menu Framework is a *soft* dependency: we use it when it is
   present and fall back to native message boxes otherwise (which is also the VR path).
6. **Borrow the player's UI; bring no art of our own.** Everything the player sees in game is drawn by
   UI they already run: SkyUI, whatever HUD/menu skin they use (or none), their icon packs
   (I4 + I5, THICC...), QuickLoot IE, and UIExtensions. They also supply its fonts and sounds. Dova Stash
   ships no textures, icons, fonts or skinned menus. The black-and-white Nexus art is for the mod page
   only, with one exception the user asked for: the Dova Stash logo above the hotkey's UIExtensions
   lists (`Interface/DovaStash/logo.dds`, tinted with the skin's own list text colour; optional, §10.3).
   Every integration is detected at runtime, and each surface falls back to the next available one.
   See §10.3.

### Non-goals (v1)

- Cross-home/central storage (planned for v2, see §12; the data model is designed for it).
- Moving items to or from non-persistent containers in unloaded cells. A home that spans several cells
  (§6.7) reaches its containers in another cell only while that cell is loaded or when the reference is
  persistent; anything else is skipped, never touched.
- A whole worldspace (a town, a hold) as a home: only the exterior cells a layout lists belong to it.
- Managing NPC/follower inventories (except possibly crafting-supply pulls in a later milestone).

---

## 2. Reference: Stash Organizer → Dova Stash mapping

Stash Organizer (FNV/TTW, by Eddoursul) is the model. Its v2 is per-cell; v3 is centralized.

| Stash Organizer                                        | Dova Stash (Skyrim)                                                                 |
|--------------------------------------------------------|-------------------------------------------------------------------------------------|
| Point at container + VATS key → mark **master**        | Look at a container + **Dova Stash hotkey** (VR: configurable activation gesture)  |
| Master container menu: process / retrieve / settings   | **Stash panel** (Menu Framework) or MessageBox menu (fallback/VR)                  |
| Secondary container → assign categories                | Same: any container in the cell can be bound to one or more categories             |
| Items put in a container become its default destination | Same: **learned destinations**, recorded from `TESContainerChangedEvent`           |
| Homeless items stay in master                          | Same                                                                                |
| Ignored containers                                     | Same: an `ignored` flag excludes a container from sorting and learning; **Remove** forgets one instead |
| Keep / always-remove / quotas, per cell or global      | Same: keep rules with quotas, global defaults plus per-home overrides              |
| Keeps equipped / hotkeyed / pinned gear                | Keeps equipped, favorited, and quest items, plus gold, lockpicks and torches by default |
| Retrieve by category / perk / repair / crafting list   | Retrieve by category; crafting materials come by themselves at a station (§8.5)   |
| Hotkey into the air → highlight managed containers     | Same: apply a timed effect shader to managed containers                            |
| `Stash Organizer.ini` + `[Cell ID].ini` override       | `Layouts/` (player) + `Templates/` (community), resolved by cell identity (§6)     |
| Settings persist across playthroughs                   | Same: layouts live in JSON, not in the save                                        |
| Crafting pulls from storage                            | Materials at the workbench (§8.5): brought on sitting down, the rest put back      |
| Mojave Express (send items between stashes)            | Superseded by central storage (v2)                                                 |

---

## 3. Locked decisions

| Topic         | Decision                                                                     |
|---------------|------------------------------------------------------------------------------|
| Storage model | Per-home in v1. The data model is designed so central storage can be added in v2 without migration pain |
| UI            | SKSE Menu Framework (QTR-Modding v3 API) as the primary UI, with a native `MessageBox` fallback |
| Plugin file   | **DLL-only** for v1. An ESL-flagged ESP is added later only if central storage requires it |
| Runtimes      | SE 1.5.97 + AE 1.6.x + VR, as one DLL via CommonLibSSE-NG                    |
| Library       | `alandtse/CommonLibVR` branch `ng` (actively maintained CommonLibSSE-NG)     |

---

## 4. Player experience

### 4.1 Zero-touch (template exists)

1. Player enters Breezehome (or any home with a template).
2. Dova Stash finds a matching template and shows a one-time notification:
   *"Dova Stash: Breezehome setup available (by &lt;author&gt;). Press [Hotkey] at any container to enable."*
   (In-game text says *setup* for what the files and this document call a layout, §10.3.)
   Auto-enable on first entry is a config option, off by default.
3. Player presses the hotkey: **Deposit** runs and loot flows into the right containers.

### 4.2 Manual setup (no template)

1. Look at a container and press the hotkey → *"Make this the Dova Stash master container?"*
2. The master's menu has these actions:
   - **Deposit**: move eligible inventory items using the keep rules
   - **Sort**: redistribute everything in the master
   - **Retrieve…**: by category, or everything
   - **Containers**: highlight managed containers
   - **Keep rules**
   - **Save as template…** (§6.5)
   - **Reset**
3. Look at any other container and press the hotkey to open its binding menu: assign categories, set
   it as ignored, rename it, or promote it to master.
4. Or skip binding entirely: just **put items into containers by hand**. Each placement teaches Dova Stash
   where that item belongs (learned destinations).

### 4.3 Day-to-day loop

- Come home and press the hotkey (from anywhere in the cell, or at the master) → **Deposit**.
- Dump things into the master and close it → items with a known destination move automatically.
- Need arrows? Look at the master → **Retrieve → Ammo**.

---

## 5. Architecture

```
┌─────────────────────────────── DovaStash.dll ───────────────────────────────┐
│                                                                             │
│  Plugin entry (SKSEPluginLoad) ── Logging (spdlog) ── Settings (INI/JSON)   │
│                                                                             │
│  ┌──────────── Input ────────────┐   ┌──────────── Events ───────────────┐  │
│  │ Hotkey sink (BSInputDevice)   │   │ CrosshairRefEvent (SKSE)          │  │
│  │ VR: activation gesture        │   │ MenuOpenCloseEvent (ContainerMenu)│  │
│  └──────────────┬────────────────┘   │ TESContainerChangedEvent          │  │
│                 │                    │ TESCellFullyLoadedEvent / PlayerCell│ │
│                 ▼                    └───────────────┬───────────────────┘  │
│        ┌──────────────── Controller ─────────────────┴──┐                   │
│        │ "what does the hotkey mean here?" state machine│                   │
│        └───────┬───────────────────┬────────────────────┘                   │
│                ▼                   ▼                                        │
│   ┌──── HomeRegistry ────┐   ┌──── SortEngine ─────────────┐                │
│   │ eligibility checks   │   │ deposit / distribute /      │                │
│   │ layout resolution    │   │ retrieve / learn            │                │
│   │ (Layouts>Templates)  │   │ KeepRules, ItemMover        │                │
│   └──────────┬───────────┘   └──────────┬──────────────────┘                │
│              ▼                          ▼                                   │
│   ┌──── Store (JSON I/O) ────┐   ┌──── CategoryEngine ─────┐                │
│   │ Layouts/, Templates/,    │   │ compiled predicates from│                │
│   │ Categories/, settings    │   │ Categories/*.json       │                │
│   └──────────────────────────┘   └─────────────────────────┘                │
│                                                                             │
│  UI facade ──► MenuFrameworkUI (ImGui, if installed and not VR)             │
│            └─► MessageBoxUI   (native fallback, VR)                         │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Threading

All inventory mutation and every layout edit run as SKSE tasks (`SKSE::GetTaskInterface()->AddTask`).
Event sinks and ImGui callbacks only enqueue work. Sorting is batched: one task per operation.
Chunking very large inventories across frames is still planned (M5).

**Two threads, not one.** On AE the task queue drains on engine worker threads, while the HUD and
other menus (our overlay, the activate prompt hook, QuickLoot's handlers, UI tasks) run on the UI
thread (confirmed from thread IDs in the log). Code on the UI thread therefore never touches the
live `Home`/`Layout` or settings:
- `HomeRegistry` publishes an immutable copy of the current home (`DisplayHome`, an
  `std::atomic<std::shared_ptr>`) after every change. Display code reads only that copy.
- `Settings` is an atomically swapped `shared_ptr<const Values>`; getters return copies.
- The UIExtensions bridge keeps its one pending menu behind a mutex, and runs callbacks unlocked.
- Category data never changes after `kDataLoaded` and is shared freely. Template data is read only
  from SKSE tasks (Controller, `HomeRegistry`, the panel's snapshot builder), never from the UI
  thread: saving a template rescans the list (`Store::ReloadTemplates`, which replaces it) and
  re-resolves the current home in the same task (§6.5).

---

## 6. Data model & files

Settings live in `Data/SKSE/Plugins/DovaStash.ini` (hotkey, auto-enable, logging level, UI mode).
All data paths are under `Data/SKSE/Plugins/DovaStash/`.

```
Categories/
  00_Default.json             shipped category definitions
  50_<anything>.json          add-on packs (e.g. a Survival Mode or Requiem pack); loaded in name order
Templates/                    community/shipped layouts — read-only from the mod's perspective
  Vanilla/Breezehome.json
  <AnyAuthor>/<AnyName>.json  file names are free-form; identity is inside the file
  User/<Name>.json            the player's own saved templates ("Save as template", §6.5) — written by the mod
Layouts/                      player-owned layouts — written by the mod
  Skyrim.esm_000165A7.json    one file per home, named after its (main) cell (§6.7)
```

Outside Data, next to the log, the mod keeps a copy of every layout (§6.6):

```
Documents/My Games/Skyrim Special Edition/SKSE/DovaStash/Backups/    (VR: My Games/Skyrim VR)
  Skyrim.esm_000165A7.json                         the latest copy of that home's layout
  Skyrim.esm_000165A7.json.20261012-140312.bak     history: the copy from an earlier session (newest 5)
```

### 6.1 Identity: how we refer to forms across load orders

Every form reference in JSON is written as **`"<PluginFile>|0x<LocalFormID>"`**, for example
`"Skyrim.esm|0x000165A7"`. This is stable across load-order changes and ESL compaction. It is resolved at
runtime via `TESDataHandler::LookupForm(localID, plugin)`.

- Keywords may also be referenced by **EditorID** (`"VendorItemGem"`), because keyword EditorIDs are kept in
  memory at runtime.
- An unresolvable reference (plugin removed) is logged and **skipped, never deleted**. User data
  survives a temporarily disabled mod.

### 6.2 Layout resolution (per cell)

On entering an eligible cell:

1. `Layouts/<cell>.json` exists → use it (the player's own setup always wins).
   Without one, a copy of the player's layout outside Data (§6.6) is offered first: the setup prompt
   starts with *Restore your last setup*, and no template is enabled automatically while it exists.
   Without one either, a player layout that lists the cell in `cells` makes it part of that home (§6.7):
   that layout is used, and the cell's own templates are not offered.
2. Otherwise, use every template in `Templates/**` whose `cell` matches:
   - With one match, offer it.
   - With several, pick the one with the highest `priority`, then the player's own saved templates
     (`Templates/User/`, §6.5) before the others, then the newest `version`. The UI lists the
     alternatives. A template is offered when its `cell` matches and every plugin in `requires` is
     loaded; player exports follow the same rules.
3. Applying a template **copies** it into `Layouts/`, with a `"basedOn"` back-reference. Later edits
   never touch the template, and updating a template pack never clobbers player tweaks. A
   "Re-apply template" action exists (the hotkey menu says *Re-apply* for the template the layout's
   `basedOn` names, *Use template* for any other).

### 6.3 Layout / template schema (v1)

```jsonc
{
  "$schema": "dovastash-layout-1",
  "name": "Breezehome",
  "note": "Each container takes the item categories listed...",  // optional, for people reading the file
  "author": "Dova Stash",
  "version": "1.0.0",
  "priority": 0,
  "requires": ["Skyrim.esm"],          // all must be loaded for the template to be offered
  "cell": "Skyrim.esm|0x000165A7",     // illustrative IDs; real ones come from xEdit or esm_dump.py
  "master": "Skyrim.esm|0x000AAAAA",   // a container reference (REFR), not the base CONT
  "containers": [
    {"ref": "Skyrim.esm|0x000BBBBB", "label": "Armory chest",    "categories": ["weapons.*", "armor.heavy", "armor.light", "armor.shields"]},
    {"ref": "Skyrim.esm|0x000CCCCC", "label": "Alchemy satchel", "categories": ["alchemy.ingredients", "alchemy.potions", "alchemy.poisons"]},
    {"ref": "Skyrim.esm|0x000DDDDD", "label": "Barrel",          "ignored": true, "note": "Respawns: what you store here can disappear."}
  ],
  "displays": [                        // optional: the displays this home fills (§8.4)
    {"ref": "Skyrim.esm|0x000EEEEE", "label": "Bow rack", "categories": ["weapons.bows"]},
    {"ref": "Skyrim.esm|0x000EEEF0", "label": "Sword rack", "items": ["MyArmory.esp|0x00000D62"], "only": true},  // only this sword; skipped without MyArmory.esp
    {"ref": "Skyrim.esm|0x000FFFFF", "kind": "item", "label": "Mask wall", "items": ["Skyrim.esm|0x00061CD6"]},  // what its script is made for
    {"ref": "Skyrim.esm|0x000EEEEF", "label": "Mannequin", "set": "Skyrim.esm|0x00013961"}  // wears the Ebony set
  ],
  "keep": {
    "inherit": true,
    "rules": [
      {"category": "ammo.arrows", "quota": 50}
    ]
  },

  // Player layouts only (never in shipped templates):
  "learned": {"Skyrim.esm|0x0001397E": "Skyrim.esm|0x000BBBBB"},
  "basedOn": "Templates/Vanilla/Breezehome.json@1.0.0"
}
```

`label` is the name the player sees for a container (the panel, menus); `note` (top level, containers,
displays) is free text for people reading the file. Dova Stash never acts on a note; the panel shows a
container's note under its name, so that is where a template says why it ignores a container.

A display entry has one shape in layouts and templates (§8.4, *Displays in templates*): `ref`, then the
optional `kind`, `label`, `categories` (racks, mannequins), `items`, `only` (racks, mannequins), `set`
(mannequins), `ignored` and `note`, written in that order. `set` names the armor set the mannequin wears by
any one of its pieces (an ARMO form; Dova Stash writes the set's first body piece). Without it the mannequin
picks a set itself ("Auto"). `only` makes a rack or mannequin take nothing but its `items`. A rack's or
mannequin's items and a set are soft references: one from a plugin that is not loaded is skipped (a template's
get one info line in the log), and their plugins never belong in `requires`. An item plaque's or case's
`items` are what its own script is made for, never a template's choice. `set` and `only` are optional fields
of schema 1: an older DLL keeps them as unknown keys.

Files are written to be read by people: tabs, one line per container (fields lined up in columns), short
lists on one line, anything longer than 200 columns spread over lines (`detail::Dump`). The shipped
templates use the same layout with two-space indents (`tools/datagen/jsonfmt.py`). Comments (`//`, `/* */`)
are accepted when reading but not kept when Dova Stash writes a file.

Version-2 forward compatibility: a layout describes **bindings of categories to storage endpoints**.
In v2 an endpoint can also be `{"remote": "central"}`, so v1 files remain valid.

### 6.4 Save game vs. JSON

Layouts are global (cross-playthrough), as in Stash Organizer. v1 needs **no SKSE co-save**. The only
per-save state we might add later is "dismissed template notifications", which can also live in
JSON keyed by character name and ID.

### 6.5 Saving a layout as a template

A player layout can be turned into a template from the game, to bring the setup back after a reset or a
lost `Layouts/` folder, or to share it. Every UI offers it, and all of them go through one code path
(`Controller::ExportTemplate`):
- the hotkey master menu (UIExtensions list and message boxes): **Save as template…** asks for a name in
  UIExtensions' text box, pre-filled with the home's name (without UIExtensions the home's name is used);
- the panel's **Template** tab: name, author and version fields (author and version may stay empty).

What is written:
- `Data/SKSE/Plugins/DovaStash/Templates/User/<name>.json` (under MO2: the overwrite folder). The file
  name keeps ASCII letters, digits, space, `-`, `_` and `.`; anything else becomes `_`
  (`core::TemplateFileStem`).
- `core::ExportAsTemplate` copies the layout and drops `basedOn`. Everything a template can hold is kept:
  master, containers (categories, explicit `forms`, label, note, ignored, unknown keys), displays (kind,
  label, note, categories, items, `only`, `set`, ignored), keep rules, the top-level note, unknown keys and
  unparsed entries. Containers that only learned items made known stay listed, without categories.
- **Learned items** (`core::ExportOptions::learnedAsForms`): a template has no `learned`, so each learned
  item becomes an explicit item binding instead, in the `forms` of the container it points at (a plain
  entry is added for a container the layout does not list). The items then go to the same containers for
  anyone who uses the template, which is what a personal backup wants. Skipped: items learned for the
  master or for an ignored container (the planner never uses those), and items some container already
  binds explicitly (that binding already wins, §7). On by default everywhere: the hotkey's **Save as
  template…** always keeps them (HUD: *saved as template "Whitepeak Tower" (incl. 12 learned items)*); the
  panel has a checkbox, *Keep learned items (as fixed item bindings)*, ticked, with a tooltip saying that
  the items will go to the same containers for anyone using the template. Unticked, `learned` is dropped
  (item-by-item habits, often hundreds of entries: personal for a shared template). Unparsed learned
  pairs are always dropped.
- `requires` gets every plugin the template references (`core::ReferencedPlugins`: cell, master,
  containers and their forms, learned items kept as forms included, displays, keep forms; not the displays'
  items or sets, which are soft references, §8.4), so it passes the same lint as shipped templates (`ValidateTemplate`, plus the category
  check); warnings are shown in the panel and counted on the HUD.
- `cell` is the home being edited; `author` defaults to the player character's name (else "Player");
  `version` defaults to 1.0.0.
- A file of that name already there: the hotkey asks first (Yes/No naming the file, its version and the
  version it becomes, and whether it belongs to another home); the panel shows **Overwrite**/**Cancel**.
  Replacing copies the old file to a timestamped `.bak` first (as layouts, newest 5 kept), and an
  automatic version is the old file's version with the patch number raised (`core::NextPatchVersion`).

Available at once: after writing, the templates are rescanned (`Store::ReloadTemplates`) and the home is
resolved again (`HomeRegistry::RefreshTemplates`), so **Use template** / **Re-apply template** and the
panel offer it without a restart. The player's own templates rank first among equal priorities (§6.2),
so after a **Reset home**, **Use template** brings the saved setup back (learned items as item bindings,
unless the panel's box was unticked).
If the layout still names forms of a plugin that is no longer loaded (kept, §6.1), the template requires
that plugin and is not offered in this game; the result says so. The player layout itself is not
changed. HUD: *Dova Stash: saved as template "&lt;name&gt;"* (plus *incl. N learned items* and the warning
count when there are any).

### 6.6 Home backups outside Data

Under MO2 every file the game writes below `Data` lands in MO2's `overwrite` folder, and cleaning that
folder deletes the player's layouts together with their `.bak` files (it happened with a Whitepeak Tower
setup). Documents is not virtualised by MO2, so `Store` keeps a copy there, in the folder of the log
(`SKSE::log::log_directory()`: `Documents/My Games/Skyrim Special Edition/SKSE`, `Skyrim VR` and the GOG
folder picked the same way) under `DovaStash/Backups/`.

- **Writing.** `Store::SavePlayerLayout` is the one place layouts are written; after it (also when the
  write under Data failed) the layout is queued for `Backups/<plugin>_<cell>.json`. One writer thread,
  started on demand, writes the queue (newest layout per file wins; temp file + rename, `core::SaveLayoutFile`),
  so a save never waits on Documents. The first write of a session for a home, and the first after a
  **Reset home**, first moves the existing copy to history (`<file>.json.<YYYYMMDD-HHMMSS>.bak`, the newest 5
  kept, the same naming and pruning as layout backups; a copy keeps the time its setup was saved). A
  failed backup never fails the save: it is warned once in the log, then logged at debug. Without a
  Documents folder there are no copies (logged once).
- **Existing setups.** At game start (`Store::Load`) every `Layouts/*.json` without a copy is copied
  there, so setups made before this feature are protected at once. An existing copy is never replaced.
- **Offering it.** On entering a home without a layout file, `HomeRegistry` looks for the latest copy
  (`Home::backup`, one file check; again after a reset or a restore). If there is one:
  - a one-time HUD line per session (instead of the template notice): *Dova Stash: a saved setup for
    &lt;home&gt; can be restored (press &lt;key&gt;)*, and `bAutoEnableTemplates` does not enable a template;
  - the setup prompt (hotkey at a container, or at nothing in such a home) starts with *Restore your last
    setup (2026-10-12 14:03)*, in the UIExtensions list and the message boxes;
  - the panel's setup page lists it as a button; on an enabled home the **Actions** tab has **Saved
    setups**: *Restore last setup (date)...* and, in a tree, *Older copies (N)* with each history copy.
    Both ask first (*Restore the setup saved &lt;date&gt;? This home's current setup is backed up first
    (.bak); no items are moved.*). A broken layout file can be replaced the same way.
  - Only meaningful restores are offered: when the latest copy says the same as the home's setup as it is
    now (`core::SameSetup`: what both serialize to, formatting and `cell` aside; unsaved edits count), the
    panel shows *The latest copy (date) is this setup as it is now.* instead of the button, and the older
    copies stay listed. Worked out when the panel refreshes or the home changes (`Store::HomeBackupMatches`,
    one file read), never per frame. Without a setup, or with a broken file, every copy is offered: there is
    nothing to compare against.
- **Restoring** (`Controller::RestoreBackup` → `HomeRegistry::RestoreBackup`). Only a file from
  `Store::HomeBackups` for this home is accepted. It is loaded with the layout loader first: a copy that
  does not parse is never used (HUD: *that saved setup is broken (see DovaStash.log)*; the file stays).
  Unsaved changes are written, the current layout file (if any, broken or not) is copied to a timestamped
  `.bak`, then the copy becomes the layout and is saved (`cell` is always the home's). HUD: *Dova Stash:
  &lt;home&gt; restored*.
- A copy is not tied to an MO2 profile: another profile without a layout for that home is offered it too.

### 6.7 Homes that span several cells

A home can be more than one cell: the main hall plus a cellar behind a load door, an upper floor, or the
exterior cells outside it where the forge, smelter, tanning rack or garden stand (Hearthfire homes have
their smithing area outside, the supplies inside). Core rules in `core/HomeCells.h` (unit-tested), the game
side in `home::Areas` (`Home/Areas.*`), `HomeRegistry` and `Store`.

**Schema** (additive and optional; still `dovastash-layout-1`). Next to `cell`, a layout or template may list
more cells of the same home:

```jsonc
"cell": "HearthFires.esm|0x00002FEC",           // the main cell: the home's identity and its layout file
"cells": [                                      // optional: more cells that belong to this home
  "HearthFires.esm|0x00004849",                 // a cell by form ref (here the cellar)
  {"cell": "Skyrim.esm|0x00009BF7", "label": "Grounds", "note": "forge, smelter, tanning rack"}
],
```

- An entry is a CELL form ref, as a string or as `{"cell", "label", "note"}` (unknown keys kept). Interior
  and exterior cells alike: an exterior cell is its own CELL record in the worldspace (`esm_dump.py cells`
  finds it, `esm_dump.py cell` shows its `grid`), so a whole worldspace is never a home, only the cells
  listed. The player's cell is compared by form id, so an exterior cell need not be in memory to be
  recognised.
- Containers, displays and stations keep being listed by reference in `containers`/`displays`: a reference
  says which cell it is in. `master` may be in any of the home's cells.
- A file without `cells` loads, saves and resolves exactly as before (no key is written;
  `tests/HomeCellsTests.cpp` checks every shipped template against the state before this section).
- Malformed entries are errors kept verbatim (`unparsedCells`); the main cell listed again and duplicates
  are warnings. Templates list the plugins of their cells in `requires` (`ReferencedPlugins`,
  `ValidateTemplate`). Applying a template over a layout keeps the player's extra cells next to the
  template's (`MergeHomeCells`); **Save as template** keeps them.

**Which home a cell belongs to** (`core::ResolveCellMembership`, `Store::MembershipOf`):
1. A cell with a layout file of its own (also a broken one) is its own home: the player's setup wins.
2. Otherwise a player layout that lists it in `cells` makes it part of that home (several: the first by
   main-cell form ref, the others logged once).
3. Otherwise it is its own home, and templates resolve by `cell` as before (§6.2).

Templates' `cells` take effect once the template is applied (copied into the player layout), never by
themselves. So the shipped `*Cellar.json` templates (Lakeview, Windstad, Heljarchen, Tundra Homestead) keep
working unchanged: a cellar with its own setup stays its own home, and a home whose layout lists a cellar
that has a setup of its own leaves that cellar out (panel: *... has a setup of its own, so it is not part of
this home*). **Decided: no merged Hearthfire templates (main + cellar + grounds) ship yet.** Players add the
areas with the hotkey; the two masters and the overlapping categories need tuning in game first, and a merged
template would replace what **Use template** offers players whose homes already work. Merged templates come
later, after in-game testing (TESTING §3.9 tests the multi-cell paths in Lakeview Manor).

**A cell belongs to one home only.** Its own setup first, else the first layout that lists it (the others are
logged once). Entering a cell that is part of another home's setup is being in that home: nothing is offered
there (no *Add this area*, no template notice, no `bAutoEnableTemplates`: an extra cell has its home's layout,
so `OnCellMaybeChanged` has nothing to announce), and the hotkey opens that home's menus.

`Store` keeps an index of every player layout's `cells` (built at game start from `Layouts/*.json`, kept
current by every layout load, save and reset). The `Home` loaded for an extra cell is that layout's home
(`Home::cell` is the main cell and decides the file; `Home::extra` is set; `Home::cellId` stays the player's
cell, so menus captured in one cell stay valid there, as before).

**Eligibility.** The main cell is checked as in §9. An extra cell needs only to be nobody else's (not owned by
an NPC or a faction the player is not in, unless it is a player house); exterior cells included, as the
layout lists them on purpose. Containers anywhere keep every check of §9, against their own cell.

**What works across a loading screen** (`core::JudgeReach`, `home::CheckHomeContainer`). An operation run in
one cell can use a container of the home in:
- the player's cell: as always (a home with one cell takes exactly the old code path);
- another cell of the home that is loaded right now (attached: the neighbouring exterior cells of the loaded
  grid);
- another cell that is not loaded, only when the reference is **persistent**. Persistent references stay in
  memory with their cell unloaded and the save keeps the changes made to them; `RemoveItem` and the inventory
  reads the `ItemMover` uses work on `InventoryChanges`, without 3D (`GetInventoryChanges` initialises a
  container never opened). A non-persistent reference of an unloaded cell is not in memory, or is about to be
  dropped with its cell: never touched. It counts as unavailable from there (a debug line, never a warning:
  it is the normal state of a sack in another area; the panel shows it: *unavailable: only reachable in
  &lt;area&gt; (not a persistent reference)*), so the planner falls back as for unbuilt furniture: the next
  container for the category, then the master, else the item stays where it is.
- Exterior persistent references whose cell is not loaded sit in their worldspace's persistent cell: they are
  matched to an area by worldspace and grid position, once that exterior cell has been in memory this session
  (looked up when the home's areas are built, or loaded as the player walked near: `TESCellFullyLoadedEvent`
  → `home::RememberCell`). Until then, and for a persistent reference that has no cell at all right now, they
  count as out of reach (*not loaded here (its area has not been visited this session)*, debug), never as
  outside the home.
- Displays are filled only in the player's cell (they need their 3D and their cell's scripts).
- The hotkey aimed at a container just across the border of the next exterior cell works when that cell is a
  loaded area of the home (the grounds often straddle two cells); displays and fronts (activators that open a
  hidden chest) only in the player's own cell.

Hearthfire furniture containers are mostly persistent (their scripts point at them): Lakeview Manor's master
and main hall chests, cupboards, wardrobes, safes and end tables are; sacks, satchels, barrels and strong boxes
are not. `esm_dump.py cell` prints `reachableFromOtherCells` per container, and `check` lists the bound
containers of a multi-cell template that are `onlyReachableInTheirCell`.

**In practice:**
- *Deposit, Sort all, Retrieve* in any cell of the home use the containers of all its cells that are
  reachable from there (the master too).
- *Materials at the workbench* (§8.5): a forge, smelter or tanning rack outside brings the materials stored in
  the persistent containers inside, and puts what is left back there when the menu closes.
- *Learn*, *Browse* and the container menus work on reachable containers (opening a remote container's menu
  is activation from afar; unverified in game).

**Adding, naming and removing an area.** Walk from the home into the next cell (out of its door, into the next
exterior cell) and press the hotkey: a cell that belongs to no home, has no setup of its own and is nobody
else's is offered to the home just left (`HomeRegistry::OfferArea`; forgotten when a game is loaded). In a
cell that is no home a message box asks *Dova Stash: add this area to Lakeview Manor?* (*Add this area to
Lakeview Manor* / *Cancel*); in a cell that could be a home of its own the setup prompt adds *Add this area to
Lakeview Manor* (UIExtensions list and message boxes); the panel shows the same button with a name box next
to it.
- *Naming it* (`ui::AddAreaNamed`): choosing *Add this area* opens the UIExtensions text box (the same as
  *Rename* and *Save as template*), pre-filled with the cell's own name, so the player can type *Grounds* or
  *Cellar*. The name becomes the entry's `label` (`core::SetHomeCellLabel`); cancelling, an empty text or the
  cell's own name typed back adds the area without a label (the add is never lost; the cell's name shows). The
  text box needs UIExtensions; without it the area is added at once, and the panel's box names it.
- *Renaming later*: in an extra area the master menu (UIExtensions list) has *Rename Grounds...*; the panel's
  **Areas** list has *Rename* per extra area (a box under the row; *Save* / Enter, empty brings the cell's own
  name back: `Controller::RenameArea`, `HomeRegistry::RenameArea`). The main cell is named by the home. HUD:
  *Dova Stash: area renamed to "Grounds"*.
- The label is what every surface shows for the area: the HUD lines, the panel's header (*in Grounds*), the
  Areas list, the container rows (*In Grounds*), the remove prompt.
- *Removing*: in an extra cell the master menu has *Remove this area from Lakeview Manor...* (asks first; its
  containers stay in the layout, out of reach), and the panel's **Areas** list has *Remove* for each extra one.

HUD: *Dova Stash: Grounds is now part of Lakeview Manor* (*this area is now part of ...* when unnamed) /
*Dova Stash: Grounds is no longer part of Lakeview Manor*. No items are moved by any of these.

**Feedback.** The log names the home's areas and, per container, its area and whether the other areas reach it
(debug, once per load of the home). The panel's header says *in Grounds* in an extra area; container rows in
another area show it (*In Cellar*, and *(Cellar)* next to *available* when used from here).

---

## 7. Category engine

Categories are hierarchical IDs (`weapons.swords`, `alchemy.potions`), matched by compiled predicates.
Bindings accept wildcards (`weapons.*`). Evaluation order for a given item:

1. Per-item **explicit binding** in the layout (rare; the escape hatch)
2. **Learned** destination for that exact base form
3. First matching **category** by priority, then specificity (deeper IDs win over shallower ones)
4. No match → item stays in / goes to **master** ("homeless")

### 7.1 Category definition schema

```jsonc
{
  "$schema": "dovastash-categories-1",
  "categories": [
    {
      "id": "misc.gems",
      "name": "Gems",
      "priority": 100,
      "icon": "misc_gem",
      "match": {
        "formTypes": ["MISC"],
        "keywordsAny": ["VendorItemGem"]
      }
    },
    {
      "id": "weapons",
      "name": "Weapons",
      "icon": { "source": "skyui/icons_category_psychosteve.swf", "label": "inv_weapons" }
    },
    {
      "id": "weapons.bows",
      "name": "Bows",
      "icon": "weapon_bow",
      "match": { "formTypes": ["WEAP"], "weaponTypes": ["Bow"] }
    },
    {
      "id": "books.skill",
      "name": "Skill books",
      "match": { "formTypes": ["BOOK"], "bookTeaches": "skill" }
    }
  ]
}
```

Predicate vocabulary (v1): `formTypes`, `keywordsAny`, `keywordsAll`, `keywordsNone`, `forms` (explicit
list), `formLists`, `plugins` (source mod), `weaponTypes`, `armorTypes` (heavy/light/clothing),
`bookTeaches` (skill/spell/none), `alchemy` (potion/poison/food), `soulGem` (empty/filled),
`enchanted` (bool), and `ammo` (arrow/bolt). Predicates combine with AND, and lists combine with OR.

A category without `match` (like `weapons` above) is a pure grouping node: it matches no item, but
gives the group a display name and an icon.

`icon` (optional, cosmetic) is the container icon for the category (§10.3). It is either a frame label
in SkyUI's item icon SWF (`"weapon_bow"`, i.e. `skyui/icons_item_psychosteve.swf`), or an object
`{ "label": "...", "source": "<SWF path relative to Data/Interface>", "color": "#RRGGBB" }` where
`source` and `color` are optional. SkyUI's category SWF (`skyui/icons_category_psychosteve.swf`:
`inv_weapons`, `inv_armor`, `inv_potions`, `inv_scrolls`, `inv_food`, `inv_ingredients`, `inv_books`,
`inv_keys`, `inv_misc`...) suits broad groups. A malformed icon is a warning and is ignored; the
category still loads. Dova Stash never ships the SWFs: they come from the player's SkyUI or icon mods.

The keyword-based approach automatically benefits from **KID (Keyword Item Distributor)**. Item packs
that are distributed with proper vendor keywords sort correctly with no Dova Stash patch.

### 7.2 Default category set (shipped)

`weapons.{swords, greatswords, daggers, axes, waraxes, maces, warhammers, bows, crossbows, staves}`,
`armor.{heavy, light, clothing, shields, jewelry}`, `ammo.{arrows, bolts}`,
`alchemy.{potions, poisons, food, ingredients}`, `books.{skill, spelltomes, notes, other}`, `scrolls`,
`soulgems.{empty, filled}`,
`misc.{ores_ingots, gems, hides_pelts, animal_parts, dwemer, clutter, building_materials, tools}`, `keys`.

#### Optional: Object Categorization Framework (OCF) set

`dist/Optional/OCF/Data/SKSE/Plugins/DovaStash/Categories/50_OCF.json` holds 63 finer categories for
users of OCF 6.1 (Nexus SSE 81469), which uses KID to put fine-grained `OCF_*` keywords on items. It is
generated by `tools/datagen/gen_ocf_categories.py` from OCF's `*_KID.ini` files. It ships as a separate optional
download that drops into the same `Categories/` folder, not in the main archive.

- **Inert without OCF.** Keywords are matched by EditorID string, and a keyword that no item carries
  simply never matches. It produces no error or warning. Every OCF category has `keywordsAny` made only of
  `OCF_*` keywords, so nothing matches without OCF. It still ships separately, because its
  subcategories would otherwise turn leaves like `armor.jewelry` into branches in the panel's category
  tree for everyone.
- **Only new ids, never overrides.** A later file that reuses an id replaces the earlier definition (a
  warning, not an error, see §7.1 / `Category.h`), which would change behaviour for everyone. All ids sit
  below a default id or group, so wildcard bindings such as `armor.*` cover them:
  `weapons.{tools, throwing, polearms}`, `armor.jewelry.{rings, necklaces, circlets, accessories}`,
  `armor.{heavy,light}.{helmets, cuirasses, gauntlets, boots}`, `armor.clothing.{headwear, outfits, gloves, footwear}`,
  `alchemy.potions.drugs`, `alchemy.food.{raw, alcohol, drinks, cooking, produce, baked, meals}`,
  `alchemy.ingredients.{mushrooms, flowers, insects, plants, creature_parts, minerals}`,
  `books.spelltomes.{alteration, conjuration, destruction, illusion, restoration, other}`,
  `books.notes.{recipes, journals, letters}`, `books.other.manuals`, `soulgems.black`, `keys.homes`,
  `misc.dwemer.{scrap, tableware}`, `misc.tools.{alchemy, exploration, camping, general}`,
  `misc.ores_ingots.{ingots, ores}`, `misc.hides_pelts.{leather, strips}`, `misc.animal_parts.dragon`,
  `misc.clutter.{valuables, tableware, empty_vessels, instruments, relics, fossils, crafting}`.
  OCF has no gem keyword for MISC items, so `misc.gems` is not split.
- **Priorities.** A subcategory takes its parent's priority, and the deeper id wins the tie. When siblings
  overlap, it takes a little more (raw food 104 beats meals 101). It never outranks a different default
  category that should keep the item: gems (140) stay gems even when OCF calls them treasure.
- **Exact parent bindings keep working.** The planner tries `MatchAll` best-first (§8). If no container
  binds `armor.jewelry.rings`, a ring falls through to `armor.jewelry`, so templates that bind exact default
  ids still receive refined items. An item that matches only the OCF category falls through to the default
  category it matched before. One exception: a `"*"` binding catches the refined item before the
  exact parent binding gets it. With this set installed, bind `armor.jewelry.*` rather than `armor.jewelry`
  when another container has `"*"`. Keep rules and Retrieve compare the *primary* category, so use `x.*`
  patterns there too.
- `tests/OcfCategoryTests.cpp` checks that the file loads next to `00_Default.json` with no diagnostics and
  that every keyword is in `tests/data/ocf_keywords.txt` for the matched record type. It also checks
  that vanilla-keyword items classify identically with and without the file, and that exact parent
  bindings still catch refined items.

---

## 8. Sort engine

### 8.1 Operations

| Op             | Source → Target                       | Trigger                                                           |
|----------------|---------------------------------------|-------------------------------------------------------------------|
| **Deposit**    | Player → destinations (or master)     | Hotkey menu, or optional auto-deposit on cell entry               |
| **Distribute** | Master → destinations                 | `ContainerMenu` closes on the master; also a "Sort" button        |
| **Sort all**   | Master + bound containers → correct one (see §16) | Manual (e.g. after changing bindings)                             |
| **Retrieve**   | Destinations → player                 | Menu: by category, or everything                                  |
| **Fill displays** | Player + managed containers → the empty displays in use (§8.4) | Part of Deposit and Sort all (`bFillDisplays`); one display with *Fill now* in its menu |
| **Crafting supply** | Managed containers → player, the unused rest back (§8.5) | Sitting down at a crafting station in an enabled home (`bCraftingSupply`); back when the crafting menu closes |
| **Learn**      | Records item → container              | `TESContainerChangedEvent` (player → non-master managed container) while `ContainerMenu` is open on that container |

### 8.2 Moving items safely (`ItemMover`)

- Iterate `GetInventory()`. For each `InventoryEntryData`, move every `ExtraDataList` stack separately
  via `RemoveItem(obj, count, kStoreInContainer, extraList, target)`, then move the plain remainder.
  This preserves tempering, player enchantments, names, stolen ownership, and soul charge.
- **Always skip:** quest items (`IsQuestObject`), non-playable forms, equipped (`ExtraWorn`/`ExtraWornLeft`),
  favorited (`ExtraHotkey`), and anything matched by keep rules. Gold, lockpicks and torches are kept by
  default (configurable).
- Never sort *while* a `ContainerMenu` is open on an affected container if that menu pauses the game.
  Defer to the menu close to avoid UI desync. Sort on arrival (items put into the master) waits for
  the close when the game is paused, and only runs with the menu open when the menu does not pause
  (Skyrim Souls). The open menus are then told to rebuild their lists.
- **Plain remainder:** the copies without extra data move with `RemoveItem` and no extra list, after the
  extra-data stacks. When an extra-data copy of the object stays in the source (worn, kept, tempered), the
  engine takes plain copies first, as the container menu does. Each such move is checked: every extra-data
  stack of the source must still be there afterwards. If one left anyway, it is moved back, the error is
  logged, and plain copies next to an extra-data copy stay where they are for the rest of the session.
- **Items that come back:** when an object Dova Stash stored in a container goes from that container to the
  player within 5 seconds (a script hands it back, for example a token that opens a prompt when stored), it
  is marked for the session and never moved again (`ItemInfo::returns`; logged once at info). Taking it out
  by hand while that container's menu is open does not count.
- Every operation logs a summary (`moved 143 items in 9 stacks to 6 containers`). Deposit and Distribute
  also log a plan summary at info (`Deposit plan: 412 stacks (377 move, 20 kept, 3 quest item, ...)`), and at
  debug one line per stack: where it goes and why (category, learned, explicit, homeless), or why it stays
  (the keep rule, quest, non-playable, nameless, comes back when stored, no destination). Homeless stacks staying in the master
  log at trace.
- At debug, the inventory journal logs every change to the player's inventory that Dova Stash did not make
  (`Inventory: +1 Coinpurse [...] from nowhere (created: script or console)`). It shows items that scripts
  add, swap or remove, and items that come back after a Deposit.

### 8.3 Keep rules

```jsonc
"keep": {
  "always":  ["equipped", "favorited", "quest", "Skyrim.esm|0x0000000F" /* gold */, "Skyrim.esm|0x0000000A" /* lockpick */],
  "never":   ["misc.clutter"],
  "quotas":  [ { "category": "alchemy.potions", "keywordsAny": ["MagicAlchRestoreHealth"], "quota": 5 } ]
}
```

Global defaults live in `DovaStash.ini`/`Settings.json`. A layout may override them or set `"inherit": true`.

---

### 8.4 Displays

Deposit and Sort all put gear on the displays the player uses, so the home looks as if the player had placed
it by hand (`[Sorting] bFillDisplays`, on by default). Dova Stash never takes anything off a display; the
player takes things down as usual.

**Turning them on.** A display is filled only when the layout lists it (and it is not `ignored`). The player
aims the hotkey at a rack, a mannequin (or the activator in front of it), an item plaque or a display case,
or at a weapon sitting on a rack, and gets a small menu (`ui::DisplayMenu`): *Use this rack* adds the entry
(nothing goes up until Deposit or Sort all). Once used: *Categories...* picks what a rack or mannequin
takes, like a container's categories (racks are offered All Weapons, each weapon category and Shields;
mannequins the armor categories); *Armor set... (Auto)* for a mannequin (see *Mannequins* below); *Fill now* while it is not full (`sort::FillDisplay`: what Deposit would move
plus what is stored; it also retries a display that failed earlier in the session, see *Filling*; a case is
full once it holds every item it is made for); *Stop using this rack* marks the entry ignored (it stays, so re-applying a template
does not turn it back on). Templates list the displays that should look hand-placed from the start. A
display case the layout uses for storage opens the container menu instead.

**Finding them.** Displays are recognised from the scripts the game binds to them (read from the Papyrus VM;
only a rack's own bookkeeping is written, see the table), in any home, with or without a template (`game::FindDisplays`):

| Kind | Recognised by | Filled by |
|---|---|---|
| `rack` | An activator running `WeaponRackActivateSCRIPT`: weapon racks, wall plaques, dagger displays, coat-of-arms racks (the middle one takes a shield), and home mods' copies of them. `RackType` (1 any weapon, 2 shield, 3 or `Patch14COARacks` no dagger or bow, 4 daggers only, 5 the Unofficial Patch's large display case: any weapon) says what it takes. Crossbows carry `WeapTypeBow`; they only go on racks whose trigger has the Unofficial Patch's `CrossbowPivot01` node, and never on coat-of-arms racks. What the Unofficial Patch's script turns away is never put up: the Forsworn staves (its `USKPForswornStavesList`) on a rack whose trigger has `BogusPivot` (known before the drop, `game::RackFitOf`), and a long staff (one with the node `Staff3rdPerson:0` or `3rdPersonStaff04:1`) in a display case (trigger node `WRDisplayCase01`): that shows only once the dropped staff has its 3D, so it goes into the player's inventory, as the script hands it back, and that rack is not offered it again this session | Exactly what the script does with the equipped weapon: dropping the very copy in the open in front of the player, making it keyframed once its 3D is loaded and moving it onto the node of the trigger the script picks with Papyrus' own `MoveToNode`, the call the script makes. The node goes by the weapon's first `WeapType*` keyword in the script's order (`SwordPivot01`, `WarAxePivot01`, `MacePivot01`, `GreatSwordPivot01`, `BattleAxePivot01`, `WarhammerPivot01`, `BowPivot01`, `StaffPivot01`, `DaggerPivot01`; `ShieldPivot01` for `ArmorShield`; `core::RackPivot`), and with the Unofficial Patch's script also by the item itself once its 3D is loaded (`game::RackNode`): crossbows on `CrossbowPivot01` (the Dwarven crossbow on `CrossbowPivot02`); staves on `StaffPivotCorruption`, `StaffPivotSanguine`, `StaffPivotFalmer`, `StaffPivotForsworn`, `StaffPivot01`/`02`/`03` by form and mesh node (on coat-of-arms racks `StaffPivotFalmer` or `StaffPivot01`); Ysgramor's, dwarven, daedric and dragonbone shields on their own `ShieldPivot*` nodes. A node the trigger lacks falls back to the plain one. If the VM has not run the move within 2 s, it is done directly (`game::MoveToNodeDirect`): CommonLib's `TESObjectREFR::MoveToNode` is not used, as its `NiMatrix3::ToEulerAnglesXYZ` goes through a `NiFastATan2` whose sign is wrong whenever |y| <= |x| (and its gimbal-lock branches are wrong too), which hung a mace sideways on a wall rack. Then, a tick later (the script's own short wait, so the trigger events of the move are past), the script's bookkeeping (`game::DisableRack`, `game::RecordRackItem`): the activator is disabled, the weapon is recorded in the script (`PlayersDroppedWeapon`; the Unofficial Patch's `PlacedItemInit`), and the trigger is put in its `WaitingForReference` state. A disabled activator is how the game knows the rack holds something; the player picks the weapon up by activating it, and the trigger then re-enables the activator. In the vanilla game the trigger disables the activator itself when the weapon enters it; the Unofficial Patch's trigger never does (its activator script disables itself, and it leaves an empty rack's trigger in a state that ignores items leaving), which is why waiting for the trigger is not enough. A weapon dropped inside the rack is pushed out by its collision and never counted, which is why it is dropped in the open. Unlike with the script, it is never seen on the way (falling in front of the player, hanging in the air until the move): its 3D is hidden from the drop (and whenever it loads, `TESObjectLoadedEvent`) until the bookkeeping is done, then shown, so it appears on the rack (`game::HideDropped`/`ShowDropped`; only the rendering is culled, collision, trigger events and the keyframed motion are untouched). Every way out shows it again: placed, taken back, the player leaving |
| `mannequin` | An actor running `MannequinActivatorSCRIPT` | Moving armor into its inventory; its script equips it |
| `item` | A reference whose script holds playable items in its properties (or a form list of them) and a "placed" flag: the `ClawToPlace` display script that home mods copy for masks, claws, Elder Scrolls, books, artifacts | Bringing the item to the player and activating the plaque for the player, so the home's own script takes it and shows the model |
| `case` | A container whose script holds playable items in its properties and handles `OnItemAdded` | Moving the items in; its script shows them |

Furniture and harvestables (chopping blocks list firewood and axes) are never displays. A container the
layout uses for storage is not a case. `esm_dump.py cell` lists the displays it can see in the plugin files.

**Layouts and templates** (`displays`, §6.3) list the displays in use, by `ref`. `categories` (patterns, as
for containers) limits what a rack or mannequin takes; without it, anything that fits. `items` names preferred
items for a rack or mannequin (`only`: nothing else goes on it), or what an item plaque or case is made for.
When an item plaque or case is turned on (the hotkey, a template, or an entry without `items` found on
arrival), Dova Stash writes the items its script is made for into `items`, so the entry shows, and keeps,
exactly what goes on it; in a player layout, edit the list to change that. `set` (mannequins) is the armor set
it wears, named by any of its pieces (see *Mannequins*). An entry for a display the scripts do not reveal needs
`kind` (racks still need their `WRackTrigger` linked ref, mannequins must be actors, cases containers).
`ignored` turns a listed display off. Applying a template merges the entries like containers (the player's
`ignored` wins; the player's `kind`, `label`, `note`, `categories`, `items` (with their `only`) and `set` stay
where the template gives none; displays only the player lists stay as they are).

**Displays in templates** (`core/TemplateDisplays.h`, unit-tested; `home::RecordDisplayItems`). A template can
pre-define a home's displays completely: which racks, mannequins, item plaques and cases are used, and for
racks and mannequins what goes on them. One entry shape, the same in layouts and templates, written in this
order (one line per entry, fields lined up, like containers):

| Field | Kinds | Meaning |
|---|---|---|
| `ref` | all | The display reference (rack activator, mannequin actor, plaque, case). Required |
| `kind` | all | `rack`, `mannequin`, `item`, `case`; only needed for a display the scripts do not reveal |
| `label`, `note` | all | The name players see; free text for people reading the file |
| `categories` | rack, mannequin | Category patterns it takes |
| `items` | all | Rack, mannequin: preferred items, chosen by the file. Item plaque, case: what its script is made for |
| `only` | rack, mannequin | `true`: nothing but its `items` goes on it, and no other display takes them |
| `set` | mannequin | The armor set it wears, by any one of its pieces |
| `ignored` | all | Listed but never filled |

```jsonc
"displays": [
  {"ref": "MyHome.esp|0x00000810", "label": "Bow rack",          "categories": ["weapons.bows"]},
  {"ref": "MyHome.esp|0x00000811", "label": "Sword rack",        "items": ["MyArmory.esp|0x00000D62", "Skyrim.esm|0x0002ACD2"], "only": true},
  {"ref": "MyHome.esp|0x00000812", "label": "Mannequin (left)",  "set": "MyArmor.esp|0x00000D70"},
  {"ref": "MyHome.esp|0x00000813", "label": "Mannequin (right)", "categories": ["armor.light"]},
  {"ref": "MyHome.esp|0x00000814", "label": "Mask wall"}
]
```

- **Soft references, only for what a template chose.** A rack's or mannequin's `items` and a mannequin's `set`
  may name forms of mods the player may not have. One from a plugin that is not loaded (or a form its plugin
  does not have) is skipped and the rest of the template works: the other items, the other displays, the
  containers. Such plugins never go in `requires` (`ReferencedPlugins` leaves display items and sets out,
  `ValidateTemplate` does not ask for them, *Save as template* does not add them); listing one there makes the
  whole template depend on it. On arrival in the home (and after applying the template) the log gets one
  info line per display, once per session, for the template's choices that don't exist:
  `Template Whitepeak Tower: Sword rack: 1 item doesn't exist in this load order (MyArmory.esp|0x00000D62);
  skipped`. Not a warning, not repeated every sort. Only what the template chose and the layout entry still
  names counts (a set the player picked in game, items the player added, and ignored displays are not
  reported). The references stay in the template and the layout (§6.1). A display left with nothing it may
  take (an `only` rack none of whose items exist, a mannequin whose set does not) waits, as a chosen set does
  (*Mannequins*), and the line says so (`...; skipped, nothing else goes on it until it does`).
- **Never for displays whose items the game or the home mod defines.** An item plaque or a case is made for
  what its own script names (dragon priest masks, claws, Elder Scrolls, "only for this item" displays). A
  template turns it on and names it, but never decides its items: for a display whose script Dova Stash reads,
  the template's `items` are not used; the copy applying the template made in the layout is replaced with the
  script's items (`core::ItemsCopiedFromTemplate`, logged at debug). Its items are never reported missing, and
  `only`, `categories` and `set` mean nothing for it (the lint says so). A player's own edit of a plaque's
  `items` in their layout keeps working as before. Only for a display declared with `kind` whose script Dova
  Stash cannot read are the entry's `items` what it is made for.
- **Lint** (`core::ValidateDisplays`, run by `ValidateTemplate` and mirrored by `esm_dump.py check`, warnings
  only): `set` on a rack, plaque or case; `categories` or `only` on a plaque or case; `only` without `items`, or
  next to `set` (the set decides); a display that is also the master or a container in use (it is storage, never
  filled as a display).
- Existing layouts and templates load and behave exactly as before (all fields optional; `only` is new and
  only written when set; `tests/ShippedTemplateTests.cpp` pins the shipped templates).

**What goes where** (`core::PlanDisplays`, unit-tested):
- Candidates are what Deposit would move from the player (never kept, worn, favorited or quest items) and
  everything movable in the managed containers. What goes on display is taken out of the deposit.
- One copy of each base form on display in the whole home. Forms already shown (loose weapons and armor in
  the cell, mannequin outfits, case contents) are not put up again.
- Items an item plaque or a case is made for are reserved for it: racks and mannequins never take them
  (a dragon priest mask waits for its plaque). So are the `items` of a rack or mannequin with `only`
  (`DisplaySpot::only`): it takes nothing else, and no other rack or mannequin takes them.
- A rack or mannequin with categories only takes items whose categories (every category the item matches,
  `CategorySet::MatchAll`, as containers use) match one of its patterns.
- Item plaques swap the item for a model and recreate it when it is taken back, so they only get copies
  without extra data (`ItemInfo::customized`: tempered, renamed, enchanted by the player, poisoned). Their
  scripts remove "one of this item", so a plaque is also skipped while the player carries any such copy, or a
  worn or favorited one, that the script could take instead. Racks, mannequins and cases keep the very copy.
- Order: item plaques, cases, mannequins with a chosen set or `only`, the other mannequins, shield racks, dagger racks,
  racks without daggers or bows, large display cases, racks for any weapon. Each takes the best remaining
  item: preferred items first, then Daedric artifacts, then the highest value; the player's copy before a
  stored one.
- A mannequin gets one set (*Mannequins*), one piece per slot (head, hair, body, hands, forearms, feet,
  calves, circlet). Shields and jewelry other than circlets stay for other displays. A mannequin wearing
  anything is left alone, except one with a chosen set (or `only`) that wears only pieces of it (of its items).
- A rack takes what its script takes (`core::RackTakes`, by the same keywords as the node): crossbows only
  where the trigger has a crossbow node (`DisplaySpot::crossbows`), and never a form its script refuses there
  (`DisplaySpot::refuses`, filled by the plugin from the script and the trigger's nodes, see the table).
- A rack is taken while its activator is disabled; unbuilt racks (trigger disabled) are skipped.

**Mannequins** (`core/ArmorSets.h`, unit-tested; `game::ArmorIndex`). Approachable by default: *Use this
mannequin* is all it takes; more control is one menu entry away, and the full list of sets lives in the panel.
- *Armor sets.* A set is worked out from plain item data, so modded armor without vanilla keywords works too.
  Pieces are split by plugin (the file defining the form) and material keyword (the first one containing
  `ArmorMaterial`; none is fine), so a mod tagging its armor `ArmorMaterialEbony` still gets its own set.
  Within that, pieces sharing a name stem are a set: the name without piece words (`Ebony Cuirass` and
  `Ebony Boots` are `Ebony`; `Armor of the Old Gods` is `Old Gods`; `Ebony Armor of Major Health` is
  `Ebony`; bracketed parts are left out), and a longer stem joins a shorter one that starts it (`Steel
  Horned` joins `Steel`); with a material keyword, stems sharing their first word are one set (`Ebony Mail`).
  Pieces still alone are put together by what tempers them (the first ingredient of their
  `CraftingSmithingArmorTable` recipe), one per slot group, so a mod's `Knight Cuirass`, `Sallet` and
  `Sabatons` make a set. Names are made unique with the armor type, then the plugin. A set's id is its first
  body piece in FormRef order.
- *Auto* (no `set`): among the armor that may go up, the set with a body piece covering the most slot groups
  (head, body, hands, feet) wins, then the highest total value: **Auto prefers the most complete set** the
  player owns; the body piece first, then the rest, pieces of the body piece's armor type first. Preferred
  `items`, when any are here, are the only set; with `only`, the mannequin wears nothing but its `items`,
  filled like a chosen set as they arrive. **Auto groups only owned armor** (what is here, without the
  tempering hint), so it can differ from the index for unusual mods. Decided and kept: the hotkey's set list
  shows the sets the player owns (below); the panel's *Armor sets* page shows every set in the load order.
  A rack or mannequin pinned with `only` whose items are all missing stays empty (it waits, as above).
- *A chosen set* (`set`): *Armor set... (Auto)* in the display menu lists `Auto` and the sets the player
  has (carried, stored in the home, or on this mannequin), most complete first: `[x] Ebony (4/4)` (slot
  groups had / slot groups of the set). Choosing one writes its id; `Auto` removes it. The mannequin then
  takes only pieces of that set (enchanted copies count as the armor they are made from), before the
  mannequins on Auto, and only on slots it leaves free: it fills up as the pieces arrive (*Fill now* stays
  offered while a piece could still go on). It never gets another set meanwhile, not even when the set's
  plugin is missing (the entry is kept). Nothing is taken off: a mannequin wearing armor of another set
  is left alone until the player takes it off (the HUD line says so when the set is chosen).
- *Every set in the load order* (the panel's *Armor sets* page, §10.1): built once, lazily, on the main
  thread, the first time a chosen set or a picker needs it, from every playable ARMO a mannequin can wear
  (`TESDataHandler`; enchanted copies, ARMO with a template, are left out); its size is logged once at
  debug. A search box (set, plugin or piece names), *Only sets I have*, a table (set, mod, pieces, slot
  groups you have), a mannequin of the current home, *Assign Ebony to Mannequin* (also a set
  the player does not have yet: it fills when the pieces arrive; an unused mannequin is turned on) and
  *Auto*.

**Filling** (`display::Fill`, over the next frames after the sort's own moves): mannequins and cases at once,
then racks one at a time (drop, out of sight until it is up, wait every 100 ms for 3D for at most 5 s, place, the script's bookkeeping a
tick later, then wait at most 2 s for the rack's activator to be disabled, then show it), then item plaques one at a time (their scripts take the item from the inventory and enable the model; nothing is seen moving). A
rack whose activator stays enabled (one without `WeaponRackActivateSCRIPT`'s `PlayersDroppedWeapon`, whose
trigger did not disable it) gets nothing: the weapon goes into the player's inventory, the HUD line says so,
and Deposit and Sort all leave the rack alone for the session. A plaque that hands an item back (it was full)
is activated once more; one that takes nothing within 3 s gets its item back where it came from and Deposit
and Sort all leave it alone for the session. *Fill now* on such a display tries again. In a home that spans several cells (§6.7) only the displays in the player's cell are filled (stock may still come from the reachable containers of its other cells). Filling stops if the player leaves the cell (a weapon dropped for a rack and not up yet goes into
the player's inventory). The player gets one line at the end (`put 12 items on display`); the log has
a summary at info (`Displays in Whitepeak Tower: 77 rack (77 empty), 6 mannequin (6 empty), 50 item (50
empty); 41 items to put up`), the plan and each placement at debug.

### 8.5 Materials at the workbench (crafting supply)

Sitting down at a crafting station in an enabled home brings the stored materials its recipes use into the
player's inventory; when the crafting menu closes, what is left of them goes back to the container each came
from (`[Sorting] bCraftingSupply`, on by default: nothing is lost either way, and everything brought goes back
on its own). Core logic in `core/Crafting.h` (unit-tested), the plugin side in `sort::crafting`
(`Sort/CraftingSupply.*`) and `game::Recipes` (`Game/RecipeIndex.*`).

**Which station, which materials.** The station is the furniture the player sits at (`TESFurnitureEvent`), so
modded stations work like vanilla ones. Its materials come from the load order's own recipes, never from a
list of ours:
- **Recipes:** every constructible-object recipe (`COBJ`) is indexed once per session, on the first station
  used (`game::Recipes`; the counts are logged at debug), by its bench keyword (`BNAM`, by EditorID). A
  station's materials are every component (`CNTO`, form lists expanded) of every recipe whose bench keyword
  the furniture carries: forge, smelter, tanning rack, sharpening wheel, armor table, cooking pot, spit, oven,
  staff enchanter, and any modded bench. New materials and recipes that use unusual items are covered by
  the data itself. Recipe conditions (perks, quests) are not checked: every component of the station comes.
- **Alchemy labs** (workbench type alchemy) bring every ingredient (`INGR`); **enchanters** (enchanting) every
  filled soul gem. A station with no recipes of its own and neither type gets nothing.
- **Never brought** (`core::crafting::Judge`): what no operation moves (quest, non-playable, nameless, items
  that come back when stored); worn or favorited copies; customized copies (tempered, renamed, enchanted by
  the player, poisoned); copies with any extra data besides a soul, a stolen item's owner, a count or a unique
  id; forms without a load-order identity; and weapons, armor and ammo that recipes use as materials, unless
  `[Sorting] bCraftingSupplyGear` is on (off by default: with a breakdown mod the smelter would bring out
  every piece of stored gear).
- **From where:** the master and every non-ignored container of the layout that passes the safety checks
  (§9), whole stacks, through the `ItemMover` (so the moves are logged, never learned, and never sorted on
  arrival). Unavailable and ignored containers are never touched. In a home that spans several cells (§6.7)
  this includes the containers of its other cells that can be reached from the station's cell (persistent,
  or loaded), so a forge outside brings what is stored inside, and the rest goes back there.

**The ledger.** What really left each container is measured (counts before and after the pull, at most what
was planned) and recorded per base form and *stack key*: `""` for plain copies, otherwise the extra data that
travels with a copy (`owner=...;` for stolen items, `soul=N;` for soul gems). With it, the player's count of
each (form, key) before the pull.

**Going back** (`core::crafting::PlanReturn`), per (form, key): the copies the player has now that could move,
minus the copies that reached the player from anywhere but the pull while the station was in use (crafted, or
handed over by a script; counted against the plain copies first), minus the copies the player had before; at
most what was brought, never below zero. So:
- what was used up is not "returned", and the player keeps every copy they carried before (used-up copies
  count against the brought ones first);
- **what was just crafted always stays with the player**, also when it is the same form as a brought material
  (iron ingots smelted while iron ingots were brought, leather strips made at the tanning rack);
- tempering or enchanting turns a copy into a customized one, which never moves;
- a reusable soul gem that was used (Azura's Star) now has another key and stays with the player.

Each (form, key) is spread back over the containers it came from in pull order (`core::crafting::ReturnMoves`
picks the player's matching stacks). A container that is no longer available keeps its share with the player.

**Triggers.** `TESFurnitureEvent` kEnter (player) pulls, before the crafting menu builds its lists; if that was
missed, the `Crafting Menu` opening pulls for the player's occupied furniture (and refreshes the menus). The
`Crafting Menu` closing returns; so does getting up (kExit) without the menu open. Crafted copies are counted
from `TESContainerChangedEvent`s that reach the player from no container (or any container the pull did not
take from) while the station is in use.

**Saving, loading, leaving.** The ledger lives in memory only (no co-save, §6.4). Loading a game or starting a
new one forgets it, and leaving the home before getting up does the same: whatever was brought stays in the
player's inventory, never lost.

**Feedback.** One HUD line each way: `Dova Stash: 34 materials at hand` and `Dova Stash: put 20 back` (nothing
when nothing went back). The log has a line per stack brought and per (form, key) at the end at debug (`had 2,
brought 10, crafted 3, now 9 -> 6 back`), and a summary each way at info.

## 9. Home eligibility & safety

A cell can host a Dova Stash layout when **all** of the following hold:

- It does not come from a home mod that sorts and displays items itself (`game::SortsItself`:
  `Clockwork.esp`, `LegacyoftheDragonborn.esm`). Dova Stash leaves those homes alone entirely, so the two
  systems never move the same items.
- It is an interior cell, or an exterior cell that is player-owned.
- It is player-owned (`TESObjectCELL::GetOwner()` is the player or the player faction), **or** unowned, **or**
  the cell's **own** location has the `LocTypePlayerHouse` keyword. That keyword covers most vanilla, CC,
  and modded homes. Parent locations are not consulted: a mod tagging a whole town or hold must not open
  every cell below it.

The other cells of a home that spans several cells (§6.7) only need to be nobody else's (unowned, the
player's, or a player house), exterior cells included: the layout lists each of them on purpose.

Per-container checks when binding (in a multi-cell home, against the container's own cell; containers in
another cell that is not loaded also need a persistent reference, §6.7):

- Reject containers whose base `TESObjectCONT` has the **`kRespawn`** flag, unless the cell's encounter zone
  is flagged *never resets*. Being a player house is not enough. Rejected containers show a clear
  warning. Respawning containers are the #1 way to lose items in Skyrim.
- Reject owned containers whose owner is not the player.
- Skip disabled references (for example, unbuilt Hearthfire furniture). They become valid automatically
  once built.

Templates bypass none of these checks. A template binding to a now-respawning container (because of a
mod conflict) is disabled with a warning.

---

## 10. UI

### 10.1 Facade

```cpp
struct IStashUI {
    virtual void OpenMasterMenu(Home&, RE::TESObjectREFR* master) = 0;
    virtual void OpenContainerMenu(Home&, RE::TESObjectREFR* container) = 0;
    virtual void OpenSetupPrompt(RE::TESObjectREFR* container) = 0;
    virtual void Notify(std::string_view msg) = 0;
};
```

- **MenuFrameworkUI:** chosen when `SKSEMenuFramework::IsInstalled()` returns true and the runtime is not VR. Uses
  `AddWindow` for the contextual stash panel and `SetSection`/`AddSectionItem("Dova Stash/...")` for a
  global settings page, a template browser and an *Armor sets* page (every armor set in the load order,
  searchable, assignable to a mannequin of the current home, §8.4 *Mannequins*). This is the rich editor:
  category trees, multi-select binding, keep-rule quotas, and template export (§6.5).
- **MessageBoxUI:** native `RE::MessageBoxData` chains with pagination. It covers every *essential* action
  (enable, restore a saved setup, bind categories, deposit, retrieve, keep defaults, save as template). Advanced editing on VR is done by editing
  JSON or by using a flat-screen session.

Header: [QTR-Modding/SKSE-Menu-Framework-3-API](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API).
It loads the framework dynamically, so it is a soft dependency. The header is LGPL-2.1 (compatible with
our GPL-3.0-or-later, inherited from CommonLibSSE-NG).

### 10.1a Live settings

The SKSE Menu Framework "Settings" page edits every INI key in place: hotkeys from a key list,
toggles, sliders, UI mode and log level. `Controller::ApplySettings` validates, applies and writes
`DovaStash.ini` (SimpleIni, comments kept) on the main thread. Every consumer reads its setting
when it needs it, so nothing needs a restart. A UI-mode change rebuilds the UIs: the old ones are kept
alive for in-flight callbacks, and the Menu Framework panel is created only once.

### 10.2 Container highlight (planned, not implemented)

A hotkey press with no crosshair target applies a timed `ApplyEffectShader` to managed containers in the
cell, using distinct shaders for the master, bound, and ignored containers. The shaders reuse vanilla
`EFSH` forms (no ESP).

### 10.3 Inheriting the player's UI stack (v0.2)

Principle 6 in practice. The Menu Framework panel (ImGui) cannot pick up a Scaleform skin, so it is
limited to **settings and power-user editing**. Everything the player touches in-world goes through
Scaleform UI they already own.

| Surface | Drawn by | Picks up from the player's setup | Fallback |
|---|---|---|---|
| Container name/role ("Chest — Weapons") | Vanilla HUD activate prompt. We hook `TESObjectCONT`'s `GetActivateText` (vtable), so nothing is written to the save | HUD skin, fonts | Always available (VR included) |
| Container contents summary, where the selected item goes, hotkey button | **QuickLoot IE** API: `PopulateInfoBar` (V20), `ModifyButtonBar` (V21; legacy `PopulateButtonBar` on V20). Plain text only | Whatever QuickLoot skin is installed, the game's button art | Activate prompt plus K hotkey |
| Learn from looting | QuickLoot IE `TakeItem` / transfer events, plus the vanilla container-changed sink | — | Vanilla sink only |
| Retrieving items | **The real `ContainerMenu`** of the bound container, opened by us. We never draw our own item lists | SkyUI 6 lists, I4/I5 item icons, sorting and search | Same (vanilla menu) |
| In-world menus (master/container actions, assignment, retrieve, browse) | **UIExtensions** `UIListMenu`: plain top-down, scrollable, flat lists (group list → category list), each headed by the menu's name, with category/container icons and the Dova Stash logo where the menu allows it ("List menus" below). Opened from the DLL (see "Skin-agnostic contracts" below), so no ESP | Whatever replaces UIExtensions' `listmenu.swf` (any skin, or none); the player's SkyUI icon SWFs | Native `MessageBoxMenu` chain (themed by the menu skin) |
| Confirmations, quick actions | Native `MessageBoxMenu` | Menu skin | — |
| Feedback | `RE::DebugNotification` + vanilla UI sound descriptors (`UIMenuOK`, `ITMGenericUp`...) | HUD skin, sound replacers | — |
| Floating container icons (off by default) | Empty clips on the vanilla HUD movie: `loadMovie` the player's icon SWF, `gotoAndStop` the label. No art of ours | The player's SkyUI category/item icon SWFs (and packs replacing them) | Off on VR (activate prompt covers it) |
| Text | `Interface/Translations/DovaStash_<LANG>.txt` (`$DOVA_*` keys) | Game language, translations | English |

**Skin-agnostic contracts.** Dova Stash targets the *mod's* interface, never a particular skin:
- UIExtensions: only what its own `UIListMenu.psc` drives. That is `UI.OpenCustomMenu("listmenu")`;
  `_root.listMenu.LM_AddTreeEntries` / `LM_SetSortingEnabled` on `UIListMenu_LoadMenu`; the choice from
  `UIListMenu_SelectItem` / `UIListMenu_CloseMenu`; at most 128 entries. A skin that still works with
  UIExtensions therefore works with us. Verified: the stock `listmenu.swf` and a replacement skin expose
  the same names. The wheel menu is not used: lists are easier to read. The list extras (icons, in-place
  toggles, logo; "List menus" below) go one step further, into the classes `listmenu.swf` is built from:
  `ListMenu` (`_root.listMenu`: `itemList`, `onItemPressed`) and SkyUI's `ScrollingList` (`entryList`,
  `UpdateList`, `getClipByIndex`, `doSetSelectedIndex`, `entryHeight`, each row's `textField`). The
  stock SWF and Nordic UI's replacement have identical ActionScript (decompiled with JPEXS); a skin
  without these names gets the plain-text lists, where choosing any entry closes the menu.
- QuickLoot IE: only its public MIT API header.
- No colours, fonts, sizes, HTML or skin-specific features in any text we hand to these menus.

**Wording.** Players never need the word *layout*: in-game text (HUD lines, hotkey lists, message boxes,
the panel, INI comments) says *setup* ("Breezehome setup enabled", "Custom setup (not from a template)",
"this home's setup file is broken (see DovaStash.log)", "Your setup is backed up"), and explicit `forms`
are *fixed item bindings*. *Layout* stays in code, the JSON schema (`dovastash-layout-1`), file and
folder names (`Layouts/`), the log, and the documentation for template authors.

**List menus.**
- **Header.** Every list starts with a plain-ASCII header naming the menu: `-- Whitepeak Tower --`
  (master menu and setup prompt), `-- Chest --`, `-- Chest: Categories --`, `-- Chest: Weapons --`,
  `-- Chest: Learned items --`, `-- Retrieve --`, `-- Retrieve: Weapons --`, `-- Browse containers --`,
  `-- Weapon Rack --`, `-- Weapon Rack: Categories --`, `-- Mannequin: Armor set --`. `UIListMenu` has no unselectable rows, so
  choosing the header does nothing (the menu stays open). `< Back` follows it on sub-lists. The
  message-box fallback names the menu in its body text instead.
- **Icons.** Category entries show their category's icon left of the text (the same `icon` as the
  container icons below, from the player's SkyUI icon SWFs): the category picker (groups, their
  categories, "All ..."), the display picker, Retrieve ("Everything" gets SkyUI's "All"), Browse (each
  container's own icon), and the container menu's `Categories...` (the container's icon). The master
  menu's actions have none. Each visible row clip gets one child clip that `loadMovie`s the SWF and
  `gotoAndStop`s the label, and the row's text moves right by one icon column (the same in every row of
  that list). Row clips are pooled and reused while scrolling, so the list's own `UpdateList` is wrapped:
  it draws the rows, then we re-dress them for the entry each now shows. Nothing runs per frame: only on
  redraws, plus a short poll (16 ms, up to ~2 s) while an icon SWF loads.
- **Toggles stay open.** Choosing a toggle (`[x]`/`[ ]`/`[+]`/`[~]` in the category and display
  pickers) keeps the menu open: `onItemPressed` is wrapped so that a stay-open entry runs its toggle on the
  main thread instead of closing the menu, and the new entry texts (marks, learned counts) and icons are
  written onto the list's entry objects, which the list then redraws. Selection and scroll position stay
  where they were, with no redraw of the menu itself. Every other entry goes to the SWF's own handler. When
  the hooks are not available the list closes and reopens on the same entry instead.
- **Logo.** The Dova Stash logo (`Interface/DovaStash/logo.dds`: white on transparent, from
  `presskit/logo/`) sits centred above every Dova Stash list, about 2.4 rows high, tinted with the colour
  the skin gives the list's text. Its space is reserved (an invisible square) before it loads, so the
  menu is measured and centred with it. If the file is missing or would be off-screen (centring off),
  the list shows without it.
- **Centring** (`[UI] bCenterMenus`, on by default). The list and the rename box open in the middle of
  the screen, whatever the skin's position. The skin's layout is not touched: we measure the menu's own
  clip (`_root.listMenu` / `_root.textEntry`) and move the movie's `_root`. There is no visible jump.
  `_root` is transparent while the menu is filled and laid out, and is shown once it is in place. It is
  measured right away, then every ~16 ms until two measurements agree and its icons and logo have
  loaded, for at most ~0.4 s. Every outcome shows the menu again (centred, not measurable, or the
  setting turned off meanwhile).

**Container icons are stable.** The icon says what a container is *for*, so it comes from the layout
and never from its current contents (contents-based icons flipped with every deposit, per in-game
feedback).
- Master: SkyUI's "All" category icon.
- Bound: the first bound pattern's category icon ("*" gives "All").
- Learned-only: the icon of the category most of its learned items belong to.
- Ignored or unmanaged: no icon.

**Where the icons come from.** Every category has an optional `icon` field (§7.1): a SkyUI icon label,
or `{label, source, color}`, falling back to the nearest ancestor category that has one. Labels that
name SkyUI's stock icon SWFs are pointed at the SWFs the player's SkyUI is configured with
(`Interface/skyui/config.txt`). Icon packs that replace those SWFs therefore apply automatically.

The engine-independent I4 rule matcher (`core/Icons.h`, with tests) is kept in core, but the plugin no
longer loads I4's rule files (0.2.2). Container icons are category icons, so per-item icons were never
needed. I4/I5 keep doing what they do in the inventory menus.

**QuickLoot IE.** One icon per bound category, in binding order (at most 5, then "+N"), in the bottom-right
corner where the info bar text would be. It is placed from `_root.lootMenu.infoBar` and
`_root.lootMenu.weight`, which QuickLoot's own DLL drives, so every QuickLoot skin has them. The master
shows the word "Master".

**Overlay rules.**
- Only shown inside an enabled home.
- Only within a configurable radius, with distance fade and a cap on how many icons are drawn.
- Anchored at the top of the reference's bounds.
- Hidden while any menu is open, during combat, and on VR.
- Toggle in settings and via the hold-hotkey highlight (§10.2).

**Open risks.**
- **Build toolchain:** building the AS2 overlay SWF needs a toolchain in CI (e.g. MTASC + swfmill).
- **SWF formats:** QuickLoot IE 3.x vs 4.0 API versions need checking; we require V20 and use V21
  extras when present. SkyUI 6 icon SWF labels and paths also need checking.
- **UIExtensions:** behaviour on AE 1.6.1170 and VR needs checking; it is unmaintained, so the
  MessageBox fallback must stay complete.

---

## 11. Input

- **Flat:** one configurable hotkey (keyboard and gamepad), read via a `BSInputDeviceManager` event sink.
  The crosshair target comes from `SKSE::CrosshairRefEvent`.
  - Tap on a container opens the contextual menu; on a display (rack, mannequin, item plaque, display case,
    or a weapon on a rack) the display menu (§8.4).
  - Tap on nothing in an enabled home opens the master menu or Deposit (configurable).
  - Hold briefly to highlight containers.
- **VR:** no keyboard. The candidate approach is "activate a container **while sneaking**" to open the
  Dova Stash menu instead of the container (vtable hook on container activation), configurable. To be
  validated by a VR tester.

---

## 12. Road to central storage (v2 preview)

- Introduce `DovaStash.esl` (ESL-flagged) holding one persistent storage container in a holding cell.
  Persistence is required because non-persistent refs in unloaded cells are not in memory. Multi-cell
  homes (§6.7) already move items to and from persistent containers in unloaded cells the same way.
- Layout endpoints gain `{"remote": "central"}`. Homes become *access points*, as in Stash Organizer v3.
- Deposit from any enabled home routes overflow or selected categories to central storage. Retrieve can
  pull from central storage anywhere a home is enabled.
- No change is needed to v1 layout or template files.

---

## 13. Milestones

| #      | Scope                                                                                                         | Exit criteria |
|--------|---------------------------------------------------------------------------------------------------------------|---------------|
| **M0** | Project scaffold: CMake + vcpkg + CommonLibSSE-NG submodule, SE/AE/VR DLL builds, logging, deploy to MO2      | DLL loads in 1.6.1170 and logs "Dova Stash loaded" |
| **M1** | Hotkey + crosshair → MessageBox: mark master, Deposit (all to master), keep defaults, ItemMover with extra data | Enchanted, tempered and renamed items survive round-trips |
| **M2** | Category engine + default categories; bind containers; Distribute on master close; Learn; Retrieve by category; JSON layouts | Full manual workflow in Breezehome |
| **M3** | Templates: loader, resolution, apply/re-apply, export; vanilla home template pack                               | All 9 vanilla homes zero-touch |
| **M4** | Menu Framework UI: panel, category tree, keep-rule editor, template browser; container highlight               | Feature parity plus the editor |
| **M4b** | Borrowed-UI integration (§10.3), in order: QuickLoot IE; activate-prompt role + category `icon` + I4 rule matcher; Retrieve via real ContainerMenu; UIExtensions list menus; floating icons; translations; optional OCF-keyword category set | Every in-world surface themed by the player's own UI mods; nothing drawn with Dova Stash art |
| **M5** | Crafting supply pulls (any crafting station in an enabled home, §8.5: done), quotas, polish, Nexus release     | Release candidate |
| **v2** | Central storage (ESL), cross-home access                                                                      | — |

---

## 14. Build & repository layout

```
DovaStash/
  CMakeLists.txt
  CMakePresets.json            presets: release / debug; ENABLE_SKYRIM_SE, _AE, _VR = ON
  vcpkg.json                   spdlog, nlohmann-json (or glaze), simpleini
  extern/CommonLibSSE-NG/      git submodule → alandtse/CommonLibVR @ ng
  extern/SKSEMenuFramework/    vendored API header (LGPL-2.1)
  src/
    main.cpp                   SKSEPluginLoad, messaging listener
    Input/  Events/  Home/  Sort/  Categories/  Store/  UI/
  dist/Data/SKSE/Plugins/DovaStash/{Categories,Templates}/   shipped data
  docs/DESIGN.md  docs/TEMPLATE_AUTHORING.md
  tests/                       pure-logic unit tests (category matching, JSON round-trip, ID parsing)
```

Toolchain on this machine: VS 2022 Build Tools (MSVC v143) and CMake are present. **vcpkg needs to be
installed** (clone, bootstrap, and set `VCPKG_ROOT`). Post-build copies the DLL and PDB into the MO2 instance
`E:\modding\Kexodus Skyrim\mods\Dova Stash\`.

Engine-independent logic (category predicates operating on a thin `ItemView` interface, JSON schemas, ID
parsing, keep-rule math) is kept separate from CommonLib calls so it can be unit-tested without the game.

---

## 15. Open questions & risks

| #  | Question / risk                                                                     | Proposed handling |
|----|-------------------------------------------------------------------------------------|-------------------|
| 1  | Default hotkey choice (avoid vanilla and common-mod conflicts)                      | Pick during M1; always rebindable |
| 2  | VR input gesture (sneak+activate hook) is untested by us                            | Ship as experimental; recruit a VR tester |
| 3  | `RemoveItem` HUD spam / sounds during large deposits                                | Test in M1; suppress or batch notifications if needed |
| 4  | Interaction with QuickLoot IE / other container-UI mods                             | Our hotkey is independent of activation; test the learn path with QuickLoot transfers |
| 5  | Multi-cell homes (Hearthfire homes + cellar, a forge outside)                       | §6.7: optional `cells` list; a cell with its own setup stays its own home and belongs to one home only; other cells reached only when loaded or through persistent references; players add and name areas with the hotkey, no merged Hearthfire templates until tested in game. To verify in game: remote `RemoveItem`/activation, exterior persistent refs while their cell is unloaded |
| 6  | Template trust: templates are data only (no code), but bad IDs could bind wrong refs | Validate that the ref's base form is a `CONT` and lives in the declared cell; reject otherwise |
| 7  | JSON library: nlohmann-json (ubiquitous) vs glaze (fast, compile-time)              | nlohmann-json for readability; layouts are small |
| 8  | Mod name collisions on Nexus                                                        | Check before release |

---

## 16. Decisions resolved during implementation (v0.1)

- **Category priority:** higher wins. Ordering is priority (desc), then id depth (desc), then definition
  order. A later category file that redefines an id overrides it in place.
- **Patterns:** only `*` and a trailing `.*` are allowed. `a.*` matches `a` and its descendants. Items
  with no category are *not* caught by a `*` binding; they go to the master.
- **Same category on several containers:** this acts as an ordered fallback chain. The most specific
  pattern wins, then list order. Unavailable containers are skipped, for example unbuilt Hearthfire wings
  (disabled refs).
- **Explicit per-item bindings:** `"forms": [FormRef…]` on a container entry.
- **Keep:** both `quotas` and `rules` are accepted. `inherit` defaults to true. Quotas keep the most
  valuable stacks first. Quest and non-playable items are always kept.
- **Learn:** an unknown container that passes the safety checks is added to the layout with no
  categories. Learning is skipped when the container is already the item's rule-based destination.
  The category picker shows where a container's learned items belong ("Books (1 learned)",
  `sort::LearnedByCategory`), though learning binds the exact item, never its category. A container with
  learned items only names those categories wherever its role is shown: activate prompt "End Table
  (Books, learned)", QuickLoot's info bar and the list summaries "Books, Potions +1, learned" (most items
  first; "1 learned item" when none has a category), and the panel's Categories column and category tree.
  The categories are worked out when the home is published (`DisplayHome::learnedCategories`, each base
  form classified once per session), so per-frame code never looks up forms.
- **Ignore vs Remove:** Ignore keeps the entry with `ignored: true`: the container gets nothing, teaches
  nothing, and stays out (Learn never re-adds it, re-applying a template keeps it off), and its
  categories come back with Stop ignoring. Remove (`Controller::RemoveContainer`, after a confirmation)
  deletes the entry and the learned destinations pointing at it; what is inside stays. The master
  can't be removed. A removed container is unknown again, so Learn may add it back.
- **Eligibility (§9) addition:** a cell owned by a faction the player belongs to is eligible. Severin
  Manor is owned by `DLC2RRSeverinManorFaction` and has no `LocTypePlayerHouse` keyword.
- **Plugin names** are compared case-insensitively everywhere. Steam stores CC files in lower case.
- **Vanilla data quirks:** firewood's keyword is `VendorItemFireword` (a vanilla typo). Hearthfire
  building materials and Dwemer parts have no shared keyword, so they use explicit `forms` lists. Letters
  carry `VendorItemBook`, so `books.notes` is weak until OCF keywords are supported.
- **Template authoring tool:** `tools/esm_dump.py` (see `tools/README.md`).
- **Sort all sources:** only the master and containers with at least one category binding or explicit
  `forms` entry are emptied by Sort all. Containers without bindings (typically added by Learn:
  "keepsake chests") are targets only, for learned items, and are never emptied.
- **Nameless items** (empty base name, usually script tokens that mods track state with) are never
  moved, learned or counted: they are invisible in container menus.
- **Applying a template over a player layout** first writes a timestamped backup
  (`<file>.json.<YYYYMMDD-HHMMSS>.bak`, newest 5 kept; Reset uses the same scheme). Only category
  bindings come from the template; learned destinations, keep rules, ignored flags, explicit forms,
  labels, unknown keys, unparsed entries and containers the template does not list (kept without
  categories) are preserved (see `Template.h`).
- **Layout file identity:** the file name decides the cell. A layout whose `cell` differs from its
  file name is used for (and saved to) the file's cell, with a warning. A layout whose `containers`
  is not an array or whose `learned` is not an object is treated as broken and never overwritten.
- **Scripted containers:** containers with attached Papyrus scripts (bookshelves, display cases...)
  are never added by Learn, and binding one shows the warning "This container runs a script and may
  reject or return items". Detection reads the VM's attached-script table for the reference's handle.
- **Container ownership:** a container without its own owner inherits the cell's owner. A foreign
  cell owner rejects the container, except in a cell whose own location is a `LocTypePlayerHouse`
  (accepted by §9), where it is a warning.
- **Plain remainder next to an extra-data copy:** the plain (no extra data) remainder of an item moves
  even while an extra-data copy of it stays in the source (worn, favorited, quest, tempered, enchanted,
  renamed, or only owned). Extra-data stacks move first. The engine takes plain copies first, and each
  such move is verified; if a special copy left, it is moved back and these moves stop for the session
  (§8.2, TESTING §2.6). 0.2.2 to 0.2.4 skipped them outright.
- **Fronts (activators that open a hidden container):** many home mods put an activator (a food
  shelf, a wine rack, the vanilla bookshelf trigger) in front of a hidden chest, linked to it with the
  default linked ref (`ActivateLinkedChestDummyScript` and similar). Activating it opens the chest's
  ContainerMenu. Dova Stash treats such an activator as that container: the hotkey binds the chest,
  the activate prompt shows the chest's role on the activator, and the floating icons sit on the
  activator. A front is an `ACTI` reference whose linked ref (no keyword) is a container in the same
  cell (`game::ContainerBehind`); the cell is scanned once when it is attached (`Home::fronts`). The
  layout stores the container, never the activator. `esm_dump.py cell` lists them as `openedBy`.
- **Ignored template bindings:** templates list respawning containers as `ignored` so they are never
  used or learned. They are not counted as "unavailable" when a template is applied.
