#include "UI/Panel/ArmorSetsPage.h"

#include "UI/ArmorSetMenu.h"
#include "UI/Panel/ArmorSetsModel.h"
#include "UI/Panel/PanelModel.h"
#include "UI/Panel/Strings.h"

#include <cstring>

// The vendored header uses std::filesystem without including it and includes <windows.h>
// (WIN32_LEAN_AND_MEAN / NOMINMAX come from CMake). Keep it the last include of this file.
#include <filesystem>

#include <SKSEMenuFramework.h>

#include "UI/Panel/Widgets.h"  // needs ImGuiMCP from the header above

// RENDER THREAD ONLY: read ArmorSetsModel snapshots, post actions to the main thread. Layout, wording and widget
// conventions: UI/Panel/Strings.h.

namespace dova::ui::panel
{
	namespace
	{
		namespace ImGui = ImGuiMCP;
		using namespace widgets;

		constexpr std::size_t kMaxRows = 500;

		struct PageState
		{
			std::array<char, 96>         search{};
			bool                         onlyHave{ false };
			std::optional<core::FormRef> mannequin;
			std::optional<std::size_t>   selected;  // index into index->sets
			std::uint32_t                cellId{ 0 };

			// filter cache
			std::uint64_t            filteredSerial{ 0 };
			std::string              filteredQuery;
			bool                     filteredOnlyHave{ false };
			std::vector<std::size_t> filtered;
		};
		PageState g_page;

		std::string Query()
		{
			return std::string(g_page.search.data(), strnlen(g_page.search.data(), g_page.search.size()));
		}

		const std::vector<std::size_t>& Filtered(const ArmorSetsSnapshot& a_snap)
		{
			const auto query = Query();
			if (g_page.filteredSerial == a_snap.serial && g_page.filteredQuery == query && g_page.filteredOnlyHave == g_page.onlyHave) {
				return g_page.filtered;
			}
			g_page.filtered.clear();
			const auto& index = *a_snap.index;
			for (std::size_t s = 0; s < index.sets.size(); ++s) {
				if (g_page.onlyHave && a_snap.coverage[s].pieces == 0) {
					continue;
				}
				if (core::ArmorSetMatches(index.sets[s], index.pieces, query)) {
					g_page.filtered.push_back(s);
				}
			}
			g_page.filteredSerial = a_snap.serial;
			g_page.filteredQuery = query;
			g_page.filteredOnlyHave = g_page.onlyHave;
			return g_page.filtered;
		}

		const MannequinRow* SelectedMannequin(const ArmorSetsSnapshot& a_snap)
		{
			if (a_snap.cellId != g_page.cellId) {
				g_page.cellId = a_snap.cellId;
				g_page.mannequin.reset();
			}
			if (!g_page.mannequin && !a_snap.mannequins.empty()) {
				g_page.mannequin = a_snap.mannequins.front().ref;
			}
			const auto it = std::ranges::find_if(a_snap.mannequins, [](const MannequinRow& m) { return g_page.mannequin && m.ref == *g_page.mannequin; });
			return it != a_snap.mannequins.end() ? &*it : nullptr;
		}

		std::string MannequinLabel(const MannequinRow& a_row)
		{
			return std::format(text::kSetsMannequinRow, a_row.name, a_row.setName) + (a_row.used ? "" : text::kSetsNotUsed);
		}

