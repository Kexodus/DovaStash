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

| | Before | After |
|---|---|---|
| Start panel | ![](before/start.png) | ![](after/start.png) |
| Selection | ![](before/selection.png) | ![](after/selection.png) |
| Containers table | ![](before/containers.png) | ![](after/containers.png) |
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
& $exe @mo2 --drafts @ald --inspector draft --screenshot "$out\drafts.png"
```

Take `start.png` first with a fresh state file: the draft runs leave an *In progress* row behind.
