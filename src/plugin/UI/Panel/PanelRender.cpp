#include "UI/Panel/PanelRender.h"

#include "Input/InputHandler.h"

#include "Controller.h"
#include "Sort/SortEngine.h"

#include <dova/core/CraftingStorage.h>
#include "UI/Labels.h"
#include "UI/Panel/ArmorSetsPage.h"
#include "UI/Panel/DisplaysPage.h"
#include "UI/Panel/EditorInfo.h"
#include "UI/Panel/PanelModel.h"
#include "UI/Panel/Strings.h"

#include <cstring>
#include <limits>

// The vendored header uses std::filesystem without including it and includes <windows.h>
// (WIN32_LEAN_AND_MEAN / NOMINMAX come from CMake). Keep it the last include of this file.
#include <filesystem>

#include <SKSEMenuFramework.h>

#include "UI/Panel/Widgets.h"  // needs ImGuiMCP from the header above

// RENDER THREAD ONLY below this line, except Register()/OpenWindow(). Never touch RE:: objects,
// HomeRegistry or Store here: read PanelModel snapshots, and post actions to the main thread.
// Layout, wording and widget conventions: UI/Panel/Strings.h.

namespace dova::ui::panel
{
	namespace
	{
		namespace ImGui = ImGuiMCP;
		using namespace widgets;

		constexpr float kFill = -std::numeric_limits<float>::min();  // "use the remaining width"
		constexpr float kNameWidth = 260.0f;                         // a name text box (container, area)
		constexpr float kFieldWidth = 320.0f;                        // a longer text box or combo

		std::atomic<SKSEMenuFramework::Model::WindowInterface*> g_window{ nullptr };

		// --- render-thread UI state (never read on the main thread) ----------------------------------

		struct Choice
		{
			std::string pattern;
			std::string label;
		};

		struct RenderState
		{
			std::uint32_t                cellId{ 0 };
			std::uint64_t                seenFocusSerial{ 0 };
			Tab                          pendingTab{ Tab::None };
			std::optional<core::FormRef> selected;
			std::string                  confirm;  // key of the inline confirmation being shown

			std::array<char, 128>        label{};
			std::optional<core::FormRef> labelFor;
			std::array<char, 64>         areaName{};   // the name for an area being added
			std::array<char, 64>         areaLabel{};  // the Areas list's rename box
			std::optional<core::FormRef> areaLabelFor;  // the area it is open for

			bool                  keepLoaded{ false };
			bool                  keepDirty{ false };
			bool                  keepHasBlock{ false };
			std::string           keepLoadedJson;
			core::KeepConfig      keepDraft;
			std::array<char, 128> addAlways{};
			std::array<char, 128> addNever{};

			bool                 exportInit{ false };
			std::uint32_t        exportCell{ 0 };  // the home the fields were filled for
			std::array<char, 96> exportName{};
			std::array<char, 96> exportAuthor{};
			std::array<char, 32> exportVersion{};
			bool                 exportLearned{ true };  // learned items as fixed item bindings

			const CategoryTree* choicesFor{ nullptr };
			std::vector<Choice> choices;
		};

		RenderState g_state;

		// --- small helpers ---------------------------------------------------------------------------

		template <std::size_t N>
		void CopyTo(std::array<char, N>& a_buffer, std::string_view a_text)
		{
			const auto n = std::min(a_text.size(), N - 1);
			std::memcpy(a_buffer.data(), a_text.data(), n);
			a_buffer[n] = '\0';
		}

		template <std::size_t N>
		std::string FromBuffer(const std::array<char, N>& a_buffer)
		{
			return std::string(a_buffer.data(), strnlen(a_buffer.data(), N));
		}

		std::string Join(const std::vector<std::string>& a_items, std::string_view a_sep = ", ")
		{
			std::string out;
			for (const auto& item : a_items) {
				if (!out.empty()) {
					out += a_sep;
				}
				out += item;
			}
			return out;
		}

		// "Breezehome v1.0.0 by Dova Stash"; without an author, "Breezehome v1.0.0".
		std::string NameLine(const std::string& a_name, const std::string& a_version, const std::string& a_author)
		{
			const auto line = std::format(text::kNameVersion, a_name, a_version);
			return a_author.empty() ? line : std::format(text::kByAuthor, line, a_author);
		}

		// Runs `a_fn` on the main thread (SKSE task queue), then rebuilds the snapshot.
		void Do(std::function<void()> a_fn, Refresh a_refresh = Refresh::Light)
		{
			PanelModel::Get().Post(std::move(a_fn), a_refresh);
		}

		// The home panel's inline confirmations (one question at a time).
		bool ConfirmButton(const std::string& a_label, const std::string& a_key, const std::string& a_question, bool a_small = false)
		{
			return widgets::ConfirmButton(g_state.confirm, a_label, a_key, a_question, a_small);
		}

		bool RowConfirm(ButtonRow& a_row, const std::string& a_label, const std::string& a_key, const std::string& a_question, bool a_small = false)
		{
			return a_row.Confirm(g_state.confirm, a_label, a_key, a_question, a_small);
		}

		// Save / Revert / "Unsaved changes" under a list edited as a draft (keep rules).
		enum class DraftAction
		{
			None,
			Save,
			Revert
		};

