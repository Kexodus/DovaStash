// The JSON Schemas for people editing files by hand (tools/schema/*.schema.json): the shipped data validates
// against them, and they reject what core rejects. A small validator for the keywords the schemas use.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <vector>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include "TestHelpers.h"

#if !defined(DOVA_DIST_DIR) || !defined(DOVA_OPTIONAL_DIR) || !defined(DOVA_SCHEMA_DIR)
#	error "DOVA_DIST_DIR, DOVA_OPTIONAL_DIR and DOVA_SCHEMA_DIR must be defined by tests/CMakeLists.txt"
#endif

namespace
{
	using json = nlohmann::json;

	json ReadJson(const std::filesystem::path& a_path)
	{
		std::ifstream in(a_path, std::ios::binary);
		const std::string text(std::istreambuf_iterator<char>(in), {});
		return json::parse(text, nullptr, true, true);
	}

	// type, const, enum, pattern, minLength, minimum, properties, patternProperties, additionalProperties,
	// required, propertyNames, items, anyOf, oneOf and local $ref ("#/$defs/name"); the rest is documentation.
	class Validator
	{
	public:
		explicit Validator(json a_schema) :
			_root(std::move(a_schema))
		{}

		std::vector<std::string> Validate(const json& a_value)
		{
			std::vector<std::string> errors;
			Check(_root, a_value, "", errors);
			return errors;
		}

	private:
		static bool IsType(const json& a_value, const std::string& a_type)
		{
			if (a_type == "object") {
				return a_value.is_object();
			}
			if (a_type == "array") {
				return a_value.is_array();
			}
			if (a_type == "string") {
				return a_value.is_string();
			}
			if (a_type == "integer") {
				return a_value.is_number_integer();
			}
			if (a_type == "number") {
				return a_value.is_number();
			}
			if (a_type == "boolean") {
				return a_value.is_boolean();
			}
			return a_type == "null" && a_value.is_null();
		}

		const json& Resolve(const json& a_schema) const
		{
			const auto ref = a_schema.find("$ref");
			if (ref == a_schema.end()) {
				return a_schema;
			}
			const auto text = ref->get<std::string>();
			REQUIRE(text.starts_with("#/$defs/"));
			return _root.at("$defs").at(text.substr(8));
		}

		bool Matches(const json& a_schema, const json& a_value)
		{
			std::vector<std::string> ignored;
			Check(a_schema, a_value, "", ignored);
			return ignored.empty();
		}

		void Check(const json& a_schemaIn, const json& a_value, const std::string& a_where, std::vector<std::string>& a_errors)
		{
			const json& s = Resolve(a_schemaIn);
			const auto  fail = [&](const std::string& a_what) { a_errors.push_back((a_where.empty() ? "/" : a_where) + ": " + a_what); };
			if (const auto t = s.find("type"); t != s.end()) {
				bool ok = false;
				for (const auto& type : t->is_array() ? *t : json::array({ *t })) {
					ok = ok || IsType(a_value, type.get<std::string>());
				}
				if (!ok) {
					return fail("not of type " + t->dump());
				}
			}
			if (const auto c = s.find("const"); c != s.end() && *c != a_value) {
				fail("not " + c->dump());
			}
			if (const auto e = s.find("enum"); e != s.end() && std::ranges::find(*e, a_value) == e->end()) {
				fail(a_value.dump() + " not one of " + e->dump());
			}
			if (a_value.is_string()) {
				const auto text = a_value.get<std::string>();
				if (const auto p = s.find("pattern"); p != s.end() && !std::regex_search(text, std::regex(p->get<std::string>(), std::regex::ECMAScript))) {
					fail("\"" + text + "\" does not match " + p->get<std::string>());
				}
				if (const auto m = s.find("minLength"); m != s.end() && text.size() < m->get<std::size_t>()) {
					fail("too short");
				}
			}
			if (const auto m = s.find("minimum"); m != s.end() && a_value.is_number() && a_value.get<double>() < m->get<double>()) {
				fail("below " + m->dump());
			}
			if (const auto any = s.find("anyOf"); any != s.end() && std::ranges::none_of(*any, [&](const json& a_s) { return Matches(a_s, a_value); })) {
				fail("matches none of anyOf");
			}
			if (const auto one = s.find("oneOf"); one != s.end() && std::ranges::count_if(*one, [&](const json& a_s) { return Matches(a_s, a_value); }) != 1) {
				fail("does not match exactly one of oneOf");
			}
			if (a_value.is_object()) {
				if (const auto r = s.find("required"); r != s.end()) {
					for (const auto& key : *r) {
						if (!a_value.contains(key.get<std::string>())) {
							fail("missing \"" + key.get<std::string>() + "\"");
						}
					}
				}
				const auto props = s.find("properties");
				const auto patterns = s.find("patternProperties");
				const auto additional = s.find("additionalProperties");
				const auto names = s.find("propertyNames");
				for (const auto& [key, value] : a_value.items()) {
					const auto where = a_where + "/" + key;
					if (names != s.end()) {
						Check(*names, json(key), where + " (name)", a_errors);
					}
					bool known = false;
					if (props != s.end() && props->contains(key)) {
						known = true;
						Check(props->at(key), value, where, a_errors);
					}
					if (patterns != s.end()) {
						for (const auto& [pattern, sub] : patterns->items()) {
							if (std::regex_search(key, std::regex(pattern, std::regex::ECMAScript))) {
								known = true;
								Check(sub, value, where, a_errors);
							}
						}
					}
					if (!known && additional != s.end()) {
						if (additional->is_boolean()) {
							if (!additional->get<bool>()) {
								a_errors.push_back(where + ": unknown key");
							}
						} else {
							Check(*additional, value, where, a_errors);
						}
					}
				}
			}
			if (a_value.is_array()) {
				if (const auto items = s.find("items"); items != s.end()) {
					for (std::size_t i = 0; i < a_value.size(); ++i) {
						Check(*items, a_value[i], a_where + "/" + std::to_string(i), a_errors);
					}
				}
			}
		}

