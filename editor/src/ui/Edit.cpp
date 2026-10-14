// Editing helpers (docs/EDITOR.md §3.7): undo/redo for every change to the template (Ctrl+Z / Ctrl+Y; a text
// field is one step once it loses focus), multi-select (Ctrl+click, box drag) with a bulk editor in the Selection
// panel, paint mode (P): pick a category, click containers to give it to them or take it away, and Delete.

#include <algorithm>
#include <format>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "Util.h"
#include "ui/App.h"
#include "ui/Common.h"
#include "ui/WidgetsExtra.h"

namespace dova::editor::ui
{
	namespace
	{
		constexpr std::size_t kUndoDepth = 200;
	}

	using namespace detail;

	void EditorApp::TrackUndo()
	{
		if (!_cell) {
			return;
		}
		if (_undoOwner != _cell.get()) {
			// Another cell: the same home keeps its history (the template came along), a new home starts afresh.
			if (_cell->json != _lastJson) {
				_undo.clear();
				_redo.clear();
			}
			_undoOwner = _cell.get();
			_lastLayout = _cell->layout;
			_lastJson = _cell->json;
			return;
		}
		if (ImGui::IsAnyItemActive() || _cell->json == _lastJson) {
			return;  // typing or dragging: one step when it is done
		}
		_undo.push_back(std::move(_lastLayout));
		if (_undo.size() > kUndoDepth) {
			_undo.erase(_undo.begin());
		}
		_redo.clear();
		_lastLayout = _cell->layout;
		_lastJson = _cell->json;
		SyncCellToDraft();  // a home of the rail keeps the change in its draft (Save N drafts, the rail's dot)
	}

	void EditorApp::Undo()
	{
		if (!_cell || _undo.empty()) {
			return;
		}
		_redo.push_back(_cell->layout);
		_cell->layout = std::move(_undo.back());
		_undo.pop_back();
		_cell->dirty = true;
		Revalidate(*_cell);
		_lastLayout = _cell->layout;
		_lastJson = _cell->json;
	}

	void EditorApp::Redo()
	{
		if (!_cell || _redo.empty()) {
			return;
		}
		_undo.push_back(_cell->layout);
		_cell->layout = std::move(_redo.back());
		_redo.pop_back();
		_cell->dirty = true;
		Revalidate(*_cell);
		_lastLayout = _cell->layout;
		_lastJson = _cell->json;
	}

	bool EditorApp::IsSelected(int a_index) const
	{
		return a_index >= 0 && (a_index == _selectedRef || std::ranges::find(_multi, a_index) != _multi.end());
	}

	std::vector<int> EditorApp::Selection() const
	{
		std::vector<int> out;
		if (_selectedRef >= 0) {
			out.push_back(_selectedRef);
		}
		for (const int i : _multi) {
			if (i >= 0 && std::ranges::find(out, i) == out.end()) {
				out.push_back(i);
			}
		}
		return out;
	}

	void EditorApp::ToggleMulti(int a_index)
	{
		if (a_index < 0) {
			return;
		}
		if (a_index == _selectedRef) {
			_selectedRef = _multi.empty() ? -1 : _multi.front();
			if (!_multi.empty()) {
				_multi.erase(_multi.begin());
			}
		} else if (const auto it = std::ranges::find(_multi, a_index); it != _multi.end()) {
			_multi.erase(it);
		} else if (_selectedRef < 0) {
			_selectedRef = a_index;
		} else {
			_multi.push_back(a_index);
		}
	}

	void EditorApp::BoxSelect(const std::vector<int>& a_indices, bool a_add)
	{
		if (!a_add) {
			_selectedRef = -1;
			_multi.clear();
		}
		for (const int i : a_indices) {
			if (!IsSelected(i)) {
				if (_selectedRef < 0) {
					_selectedRef = i;
				} else {
					_multi.push_back(i);
				}
			}
		}
		if (!a_indices.empty()) {
			_panel = Panel::Selection;
		}
	}

