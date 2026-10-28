#pragma once

// The template's references checked against the game data (docs/EDITOR.md §3.1): the part of tools/esm_dump.py
// `check` that core cannot do without the plugins. Not a reference in the home's cells (the master, area masters,
// containers, displays), not a container, deleted,
// respawns, removed by a house upgrade, a scripted bookshelf listed as a container, a master that starts disabled.
// The Checks panel lists them with core::ValidateTemplate's, in esm_dump's words.

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "dova/core/Diagnostics.h"
#include "dova/core/Layout.h"
#include "home/Cell.h"

namespace dova::editor::home
{
	// The references of the home's loaded cells by form ref.
	using RefIndex = std::unordered_map<core::FormRef, const PlacedRef*>;

	struct RefCheckScope
	{
		const RefIndex*                       refs{ nullptr };
		bool                                  complete{ false };   // every cell of the home is in `refs`: a reference not found is an error
		bool                                  multiCell{ false };  // the home has more cells: "any of the home's cells"
		std::function<bool(std::string_view)> loaded;             // the plugin is in the load order (else its references are skipped)
	};

	void CheckTemplateRefs(const core::Layout& a_layout, const RefCheckScope& a_scope, const std::string& a_source, core::Diagnostics& a_diags);
}
