# Dova Stash Editor — UX specification (the "one workspace" pass)

> Companion to `docs/EDITOR.md` (what the editor does) and `docs/DESIGN.md` (the template schema). This file
> says what the editor **looks and feels like**. When the two disagree, this file wins for the UI and
> EDITOR.md wins for behaviour. Status: specified 2026-10-13, built on branch `editor-ux`.

The in-game side of Dova Stash borrows the player's own UI so it feels like part of the game. The editor
must earn the same feeling on the desktop: **minimal chrome, the home as the hero, every power one key or
one click away**. The functionality is good; this pass is about reach, hierarchy and look.

---

## 1. Principles

1. **One job, one window.** Find a home, see it, give its chests jobs, check, save. There are no "modes":
   the start page, the home list, the drafts and the editor are panels of one workspace that never swaps out
   from under the author.
2. **The home is the hero.** The 3D view or the plan takes the largest area at all times. Chrome is one
   title row and one status row. Everything else collapses, floats over the view or lives in a popover.
3. **Progressive disclosure.** Setup, filters, layers, colours and camera are popovers. A first-time author
   sees three things: the list of homes, the home, the inspector. A power user finds the rest in one click
   or one shortcut, never by scrolling past it.
4. **Every suggestion carries its reason**, in one short line, with the full story in the tooltip. Same for
   every problem the checks find: one line, click goes to the object.
5. **Keyboard parity.** Everything in the toolbar has a key. `Ctrl+K` opens a palette that finds a home, a
   container, a category or a command by typing.
6. **Looks like the art set.** The press kit's brand (`presskit/README.md`, Style): black, white and neutral
   grays only, Cinzel capitals tracked wide for the wordmark and titles, Jost for text (the kit's own fonts,
   in the repo under the OFL), the emblem as the only ornament, no game assets. The home's own meshes as
   thumbnails are data, not decoration, and stay; so do the viewport's data colours (categories, fill, the
   see-through boxes).
7. **Terse text.** Labels are nouns, buttons are verbs, no prefixes ("Suggested categories:" is just the
   list), no sentences that restate what the control already shows. The user's rule for in-game text applies
   here too.
8. **Never lose work.** Unsaved changes are visible (a dot in the status row and in the home list), every
   destructive action asks once, undo covers every edit.

---

## 2. The workspace

```
+------------------------------------------------------------------------------------------------+
| [≡]  ◆ Dova Stash Editor      Aldamar ▾  ● unsaved        [3D | Plan]     [Draft ▾] [Save] [⚙] |  title row
+----------+-----------------------------------------------------------------+-------------------+
| Homes  ⌕ |  ┌ Select  Paint ┐ ┌ Layers ▾ ┐ ┌ Colours ▾ ┐ ┌ Camera ▾ ┐       | Selection Template|  inspector
| [search] |                                                                 | Draft  Checks     |  segments
| ▾ filter |                                                                 |-------------------|
|          |                                                                 | Potion Basket     |
| Aldamar● |                         3D view / plan                          | Eli_Druid…|0xB1B  |
| Lakeview |                                                                 |                   |
| Breezeh. |                                                                 | Role              |
| ...      |                                                                 |  Label  [        ]|
|          |                                                                 |  Categories       |
|          |  hint: Right mouse: look · WASD: fly · F: frame · Home: overview |  alchemy.potions ×|
| ‹        |                                                                 |  Add a category   |
+----------+-----------------------------------------------------------------+-------------------+
| Containers  Displays  Doors  Where items go                                         ▴ drawer   |  bottom drawer
+------------------------------------------------------------------------------------------------+
| Aldamar · 0Druid        0 errors · 2 warnings        loading meshes 403/433         Ctrl+S save |  status row
+------------------------------------------------------------------------------------------------+
```

### 2.1 Title row (one line)

- `≡` menu: Open template file… (Ctrl+O), Rescan load order (F5), Draft all homes…, Save N drafts…, Undo,
  Redo, Shortcuts…, About. Nothing here is only here: each item also lives where it is used.
- Emblem + "Dova Stash Editor" in Cinzel. Clicking the emblem opens the start panel (§2.6).
- **Home switcher**: the open home's name as a dropdown. The dropdown is the same searchable list as the rail
  (§2.2) in compact form; typing filters it. Next to it: the unsaved dot, the template status chip
  (one word, §2.2: *yours*, *shipped*, *auto-draft*, *draft*, *no template*) and, when the home has more than one
  template, an *N templates ▾* dropdown listing them.
