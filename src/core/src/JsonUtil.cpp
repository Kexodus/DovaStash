#include "JsonUtil.h"

#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

namespace dova::core::detail
{
	std::optional<json> ParseJson(std::string_view a_text, std::string_view a_source, Diagnostics& a_diags)
	{
		// Strip a UTF-8 BOM if an editor added one.
		if (a_text.size() >= 3 && static_cast<unsigned char>(a_text[0]) == 0xEF && static_cast<unsigned char>(a_text[1]) == 0xBB &&
			static_cast<unsigned char>(a_text[2]) == 0xBF) {
			a_text.remove_prefix(3);
		}
		try {
			return json::parse(a_text.begin(), a_text.end(), nullptr, true, true);
		} catch (const json::parse_error& e) {
			a_diags.Error(std::string(a_source), std::string("invalid JSON: ") + e.what());
			return std::nullopt;
		}
	}

	std::optional<std::string> ReadTextFile(const std::filesystem::path& a_path)
	{
		std::ifstream in(a_path, std::ios::binary);
		if (!in) {
			return std::nullopt;
		}
		std::ostringstream ss;
		ss << in.rdbuf();
		if (in.bad()) {
			return std::nullopt;
		}
		return std::move(ss).str();
	}

	bool WriteTextFileAtomic(const std::filesystem::path& a_path, std::string_view a_text)
	{
		std::error_code ec;
		if (a_path.has_parent_path()) {
			std::filesystem::create_directories(a_path.parent_path(), ec);
		}
		auto tmp = a_path;
		tmp += ".tmp";
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out) {
				return false;
			}
			out.write(a_text.data(), static_cast<std::streamsize>(a_text.size()));
			out.flush();
			if (!out) {
				return false;
			}
		}
		std::filesystem::rename(tmp, a_path, ec);
		if (ec) {
			std::filesystem::remove(tmp, ec);
			return false;
		}
		return true;
	}

	std::string PathToUtf8(const std::filesystem::path& a_path)
	{
		const auto u8 = a_path.generic_u8string();
		return std::string(u8.begin(), u8.end());
	}

	std::string TypeName(const json& a_value)
	{
		return a_value.type_name();
	}

	std::optional<std::vector<std::string>> ReadStringList(const json& a_value, std::string_view a_source, std::string_view a_where, Diagnostics& a_diags)
	{
		std::vector<std::string> out;
		if (a_value.is_string()) {
			out.push_back(a_value.get<std::string>());
			return out;
		}
		if (!a_value.is_array()) {
			a_diags.Error(std::string(a_source), std::string(a_where) + ": expected a string or an array of strings, got " + TypeName(a_value));
			return std::nullopt;
		}
		for (std::size_t i = 0; i < a_value.size(); ++i) {
			const auto& el = a_value[i];
			if (!el.is_string()) {
				a_diags.Warn(std::string(a_source), std::string(a_where) + "[" + std::to_string(i) + "]: expected a string, got " + TypeName(el) + " (ignored)");
				continue;
			}
			out.push_back(el.get<std::string>());
		}
		return out;
	}

	std::string Dump(const nlohmann::ordered_json& a_value)
	{
		auto out = a_value.dump(1, '\t', false, nlohmann::ordered_json::error_handler_t::replace);
		out += '\n';
		return out;
	}
}
