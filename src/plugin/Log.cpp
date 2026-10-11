#include "Log.h"

#include <mutex>

namespace dova::log
{
	bool Init()
	{
		auto path = SKSE::log::log_directory();
		if (!path) {
			return false;
		}
		*path /= std::format("{}.log", SKSE::GetPluginName());

		std::vector<spdlog::sink_ptr> sinks{
			std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true),
			std::make_shared<spdlog::sinks::msvc_sink_mt>()  // visible in a debugger's output window
		};

		auto logger = std::make_shared<spdlog::logger>("DovaStash", sinks.begin(), sinks.end());
#ifndef NDEBUG
		logger->set_level(spdlog::level::debug);
#else
		logger->set_level(spdlog::level::info);
#endif
		logger->flush_on(spdlog::level::trace);  // low volume; never lose the last lines before a CTD
		logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%=5t] [%l] [%s:%#] %v");

		spdlog::set_default_logger(std::move(logger));
		return true;
	}

	bool FirstTime(std::string_view a_key)
	{
		static std::mutex                      lock;
		static std::unordered_set<std::string> seen;
		std::scoped_lock                       guard(lock);
		return seen.emplace(a_key).second;
	}

	void SetLevel(int a_level)
	{
		const auto level = (a_level >= spdlog::level::trace && a_level <= spdlog::level::off) ?
		                       static_cast<spdlog::level::level_enum>(a_level) :
		                       spdlog::level::info;
		spdlog::set_level(level);
	}
}
