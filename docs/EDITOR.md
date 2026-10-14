# Dova Stash Editor — Design

> A standalone program for making Dova Stash home templates without starting the game: pick the home's mod
> from your MO2 list, open its cells, click the chests, racks and mannequins, give them their jobs, check,
> save. Everything a template can say (DESIGN §6.3), in one window.

What the window looks like (layout, look, keys): `docs/EDITOR-UX.md`. This file says what the editor does.

Status: **M0 built; M2 (3D view) first slice built: textured 3D view with select mode, role editor and saving (part of
M1); M1.5 built: bulk drafts with smart suggestions (§3.4) and the "where items go" preview (§3.5); start panel, paint
mode, multi-select and undo (§3.6, §3.7); one-workspace UI built (title row, homes rail, viewport, inspector, drawer,
status row, command palette; EDITOR-UX.md)** · Code: `editor/` · Executable: `DovaStashEditor.exe` · Build: preset
`editor` (docs/BUILDING.md) · Try it: §9 *How to test the editor yourself*

---

## 1. Goals

1. **Anyone can make a template.** Today a template means either setting the home up in game and saving it
   (DESIGN §6.5), or `esm_dump.py` dumps plus hand-written JSON (tools/README.md). The editor replaces the
   second path with clicking, and polishes what the first one produced.
2. **Starts from the homes, by mod.** The author finds a home the way they know it, by its mod's name or Nexus
   mod id in MO2, not by plugin file or form id. The editor finds every home of the load order and lists each
   under the mod that adds it, in a rail beside the home.
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

One window, no modes (EDITOR-UX.md §2): the start panel, the home list, the drafts and the template are panels of
one workspace.

```
+-------------------------------------------------------------------------------------------------+
| [≡] ◆ Dova Stash Editor | < Whitepeak Tower v (yours) •    [3D | Plan]       [Draft v] [Save] [⚙] |  title row
+--------------------+-------------------------------------------------------+--------------------+
| [⌕ 92177       ][▽]| [Select|Paint] [Layers v] [Colours v] [Camera v]      | Selection Template |  inspector
| v Whitepeak Tower  |                                                       | Draft  Checks      |
|   - Dawnguard 92177|                                                       |--------------------|
|  Whitepeak Tower   |                 3D view or floor plan                 | Potion Basket      |
|  89 chests · 170   |                                                       | Container          |
|  displays   (yours)|                                                       | ◆ ROLE             |
|  All cells...      |                                                       |  Make master       |
|                    |  Right mouse: look · WASD fly · Drag: box · F frame   |  Label, Categories |
| Rescan  125 homes  |                                                       |                    |
+--------------------+-------------------------------------------------------+--------------------+
| Containers 52 · Displays 170 · Doors 3 · Where items go                                     ^    |  drawer
+-------------------------------------------------------------------------------------------------+
| Whitepeak Tower · bcr58WhiteTower     0 errors · 2 warnings     loading meshes 403/433   Ctrl+K   |  status row
+-------------------------------------------------------------------------------------------------+
```

- **Title row:** `≡` menu (Open template file..., Rescan load order, Draft all homes, Save N drafts..., Undo, Redo,
  Commands..., Shortcuts..., About), the emblem (the start panel), *Back* to the cell opened before, the **home
  switcher** (the rail's list in compact form, type to filter) with the unsaved dot and the template's chip (*no
  template*, *shipped*, *yours*, *auto-draft*, *draft*), `3D | Plan` (keys `1`/`2`), *Draft* ▾ (Draft this home, Accept all,
  Reject the weak ones, Draft again, Draft all homes, Save N drafts...), *Save* (Ctrl+S; disabled while the checks
  find errors) and `⚙` *Setup* (MO2 instance, profile, author, save target, theme, UI size, Rescan; closing applies;
  *Theme* picks the window's colours: Dova Stash, the default, or a dark or light palette inspired by xEdit's UI
  themes, or one of your own files, EDITOR-UX.md §3.1).
- **Homes rail** (left, Ctrl+1; collapses to a strip): the home list (step 2).
- **Viewport** (centre): the 3D view or the plan, never both, with a floating toolbar: *Select* / *Paint* (`S` /
  `P`), *Layers* ▾ (`L`: the layers and the height cut), *Colours* ▾ (none, by category, by fill, `C` / `V`, with the
  legend), *Camera* ▾ (overview, frame, fly speed, light, *Pick anything* / *Left drag pans*). Paint mode puts the
  paint bar in place of Layers, Colours and Camera. A hint line at the bottom names the controls of the moment. With
  no home open the viewport shows the start panel (§3.6).
- **Inspector** (right, Ctrl+3): four panels. *Selection*: what is selected (its own mesh as a picture, the
  reference and base record in one block), its role (§3.1) and *Where its items go* (§3.5); a multi-selection gets
  the bulk editor (§3.7); empty: *Make the nearest chest the master*, *Draft this home*, *Open the containers list*.
  *Template*: header, cells, keep rules, save target, *Save* / *Save as...* / *Copy JSON*. *Draft*: the review of
  this home's draft (§3.4). *Checks*: the problems, then what the template still lacks. Selecting switches to
  Selection, *Draft this home* to Draft, a click on the status row's problem count to Checks.
- **Drawer** (bottom, Ctrl+2; drag to resize): *Containers* · *Displays* · *Doors* · *Where items go*, the wide
  tables, with counts on the tabs. Every column sorts: click its header (again: descending, a third time: the
  table's own order), Shift+click adds a column; *ignored*, *not listed* and *no categories* sort after real
  categories, the master first. At the tab row's right: quick filters (Containers: *Master*, *With categories*,
  *No categories*, *Ignored*, *Not listed*, *Respawning* and a *Kind* ▾ list: chest, sack, barrel, satchel,
  bookshelf...; Displays: their kinds, *Used*, *Unused*; Where items go: *Master*, *Receives items*, *Receives
  nothing*; one of a set is enough, sets combine) and a *Filter rows* box (every word must be in some column; Esc
  clears it), with "N of M" when rows are hidden. Sort and filters last for the session, per table.
- **Status row:** home · cell and the unsaved dot; `errors · warnings` (click: Checks); background work (scan,
  drafts, archives, meshes, textures) as `label n/m`, then missing meshes or textures; one shortcut for the moment.
- **Command palette** (Ctrl+K): commands, homes (opens), the open home's containers and displays (selects and frames),
  categories in paint mode (sets the brush), each with its key.

The rail's, inspector's and drawer's sizes, which are open, and the view persist in `state.json` (§3.6).

1. **Open the setup.** Launched from MO2 the editor finds its instance and profile by itself (§4.2);
   otherwise the start panel asks for the instance folder once (remembered later). `⚙` *Setup* changes it.
2. **Find the home.** The rail lists every player home of the active load order (the same scan as *Draft all
   homes*, §3.4, read once and shared) under the mod whose plugin defines its cell: the enabled mod with the
   highest priority that has the plugin, as MO2 resolves it, in MO2's order (its Nexus id muted); *Game Data folder*
   holds the base game, the DLCs and Creation Club. A row is the home's name, its storage (containers a template can
   use · displays, over its cells), a status chip (*none*, *shipped*, *yours*, *auto-draft*, *drafted*, *saved*) and the unsaved
   dot. One search box takes part of a home or mod name, a plugin, or a Nexus id (`92177` finds Whitepeak Tower);
   the filter popover (a count badge when one is on) has *Only homes without a template*, *Only enabled mods*,
   *Likely homes only*, *Exteriors*, *Group by mod*. Up/Down and Enter walk it; right-click: *Open*, *Draft*, *Open
   in Explorer* (its template file), *Copy form ref*. *Rescan* (F5) at the bottom reads the load order again after a
   change in MO2, next to the scan summary (`125 homes · 309 plugins · 2.2 s`).
