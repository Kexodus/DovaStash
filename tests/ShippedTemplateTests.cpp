// The shipped templates keep loading and behaving exactly as they did when released: schema changes are
// additive and optional only, so no existing file needs an edit (tests/data/shipped_templates.txt).

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <map>
#include <sstream>
#include <string>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"

using namespace dova::core;
using namespace dova::test;

#ifndef DOVA_DIST_DIR
#	error "DOVA_DIST_DIR must be defined by tests/CMakeLists.txt"
#endif
#ifndef DOVA_TEST_DATA_DIR
#	error "DOVA_TEST_DATA_DIR must be defined by tests/CMakeLists.txt"
#endif

namespace
{
	std::string ReadBytes(const std::filesystem::path& a_path)
	{
		std::ifstream in(a_path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(in), {});
	}

	std::string WithoutCR(std::string a_text)
	{
		std::erase(a_text, '\r');
		return a_text;
	}

	std::uint64_t Fnv1a64(std::string_view a_bytes)
	{
		std::uint64_t h = 0xcbf29ce484222325ull;
		for (const char c : a_bytes) {
			h ^= static_cast<unsigned char>(c);
			h *= 0x100000001b3ull;
		}
		return h;
	}

	// path -> hash, from tests/data/shipped_templates.txt
	std::map<std::string, std::string> Snapshot()
	{
		std::map<std::string, std::string> out;
		std::istringstream                 in(WithoutCR(ReadBytes(std::filesystem::path(DOVA_TEST_DATA_DIR) / "shipped_templates.txt")));
		for (std::string line; std::getline(in, line);) {
			if (line.empty() || line.front() == '#') {
				continue;
			}
			const auto space = line.rfind(' ');
			REQUIRE(space != std::string::npos);
			out.emplace(line.substr(0, space), line.substr(space + 1));
		}
		return out;
	}
}

TEST_CASE("Shipped templates load and behave exactly as released", "[dist][templates]")
{
	const auto dir = std::filesystem::path(DOVA_DIST_DIR) / "Templates";
	const auto snapshot = Snapshot();
	REQUIRE(snapshot.size() == 22);

	// The same files: none added, removed or edited.
	std::map<std::string, std::filesystem::path> files;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
		if (entry.is_regular_file() && entry.path().extension() == ".json") {
			files.emplace(entry.path().lexically_relative(dir).generic_string(), entry.path());
		}
	}
	for (const auto& [rel, hash] : snapshot) {
		INFO(rel);
		CHECK(files.contains(rel));
	}
	for (const auto& [rel, path] : files) {
		const auto line = std::format("{} {:016x}", rel, Fnv1a64(WithoutCR(ReadBytes(path))));
		INFO("current: " << line);
		REQUIRE(snapshot.contains(rel));
		CHECK(line == rel + " " + snapshot.at(rel));
	}

	// Loaded the way the plugin loads them: no errors, no warnings (template lint included).
	Diagnostics diags;
	const auto  templates = LoadTemplateDirectory(dir, diags);
	for (const auto& d : diags.Items()) {
		UNSCOPED_INFO(d.ToString());
	}
	CHECK(diags.Empty());
	REQUIRE(templates.size() == files.size());

	for (const auto& t : templates) {
		INFO(t.source);
		const auto rel = t.source.substr(std::string_view("Templates/").size());
		REQUIRE(files.contains(rel));

		// Every field is understood exactly as written: writing the parsed template back gives the same JSON
		// (nothing dropped, nothing added, no new key with a default value).
		const auto raw = nlohmann::json::parse(WithoutCR(ReadBytes(files.at(rel))), nullptr, true, true);
		const auto written = nlohmann::json::parse(LayoutToJson(t.layout).dump());
		CHECK(written == raw);

		// Every plugin it points into is in requires, so it is offered exactly where it was.
		for (const auto& plugin : ReferencedPlugins(t.layout)) {
			INFO(plugin);
			CHECK(std::ranges::any_of(t.layout.requiredPlugins, [&](const std::string& r) { return IEquals(r, plugin); }));
		}

		// Applying it to a home without a layout gives the template itself (plus basedOn): the display rules have
		// nothing to change, and nothing is reported missing even with every mod's forms absent.
		const auto applied = ApplyTemplate(t);
		Layout     expected = t.layout;
		expected.basedOn = MakeBasedOn(t.source, t.layout.version);
		CHECK(SerializeLayout(applied) == SerializeLayout(expected));
		CHECK(MissingTemplateItems(t.layout, t.layout, {}, [](const FormRef&) { return false; }).empty());
		const auto everyKind = [](const FormRef&) -> std::optional<DisplayKind> { return DisplayKind::Rack; };
		CHECK(MissingTemplateItems(t.layout, t.layout, everyKind, [](const FormRef&) { return false; }).empty());
	}
}
