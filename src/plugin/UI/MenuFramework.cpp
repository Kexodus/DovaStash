#include "UI/MenuFramework.h"

// The vendored header expects <filesystem> to already be included (it uses std::filesystem
// without including it) and includes <windows.h>; WIN32_LEAN_AND_MEAN/NOMINMAX come from CMake.
#include <filesystem>

#include <SKSEMenuFramework.h>

namespace dova::ui::menu_framework
{
	bool IsInstalled()
	{
		return SKSEMenuFramework::IsInstalled();
	}

	bool IsLoaded()
	{
		return GetMenuFrameworkModule() != nullptr;
	}

	float GetVersion()
	{
		return SKSEMenuFramework::GetMenuFrameworkVersion();
	}

	bool IsAnyBlockingWindowOpen()
	{
		return IsLoaded() && SKSEMenuFramework::IsAnyBlockingWindowOpened();
	}
}
