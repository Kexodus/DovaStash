// The template being made (docs/EDITOR.md §3.1): the role editor in the details pane (master, label,
// categories, ignored, note; displays used or not) shared by the 3D view, the floor plan and the tables,
// and the Template tab (header, problems, save). Everything goes through dova::core: the draft comes from
// home::DraftTemplate, validation is core::ValidateTemplate (+ ValidateLayoutCategories with the loaded
// categories), the file is core::SerializeLayout written by core::SaveLayoutFile.

#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <map>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "Util.h"
#include "dova/core/Template.h"
#include "dova/core/Version.h"
#include "home/DovaData.h"
#include "home/TemplateFile.h"
#include "ui/App.h"

namespace fs = std::filesystem;

namespace dova::editor::ui
{
	namespace
	{
		std::string GameName(const home::PlacedRef& a_ref) { return a_ref.baseName.empty() ? a_ref.baseEditorId : a_ref.baseName; }

	}

	void EditorApp::LoadCategories()
	{
		if (_categoriesLoaded || !_ws) {
			return;
		}
		_categoriesLoaded = true;
		auto data = home::LoadCategories(*_ws);
		_categories = std::move(data.defs);
		_categorySet = std::move(data.set);
		_categorySource = std::move(data.source);
		auto rules = home::LoadDraftRules(*_ws);
		_draftRules = std::move(rules.rules);
		_draftRulesSource = std::move(rules.source);
	}

	void EditorApp::Revalidate(OpenCell& a_cell)
	{
		LoadCategories();
		a_cell.layout.requiredPlugins = core::ReferencedPlugins(a_cell.layout);
		core::Diagnostics diags;
		core::ValidateTemplate(a_cell.layout, "template", diags);
		if (_categorySet) {
			core::ValidateLayoutCategories(a_cell.layout, *_categorySet, "template", diags);
		}
		a_cell.diagnostics.clear();
		for (const auto& d : diags.Items()) {
			a_cell.diagnostics.push_back(d.ToString());
		}
		a_cell.json = core::SerializeLayout(a_cell.layout);
	}

