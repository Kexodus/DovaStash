#pragma once

// Item / container icons borrowed from the player's own UI (DESIGN §1 principle 6, §10.3).
//
// Dova Stash ships no art. An icon is a (SWF, frame label, colour) triple pointing into an SWF the
// player already has: SkyUI's item/category icon SWFs, or whatever an I4 rule file names.
//
// Two layers, both engine-independent:
//
// 1. SkyUIDefaultIcon(): a port of what SkyUI 5/6 does for an inventory entry. SKSE's scaleform
//    extension fills the entry (formType, partMask, weightClass, flags, gemSize...), SkyUI's
//    InventoryDataSetter derives subType / weightClass / status from it, and InventoryIconSetter
//    picks iconLabel + iconColor from those. Sources (schlangster/skyui, master):
//      src/ItemMenus/InventoryDataSetter.as, src/ItemMenus/InventoryIconSetter.as,
//      src/Common/skyui/defines/{Armor,Item,Weapon,Form,Actor}.as
//    plus the note fix-up Inventory Injector applies before SkyUI's icon pass
//    (Exit-9B/InventoryInjector src/Hooks/IconSetter.cpp, FixNote).
//
// 2. IconRules: Inventory Interface Information Injector (I4, Exit-9B/InventoryInjector, main) rule
//    files, evaluated the way I4 evaluates them (src/Data/CustomDataManager.cpp, Config/Rule.cpp,
//    Config/RuleParser.cpp, Config/PropertyParser.cpp, Config/Property.cpp, Config/CustomDataParser.cpp):
//    - Files are loaded in plugin load order: for every active plugin "X.esp", I4 reads
//      "SKSE/Plugins/InventoryInjector/X.json" (the plugin service does the file discovery; call
//      AddFile in that order). Rules keep file order, then array order.
//    - A rule is dropped when "match" or "assign" is not an object, when it has no usable match
//      property or no assignment left after validation, and its iconSource/iconLabel pair is
//      dropped when the iconSource SWF does not exist (see SetIconSourceValidator).
//    - Matching works on the SkyUI list entry object. Properties of one rule AND together. A JSON
//      array lists alternatives for the SAME property name and I4 keeps only the LAST one (its rule
//      stores properties in a std::map). Use {"anyOf": [...]} for OR. {"min","max"} is a range.
//      null matches an absent member. Enum names are case-insensitive; keywords are case-sensitive.
//    - Two passes over all rules, in order: (1) every matching rule writes its non-icon data
//      (e.g. "subType"), each later rule seeing earlier writes; if any non-icon data was written,
//      iconLabel/iconColor are cleared and SkyUI's default icon logic runs again on the modified
//      entry (that is how "assign subType" changes the icon); (2) every matching rule writes its
//      icon data (iconLabel, iconSource, iconColor). The LAST matching rule wins per field. A rule
//      that sets iconLabel without iconSource resets the source to SkyUI's default SWF.
//    - SkyUI's "icons.item.noColor": colours SkyUI or rules set are replaced by the colour I4's
//      rules give an empty entry, or removed (I4 IconSetter.cpp).
//    - Rules that can never match an inventory item (formType Spell / Enchantment / unknown) are
//      dropped silently. Rules that match on a field Dova Stash cannot evaluate from ItemInfo
//      (e.g. "armor", "damage", "isStolen", "spellType") are skipped, with ONE warning per file.
//
// Form ids in I4 files look like "Skyrim.esm|1994F" (no 0x, local id; for light plugins the local
// id is the low 12 bits, exactly like FormRef). They are parsed with FormRef::Parse.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "dova/core/Diagnostics.h"
#include "dova/core/Item.h"

namespace dova::core
{
	struct IconRef
	{
		std::string                  source;  // SWF path relative to Data/Interface, e.g. "skyui/icons_item_psychosteve.swf"
		std::string                  label;   // root-timeline frame label in that SWF
		std::optional<std::uint32_t> color;   // 0xRRGGBB, nullopt = SWF default