		DraftAction DraftButtons(bool a_dirty, bool a_invalid)
		{
			ImGui::Separator();
			if (a_invalid) {
				ColoredWrapped(kYellow, text::kQuotaNeedsCategory);
			}
			auto action = DraftAction::None;
			ImGui::BeginDisabled(!a_dirty || a_invalid);
			if (ImGui::Button(text::kSave)) {
				action = DraftAction::Save;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(!a_dirty);
			if (ImGui::Button(text::kRevert)) {
				action = DraftAction::Revert;
			}
			ImGui::EndDisabled();
			if (a_dirty) {
				ImGui::SameLine();
				Colored(kYellow, text::kUnsaved);
			}
			return action;
		}

		// --- categories --------------------------------------------------------------------------------

		bool HasPattern(const ContainerRow& a_row, std::string_view a_pattern)
		{
			return std::ranges::any_of(a_row.patterns, [&](const std::string& p) { return core::IEquals(p, a_pattern); });
		}

		bool Covers(const ContainerRow& a_row, std::string_view a_id)
		{
			return std::ranges::any_of(a_row.patterns, [&](const std::string& p) { return core::CategoryPatternMatches(p, a_id); });
		}

		bool AnyBelow(const ContainerRow& a_row, const CategoryTree& a_tree, std::size_t a_index)
		{
			return std::ranges::any_of(a_tree.nodes[a_index].children, [&](std::size_t c) {
				return Covers(a_row, a_tree.nodes[c].id) || AnyBelow(a_row, a_tree, c);
			});
		}

		const CategoryNode* FindNode(const CategoryTree& a_tree, std::string_view a_id)
		{
			const auto it = std::ranges::find_if(a_tree.nodes, [&](const CategoryNode& n) { return core::IEquals(n.id, a_id); });
			return it != a_tree.nodes.end() ? &*it : nullptr;
		}

		std::string PatternName(const CategoryTree& a_tree, std::string_view a_pattern)
		{
			if (a_pattern == "*") {
				return "Everything";
			}
			if (a_pattern.ends_with(".*")) {
				const auto* node = FindNode(a_tree, a_pattern.substr(0, a_pattern.size() - 2));
				return node ? "All " + node->name : std::string(a_pattern);
			}
			const auto* node = FindNode(a_tree, a_pattern);
			return node ? node->name : std::string(a_pattern);
		}

		// "*", then for every node in tree order: "<id>.*" for groups, "<id>" for leaves.
		const std::vector<Choice>& PatternChoices(const CategoryTree& a_tree)
		{
			if (g_state.choicesFor == &a_tree) {
				return g_state.choices;
			}
			std::vector<Choice> out;
			out.push_back({ "*", text::kEverything });
			std::function<void(std::size_t, std::size_t)> walk = [&](std::size_t a_index, std::size_t a_depth) {
				const auto& node = a_tree.nodes[a_index];
				const std::string indent(a_depth * 2, ' ');
				if (node.children.empty()) {
					out.push_back({ node.id, std::format("{}{} ({})", indent, node.name, node.id) });
					return;
				}
				out.push_back({ node.id + ".*", indent + std::format(text::kAllOf, node.name, node.id) });
				for (const auto c : node.children) {
					walk(c, a_depth + 1);
				}
			};
			for (const auto r : a_tree.roots) {
				walk(r, 0);
			}
			g_state.choices = std::move(out);
			g_state.choicesFor = &a_tree;
			return g_state.choices;
		}

		// Combo over every category pattern. Returns true (and sets a_value) when one was picked.
		bool PatternCombo(const char* a_id, std::string& a_value, const CategoryTree& a_tree, const char* a_preview = nullptr)
		{
			bool        changed = false;
			const auto  preview = a_preview ? std::string(a_preview) : a_value.empty() ? std::string(text::kNoCategory) : a_value;
			if (ImGui::BeginCombo(a_id, preview.c_str())) {
				for (const auto& choice : PatternChoices(a_tree)) {
					const bool selected = !a_preview && core::IEquals(choice.pattern, a_value);
					if (ImGui::Selectable(choice.label.c_str(), selected)) {
						a_value = choice.pattern;
						changed = true;
					}
					if (selected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			return changed;
		}

		// --- sync with the latest snapshot ------------------------------------------------------------

		const ContainerRow* FindRow(const HomeSnapshot& a_snap, const std::optional<core::FormRef>& a_ref)
		{
			if (!a_ref) {
				return nullptr;
			}
			const auto it = std::ranges::find_if(a_snap.containers, [&](const ContainerRow& r) { return r.ref == *a_ref; });
			return it != a_snap.containers.end() ? &*it : nullptr;
		}

		void Select(const ContainerRow& a_row)
		{
			g_state.selected = a_row.ref;
			g_state.labelFor = a_row.ref;
			CopyTo(g_state.label, a_row.label);
			g_state.confirm.clear();
		}

		void Sync(const HomeSnapshot& a_snap)
		{
			if (a_snap.cellId != g_state.cellId) {
				const auto seen = g_state.seenFocusSerial;
				g_state = RenderState{};
				g_state.seenFocusSerial = seen;
				g_state.cellId = a_snap.cellId;
			}
			if (a_snap.focusSerial != g_state.seenFocusSerial) {
				g_state.seenFocusSerial = a_snap.focusSerial;
				g_state.pendingTab = a_snap.focusTab;
				g_state.confirm.clear();
				if (a_snap.focusRow && *a_snap.focusRow < a_snap.containers.size()) {
					Select(a_snap.containers[*a_snap.focusRow]);
				}
			}
		}

		void RenderStatus()
		{
			StatusLine(PanelModel::Get().Status());
		}

		// --- header / setup -----------------------------------------------------------------------------

		void RenderHeader(const HomeSnapshot& a_snap)
		{
			Colored(kAccent, std::format(text::kHeaderHome, a_snap.homeName));
			if (!a_snap.hereArea.empty()) {
				ImGui::SameLine();
				Text(std::format(text::kHereArea, a_snap.hereArea));
			}
			ImGui::SameLine();
			Disabled(a_snap.cellText);
			if (a_snap.enabled) {
				Text(std::format(text::kHeaderLayout, NameLine(a_snap.layoutName, a_snap.layoutVersion, a_snap.layoutAuthor)));
				Disabled(a_snap.basedOn ? std::format(text::kHeaderBasedOn, *a_snap.basedOn) : std::string(text::kHeaderCustom));
				if (a_snap.masterRow && *a_snap.masterRow < a_snap.containers.size()) {
					const auto& master = a_snap.containers[*a_snap.masterRow];
					Text(std::format(text::kHeaderMaster, master.label));
					if (!master.available) {
						ImGui::SameLine();
						Colored(kRed, std::format(text::kUnavailable, master.reason));
					}
				} else {
					Colored(kYellow, text::kHeaderNoMaster);
				}
			}
			ImGui::Separator();
		}

		// The templates that fit this home, in the order the game offers them (core::ResolveLayout: a template someone
		// made before an auto-draft), one row each with a small Use button; Use switches the setup to it
		// (Controller::UseTemplate: backed up first, nothing moved). It asks first only when the setup has changes of
		// the player's own that the switch replaces (HomeSnapshot::ownChanges); otherwise one click switches.
		void RenderTemplateChoices(const HomeSnapshot& a_snap)
		{
			const auto cell = a_snap.cellId;
			const bool confirm = a_snap.enabled && a_snap.ownChanges;
			for (const auto& t : a_snap.templates) {
				ImGui::PushID(t.source.c_str());
				ImGui::AlignTextToFramePadding();
				Text(NameLine(t.name, t.version, t.author));
				Tooltip(t.source);
				if (t.best) {
					ImGui::SameLine();
					Colored(kGreen, text::kTemplateBest);
				}
				if (t.autoDraft) {
					ImGui::SameLine();
					Disabled(text::kTemplateDraft);
					Tooltip(text::kTemplateDraftTip);
				}
				if (t.applied) {
					ImGui::SameLine();
					Disabled(text::kTemplateApplied);
				}
				ButtonRow row(true);
				bool      go = false;
				if (confirm) {
					go = RowConfirm(row, t.applied ? text::kReapplyTemplateAsk : text::kUseTemplateAsk, "tpl:" + t.source, std::format(text::kConfirmTemplate, t.name), true);
				} else {
					row.Next();
					go = Button(t.applied ? text::kReapplyTemplate : text::kUseTemplate, true);
				}
				if (go) {
					Do([cell, source = t.source]() { Controller::Get().UseTemplate(cell, source); }, Refresh::Full);
				}
				ImGui::PopID();
			}
		}

		// Saved setups outside Data (DESIGN §6.6): the latest copy, then the older ones in a tree. `a_confirm`:
		// this home has a setup (or a broken file) that a restore replaces, so each asks first.
		void RenderBackups(const HomeSnapshot& a_snap, bool a_confirm)
		{
			ImGui::SeparatorText(text::kBackupsHeader);
			Tooltip(text::kBackupsTip);
			if (a_snap.backups.empty()) {
				Disabled(text::kNoBackups);
				return;
			}
			const auto cell = a_snap.cellId;
			const auto offer = [&](std::size_t a_index, const std::string& a_label) {
				const auto& backup = a_snap.backups[a_index];
				const bool  go = a_confirm ? ConfirmButton(a_label, std::format("restore:{}", a_index), std::format(ui::kConfirmRestore, backup.date)) :
				                             Button(a_label);
				if (go) {
					Do([cell, path = backup.path]() { Controller::Get().RestoreBackup(cell, path); }, Refresh::Full);
				}
			};
			std::size_t older = 0;
			for (std::size_t i = 0; i < a_snap.backups.size(); ++i) {
				if (a_snap.backups[i].history) {
					++older;
				} else if (a_snap.backups[i].same) {
					// Restoring it would change nothing: say where things stand instead of offering it.
					Disabled(std::format(text::kBackupSame, a_snap.backups[i].date));
				} else {
					offer(i, a_confirm ? std::format(text::kRestoreLast, a_snap.backups[i].date) : std::format(ui::kRestoreLastSetup, a_snap.backups[i].date));
				}
			}
			if (older > 0 && ImGui::TreeNodeEx(std::format(text::kOlderBackups, older).c_str(), 0)) {
				for (std::size_t i = 0; i < a_snap.backups.size(); ++i) {
					if (a_snap.backups[i].history) {
						offer(i, std::format(text::kRestoreOlder, a_snap.backups[i].date));
					}
				}
				ImGui::TreePop();
			}
		}

		// The player's cell can join the home they just came from (DESIGN §6.7).
		void RenderAddArea(const HomeSnapshot& a_snap)
		{
			if (!a_snap.addAreaTo) {
				return;
			}
			ImGui::SeparatorText(text::kAreasHeader);
			Tooltip(text::kAreasTip);
			// With a name of the player's choosing ("Grounds"); empty keeps the cell's own name.
			ImGui::SetNextItemWidth(kNameWidth);
			const bool entered = ImGui::InputTextWithHint("##areaname", std::format(text::kAreaNameHint, a_snap.addAreaName).c_str(), g_state.areaName.data(), g_state.areaName.size(),
				ImGui::ImGuiInputTextFlags_EnterReturnsTrue);
			Tooltip(text::kAreaRenameTip);
			ImGui::SameLine();
			if (Button(std::format(ui::kAddArea, *a_snap.addAreaTo)) || entered) {
				Do([cell = a_snap.addAreaCell, label = FromBuffer(g_state.areaName)]() { Controller::Get().AddArea(cell, label); }, Refresh::Full);
				g_state.areaName.fill('\0');
			}
			Tooltip(text::kAddAreaTip);
		}

		// A home without a setup: make a master, use a template, restore a saved setup or join another home (the
		// hotkey's setup menu offers the same).
		void RenderSetup(const HomeSnapshot& a_snap)
		{
			const auto cell = a_snap.cellId;
			if (a_snap.layoutBroken) {
				ColoredWrapped(kRed, a_snap.backups.empty() ? text::kSetupBrokenNoBackup : text::kSetupBroken);
				if (!a_snap.backups.empty()) {
					RenderBackups(a_snap, true);
				}
				ImGui::SeparatorText(text::kResetHeader);
				if (ConfirmButton(text::kResetHome, "reset", std::format(text::kConfirmReset, a_snap.homeName))) {
					Do([cell]() { Controller::Get().ResetHome(cell); }, Refresh::Full);
				}
				return;
			}
			Wrapped(text::kSetupIntro);
			const ContainerRow* focus = a_snap.focusRow && *a_snap.focusRow < a_snap.containers.size() ? &a_snap.containers[*a_snap.focusRow] : nullptr;
			if (focus && !focus->master) {
				if (focus->available) {
					if (Button(std::format(text::kSetupMakeMaster, focus->label))) {
						Do([cell, ref = focus->ref]() { Controller::Get().MakeMaster(cell, ref); }, Refresh::Full);
					}
				} else {
					ColoredWrapped(kRed, std::format("{} ({})", focus->label, std::format(text::kUnavailable, focus->reason)));
				}
			} else {
				const auto global = PanelModel::Get().Global();
				Hint(std::format(text::kSetupLookAt, global ? global->hotkeyName : std::string(text::kSetupHotkeyFallback)));
			}
			if (!a_snap.templates.empty()) {
				ImGui::SeparatorText(text::kSetupTemplates);
				Tooltip(text::kTemplatesTip);
				RenderTemplateChoices(a_snap);
			}
			if (!a_snap.backups.empty()) {
				RenderBackups(a_snap, false);
			}
			RenderAddArea(a_snap);
		}

		// --- Actions tab --------------------------------------------------------------------------------

		// One row of the Retrieve tree: "<name> (<count>)" and a small Retrieve button.
		void RetrieveRow(const HomeSnapshot& a_snap, const std::string& a_label, const std::string& a_pattern, bool a_branch, bool& a_open)
		{
			if (a_branch) {
				a_open = ImGui::TreeNodeEx(a_label.c_str(), 0);
			} else {
				ImGui::TreeNodeEx(a_label.c_str(), ImGui::ImGuiTreeNodeFlags_Leaf | ImGui::ImGuiTreeNodeFlags_NoTreePushOnOpen);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton(text::kRetrieve)) {
				Do([cell = a_snap.cellId, pattern = a_pattern]() { Controller::Get().RunOperation(cell, sort::Op::Retrieve, pattern); }, Refresh::Full);
			}
			Tooltip(text::kRetrieveTip);
		}

		void RenderRetrieveNode(const HomeSnapshot& a_snap, std::size_t a_index)
		{
			const auto& tree = *a_snap.categories;
			const auto  count = [&](std::size_t i) { return i < a_snap.nodeCounts.size() ? a_snap.nodeCounts[i] : 0; };
			const auto  n = count(a_index);
			if (n <= 0) {
				return;
			}
			const auto& node = tree.nodes[a_index];
			const bool  branch = std::ranges::any_of(node.children, [&](std::size_t c) { return count(c) > 0; });

			ImGui::PushID(node.id.c_str());
			bool open = false;
			RetrieveRow(a_snap, std::format(text::kNodeCount, node.name, n), node.id + ".*", branch, open);
			if (open) {
				for (const auto c : node.children) {
					RenderRetrieveNode(a_snap, c);
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		// Multi-cell homes (DESIGN §6.7): the cells this home spans, each extra one renamable and removable.
		void RenderAreas(const HomeSnapshot& a_snap)
		{
			if (a_snap.areas.size() <= 1 && a_snap.areasExcluded.empty()) {
				return;
			}
			ImGui::SeparatorText(text::kAreasHeader);
			Tooltip(text::kAreasTip);
			const auto cell = a_snap.cellId;
			for (const auto& area : a_snap.areas) {
				ImGui::PushID(area.cellText.c_str());
				ImGui::AlignTextToFramePadding();
				Text(area.name);
				Tooltip(area.cellText);
				if (area.main) {
					ImGui::SameLine();
					Disabled(text::kAreaMain);
				}
				if (area.here) {
					ImGui::SameLine();
					Colored(kGreen, text::kAreaHere);
				}
				if (!area.main && a_snap.enabled && !a_snap.layoutBroken) {
					// Name it ("Grounds"): a box that opens under the row, like the hotkey's Rename.
					const bool renaming = g_state.areaLabelFor && *g_state.areaLabelFor == area.cell;
					ButtonRow  row(true);
					row.Next();
					if (ImGui::SmallButton(text::kRename)) {
						if (renaming) {
							g_state.areaLabelFor.reset();
						} else {
							g_state.areaLabelFor = area.cell;
							CopyTo(g_state.areaLabel, area.name);
						}
					}
					Tooltip(text::kAreaRenameTip);
					if (RowConfirm(row, text::kRemoveAsk, "area:" + area.cellText, std::format(ui::kConfirmRemoveArea, area.name, a_snap.homeName), true)) {
						Do([cell, ref = area.cell]() { Controller::Get().RemoveArea(cell, ref); }, Refresh::Full);
					}
					if (renaming) {
						ImGui::Indent();
						ImGui::SetNextItemWidth(kNameWidth);
						const bool entered = ImGui::InputText("##arealabel", g_state.areaLabel.data(), g_state.areaLabel.size(), ImGui::ImGuiInputTextFlags_EnterReturnsTrue);
						Tooltip(text::kAreaRenameTip);
						ImGui::SameLine();
						if (ImGui::Button(text::kSave) || entered) {
							Do([cell, ref = area.cell, label = FromBuffer(g_state.areaLabel)]() { Controller::Get().RenameArea(cell, ref, label); }, Refresh::Full);
							g_state.areaLabelFor.reset();
						}
						ImGui::SameLine();
						if (ImGui::Button(text::kCancel)) {
							g_state.areaLabelFor.reset();
						}
						ImGui::Unindent();
					}
				}
				ImGui::PopID();
			}
			for (const auto& excluded : a_snap.areasExcluded) {
				Hint(std::format(text::kAreaExcluded, excluded));
			}
		}

		std::string Items(std::int64_t a_n)
		{
			return a_n == 1 ? std::format(text::kItemCount, a_n) : std::format(text::kItemsCount, a_n);
		}

		// The sell chest (DESIGN §8.8): Sell now with the total, the buy-back (both ask first), and the session's sales.
		void RenderSell(const HomeSnapshot& a_snap)
		{
			const auto  cell = a_snap.cellId;
			const auto& sell = a_snap.sell;
			if (sell.chest) {
				ImGui::SeparatorText(text::kSellHeader);
				Tooltip(text::kSellTip);
				if (!sell.reachable) {
					Disabled(text::kSellNotHere);
				} else if (sell.items <= 0) {
					Disabled(sell.stays.empty() ? std::string(text::kSellNothing) : std::format("{} ({})", text::kSellNothing, sell.stays));
				} else {
					Wrapped(std::format(text::kSellQuote, Items(sell.items), sell.gold, sell.what));
					if (!sell.stays.empty()) {
						Hint(sell.stays);
					}
					if (ConfirmButton(std::format(ui::kSellNow, sell.gold), "sell", std::format(ui::kConfirmSell, Items(sell.items), sell.gold, sell.what))) {
						Do([cell, items = sell.items, gold = sell.gold]() { Controller::Get().SellConfirmed(cell, std::nullopt, items, gold); }, Refresh::Full);
					}
				}
				if (sell.buyBack) {
					const auto [items, gold] = *sell.buyBack;
					Hint(std::format(text::kBuyBackInfo, Items(items), gold, sell.buyBackWhat));
					if (ConfirmButton(std::format(ui::kBuyBack, gold), "buyback", std::format(ui::kConfirmBuyBack, Items(items), gold, sell.buyBackWhat))) {
						Do([cell]() { Controller::Get().BuyBackConfirmed(cell); }, Refresh::Full);
					}
				}
			}
			if (!sell.log.empty()) {
				ImGui::SeparatorText(text::kSalesHeader);
				for (std::size_t i = 0; i < sell.log.size(); ++i) {
					const auto& sale = sell.log[i];
					const auto  tags = std::string(sale.timed ? text::kSaleTimed : "") + (sale.boughtBack ? text::kSaleBoughtBack : "");
					ImGui::PushID(static_cast<int>(i));
					if (ImGui::TreeNodeEx(std::format(text::kSaleLine, sale.time, sale.chests, Items(sale.items), sale.gold, tags).c_str(), 0)) {
						for (const auto& [name, count, gold] : sale.lines) {
							ImGui::BulletText("%s", std::format(text::kSaleItem, name, count, gold).c_str());
						}
						ImGui::TreePop();
					}
					ImGui::PopID();
				}
			}
		}

		// Everyday actions first (Sort, Sell chest), the templates in sight above the long Retrieve tree, then the
		// home's setup (Areas, Saved setups), then Reset.
		void RenderActions(const HomeSnapshot& a_snap)
		{
			const auto cell = a_snap.cellId;

			ImGui::SeparatorText(text::kSortHeader);
			if (ImGui::Button(text::kDeposit)) {
				Do([cell]() { Controller::Get().RunOperation(cell, sort::Op::Deposit); }, Refresh::Full);
			}
			Tooltip(text::kDepositTip);
			ImGui::SameLine();
			const bool restocks = core::RestockApplies(a_snap.globalKeep, a_snap.keep ? &*a_snap.keep : nullptr);
			ImGui::BeginDisabled(!restocks);
			if (ImGui::Button(text::kRestock)) {
				Do([cell]() { Controller::Get().RunOperation(cell, sort::Op::Restock); }, Refresh::Full);
			}
			ImGui::EndDisabled();
			Tooltip(restocks ? text::kRestockTip : text::kRestockNoneTip);
			ImGui::SameLine();
			if (ImGui::Button(text::kSortAll)) {
				Do([cell]() { Controller::Get().RunOperation(cell, sort::Op::SortAll); }, Refresh::Full);
			}
			Tooltip(text::kSortAllTip);

			RenderSell(a_snap);

			ImGui::SeparatorText(text::kSetupTemplates);
			Tooltip(text::kTemplatesTip);
			if (a_snap.templates.empty()) {
				Disabled(text::kNoTemplate);
			} else {
				RenderTemplateChoices(a_snap);
			}

			ImGui::SeparatorText(text::kRetrieveHeader);
			if (ImGui::Button(text::kRefreshCounts)) {
				PanelModel::Get().RequestRefresh(Refresh::Full);
			}
			Tooltip(text::kRefreshCountsTip);
			if (!a_snap.countsValid) {
				Disabled(text::kCounting);
			} else if (a_snap.totalCount <= 0) {
				Disabled(text::kNothingStored);
			} else {
				ImGui::PushID("##everything");
				bool open = false;
				RetrieveRow(a_snap, std::format(text::kRetrieveEverything, a_snap.totalCount), "*", false, open);
				ImGui::PopID();
				for (const auto root : a_snap.categories->roots) {
					RenderRetrieveNode(a_snap, root);
				}
			}

			RenderAreas(a_snap);

			RenderBackups(a_snap, true);

			ImGui::SeparatorText(text::kResetHeader);
			if (ConfirmButton(text::kResetHome, "reset", std::format(text::kConfirmReset, a_snap.homeName))) {
				Do([cell]() { Controller::Get().ResetHome(cell); }, Refresh::Full);
			}
		}

		// --- Containers tab -----------------------------------------------------------------------------

		void RenderCategoryNode(const ContainerRow& a_row, const CategoryTree& a_tree, std::size_t a_index, const std::function<void(std::string)>& a_toggle)
		{
			const auto& node = a_tree.nodes[a_index];
			ImGui::PushID(node.id.c_str());
			if (node.children.empty()) {
				const bool exact = HasPattern(a_row, node.id);
				bool       value = exact;
				if (ImGui::Checkbox(std::format("{}{}###leaf", node.name, ui::LearnedSuffix(a_row.learnedByCategory, node.id + ".*")).c_str(), &value)) {
					a_toggle(node.id);
				}
				if (!exact && Covers(a_row, node.id)) {
					ImGui::SameLine();
					Disabled(text::kViaWider);
				}
			} else {
				const auto all = node.id + ".*";
				const bool exactAll = HasPattern(a_row, all);
				const auto mark = exactAll ? "[x] "sv : (Covers(a_row, node.id) || AnyBelow(a_row, a_tree, a_index)) ? "[~] "sv : "[ ] "sv;
				const bool open = ImGui::TreeNodeEx(std::format("{}{}{}###tree", mark, node.name, ui::LearnedSuffix(a_row.learnedByCategory, all)).c_str(), 0);
				if (!exactAll && Covers(a_row, node.id)) {
					ImGui::SameLine();
					Disabled(text::kViaWider);
				}
				if (open) {
					bool value = exactAll;
					if (ImGui::Checkbox(std::format(text::kAllOf, node.name, node.id).c_str(), &value)) {
						a_toggle(all);
					}
					for (const auto c : node.children) {
						RenderCategoryNode(a_row, a_tree, c, a_toggle);
					}
					ImGui::TreePop();
				}
			}
			ImGui::PopID();
		}

		// Patterns the tree cannot show as a checkbox ("weapons" exact, "weapons.swords.*", typos...).
		std::vector<std::string> OtherPatterns(const ContainerRow& a_row, const CategoryTree& a_tree)
		{
			std::vector<std::string> out;
			for (const auto& p : a_row.patterns) {
				const bool shown = p == "*" || std::ranges::any_of(a_tree.nodes, [&](const CategoryNode& n) {
					return n.children.empty() ? core::IEquals(p, n.id) : core::IEquals(p, n.id + ".*");
				});
				if (!shown) {
					out.push_back(p);
				}
			}
			return out;
		}

		void OpenContainerButton(const HomeSnapshot& a_snap, const ContainerRow& a_row)
		{
			if (ImGui::Button(text::kOpenContainer)) {
				// The window holds all input while it is open: close it, then open the real container menu.
				if (auto* window = g_window.load()) {
					window->IsOpen = false;
				}
				Do([cell = a_snap.cellId, ref = a_row.ref]() { Controller::Get().Browse(cell, ref); });
			}
			Tooltip(text::kOpenContainerTip);
		}

		void RenderContainerDetails(const HomeSnapshot& a_snap, const ContainerRow& a_row)
		{
			const auto cell = a_snap.cellId;
			const auto ref = a_row.ref;

			if (!g_state.labelFor || *g_state.labelFor != a_row.ref) {
				g_state.labelFor = a_row.ref;
				CopyTo(g_state.label, a_row.label);
			}

			// What it is.
			ImGui::SeparatorText(a_row.label.c_str());
			Disabled(a_row.refText);
			if (!a_row.gameName.empty() && a_row.gameName != a_row.label) {
				ImGui::SameLine();
				Disabled("(" + a_row.gameName + ")");
			}
			if (!a_row.note.empty()) {
				Wrapped(a_row.note);
			}
			if (!a_row.area.empty()) {
				Disabled(std::format(text::kInArea, a_row.area));
				if (a_row.remote) {
					Tooltip(text::kRemoteTip);
				}
			}
			if (!a_row.available) {
				ColoredWrapped(kRed, std::format(text::kUnavailableLine, a_row.reason));
			}
			if (!a_row.warning.empty()) {
				ColoredWrapped(kYellow, a_row.warning);
			}
			if (a_row.master) {
				Hint(text::kMasterInfo);
				if (a_row.available) {
					OpenContainerButton(a_snap, a_row);
				}
				return;
			}

			// Its name: empty gives back the game's name.
			ImGui::SetNextItemWidth(kNameWidth);
			const bool entered = ImGui::InputTextWithHint("##label", a_row.gameName.c_str(), g_state.label.data(), g_state.label.size(), ImGui::ImGuiInputTextFlags_EnterReturnsTrue);
			Tooltip(text::kNameTip);
			ImGui::SameLine();
			if (ImGui::Button(text::kRename) || entered) {
				Do([cell, ref, label = FromBuffer(g_state.label)]() { Controller::Get().SetLabel(cell, ref, label); });
			}

			if (a_row.sell) {
				Hint(text::kSellChestInfo);
			}

			// Everyday actions, then the ones that clear or remove something (those that ask last).
			{
				ButtonRow row;
				if (a_row.available) {
					row.Next();
					OpenContainerButton(a_snap, a_row);
				}
				row.Next();
				if (ImGui::Button(a_row.ignored ? text::kStopIgnoring : text::kIgnore)) {
					Do([cell, ref, ignored = !a_row.ignored]() { Controller::Get().SetIgnored(cell, ref, ignored); }, Refresh::Full);
				}
				if (!a_row.ignored) {
					Tooltip(text::kIgnoreTip);
				}
				if (a_row.sell) {
					row.Next();
					if (Button(ui::kNotSellChest)) {
						Do([cell, ref]() { Controller::Get().SetSellChest(cell, ref, false); }, Refresh::Full);
					}
					Tooltip(text::kNotSellChestTip);
				} else if (!a_row.ignored && a_row.available &&
						   RowConfirm(row, ui::kMakeSellChest, "sellchest:" + a_row.refText, ui::SellChestQuestion(a_row.label))) {
					Do([cell, ref]() { Controller::Get().SetSellChest(cell, ref, true); }, Refresh::Full);
				}
				if (a_row.available && RowConfirm(row, text::kMakeMaster, "master:" + a_row.refText, std::format(text::kConfirmMakeMaster, a_row.label))) {
					Do([cell, ref]() { Controller::Get().MakeMaster(cell, ref); }, Refresh::Full);
				}
			}
			if (!a_row.patterns.empty() || a_row.learnedCount > 0 || a_row.managed) {
				ButtonRow row;
				if (!a_row.patterns.empty()) {
					row.Next();
					if (ImGui::Button(text::kClearCategories)) {
						Do([cell, ref]() { Controller::Get().ClearCategories(cell, ref); });
					}
					Tooltip(text::kClearCategoriesTip);
				}
				if (a_row.learnedCount > 0 &&
					RowConfirm(row, std::format(text::kForgetLearned, a_row.learnedCount), "forget:" + a_row.refText, std::format(text::kConfirmForget, a_row.learnedCount, a_row.label))) {
					Do([cell, ref]() { Controller::Get().ForgetLearned(cell, ref); });
				}
				// Something to remove (never the master: returned above).
				if ((a_row.managed || a_row.learnedCount > 0) &&
					RowConfirm(row, text::kRemoveAsk, "remove:" + a_row.refText, std::format(text::kConfirmRemove, a_row.label, ui::kRemoveExplained))) {
					Do([cell, ref]() { Controller::Get().RemoveContainer(cell, ref); }, Refresh::Full);
				}
			}
			if (a_row.explicitForms > 0) {
				Disabled(std::format(text::kExplicitForms, a_row.explicitForms));
			}

			// What goes in it.
			ImGui::SeparatorText(text::kCategoriesHeader);
			if (a_row.ignored) {
				Hint(text::kIgnoredInfo);
			} else if (!a_row.learnedText.empty()) {
				Hint(std::format(text::kLearnedInfo, a_row.learnedText));
			}
			if (!a_row.suggestionCategory.empty()) {
				// The same question the HUD asks once the container closes (DESIGN §8.6).
				Wrapped(std::format(text::kSuggestion, a_row.suggestionName, a_row.label, a_row.suggestionCount));
				if (ImGui::Button(text::kYes)) {
					Do([cell, ref, category = a_row.suggestionCategory]() { Controller::Get().AcceptSuggestion(cell, ref, category); });
				}
				ImGui::SameLine();
				if (ImGui::Button(text::kSuggestionNo)) {
					Do([cell, ref, category = a_row.suggestionCategory]() { Controller::Get().DeclineSuggestion(cell, ref, category); });
				}
			}
			if (!a_row.declined.empty()) {
				std::string names;
				for (const auto& [id, name] : a_row.declined) {
					names += (names.empty() ? "" : ", ") + name;
				}
				ImGui::AlignTextToFramePadding();
				Disabled(std::format(text::kDeclinedInfo, names));
				ImGui::SameLine();
				if (ImGui::SmallButton(text::kAskAgain)) {
					Do([cell, ref]() { Controller::Get().ClearDeclined(cell, ref, {}); });
				}
			}
			const std::function<void(std::string)> toggle = [cell, ref](std::string a_pattern) {
				Do([cell, ref, pattern = std::move(a_pattern)]() { Controller::Get().ToggleCategory(cell, ref, pattern); });
			};
			ImGui::BeginDisabled(!a_row.available);
			bool everything = HasPattern(a_row, "*");
			if (ImGui::Checkbox(text::kEverything, &everything)) {
				toggle("*");
			}
			const auto& tree = *a_snap.categories;
			for (const auto root : tree.roots) {
				RenderCategoryNode(a_row, tree, root, toggle);
			}
			if (const auto others = OtherPatterns(a_row, tree); !others.empty()) {
				Disabled(text::kOtherPatterns);
				for (const auto& p : others) {
					ImGui::PushID(p.c_str());
					ImGui::Bullet();
					Text(p);
					ImGui::SameLine();
					if (ImGui::SmallButton(text::kRemove)) {
						toggle(p);
					}
					ImGui::PopID();
				}
			}
			ImGui::EndDisabled();
		}

		void RenderContainers(const HomeSnapshot& a_snap)
		{
			const auto& rows = a_snap.containers;
			if (ImGui::BeginTable("##containers", 4, kScrollTableFlags, ImGui::ImVec2{ 0.0f, TableHeight(rows.size(), 8) })) {
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn(text::kColContainer, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.32f);
				ImGui::TableSetupColumn(text::kColCategories, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.38f);
				ImGui::TableSetupColumn(text::kColLearned, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.08f);
				ImGui::TableSetupColumn(text::kColStatus, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.22f);
				ImGui::TableHeadersRow();
				for (const auto& row : rows) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					const bool selected = g_state.selected && *g_state.selected == row.ref;
					const auto label = std::format("{}{}##{}", row.master ? text::kMasterTag : row.sell ? text::kSellTag : "", row.label, row.refText);
					if (ImGui::Selectable(label.c_str(), selected, ImGui::ImGuiSelectableFlags_SpanAllColumns)) {
						Select(row);
					}
					Tooltip(row.refText);

					ImGui::TableNextColumn();
					if (row.master) {
						Disabled(text::kHomeless);
					} else if (row.ignored) {
						Disabled(row.patternsText.empty() ? std::string(text::kNone) : row.patternsText);
					} else if (row.patternsText.empty() && !row.learnedText.empty()) {
						Disabled(std::format(text::kLearnedOnly, row.learnedText));
					} else if (row.patternsText.empty()) {
						Disabled(text::kNone);
					} else {
						Text(row.patternsText);
					}

					ImGui::TableNextColumn();
					if (row.master) {
						Disabled(text::kNone);
					} else {
						Text(std::to_string(row.learnedCount));
					}

					ImGui::TableNextColumn();
					if (!row.available) {
						Colored(kRed, std::format(text::kUnavailable, row.reason));
					} else if (row.ignored) {
						Disabled(text::kStatusIgnored);
					} else if (!row.managed) {
						Disabled(text::kNotManaged);
					} else {
						Colored(kGreen, text::kManaged);
					}
					if (row.remote && !row.area.empty()) {
						ImGui::SameLine();
						Disabled("(" + row.area + ")");
						Tooltip(text::kRemoteTip);
					}
					if (!row.warning.empty()) {
						ImGui::SameLine();
						Colored(kYellow, "(!)");
						Tooltip(row.warning);
					}
				}
				ImGui::EndTable();
			}

			if (const auto* row = FindRow(a_snap, g_state.selected)) {
				RenderContainerDetails(a_snap, *row);
			} else {
				Disabled(text::kSelectContainer);
			}
		}

		// --- Learned tab --------------------------------------------------------------------------------

		void RenderLearned(const HomeSnapshot& a_snap)
		{
			if (a_snap.learned.empty()) {
				Hint(text::kLearnedEmpty);
				return;
			}
			Hint(text::kLearnedIntro);
			if (a_snap.learnedUnused > 0) {
				// Entries the planner never uses (DESIGN §8.6): offer to drop them in one go.
				ColoredWrapped(kYellow, std::format(text::kLearnedUnusedInfo, a_snap.learnedUnused));
				if (ConfirmButton(std::format(text::kForgetUnused, a_snap.learnedUnused), "forget-unused", std::format(text::kConfirmForgetUnused, a_snap.learnedUnused))) {
					Do([cell = a_snap.cellId]() { Controller::Get().ForgetStaleLearned(cell); });
				}
			}
			if (!ImGui::BeginTable("##learned", 3, kScrollTableFlags, ImGui::ImVec2{ 0.0f, TableHeight(a_snap.learned.size(), 16) })) {
				return;
			}
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn(text::kColItem, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.5f);
			ImGui::TableSetupColumn(text::kColContainer, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.4f);
			ImGui::TableSetupColumn("##remove", ImGui::ImGuiTableColumnFlags_WidthStretch, 0.1f);
			ImGui::TableHeadersRow();
			for (const auto& row : a_snap.learned) {
				ImGui::PushID(row.itemText.c_str());
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				Text(row.itemName);
				Tooltip(row.itemText);
				ImGui::TableNextColumn();
				if (!row.stale.empty()) {
					// Stale: the container is no longer used (yellow) or fails the checks right now (dimmed).
					if (row.unused) {
						Colored(kYellow, std::format("{} ({})", row.containerLabel, row.stale));
					} else {
						Disabled(std::format("{} ({})", row.containerLabel, row.stale));
					}
				} else if (row.containerManaged) {
					Text(row.containerLabel);
				} else {
					Disabled(row.containerLabel + text::kNotManagedSuffix);
				}
				Tooltip(row.containerText);
				ImGui::TableNextColumn();
				if (ImGui::SmallButton(text::kRemove)) {
					Do([cell = a_snap.cellId, item = row.item]() { Controller::Get().RemoveLearned(cell, item); });
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		// --- Keep rules tab -----------------------------------------------------------------------------

		std::string DescribeKeepEntry(const std::string& a_entry, const CategoryTree& a_tree)
		{
			const auto selector = core::KeepSelector::Parse(a_entry);
			if (selector.kind == core::KeepSelector::Kind::Category) {
				return std::format("{} ({})", PatternName(a_tree, a_entry), a_entry);
			}
			return a_entry;
		}

		void KeepListEditor(const char* a_id, std::vector<std::string>& a_list, std::array<char, 128>& a_buffer, const CategoryTree& a_tree, bool& a_dirty)
		{
			ImGui::PushID(a_id);
			std::optional<std::size_t> remove;
			for (std::size_t i = 0; i < a_list.size(); ++i) {
				ImGui::PushID(static_cast<int>(i));
				ImGui::Bullet();
				Text(DescribeKeepEntry(a_list[i], a_tree));
				ImGui::SameLine();
				if (ImGui::SmallButton(text::kRemove)) {
					remove = i;
				}
				ImGui::PopID();
			}
			if (a_list.empty()) {
				Disabled(text::kNone);
			}
			if (remove) {
				a_list.erase(a_list.begin() + static_cast<std::ptrdiff_t>(*remove));
				a_dirty = true;
			}
			ImGui::SetNextItemWidth(kFieldWidth);
			const bool entered = ImGui::InputTextWithHint("##add", text::kKeepAddHint, a_buffer.data(), a_buffer.size(), ImGui::ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			if (ImGui::Button(text::kAdd) || entered) {
				if (auto entry = FromBuffer(a_buffer); !entry.empty()) {
					a_list.push_back(std::move(entry));
					a_dirty = true;
				}
				a_buffer[0] = '\0';
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(200.0f);
			std::string picked;
			if (PatternCombo("##pick", picked, a_tree, text::kKeepPickCategory)) {
				a_list.push_back(std::move(picked));
				a_dirty = true;
			}
			ImGui::PopID();
		}

		bool QuotaHasSelector(const core::QuotaRule& a_rule)
		{
			return (a_rule.category && !a_rule.category->empty()) || (a_rule.keywordsAny && !a_rule.keywordsAny->empty()) ||
			       (a_rule.forms && !a_rule.forms->empty());
		}

		bool AnyQuotaWithoutSelector(const core::KeepConfig& a_keep)
		{
			return std::ranges::any_of(a_keep.quotas, [](const core::QuotaRule& q) { return !QuotaHasSelector(q); });
		}

		void RenderKeepReadOnly(const core::KeepConfig& a_keep, const CategoryTree& a_tree)
		{
			auto list = [&](const char* a_title, const std::vector<std::string>& a_items) {
				Text(a_title);
				ImGui::Indent();
				if (a_items.empty()) {
					Disabled(text::kNone);
				}
				for (const auto& e : a_items) {
					ImGui::Bullet();
					Text(DescribeKeepEntry(e, a_tree));
				}
				ImGui::Unindent();
			};
			list(text::kKeepAlways, a_keep.always);
			list(text::kKeepNever, a_keep.never);
			Text(text::kKeepQuotas);
			ImGui::Indent();
			if (a_keep.quotas.empty()) {
				Disabled(text::kNone);
			}
			for (const auto& q : a_keep.quotas) {
				ImGui::Bullet();
				Text(std::format(text::kQuotaLine, q.quota, q.category ? DescribeKeepEntry(*q.category, a_tree) : std::string(text::kNoCategory), q.restock ? text::kRestockMark : ""));
			}
			ImGui::Unindent();
		}

		// Always / never lists and quotas; shared by the per-home tab and the defaults on the Settings page.
		void KeepRulesBody(core::KeepConfig& a_draft, bool& a_dirty, std::array<char, 128>& a_addAlways, std::array<char, 128>& a_addNever, const CategoryTree& a_tree)
		{
			ImGui::SeparatorText(text::kKeepAlways);
			KeepListEditor("always", a_draft.always, a_addAlways, a_tree, a_dirty);
			ImGui::SeparatorText(text::kKeepNever);
			KeepListEditor("never", a_draft.never, a_addNever, a_tree, a_dirty);

			ImGui::SeparatorText(text::kKeepQuotas);
			std::optional<std::size_t> remove;
			if (!a_draft.quotas.empty()) {
				if (ImGui::BeginTable("##quotas", 5, kTableFlags)) {
					ImGui::TableSetupColumn(text::kColCategory, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.4f);
					ImGui::TableSetupColumn(text::kColQuota, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.17f);
					ImGui::TableSetupColumn(text::kColRestock, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.11f);
					ImGui::TableSetupColumn(text::kColOther, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.22f);
					ImGui::TableSetupColumn("##remove", ImGui::ImGuiTableColumnFlags_WidthStretch, 0.1f);
					ImGui::TableHeadersRow();
					for (std::size_t i = 0; i < a_draft.quotas.size(); ++i) {
						auto& q = a_draft.quotas[i];
						ImGui::PushID(static_cast<int>(i));
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						auto category = q.category.value_or(std::string{});
						ImGui::SetNextItemWidth(kFill);
						if (PatternCombo("##category", category, a_tree)) {
							q.category = category;
							a_dirty = true;
						}
						ImGui::TableNextColumn();
						int quota = q.quota;
						ImGui::SetNextItemWidth(kFill);
						if (ImGui::InputInt("##quota", &quota)) {
							q.quota = std::max(quota, 0);
							a_dirty = true;
						}
						ImGui::TableNextColumn();
						if (ImGui::Checkbox("##restock", &q.restock)) {
							a_dirty = true;
						}
						Tooltip(text::kRestockColTip);
						ImGui::TableNextColumn();
						std::vector<std::string> other;
						if (q.keywordsAny && !q.keywordsAny->empty()) {
							other.push_back(std::format(text::kKeywordsAny, Join(*q.keywordsAny)));
						}
						if (q.forms && !q.forms->empty()) {
							other.push_back(std::format(text::kFormsCount, q.forms->size()));
						}
						if (other.empty()) {
							Disabled(text::kNone);
						} else {
							Text(Join(other, "; "));
						}
						ImGui::TableNextColumn();
						if (ImGui::SmallButton(text::kRemove)) {
							remove = i;
						}
						ImGui::PopID();
					}
					ImGui::EndTable();
				}
			}
			if (remove) {
				a_draft.quotas.erase(a_draft.quotas.begin() + static_cast<std::ptrdiff_t>(*remove));
				a_dirty = true;
			}
			if (ImGui::Button(text::kAddQuota)) {
				core::QuotaRule rule;
				rule.quota = 5;
				rule.restock = true;  // a new quota is usually something to carry (files without the key stay off)
				a_draft.quotas.push_back(std::move(rule));
				a_dirty = true;
			}
		}

		void RenderKeep(const HomeSnapshot& a_snap)
		{
			auto&       st = g_state;
			const auto& tree = *a_snap.categories;
			if (!st.keepLoaded || (!st.keepDirty && st.keepLoadedJson != a_snap.keepJson)) {
				st.keepDraft = a_snap.keep.value_or(core::KeepConfig{});
				st.keepHasBlock = a_snap.keep.has_value();
				st.keepLoadedJson = a_snap.keepJson;
				st.keepDirty = false;
				st.keepLoaded = true;
			}
			auto& draft = st.keepDraft;

			Hint(text::kKeepIntro);
			if (!st.keepHasBlock) {
				Disabled(text::kKeepNoBlock);
			}
			bool inherit = draft.Inherits();
			if (ImGui::Checkbox(text::kKeepInherit, &inherit)) {
				draft.inherit = inherit;
				st.keepDirty = true;
			}
			Tooltip(text::kKeepInheritTip);

			KeepRulesBody(draft, st.keepDirty, st.addAlways, st.addNever, tree);

			switch (DraftButtons(st.keepDirty, AnyQuotaWithoutSelector(draft))) {
			case DraftAction::Save:
				Do([cell = a_snap.cellId, keep = draft]() { Controller::Get().SetKeep(cell, keep); });
				st.keepDirty = false;
				break;
			case DraftAction::Revert:
				st.keepLoaded = false;
				break;
			default:
				break;
			}
			if (st.keepHasBlock) {
				ButtonRow row(true);
				if (RowConfirm(row, text::kKeepUseGlobal, "keep-global", text::kConfirmKeepGlobal)) {
					Do([cell = a_snap.cellId]() { Controller::Get().SetKeep(cell, std::nullopt); });
					st.keepLoaded = false;
				}
			}

			ImGui::SeparatorText(text::kKeepGlobalHeader);
			Hint(text::kKeepGlobalWhere);
			RenderKeepReadOnly(a_snap.globalKeep, tree);
		}

		// --- Template (export) tab ----------------------------------------------------------------------

		void StartExport(const HomeSnapshot& a_snap, bool a_overwrite)
		{
			Controller::ExportRequest request;
			request.name = FromBuffer(g_state.exportName);
			request.author = FromBuffer(g_state.exportAuthor);
			request.version = FromBuffer(g_state.exportVersion);
			request.overwrite = a_overwrite;
			request.learnedAsForms = g_state.exportLearned;
			PanelModel::Get().BeginExport();
			Do([cell = a_snap.cellId, request]() {
				const auto  result = Controller::Get().ExportTemplate(cell, request);
				ExportState state;
				state.ok = result.status == Controller::ExportResult::Status::Ok;
				state.exists = result.status == Controller::ExportResult::Status::Exists;
				state.path = result.path;
				state.message = result.message;
				state.warnings = result.warnings;
				PanelModel::Get().SetExport(std::move(state));
			});
		}

		void RenderTemplateExport(const HomeSnapshot& a_snap)
		{
			auto& st = g_state;
			if (!st.exportInit || st.exportCell != a_snap.cellId) {
				// Same defaults as the hotkey's Save as template...: the home's name, the player character as
				// author (empty field) and an automatic version (empty field). Reset in another home.
				CopyTo(st.exportName, a_snap.homeName);
				st.exportAuthor[0] = '\0';
				st.exportVersion[0] = '\0';
				st.exportLearned = true;
				st.exportCell = a_snap.cellId;
				st.exportInit = true;
			}
			Hint(text::kExportIntro);
			ImGui::Spacing();
			ImGui::SetNextItemWidth(kFieldWidth);
			ImGui::InputText(text::kExportName, st.exportName.data(), st.exportName.size());
			ImGui::SetNextItemWidth(kFieldWidth);
			ImGui::InputTextWithHint(text::kExportAuthor, text::kExportAuthorHint, st.exportAuthor.data(), st.exportAuthor.size());
			ImGui::SetNextItemWidth(120.0f);
			ImGui::InputTextWithHint(text::kExportVersion, text::kExportVersionHint, st.exportVersion.data(), st.exportVersion.size());
			ImGui::Checkbox(text::kExportLearned, &st.exportLearned);
			Tooltip(text::kExportLearnedTip);
			Hint(std::format(text::kExportFile, core::TemplateFileStem(FromBuffer(st.exportName))));

			const auto state = PanelModel::Get().Export();
			if (state.pending) {
				Disabled(text::kExporting);
				return;
			}
			if (state.exists) {
				// The file is there already: the same question-and-answer as a ConfirmButton.
				ColoredWrapped(kYellow, state.message);
				if (ImGui::Button(text::kOverwrite)) {
					StartExport(a_snap, true);
				}
				ImGui::SameLine();
				if (ImGui::Button(text::kCancel)) {
					PanelModel::Get().SetExport(ExportState{});
				}
				return;
			}
			if (ImGui::Button(text::kExport)) {
				StartExport(a_snap, false);
			}
			if (state.ok) {
				ColoredWrapped(kGreen, state.message);
			} else if (!state.message.empty()) {
				ColoredWrapped(kRed, state.message);
			}
			if (!state.warnings.empty()) {
				Text(text::kExportWarnings);
				for (const auto& w : state.warnings) {
					ColoredWrapped(kYellow, w);
				}
			}
		}

		// --- the stash panel (window and "This home" page) ------------------------------------------

		template <class F>
		void TabItem(const char* a_label, Tab a_tab, F&& a_body)
		{
			const int flags = g_state.pendingTab == a_tab ? ImGui::ImGuiTabItemFlags_SetSelected : 0;
			if (ImGui::BeginTabItem(a_label, nullptr, flags)) {
				a_body();
				ImGui::EndTabItem();
			}
		}

		void RenderHome()
		{
			const auto snap = PanelModel::Get().Home();
			if (!snap || !snap->categories) {
				Disabled(text::kLoading);
				PanelModel::Get().RequestRefresh(Refresh::Full);
				return;
			}
			Sync(*snap);
			if (!snap->inHome) {
				Wrapped(std::format(text::kNotInHome, snap->notHomeReason));
				RenderAddArea(*snap);
				RenderStatus();
				return;
			}
			RenderHeader(*snap);
			if (!snap->enabled) {
				RenderSetup(*snap);
				RenderStatus();
				return;
			}
			if (ImGui::BeginTabBar("##DovaStashTabs")) {
				TabItem(text::kTabActions, Tab::Actions, [&]() { RenderActions(*snap); });
				TabItem(text::kTabContainers, Tab::Containers, [&]() { RenderContainers(*snap); });
				TabItem(text::kTabLearned, Tab::Learned, [&]() { RenderLearned(*snap); });
				TabItem(text::kTabKeep, Tab::Keep, [&]() { RenderKeep(*snap); });
				TabItem(text::kTabTemplate, Tab::Template, [&]() { RenderTemplateExport(*snap); });
				ImGui::EndTabBar();
			}
			g_state.pendingTab = Tab::None;
			RenderStatus();
		}

		void __stdcall RenderWindow()
		{
			PanelModel::Get().MarkRendered();
			// Re-centred and sized to fit every time it opens, so the title bar (and its X) can never end up
			// off screen, e.g. after a resolution or letterbox change.
			if (auto* viewport = ImGui::GetMainViewport()) {
				const ImGui::ImVec2 center{ viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f };
				ImGui::SetNextWindowPos(center, ImGui::ImGuiCond_Appearing, ImGui::ImVec2{ 0.5f, 0.5f });
				const ImGui::ImVec2 maxSize{ viewport->Size.x * 0.9f, viewport->Size.y * 0.85f };
				ImGui::SetNextWindowSize(ImGui::ImVec2{ std::min(780.0f, maxSize.x), std::min(640.0f, maxSize.y) }, ImGui::ImGuiCond_Appearing);
				ImGui::SetNextWindowSizeConstraints(ImGui::ImVec2{ 360.0f, 240.0f }, maxSize);
			}
			bool open = true;
			if (ImGui::Begin(text::kWindowTitle, &open, ImGui::ImGuiWindowFlags_NoCollapse)) {
				// Always-visible way out: Close at the top (the page can be long) and Esc.
				if (ImGui::Button(text::kClose)) {
					open = false;
				}
				ImGui::SameLine();
				Disabled(text::kCloseKey);
				if (ImGui::IsKeyPressed(ImGui::ImGuiKey_Escape, false)) {
					open = false;
				}
				RenderHome();
			}
			ImGui::End();
			if (!open) {
				if (auto* window = g_window.load()) {
					window->IsOpen = false;
				}
			}
		}

		// --- global section pages ----------------------------------------------------------------------

		void __stdcall RenderHomePage()
		{
			PanelModel::Get().MarkRendered();
			RenderHome();
		}

		// Settings page: every control applies immediately (Controller::ApplySettings validates, applies
		// and writes DovaStash.ini). The render thread edits a draft copy; it re-syncs from the snapshot
		// whenever the snapshot's values change (after an apply, or a change made elsewhere).
		struct SettingsDraft
		{
			std::optional<Settings::Values> draft;
			std::optional<Settings::Values> seen;  // snapshot values the draft was last synced to
			std::string                     confirm;  // the page's inline confirmation
		};
		SettingsDraft g_settings;

		void ApplySettings(const Settings::Values& a_values)
		{
			Do([a_values]() {
				Controller::Get().ApplySettings(a_values);
				PanelModel::Get().BuildGlobal();
			});
		}

		// Combo over `a_choices` (+ an "Off" entry for code -1). True when a_code changed.
		bool KeyCombo(const char* a_label, std::int32_t& a_code, std::span<const input::KeyChoice> a_choices, bool a_allowNone)
		{
			const auto current = a_code == Settings::kNoKey ? std::string(text::kSetKeyOff) : input::KeyName(a_code);
			bool       changed = false;
			if (ImGui::BeginCombo(a_label, current.c_str())) {
				if (a_allowNone && ImGui::Selectable(text::kSetKeyOff, a_code == Settings::kNoKey)) {
					a_code = Settings::kNoKey;
					changed = true;
				}
				for (const auto& k : a_choices) {
					const std::string name{ k.name };
					if (ImGui::Selectable(name.c_str(), k.code == a_code)) {
						a_code = k.code;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			return changed;
		}

		bool IndexCombo(const char* a_label, std::int32_t& a_value, std::span<const char* const> a_names)
		{
			int  index = std::clamp<int>(a_value, 0, static_cast<int>(a_names.size()) - 1);
			bool changed = ImGui::Combo(a_label, &index, a_names.data(), static_cast<int>(a_names.size()));
			if (changed) {
				a_value = index;
			}
			return changed;
		}

		struct GlobalKeepState
		{
			bool                  loaded{ false };
			bool                  dirty{ false };
			std::string           loadedJson;
			core::KeepConfig      draft;
			std::array<char, 128> addAlways{};
			std::array<char, 128> addNever{};
		};
		GlobalKeepState g_globalKeep;

		void RenderGlobalKeep(const GlobalSnapshot& a_global)
		{
			const auto home = PanelModel::Get().Home();
			if (!home || !home->categories) {
				Disabled(text::kLoading);
				PanelModel::Get().RequestRefresh(Refresh::Light);
				return;
			}
			auto& st = g_globalKeep;
			if (!st.loaded || (!st.dirty && st.loadedJson != a_global.globalKeepJson)) {
				st.draft = a_global.globalKeep;
				st.loadedJson = a_global.globalKeepJson;
				st.dirty = false;
				st.loaded = true;
			}
			Hint(text::kKeepGlobalIntro);
			ImGui::PushID("globalkeep");
			KeepRulesBody(st.draft, st.dirty, st.addAlways, st.addNever, *home->categories);
			switch (DraftButtons(st.dirty, AnyQuotaWithoutSelector(st.draft))) {
			case DraftAction::Save:
				Do([keep = st.draft]() {
					Controller::Get().SetGlobalKeep(keep);
					PanelModel::Get().BuildGlobal();
				});
				st.dirty = false;
				break;
			case DraftAction::Revert:
				st.loaded = false;
				break;
			default:
				break;
			}
			ImGui::PopID();
		}

		// Hover help for the setting just drawn: what it does, and the INI key it writes.
		void Help(std::string_view a_text, std::string_view a_iniKey)
		{
			if (ImGui::IsItemHovered(ImGui::ImGuiHoveredFlags_AllowWhenDisabled)) {
				const auto tip = std::format("{}\n\nDovaStash.ini: {}", a_text, a_iniKey);
				ImGui::SetTooltip("%s", tip.c_str());
			}
		}

		// Why the setting(s) above are greyed out.
		void Unavailable(std::string_view a_why)
		{
			ImGui::Indent();
			Disabled(std::string(a_why));
			ImGui::Unindent();
		}

		// Sliders apply when released, so dragging one writes the INI once.
		bool Released()
		{
			return ImGui::IsItemDeactivatedAfterEdit();
		}

		void __stdcall RenderSettingsPage()
		{
			const auto global = PanelModel::Get().Global();
			if (!global) {
				Disabled(text::kLoading);
				return;
			}
			if (!g_settings.seen || !(*g_settings.seen == global->settings)) {
				g_settings.seen = global->settings;
				g_settings.draft = global->settings;
			}
			auto& v = *g_settings.draft;
			bool  apply = false;  // checkboxes and combos apply at once; sliders when released

			Hint(text::kSettingsNote);

			ImGui::SeparatorText(text::kSetHotkeyHeader);
			apply |= KeyCombo(text::kSetHotkey, v.input.hotkey, input::KeyboardChoices(), false);
			Help(text::kSetHotkeyTip, "[Input] iHotkey");
			apply |= KeyCombo(text::kSetGamepad, v.input.gamepadHotkey, input::GamepadChoices(), true);
			Help(text::kSetGamepadTip, "[Input] iGamepadHotkey");

			ImGui::SeparatorText(text::kSetMenusHeader);
			static constexpr std::array kModes{ text::kSetModeAuto, text::kSetModeMessageBoxes, text::kSetModePanel, text::kSetModeLists };  // [UI] iMode 0-3
			apply |= IndexCombo(text::kSetMode, v.ui.mode, kModes);
			Help(text::kSetModeTip, "[UI] iMode");
			ImGui::BeginDisabled(!global->uiExtensions);
			apply |= ImGui::Checkbox(text::kSetCenter, &v.ui.centerMenus);
			Help(text::kSetCenterTip, "[UI] bCenterMenus");
			ImGui::EndDisabled();
			if (!global->uiExtensions) {
				Unavailable(global->isVR ? text::kSetNoUIExtensionsVR : text::kSetNoUIExtensions);
			}

			ImGui::SeparatorText(text::kSetSortingHeader);
			apply |= ImGui::Checkbox(text::kSetSortOnArrival, &v.sorting.sortOnArrival);
			Help(text::kSetSortOnArrivalTip, "[Sorting] bSortOnArrival");
			apply |= ImGui::Checkbox(text::kSetSortOnClose, &v.sorting.distributeOnMasterClose);
			Help(text::kSetSortOnCloseTip, "[Sorting] bDistributeOnMasterClose");
			apply |= ImGui::Checkbox(text::kSetRestock, &v.sorting.restockAfterDeposit);
			Help(text::kSetRestockTip, "[Sorting] bRestockAfterDeposit");
			apply |= ImGui::Checkbox(text::kSetFillDisplays, &v.sorting.fillDisplays);
			Help(text::kSetFillDisplaysTip, "[Sorting] bFillDisplays");

			ImGui::SeparatorText(text::kSetLearningHeader);
			apply |= ImGui::Checkbox(text::kSetLearn, &v.sorting.learn);
			Help(text::kSetLearnTip, "[Sorting] bLearn");
			ImGui::BeginDisabled(!v.sorting.learn);
			ImGui::Indent();
			ImGui::SliderInt(text::kSetSuggest, &v.sorting.suggestCategoryAfter, 0, 20, v.sorting.suggestCategoryAfter == 0 ? text::kSetSuggestNever : text::kSetSuggestFormat);
			apply |= Released();
			Help(text::kSetSuggestTip, "[Sorting] iSuggestCategoryAfter");
			ImGui::SliderInt(text::kSetUndo, &v.sorting.learnUndoSeconds, 0, 30, v.sorting.learnUndoSeconds == 0 ? text::kSetUndoOff : text::kSetUndoFormat);
			apply |= Released();
			Help(text::kSetUndoTip, "[Sorting] iLearnUndoSeconds");
			ImGui::Unindent();
			ImGui::EndDisabled();

			ImGui::SeparatorText(text::kSetCraftingHeader);
			{
				// [Sorting] iCraftingSupply 0-3 (core::crafting::SupplySetting); Auto says what it means here.
				const auto autoLabel = v.sorting.craftingSupply == 0 && !global->craftingMode.empty() ? std::format(text::kSetCraftAutoNow, global->craftingMode) :
				                                                                                         std::string(text::kSetCraftAuto);
				const std::array<const char*, 4> kSupply{ autoLabel.c_str(), text::kSetCraftOff, text::kSetCraftMove, text::kSetCraftStorage };
				apply |= IndexCombo(text::kSetCrafting, v.sorting.craftingSupply, kSupply);
				Help(text::kSetCraftingTip, "[Sorting] iCraftingSupply");
				if (!global->craftingWhy.empty()) {
					Unavailable(std::format(text::kSetCraftNow, global->craftingMode, global->craftingWhy));
				}
			}
			ImGui::BeginDisabled(global->craftingMode == core::crafting::SupplyModeName(core::crafting::SupplyMode::Off));
			ImGui::Indent();
			apply |= ImGui::Checkbox(text::kSetCraftingGear, &v.sorting.craftingSupplyGear);
			Help(text::kSetCraftingGearTip, "[Sorting] bCraftingSupplyGear");
			ImGui::Unindent();
			ImGui::EndDisabled();

			ImGui::SeparatorText(text::kSetTemplatesHeader);
			apply |= ImGui::Checkbox(text::kSetAutoEnable, &v.templates.autoEnable);
			Help(text::kSetAutoEnableTip, "[General] bAutoEnableTemplates");

			ImGui::SeparatorText(text::kSetSellHeader);
			ImGui::SliderInt(text::kSetSellPrice, &v.selling.pricePercent, 10, 200, text::kSetSellPriceFormat);
			apply |= Released();
			Help(text::kSetSellPriceTip, "[Selling] iSellPricePercent");
			ImGui::SliderInt(text::kSetAutoSell, &v.selling.autoSellHours, 0, 168, v.selling.autoSellHours == 0 ? text::kSetAutoSellOff : text::kSetAutoSellFormat);
			apply |= Released();
			Help(text::kSetAutoSellTip, "[Selling] iAutoSellHours");

			ImGui::SeparatorText(text::kSetQuickLootHeader);
			ImGui::BeginDisabled(!global->quickLootReady);
			apply |= ImGui::Checkbox(text::kSetQuickLoot, &v.integrations.quickLoot);
			Help(text::kSetQuickLootTip, "[Integrations] bQuickLoot");
			ImGui::BeginDisabled(!v.integrations.quickLoot);
			ImGui::Indent();
			apply |= ImGui::Checkbox(text::kSetItemTags, &v.integrations.quickLootItemTags);
			Help(text::kSetItemTagsTip, "[Integrations] bQuickLootItemTags");
			apply |= ImGui::Checkbox(text::kSetQuickLootIcons, &v.integrations.quickLootIcons);
			Help(text::kSetQuickLootIconsTip, "[Integrations] bQuickLootIcons");
			ImGui::Unindent();
			ImGui::EndDisabled();
			ImGui::EndDisabled();
			if (!global->quickLootReady) {
				Unavailable(text::kSetNoQuickLoot);
			}

			ImGui::SeparatorText(text::kSetWorldHeader);
			apply |= ImGui::Checkbox(text::kSetPrompt, &v.integrations.activatePrompt);
			Help(text::kSetPromptTip, "[Integrations] bActivatePrompt");
			ImGui::BeginDisabled(global->isVR);
			apply |= ImGui::Checkbox(text::kSetOverlay, &v.overlay.enabled);
			Help(text::kSetOverlayTip, "[Overlay] bEnabled");
			ImGui::BeginDisabled(!v.overlay.enabled);
			ImGui::Indent();
			apply |= ImGui::Checkbox(text::kSetLineOfSight, &v.overlay.requireLineOfSight);
			Help(text::kSetLineOfSightTip, "[Overlay] bRequireLineOfSight");
			ImGui::SliderFloat(text::kSetRange, &v.overlay.radius, 100.0f, 5000.0f, text::kSetRangeFormat);
			apply |= Released();
			Help(text::kSetRangeTip, "[Overlay] fRadius");
			ImGui::SliderInt(text::kSetMaxIcons, &v.overlay.maxIcons, 1, 32);
			apply |= Released();
			Help(text::kSetMaxIconsTip, "[Overlay] iMaxIcons");
			ImGui::SliderFloat(text::kSetIconSize, &v.overlay.iconSize, 8.0f, 128.0f, "%.0f");
			apply |= Released();
			Help(text::kSetIconSizeTip, "[Overlay] fIconSize");
			ImGui::Unindent();
			ImGui::EndDisabled();
			ImGui::EndDisabled();
			if (global->isVR) {
				Unavailable(text::kSetNoOverlayVR);
			}

			ImGui::SeparatorText(text::kSetLogHeader);
			static constexpr std::array kLevels{ text::kSetLogTrace, text::kSetLogDebug, text::kSetLogInfo, text::kSetLogWarnings, text::kSetLogErrors, text::kSetLogCritical,
				text::kSetLogOff };  // spdlog levels 0-6
			apply |= IndexCombo(text::kSetLogLevel, v.general.logLevel, kLevels);
			Help(text::kSetLogLevelTip, "[General] iLogLevel");

			// Resets the settings above; the keep rules below have their own Save and Revert.
			ImGui::Spacing();
			if (widgets::ConfirmButton(g_settings.confirm, text::kSetReset, "settings-reset", text::kSetConfirmReset)) {
				auto defaults = Settings::Values{};
				defaults.input = v.input;  // never lose the player's hotkey to a reset
				v = defaults;
				apply = true;
			}

			ImGui::SeparatorText(text::kKeepGlobalHeader);
			RenderGlobalKeep(*global);

			if (apply) {
				ApplySettings(v);
			}
		}

		void __stdcall RenderTemplatesPage()
		{
			const auto global = PanelModel::Get().Global();
			if (!global) {
				Disabled(text::kLoading);
				return;
			}
			PanelModel::Get().MarkRendered();  // keeps the current home's part fresh
			Hint(text::kTemplatesIntro);
			// The home the player is in: its templates, to switch to (the same list as This home).
			if (const auto snap = PanelModel::Get().Home(); snap && snap->inHome && !snap->layoutBroken && !snap->templates.empty()) {
				ImGui::SeparatorText(std::format(text::kTemplatesHere, snap->homeName).c_str());
				Tooltip(text::kTemplatesTip);
				RenderTemplateChoices(*snap);
				ImGui::Spacing();
			}
			if (global->templates.empty()) {
				Disabled(text::kTemplatesNone);
				return;
			}
			if (!ImGui::BeginTable("##templates", 5, kTableFlags)) {
				return;
			}
			ImGui::TableSetupColumn(text::kColTemplate, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.26f);
			ImGui::TableSetupColumn(text::kColAuthor, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.14f);
			ImGui::TableSetupColumn(text::kColHome, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.22f);
			ImGui::TableSetupColumn(text::kColRequires, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.26f);
			ImGui::TableSetupColumn(text::kColIssues, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.12f);
			ImGui::TableHeadersRow();
			for (const auto& t : global->templates) {
				ImGui::PushID(t.source.c_str());
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				Text(std::format(text::kNameVersion, t.name, t.version));
				Tooltip(std::format(text::kTemplateSourceTip, t.source, t.priority));
				if (t.autoDraft) {
					ImGui::SameLine();
					Disabled(text::kTemplateDraft);
					Tooltip(text::kTemplateDraftTip);
				}
				ImGui::TableNextColumn();
				if (t.author.empty()) {
					Disabled(text::kNone);
				} else {
					Text(t.author);
				}
				ImGui::TableNextColumn();
				if (t.cellName.empty() && t.cellText.empty()) {
					Disabled(text::kNone);
				} else {
					Text(t.cellName.empty() ? t.cellText : t.cellName);
				}
				Tooltip(t.cellText);
				ImGui::TableNextColumn();
				if (t.required.empty()) {
					Disabled(text::kNone);
				} else {
					Text(Join(t.required));
				}
				if (t.missing.empty() && t.conflicting.empty()) {
					Colored(kGreen, text::kOk);
				}
				if (!t.missing.empty()) {
					Colored(kRed, std::format(text::kMissing, Join(t.missing)));
				}
				if (!t.conflicting.empty()) {
					Colored(kRed, std::format(text::kConflicts, Join(t.conflicting)));
					Tooltip(text::kConflictsTip);
				}
				ImGui::TableNextColumn();
				if (t.diagnostics.empty()) {
					Disabled(text::kNone);
				} else {
					Colored(kYellow, std::format(text::kIssues, t.diagnostics.size()));
					Tooltip(Join(t.diagnostics, "\n"));
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		void DiagnosticsSection(const std::string& a_title, const std::vector<core::Diagnostic>& a_items)
		{
			ImGui::SeparatorText(a_title.c_str());
			if (a_items.empty()) {
				Colored(kGreen, text::kDiagNone);
				return;
			}
			const auto errors = std::ranges::count_if(a_items, [](const core::Diagnostic& d) { return d.severity == core::Diagnostic::Severity::Error; });
			Text(std::format(text::kDiagCounts, errors, static_cast<std::ptrdiff_t>(a_items.size()) - errors));
			for (const auto& d : a_items) {
				ColoredWrapped(d.severity == core::Diagnostic::Severity::Error ? kRed : kYellow, d.ToString());
			}
		}

		void __stdcall RenderDiagnosticsPage()
		{
			PanelModel::Get().MarkRendered();  // keeps the current-home part fresh
			const auto global = PanelModel::Get().Global();
			if (!global) {
				Disabled(text::kLoading);
				return;
			}
			Hint(text::kDiagIntro);
			DiagnosticsSection(text::kDiagCategories, global->categoryDiagnostics);
			DiagnosticsSection(text::kDiagTemplates, global->templateDiagnostics);
			DiagnosticsSection(text::kDiagSettings, global->settingsDiagnostics);
			DiagnosticsSection(text::kDiagSelfSorting, global->selfSortingDiagnostics);

			const auto snap = PanelModel::Get().Home();
			if (snap && snap->inHome && snap->enabled) {
				ImGui::SeparatorText(std::format(text::kDiagHome, snap->homeName).c_str());
				if (snap->layoutWarnings.empty()) {
					Colored(kGreen, text::kDiagNone);
				}
				for (const auto& w : snap->layoutWarnings) {
					ColoredWrapped(kYellow, w);
				}
			}
		}

		void __stdcall RenderAboutPage()
		{
			const auto global = PanelModel::Get().Global();
			if (!global) {
				Disabled(text::kLoading);
				return;
			}
			const auto available = [](bool a_yes) { return std::string(a_yes ? text::kAboutAvailable : text::kAboutNotAvailable); };
			Colored(kAccent, std::format(text::kAboutTitle, global->version));
			Hint(text::kAboutText);
			ImGui::Spacing();
			Text(std::format(text::kAboutRuntime, global->runtime));
			Text(std::format(text::kAboutFramework, global->frameworkVersion));
			Text(std::format(text::kAboutHotkey, global->hotkeyName));
			Text(std::format(text::kAboutCategories, global->categoryCount));
			Text(std::format(text::kAboutTemplates, global->templates.size()));
			Text(std::format(text::kAboutQuickLoot, available(global->quickLootReady)));
			Text(std::format(text::kAboutUIExtensions, available(global->uiExtensions)));
			const auto& editor = EditorInstallPath();
			Wrapped(editor.empty() ? std::string(text::kAboutEditorMissing) : std::format(text::kAboutEditor, editor));

			ImGui::SeparatorText(text::kAboutFilesHeader);
			Wrapped(text::kAboutData);
			Wrapped(text::kAboutLog);
			Wrapped(text::kAboutBackups);
			ImGui::Spacing();
			Hint(text::kAboutLicense);
		}

		void __stdcall OnFrameworkEvent(SKSEMenuFramework::Model::EventType a_type)
		{
			if (a_type == SKSEMenuFramework::Model::kOpenMenu) {
				PanelModel::Get().RequestRefresh(Refresh::Full);
			}
		}
	}

	bool Register()
	{
		static bool attempted = false;  // main thread only
		if (attempted) {
			return g_window.load() != nullptr;
		}
		attempted = true;

		if (!GetMenuFrameworkModule()) {
			logger::warn("SKSE Menu Framework: SKSEMenuFramework.dll is not loaded");
			return false;
		}
		auto* window = SKSEMenuFramework::AddWindow(RenderWindow, true);
		if (!window) {
			logger::warn("SKSE Menu Framework: AddWindow is unavailable (framework too old?)");
			return false;
		}
		g_window = window;

		// The current home's pages first, then the ones for every home.
		SKSEMenuFramework::SetSection(text::kSection);
		SKSEMenuFramework::AddSectionItem(text::kItemHome, RenderHomePage);
		SKSEMenuFramework::AddSectionItem(text::kItemDisplays, RenderDisplaysPage);    // UI/Panel/DisplaysPage.cpp
		SKSEMenuFramework::AddSectionItem(text::kItemArmorSets, RenderArmorSetsPage);  // UI/Panel/ArmorSetsPage.cpp
		SKSEMenuFramework::AddSectionItem(text::kItemSettings, RenderSettingsPage);
		SKSEMenuFramework::AddSectionItem(text::kItemTemplates, RenderTemplatesPage);
		SKSEMenuFramework::AddSectionItem(text::kItemDiagnostics, RenderDiagnosticsPage);
		SKSEMenuFramework::AddSectionItem(text::kItemAbout, RenderAboutPage);
		static auto* event = SKSEMenuFramework::AddEvent(OnFrameworkEvent, 0.0f);  // lives for the whole session
		(void)event;
		logger::info("SKSE Menu Framework: stash window and \"{}\" section registered", text::kSection);
		return true;
	}

	void OpenWindow()
	{
		if (auto* window = g_window.load()) {
			window->IsOpen = true;
		}
	}
}
