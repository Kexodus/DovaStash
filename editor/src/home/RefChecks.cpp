#include "home/RefChecks.h"

#include <algorithm>
#include <format>

namespace dova::editor::home
{
	namespace
	{
		bool HasScript(const PlacedRef& a_ref, std::string_view a_name)
		{
			return std::ranges::any_of(a_ref.scripts, [&](const std::string& s) { return core::IEquals(s, a_name); });
		}
	}

	void CheckTemplateRefs(const core::Layout& a_layout, const RefCheckScope& a_scope, const std::string& a_source, core::Diagnostics& a_diags)
	{
		if (!a_scope.refs) {
			return;
		}
		const std::string whereText = a_scope.multiCell || !a_layout.cell ? std::string("any of the home's cells") : "cell " + a_layout.cell->ToString();
		// Never a false alarm: a reference of a plugin that is not loaded, or of a cell not read, is not reported.
		auto find = [&](const core::FormRef& a_ref, const std::string& a_where) -> const PlacedRef* {
			if (const auto it = a_scope.refs->find(a_ref); it != a_scope.refs->end()) {
				return it->second;
			}
			if (a_scope.complete && (!a_scope.loaded || a_scope.loaded(a_ref.plugin))) {
				a_diags.Error(a_source, std::format("{}: {} is not a reference in {}", a_where, a_ref.ToString(), whereText));
			}
			return nullptr;
		};
		auto container = [&](const core::FormRef& a_ref, const std::string& a_where, bool a_bound, bool a_master) {
			const auto* r = find(a_ref, a_where);
			if (!r) {
				return;
			}
			const auto ref = a_ref.ToString();
			if (!r->IsContainer()) {
				a_diags.Error(a_source, std::format("{}: {} is not a container (base is not a CONT)", a_where, ref));
				return;
			}
			if (r->deleted) {
				a_diags.Error(a_source, std::format("{}: {} is deleted", a_where, ref));
			}
			if (a_bound && r->respawns) {
				a_diags.Error(a_source, std::format("{}: {} ({}) respawns: never bind it", a_where, ref, r->baseEditorId));
			} else if (a_bound && r->respawnFlag) {
				a_diags.Warn(a_source, std::format("{}: {} ({}) respawns; accepted because its cell never resets", a_where, ref, r->baseEditorId));
			}
			if (a_bound && HasScript(*r, "PlayerBookShelfContainerScript")) {
				a_diags.Warn(a_source, std::format("{}: {} is a scripted bookshelf (it holds a few books and hands the rest back): "
				                                   "mark it ignored and list it under 'displays' instead",
										   a_where, ref));
			}
			if (a_bound && r->enableParent && r->enableOpposite && !HasScript(*r, "BYOHDivertItemScript")) {
				a_diags.Warn(a_source, std::format("{}: {} disappears when its enable parent is enabled (house upgrade?)", a_where, ref));
			}
			if (a_master && r->initiallyDisabled) {
				a_diags.Warn(a_source, std::format("master {} starts disabled (the game enables it later, e.g. when built)", ref));
			}
		};
		if (a_layout.master) {
			container(*a_layout.master, "master", true, true);
		}
		for (std::size_t i = 0; i < a_layout.cells.size(); ++i) {
			// An area master (DESIGN §6.7): a container of the home, bound like the others.
			if (const auto& m = a_layout.cells[i].master; m && !a_layout.IsMaster(*m) && !a_layout.FindContainer(*m)) {
				container(*m, std::format("cells[{}].master", i), true, false);
			}
		}
		for (std::size_t i = 0; i < a_layout.containers.size(); ++i) {
			const auto& c = a_layout.containers[i];
			if (a_layout.IsMaster(c.ref)) {
				continue;  // checked as the master
			}
			container(c.ref, std::format("containers[{}]", i), !c.ignored, false);
		}
		for (std::size_t i = 0; i < a_layout.displays.size(); ++i) {
			const auto* r = find(a_layout.displays[i].ref, std::format("displays[{}]", i));
			if (r && r->deleted) {
				a_diags.Error(a_source, std::format("displays[{}]: {} is deleted", i, a_layout.displays[i].ref.ToString()));
			}
		}
	}
}
