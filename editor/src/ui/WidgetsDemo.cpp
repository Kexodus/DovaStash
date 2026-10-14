// The widget gallery (DOVASTASH_WIDGETS_DEMO=1, Window.cpp): every widget of Widgets.h in the workspace's
// own frame (title row, rails, status row), so the theme can be checked without the editor's data.

#define IMGUI_DEFINE_MATH_OPERATORS
#include "ui/Widgets.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <format>
#include <mutex>

#include <imgui_internal.h>

#include "render/Scene.h"
#include "render/Thumbnail.h"
#include "ui/Art.h"
#include "ui/WidgetsDemo.h"

namespace dova::editor::ui::w
{
	namespace
	{
		struct DemoState
		{
			int   frame{ 0 };
			int   view{ 0 };
			int   panel{ 0 };
			bool  paint{ false };
			bool  railOpen{ true };
			float railW{ 230.0f };
			bool  rightOpen{ false };
			float rightW{ 300.0f };
			char  search[64]{};
			char  search2[64]{ "potion" };
			bool  toggleA{ true };
			bool  toggleB{ false };
			bool  check{ true };
			int   radio{ 1 };
			float slider{ 0.4f };
			int   combo{ 0 };
			int   selected{ 0 };
			bool  layers[4]{ true, true, false, true };
			int   chipActive{ 1 };
		};

		struct DemoScene
		{
			std::mutex                         mutex;
			std::shared_ptr<render::Scene>     scene;
			std::unique_ptr<render::Thumbnails> thumbs;
		};

		DemoScene& D()
		{
			static DemoScene s;
			return s;
		}

		DemoState& S()
		{
			static DemoState s;
			return s;
		}

		struct IconName
		{
			Icon        icon;
			const char* name;
		};
		constexpr std::array kIcons{ IconName{ Icon::Menu, "Menu" }, IconName{ Icon::Search, "Search" }, IconName{ Icon::Filter, "Filter" },
			IconName{ Icon::Layers, "Layers" }, IconName{ Icon::Palette, "Palette" }, IconName{ Icon::Camera, "Camera" },
			IconName{ Icon::Settings, "Settings" }, IconName{ Icon::ChevronLeft, "ChevronLeft" }, IconName{ Icon::ChevronRight, "ChevronRight" },
			IconName{ Icon::ChevronDown, "ChevronDown" }, IconName{ Icon::ChevronUp, "ChevronUp" }, IconName{ Icon::Check, "Check" },
			IconName{ Icon::Warning, "Warning" }, IconName{ Icon::Error, "Error" }, IconName{ Icon::Info, "Info" }, IconName{ Icon::Undo, "Undo" },
			IconName{ Icon::Redo, "Redo" }, IconName{ Icon::Save, "Save" }, IconName{ Icon::Close, "Close" }, IconName{ Icon::Pin, "Pin" },
			IconName{ Icon::Copy, "Copy" }, IconName{ Icon::Home, "Home" }, IconName{ Icon::Cube, "Cube" }, IconName{ Icon::Plan, "Plan" },
			IconName{ Icon::Draft, "Draft" }, IconName{ Icon::Plus, "Plus" }, IconName{ Icon::Minus, "Minus" }, IconName{ Icon::Emblem, "Emblem" },
			IconName{ Icon::Door, "Door" }, IconName{ Icon::Container, "Container" }, IconName{ Icon::Display, "Display" },
			IconName{ Icon::Mannequin, "Mannequin" }, IconName{ Icon::Rack, "Rack" }, IconName{ Icon::Book, "Book" }, IconName{ Icon::Sort, "Sort" },
			IconName{ Icon::Expand, "Expand" }, IconName{ Icon::Collapse, "Collapse" }, IconName{ Icon::ExternalLink, "ExternalLink" },
			IconName{ Icon::Folder, "Folder" }, IconName{ Icon::Refresh, "Refresh" }, IconName{ Icon::Brush, "Brush" }, IconName{ Icon::Cursor, "Cursor" },
			IconName{ Icon::Trash, "Trash" }, IconName{ Icon::Eye, "Eye" }, IconName{ Icon::More, "More" } };

