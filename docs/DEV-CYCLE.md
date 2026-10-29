# Dova Stash automated dev cycle

An unattended loop that verifies, grades and fixes every aspect of Dova Stash until dedicated graders give
10/10 for ease of use and functionality. It runs as a Claude Code **Workflow** (multi-agent orchestration);
the script is `tools/devcycle/dovastash-dev-cycle.js`. Every agent in it runs on **Opus** (user rule).

## One round

1. **Verify** (one agent, serial): core tests (`core-tests` preset), plugin build (`plugin-release`, deploys to
   MO2), editor build (`editor-release`), the data checker (`python -I tools/esm_dump.py check ...`), the FULL
   in-game suite (`tools/autotest/run.ps1`, one game at a time, see the `ingame-test` skill), and headless editor
   screenshots of Breezehome, Wuth Rein and Lakeview Manor.
2. **Grade** (eight agents in parallel, read-only): one per area — core engine, gameplay, hotkey menus, Menu
   Framework panel, editor, shipped data/templates, docs and onboarding, build tooling. Each returns
   `easeOfUse` and `functionality` (1–10), `whatA10Needs`, and concrete issues with location, evidence, fix,
   severity (blocker/major/minor) and the **lane** that owns the files.
3. **Fix**: three lane agents in parallel on disjoint file sets, each building and testing its lane —
   `core` (src/core, tests/, dist/, tools/), `plugin` (src/plugin, tests/ingame, tools/autotest),
   `editor` (editor/, docs/EDITOR*.md) — then one `docs` agent (docs/DESIGN.md, docs/TESTING.md, CHANGELOG.md,
   README.md, presskit/) that applies the lines the other lanes reported and brings every description in line
   with the code as it now is.
4. Stop when every grader gives 10/10 and verify is green, else next round. Capped at `MAX_ROUNDS = 6`.

Nothing is ever committed by the cycle. The working tree accumulates the fixes; the user commits.

## Delegation policy (2026-10-25)

Per Anthropic's "when to fold subagents into your main agent loop": the coordinator does small work itself
(questions, single-file fixes, doc and changelog edits); subagents are for runs that take minutes (game,
editor), read-heavy investigations, and this cycle's parallel batches. One agent per coupled area per round
(the lanes above), never one per ask, so agents don't collide on PanelRender/Strings.h, the build tree or
the single game. Measure before reshaping: compare each round's scores with round 1.

## Standing decisions baked into the script (`DECISIONS`, applied from round 2)

- Displays in templates are optional; shipped templates need not list any.
- No templates for the Lux-list mod homes for now.
- Multi-cell homes ship as one merged template per home (`cells`, DESIGN §6.7); in-game plans 21–23 and 38
  must pass on the merged files.

## Design judge (`tools/devcycle/design-judge.js`)

A second, smaller Workflow for the editor's look alone, added 2026-10-28. One round is three agents, all on
Opus: **Capture**
builds `editor-release` and takes a fixed set of twelve screenshots (start panel, selection, the four drawer
tables with the Kind column filter open, the plan with the colours popover, drafts, template, checks, setup, and
the window at 1366x768 at 150 % text) into `build/design-judge/r<N>/`; **Judge** loads the `impeccable` skill and
runs its critique's Assessment A (the ten Nielsen heuristics 0-4, cognitive load, design specificity) plus the
`unslop-ui` verification checklist over those screenshots and the drawing code, against `PRODUCT.md`,
`DESIGN.md` and `docs/EDITOR-UX.md` §3.5, and returns an overall 1-10 with issues (severity, the rule broken,
file:line, the fix in the widget vocabulary of `Widgets.h`); **Fix** is the editor lane, which applies them,
rebuilds and re-shoots. The loop stops at `TARGET` (10) or `MAX_ROUNDS` (6). Every issue must be actionable
(a file:line, measured evidence, the exact widget or token change and what the shot will show afterwards), the
score must agree with the issue list (a major caps it at 7, minors at 8, nits at 9), and the standing decisions
in the script's `DECISIONS` (fill colours, rail groups, the brand exceptions) are neither raised nor applied. The judge never runs impeccable's
launcher, detector, live mode or hook: the editor is Dear ImGui, not a web page, and the user asked for the web
checker to be skipped. Run: `Workflow({ scriptPath: "<abs path to tools/devcycle/design-judge.js>" })`; the
first judge round's findings and what was done with them are in the Status list below.

## Running it

- New run: `Workflow({ scriptPath: "<abs path to tools/devcycle/dovastash-dev-cycle.js>" })`.
- Resume a run (completed agents are cached, the first changed/new call onward runs live):
  `Workflow({ scriptPath: ..., resumeFromRunId: "<run id>" })`. The journal of a run is
  `~/.claude/projects/E--AI-Projects-Dovastash/<session>/subagents/workflows/<run id>/journal.jsonl`;
  read it (not the agent transcripts, which are huge) for scores and fix reports.