- Centre: `3D | Plan` segmented control (keys `1` / `2`).
- Right: **Draft ▾** (Draft this home, Accept all, Reject the weak ones, Draft again, Draft all homes…),
  **Save** (Ctrl+S; disabled with a tooltip when the checks have errors; the filled button only when there is
  something to save, a change or an unsaved draft; an unchanged template is a framed *Save a copy*, or framed *Save*
  when it is already the file Save writes, the Template footer's same button), `⚙` **Setup** popover (§2.7).

> **Built:** the chip reads *no template* / *yours* / *shipped* / *auto-draft* / *draft* (neutral / ok / info / warn /
> warn; *auto-draft*: the home's template, or the open file, is one *Save N drafts...* wrote, §2.8), or *off* (warn)
> when the open template's `conflicts` names a plugin of the load order: the game does not offer it, and the
> tooltip says what the in-game list says, *Not offered: a mod that rebuilds this home is loaded.*, with the plugin.
> The chip opens the
> Template panel; its tooltip says whose it is and the file. The chip names what is open: a home opens on the
> template the game offers first (*yours* / *shipped* / *auto-draft*, nothing to save), or on its draft when it has
> none or the draft has unsaved changes; the Draft panel's *Use the draft* (a plain button, not the accent one: the
> template is often better; *The draft and this template: N of M categories the same* under it) opens the draft in
> place of the template, and the chip reads *draft* whenever the editor's draft is open (a home without a template too: one
> rule with the rail's chip). *N templates ▾* shows the Template panel's list of the
> home's templates in the game's order, the same list and count as the rail's row (every cell of the home: the
> templates of another cell, a cellar, under that cell's name, *Cellar*); click one to open it
> (`--popover templates`). Under
> ~1400 px the title collapses to the emblem, and a long home name ends in "..." (the whole name in its tooltip),
> so the row never runs into `3D | Plan`.

### 2.2 Left rail: Homes (Ctrl+1 toggles, remembers width)

- A search box at the top: home or mod name, plugin, Nexus id. Instant filter. A hint too wide for the box drops
  its last part (*Home, mod, plugin*), then reads *Search*.
- A filter popover (funnel icon, shows the count of active filters after it, in accent): *Only homes without a
  template*, *Show homes with no containers* (off by default: a cell found as a home with no usable chest and no
  template is hidden, unless open), *Only enabled mods*, *Likely homes only*, *Exteriors*, *Group by mod* (default on).
- One list that replaces **By mod**, **Any cell** and the drafts list:
  - Grouped by mod (MO2 name, then `Nexus 65088 · 2 homes` muted), the game's Data folder last. The name is what an
    author scans for: the Nexus id shows only beside the whole name, else the note is the count (while the name
    keeps about 12 characters, then none) and only then is the name cut; the group's tooltip has the full name and
    the Nexus id.
  - A home row: name, a muted summary in words (`89 chests · 170 displays · 2 cells · 3 templates`; cells and
    templates only when more than one), one status word at the right (*yours* / *shipped* / *auto-draft* / *draft*:
    a 6 px dot in the status colour and the word in muted, never a pill per row, §3.5; *off* when every template of
    the home names a loaded plugin under `conflicts`; *none* in muted, without a dot), the unsaved dot when it is
    open and dirty. No "+N" anywhere: counts are words. A home row under its mod's group starts its name on the
    group label's keyline (past the chevron), and so does the *All cells…* link. A narrow rail (under 260 px at
    100 %: a large text size, a dragged-in rail) drops the status word on every row at once, not on the rows whose
    names it would cut, so the list reads one way: the dot stays, *none* goes (the row's tooltip has the word); a
    summary that does not fit is left out, never cut to an ellipsis. The cell count is the template's when the home has one
    (a Hearthfire home's Grounds count). Two homes of one name (vanilla *Breezehome* and a mod's new cell of that
    name) add their cell's EditorID after the summary (`Breezehome  114 chests · WyntersBreezehomeRedux`), the
    EditorID alone in the narrowest form.
  - *All cells…* (a mod group's right-click menu; a footer link only under the open home's group, so the rail stays
    a list of homes) turns on the scored "any cell" listing for that mod only (what *Any cell* does today), with its
    evidence in the tooltip and *Likely homes only* respected.
  - Right-click: Open, Draft, Open in Explorer (its template file), Copy form ref.
- The row of the open home is highlighted; Enter opens the selected row; `↑`/`↓` move.
- Bottom of the rail: *Rescan (F5)* and the scan summary in one muted line (`124 homes · 302 plugins`; the last scan's time is *Rescan*'s tooltip); a
  rail too narrow for it (a large text size) keeps `Rescan · 124 homes`, the full line in the tooltip. A hairline in
  `line` on the list's bottom edge parts the scrolled list from this footer; scrolled, the same rule on its top edge
  parts it from the search box.
- Collapsed, the rail is a 32 px strip with the `›` handle and the home count.

> **Built:** the storage summary counts what a template can use over the home's cells: containers that do not
> respawn, are nobody else's and are not removed by a house upgrade, and displays (counted on the scan worker;
> the home finder's own counts include every container reference). A narrow rail drops the displays first, then
> the chests (the cells and templates stay), then ends the summary in "..."; the title row's home switcher shows
> only the cells and templates. *yours* covers a template saved this session. The row's tooltip shows the counts
> with icons, each template (its word, name, author and mod, in the game's order) and the other cells by name.

### 2.3 Centre: the viewport

- The 3D view or the plan, never both; the segmented control in the title row switches (keys `1`/`2`).
- **Floating toolbar** at the top-left inside the viewport, translucent, one row:
  `Select (S)` `Paint (P)` as a two-state pair · `Layers ▾` (the current nine checkboxes, plus *Height cut*
  with its slider) · `Colours ▾` (None / By category / By fill, keys `C` and `V`, with the legend inside the
  popover) · `Camera ▾` (Overview (Home), Frame (F), fly speed, light, outlines). Nothing else on the view.
- Paint mode replaces the toolbar's second half with the paint bar (category chip, *pick another…*, done).
- **Hint line** at the bottom of the viewport, muted, one line, changes with mode (select, paint, box); a dark
  plate (75%, the popovers' rim) behind the text alone keeps it readable over a bright scene. A view too narrow for
  the line drops whole `Key: action` pairs from its end, never half a key name.
- Load progress of meshes and textures appears in the status row, not over the view.
- Hovering an object shows its name in a small tooltip near the cursor; selection keeps the yellow frame.
- The start panel (§2.6) draws **inside the viewport area** when no home is open.

> **Built:** `S` is not a plain two-state key: in paint mode it leaves paint for select; in select mode it toggles
> *Pick anything* (3D: walls and clutter too) or *Left drag pans* (plan), also in the Camera popover. The plan
> fits as the 3D overview frames: to the largest group of references 1024 units apart or closer (the most
> containers), then 2 % trimmed per side (a stray marker or a far cluster no longer shrinks the home), keeping
> the containers, displays and doors near the rest; it re-fits while the viewport settles until the author pans or
> zooms, and on `Home`.

> **Built:** the *Layers* popover keeps its nine checkboxes, and one rule (`home::LayerOf`, EDITOR.md §4.5) puts a
> reference in them for both views: effects (fog, mist, glows, light beams, light emitters) under *Effects*, trigger
> volumes, markers and room bounds under *Other*. *Effects* is per view: the plan's starts off (their volumes covered
> the rooms in faint boxes), the 3D view's on; the popover shows the one of the current view. *Other* is the plan's
> only (off). Hidden layers are not hovered or clicked, and a layer change re-fits the plan, never to an effect or a
> trigger, until the author pans or zooms. `--layers` sets them for one run. The 3D view adds *Missing meshes*
> (off): walls and clutter whose mesh is not installed (a home overhaul that ships only its plugin) are 25%
> see-through boxes that clicks pass through; off by default, as a mod's missing roofs still hid half a home. A click
> on the status row's *N meshes, N textures missing* turns them on (`--layers missing`); a container, display, door or piece of
> furniture without its mesh stays a solid, clickable box either way.

> **Built:** an interior's 3D view has a roof cut, off by default (its missing walls and ceilings looked like
> missing meshes; *Layers* ▾ › *Roof* starts ticked), per area: 512-unit squares each cut 2 m above
> their own floor (the lowest container, furniture or door there), never above the highest container + 120 units.
> Above it only containers and displays are drawn and picked, so the overview shows a cave's rooms instead of its
> rock shell, the lower cave too (Wuth Rein), and a house's rooms instead of its roof. *Layers* ▾ › *Roof*
> (interiors, 3D) unticked turns it on; `--layers -roof` too. A view narrower than 900 px shows the toolbar as icons (*Select*, *Paint*, *Layers*,
> *Colours*, *Camera*), their names in the tooltips.

### 2.4 Right inspector (Ctrl+3 toggles, 380 px, remembers width)

A segmented control at the top: **Selection · Template · Draft · Checks**. Each is a scrolling panel.

- **Selection**: what is selected and its role editor (today's details pane + role editor + bulk editor for a
  multi-selection), then *Where its items go* for that container (today's preview details). The reference
  and base record are one muted block with a copy icon, not a form. Empty state: "Click a chest, rack or
  mannequin. `Ctrl+click` adds, drag boxes." plus three buttons: *Make the nearest chest the master*,
  *Draft this home*, *Open the containers list*.
- **Template**: name, author, version, priority, note (wrapped, as tall as its text up to 5 lines); the cells of
  the home with their labels (join or leave a cell here and from the Doors list); keep rules; the save target;
  *Save* / *Save as…* (*Save* is the title row's button: filled only with something to save, else *Save a copy*
  or *Save*, the same tooltip; the command palette's first entry has the same word).
- **Draft**: today's draft review for **this** home: the master with its one-line reason and runners-up in a
  tooltip, then per container the suggested categories as chips (green / yellow / orange by confidence,
  reason in the tooltip), tick to accept. *Accept all*, *Reject the weak ones*, *Draft again* at the top.
  Clicking a row selects that container in the view. The panel is where "Open in the editor" used to lead:
  there is no longer anywhere else to go.
- **Checks**: the problems list (errors first), each one line, click selects the object; *Validate* is
  continuous so there is no button. Also shows what the template still lacks (no master, N containers
  without a role, N containers not listed, *N containers in this cell not listed* in a home of more than one cell since the count walks the open cell alone) as links. A loaded `conflicts` plugin adds a warning first: *Not
  offered in game: <plugin> rebuilds this home*, with the template's reference checks under it as information
  (an *i*, not an error: they describe the rebuild, and *Save a copy* stays open).

The inspector's panel switches automatically when it helps: selecting something switches to Selection;
*Draft this home* switches to Draft; clicking the status row's problem count switches to Checks.

> **Built:** the Selection panel shows the object's own mesh (96 px) at the left of its name once the scene has
> loaded, and its kind with an icon (a container's in the Containers table's word, *Basket*, *Chest*; *Container*
> when unrecognised). The Template panel's fields scroll and *Save* / *Save as…* / *Copy JSON*
> stay pinned at its bottom like a form footer (framed buttons, *Save* filled when there is work to save); *Version*
> and *Priority* share a row, labels over fields. The Draft panel's colour legend is one muted line of dots under
> its *CONTAINERS 20 accepted · 52 in the home* head (*N accepted of M* in a one-cell home; *75%+ · 50 to 74% · under 50%*, the confidence bands the chips are coloured by); an accepted chip has a check before its text. A category
> chip names the category (*Swords*), its id in the tooltip. *Remove from the template* is the panel's last row, a
> framed button with a bin in `error`. A display without a game name is named by its kind (*Mannequin*, never
> `PlayerHouseMannequin`), and kinds read in sentence case (*Rack (any)*). A container has one name in every panel (Draft, Where items go, the tables, the
> Selection heading): the template's label (a landmark, `Barrel (by the grindstone)`) first, else the game's name.
> Draft rows still of the same name add their height above the cell's floor (`Barrel · 1.3 m`, the tables' rule,
> §2.5), then a number when that ties too; hovering a row highlights its container in the 3D view. With a template
> open the panel starts *Showing the template. Accept a chip to change it, or use the whole draft.* The *Not listed*
> line (`106 containers not listed (174 more come with upgrades)`, *containers in this cell* in a home of more than one cell, upgrades being initially disabled or
> enable-parented refs) opens the Containers tab on its *Not listed* chip; for a shipped or saved template it sits
> under its own quieter *Not listed* head instead of *Still to do*.

### 2.5 Bottom drawer (Ctrl+2 toggles, `L`)

Tabs: **Containers · Displays · Doors · Where items go**, the wide tables of today. The drawer opens at a
third of the height, drag to resize, remembers. Rows select the same object as the view (and frame it on
double click). *Where items go* keeps its sample picker and colour buttons in its own tab bar row.

> **Built:** the tabs show their counts as small muted text after the label (no pill, §3.5): the usable ones, the rail's count for this cell (not
> respawning, nobody else's, not removed by an upgrade), the count alone (*N of M* in the tab row is only ever the
> filter bar's, shown of total); in a home of more than one cell the home-wide figure is in the tooltip (*252 here ·
> 253 in the home*), with how many more this cell has that are not usable;
> the Draft panel's *Containers N accepted of M* counts the accepted ones of the same home-wide number, *N accepted · M in the home* when the home has more than one cell; the tables' first
> column has the object's icon. Every
> column sorts (header click, Shift+click adds a column, a third click returns to the table's order); the filters
> live in the column headers, as a data grid's do: a column with filters ends in a funnel (Containers: *Kind* with
> the objects' kinds and their counts, *Role* with *Master*, *With categories*, *No categories*, *Ignored*, *Not
> listed*, *Notes* with *Respawning*; Displays: *Kind* with the display kinds the home has, *Used* with *Used*,
> *Unused*; Where items go: *Fill* with *Master*, *Receives items*, *Receives nothing*), accent while one of its
> filters is on, and a click opens the checks with *Clear* under them; one of a column's set is enough, columns
> combine. The tab row ends with "N of M" when rows are hidden and the *Filter rows* box; per table, for the
> session. `--quick <filter>` turns one on, `--popover kind` opens the Kind column's funnel.
> Containers opens in its own order: the master, with categories, without, ignored, not listed, those that start
> disabled last in each. Doors has no *Open* column: a
> double click on a row opens the cell, a click selects the door. The drawer's height is a share of the window's
> (at most half of it); the rail and inspector together take at most 60% of the width, so at 1280x720 the view
> keeps its room, and the view has a floor (§6): 45% of the width and 560 px, half the middle's height and 240 px,
> at every UI size; where it bites the inspector gives way down to 30% of the width. Number columns have their name
> and sort arrow flush right over the figures; the *Reference* column is muted mono, and when every row shares one
> plugin it shows the local id alone (`0x00000E68`) with the plugin in the header's tooltip (the filter still finds
> the full ref). The *Kind* funnel in a low drawer opens above its header, its counts on one right edge. Cells speak words: category names (*Swords*, *Books (all)*; ids in the tooltip and the filter),
> scripts as what they do (*bookshelf*, *Hearthfire build*, *critter spawn*), *Height (m)* in metres above the cell's
> floor, the unit of the Draft panel's `· 1.3 m` (its last way to tell rows of one name apart, after the cell, the
> wing, the nearest station and the side of the room: `Cupboard · Kitchen · by the oven · east`): the lowest containers there from the start that stand together
> (another within 400 units; one left alone below does not count), leaving out any more than 2000 units from the
> median (a home overhaul parks the vanilla containers it removes far below; those read `-` and their Notes *out of
> the home*). *Containers* lists the containers that are not displays (bookshelves and cases are the *Displays*
> tab's); the Displays table's *Used* column says *yes*, the categories, or *ignored*.

### 2.6 Start panel

Shown in the viewport area when nothing is open (first launch, or after the emblem is clicked). One
column, centred, in this order:

1. Setup status in one line (instance, profile, mods enabled) with *Change…* opening the Setup popover.
   When no instance is found: a card with the path field (*MO2 instance or Skyrim Data folder*), *Browse…* and
   *Open*, and a muted hint, *Pick your MO2 instance (the folder with ModOrganizer.ini) or your Skyrim Data folder,
   or start the editor from MO2.* (the console's error names the flags instead). A folder with `ModOrganizer.ini`
   opens as the instance, one with `Skyrim.esm` (or a game folder with `Data\Skyrim.esm`) as a plain Data folder,
   remembered for the next start.
2. "124 player homes, 17 with templates" (the plugin count is the rail footer's and the scan time *Rescan*'s tooltip, not repeated).
3. Three cards: *Open a home* (Ctrl+F: focuses the rail search), *Draft all homes* (Ctrl+D), *Open a template…*
   (Ctrl+O). Short one-line descriptions, no paragraphs.
4. *In progress*: the homes with a stage, as a compact table (home, stage chip, when, saved to).

> **Built:** the header sits on plain ink: the brand pass (2026-10-14) took the game texture out again, as the art set has no game assets.

### 2.7 Setup popover (`⚙`)

MO2 instance path with *Browse…*, profile dropdown, *Author* name, *Save templates to* (the target list), 
*Rescan*. The one-line summary of the current setup. Closing applies.

> **Built:** the summary is one line ("N of M mods enabled"), the full description in its tooltip. After *Save
> templates to* come *Theme* (§3.1: Dova Stash, Dark, Light and Yours, applied on pick, with *Export...* under it)
> and *UI size* (§3.2).

### 2.8 Draft all homes

Not a mode. The `≡` and **Draft ▾** menus (and Ctrl+D) run the bulk draft; progress shows in the status row;
when it is done the rail shows a *draft* chip per home and a banner over the rail (*N drafts  Save all…*; the
count once, the link does not repeat it) opens the existing *Save N drafts* confirmation dialog. Reviewing a draft means opening the home and using the Draft panel.
`--drafts` on the command line opens the workspace with the bulk draft started and the rail filtered to
homes without a template.

> **Built:** the bulk draft does not advance the homes' stages (the start panel's *In progress*); a home's stage
> starts when it is opened. The rail's *draft* chip shows for every home with a draft.

> **Built:** *Save N drafts...* writes auto-drafts: each file is marked `"autoDraft": true` (DESIGN §6.8; a draft
> the author reviewed first is saved as theirs, unmarked) and never replaces a template someone made. The dialog
> counts new files and refreshed auto-drafts, only those that are not 0 (`78 new, 2 refreshed (backed up as .bak,
> version raised).`), its folder in the system's slashes, then lists the homes left alone by name in `warn` (the file in the way
> in the tooltip); an auto-draft is not the author's save, so it does not advance the stage. A home whose only
> template is an auto-draft shows *auto-draft* (`warn`, like *draft*) in the rail, the title row and the
> Template panel's list, and still counts as without a template. Saving it (Ctrl+S) drops the mark. With no author
> name yet the dialog asks for it, as the single *Save* does (never "Unknown"): *Save them* waits for it, and it is
> kept as Setup's *Author*.

