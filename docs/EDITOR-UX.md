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
6. **Looks like it belongs next to the game.** Dark, warm, parchment text on slate, a gold accent, Cinzel for
   titles and Jost for text (the press kit's own fonts, already in the repo under the OFL), thin ornamental
   dividers drawn in code. Nothing copyrighted ships; art the editor can read from the player's own
   install (DDS in the game's archives, the home's own meshes as thumbnails) is a bonus, never a dependency.
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
|          |  hint: Right mouse look · WASD fly · F frame · Home overview     |  alchemy.potions ×|
| ‹        |                                                                 |  + add            |
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
  (§2.2) in compact form; typing filters it. Next to it: the unsaved dot and the template status chip
  (*none*, *shipped*, *yours*, *draft*).
- Centre: `3D | Plan` segmented control (keys `1` / `2`).
- Right: **Draft ▾** (Draft this home, Accept all, Reject the weak ones, Draft again, Draft all homes…),
  **Save** (Ctrl+S; disabled with a tooltip when the checks have errors), `⚙` **Setup** popover (§2.7).

> **Built:** the chip reads *no template* / *shipped* / *yours* / *draft* (neutral / info / ok / warn). Under
> ~1400 px the title collapses to the emblem, and a long home name ends in "..." (the whole name in its tooltip),
> so the row never runs into `3D | Plan`.

### 2.2 Left rail: Homes (Ctrl+1 toggles, remembers width)

- A search box at the top: home or mod name, plugin, Nexus id. Instant filter.
- A filter popover (funnel icon, shows a count badge when any filter is active): *Only homes without a
  template*, *Only enabled mods*, *Likely homes only*, *Exteriors*, *Group by mod* (default on).
- One list that replaces **By mod**, **Any cell** and the drafts list:
  - Grouped by mod (MO2 name, Nexus id muted), the game's Data folder last.
  - A home row: name, muted storage summary (`89 chests · 170 displays`), a status chip at the right
    (*none* / *shipped* / *yours* / *drafted* / *saved*), the unsaved dot when it is open and dirty.
  - A mod group's footer link *All cells…* turns on the scored "any cell" listing for that mod only
    (what *Any cell* does today), with its evidence in the tooltip and *Likely homes only* respected.
  - Right-click: Open, Draft, Open in Explorer (its template file), Copy form ref.
- The row of the open home is highlighted; Enter opens the selected row; `↑`/`↓` move.
- Bottom of the rail: *Rescan (F5)* and the scan summary in one muted line (`124 homes · 302 plugins · 0.9 s`).
- Collapsed, the rail is a 28 px strip with the `›` handle and the home count.

> **Built:** the storage summary counts what a template can use over the home's cells: containers that do not
> respawn, are nobody else's and are not removed by a house upgrade, and displays (counted on the scan worker;
> the home finder's own counts include every container reference). A narrow rail drops the displays first, then
> ends the summary in "...". The row's tooltip shows the counts with icons and what its chip means.

### 2.3 Centre: the viewport

- The 3D view or the plan, never both; the segmented control in the title row switches (keys `1`/`2`).
- **Floating toolbar** at the top-left inside the viewport, translucent, one row:
  `Select (S)` `Paint (P)` as a two-state pair · `Layers ▾` (the current nine checkboxes, plus *Height cut*
  with its slider) · `Colours ▾` (None / By category / By fill, keys `C` and `V`, with the legend inside the
  popover) · `Camera ▾` (Overview (Home), Frame (F), fly speed, light, outlines). Nothing else on the view.
- Paint mode replaces the toolbar's second half with the paint bar (category chip, *pick another…*, done).
- **Hint line** at the bottom of the viewport, muted, one line, changes with mode (select, paint, box).
- Load progress of meshes and textures appears in the status row, not over the view.
- Hovering an object shows its name in a small tooltip near the cursor; selection keeps the yellow frame.
- The start panel (§2.6) draws **inside the viewport area** when no home is open.

> **Built:** `S` is not a plain two-state key: in paint mode it leaves paint for select; in select mode it toggles
> *Pick anything* (3D: walls and clutter too) or *Left drag pans* (plan), also in the Camera popover. The plan
> fits to its references with 2 % trimmed per side (a stray marker far away no longer shrinks the home), keeping
> the containers, displays and doors near the rest; it re-fits while the viewport settles until the author pans or
> zooms, and on `Home`.

### 2.4 Right inspector (Ctrl+3 toggles, 340 px, remembers width)

A segmented control at the top: **Selection · Template · Draft · Checks**. Each is a scrolling panel.

- **Selection**: what is selected and its role editor (today's details pane + role editor + bulk editor for a
  multi-selection), then *Where its items go* for that container (today's preview details). The reference
  and base record are one muted block with a copy icon, not a form. Empty state: "Click a chest, rack or
  mannequin. `Ctrl+click` adds, drag boxes." plus three buttons: *Make the nearest chest the master*,
  *Draft this home*, *Open the containers list*.
- **Template**: name, author, version, priority, note; the cells of the home with their labels (join or
  leave a cell here and from the Doors list); keep rules; the save target; *Save* / *Save as…*.
- **Draft**: today's draft review for **this** home: the master with its one-line reason and runners-up in a
  tooltip, then per container the suggested categories as chips (green / yellow / orange by confidence,
  reason in the tooltip), tick to accept. *Accept all*, *Reject the weak ones*, *Draft again* at the top.
  Clicking a row selects that container in the view. The panel is where "Open in the editor" used to lead:
  there is no longer anywhere else to go.
- **Checks**: the problems list (errors first), each one line, click selects the object; *Validate* is
  continuous so there is no button. Also shows what the template still lacks (no master, N containers
  without a role) as muted suggestions.

The inspector's panel switches automatically when it helps: selecting something switches to Selection;
*Draft this home* switches to Draft; clicking the status row's problem count switches to Checks.

> **Built:** the Selection panel shows the object's own mesh (96 px) at the left of its name once the scene has
> loaded, and its kind with an icon. The Template panel's fields scroll and *Save* / *Save as…* / *Copy JSON*
> stay pinned at its bottom like a form footer. The Draft panel's colour legend is a tooltip on its *Containers*
> section head (an info mark).

### 2.5 Bottom drawer (Ctrl+2 toggles, `L`)

Tabs: **Containers · Displays · Doors · Where items go**, the wide tables of today. The drawer opens at a
third of the height, drag to resize, remembers. Rows select the same object as the view (and frame it on
double click). *Where items go* keeps its sample picker and colour buttons in its own tab bar row.

> **Built:** the tabs show their counts as small badges; the tables' first column has the object's icon.

### 2.6 Start panel

Shown in the viewport area when nothing is open (first launch, or after the emblem is clicked). One
column, centred, in this order:

1. Setup status in one line (instance, profile, mods enabled) with *Change…* opening the Setup popover.
   When no instance is found: a card with the path field, *Browse…* and *Open*, and nothing else.
2. "124 player homes, 17 with templates" and the scan time, muted.
3. Three cards: *Open a home* (focuses the rail search), *Draft all homes* (Ctrl+D), *Open a template…*
   (Ctrl+O). Short one-line descriptions, no paragraphs.
4. *In progress*: the homes with a stage, as a compact table (home, stage chip, when, saved to).

> **Built:** the header sits on `interface\cc_background.dds` from the player's *Skyrim - Interface.bsa* at 20 %,
> warmed, cropped to cover and faded into the background on every side; without it the panel is flat.

### 2.7 Setup popover (`⚙`)

MO2 instance path with *Browse…*, profile dropdown, *Author* name, *Save templates to* (the target list), 
*Rescan*. The one-line summary of the current setup. Closing applies.

> **Built:** the summary is one line ("N of M mods enabled"), the full description in its tooltip.

### 2.8 Draft all homes

Not a mode. The `≡` and **Draft ▾** menus (and Ctrl+D) run the bulk draft; progress shows in the status row;
when it is done the rail shows a *drafted* chip per home and a banner over the rail offers *Save N drafts…*
(the existing confirmation dialog). Reviewing a draft means opening the home and using the Draft panel.
`--drafts` on the command line opens the workspace with the bulk draft started and the rail filtered to
homes without a template.

> **Built:** the bulk draft does not advance the homes' stages (the start panel's *In progress*); a home's stage
> starts when it is opened. The rail's *drafted* chip shows for every home with a draft.

### 2.9 Command palette (Ctrl+K)

One text field over the viewport. Typing matches, in this order: commands ("save", "draft", "overview",
"layers off"), homes (opens), containers and displays of the open home (selects and frames), categories
(in paint mode, sets the brush). `Esc` closes. Items show their shortcut on the right.

### 2.10 Status row

Left: home · cell (muted), the unsaved dot. Middle: `0 errors · 2 warnings` (click opens Checks). Right:
background work (scan, meshes, preview) as `label n/m`, then the one most useful shortcut for the moment.

---

## 3. Visual language

### 3.1 Tokens (`ui/Theme.h`)

| Token | Value | Use |
|---|---|---|
| `bg0` | `#111113` | window |
| `bg1` | `#19191d` | rail, inspector, drawer |
| `bg2` | `#222228` | rows, inputs, popovers |
| `bg3` | `#2b2b33` | hover |
| `line` | `#33333c` | borders, dividers |
| `text` | `#e8e2d6` | parchment: primary text |
| `muted` | `#9b968d` | secondary text |
| `accent` | `#c9a84c` | gold: active control, selected row, links |
| `accentDim` | `#8a7334` | accent on dark |
| `select` | `#ffd84a` | the 3D frame and plan highlight (unchanged) |
| `ok` / `warn` / `error` / `info` | `#7fbf7f` / `#e0b04a` / `#d9634c` / `#6fa8dc` | status |

Category colours (`CategoryColour`) are unchanged.

### 3.2 Type

| Role | Font | Size at 100 % |
|---|---|---|
| Title | Cinzel | 22 px |
| Heading | Jost | 16 px, letter-spaced caps for section heads |
| Body | Jost | 15 px |
| Small | Jost | 13 px |
| Mono | Cascadia Mono, else Consolas | 13 px, form refs and EditorIDs |

Fonts are embedded as resources (`editor/res`, from `presskit/src/fonts`) so the look is identical on
every machine. DPI: sizes scale with the window's DPI, times a **Text size** (Setup; Ctrl+= / Ctrl+- / Ctrl+0,
100–200 %, remembered in `state.json` as `ui.textScale`). *Auto* is 125 % on a 1440p and 150 % on a 4K monitor at
100 % Windows scaling, where 15 px is too small to read at arm's length, else 100 %. A new size rebuilds the fonts
between frames; moving the window to a monitor with another DPI does the same.

