#include "dova/core/FormRef.h"

#include <array>
#include <format>

namespace dova::core
{
	namespace
	{
		constexpr char LowerAscii(char a_ch) noexcept
		{
			return (a_ch >= 'A' && a_ch <= 'Z') ? static_cast<char>(a_ch - 'A' + 'a') : a_ch;
		}

		constexpr bool IsSpace(char a_ch) noexcept
		{
			return a_ch == ' ' || a_ch == '\t' || a_ch == '\r' || a_ch == '\n' || a_ch == '\f' || a_ch == '\v';
		}

		std::string_view Trim(std::string_view a_text) noexcept
		{
			while (!a_text.empty() && IsSpace(a_text.front())) {
				a_text.remove_prefix(1);
			}
			while (!a_text.empty() && IsSpace(a_text.back())) {
				a_text.remove_suffix(1);
			}
			return a_text;
		}

		bool EndsWithI(std::string_view a_text, std::string_view a_suffix) noexcept
		{
			return a_text.size() >= a_suffix.size() && IEquals(a_text.substr(a_text.size() - a_suffix.size()), a_suffix);
		}

		bool IsValidPluginName(std::string_view a_name) noexcept
		{
			if (a_name.size() <= 4) {  // needs at least one char before the extension
				return false;
			}
			for (const char ch : a_name) {
				const auto uch = static_cast<unsigned char>(ch);
				if (uch < 0x20 || uch == 0x7F || ch == '|' || ch == '/' || ch == '\\' || ch == ':' || ch == '"' ||
					ch == '<' || ch == '>' || ch == '?' || ch == '*') {
					return false;
				}
			}
			return EndsWithI(a_name, ".esm") || EndsWithI(a_name, ".esp") || EndsWithI(a_name, ".esl");
		}

		std::optional<std::uint32_t> ParseHexId(std::string_view a_text) noexcept
		{
			if (a_text.size() >= 2 && a_text[0] == '0' && (a_text[1] == 'x' || a_text[1] == 'X')) {
				a_text.remove_prefix(2);
			}
			if (a_text.empty() || a_text.size() > 8) {
				return std::nullopt;
			}
			std::uint32_t value = 0;
			for (const char ch : a_text) {
				std::uint32_t digit = 0;
				if (ch >= '0' && ch <= '9') {
					digit = static_cast<std::uint32_t>(ch - '0');
				} else if (ch >= 'a' && ch <= 'f') {
					digit = static_cast<std::uint32_t>(ch - 'a' + 10);
				} else if (ch >= 'A' && ch <= 'F') {
					digit = static_cast<std::uint32_t>(ch - 'A' + 10);
				} else {
					return std::nullopt;
				}
				value = (value << 4) | digit;
			}
			return value;
		}
	}

	bool IEquals(std::string_view a_lhs, std::string_view a_rhs) noexcept
	{
		if (a_lhs.size() != a_rhs.size()) {
			return false;
		}
		for (std::size_t i = 0; i < a_lhs.size(); ++i) {
			if (LowerAscii(a_lhs[i]) != LowerAscii(a_rhs[i])) {
				return false;
			}
		}
		return true;
	}

	int ICompare(std::string_view a_lhs, std::string_view a_rhs) noexcept
	{
		const std::size_t n = a_lhs.size() < a_rhs.size() ? a_lhs.size() : a_rhs.size();
		for (std::size_t i = 0; i < n; ++i) {
			const auto l = static_cast<unsigned char>(LowerAscii(a_lhs[i]));
			const auto r = static_cast<unsigned char>(LowerAscii(a_rhs[i]));
			if (l != r) {
				return l < r ? -1 : 1;
			}
		}
		if (a_lhs.size() == a_rhs.size()) {
			return 0;
		}
		return a_lhs.size() < a_rhs.size() ? -1 : 1;
	}

	std::string ToLower(std::string_view a_text)
	{
		std::string out(a_text);
		for (auto& ch : out) {
			ch = LowerAscii(ch);
		}
		return out;
	}

	std::optional<FormRef> FormRef::Parse(std::string_view a_text)
	{
		const auto bar = a_text.find('|');
		if (bar == std::string_view::npos || a_text.find('|', bar + 1) != std::string_view::npos) {
			return std::nullopt;
		}
		const auto plugin = Trim(a_text.substr(0, bar));
		const auto idText = Trim(a_text.substr(bar + 1));
		if (!IsValidPluginName(plugin)) {
			return std::nullopt;
		}
		const auto id = ParseHexId(idText);
		if (!id || *id > kMaxLocalId) {
			return std::nullopt;
		}
		return FormRef{ std::string(plugin), *id };
	}

	std::string FormRefError(std::string_view a_text)
	{
		constexpr std::string_view kShape = "expected \"Plugin.esm|0x00ABCDEF\"";
		const auto                 bar = a_text.find('|');
		if (bar == std::string_view::npos || a_text.find('|', bar + 1) != std::string_view::npos) {
			return std::string(kShape);
		}
		if (!IsValidPluginName(Trim(a_text.substr(0, bar)))) {
			return "not a plugin file name (.esm, .esp or .esl)";
		}
		const auto id = ParseHexId(Trim(a_text.substr(bar + 1)));
		if (!id) {
			return std::string(kShape);
		}
		if (*id > FormRef::kMaxLocalId) {
			if ((*id >> 24) == 0xFE) {  // a light plugin's runtime id: FE, 3 digits of light index, 3 of local id
				return std::format("form id above 0x00FFFFFF, drop the light plugin prefix (0x{:08X} -> 0x{:08X})", *id, *id & 0xFFF);
			}
			return std::format("form id above 0x00FFFFFF, drop the load-order byte (0x{:08X} -> 0x{:08X})", *id, *id & FormRef::kMaxLocalId);
		}
		return {};
	}

	std::string FormRef::ToString() const
	{
		static constexpr std::array<char, 16> kHex{ '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
		std::string out;
		out.reserve(plugin.size() + 11);
		out += plugin;
		out += "|0x";
		for (int shift = 28; shift >= 0; shift -= 4) {
			out += kHex[(localId >> shift) & 0xF];
		}
		return out;
	}

	bool operator==(const FormRef& a_lhs, const FormRef& a_rhs) noexcept
	{
		return a_lhs.localId == a_rhs.localId && IEquals(a_lhs.plugin, a_rhs.plugin);
	}

	std::strong_ordering operator<=>(const FormRef& a_lhs, const FormRef& a_rhs) noexcept
	{
		const int cmp = ICompare(a_lhs.plugin, a_rhs.plugin);
		if (cmp != 0) {
			return cmp < 0 ? std::strong_ordering::less : std::strong_ordering::greater;
		}
		return a_lhs.localId <=> a_rhs.localId;
	}

	std::size_t FormRefHash::operator()(const FormRef& a_ref) const noexcept
	{
		// FNV-1a over the lower-cased plugin name, then the id.
		std::uint64_t h = 14695981039346656037ull;
		for (const char ch : a_ref.plugin) {
			h ^= static_cast<unsigned char>(LowerAscii(ch));
			h *= 1099511628211ull;
		}
		for (int i = 0; i < 4; ++i) {
			h ^= (a_ref.localId >> (i * 8)) & 0xFF;
			h *= 1099511628211ull;
		}
		return static_cast<std::size_t>(h);
	}
}