		json _root;
	};

	const std::filesystem::path kSchemas{ DOVA_SCHEMA_DIR };

	void ValidateAll(Validator& a_validator, const std::filesystem::path& a_dir, std::size_t& a_count)
	{
		for (const auto& entry : std::filesystem::recursive_directory_iterator(a_dir)) {
			if (!entry.is_regular_file() || entry.path().extension() != ".json") {
				continue;
			}
			INFO(entry.path().string());
			const auto errors = a_validator.Validate(ReadJson(entry.path()));
			for (const auto& e : errors) {
				UNSCOPED_INFO(e);
			}
			CHECK(errors.empty());
			++a_count;
		}
	}
}

TEST_CASE("Shipped templates validate against the layout schema", "[dist][schema]")
{
	Validator   v(ReadJson(kSchemas / "dovastash-layout-1.schema.json"));
	std::size_t count = 0;
	ValidateAll(v, std::filesystem::path(DOVA_DIST_DIR) / "Templates", count);
	CHECK(count > 20);
}

TEST_CASE("Shipped categories validate against the categories schema", "[dist][schema]")
{
	Validator   v(ReadJson(kSchemas / "dovastash-categories-1.schema.json"));
	std::size_t count = 0;
	ValidateAll(v, std::filesystem::path(DOVA_DIST_DIR) / "Categories", count);
	ValidateAll(v, DOVA_OPTIONAL_DIR, count);
	CHECK(count >= 2);
}

TEST_CASE("The layout schema rejects what core rejects or warns about", "[schema]")
{
	Validator  v(ReadJson(kSchemas / "dovastash-layout-1.schema.json"));
	const auto bad = [&](const char* a_text) { return !v.Validate(json::parse(a_text)).empty(); };

	CHECK_FALSE(bad(R"({"$schema": "dovastash-layout-1", "cell": "Skyrim.esm|0x000165A7", "containers": [{"ref": "Skyrim.esm|0x2", "categories": "weapons.*"}],
		"keep": {"quotas": [{"category": "alchemy.potions", "quota": 5, "restock": true}]}})"));
	CHECK(bad(R"({"$schema": "dovastash-layout-1"})"));                                              // no cell
	CHECK(bad(R"({"cell": "Skyrim|0x1"})"));                                                         // not a plugin file
	CHECK(bad(R"({"cell": "Skyrim.esm|0x123456789"})"));                                            // more than 8 hex digits
	CHECK(bad(R"({"cell": "Skyrim.esm|0x1", "contianers": []})"));                                  // typo: unknown key
	CHECK(bad(R"({"cell": "Skyrim.esm|0x1", "containers": [{"ref": "Skyrim.esm|0x2", "categories": ["weapons.**"]}]})"));
	CHECK(bad(R"({"cell": "Skyrim.esm|0x1", "keep": {"quotas": [{"category": "ammo.*", "quota": -1}]}})"));
	CHECK(bad(R"({"cell": "Skyrim.esm|0x1", "keep": {"quotas": [{"quota": 3}]}})"));               // nothing to count
	CHECK(bad(R"({"cell": "Skyrim.esm|0x1", "keep": {"quotas": [{"category": "ammo.*", "quota": 3, "restok": true}]}})"));
	CHECK(bad(R"({"cell": "Skyrim.esm|0x1", "displays": [{"ref": "Skyrim.esm|0x3", "kind": "shelf"}]})"));
}

TEST_CASE("The categories schema lists the enum values", "[schema]")
{
	Validator  v(ReadJson(kSchemas / "dovastash-categories-1.schema.json"));
	const auto bad = [&](const char* a_text) { return !v.Validate(json::parse(a_text)).empty(); };

	CHECK_FALSE(bad(R"({"$schema": "dovastash-categories-1", "categories": [{"id": "weapons.swords", "match": {"formTypes": "WEAP", "weaponTypes": ["OneHandSword"]}}]})"));
	CHECK(bad(R"({"categories": [{"id": "weapons.swords", "match": {"weaponTypes": "Sword"}}]})"));
	CHECK(bad(R"({"categories": [{"id": "x", "match": {"keywordAny": ["A"]}}]})"));
	CHECK(bad(R"({"categories": [{"id": "x", "match": {"formTypes": ["WEAPON"]}}]})"));
}
