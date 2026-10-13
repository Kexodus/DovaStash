# Dova Stash Editor — Design

> A standalone program for making Dova Stash home templates without starting the game: pick the home's mod
> from your MO2 list, open its cells, click the chests, racks and mannequins, give them their jobs, check,
> save. Everything a template can say (DESIGN §6.3), in one window.

Status: **first slice (M0) built, decisions taken (§8)** · Code: `editor/` · Executable: `DovaStashEditor.exe` · Build: preset `editor`
(docs/BUILDING.md)

---

## 1. Goals

1. **Anyone can make a template.** Today a template means either setting the home up in game and saving it
   (DESIGN §6.5), or `esm_dump.py` dumps plus hand-written JSON (tools/README.md). The editor replaces the
   second path with clicking, and polishes what the first one produced.
2. **Starts from the mod list.** The author picks a mod the way they know it, by its name or Nexus mod id in
   MO2, not by plugin file or form id. The editor finds the home's cells in that mod's plugins.
3. **Sees the home.** A plan of the cell (M1) and later a 3D view (M2), where *select mode* turns the mouse
   cursor into a picker for containers, racks, mannequins, plaques and workbenches.
4. **All of Dova Stash in one program.** Everything a template expresses (§3), with the same validation the
   DLL and `esm_dump.py check` apply, and a preview of where items would go.
5. **Same files as the game.** Reading and writing go through `src/core` (`ParseLayout`, `SerializeLayout`,
   `ValidateTemplate`, the category engine), so a file saved by the editor is byte-for-byte what the DLL
   writes, and the editor can never disagree with the DLL about what a template means.
6. **Runs as an MO2 executable** and sees the merged Data folder; also works outside MO2 by reading the
   instance's folders itself.

Non-goals: editing plugins (the editor never writes an ESP), placing new containers, replacing the
Creation Kit or xEdit, running Papyrus.

---

## 2. User flow

```
+-------------------------------------------------------------------------------------------------+
| MO2 instance [E:\modding\Kexodus Skyrim      ] [Browse] [Open]   Profile [CS Test v]             |
+----------------------+---------------------------+----------------------------------------------+
| 1. Home mod          | 2. Player homes           | 3. Cell: Whitepeak Tower                      |
| [filter: 92177     ] | Whitepeak Tower.esp       | [Floor plan][Containers][Displays][Doors]... |
|  Whitepeak Tower ... |  > Whitepeak Tower  8  89 | [x] Select mode  Height [-391 .. 2019]       |
|  92177  WT.esp       |    White Tower Cave  ...  |  +-------------------------------+ +-------+ |
|                      | [ ] Exterior cells too    |  |   plan: footprints, markers   | |details| |
|                      | [x] Likely homes only     |  |   click = select              | | role  | |
|                      |                           |  +-------------------------------+ +-------+ |
+----------------------+---------------------------+----------------------------------------------+
| Problems: 0 errors, 2 warnings                         [Validate] [Save template...]              |
+-------------------------------------------------------------------------------------------------+
```

1. **Open the setup.** Launched from MO2 the editor finds its instance and profile by itself (§4.2);
   otherwise the author points it at the instance folder once (remembered later).
2. **Pick the home mod.** The mod list shows MO2's names, priorities, Nexus mod ids and each mod's plugins.
   A filter takes a name or a Nexus id (`92177` finds Whitepeak Tower). *(Game Data folder)* holds the base
   game, the DLCs and Creation Club.
3. **Find the home.** The mod's plugins are loaded with their masters and every cell they define or edit is
   scored (§3.2). Likely homes are listed first with their evidence (player-house location, player owned,
   never resets, containers, displays). Unticking *Likely homes only* shows every cell with containers.
4. **Open its cells.** The home's interior opens on the plan. The Doors tab lists where its load doors lead:
   the home's other interiors (a cellar, a cave) and the exterior cells outside (resolved through the
   worldspace grid, e.g. *Tamriel (-9, -2) WhitepeakTowerExterior01*). Each opens with one click and can
   join the home (multi-cell homes, §3.3).
5. **Select mode.** With select mode on (button or `S`), clicking the plan (M1) or the 3D view (M2) selects
   the object under the cursor: containers first, then displays, doors, furniture, structure. The details
   pane shows what it is and lets the author give it a role (§3.1). The tables (Containers, Displays) select
   the same objects, so a hidden chest is reachable even when it cannot be clicked.
