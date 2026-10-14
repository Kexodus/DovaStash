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

		// Added by the theme pass (all already scaled).
		float radius{ 3.0f };     // corners
		float iconSize{ 16.0f };  // DrawIcon's default size
		float chipH{ 18.0f };     // chips and key caps
		float stripW{ 28.0f };    // a collapsed rail
		float stroke{ 1.5f };     // icon strokes
		ImU32 shadow{ IM_COL32(0, 0, 0, 102) };  // the 1 px rim under popups and overlays (40 % black)
	};

	// Installs the style, colours and fonts on the current ImGui context. Call once after CreateContext
	// (Window.cpp), before the backends' Init so the font atlas is built with our fonts.
	void          ApplyTheme(float a_dpiScale);
	const Theme&  Th();

	// Font roles (EDITOR-UX.md §3.2). PushFont(Font) binds the font at its own size (ImGui 1.92's
	// PushFont(font, 0) would keep the current size); pair with PopFont().
	enum class Font
	{
		Title,
		Heading,
		Body,
		Small,
		Mono
	};
	ImFont* FontOf(Font a_font);
	void    PushFont(Font a_font);
	void    PopFont();
	ImU32   WithAlpha(ImU32 a_colour, float a_alpha);  // the colour with its alpha multiplied by `a_alpha` (0..1)

	// ---- Text ---------------------------------------------------------------------------------------
	void Title(const char* a_text);    // Cinzel
	void Heading(const char* a_text);  // Jost 16
	void Muted(const char* a_text);    // body in `muted`
	void MutedWrapped(const char* a_text);
	void Small(const char* a_text);
	void Mono(const char* a_text);     // a form ref or an EditorID
	// Small caps heading, thin line, diamond at the left (EDITOR-UX.md §3.3); `a_tooltip` adds an info mark.
	void SectionHead(const char* a_text, const char* a_tooltip = nullptr);
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
		Emblem,   // the Dova Stash mark
		// Added by the theme pass.
		Door,
		Container,     // a chest
		Display,       // a display case
		Mannequin,
		Rack,          // a weapon rack
		Book,
		Sort,          // arrows up and down
		Expand,        // expand all
		Collapse,      // collapse all
		ExternalLink,
		Folder,
		Refresh,
		Brush,         // paint mode
		Cursor,        // select mode
		Trash,
		Eye,
		More,          // three dots
		Count_
	};
	void DrawIcon(ImDrawList* a_list, Icon a_icon, ImVec2 a_min, float a_size, ImU32 a_colour);
	// A square button with an icon; `a_shortcut` goes in the tooltip ("Layers  L").
	bool IconButton(const char* a_id, Icon a_icon, const char* a_tooltip = nullptr, const char* a_shortcut = nullptr, bool a_active = false);

	// ---- Buttons ------------------------------------------------------------------------------------
	// Flat toolbar button; `a_active` draws the accent underline (modes). Width fits the text.
	bool ToolButton(const char* a_label, bool a_active = false, const char* a_tooltip = nullptr, const char* a_shortcut = nullptr);
	bool PrimaryButton(const char* a_label, bool a_enabled = true, const char* a_disabledTip = nullptr);  // filled accent (Save)
	// A ToolButton with a neutral count badge after the label (the drawer's tabs); `a_count` < 0: no badge.
	bool TabButton(const char* a_label, bool a_active, int a_count, const char* a_tooltip = nullptr, const char* a_shortcut = nullptr);
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

	// ---- Added by the theme pass --------------------------------------------------------------------
	void  Badge(const char* a_text, Kind a_kind = Kind::Accent);  // a tiny filled count pill ("3")
	bool  Toggle(const char* a_label, bool& a_value);              // a small switch, accent when on; true when toggled
	bool  Slider(const char* a_label, float& a_value, float a_min, float a_max, const char* a_format = "%.2f", bool a_log = false,
		const char* a_tooltip = nullptr);  // label and value over a thin track, the item width wide; true while it changes
	// A thin 3 px accent bar after "label done/total" in small muted text (the status row's background work).
	// `a_total` 0: indeterminate. `a_width`: the bar's width (<= 0: 80 px).
	void  Progress(const char* a_label, std::size_t a_done, std::size_t a_total, float a_width = 0.0f);
	void  KeyCap(const char* a_key);  // a small bordered key ("Ctrl+K", "F"), for shortcut hints
	// A row in a popover menu: label, the shortcut right-aligned in muted. Closes the popup when clicked.
	bool  MenuRow(const char* a_label, const char* a_shortcut = nullptr, bool a_enabled = true);
	void  ToolbarDivider();  // a 1 px vertical line between groups in a toolbar or overlay (SameLine first)
	void  Ornament(float a_width = 0.0f);  // the centred "line ◆ line" divider (start panel, empty states)
	// Text with a trailing muted part on one line ("Aldamar", "· 0Druid").
	void  TextWithMuted(const char* a_text, const char* a_muted);
	// Icon at the cursor as an item (one line high), in `a_colour` (0: text colour).
	void  IconInline(Icon a_icon, ImU32 a_colour = 0, const char* a_tooltip = nullptr);
	// An icon and a label in one flat button (ToolButton look); `a_active` as ToolButton.
	bool  IconTextButton(const char* a_label, Icon a_icon, bool a_active = false, const char* a_tooltip = nullptr, const char* a_shortcut = nullptr);
	// A 16 px checkbox / radio button (accent mark on bg2). ImGui's own are as tall as an input (26 px).
	bool  Check(const char* a_label, bool& a_value, const char* a_tooltip = nullptr);
	bool  Radio(const char* a_label, int& a_value, int a_option);  // true when it selected `a_option`
	// A tooltip with the shortcut shown as a key cap, when the last item is hovered.
	void  TipShortcut(const char* a_text, const char* a_shortcut);
	// The widget gallery (ui/WidgetsDemo.cpp): every widget, icon, font and chip kind. DOVASTASH_WIDGETS_DEMO=1.
	void  DrawDemo();

	// ---- Workspace pieces (the rail, the inspector, the drawer) -------------------------------------
	void Coloured(Kind a_kind, const char* a_text);         // a line in a status colour (Neutral: muted)
	void ColouredWrapped(Kind a_kind, const char* a_text);
	void FieldLabel(const char* a_text);                    // a small muted label above an input ("Label", "Note")
	// A suggestion's confidence as a status: green (the name says so), yellow (a station or items nearby), orange (a hint).
	[[nodiscard]] Kind ConfidenceKind(float a_confidence);
	// A row of the homes rail: name, a muted summary after it, a status chip at the right and the unsaved dot.
	// The summary falls back to `a_summaryShort` when it does not fit, then ends in "...". `a_selected` (the open
	// home) is drawn in accent, `a_cursor` (keyboard) with an outline. True on a left click; right-click it with
	// ImGui::BeginPopupContextItem() after.
	bool HomeRow(const char* a_id, const char* a_name, const char* a_summary, const char* a_summaryShort, const char* a_chip, Kind a_chipKind, bool a_selected,
		bool a_cursor, bool a_unsaved, bool a_dim = false);
	// A collapsible group heading in a list (a mod): chevron, name, muted note. Toggles `a_open` on click.
	bool GroupHeader(const char* a_id, const char* a_label, const char* a_note, bool& a_open);
	// An accent banner with one action and a close x: 1 when the action is clicked, 2 when closed.
	int  Banner(const char* a_id, const char* a_text, const char* a_action);
	void Swatch(std::uint32_t a_colour, const char* a_label);  // a colour square and a label (legends)
	// A muted block of mono lines with a copy icon at its right that copies `a_copy`.
	void RefBlock(const char* a_id, const std::string& a_lines, const std::string& a_copy);
	// A child with the theme's padding (BeginChild without borders has none). Always pair with EndPadded.
	bool BeginPadded(const char* a_id, ImVec2 a_size = ImVec2(0, 0), ImGuiWindowFlags a_flags = ImGuiWindowFlags_None);
	void EndPadded();
	// A horizontal drag handle across the window at the cursor (the drawer's top edge): dragging up grows `a_size`.
	bool SplitterTop(const char* a_id, float& a_size, float a_min, float a_max);
	// Chip flow: the width a chip takes, and SameLine() when the next item of `a_width` still fits before
	// `a_rightX` (the row's right edge in screen space, taken before the first item).
	[[nodiscard]] float ChipWidth(const char* a_text, bool a_close = false);
	void FlowNext(float a_width, float a_rightX);
	// A centred heading with a muted second line in a fixed area (the viewport while loading).
	void CentredMessage(ImVec2 a_min, ImVec2 a_max, const char* a_text, const char* a_sub = nullptr);
}
