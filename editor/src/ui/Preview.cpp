// "Where items go" (docs/EDITOR.md §3.5): the drawer's tab runs Dova Stash's own planner (core::PreviewWhereItemsGo:
// SortPlanner + the displays planner) over a sample inventory against the template being made, and shows per
// container what it would receive. Containers are coloured by fill (or by category) in the 3D view and on the plan,
// with the legend in the Colours popover; the Selection panel shows a selected container's predicted items. The
// preview re-runs by itself whenever the template changes.

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
		constexpr std::uint32_t kPreviewNothing = 1u << 0;  // the quick filters (ui/Tables.cpp): Receives nothing, Master, Receives items
		constexpr std::uint32_t kPreviewMaster = 1u << 1;
		constexpr std::uint32_t kPreviewItems = 1u << 2;

		std::string RefName(const home::PlacedRef& a_ref) { return a_ref.baseName.empty() ? a_ref.baseEditorId : a_ref.baseName; }
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
		w::Radio("None", mode, 0);
		w::Radio("By category", mode, 1);
		w::TipShortcut("Each container in the colour of its first category; the master in its own colour", "C");
		w::Radio("By fill", mode, 2);
		w::TipShortcut("How much of the sample each container would receive (Where items go)", "V");
		_colours = static_cast<Colours>(mode);
		w::SectionHead("Legend");
		if (_colours == Colours::Fill) {
			w::Swatch(kFillEmpty, "receives nothing");
			w::Swatch(kFillLight, std::format("a few (< {})", kHeavyAt).c_str());
			w::Swatch(kFillHeavy, "many");
			w::Swatch(kFillMaster, "master: what has no place");
			w::Small(_sampleKind == 0 ? "Sample: typical haul" : _sampleKind == 1 ? ("Sample: " + _samplePattern).c_str() : "Sample: every item");
		} else if (_colours == Colours::Category) {
			std::vector<std::string> used;
			for (const auto& c : a_cell.layout.containers) {
				if (!c.ignored && !c.categories.empty() && std::ranges::find(used, c.categories.front()) == used.end()) {
					used.push_back(c.categories.front());
				}
			}
			w::Swatch(kMasterColour, "master");
			for (const auto& u : used) {
				w::Swatch(CategoryColour(u), u.c_str());
			}
			if (used.empty()) {
				w::Muted("No container has categories yet");
			}
		} else {
			// The plan's own colours.
			w::Swatch(IM_COL32(90, 200, 110, 255), "container");
			w::Swatch(IM_COL32(230, 70, 60, 255), "respawning container");
			w::Swatch(IM_COL32(80, 190, 230, 255), "weapon rack");
			w::Swatch(IM_COL32(220, 110, 220, 255), "mannequin");
			w::Swatch(IM_COL32(240, 170, 60, 255), "plaque, case, bookshelf");
			w::Swatch(IM_COL32(240, 220, 80, 255), "door");
			w::Swatch(IM_COL32(140, 150, 220, 200), "furniture, activators");
		}
	}
	const esp::ItemCatalog& EditorApp::Catalog(OpenCell& a_cell)
	{
		if (_catalogLo != a_cell.lo || !_catalog) {
			_catalog = _homes && _homes->lo == a_cell.lo && _homes->catalog ? _homes->catalog : std::make_shared<esp::ItemCatalog>(esp::BuildItemCatalog(*a_cell.lo));
			_catalogLo = a_cell.lo;
		}
		return *_catalog;
	}

	const home::ArmorSetIndex& EditorApp::ArmorSets(OpenCell& a_cell)
	{
		if (_armorSetsLo != a_cell.lo || !_armorSets) {
			_armorSets = std::make_shared<home::ArmorSetIndex>(home::BuildArmorSetIndex(*a_cell.lo, Catalog(a_cell)));
			_armorSetsLo = a_cell.lo;
		}
		return *_armorSets;
	}

	void EditorApp::RunPreview(OpenCell& a_cell)
	{
		LoadCategories();
		if (!_categorySet || Busy()) {
			return;  // a job may be reading the load order (not thread-safe): next frame
		}
		const auto start = std::chrono::steady_clock::now();
		PreviewState p;
		p.kind = _sampleKind;
		p.pattern = _samplePattern;
		p.json = a_cell.json;
		p.cell = &a_cell;
		const auto kind = _sampleKind == 0 ? home::SampleKind::Haul : _sampleKind == 1 ? home::SampleKind::Category : home::SampleKind::All;
		p.items = home::MakeSample(Catalog(a_cell), *_categorySet, kind, _samplePattern);
		const auto spots = home::DisplaySpots(*a_cell.lo, std::span(&a_cell.info, 1), a_cell.layout, &ArmorSets(a_cell));
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

	void EditorApp::DrawPreviewControls(OpenCell& a_cell)
	{
		(void)a_cell;
		if (!_categorySet) {
			return;
		}
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
		const char* kinds[] = { "Typical haul", "One category", "Every item" };
		ImGui::Combo("##sample", &_sampleKind, kinds, 3);
		w::Tip("Typical haul: a handful of each kind of loot (base game items first)\nOne category: every item of the load order in it\nEvery item: one of each playable item");
		if (_sampleKind == 1) {
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
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
		if (w::ToolButton("Run again", false, "Run the planner again", "R")) {
			RunPreview(*_cell);
		}
		ImGui::SameLine();
		if (w::ToolButton("By fill", _colours == Colours::Fill, "Colour the containers by fill", "V")) {
			_colours = _colours == Colours::Fill ? Colours::None : Colours::Fill;
		}
		ImGui::SameLine(0.0f, 2.0f);
		if (w::ToolButton("By category", _colours == Colours::Category, "Colour the containers by category", "C")) {
			_colours = _colours == Colours::Category ? Colours::None : Colours::Category;
		}
		if (_preview) {
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			w::Muted(std::format("{} ms", _preview->ms).c_str());
			w::Tip("Dova Stash's own planner (explicit items, categories, then the master) and displays planner");
		}
	}

	void EditorApp::DrawPreview(OpenCell& a_cell)
	{
		LoadCategories();
		if (!_categorySet) {
			w::MutedWrapped("No Dova Stash categories found (Data/SKSE/Plugins/DovaStash/Categories). Enable Dova Stash in this MO2 profile.");
			return;
		}
		const bool stale = !_preview || _preview->cell != &a_cell || _preview->json != a_cell.json || _preview->kind != _sampleKind || _preview->pattern != _samplePattern;
		if (stale) {
			RunPreview(a_cell);  // fast: the planner over a few hundred (or thousand) stacks
		}
		if (!_preview || _preview->cell != &a_cell) {
			w::Muted("Running...");
			return;
		}
		const auto&  p = *_preview;
		std::int32_t copies = 0;
		for (const auto& i : p.items) {
			copies += i.count;
		}
		const auto empty = std::ranges::count_if(p.result.byContainer, [](const core::PreviewContainer& c) { return !c.master && c.items.empty(); });
		if (p.items.empty()) {
			w::Coloured(w::Kind::Warn, "The sample is empty: no item of the loaded plugins is in that category.");
			return;
		}
		ImGui::Text("%d items in %zu stacks", copies, p.items.size());
		ImGui::SameLine();
		w::Muted(std::format("{} by category or item · {} to the master · {} on displays", p.result.toContainers, p.result.toMaster, p.result.onDisplays.size()).c_str());
		if (empty > 0) {
			ImGui::SameLine();
			w::Coloured(w::Kind::Warn, std::format("· {} receive nothing", empty).c_str());
		}
		if (!p.result.noDestination.empty()) {
			ImGui::SameLine();
			w::Coloured(w::Kind::Error, std::format("· {} stacks have nowhere to go (no master)", p.result.noDestination.size()).c_str());
		} else if (!a_cell.layout.master) {
			ImGui::SameLine();
			w::Coloured(w::Kind::Error, "· no master: what no container takes stays with the player");
		}

		std::int32_t maxTotal = 1;
		for (const auto& c : p.result.byContainer) {
			maxTotal = std::max(maxTotal, c.total);
		}
		// The table's own order: the master first, then by items. A sort (any column, ui/Tables.h) starts from it.
		std::vector<const core::PreviewContainer*> order;
		for (const auto& c : p.result.byContainer) {
			order.push_back(&c);
		}
		std::ranges::stable_sort(order, [](const core::PreviewContainer* a, const core::PreviewContainer* b) {
			if (a->master != b->master) {
				return a->master;
			}
			return a->total > b->total;
		});
		auto&                 view = Table(DrawerTab::Preview);
		std::vector<TableRow> rows;
		for (std::size_t k = 0; k < order.size(); ++k) {
			const auto* c = order[k];
			const auto  it = p.refIndex.find(c->ref);
			const int   index = it == p.refIndex.end() ? -1 : it->second;
			std::string name = c->ref.ToString();
			if (const auto* b = a_cell.layout.FindContainer(c->ref); b && !b->label.empty()) {
				name = b->label;
			} else if (index >= 0) {
				name = RefName(a_cell.info.refs[static_cast<std::size_t>(index)]);
			}
			TableRow row;
			row.index = static_cast<int>(k);
			row.cells.resize(4);
			row.cells[0].text = std::format("{}{}", c->master ? "master: " : "", name);
			// Fill: by its colour (nothing, a few, many, the master), then by items.
			const int level = c->master ? 3 : c->total == 0 ? 0 : c->total >= kHeavyAt ? 2 : 1;
			row.cells[1].number = level * 1e7 + c->total;
			row.cells[2] = { std::to_string(c->total), static_cast<double>(c->total) };
			if (c->items.empty()) {
				const auto* b = a_cell.layout.FindContainer(c->ref);
				row.cells[3] = Grouped(c->master                                        ? "nothing falls to the master"
				                       : b && b->categories.empty() && b->forms.empty() ? "nothing: no categories (give it some, or ignore it)"
				                                                                        : "nothing of the sample is in its categories",
					1);
			} else {
				std::vector<std::pair<std::string, std::int32_t>> per(c->perCategory.begin(), c->perCategory.end());
				std::ranges::sort(per, [](const auto& a, const auto& b) { return a.second > b.second; });
				for (const auto& [cat, n] : per) {
					row.cells[3].text += std::format("{}{} {}", row.cells[3].text.empty() ? "" : ", ", cat.empty() ? "(no category)" : cat, n);
				}
			}
			row.tags = c->master ? kPreviewMaster : c->items.empty() ? kPreviewNothing : kPreviewItems;
			rows.push_back(std::move(row));
		}
		if (ImGui::BeginTable("preview", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | kSortableTable)) {
			ImGui::TableSetupScrollFreeze(0, 1);
			const auto columns = TableColumns(DrawerTable::Preview);
			ImGui::TableSetupColumn(columns[0], ImGuiTableColumnFlags_WidthStretch, 0.3f);
			ImGui::TableSetupColumn(columns[1], ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, ImGui::GetFontSize() * 7.0f);
			ImGui::TableSetupColumn(columns[2], ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, ImGui::GetFontSize() * 3.0f);
			ImGui::TableSetupColumn(columns[3], ImGuiTableColumnFlags_WidthStretch, 0.7f);
			SyncSort(view);
			FilterAndSort(view, rows, DrawerTable::Preview);
			ImGui::TableHeadersRow();
			for (const auto& row : rows) {
				const auto* c = order[static_cast<std::size_t>(row.index)];
				const auto  it = p.refIndex.find(c->ref);
				const int   index = it == p.refIndex.end() ? -1 : it->second;
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::PushID(c->ref.ToString().c_str());
				const auto colour = c->master ? kFillMaster : c->total == 0 ? kFillEmpty : c->total >= kHeavyAt ? kFillHeavy : kFillLight;
				if (c->total == 0 && !c->master) {
					ImGui::PushStyleColor(ImGuiCol_Text, w::Th().warn);
				}
				if (ImGui::Selectable(row.cells[0].text.c_str(), index >= 0 && IsSelected(index),
						ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick) &&
					index >= 0) {
					TableRowClick(index);
				}
				if (c->total == 0 && !c->master) {
					ImGui::PopStyleColor();
				}
				if (index < 0) {
					w::Tip("In another cell of the home: open that cell to see it");
				}
				ImGui::PopID();
				ImGui::TableNextColumn();
				const ImVec2 pos = ImGui::GetCursorScreenPos();
				const float  wdt = ImGui::GetContentRegionAvail().x;
				const float  h = ImGui::GetTextLineHeight() * 0.7f;
				ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y + 3.0f), ImVec2(pos.x + wdt, pos.y + 3.0f + h), w::Th().bg3, 2.0f);
				ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y + 3.0f),
					ImVec2(pos.x + std::max(wdt * static_cast<float>(c->total) / static_cast<float>(maxTotal), c->total > 0 ? 3.0f : 0.0f), pos.y + 3.0f + h), colour, 2.0f);
				ImGui::Dummy(ImVec2(wdt, ImGui::GetTextLineHeight()));
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(row.cells[2].text.c_str());
				ImGui::TableNextColumn();
				if (c->items.empty()) {
					w::Muted(row.cells[3].text.c_str());
				} else {
					ImGui::TextUnformatted(row.cells[3].text.c_str());
					if (ImGui::BeginItemTooltip()) {
						for (std::size_t k = 0; k < c->items.size() && k < 25; ++k) {
							const auto& e = c->items[k];
							ImGui::Text("%s x%d  (%s%s)", p.items[e.item].name.c_str(), e.count, std::string(core::DestinationReasonName(e.reason)).c_str(),
								e.category.empty() ? "" : (": " + e.category).c_str());
						}
						if (c->items.size() > 25) {
							w::Muted(std::format("... {} more", c->items.size() - 25).c_str());
						}
						ImGui::EndTooltip();
					}
				}
			}
			if (view.total > 0 && view.shown == 0) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				w::Muted("No row matches the filter");
			}
			ImGui::EndTable();
		}
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
		w::SectionHead("Where its items go");
		for (const auto i : shown) {
			ImGui::BulletText("on display: %s", _preview->items[i].name.c_str());
		}
		if (!c) {
			return;
		}
		if (c->items.empty()) {
			w::Coloured(w::Kind::Warn, c->master ? "Nothing falls to the master" : "Nothing from this sample");
			return;
		}
		ImGui::Text("%d items%s", c->total, c->master ? " with no other place" : "");
		std::vector<std::pair<std::string, std::int32_t>> per(c->perCategory.begin(), c->perCategory.end());
		std::ranges::sort(per, [](const auto& a, const auto& b) { return a.second > b.second; });
		for (const auto& [cat, n] : per) {
			w::Swatch(CategoryColour(cat), cat.empty() ? "(no category)" : cat.c_str());
			ImGui::SameLine();
			w::Muted(std::to_string(n).c_str());
		}
		w::Spacer();
		for (std::size_t k = 0; k < c->items.size() && k < 40; ++k) {
			const auto& e = c->items[k];
			ImGui::TextWrapped("%s x%d", _preview->items[e.item].name.c_str(), e.count);
		}
		if (c->items.size() > 40) {
			w::Muted(std::format("... {} more", c->items.size() - 40).c_str());
		}
	}
}
