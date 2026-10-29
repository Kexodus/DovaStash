// The start panel (docs/EDITOR-UX.md §2.6), drawn in the viewport when no home is open or the emblem is clicked:
// the setup in one line (or the instance card when none is found), how many homes and templates, three cards
// (open a home, draft all homes, open a template) and the homes in progress, which persist between runs (ui/State).

#include <algorithm>
#include <format>

#include <imgui.h>
#include <imgui_internal.h>

#include "Util.h"
#include "ui/App.h"
#include "ui/Art.h"
#include "ui/Common.h"

namespace dova::editor::ui
{
	using namespace detail;

	namespace
	{
		w::Kind StageKind(HomeStage a_stage)
		{
			switch (a_stage) {
			case HomeStage::Saved:
				return w::Kind::Ok;
			case HomeStage::Validated:
				return w::Kind::Ok;
			case HomeStage::Reviewed:
				return w::Kind::Info;
			case HomeStage::Drafted:
				return w::Kind::Warn;
			default:
				return w::Kind::Neutral;
			}
		}

	}

	void EditorApp::DrawStart(ImVec2 a_min, ImVec2 a_max)
	{
		const auto& t = w::Th();
		ImGui::SetCursorScreenPos(a_min);
		if (!w::BeginPadded("start", ImVec2(a_max.x - a_min.x, a_max.y - a_min.y))) {
			w::EndPadded();
			return;
		}
		const float avail = ImGui::GetContentRegionAvail().x;
		const float colW = std::min(avail - 2.0f * t.pad, 720.0f * t.scale);
		const float x0 = std::max((avail - colW) * 0.5f, t.pad);
		ImGui::Dummy(ImVec2(0, std::max((a_max.y - a_min.y) * 0.08f, t.pad)));
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x0);  // x0 is within the padded content region
		ImGui::BeginGroup();
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + colW);
		{
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float  es = 30.0f * t.scale;  // a little taller than the title
			w::DrawIcon(ImGui::GetWindowDrawList(), w::Icon::Emblem, p, es, t.accent);
			ImGui::SetCursorScreenPos(ImVec2(p.x + es + t.pad, p.y));
			w::Title("Dova Stash Editor");
		}
		w::Spacer();

		// 1. Setup in one line, or the instance card.
		if (!_ws) {
			ImGui::PushStyleColor(ImGuiCol_ChildBg, t.bg2);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(t.pad * 1.5f, t.pad * 1.5f));
			ImGui::BeginChild("instancecard", ImVec2(colW, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);
			w::Heading("MO2 instance or Data folder");
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 170.0f * t.scale);
			const bool enter = ImGui::InputTextWithHint("##mo2card", "MO2 instance or Skyrim Data folder", _mo2Path, sizeof(_mo2Path), ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			if (w::Button("Browse...")) {
				if (auto p = PickFolder()) {
					std::snprintf(_mo2Path, sizeof(_mo2Path), "%s", p->c_str());
				}
			}
			ImGui::SameLine();
			if (w::PrimaryButton("Open", _mo2Path[0] != '\0') || enter) {
				ApplySetup(_mo2Path[0] ? std::optional(PathFromUtf8(_mo2Path)) : std::nullopt, std::nullopt);
			}
			// The Skyrim instances MO2 registered (nxmhandler.ini): one click opens one, none opens unasked.
			if (!_registeredInstances) {
				_registeredInstances = mo2::RegisteredInstances();
			}
			for (const auto& dir : *_registeredInstances) {
				const auto path = PathToUtf8(dir);
				ImGui::PushID(path.c_str());
				if (w::LinkButton(path.c_str())) {
					std::snprintf(_mo2Path, sizeof(_mo2Path), "%s", path.c_str());
					ApplySetup(dir, std::nullopt);
				}
				w::Tip("Open this MO2 instance (MO2 lists it for Nexus links)");
				ImGui::PopID();
			}
			if (_wsError.starts_with(Workspace::kNoInstance)) {
				w::MutedWrapped(kPickSetup);  // nothing failed: nothing was given (the command-line wording is for the console)
			} else if (!_wsError.empty()) {
				w::ColouredWrapped(w::Kind::Error, _wsError.c_str());
			}
			ImGui::EndChild();
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
			ImGui::PopTextWrapPos();
			ImGui::EndGroup();
			w::EndPadded();
			return;
		}
		{
			int enabled = 0;
			for (const auto& m : _ws->Mods()) {
				enabled += m.enabled && !m.separator && !m.unmanaged ? 1 : 0;
			}
			const std::string where = _ws->Instance() ? PathToUtf8(_ws->Instance()->root.filename()) : PathToUtf8(_ws->DataDir());
			const std::string line = _ws->Instance() ? std::format("{} · {} · {} mods enabled", where, _ws->Profile(), enabled) : std::format("Data folder {}", where);
			w::Muted(line.c_str());
			w::Tip(_ws->Describe().c_str());
			ImGui::SameLine(0.0f, t.pad);
			if (w::LinkButton("Change...")) {
				w::OpenPopover("setup");
			}
		}

		// 2. Homes and templates.
		if (_homes) {
			int templated = 0;
			for (const auto& h : _homes->homes) {
				templated += HomeStatus(h).empty() ? 0 : 1;
			}
			// The plugin count and read time are the rail footer's (Rescan); not repeated here.
			w::Heading(std::format("{} player homes, {} with templates", _homes->homes.size(), templated).c_str());
		} else {
			w::Heading("Finding the player homes...");
		}
		w::Spacer(2.0f * t.gap);

		// 3. Three cards.
		const bool  row = colW >= 560.0f * t.scale;
		const float cw = row ? (colW - 2.0f * t.pad) / 3.0f : colW;  // parted by the item spacing, not the inline gap
		const char* bodies[] = { "Find it in the rail: name, mod or Nexus id.", "Masters and categories, with reasons.", "Shipped, or a layout saved in game." };
		int         lines = 1;  // the most any card wraps to: the row shares one bottom edge, with no empty line under it
		for (const char* b : bodies) {
			lines = std::max(lines, w::CardBodyLines(b, cw));
		}
		if (w::Card("open", "Open a home", bodies[0], "Ctrl+F", cw, lines)) {
			FindAHome();
		}
		if (row) {
			ImGui::SameLine(0.0f, t.pad);
		}
		if (w::Card("draftall", "Draft all homes", bodies[1], "Ctrl+D", cw, lines)) {
			StartBulkDraft();
			_showStart = false;
		}
		if (row) {
			ImGui::SameLine(0.0f, t.pad);
		}
		if (w::Card("opentemplate", "Open a template...", bodies[2], "Ctrl+O", cw, lines)) {
			OpenTemplateFile();
		}

		// 4. In progress.
		if (!_state.homes.empty()) {
			w::SectionHead("In progress");
			std::vector<std::pair<std::string, const HomeRecord*>> rows;
			for (const auto& [cell, r] : _state.homes) {
				rows.emplace_back(cell, &r);
			}
			std::ranges::sort(rows, [](const auto& a, const auto& b) { return a.second->updated > b.second->updated; });
			// Sized to its rows: the start panel scrolls, the table never does.
			if (ImGui::BeginTable("todo", 4, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp, ImVec2(colW, 0.0f))) {
				ImGui::TableSetupColumn("Home", ImGuiTableColumnFlags_WidthStretch, 0.32f);
				ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed, 80.0f * t.scale);
				ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthFixed, 120.0f * t.scale);
				ImGui::TableSetupColumn("Saved to", ImGuiTableColumnFlags_WidthStretch, 0.68f);
				ImGui::TableHeadersRow();
				for (const auto& [cell, r] : rows) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::PushID(cell.c_str());
					// The name ends in "..." when the column is narrow; the tooltip has all of it.
					const char*  name = r->name.empty() ? cell.c_str() : r->name.c_str();
					const ImVec2 np = ImGui::GetCursorScreenPos();
					const float  nw = ImGui::GetContentRegionAvail().x;
					if (ImGui::Selectable("##home", false, ImGuiSelectableFlags_SpanAllColumns) && _homes) {
						for (std::size_t i = 0; i < _homes->homes.size(); ++i) {
							if (_homes->homes[i].cellRef == cell) {
								OpenHome(static_cast<int>(i));
							}
						}
					}
					const ImVec2 ns = ImGui::CalcTextSize(name);
					ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), np, ImVec2(np.x + nw, np.y + ns.y), np.x + nw, name, nullptr, &ns);
					w::Tip((r->name.empty() ? cell : r->name + "\n" + cell).c_str());
					ImGui::PopID();
					ImGui::TableNextColumn();
					w::Chip(HomeStageName(r->stage), StageKind(r->stage));
					ImGui::TableNextColumn();
					w::Muted(r->updated.c_str());
					ImGui::TableNextColumn();
					w::Muted(r->savedPath.c_str());
					w::Tip(r->savedPath.c_str());
				}
				ImGui::EndTable();
			}
		}
		ImGui::PopTextWrapPos();
		ImGui::EndGroup();
		w::EndPadded();
	}
}
