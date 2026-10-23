// DovaStashEditor.com: the editor's console companion (docs/EDITOR.md §9). DovaStashEditor.exe is a Windows
// program (no console flash under MO2), so a shell that starts it does not wait for it, and its output and exit
// code are lost. Typed as `DovaStashEditor`, cmd and PowerShell pick this .com first (PATHEXT): it runs the .exe
// next to it with the same arguments and the console's handles, waits, and returns the .exe's exit code.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <string>

namespace
{
	// The command line after the program's own name (as typed: quotes and spaces kept).
	const wchar_t* Arguments()
	{
		const wchar_t* p = ::GetCommandLineW();
		if (*p == L'"') {
			for (++p; *p && *p != L'"'; ++p) {}
			if (*p == L'"') {
				++p;
			}
		} else {
			while (*p && *p != L' ' && *p != L'\t') {
				++p;
			}
		}
		while (*p == L' ' || *p == L'\t') {
			++p;
		}
		return p;
	}

	// The standard handle, inheritable, for the child (nullptr when there is none).
	HANDLE Inheritable(DWORD a_which)
	{
		const HANDLE h = ::GetStdHandle(a_which);
		HANDLE       dup = nullptr;
		if (h == nullptr || h == INVALID_HANDLE_VALUE ||
			!::DuplicateHandle(::GetCurrentProcess(), h, ::GetCurrentProcess(), &dup, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
			return nullptr;
		}
		return dup;
	}

	void Error(const std::wstring& a_text)
	{
		const HANDLE err = ::GetStdHandle(STD_ERROR_HANDLE);
		DWORD        written = 0;
		const auto   line = a_text + L"\r\n";
		if (!::WriteConsoleW(err, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr)) {
			const int n = ::WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
			std::string utf8(static_cast<std::size_t>(n), '\0');
			::WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), utf8.data(), n, nullptr, nullptr);
			::WriteFile(err, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
		}
	}
}

int wmain()
{
	std::wstring self(32768, L'\0');
	self.resize(::GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size())));
	const auto dot = self.find_last_of(L'.');
	const auto exe = (dot == std::wstring::npos ? self : self.substr(0, dot)) + L".exe";
	std::wstring command = L"\"" + exe + L"\"";
	if (const wchar_t* args = Arguments(); *args) {
		command += L" ";
		command += args;
	}
	::SetConsoleOutputCP(CP_UTF8);  // the editor writes UTF-8 (home and mod names)

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = Inheritable(STD_INPUT_HANDLE);
	si.hStdOutput = Inheritable(STD_OUTPUT_HANDLE);
	si.hStdError = Inheritable(STD_ERROR_HANDLE);
	PROCESS_INFORMATION pi{};
	if (!::CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
		Error(L"DovaStashEditor.com: cannot start " + exe + L" (error " + std::to_wstring(::GetLastError()) + L")");
		return 9009;
	}
	::WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD code = 1;
	::GetExitCodeProcess(pi.hProcess, &code);
	::CloseHandle(pi.hThread);
	::CloseHandle(pi.hProcess);
	return static_cast<int>(code);
}
