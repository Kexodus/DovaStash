#pragma once

// Semantic version used for template "version" ordering (DESIGN §6.2).
//
// Decisions: "MAJOR[.MINOR[.PATCH]][-prerelease][+build]" is accepted (missing parts are 0; a leading
// 'v' is tolerated). Precedence follows semver 2.0: build metadata is ignored, a pre-release sorts
// below its release, pre-release identifiers compare numerically when both are numeric, otherwise
// ASCII. Unparseable versions sort below every valid version (see CompareVersionStrings).

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dova::core
{
	struct SemVer
	{
		std::uint32_t            major{ 0 };
		std::uint32_t            minor{ 0 };
		std::uint32_t            patch{ 0 };
		std::vector<std::string> prerelease;

		[[nodiscard]] static std::optional<SemVer> Parse(std::string_view a_text);
		[[nodiscard]] std::string                  ToString() const;

		friend std::strong_ordering operator<=>(const SemVer& a_lhs, const SemVer& a_rhs);
		friend bool                 operator==(const SemVer& a_lhs, const SemVer& a_rhs) { return (a_lhs <=> a_rhs) == 0; }
	};

	// Orders raw version strings: invalid < valid; two invalid strings compare equal.
	[[nodiscard]] std::strong_ordering CompareVersionStrings(std::string_view a_lhs, std::string_view a_rhs);
}
