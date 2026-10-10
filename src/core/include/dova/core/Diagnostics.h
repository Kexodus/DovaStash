#pragma once

// Core never logs. Loaders and compilers append human-readable diagnostics here; the plugin decides
// how to surface them (log file, notification).
//
// Severity contract: an Error means something in the input was rejected (a file, a category, a rule
// or an entry was skipped) and the author must fix it. A Warning means the input was accepted but is
// suspicious or partly unusable at runtime (e.g. an unresolvable formList).

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace dova::core
{
	struct Diagnostic
	{
		enum class Severity : std::uint8_t
		{
			Warning,
			Error
		};

		Severity    severity{ Severity::Warning };
		std::string source;   // file name or logical origin ("Categories/00_Default.json", "keep")
		std::string message;  // includes a JSON-pointer-ish location where useful

		[[nodiscard]] std::string ToString() const
		{
			std::string out = severity == Severity::Error ? "error: " : "warning: ";
			if (!source.empty()) {
				out += source;
				out += ": ";
			}
			out += message;
			return out;
		}
	};

	class Diagnostics
	{
	public:
		void Warn(std::string a_source, std::string a_message)
		{
			_items.push_back({ Diagnostic::Severity::Warning, std::move(a_source), std::move(a_message) });
		}

		void Error(std::string a_source, std::string a_message)
		{
			_items.push_back({ Diagnostic::Severity::Error, std::move(a_source), std::move(a_message) });
		}

		void Append(const Diagnostics& a_other) { _items.insert(_items.end(), a_other._items.begin(), a_other._items.end()); }

		[[nodiscard]] const std::vector<Diagnostic>& Items() const noexcept { return _items; }
		[[nodiscard]] bool                           Empty() const noexcept { return _items.empty(); }
		[[nodiscard]] bool                           HasErrors() const noexcept { return ErrorCount() > 0; }

		[[nodiscard]] std::size_t ErrorCount() const noexcept { return Count(Diagnostic::Severity::Error); }
		[[nodiscard]] std::size_t WarningCount() const noexcept { return Count(Diagnostic::Severity::Warning); }

		void Clear() noexcept { _items.clear(); }

	private:
		[[nodiscard]] std::size_t Count(Diagnostic::Severity a_severity) const noexcept
		{
			std::size_t n = 0;
			for (const auto& d : _items) {
				n += d.severity == a_severity ? 1u : 0u;
			}
			return n;
		}

		std::vector<Diagnostic> _items;
	};
}
