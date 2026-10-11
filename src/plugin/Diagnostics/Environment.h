#pragma once

// Startup report of everything Dova Stash integrates with (DLL versions, plugin load order slots), so
// a single DovaStash.log describes the player's setup without asking them for it.

namespace dova::diagnostics
{
	// kDataLoaded, after the integrations initialised.
	void LogEnvironment();
}