		void TitleRow()
		{
			DemoState&   s = S();
			const Theme& t = Th();
			ImDrawList*  dl = ImGui::GetWindowDrawList();
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float  w = ImGui::GetContentRegionAvail().x;
			dl->AddRectFilled(p, p + ImVec2(w, t.titleH), t.bg1);
			dl->AddRectFilled(ImVec2(p.x, p.y + t.titleH - 1.0f), p + ImVec2(w, t.titleH), t.line);
			ImGui::SetCursorScreenPos(p + ImVec2(t.gap, std::round((t.titleH - t.inputH) * 0.5f)));
			IconButton("menu", Icon::Menu, "Menu");
			ImGui::SameLine(0.0f, t.pad);
			const float es = 22.0f * t.scale;
			const float y0 = p.y + (t.titleH - es) * 0.5f;
			DrawIcon(dl, Icon::Emblem, ImVec2(ImGui::GetCursorScreenPos().x, std::round(y0)), es, t.accent);
			ImGui::Dummy(ImVec2(es, t.inputH));
			ImGui::SameLine(0.0f, t.pad);
			{
				ImFont*      f = FontOf(Font::Title);
				const ImVec2 cp = ImGui::GetCursorScreenPos();
				const ImVec2 ts = f->CalcTextSizeA(f->LegacySize, FLT_MAX, 0.0f, "Dova Stash Editor");
				dl->AddText(f, f->LegacySize, ImVec2(cp.x, std::floor(p.y + (t.titleH - ts.y) * 0.5f)), t.text, "Dova Stash Editor");
				ImGui::Dummy(ImVec2(ts.x, t.inputH));
			}
			ImGui::SameLine(0.0f, 28.0f * t.scale);
			if (BeginPopover("home", "Aldamar", Icon::ChevronDown, 0, "Switch home", "Ctrl+P")) {
				MenuRow("Aldamar");
				MenuRow("Lakeview Manor");
				MenuRow("Breezehome");
				EndPopover();
			}
			ImGui::SameLine(0.0f, 4.0f * t.scale);
			ImGui::AlignTextToFramePadding();
			StatusDot(Kind::Accent, "Unsaved");
			ImGui::SameLine(0.0f, 2.0f * t.scale);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::round((t.inputH - t.chipH) * 0.5f));
			Chip("yours", Kind::Ok);
			// Centre: the view switch.
			static const char* const kViews[] = { "3D", "Plan" };
			static const char* const kViewTips[] = { "3D view  1", "Floor plan  2" };
			const float segW = TextWidth("3D") + TextWidth("Plan") + 48.0f * t.scale;
			ImGui::SameLine(std::floor((w - segW) * 0.5f));
			ImGui::SetCursorPosY(std::round((t.titleH - t.inputH) * 0.5f));
			Segmented("view", s.view, kViews, kViewTips);
			// Right: draft, save, setup.
			const float rightW = 260.0f * t.scale;
			ImGui::SameLine(w - rightW);
			if (BeginPopover("draft", "Draft", Icon::Draft, 0, "Draft this home", "Ctrl+D")) {
				MenuRow("Draft this home");
				MenuRow("Accept all");
				MenuRow("Reject the weak ones");
				ImGui::Separator();
				MenuRow("Draft all homes", "Ctrl+D");
				EndPopover();
			}
			ImGui::SameLine(0.0f, t.gap);
			PrimaryButton("Save");
			ImGui::SameLine(0.0f, t.gap);
			if (BeginPopover("setup", nullptr, Icon::Settings, 0, "Setup")) {
				Muted("Kexodus Skyrim · Dova Stash Testing");
				EndPopover();
			}
			ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + t.titleH));
			ImGui::Dummy(ImVec2(0, 0));
		}

		void StatusRow(ImVec2 a_min, ImVec2 a_max)
		{
			const Theme& t = Th();
			ImDrawList*  dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(a_min, a_max, t.bg1);
			dl->AddRectFilled(a_min, ImVec2(a_max.x, a_min.y + 1.0f), t.line);
			ImGui::SetCursorScreenPos(ImVec2(a_min.x + t.pad, a_min.y + std::round((t.statusH - ImGui::GetTextLineHeight()) * 0.5f)));
			TextWithMuted("Aldamar", "· 0Druid");
			ImGui::SameLine(0.0f, 4.0f * t.scale);
			StatusDot(Kind::Accent, "Unsaved");
			ImGui::SameLine(std::floor((a_max.x - a_min.x) * 0.42f));
			IconInline(Icon::Error, t.error);
			ImGui::SameLine(0.0f, 4.0f * t.scale);
			ImGui::TextUnformatted("0");
			ImGui::SameLine(0.0f, t.pad);
			IconInline(Icon::Warning, t.warn);
			ImGui::SameLine(0.0f, 4.0f * t.scale);
			ImGui::TextUnformatted("2");
			ImGui::SameLine(a_max.x - a_min.x - 330.0f * t.scale);
			Progress("meshes", 403, 433, 90.0f * t.scale);
			ImGui::SameLine(0.0f, 18.0f * t.scale);
			const float y = ImGui::GetCursorPosY();
			ImGui::SetCursorPosY(y - std::round((t.chipH - ImGui::GetTextLineHeight()) * 0.5f));
			KeyCap("Ctrl+S");
			ImGui::SameLine(0.0f, 4.0f * t.scale);
			ImGui::SetCursorPosY(y);
			Muted("save");
		}

		void LeftRail()
		{
			DemoState&   s = S();
			const Theme& t = Th();
			if (BeginRail("homes", Side::Left, s.railW, s.railOpen, 160.0f * t.scale, 420.0f * t.scale, "Homes 124")) {
				SearchBox("search", s.search, sizeof(s.search), "Home, mod or plugin");
				Spacer(4.0f * t.scale);
				if (BeginPopover("filter", "Filter", Icon::Filter, 2, "Filters")) {
					EndPopover();
				}
				ImGui::SameLine();
				IconButton("sort", Icon::Sort, "Sort");
				ImGui::SameLine();
				IconButton("collapse", Icon::Collapse, "Collapse all");
				SectionHead("Lux · 12345");
				const char* homes[] = { "Aldamar", "Lakeview Manor", "Breezehome", "Hjerim", "Proudspire Manor", "Vlindrel Hall" };
				const char* chips[] = { "yours", "shipped", "none", "drafted", "saved", "none" };
				const Kind  kinds[] = { Kind::Ok, Kind::Info, Kind::Neutral, Kind::Warn, Kind::Accent, Kind::Neutral };
				for (int i = 0; i < 6; ++i) {
					ImGui::PushID(i);
					if (Row("row", homes[i], s.selected == i, chips[i], kinds[i])) {
						s.selected = i;
					}
					ImGui::PopID();
				}
				SectionHead("Skyrim.esm");
				Row("custom", "", false, "89 · 170", Kind::Neutral);
				ImGui::SameLine();
				IconInline(Icon::Home, t.muted);
				ImGui::SameLine();
				ImGui::TextUnformatted("Honeyside");
				ImGui::SameLine();
				StatusDot(Kind::Accent);
				Spacer();
				LinkButton("All cells…");
				EndRail();
			}
		}

		void Section(const char* a_name)
		{
			SectionHead(a_name);
		}

		void ColumnA()
		{
			DemoState&   s = S();
			const Theme& t = Th();

			Section("Type");
			Title("Dova Stash Editor");
			Heading(std::format("Heading · Jost {:.0f}", kHeadingPx).c_str());
			ImGui::TextUnformatted(std::format("Body · Jost {:.0f} — Ærø, Fjölnir, Ysgramor's Tomb, naïve façade • ◆ ● ‹ › ▾ ▴ ×", kBodyPx).c_str());
			Small(std::format("Small · Jost {:.0f} — 89 chests · 170 displays", kSmallPx).c_str());
			Mono(std::format("Mono · {:.0f} · Skyrim.esm|0x000B1B8C  DovaStash_Master", kMonoPx).c_str());
			Muted("Muted · secondary text");

			Section("Tokens");
			const std::array<std::pair<const char*, ImU32>, 14> tokens{ { { "bg0", t.bg0 }, { "bg1", t.bg1 }, { "bg2", t.bg2 }, { "bg3", t.bg3 }, { "line", t.line },
				{ "text", t.text }, { "muted", t.muted }, { "accent", t.accent }, { "accentDim", t.accentDim }, { "select", t.select }, { "ok", t.ok },
				{ "warn", t.warn }, { "error", t.error }, { "info", t.info } } };
			for (std::size_t i = 0; i < tokens.size(); ++i) {
				if (i % 7 != 0) {
					ImGui::SameLine(0.0f, 4.0f * t.scale);
				}
				ImGui::BeginGroup();
				const ImVec2 p = ImGui::GetCursorScreenPos();
				const float  sw = 52.0f * t.scale;
				ImGui::GetWindowDrawList()->AddRectFilled(p, p + ImVec2(sw, 22.0f * t.scale), tokens[i].second, t.radius);
				ImGui::GetWindowDrawList()->AddRect(p, p + ImVec2(sw, 22.0f * t.scale), t.line, t.radius);
				ImGui::Dummy(ImVec2(sw, 22.0f * t.scale));
				PushFont(Font::Small);
				ImGui::PushStyleColor(ImGuiCol_Text, t.muted);
				ImGui::TextUnformatted(tokens[i].first);
				ImGui::PopStyleColor();
				PopFont();
				ImGui::EndGroup();
			}

			Section("Chips");
			Chip("neutral");
			ImGui::SameLine(0.0f, 4.0f * t.scale);
			Chip("ok", Kind::Ok);
			ImGui::SameLine(0.0f, 4.0f * t.scale);
			Chip("warn", Kind::Warn);
			ImGui::SameLine(0.0f, 4.0f * t.scale);
			Chip("error", Kind::Error);
			ImGui::SameLine(0.0f, 4.0f * t.scale);
			Chip("accent", Kind::Accent);
			ImGui::SameLine(0.0f, 4.0f * t.scale);
			Chip("info", Kind::Info);
			const char* cats[] = { "alchemy.potions", "ingredients", "weapons.swords" };
			for (int i = 0; i < 3; ++i) {
				if (i > 0) {
					ImGui::SameLine(0.0f, 4.0f * t.scale);
				}
				ImGui::PushID(i);
				if (ChipButton(cats[i], i == 2 ? Kind::Warn : Kind::Ok, s.chipActive == i)) {
					s.chipActive = i;
				}
				ImGui::PopID();
			}
			ImGui::SameLine(0.0f, 4.0f * t.scale);
			ChipClose("soul gems##close", Kind::Accent);
			ImGui::SameLine(0.0f, t.pad);
			Badge("3");
			ImGui::SameLine(0.0f, 4.0f * t.scale);
			Badge("12", Kind::Error);
			ImGui::SameLine(0.0f, 4.0f * t.scale);
			Badge("0", Kind::Neutral);

			Section("Buttons");
			ToolButton("Select", !s.paint, "Select", "S");
			ImGui::SameLine(0.0f, 2.0f * t.scale);
			if (ToolButton("Paint", s.paint, "Paint", "P")) {
				s.paint = !s.paint;
			}
			ImGui::SameLine();
			ToolbarDivider();
			ImGui::SameLine();
			IconTextButton("Overview", Icon::Home, false, "Overview", "Home");
			ImGui::SameLine();
			IconButton("undo", Icon::Undo, "Undo", "Ctrl+Z");
			ImGui::SameLine(0.0f, 2.0f * t.scale);
			IconButton("redo", Icon::Redo, "Redo", "Ctrl+Y");
			ImGui::SameLine(0.0f, 2.0f * t.scale);
			IconButton("pin", Icon::Pin, "Pinned", nullptr, true);
			PrimaryButton("Save");
			ImGui::SameLine();
			PrimaryButton("Save##off", false, "Fix 2 errors first");
			ImGui::SameLine();
			ImGui::Button("Browse…");
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			LinkButton("Change…");
			static const char* const kPanels[] = { "Selection", "Template", "Draft", "Checks" };
			Segmented("panels", s.panel, kPanels);

			Section("Inputs");
			SearchBox("s1", s.search, sizeof(s.search), "Search homes", 220.0f * t.scale);
			ImGui::SameLine();
			SearchBox("s2", s.search2, sizeof(s.search2), "Category", 200.0f * t.scale);
			Toggle("Group by mod", s.toggleA);
			ImGui::SameLine(0.0f, 16.0f * t.scale);
			Toggle("Exteriors", s.toggleB);
			Check("Likely homes only", s.check);
			ImGui::SameLine();
			Radio("None", s.radio, 0);
			ImGui::SameLine();
			Radio("By category", s.radio, 1);
			ImGui::SameLine();
			Radio("By fill", s.radio, 2);
			ImGui::SetNextItemWidth(220.0f * t.scale);
			ImGui::SliderFloat("Height cut", &s.slider, 0.0f, 1.0f);
			ImGui::SetNextItemWidth(220.0f * t.scale);
			const char* items[] = { "Lux templates", "My templates", "Overwrite" };
			ImGui::Combo("Save to", &s.combo, items, 3);
			static char label[64] = "Potion Basket";
			ImGui::SetNextItemWidth(220.0f * t.scale);
			ImGui::InputText("Label", label, sizeof(label));

			Section("Table");
			if (ImGui::BeginTable("t", 4, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
				ImGui::TableSetupColumn("Container");
				ImGui::TableSetupColumn("Role");
				ImGui::TableSetupColumn("Items", ImGuiTableColumnFlags_WidthFixed, 60.0f * t.scale);
				ImGui::TableSetupColumn("Ref", ImGuiTableColumnFlags_WidthFixed, 150.0f * t.scale);
				ImGui::TableHeadersRow();
				const char* names[] = { "Potion Basket", "Strongbox", "Weapon Rack", "Bookshelf" };
				const char* roles[] = { "alchemy.potions", "master", "weapons", "books" };
				for (int r = 0; r < 4; ++r) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::PushID(r);
					ImGui::Selectable(names[r], r == 1, ImGuiSelectableFlags_SpanAllColumns);
					ImGui::PopID();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(roles[r]);
					ImGui::TableNextColumn();
					ImGui::Text("%d", 12 + r * 7);
					ImGui::TableNextColumn();
					PushFont(Font::Mono);
					ImGui::PushStyleColor(ImGuiCol_Text, t.muted);
					ImGui::Text("0x000B%04X", 0x1B8C + r);
					ImGui::PopStyleColor();
					PopFont();
				}
				ImGui::EndTable();
			}
		}

		void ColumnB(ImVec2 a_viewMin, ImVec2 a_viewMax)
		{
			(void)a_viewMin;
			(void)a_viewMax;
			DemoState&   s = S();
			const Theme& t = Th();

			Section("Icons");
			const float cell = 92.0f * t.scale;
			const int   perRow = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell));
			for (std::size_t i = 0; i < kIcons.size(); ++i) {
				if (i % static_cast<std::size_t>(perRow) != 0) {
					ImGui::SameLine(0.0f, 0.0f);
				}
				ImGui::BeginGroup();
				const ImVec2 p = ImGui::GetCursorScreenPos();
				ImDrawList*  dl = ImGui::GetWindowDrawList();
				const ImU32  col = kIcons[i].icon == Icon::Emblem ? t.accent : t.text;
				DrawIcon(dl, kIcons[i].icon, p + ImVec2(4.0f * t.scale, 2.0f * t.scale), t.iconSize, col);
				DrawIcon(dl, kIcons[i].icon, p + ImVec2(28.0f * t.scale, 0.0f), 24.0f * t.scale, col);
				ImGui::Dummy(ImVec2(cell, 26.0f * t.scale));
				PushFont(Font::Small);
				ImGui::PushStyleColor(ImGuiCol_Text, t.muted);
				ImGui::TextUnformatted(kIcons[i].name);
				ImGui::PopStyleColor();
				PopFont();
				ImGui::EndGroup();
			}

			Section("Thumbnails");
			{
				std::shared_ptr<render::Scene> scene;
				{
					std::lock_guard lock(D().mutex);
					scene = D().scene;
				}
				if (!scene || !D().thumbs) {
					Muted("Loading Breezehome…");
				} else if (!scene->Progress().finished) {
					const auto pr = scene->Progress();
					Progress("meshes", pr.modelsDone, pr.modelsTotal, 120.0f * t.scale);
				} else {
					D().thumbs->SetBackground(0xFF282222);
					int shown = 0;
					for (const auto& r : scene->Desc().refs) {
						if (!r.selectable || r.ghost || r.model.empty() || r.index < 0 || r.kind != render::RefKind::Container || shown >= 6) {
							continue;
						}
						if (shown > 0) {
							ImGui::SameLine(0.0f, t.gap);
						}
						const ImTextureID tex = D().thumbs->For(*scene, r.index);
						const float       sz = 96.0f * t.scale;
						const ImVec2      p = ImGui::GetCursorScreenPos();
						ImDrawList*       tdl = ImGui::GetWindowDrawList();
						tdl->AddRectFilled(p, p + ImVec2(sz, sz), t.bg2, t.radius);
						if (tex) {
							tdl->AddImageRounded(ImTextureRef(tex), p, p + ImVec2(sz, sz), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, t.radius);
						}
						tdl->AddRect(p, p + ImVec2(sz, sz), t.line, t.radius);
						{
							const auto  slash = r.model.find_last_of('\\');
							const char* name = r.model.c_str() + (slash == std::string::npos ? 0 : slash + 1);
							ImFont*     f = FontOf(Font::Small);
							tdl->PushClipRect(p, p + ImVec2(sz, sz), true);
							tdl->AddText(f, f->LegacySize, p + ImVec2(4.0f * t.scale, sz - f->LegacySize - 3.0f * t.scale), t.muted, name);
							tdl->PopClipRect();
						}
						ImGui::Dummy(ImVec2(sz, sz));
						++shown;
					}
				}
			}

			Section("Viewport");
			const ImVec2 vp = ImGui::GetCursorScreenPos();
			const float  vw = ImGui::GetContentRegionAvail().x;
			const float  vh = 210.0f * t.scale;
			ImDrawList*  dl = ImGui::GetWindowDrawList();
			const ImVec2 vmax = vp + ImVec2(vw, vh);
			dl->AddRectFilledMultiColor(vp, vmax, IM_COL32(44, 46, 52, 255), IM_COL32(44, 46, 52, 255), IM_COL32(24, 24, 28, 255), IM_COL32(24, 24, 28, 255));
			for (int i = 1; i < 10; ++i) {
				const float x = vp.x + vw * static_cast<float>(i) / 10.0f;
				dl->AddLine(ImVec2(x, vp.y + vh * 0.55f), ImVec2(vp.x + vw * 0.5f + (x - vp.x - vw * 0.5f) * 1.8f, vmax.y), IM_COL32(255, 255, 255, 14));
			}
			if (const ImTextureID paper = ArtTexture(Art::Paper)) {
				dl->AddImage(ImTextureRef(paper), vp + ImVec2(vw * 0.62f, 50.0f * t.scale), vp + ImVec2(vw - 12.0f * t.scale, 140.0f * t.scale), ImVec2(0, 0), ImVec2(1, 1),
					IM_COL32(255, 255, 255, 51));
			}
			dl->AddRect(vp + ImVec2(vw * 0.36f, vh * 0.42f), vp + ImVec2(vw * 0.52f, vh * 0.7f), t.select, 0.0f, 0, 2.0f * t.scale);
			ImGui::Dummy(ImVec2(vw, vh));
			const ImVec2 after = ImGui::GetCursorScreenPos();
			const ImVec2 wpos = ImGui::GetWindowPos();
			if (BeginOverlay("toolbar", vp - wpos + ImVec2(t.pad, t.pad))) {
				ToolButton("Select", !s.paint, "Select", "S");
				ImGui::SameLine();
				ToolButton("Paint", s.paint, "Paint", "P");
				ImGui::SameLine();
				ToolbarDivider();
				ImGui::SameLine();
				if (BeginPopover("layers", "Layers", Icon::Layers, 0, "Layers", "L")) {
					Check("Containers", s.layers[0]);
					Check("Displays", s.layers[1]);
					Check("Doors", s.layers[2]);
					Check("Clutter", s.layers[3]);
					ImGui::Separator();
					MenuRow("All on");
					MenuRow("Height cut", "H");
					MenuRow("Unavailable", nullptr, false);
					EndPopover();
				}
				ImGui::SameLine();
				if (BeginPopover("colours", "Colours", Icon::Palette, 1, "Colours", "C")) {
					EndPopover();
				}
				ImGui::SameLine();
				if (BeginPopover("camera", "Camera", Icon::Camera, 0, "Camera")) {
					EndPopover();
				}
				EndOverlay();
			}
			HintLine("Right mouse look · WASD fly · F frame · Home overview", vp, vmax);
			ImGui::SetCursorScreenPos(after);

			Section("Rows, cards, keys");
			ImGui::BeginGroup();
			Card("open", "Open a home", "Pick one in the list on the left.", "Ctrl+1", 230.0f * t.scale);
			ImGui::SameLine();
			Card("draft", "Draft all homes", "Suggest a layout for every home.", "Ctrl+D", 230.0f * t.scale);
			ImGui::EndGroup();
			KeyCap("Ctrl+K");
			ImGui::SameLine(0.0f, 4.0f * t.scale);
			KeyCap("F");
			ImGui::SameLine(0.0f, 4.0f * t.scale);
			KeyCap("Esc");
			ImGui::SameLine(0.0f, t.pad);
			ImGui::AlignTextToFramePadding();
			Progress("preview", 0, 0, 80.0f * t.scale);
			ImGui::SameLine(0.0f, t.pad);
			Progress("scan", 12, 40, 80.0f * t.scale);

			Section("Empty state");
			ImGui::BeginChild("empty", ImVec2(0, 150.0f * t.scale), ImGuiChildFlags_Borders);
			EmptyState("Nothing selected", "Click a chest, rack or mannequin. Ctrl+click adds, drag boxes.");
			ImGui::EndChild();
		}
	}

	void DrawDemo()
	{
		DemoState&   s = S();
		const Theme& t = Th();
		if (++s.frame == 1) {
			s.railW *= t.scale;
			s.rightW *= t.scale;
		}
		if (s.frame == 3) {
			OpenPopover("layers");  // so a screenshot shows a popover
		}
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(vp->WorkPos);
		ImGui::SetNextWindowSize(vp->WorkSize);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::Begin("##demo", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
		ImGui::PopStyleVar();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		TitleRow();
		ImGui::PopStyleVar();
		const float bodyH = ImGui::GetContentRegionAvail().y - t.statusH;
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		ImGui::BeginChild("body", ImVec2(0, bodyH), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
		ImGui::PopStyleVar();
		LeftRail();
		ImGui::SameLine(0.0f, 0.0f);
		const float rightW = s.rightOpen ? s.rightW : t.stripW;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * t.scale, 10.0f * t.scale));
		ImGui::BeginChild("centre", ImVec2(ImGui::GetContentRegionAvail().x - rightW, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::PopStyleVar();
		if (ImGui::BeginTable("cols", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX)) {
			ImGui::TableNextColumn();
			ColumnA();
			ImGui::TableNextColumn();
			ColumnB(ImVec2(0, 0), ImVec2(0, 0));
			ImGui::EndTable();
		}
		ImGui::EndChild();
		ImGui::SameLine(0.0f, 0.0f);
		if (BeginRail("inspector", Side::Right, s.rightW, s.rightOpen, 240.0f * t.scale, 480.0f * t.scale, "Inspector")) {
			EndRail();
		}
		ImGui::EndChild();
		const ImVec2 wmin = ImGui::GetWindowPos();
		const ImVec2 wmax = wmin + ImGui::GetWindowSize();
		StatusRow(ImVec2(wmin.x, wmax.y - t.statusH), wmax);
		ImGui::End();
	}

	void SetDemoDevice(ID3D11Device* a_device, ID3D11DeviceContext* a_context)
	{
		D().thumbs = std::make_unique<render::Thumbnails>(a_device, a_context);
	}

	void SetDemoScene(std::shared_ptr<render::Scene> a_scene)
	{
		std::lock_guard lock(D().mutex);
		D().scene = std::move(a_scene);
	}

	void ReleaseDemo()
	{
		std::lock_guard lock(D().mutex);
		D().thumbs.reset();
		D().scene.reset();
	}
}
