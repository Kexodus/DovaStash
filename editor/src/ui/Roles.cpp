// The template being made (docs/EDITOR.md §3.1, EDITOR-UX.md §2.4): the role editor in the Selection panel
// (master, label, categories, ignored, note; displays used or not, armor sets) shared by the 3D view, the plan
// and the tables, and the Template panel (header, cells, keep rules, save target, save). Everything goes through
// dova::core: the draft comes from home::DraftTemplate or home::DraftHome, validation is core::ValidateTemplate
// (+ ValidateLayoutCategories with the loaded categories), the file is written by home::SaveTemplateFile.

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
#include "ui/Common.h"

namespace fs = std::filesystem;

namespace dova::editor::ui
{
	using namespace detail;

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

	int EditorApp::ErrorCount(const OpenCell& a_cell) const
	{
		return static_cast<int>(std::ranges::count_if(a_cell.diagnostics, [](const std::string& d) { return d.starts_with("error"); }));
	}

	int EditorApp::WarningCount(const OpenCell& a_cell) const
	{
		return static_cast<int>(a_cell.diagnostics.size()) - ErrorCount(a_cell);
	}

	bool EditorApp::CategoryEditor(const char* a_id, std::vector<std::string>& a_patterns, bool a_title)
	{
		bool changed = false;
		ImGui::PushID(a_id);
		if (a_title) {
			w::FieldLabel("Categories");
		}
		const float rightX = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
		for (std::size_t i = 0; i < a_patterns.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			const auto* cat = _categorySet ? _categorySet->Find(a_patterns[i]) : nullptr;
			if (i > 0) {
				w::FlowNext(w::ChipWidth(a_patterns[i].c_str(), true), rightX);
			}
			const bool known = cat || a_patterns[i].ends_with("*");
			if (w::ChipClose(a_patterns[i].c_str(), known ? w::Kind::Accent : w::Kind::Error)) {
				a_patterns.erase(a_patterns.begin() + static_cast<std::ptrdiff_t>(i));
				changed = true;
				ImGui::PopID();
				break;
			}
			w::Tip(cat ? cat->name.c_str() : (a_patterns[i].ends_with("*") ? "A group and everything below it" : "Unknown category"));
			ImGui::PopID();
		}
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##add", "+ add", ImGuiComboFlags_HeightLarge)) {
			static std::string filter;
			if (ImGui::IsWindowAppearing()) {
				ImGui::SetKeyboardFocusHere();
				filter.clear();
			}
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputTextWithHint("##filter", "Filter, or a pattern + Enter", &filter);
			if (ImGui::IsItemDeactivatedAfterEdit() && ImGui::IsKeyPressed(ImGuiKey_Enter) && core::IsValidCategoryPattern(core::ToLower(filter))) {
				a_patterns.push_back(core::ToLower(filter));
				changed = true;
				ImGui::CloseCurrentPopup();
			}
			if (_categories.empty()) {
				w::Muted("No Categories/*.json in Data/SKSE/Plugins/DovaStash");
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

	bool EditorApp::ArmorSetEditor(OpenCell& a_cell, std::optional<core::FormRef>& a_set)
	{
		w::FieldLabel("Armor set");
		if (Busy() && !(_armorSets && _armorSetsLo == a_cell.lo)) {
			w::Muted("Reading the armor sets...");  // the load order is busy with a job
			return false;
		}
		const auto& index = ArmorSets(a_cell);
		const auto  label = [&](const core::ArmorSet& a_s) {
            return std::format("{}  ({}/4 slots, {} piece{})", a_s.name, core::SlotGroupCount(a_s.slots), a_s.pieces.size(), a_s.pieces.size() == 1 ? "" : "s");
		};
		std::string current = "Auto: the most complete set the player has";
		if (a_set) {
			const auto s = index.SetOf(*a_set);
			current = s ? label(index.sets[*s]) : a_set->ToString() + " (not in this load order)";
		}
		bool changed = false;
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##armorset", current.c_str(), ImGuiComboFlags_HeightLarge)) {
			if (ImGui::IsWindowAppearing()) {
				ImGui::SetKeyboardFocusHere();
			}
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputTextWithHint("##armorsetfilter", "Set, plugin or piece", &_armorSetFilter);
			if (ImGui::Selectable("Auto: the most complete set the player has", !a_set)) {
				a_set.reset();
				changed = true;
			}
			std::vector<std::size_t> shown;
			for (std::size_t i = 0; i < index.sets.size(); ++i) {
				if (core::ArmorSetMatches(index.sets[i], index.pieces, _armorSetFilter)) {
					shown.push_back(i);
				}
			}
			const auto       chosen = a_set ? index.SetOf(*a_set) : std::nullopt;
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(shown.size()));
			while (clipper.Step()) {
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
					const auto  i = shown[static_cast<std::size_t>(row)];
					const auto& s = index.sets[i];
					ImGui::PushID(static_cast<int>(i));
					if (ImGui::Selectable(label(s).c_str(), chosen == i)) {
						a_set = s.id;
						changed = true;
					}
					if (ImGui::BeginItemTooltip()) {
						ImGui::TextUnformatted(s.plugin.c_str());
						for (const auto p : s.pieces) {
							ImGui::BulletText("%s", index.pieces[p].name.c_str());
						}
						ImGui::EndTooltip();
					}
					ImGui::PopID();
				}
			}
			if (shown.empty()) {
				w::Muted("No set matches.");
			}
			ImGui::EndCombo();
		}
		w::Tip(a_set ? "It wears only this set and fills up as the player gets its pieces; nothing is taken off it" :
		               "Auto: the most complete matching set the player has (its categories, if any, narrow it)");
		return changed;
	}

	void EditorApp::MakeMaster(OpenCell& a_cell, int a_index)
	{
		auto&       layout = a_cell.layout;
		const auto& r = a_cell.info.refs[static_cast<std::size_t>(a_index)];
		const auto  ref = a_cell.lo->ToFormRef(r.key);
		if (layout.IsMaster(ref)) {
			return;
		}
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
		a_cell.dirty = true;
		Revalidate(a_cell);
	}

	void EditorApp::DrawRoleEditor(OpenCell& a_cell, int a_index)
	{
		const auto& r = a_cell.info.refs[static_cast<std::size_t>(a_index)];
		const auto  ref = a_cell.lo->ToFormRef(r.key);
		auto&       layout = a_cell.layout;
		bool        changed = false;
		ImGui::PushID(a_index);
		if ((r.IsContainer() || r.display) && !CellInHome(a_cell)) {
			w::SectionHead("Role");
			w::MutedWrapped(std::format("This cell is not part of \"{}\".", layout.name).c_str());
			if (ImGui::Button("Add this cell to the home")) {
				core::HomeCell hc;
				hc.cell = a_cell.lo->ToFormRef(a_cell.info.key);
				hc.label = a_cell.info.name;
				layout.cells.push_back(std::move(hc));
				a_cell.dirty = true;
				Revalidate(a_cell);
			}
			w::Tip("Lists the cell in the template's cells: its containers and displays can then be used");
			ImGui::PopID();
			return;
		}
		if (r.IsContainer()) {
			w::SectionHead("Role");
			if (layout.IsMaster(ref)) {
				w::Chip("master", w::Kind::Accent);
				ImGui::SameLine();
				w::Muted("takes what has no place");
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
					MakeMaster(a_cell, a_index);
				}
				ImGui::EndDisabled();
				w::Tip("The container that takes everything without a place of its own (and the hotkey's target)");
				auto* b = layout.FindContainer(ref);
				if (!b) {
					ImGui::SameLine();
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
					w::Muted("Not in the template");
				} else {
					w::FieldLabel("Label");
					changed |= ImGui::InputText("##label", &b->label);
					changed |= w::Check("Ignored", b->ignored, "Never used");
					if (r.respawns && !b->ignored) {
						w::Coloured(w::Kind::Error, "Respawns: ignore it");
					}
					if (!b->ignored) {
						changed |= CategoryEditor("cats", b->categories);
						if (!b->forms.empty()) {
							w::Muted(std::format("{} fixed items (kept)", b->forms.size()).c_str());
						}
					}
					w::FieldLabel("Note");
					changed |= ImGui::InputTextMultiline("##note", &b->note, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3.0f));
					w::Tip("For people reading the file");
					ImGui::PushFont(w::Th().smallFont);
					if (w::LinkButton("Remove from the template")) {
						std::erase_if(layout.containers, [&](const core::ContainerBinding& c) { return c.ref == ref; });
						changed = true;
					}
					ImGui::PopFont();
					w::Tip("Delete");
				}
			}
		} else if (r.display) {
			w::SectionHead("Role");
			auto* d = layout.FindDisplay(ref);
			bool  used = d != nullptr;
			if (w::Check("Use this display", used)) {
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
			w::Tip("Listed displays are filled by Dova Stash; the others are left alone");
			if (d) {
				w::FieldLabel("Label");
				changed |= ImGui::InputText("##label", &d->label);
				changed |= w::Check("Ignored", d->ignored, "Never filled");
				// A mannequin with a chosen set wears that set: its categories and "only" are not used (core::PlanDisplays).
				const bool takesCategories = *r.display == core::DisplayKind::Rack || (*r.display == core::DisplayKind::Mannequin && !d->set) ||
				                             *r.display == core::DisplayKind::Bookshelf;
				if (*r.display == core::DisplayKind::Mannequin && !d->ignored) {
					changed |= ArmorSetEditor(a_cell, d->set);
				}
				if (takesCategories && !d->ignored) {
					changed |= CategoryEditor("dcats", d->categories);
					if (d->categories.empty()) {
						w::Muted(*r.display == core::DisplayKind::Rack      ? "Empty: any weapon that fits" :
						         *r.display == core::DisplayKind::Bookshelf ? "Empty: the books most worth showing" :
						                                                      "Empty: it picks an armor set");
					}
				}
				if (!d->items.empty()) {
					w::Muted(std::format("{} items{}", d->items.size(), takesCategories ? "" : " (from its script)").c_str());
					if (takesCategories) {
						changed |= w::Check("Only these items", d->only);
					}
				}
				w::FieldLabel("Note");
				changed |= ImGui::InputTextMultiline("##note", &d->note, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3.0f));
			}
		} else {
			for (const auto& door : a_cell.info.doors) {
				if (door.door != r.key) {
					continue;
				}
				w::SectionHead("Door");
				ImGui::BeginDisabled(!door.destinationCell || Busy());
				if (ImGui::Button("Open that cell")) {
					OpenCellAsync(a_cell.lo, *door.destinationCell, true, true);
				}
				ImGui::EndDisabled();
				w::Tip("The template comes along; Back returns");
			}
		}
		ImGui::PopID();
		if (changed) {
			a_cell.dirty = true;
			Revalidate(a_cell);
		}
	}

	std::vector<std::string> EditorApp::SaveTargets() const
	{
		// The editor's own mod first, then the enabled mods that already hold templates (cached per workspace).
		if (!_saveTargets) {
			auto& cached = _saveTargets.emplace(1, std::string{});
			if (_ws && _ws->Instance()) {
				std::error_code ec;
				for (auto it = _ws->Mods().rbegin(); it != _ws->Mods().rend(); ++it) {
					if (it->separator || it->unmanaged || !it->enabled || core::IEquals(it->name, home::kTemplatesMod)) {
						continue;
					}
					if (fs::is_directory(it->dir / L"SKSE" / L"Plugins" / L"DovaStash" / L"Templates", ec)) {
						cached.push_back(it->name);
					}
				}
			}
		}
		return *_saveTargets;
	}

	fs::path EditorApp::SaveBase() const
	{
		if (!_ws) {
			return {};
		}
		if (_ws->Instance() && !_state.saveTarget.empty()) {
			for (const auto& m : _ws->Mods()) {
				if (m.name == _state.saveTarget && !m.separator && !m.unmanaged) {
					return m.dir;
				}
			}
		}
		return home::SaveBase(*_ws);  // the "Dova Stash Templates" mod, else the Data folder
	}

	fs::path EditorApp::TemplatePath(const OpenCell& a_cell) const
	{
		return home::TemplatePath(SaveBase(), a_cell.layout, HomeNameFor(a_cell));
	}

	std::string EditorApp::HomeNameFor(const OpenCell& a_cell) const
	{
		if (_homes) {
			if (_cellDraft >= 0 && static_cast<std::size_t>(_cellDraft) < _homes->homes.size()) {
				return _homes->homes[static_cast<std::size_t>(_cellDraft)].name;
			}
			// Opened from a file: the rail home whose main cell (or a joined cell) is the template's.
			if (a_cell.layout.cell) {
				const auto main = a_cell.layout.cell->ToString();
				for (const auto& h : _homes->homes) {
					if (core::IEquals(h.cellRef, main) || std::ranges::any_of(h.extra, [&](const auto& e) { return core::IEquals(e.cellRef, main); })) {
						return h.name;
					}
				}
			}
		}
		return a_cell.layout.name;
	}

	std::vector<const home::KnownTemplate*> EditorApp::TemplatesFor(const OpenCell& a_cell) const
	{
		std::vector<const home::KnownTemplate*> out;
		if (!_homes || !_homes->lo) {
			return out;
		}
		auto add = [&](const core::FormRef& a_ref) {
			for (const auto* t : _homes->templates.ForCell(a_ref)) {
				if (std::ranges::find(out, t) == out.end()) {
					out.push_back(t);
				}
			}
		};
		if (a_cell.layout.cell) {
			add(*a_cell.layout.cell);
		}
		for (const auto& c : a_cell.layout.cells) {
			add(c.cell);
		}
		add(a_cell.lo->ToFormRef(a_cell.info.key));
		// Made by a person before auto-drafts (whatever the priority), then priority, then yours, then the newest
		// version: the order the game offers them (core::CompareTemplateRank, as core::ResolveLayout ranks).
		std::ranges::stable_sort(out, [](const home::KnownTemplate* a, const home::KnownTemplate* b) {
			return core::CompareTemplateRank(a->layout, a->user, b->layout, b->user) < 0;
		});
		return out;
	}

	void EditorApp::DrawTemplateList(OpenCell& a_cell)
	{
		// Every template this home has, yours and shipped, the open one marked: one home can have several
		// (a shipped one, yours, another author's) and the game offers the first of this order.
		const auto list = TemplatesFor(a_cell);
		if (list.empty()) {
			return;
		}
		const auto& th = w::Th();
		auto        same = [](const fs::path& a, const std::string& b) {
            std::error_code ec;
            return !b.empty() && fs::equivalent(a, PathFromUtf8(b), ec);
		};
		w::SectionHead(list.size() == 1 ? "This home's template" : std::format("This home's templates  {}", list.size()).c_str(),
			"Saved in one folder per home. In game the first of this list is offered: one someone made before an auto-draft, then highest priority, then yours, then the newest version");
		for (std::size_t i = 0; i < list.size(); ++i) {
			const auto* tp = list[i];
			const bool  open = same(tp->path, a_cell.filePath) || same(tp->path, a_cell.savedPath);
			ImGui::PushID(static_cast<int>(i));
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float  wdt = ImGui::GetContentRegionAvail().x;
			const float  h = th.rowH;
			const bool   clicked = ImGui::InvisibleButton("row", ImVec2(std::max(wdt, 1.0f), h));
			const bool   hovered = ImGui::IsItemHovered();
			ImDrawList*  dl = ImGui::GetWindowDrawList();
			if (open) {
				dl->AddRectFilled(p, ImVec2(p.x + wdt, p.y + h), w::WithAlpha(th.accent, 0.14f), th.radius);
				dl->AddRectFilled(p, ImVec2(p.x + std::round(2.0f * th.scale), p.y + h), th.accent);
			} else if (hovered) {
				dl->AddRectFilled(p, ImVec2(p.x + wdt, p.y + h), th.bg3, th.radius);
			}
			w::Kind      chipKind = w::Kind::Neutral;
			const char*  chip = TemplateWord(*tp, &chipKind);  // the rail's and the title row's words
			const float  chipW = w::ChipWidth(chip, false);
			const auto   detail = std::format("{} · {} · {}", tp->layout.author.empty() ? "no author" : tp->layout.author, tp->layout.version.empty() ? "no version" : tp->layout.version, tp->origin);
			const ImVec2 ns = ImGui::CalcTextSize(tp->layout.name.c_str());
			ImGui::SetCursorScreenPos(ImVec2(p.x + th.pad, p.y + (h - ns.y) * 0.5f));
			ImGui::PushClipRect(p, ImVec2(p.x + wdt - chipW - th.gap, p.y + h), true);
			w::TextWithMuted(tp->layout.name.c_str(), detail.c_str());
			ImGui::PopClipRect();
			ImGui::SetCursorScreenPos(ImVec2(p.x + wdt - chipW, p.y + (h - th.chipH) * 0.5f));
			w::Chip(chip, chipKind);
			ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
			ImGui::Dummy(ImVec2(0, 0));
			if (ImGui::BeginItemTooltip()) {
				ImGui::TextUnformatted(open ? "Open" : "Open this template");
				w::Mono(tp->source.c_str());
				w::Muted(PathToUtf8(tp->path).c_str());
				if (tp->layout.priority != 0) {
					w::Muted(std::format("Priority {}", tp->layout.priority).c_str());
				}
				if (tp->layout.autoDraft) {
					w::Muted("Saved by Draft all homes, not by a person: open it and Save to make it yours");
				}
				ImGui::EndTooltip();
			}
			if (clicked && !open) {
				OpenTemplateAt(PathToUtf8(tp->path));
			}
			ImGui::PopID();
		}
		if (a_cell.filePath.empty() && a_cell.savedPath.empty()) {
			w::Small(a_cell.dirty ? "Open now: a draft, not saved yet." : "Open now: the editor's draft.");
		}
		w::Spacer(th.gap);
	}

	void EditorApp::RequestSave()
	{
		if (!_cell || !_ws) {
			return;
		}
		if (ErrorCount(*_cell) > 0) {
			SetStatus("Not saved: the checks find errors", true);
			_panel = Panel::Checks;
			_state.ui.inspectorOpen = true;
			return;
		}
		std::error_code ec;
		const bool      exists = fs::exists(TemplatePath(*_cell), ec);
		const bool      hasTemplate = _cellDraft >= 0 && _homes && !HomeStatus(_homes->homes[static_cast<std::size_t>(_cellDraft)]).empty() && _cell->filePath.empty();
		if (exists || hasTemplate) {
			_confirmSave = true;  // asks first (DrawSavePopup)
		} else {
			SaveTemplate(*_cell);
		}
	}

	void EditorApp::DrawSavePopup()
	{
		if (_confirmSave && !ImGui::IsPopupOpen("Save the template?")) {
			ImGui::OpenPopup("Save the template?");
		}
		if (ImGui::BeginPopupModal("Save the template?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			if (!_cell) {
				_confirmSave = false;
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return;
			}
			std::error_code ec;
			const auto      path = TemplatePath(*_cell);
			if (fs::exists(path, ec)) {
				w::Coloured(w::Kind::Warn, (PathToUtf8(path) + " exists.").c_str());
				ImGui::TextUnformatted("Overwrite it? The old file is kept as .bak and the version goes up.");
				if (_homes && std::ranges::any_of(_homes->templates.templates, [&](const home::KnownTemplate& tp) { return tp.layout.autoDraft && fs::equivalent(tp.path, path, ec); })) {
					w::Muted("It is an auto-draft (Draft all homes): saved, it is yours.");
				}
			} else {
				ImGui::Text("Save to %s?", PathToUtf8(path).c_str());
			}
			if (_cellDraft >= 0 && _homes) {
				const auto& h = _homes->homes[static_cast<std::size_t>(_cellDraft)];
				const auto  existing = _homes->templates.ForCell(_homes->lo->ToFormRef(h.cell));
				for (const auto* tp : existing) {
					if (tp->path != path) {
						w::Coloured(w::Kind::Info, std::format("This home already has {}: {} ({}).", tp->layout.autoDraft ? "an auto-draft" : "a template", tp->source, tp->origin).c_str());
					}
				}
				if (std::ranges::any_of(existing, [&](const home::KnownTemplate* tp) { return tp->path != path; })) {
					w::Muted("That file is not touched; in game the higher priority, then the newer version, is offered first.");
				}
			}
			w::Spacer();
			if (w::PrimaryButton("Save")) {
				_confirmSave = false;
				ImGui::CloseCurrentPopup();
				SaveTemplate(*_cell);
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				_confirmSave = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void EditorApp::SaveTemplate(OpenCell& a_cell)
	{
		const bool ownMod = _ws && _ws->Instance() && _state.saveTarget.empty();
		const auto r = home::SaveTemplateFile(SaveBase(), a_cell.layout, { .modMeta = ownMod }, HomeNameFor(a_cell));  // a person's save: no auto-draft mark
		if (r.ok) {
			a_cell.dirty = false;
			a_cell.savedPath = PathToUtf8(r.path);
			a_cell.saveMessage = std::format("Saved version {} to {}{}", r.version, a_cell.savedPath, r.replaced ? " (the old file is kept as .bak)" : "");
			if (ownMod) {
				a_cell.saveMessage += std::format(". In MO2: refresh with F5 and enable the \"{}\" mod", home::kTemplatesMod);
			}
			SetStatus(std::format("Saved version {}", r.version));
			Revalidate(a_cell);
			if (a_cell.layout.cell) {
				const auto key = _cellDraft >= 0 && _homes ? _homes->homes[static_cast<std::size_t>(_cellDraft)].cellRef : a_cell.layout.cell->ToString();
				_state.Advance(key, a_cell.layout.name, HomeStage::Saved, a_cell.savedPath);
			}
			if (_homes && _ws) {
				_homes->templates = home::LoadTemplateIndex(*_ws);
				if (_cellDraft >= 0 && static_cast<std::size_t>(_cellDraft) < _homes->entries.size() && _homes->entries[static_cast<std::size_t>(_cellDraft)].draft) {
					auto& e = _homes->entries[static_cast<std::size_t>(_cellDraft)];
					e.draft->layout = a_cell.layout;
					e.saveMessage = a_cell.saveMessage;
				}
			}
		} else {
			a_cell.saveMessage = "Not saved: " + r.error;
			SetStatus(a_cell.saveMessage, true);
		}
	}

	void EditorApp::SaveTemplateAs(OpenCell& a_cell)
	{
		const auto file = PickSaveJsonFile(core::TemplateFileStem(a_cell.layout.name) + ".json");
		if (!file) {
			return;
		}
		a_cell.layout.requiredPlugins = core::ReferencedPlugins(a_cell.layout);
		auto copy = a_cell.layout;
		copy.autoDraft = false;  // a person's copy (DESIGN §6.8)
		if (core::SaveLayoutFile(PathFromUtf8(*file), copy)) {
			a_cell.saveMessage = "Saved a copy to " + *file;
			SetStatus("Saved a copy");
		} else {
			a_cell.saveMessage = "Not saved: cannot write " + *file;
			SetStatus(a_cell.saveMessage, true);
		}
	}

	bool EditorApp::DrawKeepRules(core::Layout& a_layout)
	{
		bool changed = false;
		if (!a_layout.keep) {
			w::Muted("None: the player's own keep rules apply.");
			if (ImGui::Button("Add keep rules")) {
				a_layout.keep.emplace();
				changed = true;
			}
			return changed;
		}
		auto& k = *a_layout.keep;
		bool  inherit = k.Inherits();
		if (w::Check("Also the player's global rules", inherit)) {
			k.inherit = inherit ? std::optional<bool>{} : std::optional<bool>{ false };
			changed = true;
		}
		w::Tip("Off writes \"inherit\": false: only these rules apply in this home");
		auto list = [&](const char* a_id, const char* a_label, std::vector<std::string>& a_items) {
			ImGui::PushID(a_id);
			w::FieldLabel(a_label);
			const float rightX = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
			for (std::size_t i = 0; i < a_items.size(); ++i) {
				ImGui::PushID(static_cast<int>(i));
				if (i > 0) {
					w::FlowNext(w::ChipWidth(a_items[i].c_str(), true), rightX);
				}
				if (w::ChipClose(a_items[i].c_str(), w::Kind::Accent)) {
					a_items.erase(a_items.begin() + static_cast<std::ptrdiff_t>(i));
					changed = true;
					ImGui::PopID();
					break;
				}
				ImGui::PopID();
			}
			static std::map<std::string, std::string> entry;
			auto&                                     text = entry[a_id];
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::InputTextWithHint("##add", "+ category, form ref, equipped, favorited, quest", &text, ImGuiInputTextFlags_EnterReturnsTrue) && !Trim(text).empty()) {
				a_items.emplace_back(Trim(text));
				text.clear();
				changed = true;
			}
			ImGui::PopID();
		};
		list("always", "Always keep", k.always);
		list("never", "Never keep", k.never);
		w::FieldLabel("Quotas");
		int remove = -1;
		for (std::size_t i = 0; i < k.quotas.size(); ++i) {
			auto& q = k.quotas[i];
			ImGui::PushID(static_cast<int>(i));
			if (q.category || (!q.keywordsAny && !q.forms)) {
				std::string cat = q.category.value_or("");
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
				if (ImGui::InputTextWithHint("##cat", "category", &cat)) {
					q.category = cat;
					changed = true;
				}
			} else {
				w::Muted(q.keywordsAny ? "by keywords (kept)" : "by forms (kept)");
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.5f);
			changed |= ImGui::InputInt("##quota", &q.quota, 0);
			ImGui::SameLine();
			if (w::IconButton("rm", w::Icon::Close, "Remove")) {
				remove = static_cast<int>(i);
			}
			ImGui::PopID();
		}
		if (remove >= 0) {
			k.quotas.erase(k.quotas.begin() + remove);
			changed = true;
		}
		ImGui::PushFont(w::Th().smallFont);
		if (w::LinkButton("+ quota")) {
			core::QuotaRule q;
			q.category = "";
			q.quota = 1;
			k.quotas.push_back(std::move(q));
			changed = true;
		}
		w::Tip("Keep up to this many items of a category on the player");
		ImGui::SameLine(0.0f, 2.0f * w::Th().pad);
		if (w::LinkButton("Remove keep rules")) {
			a_layout.keep.reset();
			changed = true;
		}
		ImGui::PopFont();
		return changed;
	}

	void EditorApp::DrawTemplate(OpenCell& a_cell)
	{
		auto&       t = a_cell.layout;
		bool        changed = false;
		const auto& th = w::Th();
		// The fields scroll; Save stays pinned under them like a form's footer (clear of the rail's collapse handle).
		const float footerH = th.inputH + 2.0f * th.gap + 1.0f;
		const float bodyH = std::max(ImGui::GetContentRegionAvail().y - footerH - 28.0f * th.scale, 80.0f * th.scale);
		ImGui::BeginChild("tplbody", ImVec2(0, bodyH), ImGuiChildFlags_None, ImGuiWindowFlags_None);
		ImGui::PushItemWidth(-FLT_MIN);
		DrawTemplateList(a_cell);
		w::SectionHead("Template");
		w::FieldLabel("Name");
		changed |= ImGui::InputText("##name", &t.name);
		w::FieldLabel("Author");
		if (ImGui::InputText("##author", &t.author)) {
			_templateAuthor = t.author;
			_state.author = t.author;
			changed = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			_state.Save();
		}
		const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
		ImGui::BeginGroup();
		w::FieldLabel("Version");
		ImGui::SetNextItemWidth(half);
		changed |= ImGui::InputText("##version", &t.version);
		ImGui::EndGroup();
		ImGui::SameLine();
		ImGui::BeginGroup();
		w::FieldLabel("Priority");
		ImGui::SetNextItemWidth(half);
		changed |= ImGui::InputInt("##priority", &t.priority, 0);
		ImGui::EndGroup();
		w::FieldLabel("Note");
		changed |= ImGui::InputTextMultiline("##note", &t.note, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3.0f));
		{
			const auto master = std::ranges::find_if(a_cell.info.refs, [&](const home::PlacedRef& p) { return t.master && a_cell.lo->ToFormRef(p.key) == *t.master; });
			const auto withCategories = std::ranges::count_if(t.containers, [](const core::ContainerBinding& b) { return !b.ignored && !b.categories.empty(); });
			const auto ignored = std::ranges::count_if(t.containers, [](const core::ContainerBinding& b) { return b.ignored; });
			ImGui::PushTextWrapPos(0.0f);
			w::MutedWrapped(std::format("Master {} · {} containers ({} with categories, {} ignored) · {} displays", master != a_cell.info.refs.end() ? GameName(*master) : "none",
				t.containers.size(), withCategories, ignored, t.displays.size())
								.c_str());
			w::MutedWrapped(("Requires " + Join(t.requiredPlugins)).c_str());
			ImGui::PopTextWrapPos();
		}

		w::SectionHead("Cells");
		{
			const auto openRef = a_cell.lo->ToFormRef(a_cell.info.key);
			w::FieldLabel(t.cell && *t.cell == openRef ? "Main · open" : "Main");
			w::Mono((t.cell ? t.cell->ToString() : std::string("-")).c_str());
			int remove = -1;
			for (std::size_t i = 0; i < t.cells.size(); ++i) {
				auto& c = t.cells[i];
				ImGui::PushID(static_cast<int>(i));
				w::FieldLabel(c.cell == openRef ? "Joined · open" : "Joined");
				w::Mono(c.cell.ToString().c_str());
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
				changed |= ImGui::InputTextWithHint("##label", "Label (what players see)", &c.label);
				ImGui::SameLine();
				if (w::IconButton("leave", w::Icon::Close, "Leave the home")) {
					remove = static_cast<int>(i);
				}
				ImGui::PopID();
			}
			if (remove >= 0) {
				t.cells.erase(t.cells.begin() + remove);
				changed = true;
			}
			if (t.cells.empty()) {
				w::Muted("One cell. A cellar joins from Doors.");
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
				w::Tip("Adds every container of the open cell the template does not list yet");
			}
			// The open cell's facts (the eligibility checks use them).
			std::string facts = std::format("{} · {} · last edited by {}", a_cell.info.editorId, a_cell.info.interior ? "interior" : "exterior", a_cell.info.lastOverride);
			facts += "\nLocation " + (a_cell.info.location ? a_cell.info.locationLabel : std::string("none"));
			if (!a_cell.info.locationKeywords.empty()) {
				facts += " [" + Join(a_cell.info.locationKeywords) + "]";
			}
			facts += "\nOwner " + (a_cell.info.owner ? a_cell.info.ownerLabel : std::string("none")) + (a_cell.info.ownedByPlayer ? " (player)" : "");
			facts += "\nZone " + (a_cell.info.encounterZone.empty() ? std::string("none") : a_cell.info.encounterZone) + (a_cell.info.neverResets ? " (never resets)" : "");
			w::Spacer(2.0f);
			ImGui::PushFont(w::Th().smallFont);
			ImGui::PushTextWrapPos(0.0f);
			w::MutedWrapped(facts.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopFont();
		}

		w::SectionHead("Keep rules");
		changed |= DrawKeepRules(t);

		w::SectionHead("Save");
		{
			const auto targets = SaveTargets();
			auto       name = [](const std::string& a_t) { return a_t.empty() ? std::string(home::kTemplatesMod) : a_t; };
			w::FieldLabel("Save to");
			if (ImGui::BeginCombo("##target", name(_state.saveTarget).c_str())) {
				for (const auto& tg : targets) {
					if (ImGui::Selectable(name(tg).c_str(), tg == _state.saveTarget)) {
						_state.saveTarget = tg;
						_state.Save();
					}
				}
				ImGui::EndCombo();
			}
			w::Tip(_state.saveTarget.empty() ? "A mod of its own, never MO2's overwrite folder: clearing overwrite cannot delete it" : "An existing mod that holds templates");
			ImGui::PushTextWrapPos(0.0f);
			w::MutedWrapped(PathToUtf8(TemplatePath(a_cell)).c_str());
			if (!a_cell.saveMessage.empty()) {
				w::ColouredWrapped(a_cell.saveMessage.starts_with("Not saved") ? w::Kind::Error : w::Kind::Ok, a_cell.saveMessage.c_str());
			}
			ImGui::PopTextWrapPos();
		}
		if (ImGui::CollapsingHeader("JSON")) {
			w::PushFont(w::Font::Mono);  // at its own size (ImGui::PushFont would keep the body size)
			ImGui::InputTextMultiline("##json", a_cell.json.data(), a_cell.json.size() + 1, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 16.0f), ImGuiInputTextFlags_ReadOnly);
			w::PopFont();
		}
		ImGui::PopItemWidth();
		ImGui::EndChild();

		// The footer: Save, Save as..., Copy JSON.
		{
			const ImVec2 p = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y + 1.0f), th.line);
			ImGui::Dummy(ImVec2(0.0f, th.gap));
			const int errors = ErrorCount(a_cell);
			if (w::PrimaryButton("Save", errors == 0, "The checks find errors: see Checks")) {
				RequestSave();
			}
			if (errors == 0) {
				w::TipShortcut(PathToUtf8(TemplatePath(a_cell)).c_str(), "Ctrl+S");
			}
			ImGui::SameLine(0.0f, th.gap);
			if (w::ToolButton("Save as...", false, "A copy anywhere; the template stays where it is saved")) {
				SaveTemplateAs(a_cell);
			}
			ImGui::SameLine(0.0f, 2.0f * th.scale);
			if (w::ToolButton("Copy JSON", false, "The template as JSON, to the clipboard")) {
				ImGui::SetClipboardText(a_cell.json.c_str());
			}
		}
		if (changed) {
			a_cell.dirty = true;
			Revalidate(a_cell);
		}
	}
}
