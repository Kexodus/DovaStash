// "Where items go" (docs/EDITOR.md §3.5): the Preview tab runs Dova Stash's own planner (core::PreviewWhereItemsGo:
// SortPlanner + the displays planner) over a sample inventory against the template being made, and shows per
// container what it would receive. Containers are coloured by fill (or by category) in the 3D view and on the plan,
// with a legend; selecting a container shows its predicted items in the details pane. The preview re-runs by
// itself whenever the template changes.

#include <algorithm>
#include <chrono>
#include <format>

#include <imgui.h>

#include "Util.h"
#include "home/Preview.h"
#include "ui/App.h"

namespace dova::editor::ui
{
	namespace
	{
		constexpr std::uint32_t kFillEmpty = IM_COL32(150, 150, 165, 255);
		constexpr std::uint32_t kFillLight = IM_COL32(110, 205, 120, 255);
		constexpr std::uint32_t kFillHeavy = IM_COL32(245, 165, 50, 255);
		constexpr std::uint32_t kFillMaster = IM_COL32(230, 90, 210, 255);
		constexpr std::uint32_t kMasterColour = IM_COL32(230, 90, 210, 255);  // as the fill colours' master
		constexpr std::int32_t  kHeavyAt = 20;  // copies: "many items"

		ImVec4 ToVec(std::uint32_t a_c)
		{
			return ImVec4(static_cast<float>(a_c & 0xFF) / 255.0f, static_cast<float>((a_c >> 8) & 0xFF) / 255.0f, static_cast<float>((a_c >> 16) & 0xFF) / 255.0f, 1.0f);
		}

		std::string RefName(const home::PlacedRef& a_ref) { return a_ref.baseName.empty() ? a_ref.baseEditorId : a_ref.baseName; }

