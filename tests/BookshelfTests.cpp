// Bookshelves as displays (DESIGN §8.4, core/Display.h): the room a shelf's script leaves, which books go up,
// and the layout / template side of `kind: "bookshelf"`.

#include <algorithm>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"

using namespace dova::core;
using namespace dova::test;

namespace
{
	ItemInfo Book(std::uint32_t a_id, std::int32_t a_value, BookTeaches a_teaches = BookTeaches::None)
	{
		auto item = MakeItem(Sk(a_id), ItemType::Book);
		item.value = a_value;
		item.bookTeaches = a_teaches;
		return item;
	}

	ItemInfo Note(std::uint32_t a_id, std::int32_t a_value)
	{
		auto item = Book(a_id, a_value);
		item.bookIsNote = true;
		return item;
	}

	DisplayStock OnPlayer(ItemInfo a_item)
	{
		const auto n = a_item.count;
		return DisplayStock{ ContainerRef::Player(), std::move(a_item), n };
	}

	DisplayStock InChest(ItemInfo a_item)
	{
		const auto n = a_item.count;
		return DisplayStock{ ContainerRef(Sk(0xC0)), std::move(a_item), n };
	}

	DisplaySpot Shelf(std::uint32_t a_id, std::int32_t a_room, std::vector<FormRef> a_items = {}, bool a_only = false)
	{
		DisplaySpot spot;
		spot.ref = Sk(a_id);
		spot.kind = DisplayKind::Bookshelf;
		spot.room = a_room;
		spot.full = a_room <= 0;
		spot.items = std::move(a_items);
		spot.only = a_only;
		return spot;
	}

	// The forms each shelf got, in the order they were assigned.
	std::vector<FormRef> On(const std::vector<DisplayAssignment>& a_plan, std::span<const DisplayStock> a_stocks, std::size_t a_spot)
	{
		std::vector<FormRef> out;
		for (const auto& a : a_plan) {
			if (a.spot == a_spot) {
				out.push_back(a_stocks[a.stock].item.form);
			}
		}
		return out;
	}

	std::size_t CountContaining(const Diagnostics& a_diags, std::string_view a_text)
	{
		std::size_t n = 0;
		for (const auto& d : a_diags.Items()) {
			n += d.message.find(a_text) != std::string::npos ? 1u : 0u;
		}
		return n;
	}
}

TEST_CASE("Bookshelf room follows its script's own limits", "[display][bookshelf]")
{
	// An empty vanilla shelf with 18 markers, as its script counted them on its first cell load.
	BookshelfState s{ .capacity = 18, .markers = 18, .counted = 0, .slots = std::vector<bool>(18, false), .inside = 0 };
	CHECK(BookshelfRoom(s) == 18);

	// Five books on it: 13 more fit.
	for (int i = 0; i < 5; ++i) {
		s.slots[static_cast<std::size_t>(i)] = true;
	}
	s.counted = 5;
	s.inside = 5;
	CHECK(BookshelfRoom(s) == 13);

	// Hearthfire's alchemy-lab shelves have 11 markers.
	CHECK(BookshelfRoom({ .capacity = 11, .markers = 11, .counted = 0, .slots = std::vector<bool>(18, false), .inside = 0 }) == 11);

	// The script's count wins when it is higher than what the slots show (it would hand the extra books back)...
	s.counted = 15;
	CHECK(BookshelfRoom(s) == 3);
	// ...and the empty slots when they are fewer (a count that drifted low: the books would go on slots past
	// its markers, where nothing shows them).
	s.counted = 0;
	s.slots = std::vector<bool>(18, true);
	s.slots[17] = false;
	CHECK(BookshelfRoom(s) == 1);
	s.slots[3] = false;
	CHECK(BookshelfRoom(s) == 2);
	// So do the books inside the container.
	s.slots = std::vector<bool>(18, false);
	s.inside = 16;
	CHECK(BookshelfRoom(s) == 2);

	// Slots past the capacity don't count; slots the script lacks count as empty.
	CHECK(BookshelfRoom({ .capacity = 4, .markers = -1, .counted = 0, .slots = { false, true }, .inside = 0 }) == 3);
	CHECK(BookshelfRoom({ .capacity = 4, .markers = -1, .counted = 0, .slots = {}, .inside = 0 }) == 4);

	// Never more than the markers linked to it, never less than nothing.
	CHECK(BookshelfRoom({ .capacity = 18, .markers = 6, .counted = 0, .slots = {}, .inside = 0 }) == 6);
	CHECK(BookshelfRoom({ .capacity = 0, .markers = 18, .counted = 0, .slots = {}, .inside = 0 }) == 0);  // its cell never loaded yet
	CHECK(BookshelfRoom({ .capacity = 10, .markers = 10, .counted = 12, .slots = {}, .inside = 0 }) == 0);
	CHECK(BookshelfRoom({ .capacity = 10, .markers = 10, .counted = -3, .slots = {}, .inside = 0 }) == 10);  // a count gone negative
}

