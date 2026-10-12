#include "JsonUtil.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
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

	namespace
	{
		using ojson = nlohmann::ordered_json;

		constexpr std::size_t kWidth = 200;  // longest line kept on one line, a tab counting as 4 columns
		constexpr std::size_t kTabWidth = 4;

		std::string Scalar(const ojson& a_value)
		{
			return a_value.dump(-1, ' ', false, ojson::error_handler_t::replace);
		}

		bool IsListOfObjects(const ojson& a_value)
		{
			return a_value.is_array() && std::ranges::any_of(a_value, [](const ojson& a_el) { return a_el.is_object(); });
		}

		// A value that may go on one line: an object whose values are scalars or lists without objects, or a
		// list of scalars. Empty objects and lists always may.
		bool Flat(const ojson& a_value)
		{
			if (a_value.empty()) {
				return true;
			}
			if (a_value.is_object()) {
				return std::ranges::none_of(a_value, [](const ojson& a_el) { return a_el.is_object() || IsListOfObjects(a_el); });
			}
			if (a_value.is_array()) {
				return std::ranges::none_of(a_value, [](const ojson& a_el) { return a_el.is_structured(); });
			}
			return true;
		}

		// `"key": value` for each member of a flat object.
		std::vector<std::string> Members(const ojson& a_object);

		std::string OneLine(const ojson& a_value)
		{
			if (a_value.is_object()) {
				std::string out = "{";
				const auto members = Members(a_value);
				for (std::size_t i = 0; i < members.size(); ++i) {
					out += (i ? ", " : "") + members[i];
				}
				return out + "}";
			}
			if (a_value.is_array()) {
				std::string out = "[";
				for (std::size_t i = 0; i < a_value.size(); ++i) {
					out += (i ? ", " : "") + OneLine(a_value[i]);
				}
				return out + "]";
			}
			return Scalar(a_value);
		}

		std::vector<std::string> Members(const ojson& a_object)
		{
			std::vector<std::string> out;
			for (const auto& [key, value] : a_object.items()) {
				out.push_back(Scalar(ojson(key)) + ": " + OneLine(value));
			}
			return out;
		}

		bool Fits(std::size_t a_depth, std::string_view a_text)
		{
			return a_depth * kTabWidth + a_text.size() <= kWidth;
		}

		void Write(const ojson& a_value, std::size_t a_depth, std::string& a_out);

		// A list of objects: one per line, and the fields of the one-line entries lined up in columns (a
		// field is padded to the widest field with the same key in the same position).
		void WriteTable(const ojson& a_list, std::size_t a_depth, std::string& a_out)
		{
			const std::string indent(a_depth + 1, '\t');
			std::vector<std::optional<std::vector<std::string>>> rows;
			std::map<std::pair<std::size_t, std::string>, std::size_t> widths;
			for (const auto& el : a_list) {
				if (el.is_object() && Flat(el) && Fits(a_depth + 1, OneLine(el))) {
					auto members = Members(el);
					std::size_t i = 0;
					for (const auto& [key, value] : el.items()) {
						auto& w = widths[{ i, key }];
						w = std::max(w, members[i].size());
						++i;
					}
					rows.emplace_back(std::move(members));
				} else {
					rows.emplace_back();
				}
			}

			a_out += "[\n";
			for (std::size_t r = 0; r < rows.size(); ++r) {
				a_out += indent;
				if (!rows[r]) {
					Write(a_list[r], a_depth + 1, a_out);
				} else {
					const auto& members = *rows[r];
					a_out += '{';
					std::size_t i = 0;
					for (const auto& [key, value] : a_list[r].items()) {
						a_out += members[i];
						if (i + 1 < members.size()) {
							a_out += ',';
							a_out.append(widths[{ i, key }] - members[i].size() + 1, ' ');
						}
						++i;
					}
					a_out += '}';
				}
				a_out += r + 1 < rows.size() ? ",\n" : "\n";
			}
			a_out += std::string(a_depth, '\t') + "]";
		}

		void Write(const ojson& a_value, std::size_t a_depth, std::string& a_out)
		{
			if (!a_value.is_structured()) {
				a_out += Scalar(a_value);
				return;
			}
			// The top-level object always spreads over lines.
			if ((a_depth > 0 || a_value.is_array()) && Flat(a_value)) {
				auto line = OneLine(a_value);
				if (Fits(a_depth, line)) {
					a_out += line;
					return;
				}
			}
			if (IsListOfObjects(a_value)) {
				WriteTable(a_value, a_depth, a_out);
				return;
			}
			const std::string indent(a_depth + 1, '\t');
			const bool        object = a_value.is_object();
			a_out += object ? "{\n" : "[\n";
			std::size_t i = 0;
			for (const auto& [key, value] : a_value.items()) {
				a_out += indent;
				if (object) {
					a_out += Scalar(ojson(key)) + ": ";
				}
				Write(value, a_depth + 1, a_out);
				a_out += ++i < a_value.size() ? ",\n" : "\n";
			}
			a_out += std::string(a_depth, '\t') + (object ? "}" : "]");
		}
	}

	std::string Dump(const nlohmann::ordered_json& a_value)
	{
		std::string out;
		Write(a_value, 0, out);
		out += '\n';
		return out;
	}
}
