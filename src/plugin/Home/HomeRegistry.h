#pragma once

// Per-cell home state (DESIGN §6.2): the player's current cell, its player layout (if any), and the
// template resolution. Only the current cell is tracked (v1 is per-home; moves never touch unloaded
// cells). Main thread only.
//
// Cell-change detection: the registry is refreshed lazily from PlayerCharacter::GetParentCell() every
// time anything asks for Current() (hotkey, events, menu actions), so it is always right even if an
// event was missed. BGSActorCellEvent / TESCellFullyLoadedEvent / kPostLoadGame only trigger an
// early refresh so the one-time template notification shows on entry.

namespace dova::home
{
	struct Home
	{
		RE::FormID                  cellId{ 0 };
		core::FormRef               cell;
		std::string                 name;
		std::optional<core::Layout> layout;  // player layout; nullopt = not set up yet
		core::LayoutResolution      resolution;
		bool                        layoutBroken{ false };  // a layout file exists but does not parse: never overwrite
		bool                        dirty{ false };

		[[nodiscard]] bool                           Enabled() const noexcept { return layout.has_value(); }
		[[nodiscard]] const core::TemplateCandidate* BestTemplate() const;
	};

	class HomeRegistry
	{
	public:
		[[nodiscard]] static HomeRegistry& Get();

		// The eligible home for the player's current cell, or nullptr (with a UI reason).
		[[nodiscard]] Home* Current(std::string* a_reason = nullptr);
		// Same, but only if it is still the cell `a_cellId` (menu callbacks arrive frames later).
		[[nodiscard]] Home* CurrentIf(RE::FormID a_cellId);

		// True the first time it is called for a cell this session (template notification gating).
		bool MarkNotified(RE::FormID a_cellId) { return _notified.insert(a_cellId).second; }

		// Persistence. MarkDirty() defers the write (learned destinations), Save() writes now.
		void MarkDirty(Home& a_home) noexcept { a_home.dirty = true; }
		bool Save(Home& a_home);
		void Flush();  // writes the current home if dirty

		// Layout edits that create the player layout.
		core::Layout& EnsureLayout(Home& a_home);  // new empty player layout for this cell if none
		bool          ApplyBestTemplate(Home& a_home);
		// Backs up the player layout file (timestamped .bak), then merges `a_template` over the current
		// layout (core::ApplyTemplate keeps the player's learned/keep/ignored/forms data) and saves.
		// Refused (false, nothing changed) when the layout file is broken or the backup fails.
		bool ApplyTemplate(Home& a_home, const core::TemplateCandidate& a_template);
		bool          Reset(Home& a_home);

	private:
		HomeRegistry() = default;

		void Load(RE::TESObjectCELL* a_cell, core::FormRef a_cellRef);
		void Resolve(Home& a_home);

		std::optional<Home>          _home;
		std::unordered_set<RE::FormID> _notified;  // cells that showed the template notification this session
	};
}
