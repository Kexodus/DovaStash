#pragma once

// The editor's own widgets and theme (docs/EDITOR-UX.md §3): every panel draws with these instead of raw
// ImGui styling, so the look lives in one place. Widgets.cpp implements them; Theme.cpp owns the tokens,
// the fonts and the ImGui style. The functions follow ImGui's conventions (return true when activated,
// Begin*/End* pairs where End must be called only when Begin returned true).

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <imgui.h>

namespace dova::editor::ui::w
{
	// ---- Theme tokens (EDITOR-UX.md §3.1) ----------------------------------------------------------
	struct Theme
	{
		ImU32 bg0, bg1, bg2, bg3, line;
		ImU32 text, muted, accent, accentDim, select;
		ImU32 ok, warn, error, info;

		ImFont* title{ nullptr };    // Cinzel 22
		ImFont* heading{ nullptr };  // Jost 16
		ImFont* body{ nullptr };     // Jost 15 (the default font)
		ImFont* smallFont{ nullptr };  // Jost 13 ("small" is a Windows macro)
		ImFont* mono{ nullptr };     // Cascadia Mono / Consolas 13

		float scale{ 1.0f };  // DPI scale; multiply every pixel size in the spec by it
		float rowH{ 24.0f }, inputH{ 26.0f }, toolbarH{ 30.0f }, titleH{ 38.0f }, statusH{ 24.0f };  // already scaled
		float pad{ 8.0f }, gap{ 6.0f };                                                           // already scaled
	};

	// Installs the style, colours and fonts on the current ImGui context. Call once after CreateContext
	// (Window.cpp), before the backends' Init so the font atlas is built with our fonts.
	void          ApplyTheme(float a_dpiScale);
	const Theme&  Th();

	// ---- Text ---------------------------------------------------------------------------------------
	void Title(const char* a_text);    // Cinzel
	void Heading(const char* a_text);  // Jost 16
	void Muted(const char* a_text);    // body in `muted`
	void MutedWrapped(const char* a_text);
	void Small(const char* a_text);
	void Mono(const char* a_text);     // a form ref or an EditorID
	void SectionHead(const char* a_text);  // small caps heading, thin line, diamond at the left (EDITOR-UX.md §3.3)
	void Tip(const char* a_text);          // tooltip when the last item is hovered (nullptr/empty: nothing)
	void Spacer(float a_px = 0.0f);        // vertical space (default: the theme gap)

	// ---- Chips and status ---------------------------------------------------------------------------
	enum class Kind
	{
		Neutral,
		Ok,
		Warn,
		Error,
		Accent,
		Info
	};
	ImU32 Colour(Kind a_kind);
	void  Chip(const char* a_text, Kind a_kind = Kind::Neutral);           // 18 px pill
	bool  ChipButton(const char* a_text, Kind a_kind = Kind::Neutral, bool a_active = false);
	bool  ChipClose(const char* a_text, Kind a_kind = Kind::Neutral);      // chip with an x; true when the x is clicked
	void  StatusDot(Kind a_kind, const char* a_tooltip = nullptr);         // a 6 px dot on the baseline (unsaved, state)

	// ---- Icons (drawn with the draw list, no icon font) ---------------------------------------------
	enum class Icon
	{
		Menu,
		Search,
		Filter,
		Layers,
		Palette,
		Camera,
		Settings,
		ChevronLeft,
		ChevronRight,
		ChevronDown,
		ChevronUp,
		Check,
		Warning,
		Error,
		Info,
		Undo,
		Redo,
		Save,
		Close,
		Pin,
		Copy,
		Home,
		Cube,     // 3D view
		Plan,     // floor plan
		Draft,    // a quill / a sparkle: suggestions
		Plus,
		Minus,
		Emblem    // the Dova Stash mark
	};
	void DrawIcon(ImDrawList* a_list, Icon a_icon, ImVec2 a_min, float a_size, ImU32 a_colour);
	// A square button with an icon; `a_shortcut` goes in the tooltip ("Layers  L").
	bool IconButton(const char* a_id, Icon a_icon, const char* a_tooltip = nullptr, const char* a_shortcut = nullptr, bool a_active = false);

