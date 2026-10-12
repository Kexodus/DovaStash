#pragma once

// FormRef <-> game form conversion (DESIGN §6.1) and small form helpers.
//
// Identity is derived from the form id's load-order byte(s), not from TESForm::GetFile(0): the file
// that *owns* the id is the one whose index is encoded in it (this also gives the right answer for
// records injected into another plugin's id space). Dynamic forms (0xFF......) have no FormRef and are
// never persisted.
//
// All functions are main-thread only and require InitFormUtil() (called at kDataLoaded; the load
// order cannot change afterwards).

namespace dova::game
{
	inline constexpr RE::FormID kPlayerRefID = 0x00000014;
	inline constexpr RE::FormID kPlayerBaseID = 0x00000007;
	inline constexpr RE::FormID kPlayerFactionID = 0x00000DB1;

	// Builds the compile-index -> file tables and the loaded plugin list. Logs the plugin counts.
	void InitFormUtil();
	[[nodiscard]] bool IsFormUtilReady() noexcept;

	// nullopt for null, dynamic (0xFF) forms, or ids whose file cannot be found.
	[[nodiscard]] std::optional<core::FormRef> ToFormRef(const RE::TESForm* a_form);

	// Case-insensitive plugin match. nullptr when the plugin is not loaded or the id does not exist.
	[[nodiscard]] RE::TESForm*       LookupForm(const core::FormRef& a_ref);
	// The runtime form id `a_ref` stands for, whether or not the form is in memory (an exterior cell that is
	// not loaded). nullopt when the plugin is not loaded or the id is not addressable in it.
	[[nodiscard]] std::optional<RE::FormID> FormIdOf(const core::FormRef& a_ref);
	[[nodiscard]] RE::TESObjectREFR* LookupRef(const core::FormRef& a_ref);

	// Names of every loaded plugin (full + light), in load order, original spelling.
	[[nodiscard]] const std::vector<std::string>& LoadedPlugins();

	[[nodiscard]] RE::PlayerCharacter* Player();

	// "Iron Sword [Skyrim.esm|0x00012EB7]" (or "[dynamic 0xFF000801]") — for logs.
	[[nodiscard]] std::string Describe(const RE::TESForm* a_form);
	// Display name of a reference ("Chest"), falling back to the base object's name / editor id.
	[[nodiscard]] std::string RefName(RE::TESObjectREFR* a_ref);
	// Cell display name, falling back to its editor id.
	[[nodiscard]] std::string CellName(RE::TESObjectCELL* a_cell);

	// Resolves a reference handle value (as stored from events) to a live reference.
	[[nodiscard]] RE::NiPointer<RE::TESObjectREFR> RefFromHandle(RE::RefHandle a_handle);
}
