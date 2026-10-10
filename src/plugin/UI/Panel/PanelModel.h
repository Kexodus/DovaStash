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
	enum class Refresh : std::uint8_t
	{
		Light,  // layout, containers, learned, keep (cheap)
		Full    // + per-category item counts (scans every managed container inventory)
	};

	class PanelModel
	{
	public:
		[[nodiscard]] static PanelModel& Get();

		// --- any thread ------------------------------------------------------------------------------
		[[nodiscard]] std::shared_ptr<const HomeSnapshot>   Home() const;
		[[nodiscard]] std::shared_ptr<const GlobalSnapshot> Global() const;
		[[nodiscard]] std::string                           Status() const;
		[[nodiscard]] ExportState                           Export() const;

		// Coalesced: at most one rebuild task is queued at a time.
		void RequestRefresh(Refresh a_refresh);
		// Runs `a_action` on the main thread, then rebuilds the snapshot.
		void Post(std::function<void()> a_action, Refresh a_refresh = Refresh::Light);

		// Render thread: the panel (window or section page) was drawn this frame. Also requests a light
		// rebuild every couple of seconds while the panel is visible.
		void MarkRendered();
		[[nodiscard]] bool RecentlyRendered() const;

		// An export was requested (shows "exporting..." until SetExport delivers the result).
		void BeginExport();
		void SetExport(ExportState a_state);
		// Last notification (shown at the bottom of the panel).
		void SetStatus(std::string a_message);

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
		ExportState                           _export;

		// main thread only
		std::shared_ptr<const CategoryTree> _categories;
		std::optional<core::FormRef>        _focus;
		std::uint32_t                       _focusCell{ 0 };
		std::uint64_t                       _focusSerial{ 0 };
		Tab                                 _focusTab{ Tab::None };
		std::uint64_t                       _serial{ 0 };

		std::atomic<bool>         _refreshQueued{ false };
		std::atomic<bool>         _fullQueued{ false };
		std::atomic<std::int64_t> _lastRenderMs{ 0 };
		std::atomic<std::int64_t> _lastAutoRefreshMs{ 0 };
	};
}
