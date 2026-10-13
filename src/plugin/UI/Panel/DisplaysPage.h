#pragma once

// "Displays" page of the Dova Stash section in the SKSE Menu Framework (DESIGN §10.1, §8.4): every display
// of the current home with its state, filters, and the hotkey display menu's actions (Use / Stop using,
// Categories, Armor set, Fill now) plus "Use all racks". RENDER THREAD: reads DisplaysModel snapshots only
// and posts Controller actions to the main thread.

namespace dova::ui::panel
{
	void __stdcall RenderDisplaysPage();
}
