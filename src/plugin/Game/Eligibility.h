#pragma once

// Home eligibility and container safety checks (DESIGN §9). Main thread only.

namespace dova::game
{
	struct Verdict
	{
		bool        ok{ false };
		std::string reason;   // why it was rejected (UI text: a short clause, no parentheses), empty when ok
		std::string warning;  // accepted but worth logging / showing (respawn flag in a never-reset cell, scripts...)
		bool        scripted{ false };  // the container has Papyrus scripts attached (never auto-added by Learn)
		std::string detail;   // the long form of `reason` for the log (empty: the same as `reason`)
		bool        notBuilt{ false };  // rejected because it is disabled (unbuilt furniture): expected, logged quietly

		static Verdict Accept(std::string a_warning = {}) { return { true, {}, std::move(a_warning), false }; }
		static Verdict Reject(std::string a_reason, std::string a_detail = {})
		{
			return { false, std::move(a_reason), {}, false, std::move(a_detail) };
		}

		// What the log says about a rejection.
		[[nodiscard]] const std::string& LogReason() const noexcept { return detail.empty() ? reason : detail; }

		void AddWarning(std::string_view a_text)
		{
			if (!warning.empty()) {
				warning += "; ";
			}
			warning += a_text;
		}
	};

	// A cell can host a layout when it is an interior (or a player-owned exterior) AND it is owned by the
	// player / the player faction / a faction the player is a member of, OR unowned, OR its location (or
	// a parent location) carries LocTypePlayerHouse.
	// A cell of a home mod with its own sorting system (SortsItself) is rejected.
	[[nodiscard]] Verdict CheckCell(RE::TESObjectCELL* a_cell);
	// The entry of the homes that sort themselves (SelfSorting/*.json, DESIGN §9.1) that leaves `a_cell` alone, or nullptr.
	[[nodiscard]] const core::SelfSortingHome* SortsItself(const core::FormRef& a_cell);

	// Per-container checks: base form is a CONT, the ref lives in `a_homeCell`, is persistent-identifiable,
	// not disabled (including through its enable parent), not owned by someone else, and does not respawn
	// unless the cell never resets / is a player house.
	// Ownership: the ref's own ExtraOwnership, else the cell's owner (as the engine's crime logic does).
	// A foreign ref owner is rejected. A foreign *cell* owner is rejected too, except in a player house
	// (LocTypePlayerHouse), which CheckCell accepts by design (DESIGN §9): there it is only a warning.
	// Containers with attached Papyrus scripts are accepted with a warning and `scripted` set. A container another
	// mod sorts itself (SelfSorting `containers`, DESIGN §9.1) is rejected.
	[[nodiscard]] Verdict CheckContainer(RE::TESObjectREFR* a_ref, RE::TESObjectCELL* a_homeCell);

	// Names of the Papyrus scripts bound to `a_ref` (scripts on the base form are bound per reference,
	// so they are included). Reads the VM's attached-script table under its lock; no side effects.
	[[nodiscard]] std::vector<std::string> AttachedScripts(RE::TESObjectREFR* a_ref);

	[[nodiscard]] bool IsContainerRef(RE::TESObjectREFR* a_ref);

	// The container `a_ref` stands for: a container is itself; an activator whose linked ref (no keyword)
	// is a container in the same cell stands for that container. Home mods use such "fronts" for shelves,
	// racks and cupboards that open a hidden chest (ActivateLinkedChestDummyScript). Else nullptr.
	[[nodiscard]] RE::TESObjectREFR* ContainerBehind(RE::TESObjectREFR* a_ref);
	// Every front in `a_cell`: activator -> the container it opens. Main thread.
	[[nodiscard]] std::unordered_map<RE::FormID, RE::FormID> FindFronts(RE::TESObjectCELL* a_cell);
	[[nodiscard]] bool IsEffectivelyDisabled(RE::TESObjectREFR* a_ref);
	// A reference xEdit "undeleted" (a deleted one restored as disabled): its enable parent is the player with the
	// opposite state, or it was moved below z -29000. Nothing ever enables it.
	[[nodiscard]] bool IsUndeletedRef(RE::TESObjectREFR* a_ref);
	// Disabled furniture a build can turn on: it has an enable parent (a build marker, as in Hearthfire) and is no
	// undeleted reference.
	[[nodiscard]] bool MayBeBuilt(RE::TESObjectREFR* a_ref);
	[[nodiscard]] bool IsPlayerHouse(RE::TESObjectCELL* a_cell);
	[[nodiscard]] bool CellNeverResets(RE::TESObjectCELL* a_cell);
	// Player NPC, PlayerFaction, or any faction the player belongs to (e.g. DLC2RRSeverinManorFaction).
	[[nodiscard]] bool IsPlayerOwner(RE::TESForm* a_owner);
}