3. **Or any cell of a mod.** For a home the scan misses (owned by a faction, an exterior), *All cells...* under a
   mod group loads the mod's plugins with their masters and scores every cell they define or edit (§3.2). Likely
   homes are listed first, their evidence (player-house location, player owned, never resets, containers,
   displays) in the tooltip; *Likely homes only* and *Exteriors* from the filter apply.
4. **Open its cells.** A click opens the home with its draft (§3.4; every cell of a multi-cell home, §3.3) in the
   viewport. The drawer's *Doors* tab lists where its load doors lead: the home's other interiors (a cellar, a
   cave) and the exterior cells outside (resolved through the worldspace grid, e.g. *Tamriel (-9, -2)
   WhitepeakTowerExterior01*). A double click on a row opens that cell (*Back* returns); its tick joins it to the
   home.
5. **Select.** A click in the plan or the 3D view selects the object under the cursor: containers first, then
   displays, doors, furniture, people, structure; Ctrl+click adds, a drag boxes. The Selection panel shows what it is and
   lets the author give it a role (§3.1). The drawer's tables (Containers, Displays) select the same objects, so a
   hidden chest is reachable even when it cannot be clicked. `S` toggles *Pick anything* (3D: walls and clutter
   too) or *Left drag pans* (plan).
6. **Assign.** Master, categories, label, note, ignored; displays on/off with categories, items, armor set. In
   the Selection panel, by paint mode (§3.7), or by accepting the draft's suggestions in the Draft panel (§3.4).
7. **Validate.** Continuous: core's `ValidateTemplate` and `ValidateLayoutCategories`, plus the plugin-aware
   checks of `esm_dump.py check` (ref is a container in the cell, never a respawning one, displays exist,
   `set` is an ARMO, `requires` complete, §5.6). The status row counts errors and warnings; the Checks panel
   lists them, errors first, a click selects the object, and under *Still to do* what the template lacks (no
   master, 35 containers without a role).
8. **Save.** *Save* (title row, Template panel, Ctrl+S) into `Data/SKSE/Plugins/DovaStash/Templates/<Home>/<Author>.json`
   of a mod chosen by the author (§4.4): under MO2 a "Dova Stash Templates" mod by default (created when missing),
   else an existing mod the author picks (*Setup* or the Template panel). Never MO2's overwrite by default
   (cleaning it would delete the templates). An existing file is backed up first and its version raised, as the
   DLL does (`core::NextPatchVersion`). A home over several cells is one file (§3.3). *Save as...* writes a copy
   anywhere. Both are a person's save: an auto-draft opened and saved loses its `"autoDraft"` mark (§3.4).

The editor also **opens existing files** (*Open a template...*, Ctrl+O): a shipped or downloaded template, a player
layout from `Layouts/` or from the backups in Documents (DESIGN §6.6). That is the path from "I set it up in game" to a template
worth sharing: open the saved setup, rename the labels, add notes, drop the learned items, save as template.

---

## 3. Scope: everything a template can express

### 3.1 Fields and how the editor edits them