TEST_CASE("Bookshelves take books only, never Elder Scrolls or customized copies", "[display][bookshelf]")
{
	CHECK(BookshelfTakes(Book(1, 10)));
	CHECK(BookshelfTakes(Note(2, 10)));  // notes are books; a shelf without categories leaves them out
	auto scroll = Book(3, 0);
	scroll.elderScroll = true;
	CHECK_FALSE(BookshelfTakes(scroll));
	auto owned = Book(4, 10);
	owned.customized = true;
	CHECK_FALSE(BookshelfTakes(owned));
	CHECK_FALSE(BookshelfTakes(Weapon(Sk(5), WeaponType::OneHandSword)));
	CHECK_FALSE(BookshelfTakes(MakeItem(Sk(6), ItemType::Scroll)));
}

TEST_CASE("A bookshelf takes the books most worth showing, up to its room", "[display][bookshelf]")
{
	auto scroll = Book(0x16, 0);
	scroll.elderScroll = true;
	auto quest = Book(0x17, 900);
	quest.questItem = true;
	const std::vector<DisplayStock> stocks{
		OnPlayer(Book(0x10, 100)),                       // 0 a plain book
		OnPlayer(Book(0x11, 20)),                        // 1 a cheap one
		OnPlayer(Book(0x12, 50, BookTeaches::Skill)),    // 2 a skill book
		OnPlayer(Book(0x13, 300, BookTeaches::Spell)),   // 3 a spell tome
		OnPlayer(Note(0x14, 500)),                       // 4 a note: not without categories
		OnPlayer(Weapon(Sk(0x15), WeaponType::OneHandSword)),  // 5 not a book
		OnPlayer(scroll),                                // 6 an Elder Scroll
		OnPlayer(quest),                                 // 7 a quest item never moves
	};
	const auto plan = PlanDisplays(std::vector{ Shelf(0xA1, 3) }, stocks, {});
	CHECK(On(plan, stocks, 0) == std::vector<FormRef>{ Sk(0x12), Sk(0x13), Sk(0x10) });  // skill, spell, then value

	// Room for everything: the cheap book goes up too, still nothing else.
	const auto all = PlanDisplays(std::vector{ Shelf(0xA1, 18) }, stocks, {});
	CHECK(On(all, stocks, 0) == std::vector<FormRef>{ Sk(0x12), Sk(0x13), Sk(0x10), Sk(0x11) });

	// No room, or full: nothing.
	CHECK(PlanDisplays(std::vector{ Shelf(0xA1, 0) }, stocks, {}).empty());
	auto oddFull = Shelf(0xA1, 5);
	oddFull.full = true;
	CHECK(PlanDisplays(std::vector{ oddFull }, stocks, {}).empty());
}

TEST_CASE("Bookshelf categories pick the kind of books, notes included", "[display][bookshelf]")
{
	std::vector<DisplayStock> stocks{
		OnPlayer(Book(0x10, 100)),
		OnPlayer(Book(0x12, 50, BookTeaches::Skill)),
		OnPlayer(Note(0x14, 500)),
	};
	stocks[0].categories = { "books.other" };
	stocks[1].categories = { "books.skill", "books.other" };
	stocks[2].categories = { "books.notes", "books.other" };

	auto skill = Shelf(0xA1, 5);
	skill.categories = { "books.skill" };
	auto notes = Shelf(0xA2, 5);
	notes.categories = { "books.notes" };
	const std::vector<DisplaySpot> spots{ skill, notes };
	const auto                     plan = PlanDisplays(spots, stocks, {});
	CHECK(On(plan, stocks, 0) == std::vector<FormRef>{ Sk(0x12) });
	CHECK(On(plan, stocks, 1) == std::vector<FormRef>{ Sk(0x14) });  // chosen by its categories: notes go up

	auto everything = Shelf(0xA3, 5);
	everything.categories = { "books.*" };
	CHECK(On(PlanDisplays(std::vector{ everything }, stocks, {}), stocks, 0) == std::vector<FormRef>{ Sk(0x12), Sk(0x14), Sk(0x10) });
}

