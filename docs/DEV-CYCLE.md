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