6. **Assign.** Master, categories, label, note, ignored; displays on/off with categories, items, armor set.
7. **Validate.** Continuous: core's `ValidateTemplate` and `ValidateLayoutCategories`, plus the plugin-aware
   checks of `esm_dump.py check` (ref is a container in the cell, never a respawning one, displays exist,
   `set` is an ARMO, `requires` complete, §5.6). Problems are listed and click through to the object.
8. **Save.** Into `Data/SKSE/Plugins/DovaStash/Templates/<Author>/<Home>.json` of a mod chosen by the
   author (§4.4): under MO2 a "Dova Stash Templates" mod by default (created when missing), else an existing
   mod the author picks. Never MO2's overwrite by default (cleaning it would delete the templates). An existing
   file is backed up first and its version raised, as the DLL does (`core::NextPatchVersion`). A home over
   several cells is one file (§3.3).

The editor also **opens existing files**: a shipped or downloaded template, a player layout from `Layouts/`
or from the backups in Documents (DESIGN §6.6). That is the path from "I set it up in game" to a template
worth sharing: open the saved setup, rename the labels, add notes, drop the learned items, save as template.

---

## 3. Scope: everything a template can express

### 3.1 Fields and how the editor edits them

| Template part (DESIGN §6.3) | Editor |
|---|---|
| `name`, `author`, `version`, `priority`, `note` | Template header form; version raised automatically on overwrite |
| `requires` | Computed (`core::ReferencedPlugins`), never typed |
| `cell` | The home's main cell (chosen in step 3) |
| `cells` (multi-cell homes, DESIGN §6.7, `core::HomeCells`) | Cells joined from the Doors tab or the home list, with `label` and `note` |
| `master` | *Make master* on a container; the draft suggests the first always-present, non-respawning chest |
| `containers[]`: `label`, `note`, `categories`, `forms`, `ignored` | Details pane: label (default: the game name), note, category picker (tree from the loaded `Categories/*.json`, wildcards `weapons.*`), fixed item bindings (item search over the load order), *Ignore* with a reason note |
| `displays[]`: `kind`, `label`, `note`, `categories`, `items`, `set`, `ignored` | Details pane on a rack, mannequin, plaque or case: *Use this display*, categories (weapon categories for racks, armor for mannequins), `items` prefilled from the plaque's script, `set` from the armor-set index (`core::BuildArmorSets` over the load order's ARMO, as the in-game *Armor sets* page) |
| `keep` (`inherit`, `always`, `never`, `quotas`) | Keep rules tab, the same vocabulary as the panel's keep-rule editor |
| Unknown keys, unparsed entries | Kept untouched (core preserves them) |
| `learned`, `basedOn` (player layouts only) | Shown when a layout is opened; *Keep learned items as fixed bindings* on export (`core::ExportAsTemplate`) |

Crafting stations need nothing in a template today (DESIGN §8.5 finds them at runtime). The plan shows
them so the author sees which room is the forge; if templates ever name stations, they get a role here.

**Preview: what goes where.** The editor can build `core::ItemInfo` for every item record in the load order
(keywords, weapon and armor types, book teaches, alchemy flags, soul gem, enchantment) and run the same
category engine and planner the DLL runs (`CategorySet::MatchAll`, `SortPlanner`). Selecting a chest then
shows "would receive 312 items: Iron Sword, ..." and unbound categories ("Soul gems go to the master").
This is the one feature only a tool with the whole load order can offer. Planned for M1.5.

### 3.2 Finding the home (implemented)

Every interior cell the plugin defines or edits (exteriors on request) is loaded with its references and
scored; a cell is a **likely home** when its location has `LocTypePlayerHouse`, it is owned by the player
(`Player` or `PlayerFaction`), or it has mannequins and nothing in it respawns (homes owned by a faction,
such as Severin Manor, DESIGN §16). The score ranks them: player house +4, player owned +3, never resets +1,
usable containers +1/+2, displays +1, mannequins without respawning containers +2. Containers, displays and
fronts (activators that open a hidden chest) are found as `esm_dump.py cell` finds them, so the counts
match (verified: Whitepeak Tower 89 containers, 77 racks, 6 mannequins, 50 plaques; Breezehome 24/3
respawning/9 racks; all 9 vanilla, Hearthfire and Dragonborn homes found in their plugins; Gallows Hall in
`Update.esm` and both Myrwatch cells in the CC ESL with the cell refs of the shipped templates).

