#pragma once

// String <-> enum conversion for the Item.h enums as spelled in JSON (CLAUDE.md "Core <-> plugin
// contract"): enumerator names, parsed case-insensitively ("bookTeaches": "skill" == "Skill").
// ItemType uses record signatures ("WEAP", "ARMO", ...); ItemType::Other has no signature and
// cannot be named in JSON.

#include <optional>
#include <string_view>

#include "dova/core/Item.h"

namespace dova::core
{
	[[nodiscard]] std::optional<ItemType>     ParseItemType(std::string_view a_signature);
	[[nodiscard]] std::optional<WeaponType>   ParseWeaponType(std::string_view a_name);
	[[nodiscard]] std::optional<ArmorType>    ParseArmorType(std::string_view a_name);
	[[nodiscard]] std::optional<BookTeaches>  ParseBookTeaches(std::string_view a_name);
	[[nodiscard]] std::optional<AlchemyKind>  ParseAlchemyKind(std::string_view a_name);
	[[nodiscard]] std::optional<SoulGemState> ParseSoulGemState(std::string_view a_name);
	[[nodiscard]] std::optional<AmmoKind>     ParseAmmoKind(std::string_view a_name);

	[[nodiscard]] std::string_view ToString(ItemType a_value) noexcept;  // "WEAP", ... ("" for Other)
	[[nodiscard]] std::string_view ToString(WeaponType a_value) noexcept;
	[[nodiscard]] std::string_view ToString(ArmorType a_value) noexcept;
	[[nodiscard]] std::string_view ToString(BookTeaches a_value) noexcept;
	[[nodiscard]] std::string_view ToString(AlchemyKind a_value) noexcept;
	[[nodiscard]] std::string_view ToString(SoulGemState a_value) noexcept;
	[[nodiscard]] std::string_view ToString(AmmoKind a_value) noexcept;
}