		[[nodiscard]] bool Empty() const noexcept { return label.empty(); }
		bool               operator==(const IconRef&) const = default;
	};

	inline constexpr std::string_view kSkyUIItemIcons = "skyui/icons_item_psychosteve.swf";
	inline constexpr std::string_view kSkyUICategoryIcons = "skyui/icons_category_psychosteve.swf";

	// SkyUI's own icon for an inventory entry (no I4). source = kSkyUIItemIcons. Never empty: SkyUI
	// shows "default_misc" for anything it has no better label for.
	[[nodiscard]] IconRef SkyUIDefaultIcon(const ItemInfo& a_item);

	// Data-file icon (category "icon" field, DESIGN §7.1):
	//   "weapon_sword"                                     label in SkyUI's item icon SWF
	//   { "label": "inv_weapons", "source": "skyui/icons_category_psychosteve.swf", "color": "#A4A5BF" }
	// "source" defaults to kSkyUIItemIcons; "color" is "#RRGGBB" (also "0xRRGGBB" or a number).
	// Problems are reported as errors in `a_diags` (source = `a_where`); returns nullopt then.
	[[nodiscard]] std::optional<IconRef> ParseIconRef(const nlohmann::json& a_json, std::string_view a_where, Diagnostics& a_diags);
	// Inverse of ParseIconRef: a plain label string when source is the SkyUI item SWF and there is
	// no colour, otherwise an object.
	nlohmann::json IconRefToJson(const IconRef& a_icon);

	// "#RRGGBB" / "RRGGBB" / "0xRRGGBB" -> 0xRRGGBB.
	[[nodiscard]] std::optional<std::uint32_t> ParseIconColor(std::string_view a_text);
	[[nodiscard]] std::string                  FormatIconColor(std::uint32_t a_color);  // "#RRGGBB"

	// Parses an I4 rule file as leniently as I4's JSON reader (jsoncpp defaults): // and /* */
	// comments, trailing commas and a UTF-8 BOM are accepted. Reports and returns nullopt on error.
	[[nodiscard]] std::optional<nlohmann::json> ParseI4Json(std::string_view a_text, std::string_view a_source, Diagnostics& a_diags);

	// I4 rule set.
	class IconRules
	{
	public:
		IconRules();
		~IconRules();
		IconRules(const IconRules& a_rhs);
		IconRules(IconRules&& a_rhs) noexcept;
		IconRules& operator=(const IconRules& a_rhs);
		IconRules& operator=(IconRules&& a_rhs) noexcept;

		// Adds one parsed rule file (call in I4's load order). `a_source` names it in diagnostics.
		void AddFile(const nlohmann::json& a_json, std::string_view a_source, Diagnostics& a_diags);
		// ParseI4Json + AddFile.
		void AddFileText(std::string_view a_text, std::string_view a_source, Diagnostics& a_diags);

		// Number of rules kept (valid and evaluable).
		[[nodiscard]] std::size_t Size() const noexcept;

		// SkyUI default, then I4 rules with I4's precedence. Never empty (see SkyUIDefaultIcon).
		[[nodiscard]] IconRef Resolve(const ItemInfo& a_item) const;

		// --- Environment (set before AddFile) ---------------------------------------------------

		// I4 drops a rule's iconSource + iconLabel when "Interface/<iconSource>" does not exist. The
		// default validator accepts everything. Results are cached per (case-insensitive) path.
		void SetIconSourceValidator(std::function<bool(std::string_view)> a_exists);
		// SkyUI's "icons.item.source" (skyui/config.txt [Appearance]); used for SkyUI's default icons
		// and for rules that set iconLabel without iconSource. Default kSkyUIItemIcons.
		void SetDefaultItemSource(std::string_view a_source);
		// SkyUI's "icons.item.noColor".
		void SetNoIconColors(bool a_noColors) noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> _impl;
	};
}
