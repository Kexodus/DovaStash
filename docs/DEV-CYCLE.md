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