		void Swatch(std::uint32_t a_colour, const char* a_label)
		{
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float  s = ImGui::GetFontSize() * 0.8f;
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + 2.0f), ImVec2(p.x + s, p.y + 2.0f + s), a_colour, 2.0f);
			ImGui::Dummy(ImVec2(s, s));
			ImGui::SameLine();
			ImGui::TextUnformatted(a_label);
		}
	}

	std::uint32_t CategoryColour(std::string_view a_pattern)
	{
		struct Hue
		{
			const char*   group;
			std::uint8_t  r, g, b;
		};
		static constexpr Hue hues[] = { { "weapons", 225, 85, 70 }, { "armor", 85, 140, 230 }, { "ammo", 240, 150, 60 }, { "alchemy", 90, 200, 105 },
			{ "books", 205, 170, 110 }, { "scrolls", 190, 150, 230 }, { "soulgems", 165, 105, 235 }, { "misc", 110, 195, 195 }, { "keys", 235, 215, 80 } };
		const auto top = a_pattern.substr(0, a_pattern.find('.'));
		Hue        h{ "", 170, 170, 170 };
		for (const auto& x : hues) {
			if (top == x.group) {
				h = x;
			}
		}
		// Subcategories: lighter or darker by a hash of the rest, so siblings differ.
		const auto   rest = a_pattern.size() > top.size() ? a_pattern.substr(top.size()) : std::string_view{};
		std::uint32_t hash = 2166136261u;
		for (const char c : rest) {
			hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
		}
		const float k = rest.empty() || rest == ".*" ? 1.0f : 0.72f + static_cast<float>(hash % 7) * 0.09f;
		auto        ch = [&](std::uint8_t v) { return static_cast<int>(std::clamp(static_cast<float>(v) * k, 30.0f, 255.0f)); };
		return IM_COL32(ch(h.r), ch(h.g), ch(h.b), 255);
	}

	std::uint32_t EditorApp::ContainerColour(const OpenCell& a_cell, int a_index) const
	{
		if (a_index < 0 || static_cast<std::size_t>(a_index) >= a_cell.info.refs.size()) {
			return 0;
		}
		const auto& r = a_cell.info.refs[static_cast<std::size_t>(a_index)];
		if (!r.IsContainer() && !r.display) {
			return 0;
		}
		const auto ref = a_cell.lo->ToFormRef(r.key);
		if (_colours == Colours::Category) {
			if (a_cell.layout.IsMaster(ref)) {
				return kMasterColour;
			}
			if (const auto* b = a_cell.layout.FindContainer(ref); b && !b->ignored && !b->categories.empty()) {
				return CategoryColour(b->categories.front());
			}
			if (const auto* d = a_cell.layout.FindDisplay(ref); d && !d->ignored && !d->categories.empty()) {
				return CategoryColour(d->categories.front());
			}
			return 0;
		}
		if (_colours == Colours::Fill && _preview && _preview->cell == &a_cell) {
			if (r.display) {
				return std::ranges::any_of(_preview->result.onDisplays, [&](const core::PreviewDisplay& d) { return d.ref == ref; }) ? kFillLight : 0;
			}
			switch (home::FillOf(_preview->result, ref, kHeavyAt)) {
			case home::Fill::Empty:
				return kFillEmpty;
			case home::Fill::Light:
				return kFillLight;
			case home::Fill::Heavy:
				return kFillHeavy;
			case home::Fill::Master:
				return kFillMaster;
			default:
				return 0;
			}
		}
		return 0;
	}

	void EditorApp::UpdateTints(OpenCell& a_cell)
	{
		_view.tint.assign(a_cell.info.refs.size(), 0u);
		if (_colours == Colours::None) {
			return;
		}
		if (_colours == Colours::Fill && (!_preview || _preview->cell != &a_cell || _preview->json != a_cell.json)) {
			RunPreview(a_cell);
		}
		for (std::size_t i = 0; i < a_cell.info.refs.size(); ++i) {
			_view.tint[i] = ContainerColour(a_cell, static_cast<int>(i));
		}
	}

	void EditorApp::DrawColourLegend(OpenCell& a_cell)
	{
		int mode = static_cast<int>(_colours);
		ImGui::TextUnformatted("Colours:");
		ImGui::SameLine();
		ImGui::RadioButton("none", &mode, 0);
		ImGui::SameLine();
		ImGui::RadioButton("by category (C)", &mode, 1);
		ImGui::SetItemTooltip("Each container in the colour of its first category; the master in gold.");
		ImGui::SameLine();
		ImGui::RadioButton("by fill (V)", &mode, 2);
		ImGui::SetItemTooltip("Where items go: how much of the sample each container would receive (Preview tab).");
		if (!ImGui::GetIO().WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().KeyCtrl) {
			if (ImGui::IsKeyPressed(ImGuiKey_C, false)) {
				mode = mode == 1 ? 0 : 1;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
				mode = mode == 2 ? 0 : 2;
			}
		}
		_colours = static_cast<Colours>(mode);
		if (_colours == Colours::Fill) {
			ImGui::SameLine();
			Swatch(kFillEmpty, "receives nothing");
			ImGui::SameLine();
			Swatch(kFillLight, std::format("a few (< {})", kHeavyAt).c_str());
			ImGui::SameLine();
			Swatch(kFillHeavy, "many");
			ImGui::SameLine();
			Swatch(kFillMaster, "master (what has no place)");
		} else if (_colours == Colours::Category) {
			std::vector<std::string> used;
			for (const auto& c : a_cell.layout.containers) {
				if (!c.ignored && !c.categories.empty() && std::ranges::find(used, c.categories.front()) == used.end()) {
					used.push_back(c.categories.front());
				}
			}
			ImGui::SameLine();
			Swatch(kMasterColour, "master");
			for (const auto& u : used) {
				ImGui::SameLine();
				if (ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 8.0f) {
					ImGui::NewLine();
				}
				Swatch(CategoryColour(u), u.c_str());
			}
			if (used.empty()) {
				ImGui::SameLine();
				ImGui::TextDisabled("(no container has categories yet)");
			}
		}
	}

	void EditorApp::RunPreview(OpenCell& a_cell)
	{
		LoadCategories();
		if (!_categorySet) {
			return;
		}
		const auto start = std::chrono::steady_clock::now();
		if (_catalogLo != a_cell.lo || !_catalog) {
			_catalog = _homes && _homes->lo == a_cell.lo && _homes->catalog ? _homes->catalog : std::make_shared<esp::ItemCatalog>(esp::BuildItemCatalog(*a_cell.lo));
			_catalogLo = a_cell.lo;
		}
		PreviewState p;
		p.kind = _sampleKind;
		p.pattern = _samplePattern;
		p.json = a_cell.json;
		p.cell = &a_cell;
		const auto kind = _sampleKind == 0 ? home::SampleKind::Haul : _sampleKind == 1 ? home::SampleKind::Category : home::SampleKind::All;
		p.items = home::MakeSample(*_catalog, *_categorySet, kind, _samplePattern);
		const auto spots = home::DisplaySpots(*a_cell.lo, std::span(&a_cell.info, 1), a_cell.layout);
		p.result = core::PreviewWhereItemsGo(a_cell.layout, *_categorySet, p.items, spots);
		for (std::size_t i = 0; i < a_cell.info.refs.size(); ++i) {
			const auto& r = a_cell.info.refs[i];
			if (r.IsContainer() || r.display) {
				p.refIndex[a_cell.lo->ToFormRef(r.key)] = static_cast<int>(i);
			}
		}
		p.ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
		_preview = std::move(p);
	}

	void EditorApp::DrawPreview(OpenCell& a_cell)
	{
		LoadCategories();
		if (!_categorySet) {
			ImGui::TextWrapped("No Dova Stash categories found (Data/SKSE/Plugins/DovaStash/Categories). Enable Dova Stash in this MO2 profile.");
			return;
		}
		// Sample.
		ImGui::TextUnformatted("Sample:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 13.0f);
		const char* kinds[] = { "Typical haul", "One category", "Every item in the load order" };
		ImGui::Combo("##sample", &_sampleKind, kinds, 3);
		ImGui::SetItemTooltip("Typical haul: a handful of each kind of loot (base game items first).\nOne category: every item of the load order in it.\n"
		                      "Every item: one of each playable item.");
		if (_sampleKind == 1) {
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
			if (ImGui::BeginCombo("##samplecat", _samplePattern.c_str(), ImGuiComboFlags_HeightLarge)) {
				for (const auto& c : _categories) {
					const bool group = c.match.Empty();
					const auto pattern = group ? c.id + ".*" : c.id;
					if (ImGui::Selectable(std::format("{}  {}", pattern, c.name).c_str(), pattern == _samplePattern)) {
						_samplePattern = pattern;
					}
				}
				ImGui::EndCombo();
			}
		}
		ImGui::SameLine();
		const bool stale = !_preview || _preview->cell != &a_cell || _preview->json != a_cell.json || _preview->kind != _sampleKind || _preview->pattern != _samplePattern;
		if (stale) {
			RunPreview(a_cell);  // fast: the planner over a few hundred (or thousand) stacks
		}
		if (ImGui::Button("Run again (R)") || (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_R, false) && !ImGui::GetIO().KeyCtrl)) {
			RunPreview(a_cell);
		}
		if (!_preview) {
			return;
		}
		const auto& p = *_preview;
		ImGui::SameLine();
		ImGui::TextDisabled("Dova Stash's own planner (explicit items, categories, then the master) and displays planner, %lld ms", p.ms);
		DrawColourLegend(a_cell);

		std::int32_t copies = 0;
		for (const auto& i : p.items) {
			copies += i.count;
		}
		const auto empty = std::ranges::count_if(p.result.byContainer, [](const core::PreviewContainer& c) { return !c.master && c.items.empty(); });
		if (p.items.empty()) {
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "The sample is empty: no item of the loaded plugins is in that category.");
			return;
		}
		ImGui::Text("%d items in %zu stacks: %d to containers by their categories or items, %d to the master, %zu on displays.", copies, p.items.size(),
			p.result.toContainers, p.result.toMaster, p.result.onDisplays.size());
		if (empty > 0) {
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.35f, 1.0f), "%lld containers receive nothing.", static_cast<long long>(empty));
		}
		if (!p.result.noDestination.empty()) {
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%zu stacks have nowhere to go: the template has no master.", p.result.noDestination.size());
		}
		if (!a_cell.layout.master) {
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "No master: what no container takes stays with the player.");
		}

		std::int32_t maxTotal = 1;
		for (const auto& c : p.result.byContainer) {
			maxTotal = std::max(maxTotal, c.total);
		}
		ImGui::TextDisabled("Click a container to select it (its items show in the details pane), double-click to look at it in the 3D view.");
		const float detailsWidth = ImGui::GetFontSize() * 19.0f;
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		ImGui::BeginChild("previewtable", ImVec2(std::max(avail.x - detailsWidth - ImGui::GetStyle().ItemSpacing.x, 100.0f), avail.y));
		if (ImGui::BeginTable("preview", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Container", ImGuiTableColumnFlags_WidthStretch, 0.3f);
			ImGui::TableSetupColumn("Fill", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.0f);
			ImGui::TableSetupColumn("Items", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.0f);
			ImGui::TableSetupColumn("What it receives", ImGuiTableColumnFlags_WidthStretch, 0.7f);
			ImGui::TableHeadersRow();
			std::vector<const core::PreviewContainer*> rows;
			for (const auto& c : p.result.byContainer) {
				rows.push_back(&c);
			}
			std::ranges::stable_sort(rows, [](const core::PreviewContainer* a, const core::PreviewContainer* b) {
				if (a->master != b->master) {
					return a->master;
				}
				return a->total > b->total;
			});
			for (const auto* c : rows) {
				const auto it = p.refIndex.find(c->ref);
				const int  index = it == p.refIndex.end() ? -1 : it->second;
				std::string name = c->ref.ToString();
				if (const auto* b = a_cell.layout.FindContainer(c->ref); b && !b->label.empty()) {
					name = b->label;
				} else if (index >= 0) {
					name = RefName(a_cell.info.refs[static_cast<std::size_t>(index)]);
				}
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::PushID(c->ref.ToString().c_str());
				const auto colour = c->master ? kFillMaster : c->total == 0 ? kFillEmpty : c->total >= kHeavyAt ? kFillHeavy : kFillLight;
				ImGui::PushStyleColor(ImGuiCol_Text, c->total == 0 && !c->master ? ImVec4(1.0f, 0.7f, 0.35f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
				if (ImGui::Selectable(std::format("{}{}", c->master ? "[master] " : "", name).c_str(), index >= 0 && _selectedRef == index,
						ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
					if (index >= 0) {
						const bool twice = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
						Select(index, twice);
						if (twice) {
							_requestTab = 0;
						}
					}
				}
				ImGui::PopStyleColor();
				if (index < 0) {
					ImGui::SetItemTooltip("In another cell of the home: open that cell to see it.");
				}
				ImGui::PopID();
				ImGui::TableNextColumn();
				const ImVec2 pos = ImGui::GetCursorScreenPos();
				const float  w = ImGui::GetContentRegionAvail().x;
				const float  h = ImGui::GetTextLineHeight() * 0.7f;
				ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y + 3.0f), ImVec2(pos.x + w, pos.y + 3.0f + h), IM_COL32(60, 60, 68, 255), 2.0f);
				ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y + 3.0f),
					ImVec2(pos.x + std::max(w * static_cast<float>(c->total) / static_cast<float>(maxTotal), c->total > 0 ? 3.0f : 0.0f), pos.y + 3.0f + h), colour, 2.0f);
				ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight()));
				ImGui::TableNextColumn();
				ImGui::Text("%d", c->total);
				ImGui::TableNextColumn();
				if (c->items.empty()) {
					const auto* b = a_cell.layout.FindContainer(c->ref);
					ImGui::TextDisabled("%s", c->master                                   ? "nothing falls to the master"
					                          : b && b->categories.empty() && b->forms.empty() ? "receives nothing: it has no categories (give it some, or ignore it)"
					                                                                          : "receives nothing: no item of the sample is in its categories");
				} else {
					std::vector<std::pair<std::string, std::int32_t>> per(c->perCategory.begin(), c->perCategory.end());
					std::ranges::sort(per, [](const auto& a, const auto& b) { return a.second > b.second; });
					std::string text;
					for (const auto& [cat, n] : per) {
						text += std::format("{}{} {}", text.empty() ? "" : ", ", cat.empty() ? "(no category)" : cat, n);
					}
					ImGui::TextUnformatted(text.c_str());
					if (ImGui::BeginItemTooltip()) {
						for (std::size_t k = 0; k < c->items.size() && k < 25; ++k) {
							const auto& e = c->items[k];
							ImGui::Text("%s x%d  (%s%s)", p.items[e.item].name.c_str(), e.count, std::string(core::DestinationReasonName(e.reason)).c_str(),
								e.category.empty() ? "" : (": " + e.category).c_str());
						}
						if (c->items.size() > 25) {
							ImGui::TextDisabled("... %zu more", c->items.size() - 25);
						}
						ImGui::EndTooltip();
					}
				}
			}
			ImGui::EndTable();
		}
		ImGui::EndChild();
		ImGui::SameLine();
		DrawDetails(a_cell, detailsWidth, avail.y);
	}

	void EditorApp::DrawPreviewDetails(OpenCell& a_cell, int a_index)
	{
		if (!_preview || _preview->cell != &a_cell) {
			return;
		}
		const auto& r = a_cell.info.refs[static_cast<std::size_t>(a_index)];
		const auto  ref = a_cell.lo->ToFormRef(r.key);
		const auto* c = _preview->result.Find(ref);
		std::vector<std::size_t> shown;
		for (const auto& d : _preview->result.onDisplays) {
			if (d.ref == ref) {
				shown.push_back(d.item);
			}
		}
		if (!c && shown.empty()) {
			return;
		}
		ImGui::SeparatorText("Where items go (preview)");
		for (const auto i : shown) {
			ImGui::BulletText("on display: %s", _preview->items[i].name.c_str());
		}
		if (!c) {
			return;
		}
		if (c->items.empty()) {
			ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.35f, 1.0f), c->master ? "Nothing falls to the master." : "Receives nothing from this sample.");
			return;
		}
		ImGui::Text("Would receive %d items%s:", c->total, c->master ? " (no other place)" : "");
		std::vector<std::pair<std::string, std::int32_t>> per(c->perCategory.begin(), c->perCategory.end());
		std::ranges::sort(per, [](const auto& a, const auto& b) { return a.second > b.second; });
		for (const auto& [cat, n] : per) {
			ImGui::TextColored(ToVec(CategoryColour(cat)), "%s", cat.empty() ? "(no category)" : cat.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("%d", n);
		}
		ImGui::Separator();
		for (std::size_t k = 0; k < c->items.size() && k < 40; ++k) {
			const auto& e = c->items[k];
			ImGui::TextWrapped("%s x%d", _preview->items[e.item].name.c_str(), e.count);
		}
		if (c->items.size() > 40) {
			ImGui::TextDisabled("... %zu more", c->items.size() - 40);
		}
	}
}
