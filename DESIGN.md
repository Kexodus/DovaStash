---
name: Dova Stash Editor
description: The desktop editor's visual system, the press kit's art set brought to a tool
colors:
  ink: "#000000"
  paper: "#ffffff"
  gray-1: "#d4d4d4"
  gray-2: "#9c9c9c"
  gray-3: "#6a6a6a"
  gray-4: "#3c3c3c"
  gray-5: "#1e1e1e"
  panel: "#0e0e0e"
  hover: "#2a2a2a"
  status-ok: "#7fbf7f"
  status-warn: "#e0b04a"
  status-error: "#d9634c"
  status-info: "#6fa8dc"
typography:
  title:
    fontFamily: Cinzel
    fontSize: 26px
    fontWeight: 500
    letterSpacing: 0.30em
    textTransform: uppercase
  heading:
    fontFamily: Jost
    fontSize: 20px
    fontWeight: 400
  section:
    fontFamily: Jost
    fontSize: 16px
    fontWeight: 400
    letterSpacing: 0.24em
    textTransform: uppercase
  body:
    fontFamily: Jost
    fontSize: 18px
    fontWeight: 400
  small:
    fontFamily: Jost
    fontSize: 16px
    fontWeight: 400
  mono:
    fontFamily: Cascadia Mono
    fontSize: 13px
rounded:
  sm: 3px
  pill: 12px
spacing:
  1: 4px
  2: 8px
  3: 12px
  4: 16px
  6: 24px
  8: 32px
components:
  button:
    background: "{colors.gray-5}"
    color: "{colors.gray-1}"
    rounded: "{rounded.sm}"
    height: 32px
  button-hover:
    background: "{colors.hover}"
  button-primary:
    background: "{colors.paper}"
    color: "{colors.ink}"
    rounded: "{rounded.sm}"
    height: 32px
  input:
    background: "{colors.gray-5}"
    border: "1px solid {colors.gray-4}"
    rounded: "{rounded.sm}"
    height: 32px
  chip:
    background: "{colors.gray-5}"
    border: "1px solid currentColor"
    rounded: "{rounded.pill}"
    height: 24px
  row:
    height: 28px
  popover:
    background: "{colors.gray-5}"
    border: "1px solid {colors.gray-4}"
    rounded: "{rounded.sm}"
---

# Design System: Dova Stash Editor

Context file for the `impeccable` design skill, written 2026-10-28 from the code and `docs/EDITOR-UX.md`,
which stays the full specification (this file summarises it; when they disagree, EDITOR-UX.md wins). The
surface is a desktop tool drawn with Dear ImGui, so the tokens are applied in `editor/src/ui/Theme.cpp` and
`editor/src/ui/Widgets.cpp`, not in CSS.

## Overview

The press kit's art set, brought to a tool: black, white and neutral grays, one serif of tracked capitals for
the wordmark and titles, one sans for everything else, the emblem as the only ornament. Restrained and exact.
Colour belongs to data (categories, fill, status) and never to chrome. Anti-reference: anything that looks like
a game launcher or a modding tool's skin; anything with glow, gradients or tinted dark surfaces.

## Colors

- Surfaces step by luminance: `ink` for the window, `panel` for the rail, inspector and drawer, `gray-5` for
  rows, inputs and popovers, `hover` on hover. Borders are 1 px `gray-4`. No shadows but a 1 px darker rim
  under popovers.
- Text: `gray-1` body, `gray-2` captions and section heads, `gray-3` disabled, `paper` for the wordmark,
  titles, icons, the active control, the selected row, links and the primary button.
- Status hues (`ok`, `warn`, `error`, `info`) appear only as 6 px dots, chips, status text and the viewport's
  data colours. Every gray has R = G = B.
- Thirteen other themes (dark and light) fill the same roles with at least 7:1 body and 4.5:1 caption contrast
  (`editor/src/ui/Themes.cpp`); the viewport stays dark in all of them.

## Typography

- Cinzel capitals tracked 0.30 em: the wordmark and the start panel's title. Jost: headings, body, small,
  and the section heads (16 px capitals tracked 0.24 em in `gray-2`, the art set's caps style).
- Jost's small x-height makes 18 px body read like 13 px mono; the mono (Cascadia Mono, else Consolas) is for
  form refs and EditorIDs only.
- Sizes scale with the monitor's DPI times a UI size setting (100 to 250 %), fonts rebuilt at whole pixels.

## Layout

One window, never a mode: title row (44 px), left rail (homes), the viewport (3D or plan) as the hero, right
inspector (380 px), bottom drawer (tables), status row (28 px). Rails collapse to 32 px strips. Everything
sits on a 4 px grid: rows 28, inputs 32, toolbar 36, padding 8, item spacing 8/4; the one off-grid value is
the 6 px inline gap inside a row.

## Elevation

Flat with tonal layering. The only depth is the 1 px darker rim under popovers, menus and tooltips, and a
translucent floating toolbar over the viewport.

## Components

- Buttons: flat `gray-5`, hover `hover`, active state as `paper` text with a 2 px underline. One primary
  button (Save): `paper` fill, `ink` text, `gray-1` on hover.
- Inputs 32 px with 1 px borders; checkboxes 16 px with a `paper` mark.
- Chips: 24 px pills, `gray-5` fill, 1 px border in the status colour, for one thing to act on (a category
  to accept, a quick filter) or one word of state in a row of its own (the title row's template word).
- Counts are plain text, muted (accent when they are an alert), never a filled pill. A status repeated down a
  list is a 6 px dot and a muted word.
- Tables: a hairline between rows, no bands; numeric columns right-aligned with their header; the mono font
  for form refs.
- Icons: draw-list primitives, 18 px, 1.5 px strokes, one step quieter than their label; no icon font, no
  emoji, no icon in a tinted box.
- Section head: tracked capitals in `gray-2`, then a hairline to the right edge. The one divider is the
  emblem between two hairlines.

## Shapes

3 px corners on everything but chips (full pill) and the 6 px status dot. Borders 1 px. No ornament.

## Motion

None beyond ImGui's hover delays (0.25 s short, 0.5 s normal). Nothing scales, slides or fades.

## Copy

Labels are nouns, buttons are verbs, sentence case throughout, no prefixes, no exclamation marks, no emoji.
Every suggestion and every problem carries its reason in one line, the full story in the tooltip.
