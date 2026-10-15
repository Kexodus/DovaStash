#pragma once

// AutoTest (see AutoTest.h): a copy of every DovaStash.log line, kept in memory so a test can ask "was this
// logged since the test began?". Installed on the plugin's logger only when a test run is on.

namespace dova::autotest::logcap
{
	struct Line
	{
		std::uint64_t             seq{ 0 };
		spdlog::level::level_enum level{ spdlog::level::info };
		std::string               text;  // the message only (no time or source)
	};

	// Adds the capturing sink to the default logger (SKSEPluginLoad, before anything else logs).
	void Install();
	// The sequence number the next line will get: a mark to search from.
	[[nodiscard]] std::uint64_t Mark();
	// Every captured line from `a_since` on, in order. The harness's own lines ("AutoTest: ...") are left out.
	[[nodiscard]] std::vector<Line> Since(std::uint64_t a_since);
}