	bool EditorApp::CategoryEditor(const char* a_id, std::vector<std::string>& a_patterns, bool a_title)
	{
		bool changed = false;
		ImGui::PushID(a_id);
		if (a_title) {
			ImGui::TextDisabled("Categories");
			if (a_patterns.empty()) {
				ImGui::SameLine();
				ImGui::TextDisabled("(none)");
			}
		}
		for (std::size_t i = 0; i < a_patterns.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			const auto* cat = _categorySet ? _categorySet->Find(a_patterns[i]) : nullptr;
			if (ImGui::SmallButton(std::format("{}  x", a_patterns[i]).c_str())) {
				a_patterns.erase(a_patterns.begin() + static_cast<std::ptrdiff_t>(i));
				changed = true;
				ImGui::PopID();
				break;
			}
			ImGui::SetItemTooltip("%s\nClick to remove", cat ? cat->name.c_str() : (a_patterns[i].ends_with("*") ? "a group and everything below it" : "unknown category"));
			ImGui::PopID();
		}
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##add", "Add a category...", ImGuiComboFlags_HeightLarge)) {
			static std::string filter;
			if (ImGui::IsWindowAppearing()) {
				ImGui::SetKeyboardFocusHere();
				filter.clear();
			}
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputTextWithHint("##filter", "filter, or type a pattern and press Enter", &filter);
			if (ImGui::IsItemDeactivatedAfterEdit() && ImGui::IsKeyPressed(ImGuiKey_Enter) && core::IsValidCategoryPattern(core::ToLower(filter))) {
				a_patterns.push_back(core::ToLower(filter));
				changed = true;
				ImGui::CloseCurrentPopup();
			}
			if (_categories.empty()) {
				ImGui::TextDisabled("No Categories/*.json found in Data/SKSE/Plugins/DovaStash");
			}
			for (const auto& c : _categories) {
				if (!filter.empty() && !IContains(c.id, filter) && !IContains(c.name, filter)) {
					continue;
				}
				const bool group = c.match.Empty();
				const auto pattern = group ? c.id + ".*" : c.id;
				const bool present = std::ranges::find(a_patterns, pattern) != a_patterns.end();
				const auto label = std::format("{}{}  {}", std::string(static_cast<std::size_t>(core::CategoryDepth(c.id) - 1) * 2, ' '), pattern, c.name);
				ImGui::BeginDisabled(present);
				if (ImGui::Selectable(label.c_str())) {
					a_patterns.push_back(pattern);
					changed = true;
				}
				ImGui::EndDisabled();
			}
			ImGui::EndCombo();
		}
		ImGui::PopID();
		return changed;
	}

	void EditorApp::DrawRoleEditor(OpenCell& a_cell, int a_index)
	{
		const auto& r = a_cell.info.refs[static_cast<std::size_t>(a_index)];
		const auto  ref = a_cell.lo->ToFormRef(r.key);
		auto&       layout = a_cell.layout;
		bool        changed = false;
		ImGui::PushID(a_index);
		if ((r.IsContainer() || r.display) && !CellInHome(a_cell)) {
			ImGui::SeparatorText("In the template");
			ImGui::TextWrapped("This cell is not part of the home \"%s\".", layout.name.c_str());
			if (ImGui::Button("Add this cell to the home")) {
				core::HomeCell hc;
				hc.cell = a_cell.lo->ToFormRef(a_cell.info.key);
				hc.label = a_cell.info.name;
				layout.cells.push_back(std::move(hc));
				a_cell.dirty = true;
				Revalidate(a_cell);
			}
			ImGui::SetItemTooltip("Lists the cell in the template's `cells` (DESIGN 6.7): its containers and displays can then be used.");
			ImGui::PopID();
			return;
		}
		if (r.IsContainer()) {
			ImGui::SeparatorText("In the template");
			const bool master = layout.IsMaster(ref);
			if (master) {
				ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "Master container");
				ImGui::TextWrapped("Everything that has no other place goes here.");
				if (ImGui::Button("Not the master")) {
					layout.master.reset();
					core::ContainerBinding b;
					b.ref = ref;
					b.label = GameName(r);
					layout.containers.push_back(std::move(b));
					changed = true;
				}
			} else {
				ImGui::BeginDisabled(r.respawns);
				if (ImGui::Button("Make master")) {
					if (layout.master) {
						// The old master becomes a plain container again.
						core::ContainerBinding old;
						old.ref = *layout.master;
						for (const auto& p : a_cell.info.refs) {
							if (a_cell.lo->ToFormRef(p.key) == *layout.master) {
								old.label = GameName(p);
							}
						}
						layout.containers.push_back(std::move(old));
					}
					std::erase_if(layout.containers, [&](const core::ContainerBinding& b) { return b.ref == ref; });
					layout.master = ref;
					changed = true;
				}
				ImGui::EndDisabled();
				ImGui::SetItemTooltip("The container that takes everything without a place of its own (and the hotkey's target).");
				auto* b = layout.FindContainer(ref);
				if (!b) {
					ImGui::TextDisabled("Not listed in the template.");
					if (ImGui::Button("Add to the template")) {
						core::ContainerBinding nb;
						nb.ref = ref;
						nb.label = GameName(r);
						if (r.respawns) {
							nb.ignored = true;
							nb.note = "Respawns: what you store here can disappear.";
						}
						layout.containers.push_back(std::move(nb));
						changed = true;
					}
				} else {
					ImGui::TextDisabled("Label");
					ImGui::SetNextItemWidth(-FLT_MIN);
					changed |= ImGui::InputText("##label", &b->label);
					changed |= ImGui::Checkbox("Ignored (never used)", &b->ignored);
					if (r.respawns && !b->ignored) {
						ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Respawns: ignore it.");
					}
					if (!b->ignored) {
						changed |= CategoryEditor("cats", b->categories);
						if (!b->forms.empty()) {
							ImGui::TextDisabled("%zu fixed items (kept)", b->forms.size());
						}
					}
					ImGui::TextDisabled("Note (for people reading the file)");
					changed |= ImGui::InputTextMultiline("##note", &b->note, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3.0f));
					if (ImGui::SmallButton("Remove from the template")) {
						std::erase_if(layout.containers, [&](const core::ContainerBinding& c) { return c.ref == ref; });
						changed = true;
					}
				}
			}
		} else if (r.display) {
			ImGui::SeparatorText("In the template");
			auto* d = layout.FindDisplay(ref);
			bool  used = d != nullptr;
			if (ImGui::Checkbox("Use this display", &used)) {
				if (used) {
					core::DisplayBinding nd;
					nd.ref = ref;
					nd.kind = r.display;
					nd.label = GameName(r);
					if (*r.display == core::DisplayKind::Item || *r.display == core::DisplayKind::Case) {
						for (const auto& item : r.displayItems) {
							nd.items.push_back(a_cell.lo->ToFormRef(item));
						}
					}
					layout.displays.push_back(std::move(nd));
				} else {
					std::erase_if(layout.displays, [&](const core::DisplayBinding& x) { return x.ref == ref; });
				}
				changed = true;
				d = layout.FindDisplay(ref);
			}
			ImGui::SetItemTooltip("Listed displays are filled by Dova Stash; the others are left alone.");
			if (d) {
				ImGui::TextDisabled("Label");
				ImGui::SetNextItemWidth(-FLT_MIN);
				changed |= ImGui::InputText("##label", &d->label);
				changed |= ImGui::Checkbox("Ignored (never filled)", &d->ignored);
				const bool takesCategories = *r.display == core::DisplayKind::Rack || *r.display == core::DisplayKind::Mannequin;
				if (takesCategories && !d->ignored) {
					changed |= CategoryEditor("dcats", d->categories);
					ImGui::TextDisabled(*r.display == core::DisplayKind::Rack ? "Empty: any weapon that fits the rack." : "Empty: the mannequin picks an armor set.");
				}
				if (!d->items.empty()) {
					ImGui::TextDisabled("%zu items%s", d->items.size(), takesCategories ? "" : " (from its script)");
					if (takesCategories) {
						changed |= ImGui::Checkbox("Only these items", &d->only);
					}
				}
				ImGui::TextDisabled("Note");
				changed |= ImGui::InputTextMultiline("##note", &d->note, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3.0f));
			}
		} else {
			for (const auto& door : a_cell.info.doors) {
				if (door.door != r.key) {
					continue;
				}
				ImGui::SeparatorText("Door");
				ImGui::TextWrapped("Leads to %s", door.destinationLabel.c_str());
				ImGui::BeginDisabled(!door.destinationCell || _job.valid());
				if (ImGui::Button("Open that cell")) {
					OpenCellAsync(a_cell.lo, *door.destinationCell, true, true);
				}
				ImGui::EndDisabled();
			}
		}
		ImGui::PopID();
		if (changed) {
			a_cell.dirty = true;
			Revalidate(a_cell);
		}
	}

	fs::path EditorApp::TemplatePath(const OpenCell& a_cell) const
	{
		fs::path base;
		if (_ws && _ws->Instance()) {
			base = _ws->Instance()->modsDir / PathFromUtf8(home::kTemplatesMod);
		} else if (_ws) {
			base = _ws->DataDir();
		}
		return home::TemplatePath(base, a_cell.layout);
	}

	void EditorApp::SaveTemplate(OpenCell& a_cell, bool a_overwrite)
	{
		std::error_code ec;
		if (fs::exists(TemplatePath(a_cell), ec) && !a_overwrite) {
			_confirmOverwrite = true;
			return;
		}
		_confirmOverwrite = false;
		const bool mod = _ws && _ws->Instance();
		const auto base = mod ? _ws->Instance()->modsDir / PathFromUtf8(home::kTemplatesMod) : (_ws ? _ws->DataDir() : fs::path{});
		const auto r = home::SaveTemplateFile(base, a_cell.layout, mod);
		if (r.ok) {
			a_cell.dirty = false;
			a_cell.savedPath = PathToUtf8(r.path);
			a_cell.saveMessage = std::format("Saved version {} to {}{}", r.version, a_cell.savedPath, r.replaced ? " (the old file is kept as .bak)" : "");
			if (mod) {
				a_cell.saveMessage += std::format(". In MO2: refresh with F5 and enable the \"{}\" mod", home::kTemplatesMod);
			}
			Revalidate(a_cell);
			if (a_cell.layout.cell) {
				_state.Advance(a_cell.layout.cell->ToString(), a_cell.layout.name, HomeStage::Saved, a_cell.savedPath);
			}
			if (_homes && _ws) {
				_homes->templates = home::LoadTemplateIndex(*_ws);
				if (_cellDraft >= 0 && static_cast<std::size_t>(_cellDraft) < _homes->entries.size() && _homes->entries[static_cast<std::size_t>(_cellDraft)].draft) {
					_homes->entries[static_cast<std::size_t>(_cellDraft)].draft->layout = a_cell.layout;
				}
			}
		} else {
			a_cell.saveMessage = r.error;
		}
	}

	void EditorApp::DrawTemplate(OpenCell& a_cell)
	{
		auto& t = a_cell.layout;
		bool  changed = false;
		if (ImGui::BeginTable("templateheader", 2, ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("field", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6.0f);
			ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
			auto row = [&](const char* a_label) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(a_label);
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-FLT_MIN);
			};
			row("Name");
			changed |= ImGui::InputText("##name", &t.name);
			row("Author");
			if (ImGui::InputText("##author", &t.author)) {
				_templateAuthor = t.author;
				changed = true;
			}
			row("Version");
			changed |= ImGui::InputText("##version", &t.version);
			row("Priority");
			changed |= ImGui::InputInt("##priority", &t.priority);
			row("Note");
			changed |= ImGui::InputTextMultiline("##note", &t.note, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3.0f));
			ImGui::EndTable();
		}
		const auto master = std::ranges::find_if(a_cell.info.refs, [&](const home::PlacedRef& p) { return t.master && a_cell.lo->ToFormRef(p.key) == *t.master; });
		const auto withCategories = std::ranges::count_if(t.containers, [](const core::ContainerBinding& b) { return !b.ignored && !b.categories.empty(); });
		const auto ignored = std::ranges::count_if(t.containers, [](const core::ContainerBinding& b) { return b.ignored; });
		ImGui::Text("Cell %s   Master: %s   Containers: %zu (%lld with categories, %lld ignored)   Displays used: %zu", t.cell ? t.cell->ToString().c_str() : "-",
			master != a_cell.info.refs.end() ? GameName(*master).c_str() : "none", t.containers.size(), static_cast<long long>(withCategories),
			static_cast<long long>(ignored), t.displays.size());
		std::string requires_;
		for (const auto& p : t.requiredPlugins) {
			requires_ += (requires_.empty() ? "" : ", ") + p;
		}
		ImGui::TextDisabled("Requires %s", requires_.c_str());
		if (!_categorySource.empty()) {
			ImGui::TextDisabled("%zu categories from %s", _categories.size(), _categorySource.c_str());
		}

		ImGui::SeparatorText("Cells");
		{
			const auto openRef = a_cell.lo->ToFormRef(a_cell.info.key);
			ImGui::Text("Main: %s", t.cell ? t.cell->ToString().c_str() : "-");
			if (t.cell && *t.cell == openRef) {
				ImGui::SameLine();
				ImGui::TextDisabled("(open)");
			}
			int remove = -1;
			for (std::size_t i = 0; i < t.cells.size(); ++i) {
				auto& c = t.cells[i];
				ImGui::PushID(static_cast<int>(i));
				ImGui::Text("%s", c.cell.ToString().c_str());
				ImGui::SameLine();
				ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
				changed |= ImGui::InputTextWithHint("##label", "label (what players see)", &c.label);
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove")) {
					remove = static_cast<int>(i);
				}
				if (c.cell == openRef) {
					ImGui::SameLine();
					ImGui::TextDisabled("(open)");
				}
				ImGui::PopID();
			}
			if (remove >= 0) {
				t.cells.erase(t.cells.begin() + remove);
				changed = true;
			}
			if (t.cells.empty()) {
				ImGui::TextDisabled("One cell. A cellar or the grounds outside join from the Doors tab (\"In the home\").");
			}
			if (CellInHome(a_cell) && !(t.cell && *t.cell == openRef)) {
				if (ImGui::Button("List this cell's containers")) {
					// The draft's container entries for this cell (labels, respawning and upgrade containers ignored).
					core::Diagnostics diags;
					const auto        draft = home::DraftTemplate(*a_cell.lo, a_cell.info, diags);
					auto              entries = draft.containers;
					if (draft.master) {
						core::ContainerBinding m;
						m.ref = *draft.master;
						for (const auto& p : a_cell.info.refs) {
							if (a_cell.lo->ToFormRef(p.key) == m.ref) {
								m.label = GameName(p);
							}
						}
						entries.push_back(std::move(m));
					}
					for (auto& e : entries) {
						if (!t.FindContainer(e.ref) && !t.IsMaster(e.ref)) {
							t.containers.push_back(std::move(e));
						}
					}
					changed = true;
				}
				ImGui::SetItemTooltip("Adds every container of the open cell that the template does not list yet.");
			}
		}

		ImGui::SeparatorText("Problems");
		if (a_cell.diagnostics.empty()) {
			ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.5f, 1.0f), "None: the template passes the same checks as the DLL's.");
		}
		for (const auto& d : a_cell.diagnostics) {
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s", d.c_str());
		}

		ImGui::SeparatorText("Save");
		const auto path = PathToUtf8(TemplatePath(a_cell));
		ImGui::TextWrapped("Saves to %s", path.c_str());
		if (_ws && _ws->Instance()) {
			ImGui::TextDisabled("A mod of its own (\"%s\"), never MO2's overwrite folder: clearing overwrite cannot delete it.", home::kTemplatesMod);
		}
		if (_confirmOverwrite) {
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "That file exists. Overwrite it (the old one is kept as .bak, the version goes up)?");
			if (ImGui::Button("Overwrite")) {
				SaveTemplate(a_cell, true);
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) {
				_confirmOverwrite = false;
			}
		} else if (ImGui::Button("Save template")) {
			SaveTemplate(a_cell, false);
		}
		ImGui::SameLine();
		if (ImGui::Button("Copy JSON")) {
			ImGui::SetClipboardText(a_cell.json.c_str());
		}
		if (!a_cell.saveMessage.empty()) {
			ImGui::TextWrapped("%s", a_cell.saveMessage.c_str());
		}
		ImGui::InputTextMultiline("##json", a_cell.json.data(), a_cell.json.size() + 1, ImGui::GetContentRegionAvail(), ImGuiInputTextFlags_ReadOnly);
		if (changed) {
			a_cell.dirty = true;
			Revalidate(a_cell);
		}
	}
}
