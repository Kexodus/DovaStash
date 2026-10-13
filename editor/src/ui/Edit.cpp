// Editing helpers (docs/EDITOR.md §3.7): undo/redo for every change to the template (Ctrl+Z / Ctrl+Y; a text
// field is one step once it loses focus), multi-select (Ctrl+click, box drag) with a bulk editor in the details
// pane, and paint mode (P): pick a category, click containers to give it to them or take it away.

#include <algorithm>
#include <format>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "Util.h"
#include "ui/App.h"

namespace dova::editor::ui
{
	namespace
	{
		constexpr std::size_t kUndoDepth = 200;

		std::string GameName(const home::PlacedRef& a_ref) { return a_ref.baseName.empty() ? a_ref.baseEditorId : a_ref.baseName; }
	}

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
	}

	void EditorApp::ClickRef(OpenCell& a_cell, int a_index)
	{
		if (_paint && a_index >= 0 && PaintAt(a_cell, a_index)) {
			return;
		}
		if (ImGui::GetIO().KeyCtrl) {
			ToggleMulti(a_index);
			return;
		}
		Select(a_index);
	}

	bool EditorApp::PaintAt(OpenCell& a_cell, int a_index)
	{
		const auto& r = a_cell.info.refs[static_cast<std::size_t>(a_index)];
		if (_paintCategory.empty()) {
			_paintMessage = "Pick the category to paint first.";
			return false;
		}
		if (!CellInHome(a_cell)) {
			_paintMessage = "This cell is not part of the home: add it from the Doors tab first.";
			return false;
		}
		const auto ref = a_cell.lo->ToFormRef(r.key);
		std::vector<std::string>* cats = nullptr;
		if (r.IsContainer() && !r.display) {
			if (a_cell.layout.IsMaster(ref)) {
				_paintMessage = "The master takes what has no place: it needs no categories.";
				return true;
			}
			if (r.respawns) {
				_paintMessage = GameName(r) + " respawns: never store things there.";
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
				_paintMessage = GameName(r) + " is ignored (" + (b->note.empty() ? std::string("see its note") : b->note) + "). Untick Ignored first.";
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

	void EditorApp::DrawPaintBar(OpenCell& a_cell)
	{
		ImGui::Checkbox("Paint (P)", &_paint);
		ImGui::SetItemTooltip("Paint mode: pick a category, then click containers (and racks, mannequins) to give it to them; click again to take it away.");
		if (_paint) {
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 13.0f);
			const std::string preview = _paintCategory.empty() ? std::string("pick a category...") : _paintCategory;
			if (ImGui::BeginCombo("##paintcat", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
				for (const auto& c : _categories) {
					const bool group = c.match.Empty();
					const auto pattern = group ? c.id + ".*" : c.id;
					const ImVec2 p = ImGui::GetCursorScreenPos();
					const float  s = ImGui::GetFontSize() * 0.7f;
					ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + 3.0f), ImVec2(p.x + s, p.y + 3.0f + s), CategoryColour(pattern), 2.0f);
					ImGui::Dummy(ImVec2(s, s));
					ImGui::SameLine();
					if (ImGui::Selectable(std::format("{}{}  {}", std::string(static_cast<std::size_t>(core::CategoryDepth(c.id) - 1) * 2, ' '), pattern, c.name).c_str(),
							pattern == _paintCategory)) {
						_paintCategory = pattern;
						if (_colours == Colours::None) {
							_colours = Colours::Category;  // see what you paint
						}
					}
				}
				ImGui::EndCombo();
			}
			if (!_paintMessage.empty()) {
				ImGui::SameLine();
				ImGui::TextDisabled("%s", _paintMessage.c_str());
			}
		}
		(void)a_cell;
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
		ImGui::TextWrapped("%zu selected: %d containers, %d displays, %zu other", sel.size(), containers, displays, sel.size() - containers - displays);
		ImGui::TextDisabled("Ctrl+click adds or removes one, a box drag selects many, Esc clears.");
		if (containers == 0) {
			ImGui::TextDisabled("Select containers to edit them together.");
			return;
		}
		if (!CellInHome(a_cell)) {
			ImGui::TextWrapped("This cell is not part of the home: add it from the Doors tab first.");
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
		// Categories any of them has, with how many have it.
		ImGui::SeparatorText("Categories of the selection");
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
			ImGui::TextDisabled("(none)");
		}
		for (const auto& [c, n] : have) {
			if (ImGui::SmallButton(std::format("{} ({}/{})  x", c, n, containers).c_str())) {
				forEach([&](core::ContainerBinding& b) { std::erase(b.categories, c); });
			}
			ImGui::SetItemTooltip("Click to take %s away from all of them", c.c_str());
		}
		std::vector<std::string> add;
		ImGui::TextDisabled("Give all of them:");
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
		ImGui::SeparatorText("Ignored");
		if (ImGui::Button("Ignore all")) {
			forEach([](core::ContainerBinding& b) { b.ignored = true; });
		}
		ImGui::SameLine();
		if (ImGui::Button("Use all")) {
			forEach([](core::ContainerBinding& b) { b.ignored = false; });
		}
		ImGui::SetItemTooltip("Un-ignores them (respawning containers stay a bad idea: check the notes).");
		ImGui::SeparatorText("Note");
		static std::string note;
		ImGui::InputTextMultiline("##bulknote", &note, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 2.5f));
		if (ImGui::Button("Set this note on all")) {
			forEach([&](core::ContainerBinding& b) { b.note = note; });
		}
		ImGui::Spacing();
		if (ImGui::Button("Clear the selection (Esc)")) {
			_selectedRef = -1;
			_multi.clear();
		}
		if (changed) {
			a_cell.dirty = true;
			Revalidate(a_cell);
		}
	}

	void EditorApp::HandleEditShortcuts()
	{
		const auto& io = ImGui::GetIO();
		if (io.WantTextInput || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
			return;
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && _ws && !_job.valid()) {
			SyncCellToDraft();
			if (_homes) {
				_mode = Mode::Drafts;
			} else {
				ScanHomes(true);
			}
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
			OpenTemplateFile();
		}
		if (_mode != Mode::Browse || !_cell) {
			return;
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, true)) {
			io.KeyShift ? Redo() : Undo();
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, true)) {
			Redo();
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
			SaveTemplate(*_cell, false);
			_requestTab = 5;  // the Template tab shows the result (and asks before overwriting)
		}
		if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P, false)) {
			_paint = !_paint;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			if (!_multi.empty()) {
				_multi.clear();
			} else if (_paint) {
				_paint = false;
			}
		}
	}
}
