#pragma once

// State shared between the main thread and the Menu Framework render thread (DESIGN §5 "Threading").
//
// - Snapshots (Snapshot.h) are BUILT on the main thread only (Rebuild/Focus/BuildGlobal), from
//   HomeRegistry/Store/game forms, and published as immutable shared_ptrs under `_lock`.
// - The render thread only READS published snapshots (Home()/Global()/Status()/Export()) and never
//   touches RE:: objects, HomeRegistry or Store.
// - Every button press becomes a closure run on the main thread through the SKSE task queue (Post),
//   which calls a Controller action (those re-validate the captured cell id) and then rebuilds the
//   snapshot. RequestRefresh() coalesces rebuild requests from either thread.

#include "UI/Panel/Snapshot.h"

#include <mutex>

namespace dova::ui::panel
{
	// How a category pattern reads on every panel page: "*" Everything, "<id>.*" All <name>, "<id>" its name.
	// `a_nameOf` names a category id; nullopt (unknown) leaves the pattern as it is.
	[[nodiscard]] std::string PatternName(std::string_view a_pattern, const std::function<std::optional<std::string>(std::string_view)>& a_nameOf);
	// The same with the names of a snapshot's category tree (render thread).
	[[nodiscard]] std::string PatternName(const CategoryTree& a_tree, std::string_view a_pattern);

	enum class Refresh : std::uint8_t
	{
		Light,  // layout, containers, learned, keep (cheap)
		Full    // + per-category item counts (scans every managed container inventory)
	};

	// The section pages (UI/Panel/Strings.h): a status line belongs to the page whose action posted it.
	enum class Page : std::uint8_t
	{
		Home,  // This home (the stash window too)
		Displays,
		Settings,
		About
	};

	class PanelModel
	{
	public:
		[[nodiscard]] static PanelModel& Get();

		// --- any thread ------------------------------------------------------------------------------
		[[nodiscard]] std::shared_ptr<const HomeSnapshot>   Home() const;
		[[nodiscard]] std::shared_ptr<const GlobalSnapshot> Global() const;
		// The status line of `a_page` (empty when the last one was posted from another page or home).
		[[nodiscard]] std::string                           Status(Page a_page = Page::Home) const;
		[[nodiscard]] ExportState                           Export() const;

		// Coalesced: at most one rebuild task is queued at a time.
		void RequestRefresh(Refresh a_refresh);
		// Runs `a_action` on the main thread, then rebuilds the snapshot.
		void Post(std::function<void()> a_action, Refresh a_refresh = Refresh::Light);

		// Render thread: the panel (window or section page) was drawn this frame. Also requests a light
		// rebuild every couple of seconds while the panel is visible.
		void MarkRendered(Page a_page = Page::Home);
		[[nodiscard]] bool RecentlyRendered() const;

		// An export was requested (shows "exporting..." until SetExport delivers the result).
		void BeginExport(std::uint32_t a_cell);
		void SetExport(ExportState a_state);
		// Last notification (shown at the top of the page, red when `a_error`: it reports a failure), for the home of
		// cell `a_cellId` (0: none). Status() is empty while the page shows another home, and after ClearStatus (the
		// window opens: the hotkey's Focus, the framework's own menu). It belongs to the page drawn last (the one the
		// player clicked on: actions run within a frame or two of the click).
		void SetStatus(std::string a_message, bool a_error = false, std::uint32_t a_cellId = 0);
		void ClearStatus();
		[[nodiscard]] bool StatusIsError() const;

		// --- main thread only ------------------------------------------------------------------------
		void Rebuild(Refresh a_refresh);
		// Panel opened by the hotkey in cell `a_cellId`: remember the container to focus and the tab to
		// show, then rebuild (the focus is ignored once the player is in another cell).
		void Focus(std::uint32_t a_cellId, std::optional<core::FormRef> a_container, Tab a_tab);
		void BuildGlobal();

	private:
		PanelModel() = default;

		[[nodiscard]] std::shared_ptr<const CategoryTree> Categories();

		mutable std::mutex                    _lock;
		std::shared_ptr<const HomeSnapshot>   _home;
		std::shared_ptr<const GlobalSnapshot> _global;
		std::string                           _status;
		bool                                  _statusError{ false };
		std::uint32_t                         _statusCell{ 0 };
		Page                                  _statusPage{ Page::Home };
		ExportState                           _export;

		// main thread only
		std::shared_ptr<const CategoryTree> _categories;
		std::optional<core::FormRef>        _focus;
		std::uint32_t                       _focusCell{ 0 };
		std::uint64_t                       _focusSerial{ 0 };
		Tab                                 _focusTab{ Tab::None };
		std::uint64_t                       _serial{ 0 };
		// The crosshair target last seen and the container it stands for (worked out again only when it changes:
		// a bookshelf's activator is found through its script).
		RE::RefHandle                _aimedHandle{ 0 };
		std::optional<core::FormRef> _aimed;
		// HomeSnapshot::ownChanges and what it was worked out for (home, template list, the setup as serialized).
		std::string _ownChangesKey;
		bool        _ownChanges{ false };

		std::atomic<bool>         _refreshQueued{ false };
		std::atomic<bool>         _fullQueued{ false };
		std::atomic<std::int64_t> _lastRenderMs{ 0 };
		std::atomic<std::int64_t> _lastAutoRefreshMs{ 0 };
		std::atomic<Page>         _renderPage{ Page::Home };  // the page drawn last
	};
}
