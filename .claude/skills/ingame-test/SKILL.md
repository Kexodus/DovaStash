---
name: ingame-test
description: Verify a Dova Stash plugin change in the running game without the user: build a DLL, run the unattended in-game suite (MO2 profile "Dova Stash AutoTest"), read only the failures, add or extend a test plan. Use after any change under src/plugin or src/core that the game exercises (sorting, moves, displays, crafting hooks, menus' Controller paths), and whenever a brief says "verify in game".
---

# In-game test run (token-lean)

The mechanics are documented once in `docs/TESTING.md` §9 (setup, runner, harness design). This skill is the
procedure and the lessons from the first runs. Do not re-read the harness source unless a step itself is broken.

## 0. Preconditions (check, don't assume)

- **The desktop session.** The game runs in the logged-on user's desktop session. An agent shell in session 0 can
  run `run.ps1` directly: it hands the run to a one-shot scheduled task in the desktop session (same arguments plus
  `-NoHandoff`, output streamed back, its exit code; files in `%TEMP%\DovaStashAutoTest\handoff-<stamp>\`, the
  newest 5 kept, older ones removed when a handoff starts). Exit 2
  only when nobody is logged on, the task cannot be made, or `-NoHandoff` is passed (it refuses instead). A leftover
  Steam, or Steam only in another session, is refused (exit 2) before the run changes anything: no run folder, no
  `summary.txt`, no quarantine, no profile save, no watchdog.
- **One game at a time.** The runner enforces it: it holds `%TEMP%\DovaStashAutoTest\.lock` and counts a running
  `SkyrimSE.exe` or an AutoTest MO2 a live run owns as busy (exit 2). Never kill a `SkyrimSE.exe` you did not start
  without the user's OK; wait with `-WaitMinutes 30` instead. What an earlier run left (§5) is not a reason to wait.
- **The user's MO2 window stays open.** The runner uses `ModOrganizer.exe --multiple` and restores
  `selected_profile`; don't close MO2.
- **Worktrees share `.git`.** Never `git fetch --depth` / any shallow command (it made the main repo shallow
  once). For CommonLib in a worktree: copy `extern/CommonLibSSE-NG` from the main checkout, then delete the copy's
  `.git`.
- Profile and AutoTest mod exist? `ls "$DOVASTASH_MO2/profiles/Dova Stash AutoTest"` (unset: `E:/modding/Kexodus Skyrim`). If not:
  `pwsh -File tests/ingame/install.ps1 -Profile`. A missing MO2 instance stops `run.ps1` and `install.ps1` with a message.
- **The player's profile comes back.** When `ModOrganizer.ini` already names the AutoTest profile and no run saved the
  player's, `run.ps1` puts back `-PlayerProfile <name>`, else "Dova Stash Testing" if it exists, else the profile whose
  `modlist.txt` changed last, and says "(guessed; -PlayerProfile sets it)". `-Build` uses the `plugin-release` preset
  and prints where it deploys.

## 1. Build a DLL without deploying

In a worktree (or any checkout that isn't yours to deploy from):

```
cmake --preset plugin -DDOVASTASH_DEPLOY=OFF
cmake --build --preset plugin-release 2>&1 | grep -E " error |warning C" | head
```

Only the coordinator deploys to `mods\Dova Stash`. The test DLL goes into the AutoTest mod via `-Dll`.

## 2. Run (background, then read the summary only)

```
pwsh -NoProfile -File tools/autotest/run.ps1 -Dll <path>\DovaStash.dll        # ~1 min per plan, ~40 min for all; run_in_background
pwsh -NoProfile -File tools/autotest/run.ps1 -Dll <dll> -Only '^(21|22)_'      # a subset of the plans (file-name regex)
```

Pass `-WaitMinutes 30` instead of a wait loop of your own. Leftover overwrite files are set aside by default (§5).
Run it in the background if you like, but don't end your turn before its summary line (the pass/fail counts, then
`Logs and results:`). If a runner was cut off, `pwsh -File tools\autotest\run.ps1 -Finish -WaitMinutes 30` puts the
player's setups and MO2 profile back (a watchdog normally does it on its own).

Many plans exist (vanilla, Creation Club and mod homes): run the ones your change touches with `-Only`, the whole set
in a few runs. Mod-added homes need their mods in the profile (`tests/ingame/profile/extra-mods.txt`,
`install.ps1 -Profile`). Plans left out for a missing plugin (the `60_`/`61_` Wynter plans) run right after the main
run in `<ProfileName> - <suffix>` when that profile exists; make it once with `pwsh -File tests\ingame\install.ps1
-Profile -ProfileName "Dova Stash AutoTest - wynter" -ExtraMods tests\ingame\profile\extra-mods-wynter.txt`. Without
it the summary prints that command and the run fails unless `-AllowNotRun`.

Read **only** the summary: the runner prints the tests that did not pass (fail, xfail, xpass, skip, manual, with their
notes), the blocked plans and `pass N, fail N, xpass N, xfail N, blocked N (N plan(s)), skip N, manual N`, and the run
folder (`build/autotest/<date-time>/`); `-ListAll` prints every test, and `<run folder>\summary.txt` always lists
every test. When plans ran in an extra profile, the last line sums both runs (`all: pass N, fail N, xpass N, blocked
N, skip N, manual N; exit <code>`): read `summary.txt` or that line, not the child run's tail. The runner keeps the
newest 20 run folders, every run younger than 48 h and the newest full run (`Plans: all`). Never open `DovaStash.log` whole (several MB for a full run); grep the test's slice. Exit codes: 0 green;
1 a failure, an xpass, a blocked plan (also plans left out for a missing plugin, unless `-AllowNotRun`) or a run that
did not finish; 2 busy or a setup problem; 3 the game did not start; 4 no results file.

A game that exits by itself mid-run is named in `stopped.txt` and `summary.txt`: *the game exited by itself after
<test id> (exit code N; crash log: <name>|none; Windows event: <faulting module>|none[; dump: ...])*. The plans with
no result yet then run once more in a run of their own (new folder, `-Only` those plans; `-NoResume` turns it off),
and the `all:` line adds its counts. A header of a run that did not finish ends *(results last said: <state>)*.

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
- **Crash Logger is in the profile** (when installed; `install.ps1` warns otherwise): a crash leaves `crash-*.log`,
  copied into the run folder. A crash after `qqq` (engine teardown, run 20261028-044838's `BSFaceGenModel`) is not a
  test failure.
- **Never activate a crafting station whose menu is open or opening.** It gets the player up and closes the menu;
  it preceded silent game exits (runs 20261028-011117, 021524). `sit` now only waits then, and leaves 2-6 s after a
  MoveTo before activating.
- **MO2's overwrite folder is shared.** The runner moves the run's files out to `<run>/written/`; a plan whose home
  already has a layout file is `blocked`, so it never touches the user's setups.
- **Leftovers from an aborted run** (layouts, backups, `Templates/User/AutoTest*` in overwrite, `selected_profile`
  still the AutoTest profile in `ModOrganizer.ini`): the next `run.ps1` finishes a killed run's cleanup first
  (`%TEMP%\DovaStashAutoTest\running.json`: its files go to that run's `written/`, the profile is put back). Files an
  earlier run wrote (inside a run's game window: run folders' `results.json`, `%TEMP%\DovaStashAutoTest\runs.json`)
  or marked as AutoTest's (`Templates/User/AutoTest*`, a layout named `... (AutoTest)`) go to that run's `written/`
  (or `<run>/earlier/`), listed, never put back. Other leftovers (the player's own) are moved to
  `<run>/preexisting/` and back afterwards (`-NoQuarantine` refuses with a list instead, exit 2). Never delete them
  by hand.
- **Processes an earlier run left**: an AutoTest MO2 (`--multiple -p "Dova Stash AutoTest"`) or a Steam started as
  `steam://run/489830` (session 0, its game gone) with no `running.json` and no live runner. The runner ends them
  itself, or stops (exit 2) with the `Stop-Process -Id <pid>` command to run. They are safe to end, unlike the
  player's MO2 window or a Skyrim you did not start.
- **MO2 may drop the VFS** if `skse64_loader` exits early; the runner retries up to 3 times. A "no results file" with
  no game log usually means that or a DLL without the harness (the deployed one predates it).
- **A message box left open blocks every later menu.** The hotkey in `iMode = 1`, a template offer and the category
  question are message boxes; `closeMenu` cannot close them and every later container/crafting menu stays shut. The
  harness fails the test that leaves one open (*left a message box open*), then closes it with its last button
  (Cancel / No) so later tests and plans don't fail too: answer it (`answerMessageBox`) or check the log/layout
  instead of pressing the hotkey.
- **Plans run on the shipped templates.** Plans 21-23 and 38 use the shipped merged templates
  (`LakeviewManor`/`WindstadManor`/`HeljarchenHall`/`TundraHomestead.json`), plans 41-46 and 51-57 the shipped
  `Templates/Mods` ones. A plan may also declare test templates (`"templates": ["X.json"]`, files in
  `mod/.../Templates/AutoTest/`), loaded only while that plan runs (`Store::ScopeTestTemplates`, `AutoTest.cpp`):
  plan 10 declares `NotInstalledHome.json`, plan 19 `BreezehomeMayNotFit.json`, plan 21 `LakeviewManorCellar.json` (a copy of the retired cellar
  template, for 3.9.6), plan 47 `WhitepeakTowerKexodus.json`. `install.ps1` leaves out undeclared ones and warns about
  a test template no plan declares.
- **Saved templates do not outlive their plan.** At each plan's start the harness removes `Templates/User/AutoTest *.json`
  (what an earlier plan's `export` saved), so a saved copy never outranks the shipped template a later plan checks.
- **`layout`/`layoutFile`/`backupFile` checks read the plan's home file**, whatever cell the player stands in: another
  cell's own setup can only be proven through the log ("Applied template ... to <home>") or counts.
- **`log` checks with `since: "action"`** miss lines the action itself logged; leave the scope off unless needed.
- **Sawn Log is non-playable** (Hearthfire hides it); use nails or quarried stone as building materials.
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
