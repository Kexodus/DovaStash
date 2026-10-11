#pragma once

// QuickLoot IE integration (DESIGN §10.3), a soft dependency through its MIT-licensed SKSE API
// (extern/QuickLootIE/QuickLootAPI.h). QuickLoot draws everything, so whatever skin it uses and the
// game's button art apply. Only the public API is used, and text is plain (no HTML or colours):
//   - info bar: what the container is to Dova Stash, and where the selected item would be sorted to
//   - button bar (API V21): the Dova Stash hotkey, labelled, while looking at a container in an
//     enabled home. The key itself is handled by our input sink as usual.
// Learning needs nothing from QuickLoot: it only takes items, and its "transfer" opens the real
// ContainerMenu, which our container-changed sink already covers.

namespace dova::integrations::quickloot
{
	// kPostLoad (every SKSE plugin is loaded, so QuickLootIE.dll is resolvable). Logs the outcome.
	// [Integrations] bQuickLoot is checked on every QuickLoot callback, so it applies live.
	void Init();
	[[nodiscard]] bool IsReady() noexcept;

	// Any thread: ask QuickLoot to rebuild its view (after our moves or layout edits). No-op when
	// QuickLoot is not installed.
	void Refresh();
}
