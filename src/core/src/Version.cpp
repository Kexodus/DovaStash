#include "dova/core/Version.h"

#include <algorithm>
#include <limits>

namespace dova::core
{
	namespace
	{
		bool IsDigits(std::string_view a_text) noexcept
		{
			return !a_text.empty() && std::ranges::all_of(a_text, [](char c) { return c >= '0' && c <= '9'; });
		}

		std::optional<std::uint32_t> ParseUint(std::string_view a_text) noexcept
		{
			if (!IsDigits(a_text) || a_text.size() > 9) {
				return std::nullopt;
			}
			std::uint32_t v = 0;
			for (const char c : a_text) {
				v = v * 10 + static_cast<std::uint32_t>(c - '0');
			}
			return v;
		}

		bool IsIdentifierChar(char a_ch) noexcept
		{
			return (a_ch >= '0' && a_ch <= '9') || (a_ch >= 'a' && a_ch <= 'z') || (a_ch >= 'A' && a_ch <= 'Z') || a_ch == '-';
		}
	}

	std::optional<SemVer> SemVer::Parse(std::string_view a_text)
	{
		while (!a_text.empty() && (a_text.front() == ' ' || a_text.front() == '\t')) {
			a_text.remove_prefix(1);
		}
		while (!a_text.empty() && (a_text.back() == ' ' || a_text.back() == '\t')) {
			a_text.remove_suffix(1);
		}
		if (!a_text.empty() && (a_text.front() == 'v' || a_text.front() == 'V')) {
			a_text.remove_prefix(1);
		}
		if (const auto plus = a_text.find('+'); plus != std::string_view::npos) {
			a_text = a_text.substr(0, plus);
		}
		std::string_view pre;
		if (const auto dash = a_text.find('-'); dash != std::string_view::npos) {
			pre = a_text.substr(dash + 1);
			a_text = a_text.substr(0, dash);
			if (pre.empty()) {
				return std::nullopt;
			}
		}

		SemVer        v;
		std::uint32_t* parts[3] = { &v.major, &v.minor, &v.patch };
		std::size_t   n = 0;
		while (true) {
			const auto dot = a_text.find('.');
			const auto part = a_text.substr(0, dot);
			if (n >= 3) {
				return std::nullopt;
			}
			const auto value = ParseUint(part);
			if (!value) {
				return std::nullopt;
			}
			*parts[n++] = *value;
			if (dot == std::string_view::npos) {
				break;
			}
			a_text.remove_prefix(dot + 1);
		}

		while (!pre.empty()) {
			const auto dot = pre.find('.');
			const auto id = pre.substr(0, dot);
			if (id.empty() || !std::ranges::all_of(id, IsIdentifierChar)) {
				return std::nullopt;
			}
			v.prerelease.emplace_back(id);
			if (dot == std::string_view::npos) {
				break;
			}
			pre.remove_prefix(dot + 1);
			if (pre.empty()) {
				return std::nullopt;
			}
		}
		return v;
	}

	std::string SemVer::ToString() const
	{
		std::string out = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
		for (std::size_t i = 0; i < prerelease.size(); ++i) {
			out += i == 0 ? '-' : '.';
			out += prerelease[i];
		}
		return out;
	}

	std::strong_ordering operator<=>(const SemVer& a_lhs, const SemVer& a_rhs)
	{
		if (auto c = a_lhs.major <=> a_rhs.major; c != 0) {
			return c;
		}
		if (auto c = a_lhs.minor <=> a_rhs.minor; c != 0) {
			return c;
		}
		if (auto c = a_lhs.patch <=> a_rhs.patch; c != 0) {
			return c;
		}
		// A version without pre-release has higher precedence.
		if (a_lhs.prerelease.empty() != a_rhs.prerelease.empty()) {
			return a_lhs.prerelease.empty() ? std::strong_ordering::greater : std::strong_ordering::less;
		}
		const auto n = std::min(a_lhs.prerelease.size(), a_rhs.prerelease.size());
		for (std::size_t i = 0; i < n; ++i) {
			const auto& l = a_lhs.prerelease[i];
			const auto& r = a_rhs.prerelease[i];
			const bool  ln = IsDigits(l);
			const bool  rn = IsDigits(r);
			if (ln && rn) {
				if (l.size() != r.size()) {
					return l.size() <=> r.size();  // no leading zeros assumed: longer = bigger
				}
				if (auto c = l <=> r; c != 0) {
					return c;
				}
			} else if (ln != rn) {
				return ln ? std::strong_ordering::less : std::strong_ordering::greater;  // numeric < alphanumeric
			} else if (auto c = l <=> r; c != 0) {
				return c;
			}
		}
		return a_lhs.prerelease.size() <=> a_rhs.prerelease.size();
	}

	std::strong_ordering CompareVersionStrings(std::string_view a_lhs, std::string_view a_rhs)
	{
		const auto l = SemVer::Parse(a_lhs);
		const auto r = SemVer::Parse(a_rhs);
		if (l && r) {
			return *l <=> *r;
		}
		if (!l && !r) {
			return std::strong_ordering::equal;
		}
		return l ? std::strong_ordering::greater : std::strong_ordering::less;
	}
}