| Template part (DESIGN §6.3) | Editor |
|---|---|
| `name`, `author`, `version`, `priority`, `note` | Template panel; version raised automatically on overwrite |
| `requires` | Computed (`core::ReferencedPlugins`), never typed |
| `cell` | The home's main cell (found in steps 2-3) |
| `cells` (multi-cell homes, DESIGN §6.7, `core::HomeCells`) | Cells joined from the drawer's Doors tab or the Selection panel (*Add this cell to the home*), listed in the Template panel with `label`; a home of the rail opens with the cells its draft found |
| `master` | *Make master* on a container; the draft suggests one with its reason (§3.4: the largest usable chest near the entrance) |
| `containers[]`: `label`, `note`, `categories`, `forms`, `ignored`, `sell` | Selection panel: label (default: the game name), note, category picker (tree from the loaded `Categories/*.json`, wildcards `weapons.*`), fixed item bindings (item search over the load order), *Ignore* with a reason note, *Sell chest* (DESIGN §8.7; hidden on an ignored container) |
| `displays[]`: `kind`, `label`, `note`, `categories`, `items`, `set`, `ignored` | Selection panel on a rack, mannequin, plaque, case or bookshelf: *Use this display*, categories (weapon categories for racks, armor for mannequins, books for bookshelves), `items` prefilled from the plaque's script, `set` picked from the armor-set index (`home/ArmorSets.*`: `core::BuildArmorSets` over the load order's ARMO built as the DLL's `game::ArmorIndex`, enchanted copies left out, tempering recipes as families; *Auto* or a set, searchable by set, plugin or piece; with a set chosen the mannequin's categories are hidden, as the set decides) |
| `keep` (`inherit`, `always`, `never`, `quotas`) | Template panel, *Keep rules*: none (the player's own rules apply) until *Add keep rules*; then *Also the player's global rules* (`inherit`), *Always keep* and *Never keep* (category, form ref, `equipped`, `favorited`, `quest`; × removes), *Quotas* (a category and how many), *Remove keep rules*. The same vocabulary as the panel's keep-rule editor; quotas by keywords or forms are kept as they are |
| Unknown keys, unparsed entries | Kept untouched (core preserves them) |
| `learned`, `basedOn` (player layouts only) | Shown when a layout is opened; *Keep learned items as fixed bindings* on export (`core::ExportAsTemplate`) |

Crafting stations need nothing in a template today (DESIGN §8.5 finds them at runtime). The plan shows
them so the author sees which room is the forge; if templates ever name stations, they get a role here.

**Preview: what goes where** (built, §3.5). The editor builds `core::ItemInfo` for every item record in the load
order (keywords, weapon and armor types, book teaches, alchemy flags, soul gem, enchantment) and runs the same
category engine and planner the DLL runs. Selecting a chest shows "would receive 312 items: Iron Sword, ..." and
what falls to the master. This is the one feature only a tool with the whole load order can offer.

### 3.2 Finding the home (implemented)

Every interior cell the plugin defines or edits (exteriors on request) is loaded with its references and
scored; a cell is a **likely home** when its location has `LocTypePlayerHouse`, it is owned by the player
(`Player` or `PlayerFaction`), or it has mannequins and nothing in it respawns (homes owned by a faction,
such as Severin Manor, DESIGN §16). The score ranks them: player house +4, player owned +3, never resets +1,
usable containers +1/+2, displays +1, mannequins without respawning containers +2. Containers, displays and
fronts (activators that open a hidden chest) are found as `esm_dump.py cell` finds them (verified: Whitepeak
Tower 89 containers, 77 racks, 6 mannequins, 50 plaques; Breezehome 24/3 respawning/9 racks; all 9 vanilla,
Hearthfire and Dragonborn homes found in their plugins; Gallows Hall in `Update.esm` and both Myrwatch cells in
the CC ESL with the cell refs of the shipped templates), except that plaques and cases are checked as the DLL
checks them (DESIGN §8.4): the editor reads the compiled script (`scripts\<name>.pex`, loose files over
archives, `esp/Pex.*`) and keeps a plaque only when its script (or one it extends) has a "placed" flag and a
case only when it handles `OnItemAdded`; the Creation Club's display plaques (`defaultDisplayUniqueItemScript`,
`defaultDisplayItemInListScript`: no flag, a `LinkedMarkerKeyword` and `DisplayItem`) count too. A script that
merely names an item (Wind Path's dining table setting out a meal, a grill, an ore vein, a planter) is no display.
`esm_dump.py` cannot read scripts and still lists every script that points at items.

### 3.3 Multi-cell homes

The core schema now has an optional `cells` list (DESIGN §6.7, `core::HomeCell`). The editor's model is a
**home = main cell + more cells**, each with its references; containers and displays keep being named by
reference, so a container in the cellar is simply bound while the cellar tab is open. The door graph the
editor already resolves (interior to interior, interior to exterior grid cell) proposes the candidates:
*Lakeview Manor* links to *Lakeview Manor, Cellar* and its exterior; the author ticks which belong, and gives
each a `label` (what players see; they can rename it in game). **Decided: a multi-cell home is saved as one
file per home** (`cell` + `cells`, DESIGN §6.7), never one file per cell; the shipped `*Cellar.json` files stay
as they are for players who use them, and the DLL keeps loading either shape.

Built: the drawer's Doors tab lists each place the home's doors lead to once (a Hearthfire home has a door per
building stage) with *In the home*; a click selects the door, a double click opens the cell. Opening a door's
cell (a double click on the tab's row or on the door in 3D,
*Open that cell* in the Selection panel) takes the template along, and *Back* (title row) returns. A home of the
rail opens with the cells its draft joined (the home finder's door graph, §3.4). Picking another home in the rail
keeps this home's unsaved draft (its row keeps the dot; *Save N drafts...* and the quit guard cover it); a template
opened from a file asks first. In a cell that is not part of the home the Selection panel offers *Add this cell to
the home*; the Template panel lists the cells (*Main*, *Joined*; *open* marks the one shown) with their labels, × leaves
the home, and *List this cell's containers* adds every container of the open cell at once (the draft's rules:
respawning, scripted and upgrade-removed containers ignored).

### 3.4 Bulk drafts and smart suggestions (built)

The homes rail (§2) lists every player home of the **active load order** (`Workspace::OpenLoadOrder`: the profile's
plugins in `loadorder.txt` order with the base masters) with its template status; a home gets its draft when it is
opened, or all of them at once with **Draft all homes**. That is an action, not a view (*Draft* ▾, the start
panel, Ctrl+D, `--drafts`): the drafts are made on a worker with the progress in the status row; then the rail shows
a *drafted* chip per home and a banner over it offers *Save N drafts...*. It does not advance the homes' stages
(§3.6). `home::FindAllHomes` reads every interior cell once (the 302 plugins of the test profile
in about a second), keeps the likely homes (§3.2), and resolves all their load doors in one pass: interior cells whose
doors lead to each other and are homes too are **one home** (a hall and its cellar), and storage behind a home's door
that nothing respawns in and nobody owns joins it (the main cell is the one whose name is not "cellar", "basement",
"cave"..., then the best score). Status per home (the rail's chip): *none*, *shipped* (Dova Stash's or another
mod's), *yours* (`Templates/User`, the "Dova Stash Templates" mod, overwrite), from every `Templates/**.json` of the
merged Data folder (`home::LoadTemplateIndex`); *auto-draft* (only a file *Save N drafts...* wrote, nobody saved it
since), *drafted* (a draft not saved), *saved* (saved this session).

**The suggestions are core code** (`core::SuggestDraft`, `DraftSuggest.h`, unit-tested in `tests/DraftSuggestTests.cpp`)
over plain facts the editor extracts (`home::BuildDraftObjects`): containers, displays, load doors, loose items (each
classified by the real category engine through `esp::ReadItem`) and furniture, with positions and bounds. The rules are
**data**: `DefaultDraftRulesText()` (schema `dovastash-draftrules-1`), overridden by a
`Data/SKSE/Plugins/DovaStash/DraftRules.json` when there is one (same fields; unknown keys warned, broken rules skipped).

- **Ignored** containers, first matching `ignore` rule, with the note the template writes: respawns, goes away with a
  house upgrade (unless it diverts its items, `BYOHDivertItemScript`), owned by someone else (`XOWN` on the reference),
  scripted bookshelves, display cases.
- **Names come first.** A container's **display name** is what the home's author wrote for players. When it has a word
  beyond the generic ones (`genericWords`: "large", "wooden", "upper"..., plus the master's chest and furniture words) it
  is **specific** ("Dragonpriest Masks", "Food & Drinks", "Arrows & Bolts"): only that name counts, never the EditorID,
  and the container gets **only what its name says** (stations and items nearby add to those categories, never others;
  a specific name the rules cannot read gets nothing, so the author decides). A generic or missing name ("Chest",
  "Barrel") lets the EditorID speak too, at `editorIdFactor` (0.8: "EditorID BarrelIngredientCommon01 says ingredients").
- **Master**: among usable containers (not ignored, present from the start, no script beyond `harmlessScripts`,
  nobody's, in the main cell when it has one): a chest-like name (+1.5; a sack's, barrel's, drawer's or strong box's
  -1), its size up to a small chest's (`fullSize` 100000 cubic units, +1: a huge shelf or urn does not win on volume),
  closeness to a load door (+1 within 1500 units, the front door counts more), closeness to the middle (+0.5), and -3
  when its name says what it holds (a specific name, or a name rule that is sure of it): such a container is only the
  master when every usable one is named. The reason is shown ("Chest: named like a chest, 96 x 49 x 49 units, 5.5 m from
  the door to Windhelm; usable (one of 69): ...") with the runners-up.
- **Categories per container**, each with a confidence and the reason: its **name** (`names` rules: "Alchemy Satchel",
  "Weapon Chest", "Cooking Ingredients" is food; EditorIDs are split into words, plurals match; a container named
  exactly like a **category** of the loaded set gets it, and one named like an **item** of the load order gets the
  item's category as a weak claim: "Garlic Basket"), **stations** near it (forge and smelter: ores and ingots; tanning
  rack: hides; alchemy lab: ingredients, potions, poisons; enchanter: soul gems; cooking pot and oven: food;
  bookshelves: books; 2.5 to 6 m, weaker with distance, another floor counts double; weapon racks and mannequins are
  hints only), and **items placed on or next to it** (by their category, a hint of at most 45%; clutter, weapons,
  armor and ammunition lying about are decoration and say nothing). Evidence for the same category adds up. Each
  category goes to **one** container: one named for exactly that category first, then one whose display name says it
  as part of a group ("Alchemy Satchel" before a barrel whose EditorID says "Ingredient"), then the strongest; it is
  accepted at 50%, the others keep it as a rejected alternative ("Potion Basket has it with 93%"). A **fill** pass then
  gives each usual group nobody took (weapons, armor, clothes, jewelry, alchemy, food, books, soul gems and scrolls,
  ammunition, crafting materials, building materials, clutter, keys) to the free container with the strongest weaker
  hint (a dresser for clothes, an end table for books, a safe for jewelry and gems), at least `fillAt` (30%) and never
  on loose items alone; the container takes the group's categories it has evidence of its own for (a shield hanging
  next to a cabinet does not send it heavy armor). Groups nothing hints at stay with the master.
- **Displays**: every rack, mannequin, plaque and case is listed (plaques and cases with the items their script is
  made for, shield and dagger racks with their category).
- The draft is validated with core (`ValidateTemplate`, `ValidateLayoutCategories`), shown in the Checks panel.

**Review** is the inspector's **Draft** panel for the open home (*Draft* ▾ > *Draft this home*, the rail's right-click
*Draft*, the Selection panel's empty state, `--inspector draft`). At the top *Accept all* (every suggestion no other
container has a stronger claim on), *Reject the weak ones* (under 50%), *Draft again* (from the plugins, after
editing `DraftRules.json`; asks when the template has changes). Then the master with the first clause of its reason
(the whole reason and the runners-up in the tooltip; *Use this one* when the template uses another), and per
container its suggestions as chips coloured by confidence (green: the name says so, yellow: a station or items
nearby, orange: a hint; the legend is the tooltip of the *Containers N of M* head), the source and reason in the
tooltip; a click accepts or rejects. A click on a container's name selects it in the view (one in another cell of
the home opens that cell). The draft's displays are a folded list at the end. For a home that has a template: how
many categories the draft sends to the same container and whether the master is the same (details in the tooltip).
The review **edits the template being made directly**: an accepted chip is the category in the Selection panel, the
preview and the checks at once, and undo covers it. *Save* (Ctrl+S) writes it into the "Dova Stash Templates" mod and
asks first when the file exists (backup, version raised) or the home already has a template. Switching homes in the
rail keeps each home's draft; *Save N drafts...* (≡ or *Draft* ▾ menu, the banner after *Draft all homes*) saves the
drafts of homes without a template after one confirmation listing new files and refreshed auto-drafts. Headless: `--draft-all [--only <text>] [--compare] [--save-to <folder>] [--base-game]` (§5.6).

**Auto-drafts** (DESIGN §6.8). What *Save N drafts...* and `--draft-all --save-to` write is marked `"autoDraft": true`
(a draft the author reviewed first is saved as theirs, without it), so a bulk save never overwrites anyone's template:
before writing, `core::ScanTemplateFiles` reads the destination's `Templates/` and `core::CheckAutoDraftTarget` allows
a missing file or that home's own earlier auto-draft (refreshed: `.bak`, version raised); `home::SaveTemplateFile`
reads the target again just before writing. A file without the mark (hand-made, downloaded, an auto-draft saved since),
one that cannot be read, another home's auto-draft (two homes of the same name share `Templates/<Home>/`), and any
home with a template someone made anywhere in the destination are left alone: the dialog lists those homes by name
(the file in the tooltip) and the command line prints a *not saved* line for each. A home whose only template is an
auto-draft counts as without one (the rail's *auto-draft* chip, the *Only homes without a template* filter). Opening
it and saving (Ctrl+S, *Save as...*) drops the mark: the template is yours, and no bulk draft touches it again. The
Template panel's list marks auto-drafts; in game they rank below every template someone made for the home, whatever
its priority (DESIGN §6.2), and the list is sorted with the game's own ranking (`core::CompareTemplateRank`).

**Accuracy** (`--draft-all --compare --base-game`: the game's own plugins, the homes as the shipped templates were
written for them; 17 shipped templates). Masters: 11 of 17 the same. Of 245 template containers with categories, 20
get the same set and 11 an overlapping one. Per category (which container it ends up in, the planner's rule), 79 of
629 agree (13%): the shipped templates spread every category over generic furniture (Breezehome's dressers, end tables
and sacks) by the author's taste, where names and stations say little, and the draft leaves to the master what it has
no real evidence for rather than guess. Ignored containers: 55 of 255 agree, because the scripted bookshelves the
shipped templates list as ignored containers are read as bookshelf displays now. Homes whose containers are **named**
for their contents (most modded homes: "Soul Gems", "Ingots", "Potion Crate") come out nearly complete. Over the 125
homes of the test profile the review lists about half as many suggestions as before the name-first rules, and no
container gets armor or weapons from decoration or a mannequin across the room. The numbers are a starting point for
tuning `DraftRules.json`.

### 3.5 Where items go (built)

The drawer's **Where items go** tab runs Dova Stash's own planner (`core::PreviewWhereItemsGo`: explicit item
bindings, categories best-first through `SortPlanner::ResolveDestination`, the master; and `PlanDisplays` for the
displays the template uses, as Deposit fills them first) over a **sample**: *Typical haul* (a handful of each kind of
loot, base game items first: 112 stacks, 266 items), *One category* (every item of the load order in a pattern), or
*Every item in the load order* (one of each; `esp::BuildItemCatalog` reads the 12,763 playable items of the test
profile in about 30 ms). It shows per container what it would receive (counts per category, the items in a tooltip),
the containers that receive nothing (and why: no categories, or nothing of the sample in them), what falls to the
master, what goes on displays, and stacks with nowhere to go (no master). It re-runs by itself when the template
changes; *Run again* (R) runs it by hand, the sample picker and the run time sit in the tab's own row. **Colours**
(both the 3D view and the plan; the toolbar's *Colours* ▾ with the legend, `C` / `V`, or *By fill* / *By category* in
the tab's row): *by fill* (receives nothing, a few, many, master) or *by category* (each container in its first
category's colour, one hue per group). Selecting a container shows its predicted items in the Selection panel
(*Where its items go*). Headless: `--preview <cell> [--sample haul|all|<pattern>] [--draft]`.

### 3.6 Start panel and to-do list (built)

Without `--mod`/`--cell`/`--home`/`--drafts` the editor opens on the **start panel**, drawn in the viewport while no
home is open (the emblem in the title row brings it back; Esc returns to the open home): the setup in one line
(instance, profile, mods enabled) with *Change...* (the `⚙` Setup popover; the instance is found from MO2, else the
one remembered; when none is found, a card with the folder, *Browse...* and *Open*, and nothing else), "N player homes,
M with templates" and the scan time, three cards (*Open a home*: focuses the rail's search; *Draft all homes*,
Ctrl+D; *Open a template...*, Ctrl+O: a shipped or downloaded template or a layout saved in game, opened on its home),
and **In progress**: each home's stage, *drafted* (opened with its draft) -> *reviewed* (a suggestion accepted or
rejected, or the template edited) -> *validated* (reviewed and no problems) -> *saved*, with where it was saved. A
home's stage starts when it is opened; *Draft all homes* does not advance it. The stages, the instance, the author,
the save target, the layout (rail, inspector and drawer: open or collapsed, their sizes; the view), the UI size and
the theme persist in `%LOCALAPPDATA%\DovaStashEditor\state.json` (`DOVASTASH_EDITOR_STATE` points tests elsewhere);
a click on a home in progress opens it. A `--tab` on the command line applies to that run only: the file keeps the
remembered layout until a panel is changed by hand; `--theme` never touches the file. The player's own colour themes
are files in `%LOCALAPPDATA%\DovaStashEditor\Themes\` (EDITOR-UX.md §3.1).

### 3.7 Editing helpers (built)

- **Paint mode** (P, or *Paint* in the viewport's toolbar): the paint bar replaces *Layers*, *Colours* and *Camera*
  (the category's colour and chip, *Pick another* ▾, *Done*); click containers (and racks, mannequins) to give them the
  category, a second click takes it away. The master, respawning and ignored containers say why they are not painted.
  Turning it on switches the colours to *by category*, so you see what you paint. P, S, Esc or *Done* return to Select.
  In the command palette, typing a category sets the brush.
- **Multi-select**: Ctrl+click adds or removes a reference (3D, plan, the drawer's tables), a box drag selects the
  containers and displays inside (Ctrl adds). The Selection panel then edits them together: the categories any of them
  has (with how many, a click takes one away from all), *Give all of them* a category, *Clear their categories*,
  *Ignore all* / *Use all*, a note *Set on all*, *Clear the selection*. Esc clears the selection; Delete removes the
  selection from the template (asks when more than one).
- **Undo/redo** for every change to the template (Ctrl+Z, Ctrl+Y or Ctrl+Shift+Z, and *Undo*/*Redo* in the ≡ menu; a
  text field is one step once it loses focus; 200 steps, kept while the template moves between the home's cells).
- **Unsaved changes**: a dot after the home's name in the title row and in the status row, and on the rail's row of
  every home whose reviewed draft is not saved; the title row's chip reads *draft*. Switching homes keeps each home's draft;
  closing the window asks first (the open template or reviewed drafts), and so does a rescan that would drop them.
- **Command palette** (Ctrl+K, or *Commands...* in the ≡ menu): one field over the viewport. Typing matches commands
  first ("save", "draft", "overview", "layers off"), then homes (opens), the open home's containers and displays
  (selects and frames), and in paint mode categories (sets the brush); each shows its key. Esc closes.

---

## 4. Architecture

```
editor/src/
  main.cpp            wmain: command line (headless) or window
  Cli.*               --list-mods, --homes, --cell [--draft], --render-cell, --nif-info, --gui [--home --tab --inspector --select --popover --palette --window --screenshot]
  CliDrafts.cpp       --draft-all [--compare --save-to --base-game], --preview <cell> [--sample]
  Workspace.*         MO2 instance + profile + mods + merged Data view; opens a plugin with its masters, or the whole load order
  mo2/Instance.*      ModOrganizer.ini, profiles, modlist.txt, meta.ini (Nexus id), plugins per mod, VFS detection
  data/DataFiles.*    MO2-style file resolution (later root wins)
  data/Assets.*       meshes and textures: loose files (lazy per-directory index over the mod folders), then archives
  esp/Plugin.*        memory-mapped TES4 reader (groups, records, subrecords, zlib)
  esp/LoadOrder.*     form keys, record index, localized strings, cell directory, cell references, LAND
  esp/Bsa.*           BSA v104/v105 reader (hashed file table; strings, meshes, textures; LZ4 frames / zlib)
  esp/Vmad.*          Papyrus script properties (rack types, plaque items)
  esp/Pex.*           compiled Papyrus scripts: variables, events, parent (plaques and cases as the DLL sees them)
  esp/Items.*         every item record as core::ItemInfo (the DLL's ItemAdapter, read from the plugins)
  home/ArmorSets.*    the load order's armor sets (mannequin set picker, chosen sets in the preview)
  home/Cell.*         cell facts, placed references (+ model paths, keywords, owner), terrain, displays, doors, home finder
  home/Drafts.*       every home of the load order (grouped by doors), draft objects, drafts (core::SuggestDraft), comparison
  home/DovaData.*     the player's categories, templates (with their status) and draft rules
  home/Preview.*      sample inventories and display spots for core::PreviewWhereItemsGo
  home/TemplateFile.* saving a template (backup, version raised)
  render/Nif.*        NIF reader: node tree, BSTriShape (+ SE subclasses, skin partitions), NiTriShape/NiTriStrips
  render/Dds.*        DDS -> Direct3D 11 texture (BC1-BC7, uncompressed, legacy 24-bit, luminance)
  render/Scene.*      the 3D scene: async loading, renderer, camera, picking, framing
  render/Image.*      PNG/BMP writing (WIC), GPU read-back
  ui/Window.cpp       Win32 + Direct3D 11 + Dear ImGui host
  ui/App.*            the workspace (EDITOR-UX.md §2): background jobs, opening cells and homes, the viewport and its toolbar, keys
  ui/TitleBar.cpp     the title row (≡ menu, home switcher, 3D | Plan, Draft, Save, Setup) and the status row
  ui/Rail.cpp         the homes rail: homes by mod, search, filter, chips, "All cells..." per mod
  ui/Start.cpp        the start panel (setup, counts, three cards, homes in progress)
  ui/Inspector.cpp    the inspector: Selection, Template, Draft, Checks
  ui/Drawer.cpp       the drawer: Containers, Displays, Doors, Where items go
  ui/Palette.cpp      the command palette (Ctrl+K)
  ui/View3D.cpp       the 3D view: camera input, picking, overlays
  ui/Plan.cpp         the floor plan
  ui/Roles.cpp        the role editor (Selection panel) and the Template panel (header, cells, keep rules, save)
  ui/Drafts.cpp       homes and drafts: the scan, "Draft all homes", the Draft panel (review), Save N drafts
  ui/Preview.cpp      the "Where items go" tab, container colours and legend
  ui/Edit.cpp         undo/redo, multi-select and the bulk editor, paint mode
  ui/Theme.cpp, Widgets.*, Icons.cpp, Art.*   the look (EDITOR-UX.md §3): tokens, embedded fonts, widgets, drawn icons,
                      optional art from the player's install and mesh thumbnails
  ui/Themes.*         the colour themes: Dova Stash, the built-in dark and light ones, the player's theme files
  ui/State.*          what is remembered between runs (instance, author, save target, each home's to-do stage, layout, theme)
```

Core additions for the editor (engine-independent, unit-tested, usable by the DLL later): `DraftSuggest.h` (the draft
heuristics and their rules file) and `Preview.h` (where items go, over the planner and the displays planner).

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
Whitepeak Tower with its 5 masters in under 0.1 s; all of `Skyrim.esm`'s cells in 0.3 s). The whole-load-order
loader exists (`Workspace::OpenLoadOrder`: 302 plugins, every interior cell read in about a second) and is what the
start panel, the homes rail, the drafts and `--draft-all`/`--preview` use; a mod's *All cells...* in the rail still
opens its plugins with their masters (`Workspace::OpenPlugin`). Left for M1: a mark on references a later plugin edits.

Archives (M2, implemented in `data/Assets`): the base game archives from `Skyrim.ini`'s `sResourceArchiveList`
and `sResourceArchiveList2` (the profile's own INI when it has one, else Documents, else the default list),
then each plugin's `<name>.bsa` / `<name> - Textures.bsa` in load order (the profile's active plugins, then every
other plugin in the merged Data folder, so a disabled home mod opened on purpose still shows its meshes); loose
files win over archives. Loose files are found without walking the Data folder: a directory is listed only in
the mod folders that have its parent, and only when a file in it is asked for (2,000 mods, a few hundred
directories per cell). Archives are opened on the 3D view's loader threads, in parallel.

### 4.4 Writing templates

- Serialization is `core::SerializeLayout`, written atomically with `core::SaveLayoutFile`; overwriting
  first copies the old file to a timestamped `.bak` and raises the patch version, like *Save as template*
  in game (DESIGN §6.5). The file name comes from `core::TemplateFileStem`.
- Target, chosen in the save dialog. **Decided: under MO2 the default is a dedicated "Dova Stash Templates"
  mod**, so clearing overwrite can never delete a template:
  - **Under MO2:** `mods/Dova Stash Templates/SKSE/Plugins/DovaStash/Templates/<Home>/<Author>.json` (one folder per
    home, so every template of a home sits together; the Template panel lists them, the open one marked), written
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
red), racks blue, mannequins magenta, plaques/cases/bookshelves orange, doors yellow, furniture lavender. Wheel zoom,
right/middle-drag pan, a scale bar, a height range to isolate one floor of a tower, layer toggles, hover
tooltips, click-to-select. **Layers** (the *Layers* popover, `L`), one rule for the plan and the 3D view
(`home::LayerOf`): *Structure* (statics, clutter, loose items, plants, candles and lanterns), *Containers*,
*Displays*, *Doors*, *Furniture* (furniture, crafting stations and sit markers, activators with a mesh), *People*
(NPCs; mannequins are displays), *Disabled* (initially disabled references), *Effects* (fog, mist, glows, light rays
and dust beams: an `FX` EditorID, a mist, fog, glow or light-ray name, or a mesh under `meshes\effects`; light
emitters, sounds, explosions, hazards) and *Other* (markers such as room and portal bounds, idle markers and
xmarkers; trigger volumes such as `defaultBlankTrigger` and Hearthfire's crafting triggers; activators and statics
without a mesh). The plan has its own *Effects*, off when the editor starts (their volumes cover the rooms), and
*Other*, off too; the rest are shared with the 3D view. A hidden layer is neither drawn, hovered nor clicked, and
the fit (on opening, on `Home`, and after a layer change until the author pans or zooms) counts the shown layers
only, never effects or *Other*, nor a footprint larger than 40 % of the home. To do in M1: automatic floors
(clusters of floor heights), labels at higher zoom, selection synced with the tables, the role editor in the Selection panel, a marker for objects a later
plugin edits, and rotation on X/Y for tilted footprints (verify the Z-rotation direction against a known
asymmetric object; the footprints of Whitepeak's walls line up, which suggests it is right).

**M2 — 3D view (first slice built).** The *3D* view, switched with the *Plan* in the title row (`1` / `2`); one
selection for both and for the tables. What is in:
- **Meshes: the editor's own NIF reader** (`render/Nif`), not nifly. Decided while building it: Skyrim NIFs
  (20.2.0.7, LE BS version 83 and SE 100) carry every block's size in the header, so a reader that knows only
  the blocks the view needs (nodes, `BSTriShape` with `BSSubIndexTriShape`/`BSMeshLODTriShape`/
  `BSDynamicTriShape`, SE skinned shapes through `NiSkinPartition`, `NiTriShape`/`NiTriStrips` with their data,
  `BSLightingShaderProperty` + texture set, `BSEffectShaderProperty`, `NiAlphaProperty`) skips everything else
  safely, is bounds-checked (a broken file is a box, never a crash) and adds no dependency. nifly stays the
  fallback if more of the format is ever needed. Hidden nodes and editor markers are left out.
- **Transforms, verified:** a reference's rotation is CommonLib's `NiMatrix3::SetEulerAnglesXYZ` (column
  vectors, angles as stored; the plan's footprints already used the Z part), NIF matrices are read as rows, and
  the root node's own transform is ignored because the game replaces it with the reference's. Checked against the
  Creation Kit's object bounds: `--render-cell <cell> --verbose` compares every mesh's bounds with its base
  record's OBND (Breezehome: the remaining differences are furniture, whose OBND includes the sitting space,
  effects and replaced meshes).
- **Textures: own DDS loader** (`render/Dds`): BC1–BC7 and the uncompressed formats straight into D3D11, mip
  levels above 1024 px skipped (4K packs load fast and stay small). Missing textures are a gray checker.
- **Renderer** (`render/Scene`): one pass, diffuse × vertex colours (when the shader asks), a key light from
  above, a headlight and ambient; alpha test, sorted blending, two-sided shapes, effect shaders with their own
  blend modes; reversed-Z depth, 4x MSAA, frustum and sub-pixel culling. Whitepeak Tower (2,134 references,
  387 meshes, 3,348 draws) draws in about 1 ms of CPU; meshes and textures load in under a second on worker
  threads while the view fills in.
- **What is drawn:** every reference with its base object's model (armor: its world model); mannequins with
  their body (the NPC's or race's skin, body addon); other people and missing meshes as solid boxes of their
  bounds; things invisible in game that a template can use (hidden chests on marker meshes, rack and plaque
  triggers, mesh-less activators) as see-through boxes in the plan's colours, room-sized volumes as outlines
  only; markers, sound and light markers are left out. *Disabled* hides initially disabled references (house
  upgrades). *Height* reuses the plan's height range to cut a tower into floors. *Effects* hides glows, fog and
light rays, the effect shaders and the *Effects* layer's references (drawn at half strength: the editor has no
scene lighting to balance them); *Light* sets how bright the editor's light is.
- **Exteriors:** the cell's terrain from LAND (heights, normals, vertex colours, base texture), from the parent
  worldspace when the cell has none (walled cities such as Whiterun), and the 8 cells around it drawn as
  surroundings (not selectable).
- **Camera** (controls in §9): fly with the right mouse button held (mouse look, WASD, Q/E), wheel, middle-button
  pan, Alt + left orbit, F frames the selection (it looks for a side where no wall is in the way, once the walls
  have loaded), Home goes back to the overview.
- **Select** (the toolbar's *Select*): a ray against each reference's bounds, then its triangles. It picks what a
  template can use (containers, displays, doors, furniture); a wall in front still blocks, clutter lying on a
  chest does not (anything within 40 units in front). *Pick anything* (S, or the *Camera* popover) picks walls and
  clutter too. Hover shows the name, reference and
  where a door leads; a double click on a door opens the cell behind it (*Back* returns).
- Not needed: Havok collision, Papyrus, lighting accuracy, LOD, normal maps.

### 4.6 How objects are selected (decided)

1. **Clicking the 2D plan** (works now): select mode turns the mouse into a picker on the floor plan; the
   plan stays as the fast overview later.
2. **The editor's own cursor in a 3D view** (Creation Kit style, M2): select mode picks in the editor's
   3D viewport, sharing the plan's selection model. Matches "without having to be in game".
3. **A live link to a running game** (optional M3, not decided): the Dova Stash DLL sends the reference under
   the crosshair to the editor over a named pipe when the player presses a key, and the editor selects it;
   *Send to game* reloads the edited template in the running game. Cheap given the DLL already tracks the
   crosshair, but it needs the game running. Only if testing in game stays part of the author's loop.

Decision: 1 now, 2 as M2 (§8). Both are built: the plan and the 3D view share one selection, and the
tables select the same objects (a double click in a table frames the object in the 3D view).

### 4.7 Validation (shared)

Core already gives template lint (`ValidateTemplate`), category typos (`ValidateLayoutCategories`) and
`cells` checks (`ValidateHomeCells`). The plugin-aware half of `esm_dump.py check` (references in the
declared cell, containers not respawning, display entries pointing at displays, `set` an ARMO, duplicate
references) lives only in Python today. Proposal: a core function over a small interface of facts the caller
provides (`ICellFacts`: is this ref in this cell, its base type, respawns, display kind), implemented by the
editor from the plugins and by the DLL from the game, so the three places check the same things.

### 4.8 Running it as an MO2 executable

*Tools > Executables > +*: Binary `...\DovaStashEditor.exe`; arguments optional (`--gui --mod 92177` shows a
mod's homes in the rail, `--gui --home "Whitepeak Tower"` opens one); optionally *Create files in mod instead of overwrite* = a "Dova Stash Templates" mod. The
exe is a Windows-subsystem program (no console flash) and finds the instance from its parent process.
A later MO2 Python tool plugin could add *Make a Dova Stash template* to a mod's right-click menu and start
the editor with `--gui --mod "<that mod>"`.

### 4.9 Licensing

The project is GPL-3.0-or-later. Dependencies: Dear ImGui (MIT), zlib (zlib), LZ4 (BSD-2), nlohmann-json
(MIT); the 3D view adds none (its NIF, DDS and BSA readers are the editor's own; DirectXMath, the HLSL compiler
and WIC come with Windows). nifly (GPL-3.0), DirectXTex/DirectXTK (MIT) and rsm-bsa (MIT) stay compatible
options. The editor reads
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
   `--draft`). `--render-cell <cell> --screenshot out.png` draws the 3D view off-screen (`--camera`, `--select`
   `--frame`, `--height`, `--pick x,y` prints what a click there selects, `--verbose` lists the boxes, the bounds
   check and the frame time); `--cell <cell> --draft --save-to <folder> [--author <name>]` saves the draft through
   the same code as the Template panel (`home/TemplateFile`: backup, version raised); `--nif-info` dumps how a mesh
   is read. The window takes the same cell flags plus `--home <text>` (the first home whose name contains it), `--tab
   3d|plan` (the view), `--tab containers|displays|doors|preview` (the drawer on that tab), `--tab template` or
   `--inspector selection|template|draft|checks` (the inspector's panel), `--select <ref>` / `--multi <ref>`,
   `--paint <category>`, `--colours category|fill`, `--palette` (the command palette open), `--popover <id>` (`menu`,
   `setup`, `switcher`, `filter`, `draftmenu`, `layers`, `colours`, `camera`, `paintcat`; `savedrafts` is the *Save N
   drafts...* dialog, with `--drafts` once the bulk draft is done), `--layers <names>` (layers
   on, or off with a leading `-`, comma separated: `structure`, `containers`, `displays`, `doors`, `furniture`,
   `people`, `disabled`, `effects` (the view's own), `other`, `terrain`; e.g. `--tab plan --layers effects,-structure`),
   `--window WxH` (the client size), `--theme <name>` (a colour theme for this run, EDITOR-UX.md §3.1; the
   remembered one stays),
   `--sort <table>:<column>[:desc]` (a drawer table sorted by a column, its header or number; repeat it for more
   columns), `--filter <text>` and `--quick <chip>` (the drawer tab's filter box and quick filters, `--popover kind`
   opens the Kind list); like `--tab` they apply once, for that run;
   `--screenshot shot.png` saves the window once everything is loaded (smoke tests, press kit).
   `--draft-all` drafts every home of the active load order and prints the master with its reason and each container's
   accepted and rejected suggestions (`--verbose`: every reason); `--compare` measures the drafts against the existing
   templates (§3.4); `--save-to <folder> [--author <name>]` saves the drafts of homes without a template as
   auto-drafts, never over a template someone made in the folder, with a *not saved* line naming the file in the way
   (`--include-templated`: also homes with a template elsewhere); `--base-game` reads only the game's own plugins. `--preview <cell> [--sample haul|all|<pattern>]
   [--draft]` prints where items go per container. Window smoke tests: `--gui` alone (start panel), `--gui --drafts`
   (Draft all homes started, the rail showing the homes without a template; with `--home <home>`, or `--only`, that
   home's Draft panel), `--gui --home <home> --tab preview`, `--inspector checks`, `--colours fill|category`, `--paint
   <category>`, `--multi <ref>`, `--palette`, `--popover setup`, `--window 1280x720` (the smallest layout).

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
| **M1** (partly built) | The template editor on the plan: roles in the Selection panel (master, categories with the category tree, label, note, ignored, fixed items; displays with categories, items, armor set), open existing templates and layouts, keep rules editor, Checks panel (core + ported `check`), save into the "Dova Stash Templates" mod with backup and version, multi-cell homes from the door graph as one file per home, automatic floors, undo/redo, whole-load-order loader as the default with the *home plugin + masters only* toggle, golden tests | 3-4 weeks |
| **M1.5** (built) | What goes where: `ItemInfo` from item records, category engine and planner preview per container (§3.5); bulk drafts with smart suggestions (§3.4); start panel, paint mode, multi-select, undo (§3.6, §3.7); the one-workspace UI (EDITOR-UX.md). Left: tuning the draft rules against more homes | done |
| **M2** (first slice built) | 3D view: archives + loose files, NIF meshes, DDS textures, D3D11 renderer, free-fly and orbit camera, ray picking, selection shared with the plan, terrain and surroundings for exteriors. Built: all of it (own readers instead of nifly/DirectXTex, §4.5), plus the role editor and saving from M1. Left: instancing (not needed so far), normal maps / specular, LOD meshes for far exteriors, water planes, alternate textures (MODS), heads for mannequins, joining a door's cell to the home (`cells`) from the 3D view | 1-2 weeks left |
| **M3** (optional) | Live link with the running game (crosshair pick, send template to game); MO2 right-click tool plugin | 1-2 weeks |

---

## 8. Decisions

Taken with the user after M0; the sections above are written to match.

| # | Decision | Where it lands |
|---|---|---|
| 1 | **Selecting objects**: by clicking on the 2D floor plan now; a Creation Kit style 3D view with the same select mode next (M2). A live link to the running game stays optional (M3), undecided | §2 step 5, §4.5, §4.6 |
| 2 | **Shipping**: the editor ships **inside the main Dova Stash zip** (the one-zip release rule: one download, short notes, no side downloads). `DovaStashEditor.exe` lives in the mod folder; MO2 runs it from there (§4.8) | §6, §4.8, the release checklist |
| 3 | **Default save target under MO2**: a dedicated **"Dova Stash Templates" mod** (created when missing), never overwrite by default, so clearing overwrite cannot delete a template. Another mod can be picked (*Save to* in the Template panel or the Setup popover) | §2 step 8, §4.4 |
| 4 | **Which plugins**: the **whole load order by default** (the home as the player sees it, patches included), with a toggle *Home plugin + masters only* (the home as its mod ships it). M0 only has the second; M1 adds the first and flips the default | §4.3, §7 (M1) |
| 5 | **Multi-cell homes**: saved as **one file per home** (`cell` + `cells`), never one file per cell. The shipped `*Cellar.json` files stay for players who use them; the DLL loads both shapes (DESIGN §6.7) | §3.3, §4.4 |

State against these: the 3D view (decision 1) is built (§4.5); saving goes into the "Dova Stash Templates" mod
(decision 3: created with a `meta.ini` when missing, an existing file backed up and its version raised); the
whole-load-order loader (decision 4) is built and used by the start panel, the homes rail and the drafts (§3.4); a
mod's *All cells...* still loads its plugins with their masters. A home over several cells (decision 5) is saved as one file with `cells` (§3.3).

---

## 9. How to test the editor yourself

**Build** (VS 2022 Build Tools, `VCPKG_ROOT=E:\vcpkg`; from the repository folder):

```powershell
cmake --preset editor
cmake --build --preset editor-release
```

The program lands in `build\editor\editor\Release\DovaStashEditor.exe` (one file; copy it anywhere, e.g. next to
MO2's other tools).

A players' guide with screenshots (install, every view, controls, FAQ) is `presskit/guide/DovaStashEditor-UserGuide.pdf`
(sources and how to rebuild it: `presskit/guide/README.md`).

**Add it to MO2:** *Tools > Executables* (the gears icon) > **+** > *Add from file...*:

| Field | Value |
|---|---|
| Title | `Dova Stash Editor` |
| Binary | `E:\AI Projects\Dovastash\build\editor\editor\Release\DovaStashEditor.exe` (or where you copied it) |
| Start in | the folder of the exe |
| Arguments | `--mo2 "E:\modding\Kexodus Skyrim" --gui` (optional: started from MO2 it finds the instance itself; add `--home "Whitepeak Tower"` to open it directly) |
| Create files in mod instead of overwrite | leave off: the editor writes its templates straight into the "Dova Stash Templates" mod folder |

Pick it in MO2's run list and press *Run*. Outside MO2 it works the same from a terminal or Explorer with
`--mo2 "<instance folder>"`.

**Controls**

| Where | Input | Does |
|---|---|---|
| 3D view | hold right mouse | look around; with it held: W/A/S/D move, Q/E down/up, Shift 4x faster, Alt slower, wheel = speed |
| 3D view | wheel / middle drag / Alt + left drag | move forward or back / pan / orbit around the selection |
| 3D view | left click / double click | select (what select mode allows) / on a door: open the cell behind it |
| 3D view | F / Home | frame the selection / overview |
| Floor plan | wheel, right or middle drag, left click | zoom, pan, select (*Left drag pans*: left drag pans too) |
| 3D view, plan | Ctrl + click / left drag | add to or remove from the selection / box select (Ctrl: add) |
| 3D view, plan | S / P / C / V / L | *Pick anything* (3D) or *Left drag pans* (plan), in paint mode back to Select / paint mode on or off / colours by category / colours by fill (the preview) / the Layers popover |
| Drawer tables | click / Ctrl + click / double click | select / add to the selection / select and frame it in the 3D view (*Doors*: open the cell behind the door) |
| Drawer tables | header click / Shift + header click / *Filter rows*, Esc | sort by the column (again: descending, then the table's order) / add the column to the sort / filter the rows, clear the box |
| Homes rail | Up/Down, Enter / right-click | walk the list, open the home / Open, Draft, Open in Explorer, Copy form ref |
| Title row | *Back* (<), home switcher, `3D | Plan`, *Draft* ▾, *Save*, `⚙` | the cell opened before / another home / the view / draft this home, accept, reject the weak ones, draft again, draft all homes, save N drafts / save the template / Setup |
| Workspace | Ctrl+K / Ctrl+1 / Ctrl+2 / Ctrl+3 / 1 / 2 | command palette / homes rail / drawer / inspector / 3D view / plan |
| Anywhere in a home | Ctrl+Z / Ctrl+Y (Ctrl+Shift+Z) / Ctrl+S / Delete / Esc | undo / redo / save the template (asks before overwriting) / remove the selection from the template / close a popover or the palette, then clear the selection |
| Where items go | R | run the preview again (it re-runs by itself when the template changes) |
| Everywhere | Ctrl+D / Ctrl+O / F5 | draft all homes / open a template file / rescan the load order |
| Everywhere | Ctrl+= / Ctrl+- / Ctrl+0 | UI size larger / smaller / auto (also the slider under `⚙` Setup › *UI size*; remembered) |

**A 2-minute test**

1. Start the editor from MO2. The start panel counts the homes of your load order; the rail on the left lists them
   by mod. Type `92177` in the rail's search and click *Whitepeak Tower*: the tower shows in 3D from above while
   meshes load (progress in the status row).
2. Hold the right mouse button and fly in with W; let go and roll the wheel. *Layers* ▾ (L): tick *Only between*
   under *Height cut* and narrow the range to see one floor. Press Home to get the overview back.
3. Hover a weapon rack (a blue see-through box shows its trigger): the tooltip names it. Click it: it turns
   yellow and the Selection panel on the right shows it; tick *Use this display* and add a category (*+ add*,
   e.g. `weapons.bows`). Press F to frame it.
4. Press 2 (plan): the same rack is selected there. Click a chest on the plan, press 1 and F: the camera finds it.
   In the Selection panel press *Make master* or add categories.
5. *Template* panel: type your name as *Author*. The status row says `0 errors · ...`; click it for the Checks panel
   (the same checks the DLL runs, and what is still to do). Press *Save* (Ctrl+S). It is written to the "Dova Stash
   Templates" mod: `mods\Dova Stash Templates\SKSE\Plugins\DovaStash\Templates\Whitepeak Tower\<Author>.json`. In
   MO2 press F5 and enable the new mod. Saving again asks first, keeps a `.bak` and raises the version.
6. Drawer, *Doors* tab (Ctrl+2 opens the drawer): double-click the exterior (*Tamriel (-9, -2)*): the tower on its rock
   with the terrain and the cells around it. *Back* (<, title row) returns to the interior.
7. Drawer, *Where items go*: the typical haul against your template: which chest gets what, which get nothing, what
   falls to the master. Press V: the chests in the view above are coloured (grey: nothing, green: a few, orange:
   many, magenta: the master; the key is in *Colours* ▾). Click one: the Selection panel lists its items.
8. Press P (paint), *Pick another* ▾ `alchemy.ingredients`, click two satchels: they turn green (*by category*). S
   returns to Select. Ctrl+Z takes it back, Ctrl+Y redoes it. Drag a box over a row of chests: the Selection panel
   edits them together.
9. Ctrl+D (*Draft all homes*): progress in the status row, then a *drafted* chip on every home and a *Save N
   drafts...* banner over the rail. Open a home without a template and pick the *Draft* panel: its master with the
   reason, each container's suggestions as chips (hover for why). Click a chip off, another on: the Selection panel
   and the preview follow. Open another home and come back: the draft is kept (the dot on its row).
10. Ctrl+K, type `layers off`, Enter: only containers and displays are left. Ctrl+K, type a container's name: it is
    selected and framed.
11. Click the emblem: the start panel lists the homes you opened, reviewed and saved with their stage, also after a
    restart. Esc returns to the open home.

Without the game or MO2 running anything, `--render-cell` shows the same view as an image:
`DovaStashEditor.exe --mo2 "E:\modding\Kexodus Skyrim" --mod 92177 --render-cell bcr58WhiteTower --screenshot wt.png`.
