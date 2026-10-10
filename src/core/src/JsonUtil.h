#pragma once

// Internal helpers shared by the JSON loaders. Not part of the public API.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "dova/core/Diagnostics.h"

namespace dova::core::detail
{
	using json = nlohmann::json;

	// Parses JSON, tolerating // and /* */ comments. Reports a parse error and returns nullopt on failure.
	std::optional<json> ParseJson(std::string_view a_text, std::string_view a_source, Diagnostics& a_diags);

	std::optional<std::string> ReadTextFile(const std::filesystem::path& a_path);
	// Writes to "<path>.tmp" then renames over the target, so a crash never leaves a half-written file.
	bool WriteTextFileAtomic(const std::filesystem::path& a_path, std::string_view a_text);

	std::string PathToUtf8(const std::filesystem::path& a_path);

	// Accepts a string or an array of strings. Non-string elements are reported and skipped.
	// Returns nullopt (after reporting an error) if the value is neither.
	std::optional<std::vector<std::string>> ReadStringList(const json& a_value, std::string_view a_source, std::string_view a_where, Diagnostics& a_diags);

	std::string TypeName(const json& a_value);

	// Serializes with tab indentation and a trailing newline.
	std::string Dump(const nlohmann::ordered_json& a_value);
}