	void EditorApp::ClickRef(OpenCell& a_cell, int a_index)
	{
		if (_paint && a_index >= 0 && PaintAt(a_cell, a_index)) {
			return;
		}
		if (ImGui::GetIO().KeyCtrl) {
			ToggleMulti(a_index);
			_panel = Panel::Selection;
			return;
		}
		SelectInView(a_index);
	}

	bool EditorApp::PaintAt(OpenCell& a_cell, int a_index)
	{
		const auto& r = a_cell.info.refs[static_cast<std::size_t>(a_index)];
		if (_paintCategory.empty()) {
			_paintMessage = "Pick the category to paint first";
			return false;
		}
		if (!CellInHome(a_cell)) {
			_paintMessage = "This cell is not part of the home: add it from the Doors tab first";
			return false;
		}
		const auto ref = a_cell.lo->ToFormRef(r.key);
		std::vector<std::string>* cats = nullptr;
		if (r.IsContainer() && !r.display) {
			if (a_cell.layout.IsMaster(ref)) {
				_paintMessage = "The master takes what has no place: it needs no categories";
				return true;
			}
			if (r.respawns) {
				_paintMessage = GameName(r) + " respawns: never store things there";
				return true;
			}
			auto* b = a_cell.layout.FindContainer(ref);
			if (!b) {
				core::ContainerBinding nb;
				nb.ref = ref;
				nb.label = GameName(r);
				a_cell.layout.containers.push_back(std::move(nb));
				b = &a_cell.layout.containers.back();
			}
			if (b->ignored) {
				_paintMessage = GameName(r) + " is ignored (" + (b->note.empty() ? std::string("see its note") : b->note) + "): untick Ignored first";
				return true;
			}
			cats = &b->categories;
		} else if (r.display && (*r.display == core::DisplayKind::Rack || *r.display == core::DisplayKind::Mannequin)) {
			auto* d = a_cell.layout.FindDisplay(ref);
			if (!d) {
				core::DisplayBinding nd;
				nd.ref = ref;
				nd.kind = r.display;
				nd.label = GameName(r);
				a_cell.layout.displays.push_back(std::move(nd));
				d = &a_cell.layout.displays.back();
			}
			cats = &d->categories;
		} else {
			return false;  // not paintable: select it instead
		}
		if (const auto it = std::ranges::find(*cats, _paintCategory); it != cats->end()) {
			cats->erase(it);
			_paintMessage = std::format("{}: {} taken away", GameName(r), _paintCategory);
		} else {
			cats->push_back(_paintCategory);
			_paintMessage = std::format("{}: {} given", GameName(r), _paintCategory);
		}
		_selectedRef = a_index;
		_multi.clear();
		a_cell.dirty = true;
		Revalidate(a_cell);
		return true;
	}

	void EditorApp::SetPaint(bool a_on)
	{
		_paint = a_on;
		_paintMessage.clear();
		if (a_on && _colours == Colours::None) {
			_colours = Colours::Category;  // see what you paint
		}
	}

