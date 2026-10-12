#pragma once

// Displays in a home cell (DESIGN §8.4): finding them and reading their state. Main thread only.
//
// Recognised by the scripts the game binds to them (read from the Papyrus VM; only the functions that
// place a weapon on a rack, below, change anything):
// - Rack: an activator running WeaponRackActivateSCRIPT (vanilla weapon racks, shield plaques, dagger
//   displays, coat-of-arms racks, the Unofficial Patch's large display cases; also the copies home mods
//   make of them). Its RackType property says
//   what it takes; its WRackTrigger linked ref is the trigger whose pivot nodes hold the weapon. The
//   activator is disabled while something is on the rack: by the trigger's script in the vanilla game,
//   by the activator's own script with the Unofficial Patch (whose trigger only re-enables it when the
//   weapon is taken). DisableRack and RecordRackItem do that bookkeeping for a weapon Dova Stash placed.
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
	// Nothing more goes on it: a rack's activator is disabled, a mannequin wears armor, a plaque's flag is
	// set, a case holds every item it is made for.
	[[nodiscard]] bool DisplayFull(const FoundDisplay& a_display);
	// What a mannequin or a case holds.
	[[nodiscard]] std::vector<RE::TESBoundObject*> DisplayHolds(const FoundDisplay& a_display);

	// What a rack takes beyond its RackType, from the Unofficial Patch's WeaponRackActivateSCRIPT and its
	// trigger's nodes (nothing with the vanilla script): crossbows where the trigger has CrossbowPivot01;
	// never the Forsworn staves (its USKPForswornStavesList) where the trigger has BogusPivot.
	struct RackFit
	{
		bool                             crossbows{ false };
		std::vector<RE::TESBoundObject*> refuses;
	};
	[[nodiscard]] RackFit RackFitOf(const FoundDisplay& a_rack);

	// The node of the rack's trigger that WeaponRackActivateSCRIPT moves `a_item` to, once its 3D is
	// loaded. `a_pivot` is core::RackPivot's choice, which is what the vanilla script does. The Unofficial
	// Patch's script picks by form, keyword and the item's own nodes: crossbow nodes, staff nodes (another
	// set on coat-of-arms racks), shield nodes for Ysgramor's, dwarven, daedric and dragonbone shields.
	// A node the trigger does not have falls back to `a_pivot`. `refusal` is set (and `node` empty) when
	// that script turns the item away here: a long staff in a display case (trigger node WRDisplayCase01,
	// staff node Staff3rdPerson:0 or 3rdPersonStaff04:1), a Forsworn staff where the trigger has BogusPivot.
	struct RackPlacement
	{
		std::string node;
		std::string refusal;
	};
	[[nodiscard]] RackPlacement RackNode(const FoundDisplay& a_rack, RE::TESObjectREFR* a_item, std::string_view a_pivot);

	// Moves `a_item` onto the node `a_node` of `a_target` with Papyrus' own ObjectReference.MoveToNode, the
	// call WeaponRackActivateSCRIPT makes. Runs on the VM's next update; the flag turns true once it ran.
	// Null if it could not be sent (no VM, no handle for `a_item`).
	[[nodiscard]] std::shared_ptr<std::atomic<bool>> PapyrusMoveToNode(RE::TESObjectREFR* a_item, RE::TESObjectREFR* a_target, std::string_view a_node);
	// The same move done directly (when the VM can't do it): CommonLib's TESObjectREFR::MoveToNode, with
	// the node's rotation turned into angles correctly. CommonLib's NiMatrix3::ToEulerAnglesXYZ uses its
	// NiFastATan2, whose result has the wrong sign whenever |y| <= |x| (any angle within 45 degrees of 0 or
	// 180), and its gimbal-lock branches do not invert SetEulerAnglesXYZ either, so it turned or mirrored
	// weapons (a mace hanging sideways on a wall rack). False if `a_target` has no 3D or no such node.
	bool MoveToNodeDirect(RE::TESObjectREFR* a_item, RE::TESObjectREFR* a_target, std::string_view a_node);

	// What WeaponRackActivateSCRIPT does when it places a weapon, in two steps. After the weapon was moved
	// onto the pivot node, DisableRack disables the activator; false (nothing changed) when the activator
	// has no such script (keeping the weapon in PlayersDroppedWeapon), so only its trigger can disable it.
	bool DisableRack(const FoundDisplay& a_rack);
	// Once the activator is disabled: records `a_item` in that script (PlayersDroppedWeapon; the Unofficial
	// Patch's PlacedItemInit) and puts the trigger in its WaitingForReference state (the Unofficial Patch
	// leaves an empty rack's trigger in a state that ignores items leaving it), so taking the weapon frees
	// the rack again and the script puts it back after a cell reload.
	void RecordRackItem(const FoundDisplay& a_rack, RE::TESObjectREFR* a_item);

	// Keeps a weapon dropped for a rack out of sight until it is up: its 3D is app-culled now if it has
	// any, and again whenever it loads (TESObjectLoadedEvent), until ShowDropped. Only the rendering is
	// hidden: collision, trigger events and the motion type are untouched. One at a time (racks are filled
	// one by one); hiding another first shows the previous one.
	void HideDropped(RE::TESObjectREFR* a_item);
	// Shows the weapon HideDropped keeps hidden (if any, and if it still has 3D) and stops hiding it.
	void ShowDropped();

	// Base forms of the weapons and armor lying loose in the cell (on racks, tables, the floor).
	[[nodiscard]] std::set<RE::FormID> LooseGear(RE::TESObjectCELL* a_cell);
}
