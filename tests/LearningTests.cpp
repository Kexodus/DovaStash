// Learning.h: category suggestions after a few learns, declined suggestions, pruning (DESIGN §8.6).

#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"

using namespace dova::core;
using namespace dova::test;

namespace
{
	const FormRef kMaster = Sk(0xA000);
	const FormRef kArmory = Sk(0xB000);    // weapons.*
	const FormRef kBowRack = Sk(0xB001);   // weapons.bows
	const FormRef kEndTable = Sk(0xF000);  // no categories (learned items only)
	const FormRef kBarrel = Sk(0xD000);    // ignored

	Layout MakeLayout()
	{
		Layout l;
		l.cell = Sk(0x165A7);
		l.master = kMaster;
		l.containers.push_back({ .ref = kArmory, .label = "Armory", .categories = { "weapons.*" } });
		l.containers.push_back({ .ref = kBowRack, .label = "Bows", .categories = { "weapons.bows" } });
		l.containers.push_back({ .ref = kEndTable, .label = "End Table" });
		l.containers.push_back({ .ref = kBarrel, .label = "Barrel", .ignored = true });
		return l;
	}

	// The learned items of the fixture: three potions and one dagger, all learned for the end table.
	struct Fixture
	{
		CategorySet                     categories = CompileCategories(kStandardCategories);
		Layout                          layout = MakeLayout();
		std::map<FormRef, ItemInfo>     items;

		Fixture()
		{
			Add(Potion(Sk(0x1), 1, 10));
			Add(Potion(Sk(0x2), 1, 10));
			Add(Potion(Sk(0x3), 1, 10));
			Add(Weapon(Sk(0x4), WeaponType::OneHandDagger));
		}

		void Add(ItemInfo a_item)
		{
			layout.learned.emplace(a_item.form, kEndTable);
			items.emplace(a_item.form, std::move(a_item));
		}

		[[nodiscard]] LearnedCategories Learned(const FormRef& a_container) const
		{
			std::map<std::string, std::int32_t> counts;
			for (const auto& [form, container] : layout.learned) {
				if (container == a_container) {
					if (const auto c = categories.Classify(items.at(form))) {
						++counts[*c];
					}
				}
			}
			return SortLearnedCategories(counts);
		}

		[[nodiscard]] ItemLookup Lookup() const
		{
			return [this](const FormRef& a_form) -> std::optional<ItemInfo> {
				const auto it = items.find(a_form);
				return it == items.end() ? std::nullopt : std::optional(it->second);
			};
		}
	};
}

TEST_CASE("SortLearnedCategories: most items first, ties in id order", "[learning]")
{
	const auto sorted = SortLearnedCategories({ { "books.skill", 2 }, { "alchemy.potions", 5 }, { "alchemy.poisons", 2 } });
	REQUIRE(sorted.size() == 3);
	CHECK(sorted[0].first == "alchemy.potions");
	CHECK(sorted[1].first == "alchemy.poisons");
	CHECK(sorted[2].first == "books.skill");
}

TEST_CASE("SuggestCategory suggests the most learned category once the threshold is reached", "[learning]")
{
	Fixture    f;
	const auto learned = f.Learned(kEndTable);
	REQUIRE(learned.size() == 2);
	CHECK(learned[0] == std::pair<std::string, std::int32_t>{ "alchemy.potions", 3 });

	auto s = SuggestCategory(f.layout, kEndTable, learned, 3);
	REQUIRE(s);
	CHECK(s->container == kEndTable);
	CHECK(s->category == "alchemy.potions");
	CHECK(s->count == 3);

	// Threshold not reached, or suggestions off.
	CHECK_FALSE(SuggestCategory(f.layout, kEndTable, learned, 4));
	CHECK_FALSE(SuggestCategory(f.layout, kEndTable, learned, 0));

	// The master, an ignored container and one not in the layout get no suggestion.
	CHECK_FALSE(SuggestCategory(f.layout, kMaster, learned, 1));
	CHECK_FALSE(SuggestCategory(f.layout, kBarrel, learned, 1));
	CHECK_FALSE(SuggestCategory(f.layout, Sk(0x9999), learned, 1));
}