	// ---- Buttons ------------------------------------------------------------------------------------
	// Flat toolbar button; `a_active` draws the accent underline (modes). Width fits the text.
	bool ToolButton(const char* a_label, bool a_active = false, const char* a_tooltip = nullptr, const char* a_shortcut = nullptr);
	bool PrimaryButton(const char* a_label, bool a_enabled = true, const char* a_disabledTip = nullptr);  // filled accent (Save)
	bool LinkButton(const char* a_label);  // text in accent, underline on hover
	// Segmented control (3D | Plan, the inspector panels). Returns true when `a_value` changed.
	bool Segmented(const char* a_id, int& a_value, std::span<const char* const> a_labels, std::span<const char* const> a_tooltips = {});

	// ---- Inputs -------------------------------------------------------------------------------------
	// Search box with the magnifier icon and a clear x; returns true when the text changed.
	bool SearchBox(const char* a_id, char* a_buf, std::size_t a_size, const char* a_hint, float a_width = -1.0f);
	bool SearchBox(const char* a_id, std::string& a_text, const char* a_hint, float a_width = -1.0f);

	// ---- Popovers (menus anchored to a button) -----------------------------------------------------
	// A button (label and/or icon, with a count badge when `a_badge` > 0) that opens a popover below it.
	// Usage: if (BeginPopover("layers", "Layers", Icon::Layers)) { ...; EndPopover(); }
	bool BeginPopover(const char* a_id, const char* a_label, Icon a_icon = Icon::ChevronDown, int a_badge = 0, const char* a_tooltip = nullptr, const char* a_shortcut = nullptr);
	void EndPopover();
	// Opens the popover `a_id` on the next frame (a shortcut key).
	void OpenPopover(const char* a_id);

	// ---- Layout -------------------------------------------------------------------------------------
	// A side rail (EDITOR-UX.md §2.2, §2.4): a child of `a_width` with a drag handle on its inner edge and a
	// collapse handle. Collapsed it is a 28 px strip showing `a_collapsedText` vertically. `a_width` and
	// `a_open` are the caller's state (persisted). Returns true when the content should be drawn.
	enum class Side
	{
		Left,
		Right,
		Bottom
	};
	bool BeginRail(const char* a_id, Side a_side, float& a_size, bool& a_open, float a_minSize, float a_maxSize, const char* a_collapsedText = nullptr);
	void EndRail();

	// A translucent toolbar floating over the viewport (EDITOR-UX.md §2.3) at `a_pos` relative to the
	// current window's content origin. Items inside are laid out horizontally by the caller.
	bool BeginOverlay(const char* a_id, ImVec2 a_pos, float a_alpha = 0.85f);
	void EndOverlay();

	// The one-line muted hint at the bottom of the viewport.
	void HintLine(const char* a_text, ImVec2 a_viewportMin, ImVec2 a_viewportMax);

	// A full-width row in a list that highlights on hover and in accent when selected; returns true on click.
	// Draw the row's content after it with SameLine() calls, or use `a_label` alone.
	bool Row(const char* a_id, const char* a_label, bool a_selected, const char* a_right = nullptr, Kind a_rightKind = Kind::Neutral);

	// Empty state: a muted heading and a short line, centred in the remaining space.
	void EmptyState(const char* a_heading, const char* a_text);

	// A card on the start panel: title, one line, click anywhere. Width is the caller's.
	bool Card(const char* a_id, const char* a_title, const char* a_text, const char* a_shortcut = nullptr, float a_width = 0.0f);

	// ---- Helpers ------------------------------------------------------------------------------------
	float TextWidth(const char* a_text);
	bool  Shortcut(ImGuiKey a_key, bool a_ctrl = false, bool a_shift = false);  // pressed this frame, no text field focused
	// Begins a child that fills the remaining space, with the rail background and no border.
	bool  BeginPanel(const char* a_id, ImVec2 a_size = ImVec2(0, 0));
	void  EndPanel();
}
