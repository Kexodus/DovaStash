#include "UI/Panel/PanelRender.h"

#include "Input/InputHandler.h"

#include "Controller.h"
#include "Sort/SortEngine.h"
#include "UI/Panel/PanelModel.h"
#include "UI/Panel/Strings.h"

#include <cstring>
#include <limits>

// The vendored header uses std::filesystem without including it and includes <windows.h>
// (WIN32_LEAN_AND_MEAN / NOMINMAX come from CMake). Keep it the last include of this file.
#include <filesystem>

#include <SKSEMenuFramework.h>

// RENDER THREAD ONLY below this line, except Register()/OpenWindow(). Never touch RE:: objects,
// HomeRegistry or Store here: read PanelModel snapshots, and post actions to the main thread.

namespace dova::ui::panel
{
	namespace
	{
		namespace ImGui = ImGuiMCP;

		constexpr ImGui::ImVec4 kRed{ 1.0f, 0.45f, 0.45f, 1.0f };
		constexpr ImGui::ImVec4 kYellow{ 1.0f, 0.82f, 0.35f, 1.0f };
		constexpr ImGui::ImVec4 kGreen{ 0.5f, 0.9f, 0.5f, 1.0f };
		constexpr ImGui::ImVec4 kAccent{ 0.55f, 0.8f, 1.0f, 1.0f };
		constexpr ImGui::ImVec4 kGrey{ 0.62f, 0.62f, 0.62f, 1.0f };

		constexpr float kFill = -std::numeric_limits<float>::min();  // "use the remaining width"

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

			bool                  keepLoaded{ false };
			bool                  keepDirty{ false };
			bool                  keepHasBlock{ false };
			std::string           keepLoadedJson;
			core::KeepConfig      keepDraft;
			std::array<char, 128> addAlways{};
			std::array<char, 128> addNever{};

			bool                 exportInit{ false };
			std::array<char, 96> exportName{};
			std::array<char, 96> exportAuthor{};
			std::array<char, 32> exportVersion{};

			const CategoryTree* choicesFor{ nullptr };
			std::vector<Choice> choices;
		};

		RenderState g_state;

		// --- small helpers ---------------------------------------------------------------------------

		void Text(std::string_view a_text)
		{
			ImGui::TextUnformatted(a_text.data(), a_text.data() + a_text.size());
		}

		void Wrapped(const std::string& a_text)
		{
			ImGui::TextWrapped("%s", a_text.c_str());
		}

		void Colored(const ImGui::ImVec4& a_color, const std::string& a_text)
		{
			ImGui::TextColored(a_color, "%s", a_text.c_str());
		}

		void ColoredWrapped(const ImGui::ImVec4& a_color, const std::string& a_text)
		{
			ImGui::PushStyleColor(ImGui::ImGuiCol_Text, a_color);
			ImGui::TextWrapped("%s", a_text.c_str());
			ImGui::PopStyleColor();
		}

		void Disabled(const std::string& a_text)
		{
			ImGui::TextDisabled("%s", a_text.c_str());
		}

		void Tooltip(const std::string& a_text)
		{
			if (!a_text.empty() && ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", a_text.c_str());
			}
		}

		bool Button(const std::string& a_label)
		{
			return ImGui::Button(a_label.c_str());
		}

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

		// Runs `a_fn` on the main thread (SKSE task queue), then rebuilds the snapshot.
		void Do(std::function<void()> a_fn, Refresh a_refresh = Refresh::Light)
		{
			PanelModel::Get().Post(std::move(a_fn), a_refresh);
		}

		// Inline confirmation: a button that turns into "<question> [Yes] [No]". True once on Yes.
		bool ConfirmButton(const std::string& a_label, const std::string& a_key, const std::string& a_question)
		{
			ImGui::PushID(a_key.c_str());
			bool confirmed = false;
			if (g_state.confirm != a_key) {
				if (Button(a_label)) {
					g_state.confirm = a_key;
				}
			} else {
				ColoredWrapped(kYellow, a_question);
				if (ImGui::Button(text::kYes)) {
					confirmed = true;
					g_state.confirm.clear();
				}
				ImGui::SameLine();
				if (ImGui::Button(text::kNo)) {
					g_state.confirm.clear();
				}
			}
			ImGui::PopID();
			return confirmed;
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
			const auto status = PanelModel::Get().Status();
			if (!status.empty()) {
				ImGui::Separator();
				Disabled(status);
			}
		}

