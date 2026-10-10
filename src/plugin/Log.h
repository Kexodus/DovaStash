#pragma once

namespace dova::log
{
	// Creates My Games/<Skyrim edition>/SKSE/DovaStash.log (truncated each launch) and makes it the
	// default spdlog logger, so SKSE::log::info(...) etc. land there. Safe to call once, early in
	// SKSEPluginLoad. Returns false if the log directory could not be resolved (logging then stays
	// on spdlog's default stdout sink, which is invisible in-game).
	bool Init();

	// Clamps to spdlog's range; out-of-range values fall back to info.
	void SetLevel(int a_level);
}
