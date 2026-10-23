#pragma once

// A mannequin's armor set picker on the Menu Framework "Displays" page (DESIGN §10.1, §8.4 "Mannequins"): Auto,
// then a search over every armor set in the load order (Set, Mod, Pieces, You have; the sets the player has first);
// clicking a set makes the mannequin wear it. RENDER THREAD: reads ArmorSetsModel snapshots only; the choice is
// posted with `a_post` (the Displays page's model, so the page rebuilds).

#include <functional>

namespace dova::ui::panel
{
	void RenderArmorSetPicker(std::uint32_t a_cellId, const core::FormRef& a_mannequin, const std::optional<core::FormRef>& a_set,
		const std::function<void(std::function<void()>)>& a_post);
}
