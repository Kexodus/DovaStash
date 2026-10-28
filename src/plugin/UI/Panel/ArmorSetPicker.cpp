#include "UI/Panel/ArmorSetPicker.h"

#include "UI/ArmorSetMenu.h"
#include "UI/Panel/ArmorSetsModel.h"
#include "UI/Panel/Strings.h"

#include <cstring>

// The vendored header uses std::filesystem without including it and includes <windows.h>
// (WIN32_LEAN_AND_MEAN / NOMINMAX come from CMake). Keep it the last include of this file.
#include <filesystem>

#include <SKSEMenuFramework.h>

#include "UI/Panel/Widgets.h"  // needs ImGuiMCP from the header above

// RENDER THREAD ONLY: read ArmorSetsModel snapshots, post the choice to the main thread. Layout, wording and widget
// conventions: UI/Panel/Strings.h.

namespace dova::ui::panel
{
	namespace
	{
		namespace ImGui = ImGuiMCP;
		using namespace widgets;

		constexpr std::size_t kMaxRows = 500;

		struct PickerState
		{
			std::array<char, 96> search{};
			bool                 onlyHave{ false };

			// filter cache
			std::uint64_t            filteredSerial{ 0 };
			std::string              filteredQuery;
			bool                     filteredOnlyHave{ false };
			std::vector<std::size_t> filtered;
		};
		PickerState g_picker;

		std::string Query()
		{
			return std::string(g_picker.search.data(), strnlen(g_picker.search.data(), g_picker.search.size()));
		}

		// The sets that match the search (and "Only sets you have"): complete ones first, then by pieces the player
		// has, then load order.
		const std::vector<std::size_t>& Filtered(const ArmorSetsSnapshot& a_snap)
		{
			const auto query = Query();
			if (g_picker.filteredSerial == a_snap.serial && g_picker.filteredQuery == query && g_picker.filteredOnlyHave == g_picker.onlyHave) {
				return g_picker.filtered;
			}
			auto&       out = g_picker.filtered;
			const auto& index = *a_snap.index;
			out.clear();
			for (std::size_t s = 0; s < index.sets.size(); ++s) {
				if (g_picker.onlyHave && a_snap.coverage[s].pieces == 0) {
					continue;
				}
				if (core::ArmorSetMatches(index.sets[s], index.pieces, query)) {
					out.push_back(s);
				}
			}
			std::ranges::stable_sort(out, [&](std::size_t a_lhs, std::size_t a_rhs) {
				const auto& l = a_snap.coverage[a_lhs];
				const auto& r = a_snap.coverage[a_rhs];
				if ((l.pieces > 0) != (r.pieces > 0)) {
					return l.pieces > 0;
				}
				if (l.Complete() != r.Complete()) {
					return l.Complete();
				}
				return l.have > r.have;
			});
			g_picker.filteredSerial = a_snap.serial;
			g_picker.filteredQuery = query;
			g_picker.filteredOnlyHave = g_picker.onlyHave;
			return out;
		}
	}

	void RenderArmorSetPicker(std::uint32_t a_cellId, const core::FormRef& a_mannequin, const std::optional<core::FormRef>& a_set,
		const std::function<void(std::function<void()>)>& a_post)
	{
		ImGui::BeginDisabled(!a_set);
		if (ImGui::Button(text::kAuto)) {
			a_post([a_cellId, a_mannequin]() { ChooseArmorSet(a_cellId, a_mannequin, std::nullopt); });
		}
		ImGui::EndDisabled();
		Tooltip(text::kAutoTip);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(220.0f);
		ImGui::InputTextWithHint("##setsearch", text::kSetsSearchHint, g_picker.search.data(), g_picker.search.size());
		Tooltip(text::kSetsSearchTip);
		ImGui::SameLine();
		ImGui::Checkbox(text::kSetsOnlyHave, &g_picker.onlyHave);
		Tooltip(text::kSetsOnlyHaveTip);

		auto& model = ArmorSetsModel::Get();
		model.RequestRefresh();
		const auto snap = model.Snapshot();
		if (!snap || !snap->index) {
			Disabled(text::kLoading);  // the first open reads every armor piece of the load order once
			return;
		}
		const auto& index = *snap->index;
		const auto& rows = Filtered(*snap);
		ImGui::SameLine();
		Disabled(std::format(text::kSetsCount, rows.size(), index.sets.size()));

		std::optional<std::size_t> current;
		if (a_set) {
			if (const auto it = index.setOf.find(*a_set); it != index.setOf.end()) {
				current = it->second;
			}
		}
		if (rows.empty()) {
			Disabled(text::kSetsNoMatch);
			return;
		}
		if (ImGui::BeginTable("##armorsets", 4, kScrollTableFlags, ImGui::ImVec2{ 0.0f, TableHeight(std::min(rows.size(), kMaxRows), 10) })) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn(text::kColSet, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.4f);
			ImGui::TableSetupColumn(text::kColMod, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.34f);
			ImGui::TableSetupColumn(text::kColPieces, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.12f);
			ImGui::TableSetupColumn(text::kColHave, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.14f);
			ImGui::TableHeadersRow();
			for (std::size_t r = 0; r < rows.size() && r < kMaxRows; ++r) {
				const auto  s = rows[r];
				const auto& set = index.sets[s];
				const auto& coverage = snap->coverage[s];
				ImGui::PushID(static_cast<int>(s));
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (ImGui::Selectable(set.name.c_str(), current == s, ImGui::ImGuiSelectableFlags_SpanAllColumns)) {
					a_post([a_cellId, a_mannequin, id = set.id]() { ChooseArmorSet(a_cellId, a_mannequin, id); });
				}
				if (ImGui::IsItemHovered()) {
					std::string pieces;
					for (const auto i : set.pieces) {
						const auto& p = index.pieces[i];
						pieces += std::format("{}{}{}", pieces.empty() ? "" : "\n", p.name, snap->have.contains(p.form) ? text::kSetsHavePiece : "");
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
					Colored(kGreen, std::format(text::kHaveOfTotal, coverage.have, coverage.total));
				} else {
					Text(std::format(text::kHaveOfTotal, coverage.have, coverage.total));
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (rows.size() > kMaxRows) {
			Disabled(std::format(text::kSetsTooMany, kMaxRows));
		}
	}
}
