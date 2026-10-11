#pragma once

// Thin wrapper around the vendored SKSE Menu Framework 3 API (extern/SKSEMenuFramework).
// That header drags in <windows.h>, so it is only ever included from MenuFramework.cpp, keeping
// Win32 macros away from CommonLib headers in every other translation unit.
namespace dova::ui::menu_framework
{
	// True when SKSEMenuFramework.dll is present (soft dependency, DESIGN §10.1).
	[[nodiscard]] bool IsInstalled();

	// True when SKSEMenuFramework.dll is actually loaded in the process (kPostLoad or later).
	[[nodiscard]] bool IsLoaded();

	// Framework version reported by the DLL, or 0 if it is not installed/loaded.
	[[nodiscard]] float GetVersion();

	// A framework window that takes the input (ours or another mod's) is on screen.
	[[nodiscard]] bool IsAnyBlockingWindowOpen();
}
