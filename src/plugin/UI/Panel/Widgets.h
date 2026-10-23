#pragma once

// The small ImGui building blocks every Menu Framework page uses, so the pages look and behave the same
// (the conventions are in UI/Panel/Strings.h). RENDER THREAD ONLY. Include after <SKSEMenuFramework.h>
// (it needs ImGuiMCP; that header drags in <windows.h>, see PanelRender.h).

#include "UI/Panel/Strings.h"

namespace dova::ui::panel::widgets
{
	namespace ImGui = ImGuiMCP;

	// The only colours of our own; everything else is the player's Menu Framework theme. Red: an error or
	// something that cannot be used. Yellow: a warning, something unsaved, a question waiting for an
	// answer. Green: fine / in use. Accent: the page's subject (the home's name, the About title).
	inline constexpr ImGui::ImVec4 kRed{ 1.0f, 0.45f, 0.45f, 1.0f };
	inline constexpr ImGui::ImVec4 kYellow{ 1.0f, 0.82f, 0.35f, 1.0f };
	inline constexpr ImGui::ImVec4 kGreen{ 0.5f, 0.9f, 0.5f, 1.0f };
	inline constexpr ImGui::ImVec4 kAccent{ 0.55f, 0.8f, 1.0f, 1.0f };

	inline void Text(std::string_view a_text)
	{
		ImGui::TextUnformatted(a_text.data(), a_text.data() + a_text.size());
	}

	inline void Wrapped(const std::string& a_text)
	{
		ImGui::TextWrapped("%s", a_text.c_str());
	}

	inline void Colored(const ImGui::ImVec4& a_color, const std::string& a_text)
	{
		ImGui::TextColored(a_color, "%s", a_text.c_str());
	}

	inline void ColoredWrapped(const ImGui::ImVec4& a_color, const std::string& a_text)
	{
		ImGui::PushStyleColor(ImGui::ImGuiCol_Text, a_color);
		ImGui::TextWrapped("%s", a_text.c_str());
		ImGui::PopStyleColor();
	}

	// A short state or value that is not the main content: "-", "Loading...", "Nothing learned yet.".
	inline void Disabled(const std::string& a_text)
	{
		ImGui::TextDisabled("%s", a_text.c_str());
	}

	// Help text (a page's or tab's intro, a note under a control): wrapped, in the theme's dimmed colour.
	inline void Hint(const std::string& a_text)
	{
		const auto* dim = ImGui::GetStyleColorVec4(ImGui::ImGuiCol_TextDisabled);
		ImGui::PushStyleColor(ImGui::ImGuiCol_Text, dim ? *dim : ImGui::ImVec4{ 0.62f, 0.62f, 0.62f, 1.0f });
		ImGui::TextWrapped("%s", a_text.c_str());
		ImGui::PopStyleColor();
	}

	// Tooltip for the item just drawn, also while it is disabled (it then says why, or what it would do).
	inline void Tooltip(std::string_view a_text)
	{
		if (!a_text.empty() && ImGui::IsItemHovered(ImGui::ImGuiHoveredFlags_AllowWhenDisabled)) {
			const std::string text(a_text);
			ImGui::SetTooltip("%s", text.c_str());
		}
	}

	inline bool Button(const std::string& a_label, bool a_small = false)
	{
		return a_small ? ImGui::SmallButton(a_label.c_str()) : ImGui::Button(a_label.c_str());
	}

	// Set while a ConfirmButton shows its question instead of its button (ButtonRow then starts a new line).
	inline bool g_questionShown = false;

	// Inline confirmation for an action that is hard to undo. The button (its label ends in "...") turns into
	// the yellow question with Yes / No under it. True once, on Yes. `a_pending`: the page's key of the
	// question being shown (one at a time per page).
	inline bool ConfirmButton(std::string& a_pending, const std::string& a_label, const std::string& a_key, const std::string& a_question, bool a_small = false)
	{
		ImGui::PushID(a_key.c_str());
		bool confirmed = false;
		g_questionShown = a_pending == a_key;
		if (!g_questionShown) {
			if (Button(a_label, a_small)) {
				a_pending = a_key;
			}
		} else {
			ColoredWrapped(kYellow, a_question);
			if (ImGui::Button(text::kYes)) {
				confirmed = true;
				a_pending.clear();
			}
			ImGui::SameLine();
			if (ImGui::Button(text::kNo)) {
				a_pending.clear();
			}
		}
		ImGui::PopID();
		return confirmed;
	}

	// Lays buttons out side by side. A question (ConfirmButton) takes the full width: it starts on a line of its
	// own and the next button goes under it. `a_continueLine`: the row continues a line already started (text
	// before the first button).
	class ButtonRow
	{
	public:
		explicit ButtonRow(bool a_continueLine = false) :
			_first(!a_continueLine)
		{
			g_questionShown = false;
		}

		// Call before each plain button (or other item) of the row.
		void Next() { Place(false); }

		bool Confirm(std::string& a_pending, const std::string& a_label, const std::string& a_key, const std::string& a_question, bool a_small = false)
		{
			Place(a_pending == a_key);
			return ConfirmButton(a_pending, a_label, a_key, a_question, a_small);
		}

	private:
		void Place(bool a_ownLine)
		{
			if (!_first && !a_ownLine && !g_questionShown) {
				ImGui::SameLine();
			}
			_first = false;
			g_questionShown = false;
		}

		bool _first;
	};

	// The page's status line: what the last action did (PanelModel::Status()), at the top of the page under its
	// header, above a separator. Red when it reports a failure. What comes after it (a tab's body, the Displays
	// page's content) is drawn in a child window that scrolls on its own, so the line stays in view of the button
	// pressed.
	inline void StatusLine(const std::string& a_status, bool a_error = false)
	{
		if (!a_status.empty()) {
			if (a_error) {
				ColoredWrapped(kRed, a_status);
			} else {
				Wrapped(a_status);
			}
			ImGui::Separator();
		}
	}

	// Height of a scrolling table: its rows (at most `a_maxRows`) plus the header.
	inline float TableHeight(std::size_t a_rows, std::size_t a_maxRows)
	{
		return ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(std::clamp<std::size_t>(a_rows, 1, a_maxRows) + 2);
	}

	inline constexpr int kTableFlags = ImGui::ImGuiTableFlags_Borders | ImGui::ImGuiTableFlags_RowBg | ImGui::ImGuiTableFlags_Resizable |
	                                   ImGui::ImGuiTableFlags_SizingStretchProp;
	inline constexpr int kScrollTableFlags = kTableFlags | ImGui::ImGuiTableFlags_ScrollY;
}