TEST_CASE("Bookshelves leave items made for plaques and cases, and show each title once", "[display][bookshelf]")
{
	const std::vector<DisplayStock> stocks{
		OnPlayer(Book(0x10, 1000)),  // 0 made for the item plaque
		OnPlayer(Book(0x11, 100)),   // 1
		InChest(Book(0x11, 100)),    // 2 another copy of the same book
		InChest(Book(0x12, 90)),     // 3 already on a shelf in the home
		InChest(Book(0x13, 80)),     // 4
	};
	DisplaySpot plaque;
	plaque.ref = Sk(0xB1);
	plaque.kind = DisplayKind::Item;
	plaque.items = { Sk(0x10) };
	const std::vector<DisplaySpot> spots{ Shelf(0xA1, 1), Shelf(0xA2, 5), plaque };
	const auto                     plan = PlanDisplays(spots, stocks, { Sk(0x12) });
	CHECK(On(plan, stocks, 2) == std::vector<FormRef>{ Sk(0x10) });  // the plaque's book waits for the plaque
	CHECK(On(plan, stocks, 0) == std::vector<FormRef>{ Sk(0x11) });
	CHECK(On(plan, stocks, 1) == std::vector<FormRef>{ Sk(0x13) });  // no second copy, nothing already shown
	// The player's copy goes up before the stored one.
	REQUIRE(plan.size() == 3);
	CHECK(std::ranges::any_of(plan, [](const DisplayAssignment& a) { return a.spot == 0 && a.stock == 1; }));

	// A case made for a book keeps it too.
	DisplaySpot bookCase;
	bookCase.ref = Sk(0xB2);
	bookCase.kind = DisplayKind::Case;
	bookCase.items = { Sk(0x13) };
	const auto withCase = PlanDisplays(std::vector{ Shelf(0xA1, 5), bookCase }, stocks, {});
	CHECK(On(withCase, stocks, 1) == std::vector<FormRef>{ Sk(0x13) });
	CHECK(On(withCase, stocks, 0) == std::vector<FormRef>{ Sk(0x10), Sk(0x11), Sk(0x12) });
}

TEST_CASE("Bookshelf items: each shelf gets its own before another fills up; only keeps them", "[display][bookshelf]")
{
	const std::vector<DisplayStock> stocks{
		OnPlayer(Book(0x10, 500)),  // 0
		OnPlayer(Book(0x11, 400)),  // 1
		OnPlayer(Book(0x12, 5)),    // 2 the second shelf's favourite, worth little
		OnPlayer(Book(0x13, 300)),  // 3
	};
	// The first shelf (no items) would take the three most valuable; the second still gets its own book.
	const std::vector<DisplaySpot> spots{ Shelf(0xA1, 3), Shelf(0xA2, 2, { Sk(0x12) }) };
	const auto                     plan = PlanDisplays(spots, stocks, {});
	CHECK(On(plan, stocks, 0) == std::vector<FormRef>{ Sk(0x10), Sk(0x11), Sk(0x13) });
	CHECK(On(plan, stocks, 1) == std::vector<FormRef>{ Sk(0x12) });

	// Preferred first even on its own shelf, then the rest by worth.
	const auto alone = PlanDisplays(std::vector{ Shelf(0xA2, 2, { Sk(0x12) }) }, stocks, {});
	CHECK(On(alone, stocks, 0) == std::vector<FormRef>{ Sk(0x12), Sk(0x10) });

	// "only": nothing but its items, and no other shelf takes them.
	const std::vector<DisplaySpot> onlySpots{ Shelf(0xA1, 5), Shelf(0xA2, 5, { Sk(0x10), Sk(0x12) }, true) };
	const auto                     onlyPlan = PlanDisplays(onlySpots, stocks, {});
	CHECK(On(onlyPlan, stocks, 0) == std::vector<FormRef>{ Sk(0x11), Sk(0x13) });
	CHECK(On(onlyPlan, stocks, 1) == std::vector<FormRef>{ Sk(0x10), Sk(0x12) });

	// An "only" shelf whose items are not here waits.
	CHECK(PlanDisplays(std::vector{ Shelf(0xA2, 5, { Sk(0x99) }, true) }, stocks, {}).empty());

	// Its room still counts with preferred items.
	CHECK(On(PlanDisplays(std::vector{ Shelf(0xA2, 1, { Sk(0x12), Sk(0x13) }) }, stocks, {}), stocks, 0) == std::vector<FormRef>{ Sk(0x13) });

	// Racks and mannequins never take books, bookshelves never take weapons.
	DisplaySpot rack;
	rack.ref = Sk(0xB1);
	rack.kind = DisplayKind::Rack;
	const std::vector<DisplayStock> mixed{ OnPlayer(Weapon(Sk(0x20), WeaponType::OneHandSword)), OnPlayer(Book(0x21, 10)) };
	const auto                      both = PlanDisplays(std::vector{ rack, Shelf(0xA1, 5) }, mixed, {});
	CHECK(On(both, mixed, 0) == std::vector<FormRef>{ Sk(0x20) });
	CHECK(On(both, mixed, 1) == std::vector<FormRef>{ Sk(0x21) });
}

