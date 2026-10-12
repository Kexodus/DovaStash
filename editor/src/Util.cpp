#include "Util.h"

#include <fstream>
#include <sstream>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "dova/core/FormRef.h"

namespace dova::editor
{
	std::wstring Widen(std::string_view a_utf8)
	{
		if (a_utf8.empty()) {
			return {};
		}
		const int n = ::MultiByteToWideChar(CP_UTF8, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), nullptr, 0);
		std::wstring out(static_cast<std::size_t>(n), L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), out.data(), n);
		return out;
	}

	std::string Narrow(std::wstring_view a_wide)
	{
		if (a_wide.empty()) {
			return {};
		}
		const int n = ::WideCharToMultiByte(CP_UTF8, 0, a_wide.data(), static_cast<int>(a_wide.size()), nullptr, 0, nullptr, nullptr);
		std::string out(static_cast<std::size_t>(n), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, a_wide.data(), static_cast<int>(a_wide.size()), out.data(), n, nullptr, nullptr);
		return out;
	}

	std::string PathToUtf8(const std::filesystem::path& a_path) { return Narrow(a_path.native()); }

	std::filesystem::path PathFromUtf8(std::string_view a_utf8) { return std::filesystem::path(Widen(a_utf8)); }

	std::string BytesToUtf8(std::string_view a_bytes)
	{
		if (a_bytes.empty()) {
			return {};
		}
		if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_bytes.data(), static_cast<int>(a_bytes.size()), nullptr, 0) > 0) {
			return std::string(a_bytes);
		}
		const int n = ::MultiByteToWideChar(1252, 0, a_bytes.data(), static_cast<int>(a_bytes.size()), nullptr, 0);
		std::wstring wide(static_cast<std::size_t>(n), L'\0');
		::MultiByteToWideChar(1252, 0, a_bytes.data(), static_cast<int>(a_bytes.size()), wide.data(), n);
		return Narrow(wide);
	}

	std::string_view Trim(std::string_view a_text)
	{
		const auto first = a_text.find_first_not_of(" \t\r\n");
		if (first == std::string_view::npos) {
			return {};
		}
		const auto last = a_text.find_last_not_of(" \t\r\n");
		return a_text.substr(first, last - first + 1);
	}

	bool IContains(std::string_view a_haystack, std::string_view a_needle)
	{
		if (a_needle.empty()) {
			return true;
		}
		return core::ToLower(a_haystack).find(core::ToLower(a_needle)) != std::string::npos;
	}

	bool IEndsWith(std::string_view a_text, std::string_view a_suffix)
	{
		return a_text.size() >= a_suffix.size() && core::IEquals(a_text.substr(a_text.size() - a_suffix.size()), a_suffix);
	}

	std::optional<std::string> ReadTextFile(const std::filesystem::path& a_path)
	{
		std::ifstream in(a_path, std::ios::binary);
		if (!in) {
			return std::nullopt;
		}
		std::ostringstream ss;
		ss << in.rdbuf();
		std::string text = std::move(ss).str();
		if (text.starts_with("\xEF\xBB\xBF")) {
			text.erase(0, 3);
		}
		return text;
	}

	std::vector<std::string> SplitLines(std::string_view a_text)
	{
		std::vector<std::string> out;
		std::size_t              pos = 0;
		while (pos <= a_text.size()) {
			auto end = a_text.find('\n', pos);
			if (end == std::string_view::npos) {
				end = a_text.size();
			}
			auto line = a_text.substr(pos, end - pos);
			if (!line.empty() && line.back() == '\r') {
				line.remove_suffix(1);
			}
			if (end < a_text.size() || !line.empty()) {
				out.emplace_back(line);
			}
			pos = end + 1;
		}
		return out;
	}

	std::string QtUnescape(std::string_view a_value)
	{
		auto v = Trim(a_value);
		if (v.starts_with("@ByteArray(") && v.ends_with(")")) {
			v = v.substr(11, v.size() - 12);
		} else if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
			v = v.substr(1, v.size() - 2);
		}
		std::string out;
		out.reserve(v.size());
		for (std::size_t i = 0; i < v.size(); ++i) {
			if (v[i] == '\\' && i + 1 < v.size()) {
				const char n = v[++i];
				switch (n) {
				case 'n':
					out += '\n';
					break;
				case 't':
					out += '\t';
					break;
				case '0':
					out += '\0';
					break;
				default:
					out += n;  // "\\" -> "\", "\"" -> '"'
					break;
				}
			} else {
				out += v[i];
			}
		}
		return out;
	}

	std::optional<IniFile> IniFile::Load(const std::filesystem::path& a_path)
	{
		const auto text = ReadTextFile(a_path);
		if (!text) {
			return std::nullopt;
		}
		IniFile     ini;
		std::string section;
		for (const auto& raw : SplitLines(*text)) {
			const auto line = Trim(raw);
			if (line.empty() || line.front() == ';' || line.front() == '#') {
				continue;
			}
			if (line.front() == '[' && line.back() == ']') {
				section = core::ToLower(Trim(line.substr(1, line.size() - 2)));
				continue;
			}
			const auto eq = line.find('=');
			if (eq == std::string_view::npos) {
				continue;
			}
			ini._sections[section][core::ToLower(Trim(line.substr(0, eq)))] = std::string(Trim(line.substr(eq + 1)));
		}
		return ini;
	}

	std::optional<std::string> IniFile::Get(std::string_view a_section, std::string_view a_key) const
	{
		const auto s = _sections.find(core::ToLower(a_section));
		if (s == _sections.end()) {
			return std::nullopt;
		}
		const auto k = s->second.find(core::ToLower(a_key));
		if (k == s->second.end()) {
			return std::nullopt;
		}
		return QtUnescape(k->second);
	}
}
