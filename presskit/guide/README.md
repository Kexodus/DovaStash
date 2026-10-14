# Dova Stash Editor: User Guide

[`DovaStashEditor-UserGuide.pdf`](DovaStashEditor-UserGuide.pdf): 21 US Letter pages for players. What the editor is
for, adding it to MO2, the window (homes rail, start panel, inspector, drawer), opening a home in the 3D view and floor
plan, setting up containers, paint mode, multi-select and undo, displays, drafts and Draft all homes, Where items go,
multi-cell homes, checking and saving, troubleshooting, and a one-page controls reference.

It uses the press kit's look (`../src/`): black, white and grays only, Cinzel titles, Jost text, the emblem as the
chapter bullet and in the running head, the line icons, and the in-game examples' callouts (corner brackets and
numbered badges) over the screenshots. The screenshots are the only colour.

| Path | What |
|---|---|
| `src/guide.html` | The pages, one `<section class="page">` each; chapter heads (`<header class="chapter-head">`) also fill the contents |
| `src/figs.js` | Each screenshot's crop, caption and callout boxes (in the screenshot's own pixels) |
| `src/guide.js` | Running head and foot, page numbers, contents, chapter heads, figures and their overlays |
| `src/guide.css`, `src/pages.css` | Palette, type, page frame; the cover, contents and page-specific pieces |
| `src/build.py` | Prints the PDF with headless Edge and checks the page count |
| `img/` | The screenshots (the window as PNG, 3D views as JPEG) |

## Rebuild the PDF

Windows, Python 3.10 or later (standard library only), Microsoft Edge:

```powershell
python -I presskit/guide/src/build.py
python -I presskit/guide/src/build.py --copy-to "E:\modding\Kexodus Skyrim\mods\Dova Stash\Tools"   # also next to the editor
```

It fails when a page overflows onto a second sheet (the PDF must have exactly one page per `<section class="page">`).
Open `src/guide.html` in Edge or Chrome to work on the layout (start the browser with
`--allow-file-access-from-files` so the line icons load).

## Retake the screenshots

Build the editor (`cmake --preset editor && cmake --build --preset editor-release`, docs/BUILDING.md), then run it
with its own state file, so your real `%LOCALAPPDATA%\DovaStashEditor\state.json` is untouched. The start panel shows
the homes in progress from that file; the guide's has four (drafted, reviewed, validated, saved) and the author
`Kexodus`. The state file also remembers the layout (rail, inspector and drawer sizes, the view); `--tab` and
`--inspector` set them for one run only.

```powershell
$env:DOVASTASH_EDITOR_STATE = "$env:TEMP\ds-guide-state.json"
$exe = 'build\editor\editor\Release\DovaStashEditor.exe'
$mo2 = @('--mo2', 'E:\modding\Kexodus Skyrim', '--profile', 'Dova Stash Testing', '--gui', '--window', '1600x900')
$wt  = @('--home', 'Whitepeak Tower')
$ald = @('--home', 'Aldamar')
$lv  = @('--home', 'Lakeview Manor')
& $exe @mo2 --screenshot start.png                                           # rail, start panel, inspector
& $exe @mo2 --mod 92177 --screenshot homes-by-mod.png                       # the rail searched by Nexus id
& $exe @mo2 @wt --tab 3d --inspector selection --screenshot view3d-overview.png
& $exe @mo2 @wt --tab plan --select 'Whitepeak Tower.esp|0x00230B30' --screenshot floor-plan.png
& $exe @mo2 @wt --tab plan --colours fill --popover colours --screenshot plan-fill.png
& $exe @mo2 @wt --tab 3d --select 'Whitepeak Tower.esp|0x002270B2' --screenshot mannequin.png
& $exe @mo2 @wt --tab 3d --select 'Whitepeak Tower.esp|0x00083DAB' --screenshot rack.png
& $exe @mo2 @ald --tab 3d --select 'Eli_Druid Home.esp|0x00000B1B' --screenshot view3d-select.png
& $exe @mo2 @ald --tab 3d --select 'Eli_Druid Home.esp|0x00000B0E' --screenshot role-ignored.png
& $exe @mo2 @ald --tab containers --select 'Eli_Druid Home.esp|0x00000B1B' --screenshot containers-table.png
& $exe @mo2 @ald --tab 3d --paint alchemy.potions --colours category --select 'Eli_Druid Home.esp|0x00000A94' --screenshot paint.png
& $exe @mo2 @ald --tab 3d --select 'Eli_Druid Home.esp|0x00000B57' --multi 'Eli_Druid Home.esp|0x00000ABA' --multi 'Eli_Druid Home.esp|0x00000BEA' --screenshot multi-select.png
& $exe @mo2 @ald --tab preview --screenshot preview.png
& $exe @mo2 @ald --tab 3d --colours fill --popover colours --select 'Eli_Druid Home.esp|0x00000E68' --screenshot preview-3d-fill.png
& $exe @mo2 @ald --tab 3d --colours category --popover colours --select 'Eli_Druid Home.esp|0x00000B1B' --screenshot colours-category.png
& $exe @mo2 --drafts @ald --inspector draft --screenshot drafts.png          # Draft all homes done: chips, banner, Aldamar's Draft panel
& $exe @mo2 --drafts @lv --inspector draft --screenshot drafts-multicell.png
& $exe @mo2 @lv --tab doors --screenshot doors.png
& $exe @mo2 @lv --inspector template --screenshot template.png
& $exe --mo2 'E:\modding\Kexodus Skyrim' --profile 'Dova Stash Testing' --render-cell WhiterunBreezehome --camera -600,-100,110,20,-10 --size 1600x760 --screenshot hero.png
```

`--palette` (the command palette, Ctrl+K) and `--popover <id>` (`menu`, `setup`, `switcher`, `filter`, `draftmenu`,
`layers`, `colours`, `camera`, `paintcat`) open the rest for a picture of its own, e.g. `& $exe @mo2 @ald --palette
--screenshot palette.png`.

The window captures are 1600×900 (`--window 1600x900` at 100% scaling). Callout boxes in `src/figs.js` are in the
capture's pixels; the figures not retaken yet are 1584×911 captures of the old window, which `src/guide.js` assumes
when a figure has no `size`, so a retaken figure gets `size: [1600, 900]` and its boxes measured again (a figure whose
layout changed has `callouts: []` until then). 3D captures go into `img/` as JPEG (quality 90), the rest as PNG.
Callouts follow the press kit: white corner brackets, a dark halo, numbered badges, and the numbers explained under the
figure.

The guide shares the repository license (`LICENSE`); the fonts keep their SIL Open Font License 1.1 (`../src/fonts/`).
