#pragma once

// Displays in a home cell (DESIGN §8.4): finding them and reading their state. Main thread only.
//
// Recognised by the scripts the game binds to them (read from the Papyrus VM, nothing is changed):
// - Rack: an activator running WeaponRackActivateSCRIPT (vanilla weapon racks, shield plaques, dagger
//   displays, coat-of-arms racks; also the copies home mods make of them). Its RackType property says
//   what it takes; its WRackTrigger linked ref is the trigger whose pivot nodes hold the weapon. The
//   trigger's own script disables the activator while something is on the rack.
// - Mannequin: an actor running MannequinActivatorSCRIPT, which equips the armor added to its inventory.
// - Item: a reference whose script holds playable items in its properties (or a form list of them) and
//   a "placed" flag (bool or int, named isPlaced, placed...): the plaques home mods copy from the
//   "ClawToPlace" display script, for masks, claws, Elder Scrolls, books and artifacts. Activating one
//   with the item in the player's inventory takes it and shows a model; activating it again gives the
//   item back.
// - Case: a container whose script holds playable items in its properties and handles OnItemAdded (it
//   shows a model of each item put in and sends anything else back).

namespace dova::game
{
	struct FoundDisplay
	{
		RE::FormID                       ref{ 0 };  // rack activator, plaque, mannequin actor or case container
		core::DisplayKind                kind{ core::DisplayKind::Rack };
		core::RackType                   rack{ core::RackType::Any };
		RE::FormID                       trigger{ 0 };  // Rack: holds the pivot nodes (WRackTrigger linked ref)
		std::vector<RE::TESBoundObject*> items;         // Item/Case: what it is made for; Rack/Mannequin: preferred
		std::optional<bool>              placed;        // Item: the script's placed flag (nullopt = unknown)
		std::string                      script;        // the script that makes it a display (logs)
	};

	// Every display in `a_cell` recognised from its scripts.
	[[nodiscard]] std::vector<FoundDisplay> FindDisplays(RE::TESObjectCELL* a_cell);
	// The same checks for one reference; nullopt when it is no display Dova Stash recognises.
	[[nodiscard]] std::optional<FoundDisplay> InspectDisplay(RE::TESObjectREFR* a_ref);
	// The display the player aims at with the hotkey: the display itself, a mannequin through the activator
	// in front of it, or a rack through the weapon sitting on it (a full rack's activator is disabled, so
	// the crosshair finds the weapon). nullopt when it is none of these.
	[[nodiscard]] std::optional<FoundDisplay> DisplayAt(RE::TESObjectREFR* a_ref);
	// A display a layout declares with `kind` that the scripts do not reveal (racks still need their
	// WRackTrigger linked ref, mannequins must be actors, cases containers). Its state is unknown.
	[[nodiscard]] std::optional<FoundDisplay> DeclaredDisplay(RE::TESObjectREFR* a_ref, core::DisplayKind a_kind);

	// Built (not disabled, also through enable parents) and, for racks, its trigger has 3D.
	[[nodiscard]] bool DisplayAvailable(const FoundDisplay& a_display);
	// Something is on it: a rack's activator is disabled, a mannequin wears armor, a plaque's flag is set.
	// Cases are never full (see DisplayHolds).
	[[nodiscard]] bool DisplayFull(const FoundDisplay& a_display);
	// What a mannequin or a case holds.
	[[nodiscard]] std::vector<RE::TESBoundObject*> DisplayHolds(const FoundDisplay& a_display);

	// Base forms of the weapons and armor lying loose in the cell (on racks, tables, the floor).
	[[nodiscard]] std::set<RE::FormID> LooseGear(RE::TESObjectCELL* a_cell);
}
