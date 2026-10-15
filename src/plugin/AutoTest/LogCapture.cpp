#include "AutoTest/LogCapture.h"

#include <spdlog/sinks/base_sink.h>

#include <deque>
#include <mutex>

namespace dova::autotest::logcap
{
	namespace
	{
		constexpr std::size_t kMaxLines = 200000;  // a full run logs a few thousand lines

		class Sink final : public spdlog::sinks::base_sink<std::mutex>
		{
		public:
			std::uint64_t Next()
			{
				std::scoped_lock guard(mutex_);
				return _next;
			}

			std::vector<Line> Since(std::uint64_t a_since)
			{
				std::scoped_lock  guard(mutex_);
				std::vector<Line> out;
				for (const auto& line : _lines) {
					if (line.seq >= a_since && !line.text.starts_with("AutoTest:")) {
						out.push_back(line);
					}
				}
				return out;
			}

		protected:
			void sink_it_(const spdlog::details::log_msg& a_msg) override
			{
				_lines.push_back(Line{ _next++, a_msg.level, std::string(a_msg.payload.begin(), a_msg.payload.end()) });
				if (_lines.size() > kMaxLines) {
					_lines.pop_front();
				}
			}

			void flush_() override {}

		private:
			std::deque<Line> _lines;
			std::uint64_t    _next{ 1 };
		};

		std::shared_ptr<Sink>& Instance()
		{
			static std::shared_ptr<Sink> sink;
			return sink;
		}
	}

	void Install()
	{
		auto& sink = Instance();
		if (sink) {
			return;
		}
		sink = std::make_shared<Sink>();
		sink->set_level(spdlog::level::trace);
		if (auto logger = spdlog::default_logger()) {
			logger->sinks().push_back(sink);
		}
	}

	std::uint64_t Mark()
	{
		const auto& sink = Instance();
		return sink ? sink->Next() : 0;
	}

	std::vector<Line> Since(std::uint64_t a_since)
	{
		const auto& sink = Instance();
		return sink ? sink->Since(a_since) : std::vector<Line>{};
	}
}
