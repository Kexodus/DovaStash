// Validates the shipped data under dist/Data/SKSE/Plugins/DovaStash against the core loaders.
// Passes trivially while the directories are empty.

#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"

using namespace dova::core;

#ifndef DOVA_DIST_DIR
#	error "DOVA_DIST_DIR must be defined by tests/CMakeLists.txt"
#endif

namespace
{
	const std::filesystem::path kDist{ DOVA_DIST_DIR };

	void Report(const Diagnostics& a_diags)
	{
		for (const auto& d : a_diags.Items()) {
			if (d.severity == Diagnostic::Severity::Error) {
				UNSCOPED_INFO(d.ToString());
			} else {
				WARN(d.ToString());
			}
		}
	}
}

TEST_CASE("Shipped categories parse without errors", "[dist]")
{
	const auto dir = kDist / "Categories";
	std::error_code ec;
	if (!std::filesystem::is_directory(dir, ec)) {
		SUCCEED("no Categories directory");
		return;
	}
	for (const auto& entry : std::filesystem::directory_iterator(dir)) {
		if (!entry.is_regular_file() || entry.path().extension() != ".json") {
			continue;
		}
		Diagnostics diags;
		const auto  text = [&] {
			std::ifstream in(entry.path(), std::ios::binary);
			return std::string(std::istreambuf_iterator<char>(in), {});
		}();
		const auto defs = ParseCategoryText(text, entry.path().filename().string(), diags);
		INFO(entry.path().string());
		Report(diags);
		CHECK_FALSE(diags.HasErrors());
		CHECK_FALSE(defs.empty());
	}

	Diagnostics all;
	(void)CategorySet::Compile(LoadCategoryDirectory(dir, all), {}, all);
	Report(all);
	CHECK_FALSE(all.HasErrors());
}

TEST_CASE("Shipped templates parse without errors", "[dist]")
{
	const auto dir = kDist / "Templates";
	std::error_code ec;
	if (!std::filesystem::is_directory(dir, ec)) {
		SUCCEED("no Templates directory");
		return;
	}
	Diagnostics diags;
	const auto  templates = LoadTemplateDirectory(dir, diags);
	Report(diags);
	CHECK(diags.Empty());  // warnings too (ValidateTemplate: pattern typos, forms outside "requires")

	// Every template must round-trip stably and name a cell.
	for (const auto& t : templates) {
		INFO(t.source);
		CHECK(t.layout.cell.has_value());
		Diagnostics again;
		const auto  reparsed = ParseLayoutText(SerializeLayout(t.layout), t.source, again);
		REQUIRE(reparsed);
		CHECK(SerializeLayout(*reparsed) == SerializeLayout(t.layout));
	}

	// Cross-check bindings against the shipped categories: a binding no category matches fails the test.
	Diagnostics catDiags;
	const auto  categories = CategorySet::Compile(LoadCategoryDirectory(kDist / "Categories", catDiags), {}, catDiags);
	if (categories.Size() > 0) {
		Diagnostics lint;
		for (const auto& t : templates) {
			ValidateLayoutCategories(t.layout, categories, t.source, lint);
			// A display's categories it can't take (by the entry's `kind`; the rack type is not known here).
			Diagnostics displays;
			ValidateDisplayCategories(t.layout, categories, {}, t.source, displays);
			Report(displays);
			CHECK(displays.Empty());
		}
		Report(lint);
		CHECK(lint.Empty());
	}
}

TEST_CASE("Shipped categories: torches have one, every name tells its category apart", "[dist]")
{
	Diagnostics diags;
	const auto  set = CategorySet::Compile(LoadCategoryDirectory(kDist / "Categories", diags), {}, diags);
	REQUIRE(set.Size() > 0);
	CHECK(set.Classify(dova::test::MakeItem(dova::test::Sk(0x1D4EC), ItemType::Light)) == "misc.lights");  // Torch
	std::set<std::string> names;
	for (const auto& c : set.Categories()) {
		INFO(c.id << " " << c.name);
		CHECK(names.insert(c.name).second);  // "Books" and "Other books", never two of one name
	}
}
