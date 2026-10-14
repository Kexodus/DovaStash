#include "UI/Panel/DisplaysPage.h"

#include "Controller.h"
#include "UI/ArmorSetMenu.h"
#include "UI/Panel/ArmorSetsModel.h"
#include "UI/Panel/DisplaysModel.h"
#include "UI/Panel/PanelModel.h"
#include "UI/Panel/Strings.h"

#include <cctype>
#include <cstring>

// The vendored header uses std::filesystem without including it and includes <windows.h>
// (WIN32_LEAN_AND_MEAN / NOMINMAX come from CMake). Keep it the last include of this file.
#include <filesystem>

#include <SKSEMenuFramework.h>

#include "UI/Panel/Widgets.h"  // needs ImGuiMCP from the header above

// RENDER THREAD ONLY: read DisplaysModel / ArmorSetsModel snapshots, post Controller actions to the main
// thread (they re-check the captured cell id, as the hotkey's display menu does). Layout, wording and widget
// conventions: UI/Panel/Strings.h.

namespace dova::ui::panel
{
	namespace
	{
		namespace ImGui = ImGuiMCP;
		using namespace widgets;

		constexpr std::size_t kMaxRows = 500;
		constexpr std::size_t kMaxSets = 100;

		enum Status : int
		{
			kAnyStatus,
			kUsed,
			kNotUsed,
			kIgnored
		};
		enum Contents : int
		{
			kAnyContents,
			kEmpty,
			kHolds,
			kRoom
		};
		enum Sort : int
		{
			kByName,
			kByKind,
			kByArea,
			kByStatus
		};

		struct PageState
		{
			std::array<char, 96>             search{};
			std::optional<core::DisplayKind> kind;
			int                              status{ kAnyStatus };
			int                              contents{ kAnyContents };
			int                              sort{ kByName };
			std::optional<core::FormRef>     selected;
			std::uint32_t                    cellId{ 0 };
			std::string                      confirm;  // key of the inline confirmation being shown
			std::array<char, 64>             setSearch{};

			// filter cache
			std::uint64_t            filteredSerial{ 0 };
			std::string              filteredKey;
			std::vector<std::size_t> filtered;

			// armor set list cache
			std::uint64_t            setsSerial{ 0 };
			std::string              setsQuery;
			std::vector<std::size_t> sets;
		};
		PageState g_page;

		template <std::size_t N>
		std::string FromBuffer(const std::array<char, N>& a_buffer)
		{
			return std::string(a_buffer.data(), strnlen(a_buffer.data(), N));
		}

		bool LessNoCase(std::string_view a_lhs, std::string_view a_rhs)
		{
			return std::ranges::lexicographical_compare(a_lhs, a_rhs, [](char a, char b) {
				return std::tolower(static_cast<unsigned char>(a)) < std::tolower(static_cast<unsigned char>(b));
			});
		}

		// Runs `a_fn` on the main thread, then rebuilds the page's snapshot.
		void Do(std::function<void()> a_fn)
		{
			DisplaysModel::Get().Post(std::move(a_fn));
		}

		// The page's inline confirmations (one question at a time).
		bool ConfirmButton(const std::string& a_label, const std::string& a_key, const std::string& a_question)
		{
			return widgets::ConfirmButton(g_page.confirm, a_label, a_key, a_question);
		}

		int StatusRank(const DisplayRow& a_row)
		{
			return a_row.used ? 0 : a_row.ignored ? 2 : 1;
		}

		bool Passes(const DisplayRow& a_row, std::string_view a_query)
		{
			if (g_page.kind && a_row.kind != g_page.kind) {
				return false;
			}
			switch (g_page.status) {
			case kUsed:
				if (!a_row.used) {
					return false;
				}
				break;
			case kNotUsed:
				if (a_row.listed) {
					return false;
				}
				break;
			case kIgnored:
				if (!a_row.ignored) {
					return false;
				}
				break;
			default:
				break;
			}
			switch (g_page.contents) {
			case kEmpty:
				if (!a_row.loaded || !a_row.empty) {
					return false;
				}
				break;
			case kHolds:
				if (!a_row.loaded || a_row.empty) {
					return false;
				}
				break;
			case kRoom:
				if (!a_row.loaded || a_row.full) {
					return false;
				}
				break;
			default:
				break;
			}
			// Every word of the query, anywhere in the row.
			std::size_t pos = 0;
			while (pos < a_query.size()) {
				const auto end = std::min(a_query.find(' ', pos), a_query.size());
				if (end > pos && a_row.haystack.find(a_query.substr(pos, end - pos)) == std::string::npos) {
					return false;
				}
				pos = end + 1;
			}
			return true;
		}