TEST_CASE("SuggestCategory skips covered, declined and exactly-bound-elsewhere categories", "[learning]")
{
	Fixture    f;
	const auto learned = f.Learned(kEndTable);

	// Already covered by one of the container's own patterns: nothing to suggest for potions; the dagger
	// (1 item) is below the threshold.
	auto copy = f.layout;
	copy.FindContainer(kEndTable)->categories = { "alchemy.*" };
	CHECK_FALSE(SuggestCategory(copy, kEndTable, learned, 3));
	CHECK(SuggestCategory(copy, kEndTable, learned, 1)->category == "weapons.daggers");

	// Declined: never asked again, so the next category in line is suggested (dagger at threshold 1).
	copy = f.layout;
	CHECK(DeclineSuggestion(copy, kEndTable, "alchemy.potions"));
	CHECK_FALSE(DeclineSuggestion(copy, kEndTable, "ALCHEMY.POTIONS"));  // case-insensitive, already there
	CHECK(IsDeclined(copy, kEndTable, "alchemy.potions"));
	CHECK_FALSE(SuggestCategory(copy, kEndTable, learned, 3));
	CHECK(SuggestCategory(copy, kEndTable, learned, 1)->category == "weapons.daggers");
	CHECK(ClearDeclined(copy, kEndTable, "alchemy.potions") == 1);
	CHECK(copy.declined.empty());
	CHECK(SuggestCategory(copy, kEndTable, learned, 3)->category == "alchemy.potions");

	// The dagger: the armory binds weapons.* (a wider pattern): still suggested, the exact id wins there.
	CHECK(SuggestCategory(f.layout, kEndTable, learned, 1)->category == "alchemy.potions");
	copy = f.layout;
	const LearnedCategories daggerOnly{ { "weapons.daggers", 1 } };
	CHECK(SuggestCategory(copy, kEndTable, daggerOnly, 1)->category == "weapons.daggers");
	// ... but not when another container binds weapons.daggers exactly.
	copy.FindContainer(kBowRack)->categories.push_back("weapons.daggers");
	CHECK_FALSE(SuggestCategory(copy, kEndTable, daggerOnly, 1));
	// An ignored container's exact binding does not count.
	copy.FindContainer(kBowRack)->ignored = true;
	CHECK(SuggestCategory(copy, kEndTable, daggerOnly, 1)->category == "weapons.daggers");
}

TEST_CASE("AcceptSuggestion binds the category and prunes the learned entries it covers", "[learning]")
{
	Fixture f;
	DeclineSuggestion(f.layout, kEndTable, "alchemy.potions");  // a No that is overruled by the Yes

	const auto pruned = AcceptSuggestion(f.layout, kEndTable, "alchemy.potions", f.categories, f.Lookup());
	REQUIRE(pruned);
	CHECK(*pruned == 3);
	const auto* table = f.layout.FindContainer(kEndTable);
	REQUIRE(table);
	CHECK(table->categories == std::vector<std::string>{ "alchemy.potions" });
	CHECK(f.layout.learned.size() == 1);  // the dagger stays learned
	CHECK(f.layout.learned.contains(Sk(0x4)));
	CHECK(f.layout.declined.empty());

	// The potions still go to the end table, now by category.
	const auto dest = SortPlanner(f.layout, f.categories).ResolveDestination(f.items.at(Sk(0x1)));
	REQUIRE(dest);
	CHECK(dest->container == kEndTable);
	CHECK(dest->reason == Destination::Reason::Category);

	// Accepting again changes nothing (the pattern is not added twice).
	CHECK(AcceptSuggestion(f.layout, kEndTable, "alchemy.potions", f.categories, f.Lookup()) == 0);
	CHECK(table->categories.size() == 1);

	// Refused for the master, an ignored container, an unknown container.
	CHECK_FALSE(AcceptSuggestion(f.layout, kMaster, "alchemy.potions", f.categories, f.Lookup()));
	CHECK_FALSE(AcceptSuggestion(f.layout, kBarrel, "alchemy.potions", f.categories, f.Lookup()));
	CHECK_FALSE(AcceptSuggestion(f.layout, Sk(0x9999), "alchemy.potions", f.categories, f.Lookup()));
}

TEST_CASE("PruneRedundantLearned keeps what the lookup cannot resolve and what still needs the entry", "[learning]")
{
	Fixture f;
	f.layout.learned.emplace(Sk(0x5), kEndTable);  // no item data: a form of a missing plugin
	f.layout.FindContainer(kEndTable)->categories = { "alchemy.potions" };

	CHECK(PruneRedundantLearned(f.layout, kEndTable, f.categories, f.Lookup()) == 3);
	CHECK(f.layout.learned.size() == 2);
	CHECK(f.layout.learned.contains(Sk(0x4)));  // the dagger would go to the armory without it
	CHECK(f.layout.learned.contains(Sk(0x5)));  // unresolvable: kept

	// Entries for other containers are never touched.
	f.layout.learned.emplace(Sk(0x6), kArmory);
	CHECK(PruneRedundantLearned(f.layout, kEndTable, f.categories, f.Lookup()) == 0);
	CHECK(f.layout.learned.contains(Sk(0x6)));
}