TEST_CASE("Bookshelf entries in layouts and templates", "[display][bookshelf][layout]")
{
	CHECK(DisplayKindName(DisplayKind::Bookshelf) == "bookshelf");
	CHECK(ParseDisplayKind("Bookshelf") == DisplayKind::Bookshelf);
	CHECK(ParseDisplayKind("BOOKSHELF") == DisplayKind::Bookshelf);
	CHECK(ItemsChosenByFile(DisplayKind::Bookshelf));

	constexpr std::string_view kText = R"({
		"$schema": "dovastash-layout-1",
		"cell": "Skyrim.esm|0x000165A8",
		"containers": [ { "ref": "Skyrim.esm|0x00102A59", "label": "Book Shelf", "ignored": true } ],
		"displays": [
			{ "ref": "Skyrim.esm|0x00102A59", "kind": "bookshelf", "label": "Living room shelf", "categories": ["books.skill"], "items": ["Books.esp|0x00000801"], "only": true }
		]
	})";
	Diagnostics diags;
	const auto  layout = ParseLayoutText(kText, "t.json", diags);
	REQUIRE(layout);
	CHECK(diags.Empty());
	REQUIRE(layout->displays.size() == 1);
	const auto& d = layout->displays[0];
	CHECK(d.kind == DisplayKind::Bookshelf);
	CHECK(d.categories == std::vector<std::string>{ "books.skill" });
	CHECK(d.only);
	const auto text = SerializeLayout(*layout);
	CHECK(text.find("\"kind\": \"bookshelf\"") != std::string::npos);
	Diagnostics again;
	CHECK(SerializeLayout(*ParseLayoutText(text, "t.json", again)) == text);

	// Lint: categories, items and only are fine on a bookshelf (an ignored container entry for it too); set is not.
	Diagnostics lint;
	ValidateDisplays(*layout, "t.json", lint);
	CHECK(lint.Empty());
	Layout withSet = *layout;
	withSet.displays[0].set = Sk(0x99);
	Diagnostics setLint;
	ValidateDisplays(withSet, "t.json", setLint);
	CHECK(CountContaining(setLint, "\"set\" is only used by mannequins") == 1);
	// A shelf the layout stores items in is storage, never filled.
	Layout storage = *layout;
	storage.containers[0].ignored = false;
	Diagnostics storageLint;
	ValidateDisplays(storage, "t.json", storageLint);
	CHECK(CountContaining(storageLint, "also listed as a container") == 1);

	// A template's books are soft references: one that doesn't exist is reported (once, by the plugin), never required.
	const auto kindOf = [](const FormRef&) -> std::optional<DisplayKind> { return DisplayKind::Bookshelf; };
	const auto missing = MissingTemplateItems(*layout, *layout, kindOf, [](const FormRef&) { return false; });
	REQUIRE(missing.size() == 1);
	CHECK(missing[0].items == std::vector<FormRef>{ F("Books.esp|0x801") });
	CHECK(missing[0].waits);  // "only" and none of its books exist
	CHECK(std::ranges::none_of(ReferencedPlugins(*layout), [](const std::string& p) { return IEquals(p, "Books.esp"); }));
}