	bool EditorApp::DrawCategoryList(std::string& a_filter, std::string& a_chosen)
	{
		bool chosen = false;
		if (ImGui::IsWindowAppearing()) {
			ImGui::SetKeyboardFocusHere();
		}
		w::SearchBox("catsearch", a_filter, "Category", 300.0f * w::Th().scale);
		ImGui::BeginChild("cats", ImVec2(300.0f * w::Th().scale, 360.0f * w::Th().scale));
		if (_categories.empty()) {
			w::Muted("No Categories/*.json in Data/SKSE/Plugins/DovaStash");
		}
		for (const auto& c : _categories) {
			const bool group = c.match.Empty();
			const auto pattern = group ? c.id + ".*" : c.id;
			if (!a_filter.empty() && !IContains(pattern, a_filter) && !IContains(c.name, a_filter)) {
				continue;
			}
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float  s = ImGui::GetFontSize() * 0.7f;
			const float  indent = static_cast<float>(core::CategoryDepth(c.id) - 1) * ImGui::GetFontSize();
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x + indent, p.y + 3.0f), ImVec2(p.x + indent + s, p.y + 3.0f + s), CategoryColour(pattern), 2.0f);
			ImGui::SetCursorScreenPos(ImVec2(p.x + indent + s + 6.0f, p.y));
			if (ImGui::Selectable(std::format("{}  {}", pattern, c.name).c_str(), pattern == a_chosen)) {
				a_chosen = pattern;
				chosen = true;
			}
		}
		ImGui::EndChild();
		return chosen;
	}

	void EditorApp::DrawPaintBar(OpenCell& a_cell)
	{
		(void)a_cell;
		const auto& t = w::Th();
		LoadCategories();
		// The brush: a chip in the category's colour.
		if (!_paintCategory.empty()) {
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float  s = 10.0f * t.scale;
			const float  h = ImGui::GetFrameHeight();
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + (h - s) * 0.5f), ImVec2(p.x + s, p.y + (h + s) * 0.5f), CategoryColour(_paintCategory), 2.0f);
			ImGui::Dummy(ImVec2(s, h));
			ImGui::SameLine(0.0f, 4.0f * t.scale);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (h - 18.0f * t.scale) * 0.5f);
			w::Chip(_paintCategory.c_str(), w::Kind::Accent);
			ImGui::SameLine(0.0f, t.gap);
		}
		if (w::BeginPopover("paintcat", _paintCategory.empty() ? "Pick a category" : "Pick another", w::Icon::ChevronDown, 0, "The category to paint")) {
			if (DrawCategoryList(_paintFilter, _paintCategory)) {
				_paintMessage.clear();
				if (_colours == Colours::None) {
					_colours = Colours::Category;
				}
				ImGui::CloseCurrentPopup();
			}
			w::EndPopover();
		}
		ImGui::SameLine(0.0f, t.gap);
		if (w::ToolButton("Done", false, "Paint off", "P")) {
			SetPaint(false);
		}
	}

	void EditorApp::DrawBulkEditor(OpenCell& a_cell)
	{
		const auto sel = Selection();
		auto&      layout = a_cell.layout;
		int        containers = 0, displays = 0;
		std::vector<core::FormRef> refs;
		for (const int i : sel) {
			const auto& r = a_cell.info.refs[static_cast<std::size_t>(i)];
			if (r.IsContainer() && !r.display) {
				++containers;
				refs.push_back(a_cell.lo->ToFormRef(r.key));
			} else if (r.display) {
				++displays;
			}
		}
		w::Heading(std::format("{} selected", sel.size()).c_str());
		w::Muted(std::format("{} containers · {} displays · {} other", containers, displays, sel.size() - containers - displays).c_str());
		if (containers == 0) {
			w::Spacer();
			w::MutedWrapped("Select containers to edit them together.");
			return;
		}
		if (!CellInHome(a_cell)) {
			w::Spacer();
			w::MutedWrapped("This cell is not part of the home: add it from the Doors tab first.");
			return;
		}
		bool changed = false;
		auto forEach = [&](const std::function<void(core::ContainerBinding&)>& a_fn) {
			for (std::size_t k = 0; k < refs.size(); ++k) {
				if (layout.IsMaster(refs[k])) {
					continue;
				}
				auto* b = layout.FindContainer(refs[k]);
				if (!b) {
					core::ContainerBinding nb;
					nb.ref = refs[k];
					for (const int i : sel) {
						if (a_cell.lo->ToFormRef(a_cell.info.refs[static_cast<std::size_t>(i)].key) == refs[k]) {
							nb.label = GameName(a_cell.info.refs[static_cast<std::size_t>(i)]);
						}
					}
					layout.containers.push_back(std::move(nb));
					b = &layout.containers.back();
				}
				a_fn(*b);
			}
			changed = true;
		};
		// Categories any of them has, with how many have it: the x takes one away from all.
		w::SectionHead("Categories");
		std::vector<std::pair<std::string, int>> have;
		for (const auto& ref : refs) {
			if (const auto* b = layout.FindContainer(ref)) {
				for (const auto& c : b->categories) {
					auto it = std::ranges::find_if(have, [&](const auto& h) { return h.first == c; });
					if (it == have.end()) {
						have.emplace_back(c, 1);
					} else {
						++it->second;
					}
				}
			}
		}
		if (have.empty()) {
			w::Muted("none");
		}
		const float rightX = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
		for (std::size_t k = 0; k < have.size(); ++k) {
			const auto& [c, n] = have[k];
			ImGui::PushID(static_cast<int>(k));
			const auto text = std::format("{} {}/{}", c, n, containers);
			if (k > 0) {
				wx::FlowNext(wx::ChipWidth(text.c_str(), true), rightX);
			}
			if (w::ChipClose(text.c_str(), w::Kind::Accent)) {
				const std::string cat = c;
				forEach([&](core::ContainerBinding& b) { std::erase(b.categories, cat); });
			}
			w::Tip(("Take " + c + " away from all of them").c_str());
			ImGui::PopID();
		}
		std::vector<std::string> add;
		wx::FieldLabel("Give all of them");
		if (CategoryEditor("bulkadd", add, false) && !add.empty()) {
			forEach([&](core::ContainerBinding& b) {
				if (!b.ignored && std::ranges::find(b.categories, add.back()) == b.categories.end()) {
					b.categories.push_back(add.back());
				}
			});
		}
		if (ImGui::Button("Clear their categories")) {
			forEach([](core::ContainerBinding& b) { b.categories.clear(); });
		}
		w::SectionHead("Ignored");
		if (ImGui::Button("Ignore all")) {
			forEach([](core::ContainerBinding& b) { b.ignored = true; });
		}
		ImGui::SameLine();
		if (ImGui::Button("Use all")) {
			forEach([](core::ContainerBinding& b) { b.ignored = false; });
		}
		w::Tip("Respawning containers stay a bad idea: check the notes");
		w::SectionHead("Note");
		static std::string note;
		ImGui::InputTextMultiline("##bulknote", &note, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 2.5f));
		if (ImGui::Button("Set on all")) {
			forEach([&](core::ContainerBinding& b) { b.note = note; });
		}
		w::Spacer();
		if (w::LinkButton("Clear the selection")) {
			Select(-1);
		}
		w::Tip("Esc");
		if (changed) {
			a_cell.dirty = true;
			Revalidate(a_cell);
		}
	}

	void EditorApp::RequestDelete()
	{
		if (!_cell || Selection().empty()) {
			return;
		}
		if (Selection().size() > 1) {
			_confirmDelete = true;
		} else {
			DeleteSelection(*_cell);
		}
	}

	void EditorApp::DeleteSelection(OpenCell& a_cell)
	{
		auto& layout = a_cell.layout;
		int   removed = 0;
		for (const int i : Selection()) {
			const auto ref = a_cell.lo->ToFormRef(a_cell.info.refs[static_cast<std::size_t>(i)].key);
			if (layout.IsMaster(ref)) {
				layout.master.reset();
				++removed;
			}
			removed += static_cast<int>(std::erase_if(layout.containers, [&](const core::ContainerBinding& b) { return b.ref == ref; }));
			removed += static_cast<int>(std::erase_if(layout.displays, [&](const core::DisplayBinding& d) { return d.ref == ref; }));
		}
		if (removed > 0) {
			a_cell.dirty = true;
			Revalidate(a_cell);
			SetStatus(std::format("Removed {} from the template (Ctrl+Z brings {} back)", removed, removed == 1 ? "it" : "them"));
		}
	}

	void EditorApp::DrawDeletePopup()
	{
		if (_confirmDelete && !ImGui::IsPopupOpen("Remove from the template?")) {
			ImGui::OpenPopup("Remove from the template?");
		}
		if (ImGui::BeginPopupModal("Remove from the template?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Remove the %zu selected objects from the template?", Selection().size());
			w::Muted("Their roles go; Ctrl+Z brings them back.");
			w::Spacer();
			if (ImGui::Button("Remove")) {
				if (_cell) {
					DeleteSelection(*_cell);
				}
				_confirmDelete = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				_confirmDelete = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}
}