### 2.9 Command palette (Ctrl+K)

One text field over the viewport. Typing matches, in this order: commands ("save", "draft", "overview",
"layers off"), homes (opens), containers and displays of the open home (selects and frames), categories
(in paint mode, sets the brush). `Esc` closes. Items show their shortcut on the right.

> **Built:** *Accept all suggestions*, *Reject the weak suggestions* and *Draft again* are offered only while the
> open home has a draft, *Save N drafts...* only when N is more than 0.

### 2.10 Status row

Left, on the rail's 16 px keyline: home · cell (muted), the unsaved dot; *No home open* without one (the home count is the start heading's and
the rail footer's). Middle: `0 errors · 2 warnings` (click opens Checks), each count in its
status hue only when it is not 0, starting on the viewport's keyline (its left edge plus 8 px, under the floating
toolbar). Right: background work (scan, meshes, preview) as `label n/m`, then the one most
useful shortcut for the moment, its keycap centred in the row and its word on the row's text line, the row's right
end 16 px in from the edge (under the inspector's content edge) as its left is.

> **Built:** missing or unreadable meshes and textures as `1 missing`; the tooltip counts them in words (`1 texture
> missing`) and names, for a texture, the mesh that uses it and the mod or archive that mesh comes from (meshes
> first). A click shows or hides the missing meshes' boxes (the *Missing meshes* layer), a right-click copies the
> whole list; `editor.log` has it too.

---

## 3. Visual language

### 3.1 Tokens (`ui/Theme.h`)

The art set's palette (`presskit/README.md`, Style; `presskit/src/style.css`): every gray has R = G = B.

| Token | Value | Use |
|---|---|---|
| `bg0` | `#000000` | ink: the window |
| `bg1` | `#0e0e0e` | rail, inspector, drawer |
| `bg2` | `#1e1e1e` | gray-5: rows, inputs, popovers |
| `bg3` | `#2a2a2a` | hover |
| `line` | `#3c3c3c` | gray-4: borders, rules |
| `text` | `#d4d4d4` | gray-1: text |
| `muted` | `#9c9c9c` | gray-2: captions, section heads |
| disabled | `#6a6a6a` | gray-3 |
| `accent` | `#ffffff` | paper: wordmark, titles, icons, the active control, the selected row, links, the primary button |
| `accentDim` | `#9c9c9c` | a pressed primary button, the slider's filled track (its empty track is `line`) |
| `select` | `#ffffff` | the 3D frame and plan highlight |
| `ok` / `warn` / `error` / `info` | `#7fbf7f` / `#e0b04a` / `#d9634c` / `#6fa8dc` | status: pills, dots and status text (yours, shipped, auto-draft, draft, confidence, errors) |

Colour is for data and state only: the status hues above, category colours (`CategoryColour`), *Colours by
fill*, and the see-through boxes in the 3D view and on the plan. Surfaces, type, icons and controls are the
brand's black, white and grays.

**Themes.** The tokens above are *Dova Stash*, the default. `⚙` Setup › *Theme* (also *Theme: <name>* in the
palette, `--theme <name>` for one run) offers other palettes in two groups, named after and inspired by the VCL
styles xEdit offers under Options › UI Theme (colour cues only; no style art is copied). *Dark*: Windows10 Dark,
Carbon, Charcoal Dark Slate, Tablet Dark, Cyan Night, Golden Graphite, Ruby Graphite. *Light*: Windows10, Light,
Tablet Light, Aqua Light Slate, Lavender Classico, Emerald Light Slate. Each fills the same roles (`ui/Themes.cpp`):
body text at least 7:1 on `bg0` to `bg2`, captions, links and status hues at least 4.5:1; light themes take darker
status hues and accents so the text on a filled accent (`bg0`) stays readable. The disabled gray, the scrollbar and
the tables' hairlines follow the tokens. The viewport stays dark in every theme (the 3D view draws its own
sky in a neutral `#1c1c1c`; the plan's ground is `bg0` in a dark theme, the same `#1c1c1c` in a light one), and the data colours never change. A
pick applies before the next frame (the fonts stay) and is remembered as `ui.theme` in `state.json` (empty: Dova
Stash).

*Yours* in the same list: JSON files in `%LOCALAPPDATA%\DovaStashEditor\Themes\` or a `Themes` folder next to the
exe, `{"name": "My Carbon", "dark": true, "colours": {"bg0": "#141414", "text": "#e6e6e6", ...}}` with the table's
keys (`disabled` included); a key left out comes from Dova Stash (dark) or Light (light). *Export...* under the list
writes the theme in effect there (a shipped one as "My <name>"; an existing file is never overwritten) and shows it
in Explorer. The folder is read again each time the list opens, so an edited file applies when it is picked; a file
that does not parse, or whose name is taken, is skipped.

### 3.2 Type

| Role | Font | Size at 100 % |
|---|---|---|
| Title | Cinzel capitals, tracked 0.30 em | 26 px on the start panel and About, 18 px as the title row's wordmark |
| Heading | Jost | 20 px; section heads are 16 px capitals tracked 0.24 em in `muted` |
| Body | Jost | 18 px |
| Small | Jost | 16 px |
| Mono | Cascadia Mono, else Consolas | 13 px, form refs and EditorIDs |

ImGui sizes a font by its line (ascender + descender), not its em; Jost's tall line and small x-height make it
read about a fifth smaller than the mono at the same size, so the body is 18 px: the x-height of the 13 px mono
(5.7 px). Fonts are loaded at whole pixels at every UI size.

Fonts are embedded as resources (`editor/res`, from `presskit/src/fonts`) so the look is identical on
every machine. DPI: sizes scale with the window's DPI, times a **UI size** (a slider under Setup, 100–250 % in
5 % steps; Ctrl+= / Ctrl+- step 10 %, Ctrl+0 is *Auto*; remembered in `state.json` as `ui.textScale`). *Auto* is
125 % on a 1440p and 150 % on a 4K monitor at 100 % Windows scaling, where the text is too small to read at arm's
length, else 100 %. A new size rebuilds the fonts between frames; moving the window to a monitor with another DPI
does the same. Screenshots draw at 100 % (the same pixels on every machine) unless `--text-size <percent|auto>`
sets the size for that run.

### 3.3 Shapes and ornament

- Corners 3 px, borders 1 px `line`, no shadows except the floating toolbar and popovers (a 1 px darker rim).
- Section head: tracked capitals in `muted`, then a hairline rule to the right edge. On a body too narrow for a
  head's count (a large text size) the hairline goes first, then the capitals close up to a third of their tracking,
  then the info mark (the head itself keeps the tooltip): the words clip last. No diamonds or other
  ornament; the one divider (empty states, the start panel) is the art set's: the emblem between two hairlines.
- Chips: 24 px pills with `bg2` fill and a 1 px border in the status gray; text in the same gray. A chip is one
  thing to act on (a category to accept, the master); a status, in the title row, down a list or among a
  container's facts, and every count, is a 6 px dot and a muted word (`w::Status`) or plain text instead (§3.5). A chip that switches (a
  suggestion) shows a 12 px check in its colour before `text` and a full border when on; off, muted text and the
  border at half strength. Its fill stays `bg2` either way (no tint in the status colour), `bg3` on hover.
- Buttons, three kinds. *Tool* buttons (`ToolButton`: the floating toolbar, tabs, modes) are flat, hover `bg3`,
  active `accent` text with an underline. A panel's actions (`Button`: the Draft row, the Template footer, empty
  states, Setup's *Browse...* and *Export...* (one width, one right edge) and *Rescan*, an unfilled *Save* in the title row) are framed: `bg2`, a 1 px `line` border, input height, hover `bg3`,
  `text` (a destructive one in `error`, with its icon); a fixed width wraps a long label. Primary (Save) is filled
  paper with ink text, gray-1 on hover. Links (accent text) are for navigation and for small actions inside a line
  of text.
- Dropdowns: the input frame with a muted chevron at its right (`text` on hover), no filled triangle.
- Icons: drawn with draw-list primitives at 18 px (menu, search, funnel, layers, palette, camera, gear,
  chevrons, check, warning, error, undo, redo, save, close, pin, a quill for *Draft*). No icon font.
- Row height 28 px, inputs 32 px, toolbar 36 px, title row 44 px, status row 28 px, padding 8, item spacing 8/4.
- Tables: rows parted by a 1 px hairline, no alternating bands; column rules only in the header row (they mark the
  resize handles), none in the body; numbers (*Height (m)*, *Items*) right-aligned with their header.

### 3.5 Design protocol (unslop-ui and impeccable)

The look is checked against two design skills the repo carries under `.claude/skills/` (`unslop-ui`, the anti-slop
protocol, and `impeccable`, whose Operate-mode guidance fits a tool like this; the root `PRODUCT.md` and `DESIGN.md`
are their context files, and `docs/design-pass/` has the before/after screenshots of the 2026-10-28 pass). What they
settle here:

- **4 px grid.** Every height, padding and spacing is a multiple of 4 (28, 32, 36, 44; 8/4). The one exception is
  the 6 px inline gap inside a row (name · summary, icon · label), a micro gap by the protocol's own rule. A flat
  button (tool, tab, popover) pads its label 8 px, so a tab's text stands on its row's 16 px keyline. A field
  label (`w::FieldLabel`) has 8 px above it and 4 px to its field, so it reads with its own field, not the one above
  (straight under a section head it adds nothing, so the head groups with the fields it heads);
  a plain vertical step (`w::Spacer()`) is 8 px, 16 px between items with the item spacing.
- **Neutral surfaces, hairline borders.** Already the brand: black, white and grays, 1 px `line` borders, no glow,
  no shadow but the popovers' 1 px rim. Every surface is a token of the ramp (the rail's draft banner is `bg3` in an
  `accentDim` rim, not a white wash; a selected row, in the rail, the Template panel's list and ImGui's own
  selectables, is `line` with the 2 px white bar at its left, hovered or not), and a vertical divider is a 1 px column on a whole pixel, never an
  antialiased line.
- **No pills for counts or repeated status.** A count (the drawer's tabs, the filter button) is plain muted text,
  accent when it is an alert; a status down a list (the rail, the Template panel's list of the home's templates: rail rows both) is a 6 px dot
  and a muted word. A chip stays for one
  thing to act on.
- **Numbers on one right edge.** Numeric table columns are right-aligned, header included.
- **Tables without bands.** Hairlines between rows, not alternating fills; column rules stay in the header.
- **Copy.** Sentence case for every label and button, no exclamation marks, no emoji, no icon in a tinted box.
- **Motion.** None beyond ImGui's hover delays; nothing scales or slides.

> **Built:** the one alert count, the *Checks N* tab, draws its figure in the warn colour whatever the tab's state,
> the hue the status row gives the same warning (`w::Segmented` colours a label's trailing number); the status row
> colours each count alone, so `0 errors` stays gray beside an amber `1 warning`. A row of start
> cards shares one bottom edge (`w::Card` keeps the room of the most lines any card in the row wraps to: one today, so
> no empty line under the bodies), parted by the 8 px item spacing (the 6 px gap is for inside a row, so framed
> buttons and fields side by side, as *Accept all · Reject the weak ones · Draft again*, the Template footer,
> *Version* and *Priority* and *Browse...*, take the 8 px too, and so do framed buttons stacked, as the empty
> Selection panel's three), and the start column is centred in the view; the rail's
> mod rows, the status row's *home · cell* and the summary lines (inspector, *Where items go*) space their separator by one text space on both sides and never let a wrapped
> line begin with it. *Save* has one look wherever it shows (title row, Template footer): filled with work to save,
> framed otherwise. A fact shows once: the status row says *No home open* rather than repeat the home count of the
> start heading and the rail footer (the empty inspector says what it will hold, *Nothing to inspect*), the preview's run time is *Run again*'s tooltip, the scan time *Rescan*'s, *Draft all homes* reports `134 homes
> drafted · 89 to save` so its count meets the rail banner's, and the fill legend names its
> sample in its head (`LEGEND  Typical haul`), not in a caption that reads as a fifth swatch. *Where items go* names
> a one-category container's category alone (the *Items* column has the count) and says its unit when it switches
> one (`· 32 containers receive nothing` after a line of items), and its total ends in a colon before the split it
> adds up from (`266 items in 112 stacks: 260 by category or item · ...`, one text space after the colon), the line on
> the drawer's 16 px keyline with the table under it; the Template panel puts *Master: Chest* on a line
> of its own, a field gap (16 px) under the Note box, the container and display counts beneath it, and the Selection
> panel's *Remove from the template* takes the same gap under its Note box; the start cards'
> bodies each fit one line and their corners take the 3 px radius token like every other framed widget; the Containers and Displays tables' *Height (m)* both read a muted
> *outside* for a container off the home (one fact, one look). One control, one order: *By category* then *By fill*, in the
> drawer's row as in the *Colours* popover. Check and radio rows are 20 px (a 24 px pitch with the item spacing),
> and a legend swatch takes their 16 px mark and 8 px gap, so the *Colours* popover has one label keyline; the
> floating toolbar and the hint plate both sit 8 px in from the viewport's edges, with the 3 px radius; the drawer's
> short columns (*Kind*, *Reference*, *Height (m)*) are fixed to their values' width, in Containers and Displays
> alike, and the text columns share the rest. The panels share their keylines: the rail, inspector and drawer
> bodies have 4 px over their first row (with the 8 px above the panel, 12 px under the title row, so the rail's
> search, the inspector's tabs and the floating toolbar end on one line), the inspector's content sits 16 px in from
> its edges like the rail's (its content starts just past its 1 px rule, the resize handle under that padding), the drawer's tabs 8 px under its rule and over the table header, the drawer tables
> padding their outer cells too (the first column 8 px inside the header band, on the rail's 16 px keyline with the
> status row's *home · cell*; the last column's figures 8 px short of the edge), the inspector's panel 12 px under its
> tabs whether it starts with a picture, a row of buttons, a line of text (*Nothing selected*, *No problems*) or a
> section head (a section head adds its 8 px over itself only under something), its
> scrolling body ending a 32 px strip above the bottom, the rail's and the inspector's collapse chevrons level with
> the footer's *Rescan* line, the title row's `≡` and gear on the 16 px keylines of the search box and the inspector's
> content edge, the rail banner's close icon 8 px inside its border like its text, and the Template footer's buttons 8 px under its rule; the Note box rounds its height
> up to the grid, the rail's *All cells...* link sits in a 28 px row like the homes around it, *Where items go*
> starts each container with the kind icon the Containers table shows, its fill bars are 12 px with the 3 px
> radius, and the plan's scale bar and *N* sit on the hint line's 12 px inset. The title row's divider has 12 px
> of air on each side (the wordmark's end, the home switcher's icon; 18 px at 150 %). A column's filter funnel ends 8 px inside its
> rule, the figures' inset, with the sort arrow beside the name rather than kept room at the cell's end. The
> Selection panel's *Ignored* and *Sell chest* checks sit 8 px apart and 14 px under the *Label* field, one group of
> their own; Setup's *UI size* is one slider row, the name at its left and the value (`Auto · 100%`) at its right. Every hint pair reads `Key: action`, and the category picker's
> closed frame reads *Add a category*, a verb in sentence case like every other control.

Two brand rules override the protocol's defaults, on purpose: the wide-tracked capitals (the Cinzel wordmark and
titles, the Jost section heads) are the art set's own style (`presskit/README.md`, Style), and the icons stay 18 px
so they sit level with the 18 px body line beside them.

### 3.4 Art the editor reads from the player's install (optional)

`Assets::Read` already serves any DDS in the merged Data folder and archives. The art set rules out game
assets as decoration, so the editor draws none (the start panel's header sits on plain ink; `ui/Art` stays
for the thumbnails' textures). What it reads is data:

- No texture behind the start panel's title.
- The selected object's **own mesh** rendered as a 96 px thumbnail at the top of the Selection panel
  (`render::Scene` already loads it; render the one model to a small offscreen target with a fixed camera).
- The home's main cell as a thumbnail in the switcher dropdown is out of scope for now (cost).

Never read SWF (SkyUI's icons) and never copy art into the repo or the zip.

---

## 4. Keyboard

| Key | Does |
|---|---|
| `Ctrl+K` | command palette |
| `Ctrl+1` / `Ctrl+2` / `Ctrl+3` | rail / drawer / inspector |
| `1` / `2` | 3D / plan |
| `S` / `P` | select / paint mode |
| `C` / `V` | colours by category / by fill (toggle) |
| `F` / `Home` | frame selection / overview |
| `L` | layers popover |
| `Ctrl+S` / `Ctrl+O` / `Ctrl+D` | save / open template / draft all |
| `Ctrl+F` | find a home (rail search) |
| `Ctrl+Z` / `Ctrl+Y` | undo / redo |
| `Ctrl+click`, drag | add to selection, box select |
| `Esc` | close popover or palette, then clear selection |
| `Ctrl+=` / `Ctrl+-` / `Ctrl+0` | UI size larger / smaller / auto |
| `F5` | rescan |
| `Delete` | remove the selection from the template (asks when more than one) |

> **Built:** the command line's `--tab` (view, drawer) applies to that run only: the state file keeps the
> remembered layout until a panel is changed by hand.

The command line keeps every flag (`--tab` maps: `3d`/`plan` to the view, `containers`/`displays`/`doors`/
`preview` to the drawer tab, `template` to the inspector panel; `--drafts` per §2.8). `--screenshot` keeps
working so the guide's figures can be retaken. A value the window does not know (`--tab`, `--inspector`,
`--popover`, `--colours`, `--layers`, `--theme`) is an error (exit 2), and a `--home` that matches no home exits 1
after its screenshot (EDITOR.md §5.6).

---

## 5. What goes away

- The three top buttons (Start / Homes by mod / Draft all homes) and `Mode`.
- The MO2 path row and the setup status line as permanent chrome.
- The "1. Player homes / 2. Cell / 3. Cell" numbered headers.
- The three toolbar rows over the 3D view.
- The separate drafts view, its home list and *Open in the editor*.
- Top-level tabs for Containers / Displays / Doors / Template / Where items go.

---

## 6. Done means

- Every feature of EDITOR.md reachable in the new layout; the CLI flags and `--screenshot` work.
- The guide's figures retaken; EDITOR.md §2, the user guide and the Nexus article updated.
- A first-time author opens the editor, sees the rail, clicks a home, clicks a chest, picks a category and
  saves without reading anything.
- The window at 1280 × 720 is still usable (rail and inspector collapse to strips on demand).
- The home is the hero at any size: at 1366 × 768 and 150 % the view keeps at least 45% of the width and half the
  middle's height (App.cpp's floor), and the inspector's empty-state buttons wrap instead of clipping.