		// --- header / setup -----------------------------------------------------------------------------

		void RenderHeader(const HomeSnapshot& a_snap)
		{
			Colored(kAccent, std::format(text::kHeaderHome, a_snap.homeName));
			ImGui::SameLine();
			Disabled(a_snap.cellText);
			if (a_snap.enabled) {
				Text(std::format(text::kHeaderLayout, a_snap.layoutName, a_snap.layoutVersion, a_snap.layoutAuthor.empty() ? "?"s : a_snap.layoutAuthor));
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

		void RenderTemplateChoices(const HomeSnapshot& a_snap, bool a_confirm)
		{
			const auto cell = a_snap.cellId;
			for (const auto& t : a_snap.templates) {
				ImGui::PushID(t.source.c_str());
				ImGui::AlignTextToFramePadding();
				Text(std::format(text::kTemplateLine, t.name, t.version, t.author.empty() ? "?"s : t.author));
				Tooltip(t.source);
				if (t.best) {
					ImGui::SameLine();
					Colored(kGreen, text::kTemplateBest);
				}
				if (t.applied) {
					ImGui::SameLine();
					Disabled(text::kTemplateApplied);
				}
				ImGui::SameLine();
				const std::string label = t.applied ? text::kReapplyTemplate : text::kUseTemplate;
				const bool        go = a_confirm ? ConfirmButton(label, "tpl:" + t.source, std::format(text::kConfirmTemplate, t.name)) : Button(label);
				if (go) {
					Do([cell, source = t.source]() { Controller::Get().UseTemplate(cell, source); }, Refresh::Full);
				}
				ImGui::PopID();
			}
		}

		void RenderSetup(const HomeSnapshot& a_snap)
		{
			const auto cell = a_snap.cellId;
			if (a_snap.layoutBroken) {
				ColoredWrapped(kRed, text::kSetupBroken);
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
					Colored(kRed, std::format("{}: {}", focus->label, focus->reason));
				}
			} else {
				const auto global = PanelModel::Get().Global();
				Wrapped(std::format(text::kSetupLookAt, global ? global->hotkeyName : "the hotkey"s));
			}
			if (!a_snap.templates.empty()) {
				ImGui::SeparatorText(text::kSetupTemplates);
				RenderTemplateChoices(a_snap, false);
			}
		}

		// --- Actions tab --------------------------------------------------------------------------------

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
			const auto label = std::format(text::kNodeCount, node.name, n);
			bool       open = false;
			if (branch) {
				open = ImGui::TreeNodeEx(label.c_str(), 0);
			} else {
				ImGui::TreeNodeEx(label.c_str(), ImGui::ImGuiTreeNodeFlags_Leaf | ImGui::ImGuiTreeNodeFlags_NoTreePushOnOpen);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton(text::kRetrieve)) {
				Do([cell = a_snap.cellId, pattern = node.id + ".*"]() { Controller::Get().RunOperation(cell, sort::Op::Retrieve, pattern); }, Refresh::Full);
			}
			if (open) {
				for (const auto c : node.children) {
					RenderRetrieveNode(a_snap, c);
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		void RenderActions(const HomeSnapshot& a_snap)
		{
			const auto cell = a_snap.cellId;

			ImGui::SeparatorText(text::kSortHeader);
			if (ImGui::Button(text::kDeposit)) {
				Do([cell]() { Controller::Get().RunOperation(cell, sort::Op::Deposit); }, Refresh::Full);
			}
			Tooltip(text::kDepositTip);
			ImGui::SameLine();
			if (ImGui::Button(text::kSortAll)) {
				Do([cell]() { Controller::Get().RunOperation(cell, sort::Op::SortAll); }, Refresh::Full);
			}
			Tooltip(text::kSortAllTip);
			ImGui::SameLine();
			if (ImGui::Button(text::kRefreshCounts)) {
				PanelModel::Get().RequestRefresh(Refresh::Full);
			}

			ImGui::SeparatorText(text::kRetrieveHeader);
			if (!a_snap.countsValid) {
				Disabled(text::kCounting);
			} else if (a_snap.totalCount <= 0) {
				Disabled(text::kNothingStored);
			} else {
				if (Button(std::format(text::kRetrieveEverything, a_snap.totalCount))) {
					Do([cell]() { Controller::Get().RunOperation(cell, sort::Op::Retrieve, "*"); }, Refresh::Full);
				}
				for (const auto root : a_snap.categories->roots) {
					RenderRetrieveNode(a_snap, root);
				}
			}

			ImGui::SeparatorText(text::kTemplateHeader);
			if (a_snap.templates.empty()) {
				Disabled(text::kNoTemplate);
			} else {
				RenderTemplateChoices(a_snap, true);
			}

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
				if (ImGui::Checkbox(node.name.c_str(), &value)) {
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
				const bool open = ImGui::TreeNodeEx(std::format("{}{}###tree", mark, node.name).c_str(), 0);
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

		void RenderContainerDetails(const HomeSnapshot& a_snap, const ContainerRow& a_row)
		{
			const auto cell = a_snap.cellId;
			const auto ref = a_row.ref;

			if (!g_state.labelFor || *g_state.labelFor != a_row.ref) {
				g_state.labelFor = a_row.ref;
				CopyTo(g_state.label, a_row.label);
			}

			ImGui::SeparatorText(a_row.label.c_str());
			Disabled(a_row.refText);
			if (!a_row.gameName.empty() && a_row.gameName != a_row.label) {
				ImGui::SameLine();
				Disabled("(" + a_row.gameName + ")");
			}
			if (!a_row.available) {
				ColoredWrapped(kRed, std::format(text::kUnavailable, a_row.reason));
			}
			if (!a_row.warning.empty()) {
				ColoredWrapped(kYellow, a_row.warning);
			}
			if (a_row.master) {
				Wrapped(text::kMasterInfo);
				return;
			}

			ImGui::AlignTextToFramePadding();
			Text(text::kLabel);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(260.0f);
			ImGui::InputText("##label", g_state.label.data(), g_state.label.size());
			ImGui::SameLine();
			if (ImGui::Button(text::kRename)) {
				Do([cell, ref, label = FromBuffer(g_state.label)]() { Controller::Get().SetLabel(cell, ref, label); });
			}

			if (ImGui::Button(a_row.ignored ? text::kStopIgnoring : text::kIgnore)) {
				Do([cell, ref, ignored = !a_row.ignored]() { Controller::Get().SetIgnored(cell, ref, ignored); }, Refresh::Full);
			}
			if (!a_row.patterns.empty()) {
				ImGui::SameLine();
				if (ImGui::Button(text::kClearCategories)) {
					Do([cell, ref]() { Controller::Get().ClearCategories(cell, ref); });
				}
			}
			if (a_row.available) {
				ImGui::SameLine();
				if (ConfirmButton(text::kMakeMaster, "master:" + a_row.refText, std::format(text::kConfirmMakeMaster, a_row.label))) {
					Do([cell, ref]() { Controller::Get().MakeMaster(cell, ref); }, Refresh::Full);
				}
			}
			if (a_row.learnedCount > 0) {
				if (ConfirmButton(std::format(text::kForgetLearned, a_row.learnedCount), "forget:" + a_row.refText,
						std::format(text::kConfirmForget, a_row.learnedCount, a_row.label))) {
					Do([cell, ref]() { Controller::Get().ForgetLearned(cell, ref); });
				}
			}
			if (a_row.explicitForms > 0) {
				Disabled(std::format(text::kExplicitForms, a_row.explicitForms));
			}

			ImGui::SeparatorText(text::kCategoriesHeader);
			if (a_row.ignored) {
				ColoredWrapped(kGrey, text::kIgnoredInfo);
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
			const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
			const float height = lineHeight * static_cast<float>(std::min<std::size_t>(rows.size(), 8) + 2);
			const int   flags = ImGui::ImGuiTableFlags_Borders | ImGui::ImGuiTableFlags_RowBg | ImGui::ImGuiTableFlags_Resizable |
			                  ImGui::ImGuiTableFlags_ScrollY | ImGui::ImGuiTableFlags_SizingStretchProp;
			if (ImGui::BeginTable("##containers", 4, flags, ImGui::ImVec2{ 0.0f, height })) {
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
					const auto label = std::format("{}{}##{}", row.master ? text::kMasterTag : "", row.label, row.refText);
					if (ImGui::Selectable(label.c_str(), selected, ImGui::ImGuiSelectableFlags_SpanAllColumns)) {
						Select(row);
					}
					Tooltip(row.refText);

					ImGui::TableNextColumn();
					if (row.master) {
						Disabled(text::kHomeless);
					} else if (row.ignored) {
						Disabled(std::format("{} {}", text::kIgnoredTag, row.patternsText));
					} else {
						Text(row.patternsText.empty() ? std::string(text::kNone) : row.patternsText);
					}

					ImGui::TableNextColumn();
					Text(row.master ? std::string(text::kNone) : std::to_string(row.learnedCount));

					ImGui::TableNextColumn();
					if (!row.available) {
						Colored(kRed, std::format(text::kUnavailable, row.reason));
					} else if (row.ignored) {
						Disabled(text::kIgnoredTag);
					} else if (!row.managed) {
						Disabled(text::kNotManaged);
					} else {
						Colored(kGreen, text::kAvailable);
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
				ColoredWrapped(kGrey, text::kLearnedEmpty);
				return;
			}
			const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
			const float height = lineHeight * static_cast<float>(std::min<std::size_t>(a_snap.learned.size(), 16) + 2);
			const int   flags = ImGui::ImGuiTableFlags_Borders | ImGui::ImGuiTableFlags_RowBg | ImGui::ImGuiTableFlags_Resizable |
			                  ImGui::ImGuiTableFlags_ScrollY | ImGui::ImGuiTableFlags_SizingStretchProp;
			if (!ImGui::BeginTable("##learned", 3, flags, ImGui::ImVec2{ 0.0f, height })) {
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
				if (row.containerManaged) {
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
			ImGui::SetNextItemWidth(320.0f);
			ImGui::InputTextWithHint("##add", text::kKeepAddHint, a_buffer.data(), a_buffer.size());
			ImGui::SameLine();
			if (ImGui::Button(text::kAdd)) {
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
				Text(std::format("{} x {}", q.quota, q.category ? DescribeKeepEntry(*q.category, a_tree) : std::string(text::kNoCategory)));
			}
			ImGui::Unindent();
		}

		// Always / never lists and quotas; shared by the per-home tab and the all-homes defaults.
		void KeepRulesBody(core::KeepConfig& a_draft, bool& a_dirty, std::array<char, 128>& a_addAlways, std::array<char, 128>& a_addNever, const CategoryTree& a_tree)
		{
			ImGui::SeparatorText(text::kKeepAlways);
			KeepListEditor("always", a_draft.always, a_addAlways, a_tree, a_dirty);
			ImGui::SeparatorText(text::kKeepNever);
			KeepListEditor("never", a_draft.never, a_addNever, a_tree, a_dirty);

			ImGui::SeparatorText(text::kKeepQuotas);
			std::optional<std::size_t> remove;
			if (!a_draft.quotas.empty()) {
				const int flags = ImGui::ImGuiTableFlags_Borders | ImGui::ImGuiTableFlags_RowBg | ImGui::ImGuiTableFlags_SizingStretchProp;
				if (ImGui::BeginTable("##quotas", 4, flags)) {
					ImGui::TableSetupColumn(text::kColCategories, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.45f);
					ImGui::TableSetupColumn(text::kColQuota, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.2f);
					ImGui::TableSetupColumn(text::kColOther, ImGui::ImGuiTableColumnFlags_WidthStretch, 0.25f);
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
						std::vector<std::string> other;
						if (q.keywordsAny && !q.keywordsAny->empty()) {
							other.push_back(std::format(text::kKeywordsAny, Join(*q.keywordsAny)));
						}
						if (q.forms && !q.forms->empty()) {
							other.push_back(std::format(text::kFormsCount, q.forms->size()));
						}
						Text(other.empty() ? std::string(text::kNone) : Join(other, "; "));
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

			Wrapped(text::kKeepIntro);
			if (!st.keepHasBlock) {
				Disabled(text::kKeepNoBlock);
			}
			bool inherit = draft.Inherits();
			if (ImGui::Checkbox(text::kKeepInherit, &inherit)) {
				draft.inherit = inherit;
				st.keepDirty = true;
			}

			KeepRulesBody(draft, st.keepDirty, st.addAlways, st.addNever, tree);

			ImGui::Separator();
			const bool invalid = std::ranges::any_of(draft.quotas, [](const core::QuotaRule& q) { return !QuotaHasSelector(q); });
			if (invalid) {
				ColoredWrapped(kYellow, text::kQuotaNeedsCategory);
			}
			ImGui::BeginDisabled(!st.keepDirty || invalid);
			if (ImGui::Button(text::kSave)) {
				Do([cell = a_snap.cellId, keep = draft]() { Controller::Get().SetKeep(cell, keep); });
				st.keepDirty = false;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(!st.keepDirty);
			if (ImGui::Button(text::kRevert)) {
				st.keepLoaded = false;
			}
			ImGui::EndDisabled();
			if (st.keepDirty) {
				ImGui::SameLine();
				Colored(kYellow, text::kUnsaved);
			}
			if (st.keepHasBlock && ConfirmButton(text::kKeepUseGlobal, "keep-global", text::kConfirmKeepGlobal)) {
				Do([cell = a_snap.cellId]() { Controller::Get().SetKeep(cell, std::nullopt); });
				st.keepLoaded = false;
			}

			ImGui::SeparatorText(text::kKeepGlobalHeader);
			RenderKeepReadOnly(a_snap.globalKeep, tree);
			Disabled(text::kKeepGlobalWhere);
		}

		// --- Template (export) tab ----------------------------------------------------------------------

		void StartExport(const HomeSnapshot& a_snap, bool a_overwrite)
		{
			Controller::ExportRequest request;
			request.name = FromBuffer(g_state.exportName);
			request.author = FromBuffer(g_state.exportAuthor);
			request.version = FromBuffer(g_state.exportVersion);
			request.overwrite = a_overwrite;
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
			if (!st.exportInit) {
				CopyTo(st.exportName, a_snap.layoutName.empty() ? a_snap.homeName : a_snap.layoutName);
				CopyTo(st.exportAuthor, a_snap.layoutAuthor.empty() ? "Player"sv : std::string_view(a_snap.layoutAuthor));
				CopyTo(st.exportVersion, a_snap.layoutVersion.empty() ? "1.0.0"sv : std::string_view(a_snap.layoutVersion));
				st.exportInit = true;
			}
			Wrapped(text::kExportIntro);
			ImGui::Spacing();
			ImGui::SetNextItemWidth(320.0f);
			ImGui::InputText(text::kExportName, st.exportName.data(), st.exportName.size());
			ImGui::SetNextItemWidth(320.0f);
			ImGui::InputText(text::kExportAuthor, st.exportAuthor.data(), st.exportAuthor.size());
			ImGui::SetNextItemWidth(120.0f);
			ImGui::InputText(text::kExportVersion, st.exportVersion.data(), st.exportVersion.size());
			ColoredWrapped(kGrey, std::format(text::kExportFile, FromBuffer(st.exportName)));

			const auto state = PanelModel::Get().Export();
			if (state.pending) {
				Disabled(text::kExporting);
				return;
			}
			if (state.exists) {
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
				Disabled("(Esc)");
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
		};
		SettingsDraft g_settings;

		void ApplySettings(const Settings::Values& a_values)
		{
			Do([a_values]() {
				Controller::Get().ApplySettings(a_values);
				PanelModel::Get().BuildGlobal();
			});
		}

		// Combo over `a_choices` (+ an optional "Disabled" entry for code -1). True when a_code changed.
		bool KeyCombo(const char* a_label, std::int32_t& a_code, std::span<const input::KeyChoice> a_choices, bool a_allowNone)
		{
			const auto current = a_code == Settings::kNoKey ? "Disabled"s : input::KeyName(a_code);
			bool       changed = false;
			if (ImGui::BeginCombo(a_label, current.c_str())) {
				if (a_allowNone && ImGui::Selectable("Disabled", a_code == Settings::kNoKey)) {
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
			Wrapped(text::kKeepGlobalIntro);
			ImGui::PushID("globalkeep");
			KeepRulesBody(st.draft, st.dirty, st.addAlways, st.addNever, *home->categories);
			const bool invalid = std::ranges::any_of(st.draft.quotas, [](const core::QuotaRule& q) { return !QuotaHasSelector(q); });
			if (invalid) {
				ColoredWrapped(kYellow, text::kQuotaNeedsCategory);
			}
			ImGui::BeginDisabled(!st.dirty || invalid);
			if (ImGui::Button(text::kSave)) {
				Do([keep = st.draft]() {
					Controller::Get().SetGlobalKeep(keep);
					PanelModel::Get().BuildGlobal();
				});
				st.dirty = false;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(!st.dirty);
			if (ImGui::Button(text::kRevert)) {
				st.loaded = false;
			}
			ImGui::EndDisabled();
			if (st.dirty) {
				ImGui::SameLine();
				Colored(kYellow, text::kUnsaved);
			}
			ImGui::PopID();
		}

		// Hover help for the control just drawn: what it does, and the INI key it writes.
		void Help(std::string_view a_text, std::string_view a_iniKey)
		{
			if (ImGui::IsItemHovered()) {
				const auto tip = std::format("{}\n\nDovaStash.ini: {}", a_text, a_iniKey);
				ImGui::SetTooltip("%s", tip.c_str());
			}
		}

		void Unavailable(std::string_view a_why)
		{
			ImGui::Indent();
			Disabled(std::string(a_why));
			ImGui::Unindent();
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

			ColoredWrapped(kGrey, text::kSettingsNote);

			ImGui::SeparatorText("Controls");
			apply |= KeyCombo("Hotkey (keyboard / mouse)", v.input.hotkey, input::KeyboardChoices(), false);
			Help("Opens Dova Stash: at a container, its menu; at nothing, the home's menu.", "[Input] iHotkey");
			apply |= KeyCombo("Hotkey (gamepad)", v.input.gamepadHotkey, input::GamepadChoices(), true);
			Help("Same, on a controller. Every button already has a game action, so this is off by default.", "[Input] iGamepadHotkey");

			ImGui::SeparatorText("Menus");
			static constexpr std::array kModes{ "Auto (UIExtensions lists, else message boxes)", "Message boxes", "SKSE Menu Framework panel", "UIExtensions lists" };
			apply |= IndexCombo("What the hotkey opens", v.ui.mode, kModes);
			Help("Auto uses UIExtensions' lists when it is installed (any skin), else the game's message boxes.", "[UI] iMode");
			ImGui::BeginDisabled(!global->uiExtensions);
			apply |= ImGui::Checkbox("Open the lists in the middle of the screen", &v.ui.centerMenus);
			Help("Moves UIExtensions' lists and the rename box to the middle of the screen, whatever your skin does.", "[UI] bCenterMenus");
			ImGui::EndDisabled();
			if (!global->uiExtensions) {
				Unavailable(global->isVR ? "UIExtensions lists are not used on VR." : "UIExtensions is not installed.");
			}

			ImGui::SeparatorText("Sorting");
			apply |= ImGui::Checkbox("Sort items the moment they go into the master container", &v.sorting.sortOnArrival);
			Help("Anything you put into the master leaves it for its container about half a second later, even with the menu still open.", "[Sorting] bSortOnArrival");
			apply |= ImGui::Checkbox("Sort the master container when it closes", &v.sorting.distributeOnMasterClose);
			Help("Closing the master's menu sorts whatever is left in it.", "[Sorting] bDistributeOnMasterClose");
			apply |= ImGui::Checkbox("Learn where you put things by hand", &v.sorting.learn);
			Help("Put an item into a container yourself and that container becomes its home from then on.", "[Sorting] bLearn");
			apply |= ImGui::Checkbox("Enable a home's template automatically on first visit", &v.templates.autoEnable);
			Help("Homes with a ready-made layout are set up as soon as you walk in, instead of asking.", "[General] bAutoEnableTemplates");

			ImGui::SeparatorText("QuickLoot IE");
			ImGui::BeginDisabled(!global->quickLootReady);
			apply |= ImGui::Checkbox("Use QuickLoot IE", &v.integrations.quickLoot);
			Help("Everything Dova Stash adds to QuickLoot IE. Off = QuickLoot looks exactly as without Dova Stash.", "[Integrations] bQuickLoot");
			ImGui::BeginDisabled(!v.integrations.quickLoot);
			ImGui::Indent();
			apply |= ImGui::Checkbox("Show where each item goes in its list", &v.integrations.quickLootItemTags);
			Help("Items that belong somewhere else read \"Iron Sword  >  Weapon Rack\".", "[Integrations] bQuickLootItemTags");
			apply |= ImGui::Checkbox("Category icons instead of names", &v.integrations.quickLootIcons);
			Help("Bottom-right: one icon per category the container holds (your SkyUI/I4 icons). Off = the names.", "[Integrations] bQuickLootIcons");
			ImGui::Unindent();
			ImGui::EndDisabled();
			ImGui::EndDisabled();
			if (!global->quickLootReady) {
				Unavailable("QuickLoot IE is not installed.");
			}

			ImGui::SeparatorText("World");
			apply |= ImGui::Checkbox("Show a container's role in the activate prompt", &v.integrations.activatePrompt);
			Help("\"Chest (Weapons)\" when you look at a container. Nothing is written to your save.", "[Integrations] bActivatePrompt");
			ImGui::BeginDisabled(global->isVR);
			apply |= ImGui::Checkbox("Show icons above containers", &v.overlay.enabled);
			Help("Floating category icons above the containers around you, in your homes.", "[Overlay] bEnabled");
			ImGui::BeginDisabled(!v.overlay.enabled);
			ImGui::Indent();
			apply |= ImGui::Checkbox("Only containers you can see", &v.overlay.requireLineOfSight);
			Help("No icons through walls.", "[Overlay] bRequireLineOfSight");
			ImGui::SliderFloat("Range (game units)", &v.overlay.radius, 100.0f, 5000.0f, "%.0f");
			apply |= ImGui::IsItemDeactivatedAfterEdit();
			Help("How far away icons are shown (about 70 units per metre).", "[Overlay] fRadius");
			ImGui::SliderInt("Most icons at once", &v.overlay.maxIcons, 1, 32);
			apply |= ImGui::IsItemDeactivatedAfterEdit();
			Help("Only the nearest ones are shown.", "[Overlay] iMaxIcons");
			ImGui::SliderFloat("Icon size", &v.overlay.iconSize, 8.0f, 128.0f, "%.0f");
			apply |= ImGui::IsItemDeactivatedAfterEdit();
			Help("Size when you stand close; icons shrink with distance.", "[Overlay] fIconSize");
			ImGui::Unindent();
			ImGui::EndDisabled();
			ImGui::EndDisabled();
			if (global->isVR) {
				Unavailable("Floating icons are not available on VR.");
			}

			ImGui::SeparatorText("Diagnostics");
			static constexpr std::array kLevels{ "Trace (everything)", "Debug", "Info", "Warnings", "Errors", "Critical", "Off" };
			apply |= IndexCombo("Log detail (DovaStash.log)", v.general.logLevel, kLevels);
			Help("How much goes into Documents/My Games/Skyrim Special Edition/SKSE/DovaStash.log.", "[General] iLogLevel");

			ImGui::Spacing();
			if (ConfirmButton("Reset all settings to defaults", "settings-reset", "Reset every setting on this page to its default? Your hotkey is kept.")) {
				auto defaults = Settings::Values{};
				defaults.input = v.input;  // never lose the player's hotkey to a reset
				v = defaults;
				apply = true;
			}

			ImGui::SeparatorText("Keep rules for every home");
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
			ColoredWrapped(kGrey, text::kTemplatesIntro);
			if (global->templates.empty()) {
				Disabled(text::kTemplatesNone);
				return;
			}
			const int flags = ImGui::ImGuiTableFlags_Borders | ImGui::ImGuiTableFlags_RowBg | ImGui::ImGuiTableFlags_Resizable |
			                  ImGui::ImGuiTableFlags_SizingStretchProp;
			if (!ImGui::BeginTable("##templates", 5, flags)) {
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
				Text(std::format("{} v{}", t.name, t.version));
				Tooltip(std::format("{}\npriority {}", t.source, t.priority));
				ImGui::TableNextColumn();
				Text(t.author.empty() ? std::string(text::kNone) : t.author);
				ImGui::TableNextColumn();
				Text(t.cellName.empty() ? (t.cellText.empty() ? std::string(text::kNone) : t.cellText) : t.cellName);
				Tooltip(t.cellText);
				ImGui::TableNextColumn();
				Text(t.required.empty() ? std::string(text::kNone) : Join(t.required));
				if (t.missing.empty()) {
					Colored(kGreen, text::kOk);
				} else {
					Colored(kRed, std::format(text::kMissing, Join(t.missing)));
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

		void DiagnosticsSection(const char* a_title, const std::vector<core::Diagnostic>& a_items)
		{
			ImGui::SeparatorText(a_title);
			const auto errors = std::ranges::count_if(a_items, [](const core::Diagnostic& d) { return d.severity == core::Diagnostic::Severity::Error; });
			if (a_items.empty()) {
				Colored(kGreen, text::kDiagNone);
				return;
			}
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
			ColoredWrapped(kGrey, text::kDiagIntro);
			DiagnosticsSection(text::kDiagCategories, global->categoryDiagnostics);
			DiagnosticsSection(text::kDiagTemplates, global->templateDiagnostics);
			DiagnosticsSection(text::kDiagSettings, global->settingsDiagnostics);

			const auto snap = PanelModel::Get().Home();
			if (snap && snap->inHome && snap->enabled) {
				ImGui::SeparatorText(std::format("{}: {}", text::kDiagHome, snap->homeName).c_str());
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
			Colored(kAccent, std::format("Dova Stash v{}", global->version));
			Wrapped(text::kAboutText);
			ImGui::Spacing();
			Text(std::format("Runtime: {}", global->runtime));
			Text(std::format("SKSE Menu Framework: v{:.2f}", global->frameworkVersion));
			Text(std::format("Categories: {}   Templates: {}", global->categoryCount, global->templates.size()));
			Text(std::format("Hotkey: {}", global->hotkeyName));
			Text(std::format("QuickLoot IE: {}   UIExtensions: {}   I4 icon rules: {}", global->quickLootReady ? "yes" : "no",
				global->uiExtensions ? "yes" : "no", global->i4Rules));
			ImGui::Spacing();
			ColoredWrapped(kGrey, "Data: Data/SKSE/Plugins/DovaStash/ (Categories, Templates, Layouts). Log: Documents/My Games/Skyrim Special Edition/SKSE/DovaStash.log");
			ColoredWrapped(kGrey, "License: GPL-3.0-or-later. Uses the SKSE Menu Framework 3 API header (LGPL-2.1).");
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

		SKSEMenuFramework::SetSection(text::kSection);
		SKSEMenuFramework::AddSectionItem(text::kItemHome, RenderHomePage);
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
