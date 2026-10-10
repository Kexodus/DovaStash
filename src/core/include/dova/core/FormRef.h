#pragma once

// Load-order independent form identity: "<PluginFile>|0x<LocalFormID>" (DESIGN §6.1).
//
// Decisions:
// - Plugin names compare case-insensitively (ASCII) for equality, ordering and hashing; the original
//   spelling is preserved for output.
// - Parsing tolerates surrounding/inner whitespace ("Skyrim.esm | 0x165a7") and a missing "0x" prefix,
//   accepts 1-8 hex digits, and formats back as 8 upper-case hex digits.
// - The plugin part must be a non-empty file name ending in .esm, .esp or .esl and must not contain
//   '|', path separators or control characters.
// - The id is a *local* form id, so values above 0x00FFFFFF are rejected: they almost certainly are
//   full runtime form ids including a load-order byte, and resolving them would bind the wrong form.

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace dova::core
{
	struct FormRef
	{
		std::string   plugin;
		std::uint32_t localId{ 0 };

		static constexpr std::uint32_t kMaxLocalId = 0x00FFFFFF;

		// Returns nullopt for anything that is not a well-formed reference.
		[[nodiscard]] static std::optional<FormRef> Parse(std::string_view text);

		// "Plugin.esm|0x00ABCDEF"
		[[nodiscard]] std::string ToString() const;

		// True when the plugin name is non-empty (a default-constructed FormRef is invalid).
		[[nodiscard]] bool IsValid() const noexcept { return !plugin.empty(); }

		friend bool                 operator==(const FormRef& a_lhs, const FormRef& a_rhs) noexcept;
		friend std::strong_ordering operator<=>(const FormRef& a_lhs, const FormRef& a_rhs) noexcept;
	};

	struct FormRefHash
	{
		[[nodiscard]] std::size_t operator()(const FormRef& a_ref) const noexcept;
	};

	// Case-insensitive (ASCII) helpers shared by everything that compares plugin names, keywords and
	// enum spellings.
	[[nodiscard]] bool        IEquals(std::string_view a_lhs, std::string_view a_rhs) noexcept;
	[[nodiscard]] std::string ToLower(std::string_view a_text);
	[[nodiscard]] int         ICompare(std::string_view a_lhs, std::string_view a_rhs) noexcept;
}

template <>
struct std::hash<dova::core::FormRef>
{
	[[nodiscard]] std::size_t operator()(const dova::core::FormRef& a_ref) const noexcept
	{
		return dova::core::FormRefHash{}(a_ref);
	}
};
