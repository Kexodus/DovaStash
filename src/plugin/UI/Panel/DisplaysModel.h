#pragma once

// Data for the Menu Framework "Displays" page (DESIGN §10.1, §8.4): every display Dova Stash recognises in
// the current home's loaded cells, plus the layout's display entries it cannot see from here. Same
// threading contract as PanelModel: the snapshot is built on the MAIN thread (SKSE task) and only read on
// the render thread; every action is a closure posted to the main thread, followed by a rebuild.
//
// Scanning a cell for displays runs the VM's script lookups for every reference, so it happens only when
// the home or its loaded cells change, or on the page's Refresh button. The periodic rebuild while the page
// is shown only re-reads the state of the displays already found (used, full, what they hold).
//
// Display kinds are handled generically (core::DisplayKindName): a kind added later shows up with its own
// name, and gets the category picker when ui::DisplayCategoryChoices offers it any (per what it holds: a
// shield rack's picker is not a weapon rack's).

#include "Game/Displays.h"

#include <mutex>

namespace dova::ui::panel
{
	// One line of a display's category picker.
	struct DisplayChoice
	{
		std::string pattern;  // "weapons.*", "weapons.bows"
		std::string label;    // "All Weapons", "Bows"
	};

	// A display kind present in this home (filters, bulk actions).
	struct DisplayKindInfo
	{
		core::DisplayKind kind{ core::DisplayKind::Rack };
		std::string       name;    // "Rack", "Plaque"
		std::string       plural;  // "racks", "plaques"
		std::size_t       total{ 0 };
		std::size_t       unlisted{ 0 };  // not in the setup: what "Use all" turns on
		std::size_t       ignored{ 0 };   // in the setup but turned off: "Use all" leaves them off
		std::vector<core::FormRef> unlistedRefs;
	};

	struct DisplayRow
	{
		core::FormRef                    ref;
		std::string                      refText;
		std::string                      name;  // the entry's label, else the game name
		std::optional<core::DisplayKind> kind;  // nullopt: a layout entry without `kind` that is not loaded here
		std::optional<core::DisplayFit>  fit;   // what it holds (its category picker); nullopt with `kind`
		std::string                      kindName;
		std::string                      area;  // multi-cell homes: its area ("" otherwise, or unknown)
		bool                             here{ false };    // in the player's cell: Fill now works
		bool                             loaded{ false };  // found in a loaded cell of the home
		bool                             available{ false };  // built (racks: the trigger has 3D)
		bool                             listed{ false };  // has a layout entry
		bool                             used{ false };    // listed and not ignored
		bool                             ignored{ false };
		bool                             full{ false };   // nothing more goes on it (Fill now is not offered)
		bool                             empty{ false };  // holds nothing
		std::vector<std::string>         categories;     // the entry's patterns
		std::string                      categoriesText;  // their names, or "any weapon" / "any armor"
		bool                             mannequin{ false };
		bool                             hasSet{ false };
		std::optional<core::FormRef>     set;      // the entry's `set` (a piece of it)
		std::string                      setName;  // mannequins: "Ebony", "Auto"
		std::vector<std::string>         madeFor;  // plaques, cases: what it is made for; racks, mannequins: preferred items
		bool                             only{ false };
		std::string                      takesText;  // one line: categories, set, made for
		std::string                      holdsText;  // "Ebony Cuirass, Ebony Boots", "full", "empty"
		std::string                      haystack;   // lowercase text the search box looks in
	};

	struct DisplaysSnapshot
	{
		std::uint64_t serial{ 0 };

		bool          inHome{ false };
		bool          enabled{ false };  // the home has a player layout
		bool          broken{ false };   // its setup file does not parse
		bool          multiCell{ false };
		std::uint32_t cellId{ 0 };
		std::string   homeName;
		std::string   hereArea;  // multi-cell homes: the area the player is in

		std::vector<DisplayRow>                                 rows;
		std::vector<DisplayKindInfo>                            kinds;    // kinds present, in enum order
		std::map<core::DisplayFit, std::vector<DisplayChoice>> choices;  // category picker per fit (none for plaques, cases)
	};

	class DisplaysModel
	{
	public:
		[[nodiscard]] static DisplaysModel& Get();

		// Any thread.
		[[nodiscard]] std::shared_ptr<const DisplaysSnapshot> Snapshot() const;
		// Any thread. Coalesced; at most every few seconds unless `a_now`. `a_rescan` looks for displays in
		// the loaded cells again (the page's Refresh button).
		void RequestRefresh(bool a_now = false, bool a_rescan = false);
		// Runs `a_action` on the main thread, then rebuilds the snapshot.
		void Post(std::function<void()> a_action);

	private:
		DisplaysModel() = default;

		void Rebuild();  // main thread

		mutable std::mutex                      _lock;
		std::shared_ptr<const DisplaysSnapshot> _snapshot;
		std::atomic<bool>                       _queued{ false };
		std::atomic<bool>                       _rescan{ false };
		std::atomic<std::int64_t>               _lastMs{ 0 };
		std::uint64_t                           _serial{ 0 };  // main thread

		// main thread: the displays found in the last scan, and what was scanned (the player's cell, then the
		// home's other loaded cells).
		std::vector<RE::FormID>         _scannedCells;
		std::vector<game::FoundDisplay> _found;
		std::map<RE::FormID, core::DisplayFit> _fits;  // what each found display holds (racks: read once per scan)
	};
}