### 3.3 Shapes and ornament

- Corners 3 px, borders 1 px `line`, no shadows except the floating toolbar and popovers (a 1 px darker rim).
- Section head: small caps heading in `muted` with a thin line and a small diamond `◆` at its left end,
  drawn with the draw list (the Skyrim menu divider feel, no bitmap).
- Chips: 18 px pills with `bg2` fill and a 1 px border in the status colour; text in the same colour.
- Buttons: flat, `bg2`, hover `bg3`, active `accent` text on `bg2` with an accent underline. Primary (Save)
  is filled accent with `bg0` text.
- Icons: drawn with draw-list primitives at 16 px (menu, search, funnel, layers, palette, camera, gear,
  chevrons, check, warning, error, undo, redo, save, close, pin). No icon font.
- Row height 24 px, inputs 26 px, toolbar 30 px, title row 38 px, status row 24 px, padding 8/6.

### 3.4 Art the editor reads from the player's install (optional)

`Assets::Read` already serves any DDS in the merged Data folder and archives. Where it helps and the file
exists, the editor may use it; where it does not, the flat theme is the design, not a degraded version:

- A soft paper or stone texture behind the start panel's title (candidates in `Skyrim - Interface.bsa` under
  `textures\interface\`; pick one that reads well at 20 % opacity, else none).
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
| `Ctrl+Z` / `Ctrl+Y` | undo / redo |
| `Ctrl+click`, drag | add to selection, box select |
| `Esc` | close popover or palette, then clear selection |
| `Ctrl+=` / `Ctrl+-` / `Ctrl+0` | text size larger / smaller / auto |
| `F5` | rescan |
| `Delete` | remove the selection from the template (asks when more than one) |

> **Built:** the command line's `--tab` (view, drawer) applies to that run only: the state file keeps the
> remembered layout until a panel is changed by hand.

The command line keeps every flag (`--tab` maps: `3d`/`plan` to the view, `containers`/`displays`/`doors`/
`preview` to the drawer tab, `template` to the inspector panel; `--drafts` per §2.8). `--screenshot` keeps
working so the guide's figures can be retaken.

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
