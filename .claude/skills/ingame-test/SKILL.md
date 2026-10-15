---
name: ingame-test
description: Verify a Dova Stash plugin change in the running game without the user: build a DLL, run the unattended in-game suite (MO2 profile "Dova Stash AutoTest"), read only the failures, add or extend a test plan. Use after any change under src/plugin or src/core that the game exercises (sorting, moves, displays, crafting hooks, menus' Controller paths), and whenever a brief says "verify in game".
---

# In-game test run (token-lean)

The mechanics are documented once in `docs/TESTING.md` §9 (setup, runner, harness design). This skill is the
procedure and the lessons from the first runs. Do not re-read the harness source unless a step itself is broken.

## 0. Preconditions (check, don't assume)

- **One game at a time.** Never run the suite while another agent may be running it. If a `SkyrimSE.exe` is
  running that you did not start, stop and report; never kill it without the user's OK.
- **The user's MO2 window stays open.** The runner uses `ModOrganizer.exe --multiple` and restores
  `selected_profile`; don't close MO2.
- **Worktrees share `.git`.** Never `git fetch --depth` / any shallow command (it made the main repo shallow
  once). For CommonLib in a worktree: copy `extern/CommonLibSSE-NG` from the main checkout, then delete the copy's
  `.git`.
- Profile and AutoTest mod exist? `ls "E:/modding/Kexodus Skyrim/profiles/Dova Stash AutoTest"`. If not:
  `pwsh -File tests/ingame/install.ps1 -Profile`.

## 1. Build a DLL without deploying

In a worktree (or any checkout that isn't yours to deploy from):

```
cmake --preset plugin -DDOVASTASH_DEPLOY=OFF
cmake --build --preset plugin-release 2>&1 | grep -E " error |warning C" | head
```

Only the coordinator deploys to `mods\Dova Stash`. The test DLL goes into the AutoTest mod via `-Dll`.

## 2. Run (background, then read the summary only)

```
pwsh -NoProfile -File tools/autotest/run.ps1 -Dll <path>\DovaStash.dll        # ~5 min; run_in_background
```

Read **only** the tail: one line per test plus `pass N, fail N, skip N, manual N` and the run folder
(`build/autotest/<date-time>/`). Do not open `DovaStash.log` whole (300+ KB). Exit code 1 = a failure or no
results.

## 3. A failure: narrow, then read

1. Failed checks: `grep -B1 -A2 '"ok": false' <run>/results.json | head -40` (each check has `ok`, `what`,
   `detail`; a test has `status`), or the runner's printed checks. `"finished": false` + `reason` = the run
   itself didn't complete.
2. The test's slice of the log: every test starts with `AutoTest: --- <id> <title>`; take that test's lines and
   grep them for `\[error\]|\[warn|  FAIL` plus the subsystem you changed.
3. Decide: Dova Stash bug, harness/step bug, or environment (see §5). Fix, rebuild, rerun with the same `-Dll`.
   Iterate until green or until you have a real bug with log evidence to report.

## 4. Adding or extending a test

- Plans: `tests/ingame/mod/SKSE/Plugins/DovaStash/AutoTest/NN_<Home>.json` (`dovastash-autotest-1`). Copy the
  shape of an existing test in `10_Breezehome.json` (it uses almost every step and check); test `id` = the
  `docs/TESTING.md` step it automates.
- Steps (`do`) act like the player or call the Controller as the menus do; checks (`check`) read inventories,
  gold, the layout file (JSON pointer), log/HUD lines (`notify`), `noErrors`, `conserved` against a `snapshot`.
  New step kinds go in `src/plugin/AutoTest/Steps.cpp`; keep them small and documented in TESTING §9.3.
- Prefer **deltas** over absolutes: homes have leveled loot that varies per run (`snapshot` then
  `conserved` / counts relative to the snapshot; "at least" where the home adds its own stock).
- Use `within` on checks that wait for the game (cell load, display fill, a menu).
- Mark what can't be automated as `manual` with the reason, so the summary keeps the list honest.
- `run.ps1` re-installs plans itself; profile INI changes need `install.ps1 -Profile`.

## 5. Lessons from the runs (check these before blaming the feature)

- **AE has no single main thread.** SKSE tasks, menu events and the crafting menu's calls arrive on different
  threads. Anything gated on "the thread that started it" silently never fires. Crafting checks run in UI tasks.
- **Equipped ammo is one stack to the engine.** A move without an extra list takes from the worn stack first.
- **Message boxes block every other menu.** A box nobody answers (SkyUI MCM warning, Survival Mode) makes
  container/crafting menus never open; list it in `_run.json` `messageBoxes`. A stuck box looks like a feature bug.
- **QuickLoot IE needs SkyUI** or it stops the game at the main menu with a dialog (the runner saves `dialog.txt`).
- **MO2's overwrite folder is shared.** The runner moves the run's files out to `<run>/written/`; a plan whose home
  already has a layout file is refused, so it never touches the user's setups.
- **MO2 may drop the VFS** if `skse64_loader` exits early; the runner retries up to 3 times. A "no results file" with
  no game log usually means that or a DLL without the harness (the deployed one predates it).
- **`CommonLib` wrappers aren't the hooked functions.** E.g. `InventoryChanges::GetItemCount` (15868) doesn't
  route through the hooked 15869; test through the real path (`hookedCount`, `craft`).

## 6. Report (keep it short)

`pass/fail/manual` line, run folder, for each failure: test id, root cause in one sentence, fix or evidence. New
manual-only items. Don't paste logs; quote the 1-3 lines that prove the point.

## Brief template for a subagent

> Verify <change> in game with the `ingame-test` skill (`.claude/skills/ingame-test/SKILL.md`): build with
> `-DDOVASTASH_DEPLOY=OFF` in your worktree, run `tools/autotest/run.ps1 -Dll <dll>`, iterate until green; add or
> extend a plan for <TESTING step ids>. You may launch the game; never kill a Skyrim you didn't start; never run
> shallow git commands. Report per §6.
