#pragma once

#include <atomic>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <random>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "dova/core/Core.h"

namespace dova::test
{
	using namespace dova::core;

	inline FormRef F(std::string_view a_text)
	{
		auto ref = FormRef::Parse(a_text);
		REQUIRE(ref.has_value());
		return *ref;
	}

	inline FormRef Sk(std::uint32_t a_id) { return FormRef{ "Skyrim.esm", a_id }; }

	inline ItemInfo MakeItem(FormRef a_form, ItemType a_type, std::int32_t a_count = 1, std::uint32_t a_stackId = 0)
	{
		ItemInfo item;
		item.form = std::move(a_form);
		item.type = a_type;
		item.count = a_count;
		item.stackId = a_stackId;
		item.name = "Test item";  // nameless items are never moved (script tokens)
		return item;
	}

	inline ItemInfo Weapon(FormRef a_form, WeaponType a_type, std::int32_t a_count = 1)
	{
		auto item = MakeItem(std::move(a_form), ItemType::Weapon, a_count);
		item.weaponType = a_type;
		return item;
	}

	inline ItemInfo Potion(FormRef a_form, std::int32_t a_count, std::int32_t a_value, std::uint32_t a_stackId = 0)
	{
		auto item = MakeItem(std::move(a_form), ItemType::Alchemy, a_count, a_stackId);
		item.alchemyKind = AlchemyKind::Potion;
		item.value = a_value;
		return item;
	}

	inline ItemInfo Misc(FormRef a_form, std::int32_t a_count = 1, std::initializer_list<const char*> a_keywords = {})
	{
		auto item = MakeItem(std::move(a_form), ItemType::Misc, a_count);
		for (const auto* k : a_keywords) {
			item.keywords.emplace_back(k);
		}
		return item;
	}

	inline CategorySet CompileCategories(std::string_view a_json, Diagnostics& a_diags, const FormListResolver& a_resolver = {})
	{
		auto defs = ParseCategoryText(a_json, "test.json", a_diags);
		return CategorySet::Compile(std::move(defs), a_resolver, a_diags);
	}

	inline CategorySet CompileCategories(std::string_view a_json)
	{
		Diagnostics diags;
		auto        set = CompileCategories(a_json, diags);
		INFO((diags.Empty() ? std::string() : diags.Items().front().ToString()));
		REQUIRE_FALSE(diags.HasErrors());
		return set;
	}

	// A fresh, empty directory under the system temp dir; removed on destruction.
	class TempDir
	{
	public:
		TempDir()
		{
			static std::atomic<int> counter{ 0 };
			std::random_device      rd;
			_path = std::filesystem::temp_directory_path() / ("dovastash_test_" + std::to_string(rd()) + "_" + std::to_string(counter++));
			std::filesystem::create_directories(_path);
		}
		~TempDir()
		{
			std::error_code ec;
			std::filesystem::remove_all(_path, ec);
		}
		TempDir(const TempDir&) = delete;
		TempDir& operator=(const TempDir&) = delete;

		[[nodiscard]] const std::filesystem::path& Path() const { return _path; }

		void Write(const std::filesystem::path& a_relative, std::string_view a_text) const
		{
			const auto full = _path / a_relative;
			std::filesystem::create_directories(full.parent_path());
			std::ofstream out(full, std::ios::binary);
			out << a_text;
		}

	private:
		std::filesystem::path _path;
	};

	// Category set used by the keep-rule and planner tests.
	inline constexpr std::string_view kStandardCategories = R"({
		"$schema": "dovastash-categories-1",
		"categories": [
			{ "id": "weapons", "name": "Weapons" },
			{ "id": "weapons.swords", "match": { "formTypes": ["WEAP"], "weaponTypes": ["OneHandSword"] } },
			{ "id": "weapons.bows", "match": { "formTypes": "WEAP", "weaponTypes": "bow" } },
			{ "id": "weapons.daggers", "match": { "formTypes": ["WEAP"], "weaponTypes": ["OneHandDagger"] } },
			{ "id": "armor.heavy", "match": { "formTypes": ["ARMO"], "armorTypes": ["Heavy"] } },
			{ "id": "ammo.arrows", "match": { "formTypes": ["AMMO"], "ammo": ["Arrow"] } },
			{ "id": "alchemy.potions", "match": { "formTypes": ["ALCH"], "alchemy": ["Potion"] } },
			{ "id": "alchemy.poisons", "match": { "formTypes": ["ALCH"], "alchemy": ["Poison"] } },
			{ "id": "misc.gems", "priority": 100, "match": { "formTypes": ["MISC"], "keywordsAny": ["VendorItemGem"] } },
			{ "id": "misc.valuables", "priority": 50, "match": { "formTypes": ["MISC"], "keywordsAny": ["VendorItemGem", "VendorItemJewelry"] } },
			{ "id": "misc.clutter", "match": { "formTypes": ["MISC"] } },
			{ "id": "keys", "match": { "formTypes": ["KEYM"] } }
		]
	})";
}
