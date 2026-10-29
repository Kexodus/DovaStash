# Design pass, 2026-10-28: unslop-ui and impeccable

The editor checked against the two design skills the repo carries under `.claude/skills/` (`unslop-ui` and
`impeccable`; the rules they settled are in `docs/EDITOR-UX.md` §3.5). The window at 1600 x 900, 100 % text, taken
with `--screenshot` the way the guide's figures are (`presskit/guide/README.md`).

What changed:

- **Rail status**: a pill on every row (*shipped*, *draft*) became a 6 px dot and a muted word.
- **Counts**: the drawer tabs' filled pills (*46 of 52*, *6*, *3*) and the filter button's count became plain text.
- **Tables**: *Height (m)* and *Items* right-aligned with their headers; a hairline between rows instead of
  alternating bands.
- **Grid**: inputs 30 to 32 px, toolbar 34 to 36, title row 42 to 44, chips 22 to 24, item spacing 8/6 to 8/4.
- **Column filters**: the tab row's quick-filter chips became funnel filters in the column headers (Kind, Role,
  Notes; Kind, Used; Fill), the way a data grid filters; the tab row keeps only the count and the *Filter rows* box.
- **Design judge** (`tools/devcycle/design-judge.js`: impeccable's Assessment A plus the unslop checklist, Opus
  agents): 7/10 at the start, 10/10 after 19 rounds and 107 fixes. The first rounds: accepted Draft suggestions
  show a check with a legend under the head; Selection's category chips show names; one framed `Button` for panel
  actions, *Remove from the template* alone in the error colour; chevron dropdowns; number headers flush right;
  a muted local-id Reference column; the Kind filter opens upward in short tables; a plate behind the hint line;
  a quill Draft icon; the plan legend lists every colour it draws, its scale bar reads in metres; a viewport floor
  keeps the home the hero at 1366x768 at 150 % text. Then grid and keyline alignment across the rail, inspector,
  drawer, status row and Setup, and last the wording of counts so each says what and where it counts (*Containers
  46*, *20 accepted of 52*, an off-home container reads *outside* once). Skipped on purpose: a gray fill ramp and
  flattening one-home mod groups.
- **The 10/10 was premature** (2026-10-29): the judge scored still screenshots only. Typing in the rail's search
  until a home's *All cells...* row ended the list raised a Dear ImGui error box, the title row still wore a status
  pill, and a search with no match left the rail blank. All three are fixed; every run now logs ImGui errors and a
  screenshot run fails on one (exit 5); the judge's set has six interaction states (searches, a popover, a narrow
  window) and an ImGui error caps its score at 5.

| | Before | After |
|---|---|---|
| Start panel | ![](before/start.png) | ![](after/start.png) |
| Selection | ![](before/selection.png) | ![](after/selection.png) |
| Containers table | ![](before/containers.png) | ![](after/containers.png) |
| Containers, Kind filter open | | ![](after/containers-filter.png) |
| Draft all homes | ![](before/drafts.png) | ![](after/drafts.png) |

Retake, from the repo root with the editor built (`cmake --build --preset editor-release`):

```powershell
$env:DOVASTASH_EDITOR_STATE = "$env:TEMP\ds-design-pass-state.json"
$exe = 'build\editor\editor\Release\DovaStashEditor.exe'
$mo2 = @('--mo2', 'E:\modding\Kexodus Skyrim', '--profile', 'Dova Stash Testing', '--gui', '--window', '1600x900')
$ald = @('--home', 'Aldamar')
$out = 'docs\design-pass\after'
& $exe @mo2 --screenshot "$out\start.png"
& $exe @mo2 @ald --tab 3d --select 'Eli_Druid Home.esp|0x00000B1B' --screenshot "$out\selection.png"
& $exe @mo2 @ald --tab containers --select 'Eli_Druid Home.esp|0x00000B1B' --screenshot "$out\containers.png"
& $exe @mo2 @ald --tab containers --popover kind --screenshot "$out\containers-filter.png"
& $exe @mo2 --drafts @ald --inspector draft --screenshot "$out\drafts.png"
```

Take `start.png` first with a fresh state file: the draft runs leave an *In progress* row behind.
