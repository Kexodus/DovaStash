#pragma once

// Display kinds (DESIGN §8.4), shared by the layout schema (Layout.h) and the display planner (Display.h).

#include <cstdint>
#include <optional>
#include <string_view>

namespace dova::core
{
	enum class DisplayKind : std::uint8_t
	{
		Rack,       // vanilla weapon rack, shield plaque or dagger display (WeaponRackActivateSCRIPT)
		Mannequin,  // an actor that wears the armor put into its inventory (MannequinActivatorSCRIPT)
		Item,       // a plaque made for one item: activating it with the item takes it and shows a model
		Case        // a scripted container that shows models of the items it is made for
	};

	// Which items a vanilla rack takes (WeaponRackActivateSCRIPT.RackType; Patch14COARacks counts as 3).
	enum class RackType : std::uint8_t
	{
		Any = 1,            // every weapon except crossbows (and shields)
		Shield = 2,         // shields only
		NoDaggerOrBow = 3,  // no daggers, bows or crossbows (coat-of-arms racks)
		Dagger = 4          // daggers only
	};

	[[nodiscard]] std::string_view           DisplayKindName(DisplayKind a_kind) noexcept;  // "rack", "mannequin", "item", "case"
	[[nodiscard]] std::optional<DisplayKind> ParseDisplayKind(std::string_view a_text);      // case-insensitive
}
