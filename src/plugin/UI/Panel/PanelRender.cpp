#include "UI/Panel/PanelRender.h"

#include "Input/InputHandler.h"

#include "Controller.h"
#include "Sort/SortEngine.h"

#include <dova/core/CraftingStorage.h>
#include "UI/Labels.h"
#include "UI/Panel/DisplaysPage.h"
#include "UI/Panel/EditorInfo.h"
#include "UI/Panel/KeyCapture.h"
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
			std::optional<core::FormRef> aimedSeen;  // the aimed container last scrolled into view
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

		// Typed keep entries that are not a selector (a typo such as "quets"): the field's buffer (each page has its
		// own, so the home's Keep rules and the Settings defaults never show each other's), shown under that field.
		// Cleared on a home change (Sync), on Revert and when the field is edited.
		const char* g_keepInvalid = nullptr;

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
		bool ConfirmButton(const std::string& a_label, const std::string& a_key, const std::string& a_question, bool a_small = false, std::string_view a_tip = {})
		{
			return widgets::ConfirmButton(g_state.confirm, a_label, a_key, a_question, a_small, a_tip);
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

		// PatternName (PanelModel.h): the one wording the Displays page shares.

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
					out.push_back({ node.id, indent + node.name });
					return;
				}
				out.push_back({ node.id + ".*", indent + std::format(text::kAllOf, node.name) });
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
			const auto  preview = a_preview ? std::string(a_preview) : a_value.empty() ? std::string(text::kNoCategory) : PatternName(a_tree, a_value);
			const bool  open = ImGui::BeginCombo(a_id, preview.c_str());
			if (!open && !a_preview && !a_value.empty()) {
				Tooltip(std::format(text::kPatternIdTip, a_value));
			}
			if (open) {
				for (const auto& choice : PatternChoices(a_tree)) {
					const bool selected = !a_preview && core::IEquals(choice.pattern, a_value);
					if (ImGui::Selectable(choice.label.c_str(), selected)) {
						a_value = choice.pattern;
						changed = true;
					}
					Tooltip(std::format(text::kPatternIdTip, choice.pattern));
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
				if (g_keepInvalid == g_state.addAlways.data() || g_keepInvalid == g_state.addNever.data()) {
					g_keepInvalid = nullptr;  // the buffers stay where they are: the old home's typo would show under the new one's field
				}
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
			StatusLine(PanelModel::Get().Status(), PanelModel::Get().StatusIsError());
		}

		// --- header / setup -----------------------------------------------------------------------------

		void RenderHeader(const HomeSnapshot& a_snap)
		{
			Colored(kAccent, std::format(text::kHeaderHome, a_snap.homeName));
			Tooltip(a_snap.cellText);  // the cell's form reference, for bug reports and template authors
			if (!a_snap.hereArea.empty()) {
				ImGui::SameLine();
				Text(std::format(text::kHereArea, a_snap.hereArea));
			}
			if (a_snap.enabled) {
				Text(std::format(text::kHeaderLayout, NameLine(a_snap.layoutName, a_snap.layoutVersion, a_snap.layoutAuthor)));
				Disabled(a_snap.basedOn ? std::format(text::kHeaderBasedOn, a_snap.basedOnText) : std::string(text::kHeaderCustom));
				if (a_snap.basedOn) {
					Tooltip(*a_snap.basedOn);  // the template file and version the setup came from
				}
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
		// With a setup, each row also has Remove... (asks first): the template is no longer offered in this home
		// (Controller::SetTemplateHidden; the file stays). Removed ones come last, dimmed, with Add back.
		void RenderTemplateChoices(const HomeSnapshot& a_snap)
		{
			const auto cell = a_snap.cellId;
			const bool confirm = a_snap.enabled && a_snap.ownChanges;
			for (const auto& t : a_snap.templates) {
				ImGui::PushID(t.source.c_str());
				ImGui::AlignTextToFramePadding();
				if (t.hidden) {
					Disabled(NameLine(t.name, t.version, t.author));
					Tooltip(t.source);
					ImGui::SameLine();
					Disabled(text::kTemplateRemoved);
					Tooltip(text::kTemplateRemovedTip);
					ButtonRow row(true);
					row.Next();
					if (Button(text::kAddBackTemplate, true)) {
						Do([cell, source = t.source]() { Controller::Get().SetTemplateHidden(cell, source, false); }, Refresh::Full);
					}
					Tooltip(text::kAddBackTemplateTip);
					ImGui::PopID();
					continue;
				}
				Text(NameLine(t.name, t.version, t.author));
				Tooltip(t.source);
				if (t.applied) {
					ImGui::SameLine();
					Colored(kGreen, text::kTemplateApplied);
					Tooltip(text::kStatusInUseTip);
				}
				if (t.best) {
					ImGui::SameLine();
					Disabled(text::kTemplateBest);
					Tooltip(text::kTemplateBestTip);
				}
				if (t.autoDraft) {
					ImGui::SameLine();
					Disabled(text::kTemplateDraft);
					Tooltip(text::kTemplateDraftTip);
				}
				ButtonRow  row(true);
				bool       go = false;
				const auto tip = t.applied ? text::kReapplyTemplateTip : text::kUseTemplateTip;
				if (confirm) {
					const auto key = "tpl:" + t.source;
					go = RowConfirm(row, t.applied ? text::kReapplyTemplateAsk : text::kUseTemplateAsk, key, t.applied ? std::format(text::kConfirmReapplyTemplate, t.name) : std::format(text::kConfirmTemplate, t.name), true);
					if (!go && g_state.confirm != key) {
						Tooltip(tip);  // the button, not the question's Yes / No
					}
				} else {
					row.Next();
					go = Button(t.applied ? text::kReapplyTemplate : text::kUseTemplate, true);
					Tooltip(tip);
				}
				if (go) {
					Do([cell, source = t.source]() { Controller::Get().UseTemplate(cell, source); }, Refresh::Full);
				}
				if (a_snap.enabled) {  // the list of removed templates lives in the setup
					const auto key = "rm:" + t.source;
					if (RowConfirm(row, text::kRemoveAsk, key, std::format(text::kConfirmRemoveTemplate, t.name), true)) {
						Do([cell, source = t.source]() { Controller::Get().SetTemplateHidden(cell, source, true); }, Refresh::Full);
					} else if (g_state.confirm != key) {
						Tooltip(text::kRemoveTemplateTip);
					}
				}
				ImGui::PopID();
			}
		}

		// "Templates" (This home > Template, and the setup page): the header, its
		// tooltip saying what the list is, then the list or why it is empty.
		void RenderTemplateList(const HomeSnapshot& a_snap, const std::string& a_header)
		{
			ImGui::SeparatorText(a_header.c_str());
			Tooltip(text::kTemplatesTip);
			if (std::ranges::all_of(a_snap.templates, &TemplateOption::hidden)) {
				Disabled(text::kNoTemplate);  // the removed ones, if any, still follow (Add back)
			}
			RenderTemplateChoices(a_snap);
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
				bool go = false;
				if (a_confirm) {
					go = ConfirmButton(a_label, std::format("restore:{}", a_index), std::format(ui::kConfirmRestore, backup.date), false, text::kRestoreTip);
				} else {
					go = Button(a_label);
					Tooltip(text::kRestoreNowTip);
				}
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
						const auto& date = a_snap.backups[i].date;
						offer(i, a_confirm ? std::format(text::kRestoreOlder, date) : std::format(text::kRestoreOlderNow, date));
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
				if (ConfirmButton(text::kResetHome, "reset", std::format(text::kConfirmReset, a_snap.homeName), false, text::kResetHomeTip)) {
					Do([cell]() { Controller::Get().ResetHome(cell); }, Refresh::Full);
				}
				return;
			}
			Wrapped(text::kSetupIntro);
			// The container the hotkey was pressed at, else the one the player looks at (opened from the framework's menu).
			const auto          focusIndex = a_snap.focusRow ? a_snap.focusRow : a_snap.aimedRow;
			const ContainerRow* focus = focusIndex && *focusIndex < a_snap.containers.size() ? &a_snap.containers[*focusIndex] : nullptr;
			if (focus && !focus->master) {
				if (focus->available) {
					if (Button(std::format(text::kSetupMakeMaster, focus->label))) {
						Do([cell, ref = focus->ref]() { Controller::Get().MakeMaster(cell, ref); }, Refresh::Full);
					}
					Tooltip(text::kSetupMakeMasterTip);
				} else {
					ColoredWrapped(kRed, std::format(text::kNameDetail, focus->label, std::format(text::kUnavailable, focus->reason)));
				}
			} else {
				const auto global = PanelModel::Get().Global();
				Hint(std::format(text::kSetupLookAt, global ? global->hotkeyName : std::string(text::kSetupHotkeyFallback)));
			}
			if (!a_snap.templates.empty()) {
				RenderTemplateList(a_snap, text::kSetupTemplates);
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
			// "###node": the ID leaves out the count, so a branch stays open when a Retrieve changes it (PushID keeps it unique).
			const auto label = std::format("{}###node", a_label);
			if (a_branch) {
				a_open = ImGui::TreeNodeEx(label.c_str(), 0);
			} else {
				ImGui::TreeNodeEx(label.c_str(), ImGui::ImGuiTreeNodeFlags_Leaf | ImGui::ImGuiTreeNodeFlags_NoTreePushOnOpen);
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
					Tooltip(text::kAreaRenameButtonTip);
					if (RowConfirm(row, text::kRemoveAsk, "area:" + area.cellText, std::format(ui::kConfirmRemoveArea, area.name, a_snap.homeName), true)) {
						Do([cell, ref = area.cell]() { Controller::Get().RemoveArea(cell, ref); }, Refresh::Full);
					} else if (g_state.confirm != "area:" + area.cellText) {
						Tooltip(text::kAreaRemoveTip);
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

		// "1 warning", "3 warnings": a count with its singular or plural pattern (Strings.h has no "(s)").
		std::string Count(std::int64_t a_n, std::string_view a_one, std::string_view a_many)
		{
			return std::vformat(a_n == 1 ? a_one : a_many, std::make_format_args(a_n));
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
					Disabled(sell.stays.empty() ? std::string(text::kSellNothing) : std::format(text::kNameDetail, text::kSellNothing, sell.stays));
				} else {
					Wrapped(std::format(text::kSellQuote, Items(sell.items), sell.gold, sell.what));
					if (!sell.stays.empty()) {
						Hint(sell.stays);
					}
					if (ConfirmButton(std::format(ui::kSellNow, sell.gold), "sell", std::format(ui::kConfirmSell, Items(sell.items), sell.gold, sell.what), false, text::kSellNowTip)) {
						Do([cell, items = sell.items, gold = sell.gold]() { Controller::Get().SellConfirmed(cell, std::nullopt, items, gold); }, Refresh::Full);
					}
				}
				if (sell.buyBack) {
					const auto [items, gold] = *sell.buyBack;
					Hint(std::format(text::kBuyBackInfo, Items(items), gold, sell.buyBackWhat));
					if (ConfirmButton(std::format(ui::kBuyBack, gold), "buyback", std::format(ui::kConfirmBuyBack, Items(items), gold, sell.buyBackWhat), false, text::kBuyBackTip)) {
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

		// Everyday actions first (Sort, Sell chest), a way to the Template tab above the long Retrieve tree, then the
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

			// Templates live on the Template tab (use one, save this setup as one); one button takes the player there.
			ImGui::SeparatorText(text::kActionsTemplates);
			if (ImGui::Button(text::kOpenTemplateTab)) {
				g_state.pendingTab = Tab::Template;  // TabItem selects it this frame (the tab comes after this one)
			}
			Tooltip(text::kOpenTemplateTabTip);

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
			if (ConfirmButton(text::kResetHome, "reset", std::format(text::kConfirmReset, a_snap.homeName), false, text::kResetHomeTip)) {
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
				Tooltip(std::format(text::kPatternIdTip, node.id));
				if (!exact && Covers(a_row, node.id)) {
					ImGui::SameLine();
					Disabled(text::kViaWider);
				}
			} else {
				const auto all = node.id + ".*";
				const bool exactAll = HasPattern(a_row, all);
				const auto mark = exactAll ? ui::kMarkAll : (Covers(a_row, node.id) || AnyBelow(a_row, a_tree, a_index)) ? ui::kMarkSome : ui::kMarkNone;
				const bool open = ImGui::TreeNodeEx(std::format("{} {}{}###tree", mark, node.name, ui::LearnedSuffix(a_row.learnedByCategory, all)).c_str(), 0);
				Tooltip(std::format(text::kPatternIdTip, all));
				if (!exactAll && Covers(a_row, node.id)) {
					ImGui::SameLine();
					Disabled(text::kViaWider);
				}
				if (open) {
					bool value = exactAll;
					if (ImGui::Checkbox(std::format(text::kAllOf, node.name).c_str(), &value)) {
						a_toggle(all);
					}
					Tooltip(std::format(text::kPatternIdTip, all));
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
			Tooltip(a_row.refText);  // the form reference, for bug reports and template authors
			if (!a_row.gameName.empty() && a_row.gameName != a_row.label) {
				Disabled(std::format(text::kInParens, a_row.gameName));
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
			Tooltip(text::kRenameTip);

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
				Tooltip(a_row.ignored ? text::kStopIgnoringTip : text::kIgnoreTip);
				if (a_row.sell) {
					row.Next();
					if (Button(ui::kNotSellChest)) {
						Do([cell, ref]() { Controller::Get().SetSellChest(cell, ref, false); }, Refresh::Full);
					}
					Tooltip(text::kNotSellChestTip);
				} else if (!a_row.ignored && a_row.available) {
					if (RowConfirm(row, ui::kMakeSellChest, "sellchest:" + a_row.refText, ui::SellChestQuestion(a_row.label))) {
						Do([cell, ref]() { Controller::Get().SetSellChest(cell, ref, true); }, Refresh::Full);
					} else if (g_state.confirm != "sellchest:" + a_row.refText) {
						Tooltip(text::kMakeSellChestTip);
					}
				}
				if (a_row.available && a_row.extraArea) {
					// A container of an extra area becomes that area's master (DESIGN §6.7), as in the hotkey menus.
					if (RowConfirm(row, std::format(ui::kMakeAreaMasterEntry, a_row.extraAreaName), "master:" + a_row.refText,
							ui::MakeAreaMasterQuestion(a_row.label, a_row.extraAreaName, a_row.managed || a_row.learnedCount > 0))) {
						Do([cell, ref, areaCell = *a_row.extraArea]() { Controller::Get().MakeAreaMaster(cell, ref, areaCell); }, Refresh::Full);
					} else if (g_state.confirm != "master:" + a_row.refText) {
						Tooltip(text::kMakeAreaMasterTip);
					}
				} else if (a_row.available) {
					// The hotkey menus ask the same (ui::ContainerMenuEntries): its binding and learned items go.
					if (RowConfirm(row, text::kMakeMaster, "master:" + a_row.refText, ui::MakeMasterQuestion(a_row.label, a_row.managed || a_row.learnedCount > 0))) {
						Do([cell, ref]() { Controller::Get().MakeMaster(cell, ref); }, Refresh::Full);
					} else if (g_state.confirm != "master:" + a_row.refText) {
						Tooltip(text::kMakeMasterTip);
					}
				}
			}
			if (!a_row.patterns.empty() || a_row.learnedCount > 0 || a_row.managed) {
				ButtonRow row;
				if (!a_row.patterns.empty()) {
					// The tooltip only on the button itself, not on the question's No that takes its place.
					if (RowConfirm(row, text::kClearCategories, "clear:" + a_row.refText, std::format(ui::kConfirmClearCategories, a_row.label))) {
						Do([cell, ref]() { Controller::Get().ClearCategories(cell, ref); });
					} else if (g_state.confirm != "clear:" + a_row.refText) {
						Tooltip(text::kClearCategoriesTip);
					}
				}
				if (a_row.learnedCount > 0) {
					if (RowConfirm(row, std::format(text::kForgetLearned, a_row.learnedCount), "forget:" + a_row.refText,
							ui::ForgetLearnedQuestion(a_row.learnedCount, a_row.label))) {
						Do([cell, ref]() { Controller::Get().ForgetLearned(cell, ref); });
					} else if (g_state.confirm != "forget:" + a_row.refText) {
						Tooltip(text::kForgetLearnedTip);
					}
				}
				// Something to remove (never the master: returned above).
				if (a_row.managed || a_row.learnedCount > 0) {
					if (RowConfirm(row, text::kRemoveAsk, "remove:" + a_row.refText, std::format(ui::kConfirmRemove, a_row.label, ui::kRemoveExplained))) {
						Do([cell, ref]() { Controller::Get().RemoveContainer(cell, ref); }, Refresh::Full);
					} else if (g_state.confirm != "remove:" + a_row.refText) {
						Tooltip(text::kRemoveContainerTip);
					}
				}
			}
			if (a_row.explicitForms > 0) {
				Disabled(std::format(text::kExplicitForms, Count(a_row.explicitForms, text::kExplicitForm1, text::kExplicitFormN)));
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
				Tooltip(text::kSuggestionYesTip);
				ImGui::SameLine();
				if (ImGui::Button(text::kSuggestionNo)) {
					Do([cell, ref, category = a_row.suggestionCategory]() { Controller::Get().DeclineSuggestion(cell, ref, category); });
				}
				Tooltip(text::kSuggestionNoTip);
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
				Tooltip(text::kAskAgainTip);
			}
			const std::function<void(std::string)> toggle = [cell, ref](std::string a_pattern) {
				Do([cell, ref, pattern = std::move(a_pattern)]() { Controller::Get().ToggleCategory(cell, ref, pattern); });
			};
			ImGui::BeginDisabled(!a_row.available);
			bool everything = HasPattern(a_row, "*");
			if (ImGui::Checkbox(text::kEverything, &everything)) {
				toggle("*");
			}
			Tooltip(std::format(text::kPatternIdTip, "*"));
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
					Tooltip(text::kRemovePatternTip);
					ImGui::PopID();
				}
			}
			ImGui::EndDisabled();
		}

		// The table of containers, the one the player looks at marked "(looking at it)" (selected when nothing is, and scrolled
		// into view once each time it changes), then the selected one's details.
		void RenderContainers(const HomeSnapshot& a_snap)
		{
			const auto& rows = a_snap.containers;
			const auto* aimed = a_snap.aimedRow && *a_snap.aimedRow < rows.size() ? &rows[*a_snap.aimedRow] : nullptr;
			const bool  scrollToAimed = aimed && g_state.aimedSeen != aimed->ref;
			if (scrollToAimed) {
				g_state.aimedSeen = aimed->ref;
				if (!g_state.selected) {
					Select(*aimed);
				}
			}
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
					if (scrollToAimed && &row == aimed) {
						ImGui::SetScrollHereY(0.5f);
					}

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
						Disabled(std::format(text::kInParens, row.area));
						Tooltip(text::kRemoteTip);
					}
					if (!row.warning.empty()) {
						ImGui::SameLine();
						Colored(kYellow, text::kWarningMark);
						Tooltip(row.warning);
					}
					if (&row == aimed) {
						ImGui::SameLine();
						Colored(kAccent, text::kStatusAimed);
						Tooltip(text::kStatusAimedTip);
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
				if (ConfirmButton(std::format(text::kForgetUnused, a_snap.learnedUnused), "forget-unused", std::format(text::kConfirmForgetUnused, Items(a_snap.learnedUnused)), false,
						text::kForgetUnusedTip)) {
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
						Colored(kYellow, std::format(text::kNameDetail, row.containerLabel, row.stale));
					} else {
						Disabled(std::format(text::kNameDetail, row.containerLabel, row.stale));
					}
				} else if (row.containerManaged) {
					Text(row.containerLabel);
				} else {
					Disabled(row.containerLabel + text::kNotManagedSuffix);
				}
				Tooltip(row.containerText);
				ImGui::TableNextColumn();
				if (ImGui::SmallButton(text::kForgetOne)) {
					Do([cell = a_snap.cellId, item = row.item]() { Controller::Get().RemoveLearned(cell, item); });
				}
				Tooltip(text::kForgetOneTip);
				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		// --- Keep rules tab -----------------------------------------------------------------------------

		using KeepNames = std::map<std::string, std::string>;

		// What a keep entry reads as: a category's name, a rule's words, an item's name (the model resolves the
		// saved ones; the Add item picker knows what was just added), or the ref with "(not loaded)".
		std::string DescribeKeepEntry(const std::string& a_entry, const CategoryTree& a_tree, const KeepNames& a_names,
			const std::vector<InventoryEntry>& a_items = {})
		{
			const auto selector = core::KeepSelector::Parse(a_entry);
			switch (selector.kind) {
			case core::KeepSelector::Kind::Category:
				return PatternName(a_tree, a_entry);  // the id is the row's tooltip
			case core::KeepSelector::Kind::Equipped:
				return text::kKeepRuleEquipped;
			case core::KeepSelector::Kind::Favorited:
				return text::kKeepRuleFavorited;
			case core::KeepSelector::Kind::Quest:
				return text::kKeepRuleQuest;
			case core::KeepSelector::Kind::Form:
				{
					const auto ref = selector.form.ToString();
					if (const auto it = a_names.find(ref); it != a_names.end()) {
						return it->second.empty() ? std::format(text::kKeepNotLoaded, ref) : it->second;
					}
					if (const auto it = std::ranges::find(a_items, ref, &InventoryEntry::refText); it != a_items.end()) {
						return it->name;
					}
					return a_entry;
				}
			default:
				return a_entry;
			}
		}

		// A keep entry as text; an item's form ref is its tooltip.
		void KeepEntryText(const std::string& a_entry, const CategoryTree& a_tree, const KeepNames& a_names,
			const std::vector<InventoryEntry>& a_items = {})
		{
			Text(DescribeKeepEntry(a_entry, a_tree, a_names, a_items));
			if (const auto selector = core::KeepSelector::Parse(a_entry); selector.kind == core::KeepSelector::Kind::Form) {
				Tooltip(selector.form.ToString());
			} else if (selector.kind == core::KeepSelector::Kind::Category) {
				Tooltip(std::format(text::kPatternIdTip, a_entry));
			}
		}


		// A keep entry that does something: a token, a form ref, or a category pattern some category matches.
		bool IsKeepSelector(const std::string& a_entry, const CategoryTree& a_tree)
		{
			const auto selector = core::KeepSelector::Parse(a_entry);
			if (selector.kind == core::KeepSelector::Kind::Invalid) {
				return false;
			}
			if (selector.kind != core::KeepSelector::Kind::Category) {
				return true;
			}
			return std::ranges::any_of(a_tree.nodes, [&](const CategoryNode& a_node) { return core::CategoryPatternMatches(selector.pattern, a_node.id); });
		}

		void KeepListEditor(const char* a_id, std::vector<std::string>& a_list, std::array<char, 128>& a_buffer, const CategoryTree& a_tree,
			const std::vector<InventoryEntry>& a_items, const KeepNames& a_names, bool& a_dirty)
		{
			const auto add = [&](std::string a_entry) {
				if (std::ranges::none_of(a_list, [&](const std::string& e) { return core::IEquals(e, a_entry); })) {
					a_list.push_back(std::move(a_entry));
					a_dirty = true;
				}
			};
			ImGui::PushID(a_id);
			std::optional<std::size_t> remove;
			for (std::size_t i = 0; i < a_list.size(); ++i) {
				ImGui::PushID(static_cast<int>(i));
				ImGui::Bullet();
				KeepEntryText(a_list[i], a_tree, a_names, a_items);
				ImGui::SameLine();
				if (ImGui::SmallButton(text::kRemove)) {
					remove = i;
				}
				Tooltip(text::kRemoveKeepEntryTip);
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
			Tooltip(text::kKeepAddTip);
			if (ImGui::IsItemEdited() && g_keepInvalid == a_buffer.data()) {
				g_keepInvalid = nullptr;  // being corrected
			}
			ImGui::SameLine();
			if (ImGui::Button(text::kAdd) || entered) {
				if (auto entry = FromBuffer(a_buffer); !entry.empty()) {
					if (IsKeepSelector(entry, a_tree)) {
						add(std::move(entry));
						a_buffer[0] = '\0';
						g_keepInvalid = nullptr;
					} else {
						g_keepInvalid = a_buffer.data();  // kept in the field, to correct it
					}
				}
			}
			if (g_keepInvalid == a_buffer.data()) {
				Colored(kYellow, text::kKeepNotSelector);
			}
			// Pickers, so nobody has to type a form reference: a rule, a category, an item the player carries.
			ImGui::SetNextItemWidth(140.0f);
			if (ImGui::BeginCombo("##rule", text::kKeepPickRule)) {
				const std::array<std::pair<const char*, const char*>, 3> kRules{ { { text::kKeepRuleEquipped, "equipped" },
					{ text::kKeepRuleFavorited, "favorited" }, { text::kKeepRuleQuest, "quest" } } };
				for (const auto& [label, token] : kRules) {
					if (ImGui::Selectable(label, false)) {
						add(token);
					}
				}
				ImGui::EndCombo();
			}
			Tooltip(text::kKeepPickRuleTip);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(200.0f);
			std::string picked;
			if (PatternCombo("##pick", picked, a_tree, text::kKeepPickCategory)) {
				add(std::move(picked));
			}
			Tooltip(text::kKeepPickCategoryTip);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::BeginCombo("##item", text::kKeepPickItem, ImGui::ImGuiComboFlags_HeightLarge)) {
				// A late-game inventory has hundreds of entries and combos have no type-ahead: a search box first.
				static std::array<char, 64> search{};
				if (ImGui::IsWindowAppearing()) {
					search.fill('\0');
					ImGui::SetKeyboardFocusHere();
				}
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::InputTextWithHint("##itemsearch", text::kKeepItemSearchHint, search.data(), search.size());
				const auto lower = [](std::string a_text) {
					std::ranges::transform(a_text, a_text.begin(), [](unsigned char a_c) { return static_cast<char>(std::tolower(a_c)); });
					return a_text;
				};
				const auto needle = lower(search.data());
				if (a_items.empty()) {
					Disabled(text::kKeepNoItems);
				}
				std::size_t shown = 0;
				for (const auto& item : a_items) {
					if (!needle.empty() && lower(item.name).find(needle) == std::string::npos) {
						continue;
					}
					++shown;
					ImGui::PushID(item.refText.c_str());
					if (ImGui::Selectable(item.name.c_str(), false)) {
						add(item.refText);
					}
					Tooltip(item.refText);
					ImGui::PopID();
				}
				if (shown == 0 && !a_items.empty()) {
					Disabled(text::kKeepNoItemMatch);
				}
				ImGui::EndCombo();
			}
			Tooltip(text::kKeepPickItemTip);
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

		void RenderKeepReadOnly(const core::KeepConfig& a_keep, const CategoryTree& a_tree, const KeepNames& a_names)
		{
			auto list = [&](const char* a_title, const std::vector<std::string>& a_items) {
				Text(a_title);
				ImGui::Indent();
				if (a_items.empty()) {
					Disabled(text::kNone);
				}
				for (const auto& e : a_items) {
					ImGui::Bullet();
					KeepEntryText(e, a_tree, a_names);
				}
				ImGui::Unindent();
			};
			list(text::kKeepAlways, a_keep.always);
			list(text::kKeepNever, a_keep.never);
			Text(text::kKeepQuotas);
			Tooltip(text::kKeepQuotasTip);
			ImGui::Indent();
			if (a_keep.quotas.empty()) {
				Disabled(text::kNone);
			}
			for (const auto& q : a_keep.quotas) {
				ImGui::Bullet();
				Text(std::format(text::kQuotaLine, q.quota, q.category ? DescribeKeepEntry(*q.category, a_tree, a_names) : std::string(text::kNoCategory), q.restock ? text::kRestockMark : ""));
			}
			ImGui::Unindent();
		}

		// Always / never lists and quotas; shared by the per-home tab and the defaults on the Settings page.
		void KeepRulesBody(core::KeepConfig& a_draft, bool& a_dirty, std::array<char, 128>& a_addAlways, std::array<char, 128>& a_addNever, const CategoryTree& a_tree,
			const std::vector<InventoryEntry>& a_items, const KeepNames& a_names)
		{
			ImGui::SeparatorText(text::kKeepAlways);
			KeepListEditor("always", a_draft.always, a_addAlways, a_tree, a_items, a_names, a_dirty);
			ImGui::SeparatorText(text::kKeepNever);
			KeepListEditor("never", a_draft.never, a_addNever, a_tree, a_items, a_names, a_dirty);

			ImGui::SeparatorText(text::kKeepQuotas);
			Tooltip(text::kKeepQuotasTip);
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
							other.push_back(Count(static_cast<std::int64_t>(q.forms->size()), text::kFormCount, text::kFormsCount));
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
						Tooltip(text::kRemoveQuotaTip);
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
			Tooltip(text::kAddQuotaTip);
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

			KeepRulesBody(draft, st.keepDirty, st.addAlways, st.addNever, tree, a_snap.inventory, a_snap.keepNames);

			switch (DraftButtons(st.keepDirty, AnyQuotaWithoutSelector(draft))) {
			case DraftAction::Save:
				Do([cell = a_snap.cellId, keep = draft]() { Controller::Get().SetKeep(cell, keep); });
				st.keepDirty = false;
				break;
			case DraftAction::Revert:
				st.keepLoaded = false;
				g_keepInvalid = nullptr;
				break;
			default:
				break;
			}
			if (st.keepHasBlock) {
				ButtonRow row(true);
				if (RowConfirm(row, text::kKeepUseGlobal, "keep-global", text::kConfirmKeepGlobal)) {
					Do([cell = a_snap.cellId]() { Controller::Get().SetKeep(cell, std::nullopt); });
					st.keepLoaded = false;
				} else if (g_state.confirm != "keep-global") {
					Tooltip(text::kKeepUseGlobalTip);
				}
			}

			ImGui::SeparatorText(text::kKeepGlobalHeader);
			Hint(text::kKeepGlobalWhere);
			RenderKeepReadOnly(a_snap.globalKeep, tree, a_snap.keepNames);
		}

		bool g_onlyInstalled = true;  // All templates' "Only installed homes" (for the session, like the Displays filters)

		// The All templates table's Status cell: why a template is not offered, or what it is to the home the player is in.
		void RenderTemplateStatus(const TemplateInfo& a_template, const HomeSnapshot* a_home)
		{
			const TemplateOption* here = nullptr;
			if (a_home) {
				const auto it = std::ranges::find(a_home->templates, a_template.source, &TemplateOption::source);
				here = it != a_home->templates.end() ? &*it : nullptr;
			}
			if (here && here->hidden) {
				Disabled(text::kStatusRemoved);
				Tooltip(text::kTemplateRemovedTip);
				ImGui::SameLine();
				if (Button(text::kAddBackTemplate, true)) {
					Do([cell = a_home->cellId, source = here->source]() { Controller::Get().SetTemplateHidden(cell, source, false); }, Refresh::Full);
				}
				Tooltip(text::kAddBackTemplateTip);
			} else if (!a_template.missing.empty()) {
				Colored(kRed, std::format(text::kMissing, Join(a_template.missing)));
				Tooltip(text::kMissingTip);
			} else if (!a_template.homeLoaded) {
				Colored(kRed, std::format(text::kMissing, a_template.cellText.substr(0, a_template.cellText.find('|'))));
				Tooltip(text::kMissingTip);
			} else if (!a_template.conflicting.empty()) {
				Colored(kRed, std::format(text::kConflicts, Join(a_template.conflicting)));
				Tooltip(text::kConflictsTip);
			} else if (here && here->applied) {
				Colored(kGreen, text::kStatusInUse);
				Tooltip(text::kStatusInUseTip);
			} else if (here) {
				Colored(kGreen, text::kStatusFits);
				Tooltip(text::kStatusFitsTip);
			} else {
				Text(text::kOk);
				Tooltip(text::kStatusOkTip);
			}
		}

		// All templates (DESIGN §10.1): the catalogue of every loaded template, by default only those for homes that are
		// installed, as a collapsed section at the end of This home's Template tab and setup page. Outside a home it is
		// that page's content and starts open. `a_home`: the home the player is in (its Status column), or null.
		void RenderAllTemplates(const HomeSnapshot* a_home, bool a_open)
		{
			const bool open = ImGui::CollapsingHeader(text::kAllTemplates, a_open ? ImGui::ImGuiTreeNodeFlags_DefaultOpen : 0);
			Tooltip(text::kAllTemplatesTip);
			if (!open) {
				return;
			}
			const auto global = PanelModel::Get().Global();
			if (!global) {
				Disabled(text::kLoading);
				return;
			}
			if (global->templates.empty()) {
				Disabled(text::kTemplatesNone);
				return;
			}
			ImGui::Checkbox(text::kOnlyInstalled, &g_onlyInstalled);
			Tooltip(text::kOnlyInstalledTip);
			const auto shown = [](const TemplateInfo& a_template) { return !g_onlyInstalled || a_template.Installed(); };
			if (std::ranges::none_of(global->templates, shown)) {
				Disabled(text::kTemplatesNoneInstalled);
				return;
			}
			if (!ImGui::BeginTable("##templates", 6, kTableFlags)) {
				return;
			}
			ImGui::TableSetupColumn(text::kColTemplate, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.24f);
			ImGui::TableSetupColumn(text::kColAuthor, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.12f);
			ImGui::TableSetupColumn(text::kColHome, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.18f);
			ImGui::TableSetupColumn(text::kColRequires, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.20f);
			ImGui::TableSetupColumn(text::kColStatus, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.16f);
			ImGui::TableSetupColumn(text::kColIssues, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.10f);
			ImGui::TableHeadersRow();
			for (const auto& t : global->templates) {
				if (!shown(t)) {
					continue;
				}
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
					Text(t.cellName.empty() ? t.cellText.substr(0, t.cellText.find('|')) : t.cellName);  // the plugin when the home is not loaded
				}
				Tooltip(t.cellText);
				ImGui::TableNextColumn();
				if (t.required.empty()) {
					Disabled(text::kNone);
				} else {
					Wrapped(Join(t.required));
				}
				ImGui::TableNextColumn();
				RenderTemplateStatus(t, a_home);
				ImGui::TableNextColumn();
				if (t.diagnostics.empty()) {
					Disabled(text::kNone);
				} else {
					Colored(kYellow, Count(static_cast<std::int64_t>(t.diagnostics.size()), text::kIssue, text::kIssues));
					Tooltip(Join(t.diagnostics, "\n"));
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}


		// --- Template tab: templates for this home, Save as template, All templates ----------------------

		void StartExport(const HomeSnapshot& a_snap, bool a_overwrite)
		{
			Controller::ExportRequest request;
			request.name = FromBuffer(g_state.exportName);
			request.author = FromBuffer(g_state.exportAuthor);
			request.version = FromBuffer(g_state.exportVersion);
			request.overwrite = a_overwrite;
			request.learnedAsForms = g_state.exportLearned;
			PanelModel::Get().BeginExport(a_snap.cellId);
			Do([cell = a_snap.cellId, request]() {
				const auto  result = Controller::Get().ExportTemplate(cell, request);
				ExportState state;
				state.cell = cell;
				state.name = request.name;
				state.ok = result.status == Controller::ExportResult::Status::Ok;
				state.exists = result.status == Controller::ExportResult::Status::Exists;
				state.path = result.path;
				state.message = result.message;
				state.warnings = result.warnings;
				PanelModel::Get().SetExport(std::move(state));
			});
		}

		// The Save as template form (This home > Template), under its section header.
		void RenderSaveForm(const HomeSnapshot& a_snap)
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
			// Enter in any of the three fields saves, like the Save as template button.
			constexpr auto enter = ImGui::ImGuiInputTextFlags_EnterReturnsTrue;
			bool           entered = false;
			ImGui::SetNextItemWidth(kFieldWidth);
			entered |= ImGui::InputText(text::kExportName, st.exportName.data(), st.exportName.size(), enter);
			Tooltip(text::kExportNameTip);
			ImGui::SetNextItemWidth(kFieldWidth);
			entered |= ImGui::InputTextWithHint(text::kExportAuthor, text::kExportAuthorHint, st.exportAuthor.data(), st.exportAuthor.size(), enter);
			Tooltip(text::kExportAuthorTip);
			ImGui::SetNextItemWidth(120.0f);
			entered |= ImGui::InputTextWithHint(text::kExportVersion, text::kExportVersionHint, st.exportVersion.data(), st.exportVersion.size(), enter);
			Tooltip(text::kExportVersionTip);
			ImGui::Checkbox(text::kExportLearned, &st.exportLearned);
			Tooltip(text::kExportLearnedTip);
			Hint(std::format(text::kExportFile, core::TemplateFileStem(FromBuffer(st.exportName))));

			auto state = PanelModel::Get().Export();
			if (state.cell != a_snap.cellId) {
				state = ExportState{};  // saved (or asked about) in another home: nothing to show or answer here
			}
			if (state.pending) {
				Disabled(text::kExporting);
				return;
			}
			if (state.exists && FromBuffer(st.exportName) != state.name) {
				// The name was changed to avoid the clash: the question about the old name is withdrawn.
				PanelModel::Get().SetExport(ExportState{});
				state = ExportState{};
			}
			if (state.exists) {
				// The file is there already: a yellow question with Yes / No, like every ConfirmButton.
				ColoredWrapped(kYellow, state.message);
				if (ImGui::Button(text::kYes)) {
					StartExport(a_snap, true);
				}
				ImGui::SameLine();
				if (ImGui::Button(text::kNo)) {
					PanelModel::Get().SetExport(ExportState{});
				}
				return;
			}
			if (ImGui::Button(text::kExport) || entered) {
				StartExport(a_snap, false);
			}
			Tooltip(text::kExportTip);
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

		// Use a template, save this setup as one, then the catalogue of every template (collapsed).
		void RenderTemplateTab(const HomeSnapshot& a_snap)
		{
			Hint(text::kTemplateTabIntro);
			RenderTemplateList(a_snap, text::kSetupTemplates);
			ImGui::SeparatorText(text::kSaveTemplateHeader);
			Tooltip(text::kSaveTemplateTip);
			RenderSaveForm(a_snap);
			RenderAllTemplates(&a_snap, false);
		}

		// --- the stash panel (window and "This home" page) ------------------------------------------

		template <class F>
		void TabItem(const char* a_label, Tab a_tab, F&& a_body)
		{
			const int flags = g_state.pendingTab == a_tab ? ImGui::ImGuiTabItemFlags_SetSelected : 0;
			if (ImGui::BeginTabItem(a_label, nullptr, flags)) {
				// Only the tab's body scrolls: the header, the status line and the tabs stay in view.
				if (ImGui::BeginChild("##tabbody")) {
					a_body();
				}
				ImGui::EndChild();
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
			// What the last action did comes first, under the header: long pages would push it out of view.
			if (!snap->inHome) {
				// No home to use a template in or save from, then every template (open: it is the page's content).
				RenderStatus();
				Wrapped(std::format(text::kNotInHome, snap->notHomeReason));
				RenderAddArea(*snap);
				ImGui::SeparatorText(text::kSetupTemplates);
				Disabled(text::kTemplatesNoHome);
				RenderAllTemplates(nullptr, true);
				return;
			}
			RenderHeader(*snap);
			RenderStatus();
			if (!snap->enabled) {
				RenderSetup(*snap);
				if (!snap->layoutBroken) {
					RenderAllTemplates(snap.get(), false);
				}
				return;
			}
			if (ImGui::BeginTabBar("##DovaStashTabs")) {
				TabItem(text::kTabActions, Tab::Actions, [&]() { RenderActions(*snap); });
				TabItem(text::kTabContainers, Tab::Containers, [&]() { RenderContainers(*snap); });
				TabItem(text::kTabLearned, Tab::Learned, [&]() { RenderLearned(*snap); });
				TabItem(text::kTabKeep, Tab::Keep, [&]() { RenderKeep(*snap); });
				TabItem(text::kTabTemplate, Tab::Template, [&]() { RenderTemplateTab(*snap); });
				ImGui::EndTabBar();
			}
			g_state.pendingTab = Tab::None;
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

		void Help(std::string_view a_text, std::string_view a_iniKey);

		// Press-to-bind hotkey (UI/Panel/KeyCapture.h): a button showing the key's name; clicked, it turns into
		// a yellow "Press a key..." and binds the next key or button pressed. `a_allowOff`: a Clear button turns
		// the hotkey off (code -1). Laid out like a combo (field, then label). True when a_code changed.
		bool KeyBind(const char* a_label, std::int32_t& a_code, key_capture::Target a_target, bool a_allowOff, std::string_view a_tip, std::string_view a_iniKey)
		{
			ImGui::PushID(a_label);
			bool changed = false;
			if (const auto code = key_capture::TakeResult(a_target)) {
				changed = *code != a_code;
				a_code = *code;
			}
			const bool waiting = key_capture::Waiting() == a_target;
			if (waiting) {
				key_capture::KeepAlive();
			}
			const bool showClear = a_allowOff && !waiting && a_code != Settings::kNoKey;
			const auto* style = ImGui::GetStyle();
			float       width = ImGui::CalcItemWidth();
			if (showClear) {  // keep the label lined up with the other settings' labels
				width -= ImGui::CalcTextSize(text::kSetKeyClear).x + style->FramePadding.x * 2.0f + style->ItemSpacing.x;
			}
			if (waiting) {
				const auto label = std::format("{}###bind", a_target == key_capture::Target::Gamepad ? text::kSetKeyWaitButton : text::kSetKeyWaitKey);
				ImGui::PushStyleColor(ImGui::ImGuiCol_Text, kYellow);
				const bool cancel = ImGui::Button(label.c_str(), ImGui::ImVec2{ width, 0.0f });
				ImGui::PopStyleColor();
				Tooltip(text::kSetKeyWaitTip);
				if (cancel) {
					key_capture::Cancel();
				}
			} else {
				const auto name = a_code == Settings::kNoKey ? std::string(text::kSetKeyOff) : input::KeyName(a_code);
				if (ImGui::Button(std::format("{}###bind", name).c_str(), ImGui::ImVec2{ width, 0.0f })) {
					key_capture::Begin(a_target);
				}
				Help(a_tip, a_iniKey);
			}
			if (showClear) {
				ImGui::SameLine();
				if (ImGui::Button(text::kSetKeyClear)) {
					a_code = Settings::kNoKey;
					changed = true;
				}
				Help(text::kSetKeyClearTip, a_iniKey);
			}
			ImGui::SameLine(0.0f, style->ItemInnerSpacing.x);
			ImGui::TextUnformatted(a_label);
			Help(a_tip, a_iniKey);  // the label belongs to the setting, as on the other rows
			if (const auto clash = a_code == Settings::kNoKey ? std::string{} : key_capture::Clash(a_target, a_code); !clash.empty()) {
				Colored(kYellow, std::format(text::kSetKeyClash, input::KeyName(a_code), clash));
			}
			ImGui::PopID();
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
			KeepRulesBody(st.draft, st.dirty, st.addAlways, st.addNever, *home->categories, home->inventory, home->keepNames);
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
				g_keepInvalid = nullptr;
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
				const auto tip = std::format(text::kIniKeyTip, a_text, a_iniKey);
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
			PanelModel::Get().MarkRendered(Page::Settings);  // a setting that could not be saved says so on this page
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
			StatusLine(PanelModel::Get().Status(Page::Settings), PanelModel::Get().StatusIsError());  // a setting that could not be saved (red)
			// Only what is below the status line scrolls, so a failure at the bottom of the page stays in view.
			ImGui::BeginChild("##settingsbody");

			ImGui::SeparatorText(text::kSetHotkeyHeader);
			apply |= KeyBind(text::kSetHotkey, v.input.hotkey, key_capture::Target::Keyboard, false, text::kSetHotkeyTip, "[Input] iHotkey");
			apply |= KeyBind(text::kSetGamepad, v.input.gamepadHotkey, key_capture::Target::Gamepad, true, text::kSetGamepadTip, "[Input] iGamepadHotkey");

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
				// What the chosen value does (the tooltip stays two sentences).
				const std::array<const char*, 4> kSupplyHint{ text::kSetCraftAutoHint, text::kSetCraftOffHint, text::kSetCraftMoveHint, text::kSetCraftStorageHint };
				Hint(kSupplyHint[static_cast<std::size_t>(std::clamp(v.sorting.craftingSupply, 0, 3))]);
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
			ImGui::SliderFloat(text::kSetIconSize, &v.overlay.iconSize, 8.0f, 128.0f, text::kSetIconSizeFormat);
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

			ImGui::SeparatorText(text::kKeepGlobalHeader);
			RenderGlobalKeep(*global);

			// Last, as the page's order puts what resets things: the settings above (the keep rules have their own
			// Save and Revert).
			ImGui::SeparatorText(text::kSetResetHeader);
			if (widgets::ConfirmButton(g_settings.confirm, text::kSetReset, "settings-reset", text::kSetConfirmReset, false, text::kSetResetTip)) {
				auto defaults = Settings::Values{};
				defaults.input = v.input;  // never lose the player's hotkey to a reset
				v = defaults;
				apply = true;
			}
			ImGui::EndChild();

			if (apply) {
				ApplySettings(v);
			}
		}

		// One line per kind of data file: "Categories: No problems.", or the counts with every problem under it.
		void DiagnosticsLines(std::string_view a_title, std::size_t a_errors, const std::vector<std::string>& a_lines, const std::vector<bool>& a_isError)
		{
			if (a_lines.empty()) {
				Text(std::format(text::kDiagLine, a_title, ""));
				ImGui::SameLine();
				Colored(kGreen, text::kDiagNone);
				return;
			}
			Text(std::format(text::kDiagLine, a_title, std::format(text::kDiagCounts, Count(static_cast<std::int64_t>(a_errors), text::kDiagError, text::kDiagErrors), Count(static_cast<std::int64_t>(a_lines.size() - a_errors), text::kIssue, text::kIssues))));
			ImGui::Indent();
			for (std::size_t i = 0; i < a_lines.size(); ++i) {
				ColoredWrapped(a_isError[i] ? kRed : kYellow, a_lines[i]);
			}
			ImGui::Unindent();
		}

		void DiagnosticsSection(std::string_view a_title, const std::vector<core::Diagnostic>& a_items)
		{
			std::vector<std::string> lines;
			std::vector<bool>        isError;
			std::size_t              errors = 0;
			for (const auto& d : a_items) {
				const bool error = d.severity == core::Diagnostic::Severity::Error;
				errors += error ? 1 : 0;
				lines.push_back(d.ToString());
				isError.push_back(error);
			}
			DiagnosticsLines(a_title, errors, lines, isError);
		}

		// Who we are and where the editor is, then what went wrong loading the data files (Diagnostics), then what a
		// bug report needs, collapsed.
		void __stdcall RenderAboutPage()
		{
			PanelModel::Get().MarkRendered(Page::About);  // keeps the current home's setup warnings fresh
			const auto global = PanelModel::Get().Global();
			if (!global) {
				Disabled(text::kLoading);
				return;
			}
			Colored(kAccent, std::format(text::kAboutTitle, global->version));
			Hint(text::kAboutText);
			ImGui::Spacing();
			if (const auto pad = global->settings.input.gamepadHotkey; pad != Settings::kNoKey) {
				Text(std::format(text::kAboutHotkeyPad, global->hotkeyName, input::KeyName(pad)));
			} else {
				Text(std::format(text::kAboutHotkey, global->hotkeyName));
			}
			const auto& editor = EditorInstallPath();
			Wrapped(editor.empty() ? std::string(text::kAboutEditorMissing) : std::format(text::kAboutEditor, editor));
			Disabled(text::kAboutLicense);

			ImGui::SeparatorText(text::kDiagHeader);
			Tooltip(text::kDiagIntro);
			DiagnosticsSection(text::kDiagCategories, global->categoryDiagnostics);
			DiagnosticsSection(text::kDiagTemplates, global->templateDiagnostics);
			DiagnosticsSection(text::kDiagSettings, global->settingsDiagnostics);
			DiagnosticsSection(text::kDiagSelfSorting, global->selfSortingDiagnostics);
			if (const auto snap = PanelModel::Get().Home(); snap && snap->inHome && snap->enabled) {
				DiagnosticsLines(std::format(text::kDiagHome, snap->homeName), 0, snap->layoutWarnings, std::vector<bool>(snap->layoutWarnings.size(), false));
			}

			ImGui::Spacing();
			const bool open = ImGui::CollapsingHeader(text::kAboutBugsHeader);
			Tooltip(text::kAboutBugsTip);
			if (open) {
				const auto available = [](bool a_yes) { return std::string(a_yes ? text::kAboutAvailable : text::kAboutNotAvailable); };
				Text(std::format(text::kAboutRuntime, global->runtime));
				Text(std::format(text::kAboutFramework, global->frameworkVersion));
				Text(std::format(text::kAboutQuickLoot, available(global->quickLootReady)));
				Text(std::format(text::kAboutUIExtensions, available(global->uiExtensions)));
				Text(std::format(text::kAboutCategories, global->categoryCount));
				Text(std::format(text::kAboutTemplates, global->templates.size()));
				Wrapped(text::kAboutData);
				Wrapped(text::kAboutLog);
				Wrapped(text::kAboutBackups);
			}
		}

		void __stdcall OnFrameworkEvent(SKSEMenuFramework::Model::EventType a_type)
		{
			if (a_type == SKSEMenuFramework::Model::kOpenMenu) {
				PanelModel::Get().ClearStatus();  // a status from before the menu opened is stale
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

		// Four pages: the current home's first (This home with its templates, Displays with the armor set picker),
		// then Settings and About (with Diagnostics). Page map: the top of UI/Panel/Strings.h.
		SKSEMenuFramework::SetSection(text::kSection);
		SKSEMenuFramework::AddSectionItem(text::kItemHome, RenderHomePage);
		SKSEMenuFramework::AddSectionItem(text::kItemDisplays, RenderDisplaysPage);  // UI/Panel/DisplaysPage.cpp
		SKSEMenuFramework::AddSectionItem(text::kItemSettings, RenderSettingsPage);
		SKSEMenuFramework::AddSectionItem(text::kItemAbout, RenderAboutPage);
		static auto* event = SKSEMenuFramework::AddEvent(OnFrameworkEvent, 0.0f);  // lives for the whole session
		(void)event;
		key_capture::Register();  // Settings > Hotkey's press-to-bind
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