### 3.3 Multi-cell homes

The core schema now has an optional `cells` list (DESIGN §6.7, `core::HomeCell`). The editor's model is a
**home = main cell + more cells**, each with its references; containers and displays keep being named by
reference, so a container in the cellar is simply bound while the cellar tab is open. The door graph the
editor already resolves (interior to interior, interior to exterior grid cell) proposes the candidates:
*Lakeview Manor* links to *Lakeview Manor, Cellar* and its exterior; the author ticks which belong, and gives
each a `label` (what players see; they can rename it in game). **Decided: a multi-cell home is saved as one
file per home** (`cell` + `cells`, DESIGN §6.7), never one file per cell; the shipped `*Cellar.json` files stay
as they are for players who use them, and the DLL keeps loading either shape.

---

## 4. Architecture

```
editor/src/
  main.cpp            wmain: command line (headless) or window
  Cli.*               --list-mods, --homes, --cell [--draft], --gui, --screenshot
  Workspace.*         MO2 instance + profile + mods + merged Data view; opens a plugin with its masters
  mo2/Instance.*      ModOrganizer.ini, profiles, modlist.txt, meta.ini (Nexus id), plugins per mod, VFS detection
  data/DataFiles.*    MO2-style file resolution (later root wins)
  esp/Plugin.*        memory-mapped TES4 reader (groups, records, subrecords, zlib)
  esp/LoadOrder.*     form keys, record index, localized strings, cell directory, cell references
  esp/Bsa.*           BSA v104/v105 reader (strings now, meshes/textures in M2)
  esp/Vmad.*          Papyrus script properties (rack types, plaque items)
  home/Cell.*         cell facts, placed references, displays, doors, home finder, draft template
  ui/Window.cpp       Win32 + Direct3D 11 + Dear ImGui host
  ui/App.*            the editor window (mods, homes, cell: plan, tables, draft)
```

Core (`dova::core`) is linked unchanged: FormRef, Layout/Template schema and serialization, validation,
categories, armor sets, display kinds. The editor never duplicates a rule core already has.

### 4.1 Language and UI stack

| Option | For | Against |
|---|---|---|
| **C++23 + Dear ImGui + Direct3D 11** (recommended, used by the scaffold) | Links `src/core` directly: one implementation of the schema, validation, categories, armor sets and planner for the DLL and the editor. Same compiler, vcpkg and presets as the plugin. One small exe, no runtime. Fast enough for 1,000-plugin load orders. The 3D view (M2) lives in the same D3D11 device. ImGui is the stack of the in-game SKSE Menu Framework panel, so the know-how is shared. | ImGui looks like a tool, not a native app; text editing and accessibility are basic; docking needs the `docking-experimental` feature |
| C++ + Qt 6 | Native widgets; MO2 itself is Qt | ~30 MB of Qt DLLs, LGPL dynamic-linking rules, a multi-hour vcpkg build, a second UI paradigm for one developer |
| C# (WPF/Avalonia) + Mutagen | Mutagen reads every record type; Synthesis-style ecosystem | Cannot link `src/core`: the schema and validation would be written twice and drift (against "keep everything consistent"); .NET runtime |
| Python (extend `esm_dump.py`, tkinter) | Parser exists | No core reuse, no realistic 3D, slow on big load orders, PyInstaller exe |
| Web (Electron/Tauri) | Pretty UI | Heavy; native parsing and 3D through bridges |

### 4.2 Reading the MO2 instance (implemented)

