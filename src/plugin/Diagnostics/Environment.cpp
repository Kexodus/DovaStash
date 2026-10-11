#include "Diagnostics/Environment.h"

#include "Settings.h"
#include "UI/UIExtensions.h"

#include <Windows.h>

namespace dova::diagnostics
{
	namespace
	{
		// "1.2.3.4" from a loaded module's version resource, "" when it has none.
		std::string ModuleVersion(HMODULE a_module)
		{
			wchar_t path[MAX_PATH]{};
			if (!GetModuleFileNameW(a_module, path, MAX_PATH)) {
				return {};
			}
			DWORD      handle = 0;
			const auto size = GetFileVersionInfoSizeW(path, &handle);
			if (size == 0) {
				return {};
			}
			std::vector<std::byte> data(size);
			if (!GetFileVersionInfoW(path, 0, size, data.data())) {
				return {};
			}
			VS_FIXEDFILEINFO* info = nullptr;
			UINT              len = 0;
			if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &len) || !info) {
				return {};
			}
			return std::format("{}.{}.{}.{}", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS),
				LOWORD(info->dwFileVersionLS));
		}

		void LogDll(const char* a_module, std::string_view a_role)
		{
			const auto module = GetModuleHandleA(a_module);
			if (!module) {
				logger::info("  {:<32} not loaded   ({})", a_module, a_role);
				return;
			}
			const auto version = ModuleVersion(module);
			logger::info("  {:<32} {:<12} ({})", a_module, version.empty() ? "loaded"s : version, a_role);
		}

		void LogPlugin(std::string_view a_file, std::string_view a_role)
		{
			auto*       data = RE::TESDataHandler::GetSingleton();
			const auto* file = data ? data->LookupModByName(a_file) : nullptr;
			if (!file) {
				logger::info("  {:<32} not loaded   ({})", a_file, a_role);
				return;
			}
			const auto slot = file->IsLight() ? std::format("FE:{:03X}", file->smallFileCompileIndex) : std::format("{:02X}", file->compileIndex);
			logger::info("  {:<32} {:<12} ({})", a_file, slot, a_role);
		}
	}

	void LogEnvironment()
	{
		logger::info("Environment:");
		LogDll("QuickLootIE.dll", "info bar + button bar");
		LogDll("SKSEMenuFramework.dll", "editor panel");
		LogDll("InventoryInjector.dll", "I4: item icons in menus; not read by Dova Stash");
		LogDll("InventoryInjectorImproved.dll", "I5: I4 cache layer; not read by Dova Stash");
		LogDll("DynamicInventoryIconInjector.dll", "DIII: not read by Dova Stash");
		LogDll("po3_KeywordItemDistributor.dll", "KID: keywords used by categories");
		LogDll("po3_Tweaks.dll", "editor IDs in logs");
		LogPlugin("SkyUI_SE.esp", "item/category icon SWFs");
		LogPlugin("UIExtensions.esp", "in-world list menus");
		LogPlugin("QuickLootIE.esp", "QuickLoot IE MCM");
		logger::info("  In-world menus: {}", ui::uiext::IsInstalled() ? "UIExtensions lists" : "native message boxes");
		logger::info("  Runtime: {}", REL::Module::IsVR() ? "VR (overlay and UIExtensions lists off)" : REL::Module::IsAE() ? "AE" : "SE");
	}
}
