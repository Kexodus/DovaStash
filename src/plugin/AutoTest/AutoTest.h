#pragma once

// AutoTest: the in-game test harness (docs/TESTING.md, "Automated in-game tests").
//
// Inert in normal play. It only wakes up when Data/SKSE/Plugins/DovaStash/AutoTest/ holds a plan (*.json, not
// starting with "_"), which only the "Dova Stash AutoTest" mod of the MO2 test profile provides (tests/ingame).
// Without one, Init() returns false after one directory check and nothing below ever runs: no sink, no thread,
// no hook. Nothing of it is shipped in dist/.
//
// With plans (one per home, run in file-name order):
//   - startup (SKSEPluginLoad): a copy of every log line is kept in memory (LogCapture.h), and the home backups
//     outside Data go to SKSE/DovaStash/AutoTest/Backups, so a run never touches the player's own copies;
//   - kDataLoaded: the plans are read; a plan whose home already has a layout file (the player's, visible
//     through MO2's overwrite folder) is refused, so the run only ever changes setups it made itself (the runner,
//     tools/autotest/run.ps1, moves what the run wrote there out again afterwards);
//   - at the main menu: a new game starts with `coc <home>` (the console command, through RE::Script), no input;
//     message boxes the game or another mod shows by itself are answered as _run.json says (they block every
//     other menu);
//   - each plan's steps run in SKSE tasks (polled every 100 ms; the crafting steps in UI tasks, where the crafting
//     menu runs): actions call the Controller
//     and the game as a player would (Steps.cpp), checks assert on inventories, gold, layout files, the log and
//     the HUD lines ("Notify: ..." in the log). A check may wait ("within": seconds) for asynchronous work;
//   - results go to SKSE/DovaStash/AutoTest/results.json (rewritten after every test) and the log; at the end,
//     or after the run's timeout, the game quits (`qqq`). Nothing is ever saved.
//
// Plan format, step reference, what is covered: docs/TESTING.md §9.

namespace dova::autotest
{
	// SKSEPluginLoad, after the log and the settings: looks for plans. True when a test run is on.
	bool Init();
	[[nodiscard]] bool Enabled() noexcept;
	// kDataLoaded, after Dova Stash's own setup: reads the plans and starts the run (it waits for the main menu).
	void OnDataLoaded();
}