- **Finding it:** `--mo2 <folder>`; else the parent process when it is `ModOrganizer.exe` (portable instance
  next to it, or the registry's `CurrentInstance` under `%LOCALAPPDATA%\ModOrganizer`); else a
  `ModOrganizer.ini` in the folders above the exe or the current directory (an editor kept in
  `<instance>\Mod Tools\` like xEdit).
- `ModOrganizer.ini`: `gamePath`, `selected_profile`, `[Settings]` `base_directory`/`mod_directory`/
  `profiles_directory`/`overwrite_directory` (with `%BASE_DIR%`), Qt escapes (`@ByteArray(...)`, `\\`).
- `profiles/<p>/modlist.txt`: `+` enabled, `-` disabled, `*` unmanaged (DLC/CC), `_separator` headings;
  the first line is the highest priority. `mods/<m>/meta.ini`: `modid` (Nexus), `version`,
  `installationFile`. Plugins and BSAs are the files in the mod's root.
- `plugins.txt` / `loadorder.txt` give the active load order (for a later "whole load order" mode).

### 4.3 Data files and MO2's virtual file system

MO2 starts executables inside usvfs: the process sees `<game>\Data` as the merge of the game folder, every
enabled mod in priority order and overwrite; writes below Data land in overwrite, or in the mod chosen in
the executable's *Create files in mod instead of overwrite*; the profile's `plugins.txt` appears at
`%LOCALAPPDATA%\Skyrim Special Edition\plugins.txt`. The editor detects usvfs (`usvfs_x64.dll` loaded) and
then reads Data directly. Outside MO2 it rebuilds the same view itself (`DataFiles`: game Data, enabled
mods lowest priority first, overwrite; a later root wins), so both ways give the same answers and the editor
can be used from Explorer, a terminal or tests. The instance is still read under the VFS: it is what maps
plugins back to mods and Nexus ids.

**Which plugins are loaded.** **Decided: the whole load order by default** (all active plugins in
`loadorder.txt` order: the home as the player sees it, with the patches that move or remove references, Lux,
USSEP), and a toggle *Home plugin + masters only* for the home as its mod ships it (deterministic, fast:
Whitepeak Tower with its 5 masters in under 0.1 s; all of `Skyrim.esm`'s cells in 0.3 s). The M0 scaffold can
only do the second (`Workspace::OpenPlugin`); M1 adds the whole-load-order loader and makes it the default,
with a mark on references a later plugin edits.

Archives (M2): the base game archives from `Skyrim.ini`'s `sResourceArchiveList`, then each plugin's
`<name>.bsa` / `<name> - Textures.bsa` in load order; loose files win over archives.

### 4.4 Writing templates

- Serialization is `core::SerializeLayout`, written atomically with `core::SaveLayoutFile`; overwriting
  first copies the old file to a timestamped `.bak` and raises the patch version, like *Save as template*
  in game (DESIGN §6.5). The file name comes from `core::TemplateFileStem`.
- Target, chosen in the save dialog. **Decided: under MO2 the default is a dedicated "Dova Stash Templates"
  mod**, so clearing overwrite can never delete a template:
  - **Under MO2:** `mods/Dova Stash Templates/SKSE/Plugins/DovaStash/Templates/<Author>/<Home>.json`, written
    straight into the mod folder (the editor knows `mod_directory` from the instance, §4.2), created with a
    `meta.ini` when missing; the author can pick another existing mod instead. Writing below the virtual
    `Data` is avoided on purpose: usvfs would route it to overwrite unless the executable's *Create files in
    mod* is set. MO2 must refresh (F5) to see a new mod; the editor never edits `modlist.txt` while MO2 may be
    running (MO2 rewrites it on exit).
  - **Outside MO2:** the same, into the instance's mod folder, or into a plain Data folder when there is no
    instance.
  - **Export zip** for Nexus: the template under `SKSE/Plugins/DovaStash/Templates/...` and nothing else.

### 4.5 Rendering, staged

**M1 — 2D floor plan (first version in the scaffold).** Top-down, north up. Each reference is drawn from
data already in the plugins: position, Z rotation, scale, and the base record's object bounds (OBND) as a
footprint. Structure (statics) as faint outlines gives the room shapes; containers green (respawning
red), racks blue, mannequins magenta, plaques/cases orange, doors yellow, furniture lavender. Wheel zoom,
right/middle-drag pan, a scale bar, a height range to isolate one floor of a tower, layer toggles, hover
tooltips, click-to-select. To do in M1: automatic floors (clusters of floor heights), labels at higher
zoom, selection synced with the tables, the role editor in the details pane, a marker for objects a later
plugin edits, and rotation on X/Y for tilted footprints (verify the Z-rotation direction against a known
asymmetric object; the footprints of Whitepeak's walls line up, which suggests it is right).

**M2 — 3D view.** Same window, same selection model, a D3D11 viewport beside the plan.
- Meshes: **nifly** (GPL-3.0, compatible with our GPL-3.0-or-later; in vcpkg) reads SSE NIFs
  (`BSTriShape`, `BSLightingShaderProperty`, texture sets). Static and furniture meshes first; skinned
  actors (mannequins) as a placeholder until their race skeleton and outfit are worth it.
- Textures: **DirectXTex** (MIT, in vcpkg) or DirectXTK's DDSTextureLoader (MIT, already a plugin
  dependency) turn BC-compressed DDS straight into D3D11 views.
- Archives: the editor's own BSA reader (extended to batch reads) or **rsm-bsa** (MIT, in vcpkg).
- Renderer: one pass, diffuse + simple directional and ambient light, alpha test for foliage, instancing
  by base form (Whitepeak Tower: 2,134 references, a few hundred unique meshes), frustum culling.
- Camera: Creation Kit style free-fly (right mouse look, WASD) and orbit around the selection; *select
  mode* picks with a ray against each reference's oriented bounds, refined by mesh triangles when two
  overlap.
- Not needed: Havok collision, Papyrus, lighting accuracy, LOD.

### 4.6 How objects are selected (decided)

1. **Clicking the 2D plan** (works now): select mode turns the mouse into a picker on the floor plan; the
   plan stays as the fast overview later.
2. **The editor's own cursor in a 3D view** (Creation Kit style, M2): select mode picks in the editor's
   3D viewport, sharing the plan's selection model. Matches "without having to be in game".
3. **A live link to a running game** (optional M3, not decided): the Dova Stash DLL sends the reference under
   the crosshair to the editor over a named pipe when the player presses a key, and the editor selects it;
   *Send to game* reloads the edited template in the running game. Cheap given the DLL already tracks the
   crosshair, but it needs the game running. Only if testing in game stays part of the author's loop.

Decision: 1 now, 2 as M2 (§8).

### 4.7 Validation (shared)

Core already gives template lint (`ValidateTemplate`), category typos (`ValidateLayoutCategories`) and
`cells` checks (`ValidateHomeCells`). The plugin-aware half of `esm_dump.py check` (references in the
declared cell, containers not respawning, display entries pointing at displays, `set` an ARMO, duplicate
references) lives only in Python today. Proposal: a core function over a small interface of facts the caller
provides (`ICellFacts`: is this ref in this cell, its base type, respawns, display kind), implemented by the
editor from the plugins and by the DLL from the game, so the three places check the same things.

### 4.8 Running it as an MO2 executable

*Tools > Executables > +*: Binary `...\DovaStashEditor.exe`; arguments optional (`--gui --mod 92177` opens
a mod directly); optionally *Create files in mod instead of overwrite* = a "Dova Stash Templates" mod. The
exe is a Windows-subsystem program (no console flash) and finds the instance from its parent process.
A later MO2 Python tool plugin could add *Make a Dova Stash template* to a mod's right-click menu and start
the editor with `--gui --mod "<that mod>"`.

### 4.9 Licensing

The project is GPL-3.0-or-later. Dependencies: Dear ImGui (MIT), zlib (zlib), LZ4 (BSD-2), nlohmann-json
(MIT); for M2 nifly (GPL-3.0), DirectXTex/DirectXTK (MIT), rsm-bsa (MIT): all compatible. The editor reads
the user's own game and mod files at run time and never ships or extracts Bethesda or mod assets.

---

## 5. Implementation notes

1. **Form keys:** `(file index, local id)` with the file table of the load order; light plugins use 12-bit
   ids; output as `core::FormRef` strings, so ESL and `Update.esm`-allocated CC cells come out exactly as
   the shipped templates write them.
2. **Plugin parsing** is a C++ port of `esm_dump.py` (same heuristics, same answers), memory-mapped, with a
   per-plugin cell directory built in one pass (cells, worldspace, child groups) and a lazily built index of
   the record types references point at. `esm_dump.py` stays the reference: golden tests compare the
   editor's `--cell` output with `esm_dump.py cell` for every shipped template's cell.
3. **Strings:** loose `Strings/` first, then the plugin's BSA, then `Skyrim - Interface.bsa`.
4. **Exterior cells:** a cell's references include the worldspace's persistent references in its grid
   square; persistent exterior doors are mapped to their grid cell from the arrival position (XTEL).
5. **Threading:** parsing runs on a worker; the UI only reads finished, preformatted results.
6. **Headless mode** keeps the editor testable and scriptable (`--list-mods`, `--homes`, `--cell`,
   `--draft`), and `--gui --screenshot` renders the window off-screen to a BMP (smoke tests, press kit).

---

## 6. Risks

| Risk | Handling |
|---|---|
| Plugin edge cases (compressed, localized, ESL, overrides spread over plugins) | Port of a reader that already handles the shipped templates; golden tests against `esm_dump.py` |
| The home as shipped vs. as patched (Lux, USSEP move or delete references) | Whole-load-order toggle; mark references later plugins edit; never bind a deleted reference |
| Displays that only exist at runtime (script state, scripts the plugin does not show) | Same limits as `esm_dump.py`; the author can still add an entry with `kind` (DESIGN §8.4) |
| 3D effort (NIF variants, skinned actors, alpha, performance) | Plan first; 3D static meshes only; placeholders for actors |
| Template schema moving (multi-cell, new display fields) | Core owns the schema; the editor edits `core::Layout`, unknown keys survive |
| MO2 quirks (usvfs, profiles with local settings, non-portable instances) | Detection with fallbacks; read-only on MO2's files; `--mo2`/`--profile` overrides |
| Two programs to ship | Decided: in the main zip (one download, per the release policy; §8). The exe sits in the mod folder and MO2 runs it from there |

---

## 7. Milestones

| # | Scope | Effort (one developer) |
|---|---|---|
| **M0** (done) | Scaffold: MO2 instance and mods with Nexus ids, plugin reader, home finder, cell facts, displays, door links, 2D plan with select mode, tables, draft template, headless commands, preset `editor` | done |
| **M1** | The template editor on the plan: roles in the details pane (master, categories with the category tree, label, note, ignored, fixed items; displays with categories, items, armor set), open existing templates and layouts, keep rules tab, validation panel (core + ported `check`), save into the "Dova Stash Templates" mod with backup and version, multi-cell homes from the door graph as one file per home, automatic floors, undo/redo, whole-load-order loader as the default with the *home plugin + masters only* toggle, golden tests | 3-4 weeks |
| **M1.5** | What goes where: `ItemInfo` from item records, category engine and planner preview per container; armor-set index for mannequins | 1-2 weeks |
| **M2** | 3D view: archives + loose files, NIF meshes (nifly), DDS textures, D3D11 renderer with instancing, free-fly and orbit camera, ray picking, selection shared with the plan | 5-8 weeks |
| **M3** (optional) | Live link with the running game (crosshair pick, send template to game); MO2 right-click tool plugin | 1-2 weeks |

---

## 8. Decisions

Taken with the user after M0; the sections above are written to match.

| # | Decision | Where it lands |
|---|---|---|
| 1 | **Selecting objects**: by clicking on the 2D floor plan now; a Creation Kit style 3D view with the same select mode next (M2). A live link to the running game stays optional (M3), undecided | §2 step 5, §4.5, §4.6 |
| 2 | **Shipping**: the editor ships **inside the main Dova Stash zip** (the one-zip release rule: one download, short notes, no side downloads). `DovaStashEditor.exe` lives in the mod folder; MO2 runs it from there (§4.8) | §6, §4.8, the release checklist |
| 3 | **Default save target under MO2**: a dedicated **"Dova Stash Templates" mod** (created when missing), never overwrite by default, so clearing overwrite cannot delete a template. Another mod can be picked in the save dialog | §2 step 8, §4.4 |
| 4 | **Which plugins**: the **whole load order by default** (the home as the player sees it, patches included), with a toggle *Home plugin + masters only* (the home as its mod ships it). M0 only has the second; M1 adds the first and flips the default | §4.3, §7 (M1) |
| 5 | **Multi-cell homes**: saved as **one file per home** (`cell` + `cells`), never one file per cell. The shipped `*Cellar.json` files stay for players who use them; the DLL loads both shapes (DESIGN §6.7) | §3.3, §4.4 |

Scaffold state against these: the draft's hint names the "Dova Stash Templates" mod; the save dialog, the
whole-load-order loader and its toggle are M1 work (the scaffold has neither a save path nor a plugin-set
option to change).