		const std::vector<std::size_t>& Filtered(const DisplaysSnapshot& a_snap)
		{
			const auto query = core::ToLower(FromBuffer(g_page.search));
			const auto key = std::format("{}|{}|{}|{}|{}", query, g_page.kind ? static_cast<int>(*g_page.kind) : -1, g_page.status, g_page.contents, g_page.sort);
			if (g_page.filteredSerial == a_snap.serial && g_page.filteredKey == key) {
				return g_page.filtered;
			}
			g_page.filtered.clear();
			for (std::size_t i = 0; i < a_snap.rows.size(); ++i) {
				if (Passes(a_snap.rows[i], query)) {
					g_page.filtered.push_back(i);
				}
			}
			const auto& rows = a_snap.rows;
			std::ranges::stable_sort(g_page.filtered, [&](std::size_t a_lhs, std::size_t a_rhs) {
				const auto& l = rows[a_lhs];
				const auto& r = rows[a_rhs];
				switch (g_page.sort) {
				case kByKind:
					if (l.kindName != r.kindName) {
						return LessNoCase(l.kindName, r.kindName);
					}
					break;
				case kByArea:
					if (l.area != r.area) {
						return LessNoCase(l.area, r.area);
					}
					break;
				case kByStatus:
					if (StatusRank(l) != StatusRank(r)) {
						return StatusRank(l) < StatusRank(r);
					}
					break;
				default:
					break;
				}
				return LessNoCase(l.name, r.name);
			});
			g_page.filteredSerial = a_snap.serial;
			g_page.filteredKey = key;
			return g_page.filtered;
		}

