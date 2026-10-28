// The inspector (docs/EDITOR-UX.md §2.4): Selection · Template · Draft · Checks. Selection is what is selected
// (one muted block for its reference and base record), its role in the template and where its items go; a
// multi-selection gets the bulk editor. Checks lists the template's problems, errors first, a click selects the
// object, and what the template still lacks as muted suggestions.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <numbers>

#include <imgui.h>

#include "Util.h"
#include "render/Thumbnail.h"
#include "ui/App.h"
#include "ui/Common.h"

namespace dova::editor::ui
{
	using namespace detail;

	int EditorApp::RefIndexOf(const OpenCell& a_cell, std::string_view a_ref) const
	{
		const auto parsed = core::FormRef::Parse(a_ref);
		for (std::size_t i = 0; i < a_cell.info.refs.size(); ++i) {
			if (parsed ? a_cell.lo->ToFormRef(a_cell.info.refs[i].key) == *parsed : core::IEquals(a_cell.refText[i], a_ref)) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	void EditorApp::DrawInspector()
	{
		const auto& t = w::Th();
		// The same bound as the frame's layout (App.cpp): from the window, not from the space left for the inspector
		// (that is the inspector's own width and would shrink the saved width every run).
		const float maxW = InspectorMaxWidth();
		if (!w::BeginRail("inspector", w::Side::Right, _state.ui.inspectorWidth, _state.ui.inspectorOpen, 280.0f * t.scale, maxW, "Inspector")) {
			return;
		}
		if (w::BeginPadded("inspad", ImVec2(0, 0), ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
			ImGui::Indent(4.0f * t.scale);  // clear of the resize handle on the left edge
			int        panel = static_cast<int>(_panel);
			const int  problems = _cell ? ErrorCount(*_cell) + WarningCount(*_cell) : 0;
			const auto checks = problems > 0 ? std::format("Checks {}###checks", problems) : std::string("Checks###checks");
			const char* labels[] = { "Selection", "Template", "Draft", checks.c_str() };
			const char* tips[] = { "What is selected", "Name, author, cells, keep rules, save", "Suggestions for this home", "Problems, errors first" };
			if (w::Segmented("panel", panel, labels, tips)) {
				_panel = static_cast<Panel>(panel);
			}
			ImGui::Unindent(4.0f * t.scale);
			w::Spacer(2.0f * t.scale);
			// Ends above the strip that holds the collapse chevron, so nothing scrolls under it.
			ImGui::BeginChild("panelbody", ImVec2(0, -std::round(30.0f * t.scale)), ImGuiChildFlags_None, ImGuiWindowFlags_None);
			ImGui::Indent(4.0f * t.scale);
			ImGui::PushItemWidth(-FLT_MIN);
			if (!_cell) {
				w::EmptyState("No home open", "Pick one in the rail.");
			} else {
				auto& c = *_cell;
				switch (_panel) {
				case Panel::Selection:
					DrawSelectionPanel(c);
					break;
				case Panel::Template:
					DrawTemplate(c);
					break;
				case Panel::Draft:
					DrawDraftPanel(c);
					break;
				case Panel::Checks:
					DrawChecksPanel(c);
					break;
				}
			}
			ImGui::PopItemWidth();
			ImGui::Unindent(4.0f * t.scale);
			ImGui::EndChild();
		}
		w::EndPadded();
		w::EndRail();
	}

	void EditorApp::DrawSelectionPanel(OpenCell& a_cell)
	{
		const auto sel = Selection();
		if (sel.size() > 1) {
			DrawBulkEditor(a_cell);
			return;
		}
		if (_selectedRef >= 0 && static_cast<std::size_t>(_selectedRef) < a_cell.info.refs.size()) {
			DrawRefDetails(a_cell, _selectedRef);
			return;
		}
		// Empty: what to do first.
		w::Spacer();
		w::Heading("Nothing selected");
		w::MutedWrapped("Click a chest, rack or mannequin. Ctrl+click adds, drag boxes.");
		w::Spacer();
		// Full width; a narrow panel wraps a label onto a second line rather than cutting it.
		const float bw = ImGui::GetContentRegionAvail().x;
		ImGui::BeginDisabled(Busy());
		if (w::Button("Make the nearest chest the master", "The usable chest nearest to the camera (3D) or the plan's middle", nullptr, w::Kind::Neutral, bw)) {
			MakeNearestMaster(a_cell);
		}
		if (w::Button("Draft this home", nullptr, nullptr, w::Kind::Neutral, bw)) {
			DraftThisHome();
		}
		ImGui::EndDisabled();
		if (w::Button("Open the containers list", nullptr, nullptr, w::Kind::Neutral, bw)) {
			_drawerTab = DrawerTab::Containers;
			_state.ui.drawerOpen = true;
		}
	}

	void EditorApp::DrawRefDetails(OpenCell& a_cell, int a_index)
	{
		const auto& t = w::Th();
		const auto& r = a_cell.info.refs[static_cast<std::size_t>(a_index)];
		// The object's own mesh (docs/EDITOR-UX.md §3.4) at the left of its name, once the scene has loaded.
		ImTextureID thumb{};
		if (_scene && _device) {
			if (!_thumbs) {
				_thumbs = std::make_unique<render::Thumbnails>(_device, _context);
			}
			if (_thumbs->Background() != t.bg2) {  // another theme: the pictures are drawn again on its panel colour
				_thumbs->Clear();
				_thumbs->SetBackground(t.bg2);
			}
			thumb = _thumbs->For(*_scene, a_index);
		}
		if (thumb) {
			const float  s = std::round(96.0f * t.scale);
			const ImVec2 p = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddImageRounded(ImTextureRef(thumb), p, ImVec2(p.x + s, p.y + s), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, t.radius);
			ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + s, p.y + s), t.line, t.radius);
			ImGui::Dummy(ImVec2(s, s));
			ImGui::SameLine(0.0f, t.pad);
		}
		ImGui::BeginGroup();
		ImGui::PushTextWrapPos(0.0f);
		w::Heading(RefLabel(a_cell.layout, a_cell.lo->ToFormRef(r.key), RefTitle(r)).c_str());
		ImGui::PopTextWrapPos();
		{
			std::string kind = r.display ? home::DisplayLabel(r) : r.IsContainer() ? "Container" : esp::SigToString(r.baseType);
			for (const auto& d : a_cell.info.doors) {
				if (d.door == r.key) {
					kind = "Door to " + d.destinationLabel;
				}
			}
			w::IconInline(IconOf(r), t.muted);
			ImGui::SameLine(0.0f, t.gap);
			w::Muted(kind.c_str());
		}
		// Facts that matter for a template, as chips.
		const float rightX = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
		bool        chips = false;
		auto chip = [&](const char* a_text, w::Kind a_kind, const char* a_tip) {
			if (chips) {
				w::FlowNext(w::ChipWidth(a_text), rightX);
			}
			w::Chip(a_text, a_kind);
			w::Tip(a_tip);
			chips = true;
		};
		if (r.respawns) {
			chip("respawns", w::Kind::Error, "Its contents reset: never bind it");
		} else if (r.respawnFlag) {
			chip("respawn flag", w::Kind::Warn, "Usable: this cell never resets, so its contents stay (as in game)");
		}
		if (r.initiallyDisabled) {
			chip("appears later", w::Kind::Neutral, "Initially disabled: an upgrade or a quest enables it");
		}
		if (r.enableParent) {
			chip(r.enableOpposite ? "goes with an upgrade" : "comes with its parent", w::Kind::Warn,
				r.enableOpposite ? "Disappears when its enable parent appears" : "Appears with its enable parent");
		}
		if (r.opens) {
			chip("opens a container", w::Kind::Info, "Dova Stash binds the container behind it");
		}
		if (r.ownedByOther) {
			chip("owned", w::Kind::Warn, "Owned by someone else");
		}
		ImGui::EndGroup();
		w::Spacer(chips || thumb ? t.gap : 2.0f);
		// The reference and the base record: one muted block with a copy icon.
		std::string lines = a_cell.refText[static_cast<std::size_t>(a_index)];
		if (!r.editorId.empty()) {
			lines += "  " + r.editorId;
		}
		lines += std::format("\n{} {}", esp::SigToString(r.baseType), r.baseEditorId);
		if (!r.model.empty()) {
			lines += "\n" + r.model;
		}
		lines += std::format("\n{:.0f}, {:.0f}, {:.0f} · {:.0f}° · x{:.2f}", r.position[0], r.position[1], r.position[2], r.rotation[2] * 180.0f / std::numbers::pi_v<float>, r.scale);
		if (std::abs(r.rotation[0]) > 1e-4f || std::abs(r.rotation[1]) > 1e-4f) {
			lines += std::format(" · tilt {:.0f}°, {:.0f}°", r.rotation[0] * 180.0f / std::numbers::pi_v<float>, r.rotation[1] * 180.0f / std::numbers::pi_v<float>);
		}
		for (const auto& s : r.scripts) {
			lines += "\n" + s;
		}
		w::RefBlock("ref", lines, a_cell.refText[static_cast<std::size_t>(a_index)]);
		DrawRoleEditor(a_cell, a_index);
		DrawPreviewDetails(a_cell, a_index);
		DrawRemoveButton(a_cell, a_index);
	}

	void EditorApp::DrawChecksPanel(OpenCell& a_cell)
	{
		const auto& t = w::Th();
		std::vector<const std::string*> sorted;
		for (const auto& d : a_cell.diagnostics) {
			sorted.push_back(&d);
		}
		// The game's own check, not the template's: a loaded plugin under "conflicts" switches it off.
		const auto conflict = LoadedConflict(a_cell.layout);
		const auto conflictText = std::format("warning: Not offered in game: {} {} this home", conflict,
			a_cell.layout.conflictPlugins.size() > 1 && conflict.find(',') != std::string::npos ? "rebuild" : "rebuilds");
		if (!conflict.empty()) {
			sorted.insert(sorted.begin(), &conflictText);
		}
		// Errors, the "Not offered in game" line with the reference checks it turns into information, then warnings.
		auto rank = [&](const std::string* a_text) { return a_text->starts_with("error") ? 0 : a_text == &conflictText ? 1 : a_text->starts_with("info") ? 2 : 3; };
		std::ranges::stable_sort(sorted, [&](const std::string* a, const std::string* b) { return rank(a) < rank(b); });
		if (sorted.empty()) {
			w::Spacer();
			w::Coloured(w::Kind::Ok, "No problems");
		}
		// The object a problem is about: "containers[3]", "displays[2]" or a form ref in the text (its form ref).
		auto refTextOf = [&](const std::string& a_text) -> std::string {
			for (const auto& [key, isDisplay] : { std::pair{ std::string("containers["), false }, std::pair{ std::string("displays["), true } }) {
				if (const auto at = a_text.find(key); at != std::string::npos) {
					const auto n = static_cast<std::size_t>(std::strtoul(a_text.c_str() + at + key.size(), nullptr, 10));
					const auto& l = a_cell.layout;
					if (!isDisplay && n < l.containers.size()) {
						return l.containers[n].ref.ToString();
					}
					if (isDisplay && n < l.displays.size()) {
						return l.displays[n].ref.ToString();
					}
				}
			}
			if (const auto& m = a_cell.layout.master; m && a_text.contains(m->ToString())) {
				return m->ToString();  // "master: <ref> ..." (a plugin name may have spaces)
			}
			if (const auto bar = a_text.find("|0x"); bar != std::string::npos) {
				const auto open = a_text.find_last_of("\"'( ", bar);
				const auto from = open == std::string::npos ? 0 : open + 1;
				const auto to = a_text.find_first_of(" \"'),;", bar);
				return std::string(Trim(std::string_view(a_text).substr(from, to == std::string::npos ? std::string::npos : to - from)));
			}
			return {};
		};
		// The line as the panel shows it: no "error:" nor "template:", and the container's name (the rail's and the
		// drawer's label) for "containers[N]: <ref>"; the form ref goes in the tooltip.
		auto shown = [&](const std::string& a_text, const std::string& a_ref, int a_target) {
			std::string body(Trim(std::string_view(a_text).substr(a_text.find(':') + 1)));
			if (body.starts_with("template:")) {
				body = std::string(Trim(std::string_view(body).substr(9)));
			}
			if (a_ref.empty()) {
				return body;
			}
			const auto form = core::FormRef::Parse(a_ref);
			auto       game = a_target >= 0 ? RefTitle(a_cell.info.refs[static_cast<std::size_t>(a_target)]) : a_ref;
			if (a_target < 0 && form && _cellDraft >= 0 && _homes && static_cast<std::size_t>(_cellDraft) < _homes->entries.size()) {
				// In another cell of the home: its name from the home's draft, which has read every cell.
				if (const auto* d = _homes->entries[static_cast<std::size_t>(_cellDraft)].draft.get()) {
					for (const auto& c : d->cells) {
						for (const auto& r : c.refs) {
							if (_homes->lo->ToFormRef(r.key) == *form) {
								game = RefTitle(r);
							}
						}
					}
				}
			}
			const auto name = form ? RefLabel(a_cell.layout, *form, game) : game;
			for (const auto* master : { "master: ", "master " }) {
				if (body.starts_with(std::string(master) + a_ref)) {
					return "The master" + body.substr(std::strlen(master) + a_ref.size());  // "The master starts disabled"
				}
			}
			if ((body.starts_with("containers[") || body.starts_with("displays[")) && body.find("]:") != std::string::npos) {
				const std::string rest(Trim(std::string_view(body).substr(body.find("]:") + 2)));
				return rest.starts_with(a_ref) ? name + rest.substr(a_ref.size()) : name + ": " + rest;  // "Cupboard is not a container"
			}
			if (const auto at = body.find(a_ref); at != std::string::npos && name != a_ref) {
				body.replace(at, a_ref.size(), name);
			}
			return body;
		};
		for (std::size_t k = 0; k < sorted.size(); ++k) {
			const auto& text = *sorted[k];
			const bool  error = text.starts_with("error");
			const bool  info = text.starts_with("info");
			const auto  refText = refTextOf(text);
			const int   target = refText.empty() ? -1 : RefIndexOf(a_cell, refText);
			const auto  body = shown(text, refText, target);
			ImGui::PushID(static_cast<int>(k));
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float  is = 14.0f * t.scale;
			ImGui::BeginGroup();
			ImGui::Dummy(ImVec2(is, is));
			ImGui::SameLine(0.0f, t.gap);
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(body.c_str());
			ImGui::PopTextWrapPos();
			ImGui::EndGroup();
			w::DrawIcon(ImGui::GetWindowDrawList(), error ? w::Icon::Error : info ? w::Icon::Info : w::Icon::Warning, ImVec2(p.x, p.y + 2.0f * t.scale), is,
				error ? t.error : info ? t.muted : t.warn);
			if (target >= 0 && ImGui::IsItemHovered()) {
				ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			}
			if (!refText.empty()) {
				w::Tip(target >= 0 ? ("Select it\n" + refText).c_str() : refText.c_str());
			}
			if (target >= 0 && ImGui::IsItemClicked()) {
				Select(target, true);
			}
			ImGui::PopID();
			w::Spacer(2.0f * t.scale);
		}

		// What the template still lacks. Containers left out: those there from the start, and apart those that come with
		// an upgrade (initially disabled or behind an enable parent).
		int listedBare = 0, unlisted = 0, unlistedLater = 0;
		for (const auto& r : a_cell.info.refs) {
			if (!r.IsContainer() || r.display || r.respawns) {
				continue;
			}
			const auto ref = a_cell.lo->ToFormRef(r.key);
			if (a_cell.layout.IsAnyMaster(ref)) {
				continue;  // the home's master or a joined cell's area master: listed, not in containers
			}
			if (const auto* b = a_cell.layout.FindContainer(ref)) {
				listedBare += !b->ignored && b->categories.empty() && b->forms.empty() ? 1 : 0;
			} else if (CellInHome(a_cell)) {
				++(r.initiallyDisabled || r.enableParent ? unlistedLater : unlisted);
			}
		}
		// A shipped or saved template leaves containers out on purpose: a quieter heading of their own.
		const bool saved = !a_cell.filePath.empty() || (!a_cell.savedPath.empty() && !a_cell.dirty);
		auto       notListed = [&]() {
			const auto text = unlisted == 0     ? std::format("{} containers not listed: they come with upgrades", unlistedLater)
			                  : unlistedLater > 0 ? std::format("{} containers not listed ({} more come with upgrades)", unlisted, unlistedLater)
			                                      : std::format("{} containers not listed", unlisted);
			if (w::LinkButton(text.c_str())) {
				_drawerTab = DrawerTab::Containers;
				_state.ui.drawerOpen = true;
				if (const auto bit = QuickFilterBit(DrawerTable::Containers, "Not listed")) {
					Table(DrawerTab::Containers).quick = *bit;
				}
			}
			w::Tip("Show them in the Containers tab");
		};
		const bool anyUnlisted = unlisted + unlistedLater > 0;
		if (!a_cell.layout.master || listedBare > 0 || (!saved && anyUnlisted)) {
			w::SectionHead("Still to do");
			ImGui::PushFont(t.smallFont);
			if (!a_cell.layout.master) {
				if (w::LinkButton("No master")) {
					MakeNearestMaster(a_cell);
				}
				w::Tip("Make the nearest chest the master");
			}
			if (listedBare > 0) {
				if (w::LinkButton(std::format("{} containers without a role", listedBare).c_str())) {
					_drawerTab = DrawerTab::Containers;
					_state.ui.drawerOpen = true;
				}
				w::Tip("Neither categories nor items, not ignored: they receive nothing");
			}
			if (!saved && anyUnlisted) {
				notListed();
			}
			ImGui::PopFont();
		}
		if (saved && anyUnlisted) {
			w::SectionHead("Not listed");
			ImGui::PushFont(t.smallFont);
			notListed();
			ImGui::PopFont();
		}
	}

	void EditorApp::MakeNearestMaster(OpenCell& a_cell)
	{
		if (!CellInHome(a_cell)) {
			SetStatus("This cell is not part of the home: add it from the Doors tab first", true);
			return;
		}
		// From the camera (3D) or the plan's middle; chests first, any usable container else.
		float x = _centerX - _panX / std::max(_zoom, 1e-4f), y = _centerY + _panY / std::max(_zoom, 1e-4f), z = 0.0f;
		const bool threeD = _state.ui.view == 0 && _scene;
		if (threeD) {
			x = _camera.position.x;
			y = _camera.position.y;
			z = _camera.position.z;
		}
		int   best = -1;
		float bestD = 0.0f;
		bool  bestChest = false;
		for (std::size_t i = 0; i < a_cell.info.refs.size(); ++i) {
			const auto& r = a_cell.info.refs[i];
			if (!r.IsContainer() || r.display || r.respawns || r.ownedByOther || (r.enableParent && r.enableOpposite)) {
				continue;
			}
			const bool  chest = IContains(r.baseName, "chest") || IContains(r.baseEditorId, "chest");
			const float dx = r.position[0] - x, dy = r.position[1] - y, dz = threeD ? r.position[2] - z : 0.0f;
			const float d = dx * dx + dy * dy + dz * dz;
			if (best < 0 || (chest && !bestChest) || (chest == bestChest && d < bestD)) {
				best = static_cast<int>(i);
				bestD = d;
				bestChest = chest;
			}
		}
		if (best < 0) {
			SetStatus("No usable container in this cell", true);
			return;
		}
		MakeMaster(a_cell, best);
		SelectInView(best, true);
	}
}
