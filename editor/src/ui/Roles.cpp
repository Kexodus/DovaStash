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
#include "ui/App.h"

namespace fs = std::filesystem;

namespace dova::editor::ui
{
	namespace
	{
		std::string GameName(const home::PlacedRef& a_ref) { return a_ref.baseName.empty() ? a_ref.baseEditorId : a_ref.baseName; }

		const char* const kTemplatesDir = "SKSE\\Plugins\\DovaStash\\Templates";
		const char* const kCategoriesDir = "SKSE\\Plugins\\DovaStash\\Categories";
		const char* const kTemplatesMod = "Dova Stash Templates";
	}

	void EditorApp::LoadCategories()
	{
		if (_categoriesLoaded || !_ws) {
			return;
		}
		_categoriesLoaded = true;
		// Every Categories/*.json of the merged Data folder (a later root wins a file name), in name order, as the DLL loads them.
		std::map<std::string, fs::path> files;
		std::error_code                 ec;
		auto                            addDir = [&](const fs::path& a_root) {
            const auto dir = a_root / PathFromUtf8(kCategoriesDir);
            if (!fs::is_directory(dir, ec)) {
                return;
            }
            for (const auto& e : fs::directory_iterator(dir, ec)) {
                if (e.is_regular_file(ec) && IEndsWith(PathToUtf8(e.path().filename()), ".json")) {
                    files[core::ToLower(PathToUtf8(e.path().filename()))] = e.path();
                    _categorySource = PathToUtf8(dir);
                }
            }
		};
		for (const auto& root : _ws->Files().Roots()) {
			addDir(root);
		}
		// Dova Stash itself not enabled in this profile: the folder the editor ships in, else the mod named
		// "Dova Stash" (enabled or not), else any mod that has the default set.
		if (files.empty()) {
			wchar_t exe[MAX_PATH]{};
			if (::GetModuleFileNameW(nullptr, exe, MAX_PATH) > 0) {
				addDir(fs::path(exe).parent_path());
			}
		}
		if (files.empty() && _ws->Instance()) {
			for (const auto& m : _ws->Mods()) {
				if (core::IEquals(m.name, "Dova Stash")) {
					addDir(m.dir);
				}
			}
			for (const auto& m : _ws->Mods()) {
				if (files.empty() && fs::exists(m.dir / PathFromUtf8(kCategoriesDir) / L"00_Default.json", ec)) {
					addDir(m.dir);
				}
			}
		}
		core::Diagnostics                     diags;
		std::vector<std::vector<core::CategoryDef>> parsed;
		for (const auto& [name, path] : files) {
			if (const auto text = ReadTextFile(path)) {
				parsed.push_back(core::ParseCategoryText(*text, name, diags));
			}
		}
		_categories = core::MergeCategoryFiles(parsed, diags);
		if (!_categories.empty()) {
			_categorySet = core::CategorySet::Compile(_categories, [](const core::FormRef&) { return std::vector<core::FormRef>{}; }, diags);
		}
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

	bool EditorApp::CategoryEditor(const char* a_id, std::vector<std::string>& a_patterns)
	{
		bool changed = false;
		ImGui::PushID(a_id);
		ImGui::TextDisabled("Categories");
		if (a_patterns.empty()) {
			ImGui::SameLine();
			ImGui::TextDisabled("(none)");
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
					OpenCellAsync(a_cell.lo, *door.destinationCell);
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
		const auto author = core::TemplateFileStem(a_cell.layout.author.empty() ? std::string("Unknown") : a_cell.layout.author);
		const auto file = core::TemplateFileStem(a_cell.layout.name) + ".json";
		fs::path   base;
		if (_ws && _ws->Instance()) {
			base = _ws->Instance()->modsDir / PathFromUtf8(kTemplatesMod);
		} else if (_ws) {
			base = _ws->DataDir();
		}
		return base / PathFromUtf8(kTemplatesDir) / PathFromUtf8(author) / PathFromUtf8(file);
	}

	void EditorApp::SaveTemplate(OpenCell& a_cell, bool a_overwrite)
	{
		const auto      path = TemplatePath(a_cell);
		std::error_code ec;
		if (fs::exists(path, ec) && !a_overwrite) {
			_confirmOverwrite = true;
			return;
		}
		_confirmOverwrite = false;
		core::Layout out = a_cell.layout;
		out.requiredPlugins = core::ReferencedPlugins(out);
		if (fs::exists(path, ec)) {
			// As the DLL does: back up the old file, raise its version.
			core::Diagnostics diags;
			if (const auto old = core::LoadLayoutFile(path, "old", diags)) {
				if (core::CompareVersionStrings(out.version, old->version) != std::strong_ordering::greater) {
					out.version = core::NextPatchVersion(old->version);
				}
			}
			const auto stamp = std::format("{:%Y%m%d-%H%M%S}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
			fs::copy_file(path, fs::path(path.native() + L"." + Widen(stamp) + L".bak"), fs::copy_options::overwrite_existing, ec);
		}
		if (_ws && _ws->Instance()) {
			const auto mod = _ws->Instance()->modsDir / PathFromUtf8(kTemplatesMod);
			if (!fs::exists(mod / L"meta.ini", ec)) {
				fs::create_directories(mod, ec);
				std::ofstream meta(mod / L"meta.ini", std::ios::binary);
				meta << "[General]\r\nmodid=0\r\nversion=1.0.0\r\ncomments=Templates made with Dova Stash Editor\r\n";
			}
		}
		fs::create_directories(path.parent_path(), ec);
		if (core::SaveLayoutFile(path, out)) {
			a_cell.layout.version = out.version;
			a_cell.layout.requiredPlugins = out.requiredPlugins;
			a_cell.dirty = false;
			a_cell.savedPath = PathToUtf8(path);
			a_cell.saveMessage = std::format("Saved version {} to {}", out.version, a_cell.savedPath);
			if (_ws && _ws->Instance()) {
				a_cell.saveMessage += std::format(" (in MO2: refresh with F5 and enable the \"{}\" mod)", kTemplatesMod);
			}
			Revalidate(a_cell);
		} else {
			a_cell.saveMessage = "Could not write " + PathToUtf8(path);
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
			ImGui::TextDisabled("A mod of its own (\"%s\"), never MO2's overwrite folder: clearing overwrite cannot delete it.", kTemplatesMod);
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