		void RenderMannequinPicker(const ArmorSetsSnapshot& a_snap, const MannequinRow* a_current)
		{
			if (!a_snap.inHome) {
				Disabled(text::kSetsNotInHome);
				return;
			}
			if (!a_snap.enabled) {
				Disabled(text::kNeedsSetup);
				return;
			}
			if (a_snap.mannequins.empty()) {
				Disabled(text::kSetsNoMannequins);
				return;
			}
			const auto preview = a_current ? MannequinLabel(*a_current) : std::string(text::kNone);
			if (ImGui::BeginCombo(text::kSetsMannequin, preview.c_str())) {
				for (const auto& m : a_snap.mannequins) {
					ImGui::PushID(m.ref.ToString().c_str());
					const bool selected = a_current && a_current->ref == m.ref;
					if (ImGui::Selectable(MannequinLabel(m).c_str(), selected)) {
						g_page.mannequin = m.ref;
					}
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}
		}

		void RenderActions(const ArmorSetsSnapshot& a_snap, const MannequinRow* a_mannequin)
		{
			if (!a_mannequin) {
				return;
			}
			const auto& index = *a_snap.index;
			const bool  haveSet = g_page.selected && *g_page.selected < index.sets.size();
			const auto  cellId = a_snap.cellId;
			const auto  ref = a_mannequin->ref;
			if (haveSet) {
				const auto& set = index.sets[*g_page.selected];
				if (ImGui::Button(std::format(text::kSetsAssign, set.name, a_mannequin->name).c_str())) {
					ArmorSetsModel::Get().Post([cellId, ref, id = set.id]() { ChooseArmorSet(cellId, ref, id); });
				}
				Tooltip(text::kSetsAssignTip);
			} else {
				ImGui::AlignTextToFramePadding();
				Disabled(text::kSetsPickSet);
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(!a_mannequin->set);
			if (ImGui::Button(text::kAuto)) {
				ArmorSetsModel::Get().Post([cellId, ref]() { ChooseArmorSet(cellId, ref, std::nullopt); });
			}
			ImGui::EndDisabled();
			Tooltip(text::kAutoTip);
		}

		void RenderTable(const ArmorSetsSnapshot& a_snap, const std::vector<std::size_t>& a_rows)
		{
			const auto& index = *a_snap.index;
			if (!ImGui::BeginTable("##armorsets", 4, kScrollTableFlags, ImGui::ImVec2{ 0.0f, TableHeight(std::min(a_rows.size(), kMaxRows), 16) })) {
				return;
			}
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn(text::kColSet, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.4f);
			ImGui::TableSetupColumn(text::kColMod, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.34f);
			ImGui::TableSetupColumn(text::kColPieces, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.12f);
			ImGui::TableSetupColumn(text::kColHave, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.14f);
			ImGui::TableHeadersRow();
			for (std::size_t r = 0; r < a_rows.size() && r < kMaxRows; ++r) {
				const auto  s = a_rows[r];
				const auto& set = index.sets[s];
				const auto& coverage = a_snap.coverage[s];
				ImGui::PushID(static_cast<int>(s));
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (ImGui::Selectable(set.name.c_str(), g_page.selected == s, ImGui::ImGuiSelectableFlags_SpanAllColumns)) {
					g_page.selected = s;
				}
				if (ImGui::IsItemHovered()) {
					std::string pieces;
					for (const auto i : set.pieces) {
						const auto& p = index.pieces[i];
						pieces += std::format("{}{}{}", pieces.empty() ? "" : "\n", p.name, a_snap.have.contains(p.form) ? text::kSetsHavePiece : "");
					}
					ImGui::SetTooltip("%s", pieces.c_str());
				}
				ImGui::TableNextColumn();
				Text(set.plugin);
				ImGui::TableNextColumn();
				Text(std::to_string(set.pieces.size()));
				ImGui::TableNextColumn();
				if (coverage.pieces == 0) {
					Disabled(text::kNone);
				} else if (coverage.Complete()) {
					Colored(kGreen, std::format("{}/{}", coverage.have, coverage.total));
				} else {
					Text(std::format("{}/{}", coverage.have, coverage.total));
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
	}

	void __stdcall RenderArmorSetsPage()
	{
		auto& model = ArmorSetsModel::Get();
		model.RequestRefresh();
		const auto snap = model.Snapshot();
		if (!snap || !snap->index) {
			Disabled(text::kLoading);
			return;
		}
		Hint(text::kSetsIntro);
		ImGui::Spacing();
		const auto* mannequin = SelectedMannequin(*snap);
		RenderMannequinPicker(*snap, mannequin);

		ImGui::SetNextItemWidth(260.0f);
		ImGui::InputTextWithHint("##search", text::kSetsSearchHint, g_page.search.data(), g_page.search.size());
		ImGui::SameLine();
		ImGui::Checkbox(text::kSetsOnlyHave, &g_page.onlyHave);
		const auto& rows = Filtered(*snap);
		ImGui::SameLine();
		Disabled(std::format(text::kSetsCount, rows.size(), snap->index->sets.size()));

		RenderTable(*snap, rows);
		if (rows.size() > kMaxRows) {
			Disabled(std::format(text::kSetsTooMany, kMaxRows));
		}
		RenderActions(*snap, mannequin);
		StatusLine(PanelModel::Get().Status());
	}
}
