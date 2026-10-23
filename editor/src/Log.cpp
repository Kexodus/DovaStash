#include "Log.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace fs = std::filesystem;

namespace dova::editor
{
	namespace
	{
		std::mutex g_mutex;
		FILE*      g_file = nullptr;
		bool       g_tried = false;
		bool       g_runLog = false;
	}

	void UseRunLog()
	{
		std::scoped_lock lock(g_mutex);
		g_runLog = true;
	}

	fs::path LogFile()
	{
		wchar_t*    env = nullptr;
		std::size_t len = 0;
		// Tests and screenshots: next to their own state file.
		if (_wdupenv_s(&env, &len, L"DOVASTASH_EDITOR_STATE") == 0 && env) {
			fs::path p = fs::path(env).parent_path() / L"editor.log";
			std::free(env);
			return p;
		}
		if (g_runLog) {
			return fs::temp_directory_path() / L"DovaStashEditor" / L"editor-run.log";  // the author's editor.log stays theirs
		}
		fs::path base;
		if (_wdupenv_s(&env, &len, L"LOCALAPPDATA") == 0 && env) {
			base = env;
			std::free(env);
		} else {
			base = fs::temp_directory_path();
		}
		return base / L"DovaStashEditor" / L"editor.log";
	}

	void LogLine(std::string_view a_text)
	{
		std::scoped_lock lock(g_mutex);
		if (!g_file && !g_tried) {
			g_tried = true;
			const auto      path = LogFile();
			std::error_code ec;
			fs::create_directories(path.parent_path(), ec);
			if (path.filename() == L"editor.log") {
				auto old = path;
				old.replace_filename(L"editor.old.log");
				fs::rename(path, old, ec);
			}
			g_file = _wfsopen(path.c_str(), L"wb", _SH_DENYWR);
		}
		if (!g_file) {
			return;
		}
		SYSTEMTIME t{};
		::GetLocalTime(&t);
		std::fprintf(g_file, "%02u:%02u:%02u.%03u [%5lu] %.*s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, ::GetCurrentThreadId(),
			static_cast<int>(a_text.size()), a_text.data());
		std::fflush(g_file);
	}
}
