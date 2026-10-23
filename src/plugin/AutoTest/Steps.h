#pragma once

// AutoTest internals: the steps a plan is made of (actions and checks) and the state they share. Main thread
// (SKSE tasks) only. See AutoTest.h for the design and docs/TESTING.md §9 for the step reference.

#include "Sort/SellChest.h"

namespace dova::autotest
{
	using Json = nlohmann::json;
	using Clock = std::chrono::steady_clock;

	struct CheckRecord
	{
		bool        ok{ false };
		std::string what;
		std::string detail;
	};

	struct TestResult
	{
		std::string              id;
		std::string              title;
		std::string              plan;
		std::string              status;      // "pass", "fail", "skip", "manual", "xfail", "xpass" (expectFail)
		std::string              note;        // skip / manual / xfail: why
		std::string              expectFail;  // the test's "expectFail": a known gap (a fail is "xfail", a pass "xpass")
		std::vector<CheckRecord> checks;
		double                   seconds{ 0.0 };
	};

	// What `a_ref` held at a moment: count per base form (FormRef text) for each holder.
	struct Snapshot
	{
		std::map<std::string, std::map<std::string, std::int32_t>> holders;  // holder -> item -> count
	};

	// A step that is not finished when it returns: polled every tick until `done` or the deadline.
	struct Wait
	{
		std::function<bool()>     done;
		Clock::time_point         deadline;
		std::string               what;
		std::function<void(bool)> finish;  // called once: true = timed out
	};

	struct Context
	{
		core::FormRef home;          // the plan's home (main cell)
		std::string   homeCoc;       // the plan's own coc target, if any
		TestResult*   test{ nullptr };
		std::uint64_t runMark{ 0 };  // log sequence marks (LogCapture)
		std::uint64_t planMark{ 0 };
		std::uint64_t testMark{ 0 };
		std::uint64_t stepMark{ 0 };
		std::map<std::string, Snapshot>        snapshots;
		std::set<RE::FormID>                   planCells;  // cells this plan's `enter` steps arrived in
		std::set<RE::FormID>                   planEnabled;  // references this plan's `enable` steps turned on (they were off)
		std::optional<sort::selling::Quote>    quote;
		std::optional<core::FormRef>           quoteChest;
		bool          abortTest{ false };  // an action failed: the rest of the test is skipped
		bool          abortPlan{ false };  // the home could not be entered: the rest of the plan is skipped
		std::string   skipReason;          // a "require" step found the game without what the test needs
		std::uint64_t actionMark{ 0 };     // log mark when the last action began ("since": "action")
		std::optional<Wait> next;          // set by a finishing wait: the step waits again (a fallback)
		RE::FormID    simulatedContainer{ 0 };  // a container menu the Controller was told about (none would open)
		std::set<std::string> writtenLayouts;   // other cells' setup files writeLayout wrote in this run (FormRef text)

		void Record(bool a_ok, std::string a_what, std::string a_detail = {});
	};

	// Runs one step. An action that must wait for the game (a cell to load, a menu to open) or a check with
	// "within" returns a Wait; everything else is done when it returns. Results go to `a_ctx.test`.
	[[nodiscard]] std::optional<Wait> RunStep(const Json& a_step, Context& a_ctx);

	// Runs a console command as if typed (RE::Script, as ConsoleUtil does).
	void Console(std::string_view a_command, RE::TESObjectREFR* a_target = nullptr);

	// The text of the open message box ("" when none is open), and its button `a_index` pressed as a click would.
	[[nodiscard]] std::string MessageBoxText();
	bool                      PressMessageBoxButton(std::int32_t a_index);

	// The player is in `a_cell` (a FormRef, or 0 = any), it is attached, no loading screen or main menu is up.
	[[nodiscard]] bool InCell(std::optional<RE::FormID> a_cell);
	// coc target for a cell FormRef: its editor id (cells keep theirs in game).
	[[nodiscard]] std::string CocTarget(const core::FormRef& a_cell);
}