- Stop: `TaskStop` on the workflow task, then kill any `SkyrimSE.exe` / `ModOrganizer.exe` the verify agent
  launched (`taskkill.exe /F /PID <pid> /T`). The runner leaves `selected_profile` on the AutoTest profile if
  killed mid-run; set it back to the user's profile in `ModOrganizer.ini`.
- Editing: change the script in the repo copy, then pass that path. Changing a prompt invalidates the cache
  from that agent call onward; gate new text on `round >= N` to keep earlier rounds cached.

## Status (keep current)

- 2026-10-21: round 1 done (scores 5–8 across areas; 89 issues fixed, 22 skipped). Run id `wf_a909487f-fc4`,
  session `89e343e5-cc88-4d00-ae00-3b4551bf5997`. Stopped at the start of round 2 on the owner's request.
- 2026-10-23: scheduled to resume Tuesday 2026-10-27 22:00 local from that run id (session-only cron; if the
  session is gone, relaunch with the same script and run id, or a fresh run).
- 2026-10-28: round 4: the plugin fixer could not run the in-game suite (session 0, `run.ps1` exit 2), so the
  cell-change hang stays open.
- 2026-10-28: round 5: `run.ps1` now hands a session-0 run to the desktop session (TESTING §9.2). Runs:
  `20261028-011117` (+ wynter `20261028-013235`) all plans pass 155 / fail 10 (before fixes); `20261028-013814`
  plans 10-18, 20-25, 38 pass 161 / fail 4 / skip 1 (3.9.4 anvil menu did not open, environmental) / manual 13;
  `20261028-020304` plan 38 pass 13 / fail 0 / manual 2. To bisect a hang, read the debug and trace lines listed
  in TESTING §9.5 (*Cell change (&lt;source&gt;): start* with no *done*, *Task #N*, *AutoTest: wait*, *AutoTest:
  console*, *Overlay: first frame*).
- 2026-10-28: round 6 (the last of MAX_ROUNDS=6) done; the run ended after 78 agents, no errors. Final scores
  (ease / function): core 9/9, hotkey menus 9/9, panel 9/9, data-templates 9/9, docs 9/8, editor 8/8,
  build-tooling 8/8, gameplay 8/7. Nothing reached 10. The graders' remaining lists are in the run's journal
  (`subagents/workflows/wf_a909487f-fc4/journal.jsonl`, last eight `grade:*` results). Recurring blockers:
  the main in-game run twice ended silently inside plan 23 (Windstad 3.9.4-grounds, after "Crafting from
  storage (got up)"), so plans 30-37, 39, 41-46 and 51-59 have no result on the final DLL; UIExtensions lists
  cut off at 128 entries; the Retrieve tree collapses after each Retrieve (node IDs carry the count); the
  manual ImGui pass of TESTING §5.1-5.8 is still unrecorded. Separately the same day: the Whitepeak Tower
  display-stock bug (a home's own base-record stock went on its plaques) was fixed outside the cycle and plan
  47 passes 5/5 (`20261028-024118`). A new round starts from this run id with a raised MAX_ROUNDS, or fresh.
- 2026-10-28: design judge round 1, run by hand (one Opus judge, one Opus fixer; shots in
  `build/design-judge/r1` and `r1-after`): overall 7/10, 15 issues (6 major, 8 minor, 1 nit); 13 fixed, 2 skipped
  by decision (a gray fill ramp, flattening one-home rail groups). The list and the before/after shots are in
  `docs/design-pass/README.md`.
- 2026-10-29: design judge run `wf_43262b01-d04` (Opus, resumed three times): 10/10 at round 19, 107 issues fixed.
  Scores: r1 7, r2-r13 8, r14 9, r15-r16 8, r17-r18 9, r19 10. Rounds 1-6 stalled because the judge was told to
  prefer few issues (drip-feeding); from round 7 it lists everything (`EXHAUSTIVE_FROM`). Rounds 7-12 stalled on
  pixel measurements, some re-tuning earlier fixes; from round 13 only what a user notices counts and earlier fixes
  are not reopened (`CALIBRATED_FROM`), which converged in seven rounds. A fresh run starts with both on from
  round 1 if the two constants are set to 1. Round 6 moved a clause in a core draft reason (`DraftSuggest.cpp`);
  core tests 294/294.
- 2026-10-29: the 10/10 did not hold. The owner hit a Dear ImGui error box within minutes (a rail search ending on a
  home's *All cells...* row: a SetCursorPos with no item after it), and the title row still showed a status pill. The
  judge had only ever seen twelve still frames. Fixed: the row, the pill (now `w::Status`, dot and word, also the
  start panel's stages and the Selection facts), a blank rail on a search with no match. Guard: every run logs ImGui
  errors, a screenshot run exits 5 on one, the capture reports them, the judge caps the score at 5 while any remains
  and must name the states its shots do not cover; six interaction shots added (`--search`, a new flag).
