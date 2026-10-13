#include "UI/Panel/EditorInfo.h"

#include <Windows.h>

namespace dova::ui::panel
{
	namespace
	{
		std::string Find()
		{
			HMODULE module = nullptr;
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(&EditorInstallPath), &module)) {
				return {};
			}
			std::wstring buffer(MAX_PATH, L'\0');
			for (;;) {
				const auto n = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
				if (n == 0) {
					return {};
				}
				if (n < buffer.size()) {
					buffer.resize(n);
					break;
				}
				buffer.resize(buffer.size() * 2);
			}
			// <mod folder>/SKSE/Plugins/DovaStash.dll -> <mod folder>/Tools/DovaStashEditor.exe
			const auto      root = std::filesystem::path(buffer).parent_path().parent_path().parent_path();
			const auto      editor = root / L"Tools" / L"DovaStashEditor.exe";
			std::error_code ec;
			if (!std::filesystem::is_regular_file(editor, ec)) {
				return {};
			}
			const auto text = editor.lexically_normal().u8string();
			return std::string(reinterpret_cast<const char*>(text.data()), text.size());
		}
	}

	const std::string& EditorInstallPath()
	{
		static const std::string path = Find();
		return path;
	}
}