		void RenderFilters(const DisplaysSnapshot& a_snap)
		{
			const auto kindPreview = [&]() -> std::string {
				if (g_page.kind) {
					const auto it = std::ranges::find(a_snap.kinds, *g_page.kind, &DisplayKindInfo::kind);
					if (it != a_snap.kinds.end()) {
						return it->name;
					}
				}
				return text::kDispAnyKind;
			}();
			ImGui::SetNextItemWidth(140.0f);
			if (ImGui::BeginCombo("##kind", kindPreview.c_str())) {
				if (ImGui::Selectable(text::kDispAnyKind, !g_page.kind)) {
					g_page.kind.reset();
				}
				for (const auto& k : a_snap.kinds) {
					const auto label = std::format("{} ({})", k.name, k.total);
					if (ImGui::Selectable(label.c_str(), g_page.kind == k.kind)) {
						g_page.kind = k.kind;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			static constexpr std::array kStatuses{ text::kDispAnyStatus, text::kDispUsedOnly, text::kDispNotUsedOnly, text::kDispIgnoredOnly };
			ImGui::SetNextItemWidth(120.0f);
			ImGui::Combo("##status", &g_page.status, kStatuses.data(), static_cast<int>(kStatuses.size()));
			ImGui::SameLine();
			static constexpr std::array kContents{ text::kDispAnyContents, text::kDispEmptyOnly, text::kDispHoldsOnly, text::kDispRoomOnly };
			ImGui::SetNextItemWidth(140.0f);
			ImGui::Combo("##contents", &g_page.contents, kContents.data(), static_cast<int>(kContents.size()));
			ImGui::SameLine();
			static constexpr std::array kSorts{ text::kDispSortName, text::kDispSortKind, text::kDispSortArea, text::kDispSortStatus };
			ImGui::SetNextItemWidth(130.0f);
			ImGui::Combo("##sort", &g_page.sort, kSorts.data(), static_cast<int>(kSorts.size()));

			ImGui::SetNextItemWidth(260.0f);
			ImGui::InputTextWithHint("##search", text::kDispSearchHint, g_page.search.data(), g_page.search.size());
		}

		void RenderStatusCell(const DisplayRow& a_row)
		{
			if (a_row.used) {
				Colored(kGreen, text::kDispUsed);
			} else if (a_row.ignored) {
				Disabled(text::kDispIgnored);
				Tooltip(text::kDispIgnoredTip);
			} else {
				Disabled(text::kDispNotUsed);
			}
			if (!a_row.loaded) {
				ImGui::SameLine();
				Disabled(std::format("({})", text::kDispNotLoaded));
				Tooltip(text::kDispNotLoadedTip);
			} else if (!a_row.available) {
				ImGui::SameLine();
				Colored(kYellow, std::format("({})", text::kDispNotBuilt));
			}
		}

		void Cell(const std::string& a_text)
		{
			if (a_text.empty()) {
				Disabled(text::kNone);
				return;
			}
			Text(a_text);
			Tooltip(a_text.size() > 40 ? a_text : std::string{});
		}

		void RenderTable(const DisplaysSnapshot& a_snap, const std::vector<std::size_t>& a_rows)
		{
			const int columns = a_snap.multiCell ? 6 : 5;
			if (!ImGui::BeginTable("##displays", columns, kScrollTableFlags, ImGui::ImVec2{ 0.0f, TableHeight(std::min(a_rows.size(), kMaxRows), 12) })) {
				return;
			}
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn(text::kColDisplay, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.24f);
			ImGui::TableSetupColumn(text::kColKind, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.11f);
			if (a_snap.multiCell) {
				ImGui::TableSetupColumn(text::kColArea, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.12f);
			}
			ImGui::TableSetupColumn(text::kColStatus, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.13f);
			ImGui::TableSetupColumn(text::kColTakes, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.22f);
			ImGui::TableSetupColumn(text::kColHolds, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.18f);
			ImGui::TableHeadersRow();
			for (std::size_t r = 0; r < a_rows.size() && r < kMaxRows; ++r) {
				const auto& row = a_snap.rows[a_rows[r]];
				ImGui::PushID(row.refText.c_str());
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				const bool selected = g_page.selected && *g_page.selected == row.ref;
				if (ImGui::Selectable(row.name.c_str(), selected, ImGui::ImGuiSelectableFlags_SpanAllColumns)) {
					g_page.selected = row.ref;
					g_page.confirm.clear();
				}
				Tooltip(row.refText);
				ImGui::TableNextColumn();
				Cell(row.kindName);
				if (a_snap.multiCell) {
					ImGui::TableNextColumn();
					Cell(row.area);
				}
				ImGui::TableNextColumn();
				RenderStatusCell(row);
				ImGui::TableNextColumn();
				Cell(row.takesText);
				ImGui::TableNextColumn();
				Cell(row.holdsText);
				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		// The category picker of a used rack or mannequin: the hotkey picker's choices, as checkboxes.
		void RenderCategories(const DisplaysSnapshot& a_snap, const DisplayRow& a_row)
		{
			const auto it = a_row.fit ? a_snap.choices.find(*a_row.fit) : a_snap.choices.end();
			if (it == a_snap.choices.end()) {
				return;
			}
			const auto summary = a_row.categoriesText.empty() ? (a_row.mannequin ? std::string(text::kDispAnyArmor) : std::string(text::kDispAnyWeapon)) : a_row.categoriesText;
			if (!ImGui::TreeNode("##categories", "%s", std::format(text::kDispCategories, summary).c_str())) {
				Tooltip(text::kDispCategoriesTip);
				return;
			}
			const auto cellId = a_snap.cellId;
			const auto ref = a_row.ref;
			std::string root;  // the last "<root>.*" choice: the ones below it are indented
			for (const auto& choice : it->second) {
				const bool isRoot = choice.pattern.ends_with(".*");
				const auto id = isRoot ? choice.pattern.substr(0, choice.pattern.size() - 2) : choice.pattern;
				const bool below = !isRoot && !root.empty() && core::CategoryPatternMatches(root, id);
				if (isRoot) {
					root = choice.pattern;
				}
				bool       exact = std::ranges::any_of(a_row.categories, [&](const std::string& p) { return core::IEquals(p, choice.pattern); });
				const bool covered = !exact && std::ranges::any_of(a_row.categories, [&](const std::string& p) { return core::CategoryPatternMatches(p, id); });
				if (below) {
					ImGui::Indent();
				}
				ImGui::PushID(choice.pattern.c_str());
				if (ImGui::Checkbox(choice.label.c_str(), &exact)) {
					Do([cellId, ref, pattern = choice.pattern]() { Controller::Get().ToggleDisplayCategory(cellId, ref, pattern); });
				}
				ImGui::PopID();
				if (covered) {
					ImGui::SameLine();
					Disabled(text::kViaWider);
				}
				if (below) {
					ImGui::Unindent();
				}
			}
			ImGui::TreePop();
		}

		// A mannequin's armor set: Auto, or any set of the load order (the Armor sets page's data); the sets
		// the player has first.
		void RenderArmorSet(const DisplaysSnapshot& a_snap, const DisplayRow& a_row)
		{
			if (!ImGui::TreeNode("##armorset", "%s", std::format(text::kDispArmorSet, a_row.setName).c_str())) {
				Tooltip(text::kDispSetTip);
				return;
			}
			const auto cellId = a_snap.cellId;
			const auto ref = a_row.ref;
			ImGui::BeginDisabled(!a_row.hasSet);
			if (ImGui::Button(text::kAuto)) {
				Do([cellId, ref]() { ChooseArmorSet(cellId, ref, std::nullopt); });
			}
			ImGui::EndDisabled();
			Tooltip(text::kAutoTip);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(220.0f);
			ImGui::InputTextWithHint("##setsearch", text::kDispSetSearchHint, g_page.setSearch.data(), g_page.setSearch.size());

			auto& model = ArmorSetsModel::Get();
			model.RequestRefresh();
			const auto sets = model.Snapshot();
			if (!sets || !sets->index) {
				Disabled(text::kLoading);
				ImGui::TreePop();
				return;
			}
			const auto& index = *sets->index;
			const auto  query = FromBuffer(g_page.setSearch);
			if (g_page.setsSerial != sets->serial || g_page.setsQuery != query) {
				// No search: the sets the player has. A search: every set that matches, those first.
				g_page.sets.clear();
				for (std::size_t s = 0; s < index.sets.size(); ++s) {
					const bool have = sets->coverage[s].pieces > 0;
					if ((query.empty() && have) || (!query.empty() && core::ArmorSetMatches(index.sets[s], index.pieces, query))) {
						g_page.sets.push_back(s);
					}
				}
				std::ranges::stable_sort(g_page.sets, [&](std::size_t a_lhs, std::size_t a_rhs) {
					const auto& l = sets->coverage[a_lhs];
					const auto& r = sets->coverage[a_rhs];
					if (l.Complete() != r.Complete()) {
						return l.Complete();
					}
					return l.have > r.have;
				});
				g_page.setsSerial = sets->serial;
				g_page.setsQuery = query;
			}
			std::optional<std::size_t> current;
			if (a_row.set) {
				if (const auto it = index.setOf.find(*a_row.set); it != index.setOf.end()) {
					current = it->second;
				}
			}
			if (ImGui::BeginChild("##sets", ImGui::ImVec2{ 0.0f, 150.0f }, ImGui::ImGuiChildFlags_Border)) {
				for (std::size_t i = 0; i < g_page.sets.size() && i < kMaxSets; ++i) {
					const auto  s = g_page.sets[i];
					const auto& set = index.sets[s];
					const auto& coverage = sets->coverage[s];
					const auto  label = coverage.pieces > 0 ? std::format(text::kDispSetHave, set.name, coverage.have, coverage.total) : std::format(text::kDispSetPlugin, set.name, set.plugin);
					ImGui::PushID(static_cast<int>(s));
					if (ImGui::Selectable(label.c_str(), current == s)) {
						Do([cellId, ref, id = set.id]() { ChooseArmorSet(cellId, ref, id); });
					}
					ImGui::PopID();
				}
				if (g_page.sets.empty()) {
					Disabled(text::kNone);
				}
			}
			ImGui::EndChild();
			ImGui::TreePop();
		}

		void RenderSelected(const DisplaysSnapshot& a_snap, const DisplayRow& a_row, bool a_canEdit)
		{
			ImGui::SeparatorText(a_row.name.c_str());
			std::string where = a_row.kindName.empty() ? a_row.refText : std::format("{}, {}", a_row.kindName, a_row.refText);
			if (!a_row.area.empty()) {
				where += std::format("  ({})", a_row.area);
			}
			Disabled(where);
			if (!a_canEdit) {
				return;
			}
			const auto cellId = a_snap.cellId;
			const auto ref = a_row.ref;
			if (!a_row.used) {
				if (ImGui::Button(text::kDispUse)) {
					Do([cellId, ref]() { Controller::Get().SetDisplayUsed(cellId, ref, true); });
				}
				Tooltip(text::kDispUseTip);
			} else {
				const char* why = !a_row.loaded || !a_row.here ? text::kDispFillElsewhere : !a_row.available ? text::kDispFillNotBuilt : a_row.full ? text::kDispFillFull : nullptr;
				ImGui::BeginDisabled(why != nullptr);
				if (ImGui::Button(text::kDispFill)) {
					Do([cellId, ref]() { Controller::Get().FillDisplay(cellId, ref); });
				}
				ImGui::EndDisabled();
				Tooltip(why ? why : text::kDispFillTip);
				ImGui::SameLine();
				if (ImGui::Button(text::kDispStopUsing)) {
					Do([cellId, ref]() { Controller::Get().SetDisplayUsed(cellId, ref, false); });
				}
				Tooltip(text::kDispStopTip);
				RenderCategories(a_snap, a_row);
			}
			if (a_row.mannequin) {
				RenderArmorSet(a_snap, a_row);
			}
		}

		void RenderBulk(const DisplaysSnapshot& a_snap)
		{
			if (std::ranges::none_of(a_snap.kinds, [](const DisplayKindInfo& k) { return k.unlisted > 0; })) {
				return;
			}
			ImGui::SeparatorText(text::kDispBulkHeader);
			const auto cellId = a_snap.cellId;
			for (const auto& k : a_snap.kinds) {
				if (k.unlisted == 0) {
					continue;
				}
				auto question = std::format(text::kDispConfirmUseAll, k.unlisted, k.plural);
				if (k.ignored > 0) {
					question += ' ' + std::format(text::kDispIgnoredStayOff, k.ignored);
				}
				if (ConfirmButton(std::format(text::kDispUseAll, k.unlisted, k.plural), "useall-" + k.name, question)) {
					Do([cellId, refs = k.unlistedRefs]() { Controller::Get().UseDisplays(cellId, refs); });
				}
			}
		}
	}

	void __stdcall RenderDisplaysPage()
	{
		PanelModel::Get().MarkRendered();  // notifications go to the status line below while the page is open
		auto& model = DisplaysModel::Get();
		model.RequestRefresh();
		// The player changed homes (the panel's home snapshot follows cell changes): rebuild now, once.
		static std::uint32_t requestedFor = 0;
		if (const auto home = PanelModel::Get().Home(); home && home->cellId != requestedFor) {
			requestedFor = home->cellId;
			model.RequestRefresh(true);
		}
		const auto snap = model.Snapshot();
		if (!snap) {
			Disabled(text::kLoading);
			return;
		}
		Hint(text::kDispIntro);
		ImGui::Spacing();
		if (!snap->inHome) {
			Disabled(text::kDispNotInHome);
			return;
		}
		if (snap->cellId != g_page.cellId) {
			g_page.cellId = snap->cellId;
			g_page.selected.reset();
			g_page.confirm.clear();
		}

		ImGui::AlignTextToFramePadding();
		Colored(kAccent, snap->hereArea.empty() ? snap->homeName : std::format(text::kDispHereArea, snap->homeName, snap->hereArea));
		ImGui::SameLine();
		if (ImGui::Button(text::kRefresh)) {
			model.RequestRefresh(true, true);
		}
		Tooltip(text::kDispRefreshTip);
		const bool canEdit = snap->enabled && !snap->broken;
		if (snap->broken) {
			ColoredWrapped(kYellow, text::kDispBroken);
		} else if (!snap->enabled) {
			Disabled(text::kNeedsSetup);
		}
		if (snap->rows.empty()) {
			Disabled(text::kDispNone);
			StatusLine(PanelModel::Get().Status());
			return;
		}

		RenderFilters(*snap);
		const auto& rows = Filtered(*snap);
		ImGui::SameLine();
		Disabled(std::format(text::kDispCount, rows.size(), snap->rows.size()));
		RenderTable(*snap, rows);

		const auto selected = g_page.selected ? std::ranges::find(snap->rows, *g_page.selected, &DisplayRow::ref) : snap->rows.end();
		if (selected != snap->rows.end()) {
			RenderSelected(*snap, *selected, canEdit);
		} else {
			Disabled(text::kDispSelect);
		}
		if (canEdit) {
			RenderBulk(*snap);
		}

		StatusLine(PanelModel::Get().Status());
	}
}
