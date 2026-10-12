#pragma once

// "Armor sets" page of the Dova Stash section in the SKSE Menu Framework (DESIGN §10.1, §8.4
// "Mannequins"): search every armor set in the load order and assign one to a mannequin of the current
// home (or Auto). RENDER THREAD: reads ArmorSetsModel snapshots only and posts actions.

namespace dova::ui::panel
{
	void __stdcall RenderArmorSetsPage();
}
