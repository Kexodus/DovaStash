#pragma once

// Widgets the workspace needs that Widgets.h does not have yet (docs/EDITOR-UX.md §2). Same conventions as
// Widgets.h (return true when activated, theme tokens only, no raw colours); each is meant to be folded into
// Widgets.h by the theme pass. Built on w:: wherever it can be.

#include <cstdint>
#include <string>

#include <imgui.h>

#include "ui/Widgets.h"

namespace dova::editor::ui::wx
{
	using w::Kind;

	// A line of text in a status colour (ok, warn, error...); `Muted` for neutral.
	void Text(Kind a_kind, const char* a_text);
	void TextWrapped(Kind a_kind, const char* a_text);

	// A small muted label above an input ("Label", "Note").
	void FieldLabel(const char* a_text);

	// Confidence of a suggestion as a status: green (the name says so), yellow (a station or items nearby),
	// orange (a hint).
	[[nodiscard]] Kind ConfidenceKind(float a_confidence);

	// A row of the homes rail: name, a muted summary after it, a status chip at the right and the unsaved dot.
	// The summary falls back to `a_summaryShort` when it does not fit, then ends in "…". `a_selected` (the open
	// home) is drawn in accent, `a_cursor` (keyboard) with an outline. Returns true on a left click; right-click
	// it with ImGui::BeginPopupContextItem() after.
	bool HomeRow(const char* a_id, const char* a_name, const char* a_summary, const char* a_summaryShort, const char* a_chip, Kind a_chipKind, bool a_selected,
		bool a_cursor, bool a_unsaved, bool a_dim = false);

	// A collapsible group heading in a list (a mod): chevron, name, muted note. Toggles `a_open` on click.
	bool GroupHeader(const char* a_id, const char* a_label, const char* a_note, bool& a_open);

	// An accent banner with one action and a close x. Returns 1 when the action is clicked, 2 when closed.
	int Banner(const char* a_id, const char* a_text, const char* a_action);

	// "label n/m" with a thin bar under it, for the status row. `a_total` 0: an indeterminate label.
	void StatusProgress(const char* a_label, std::size_t a_done, std::size_t a_total, float a_width);

	// A row in a popover menu: label left, shortcut right (muted). TODO(theme): use w::MenuRow when available.
	bool MenuItem(const char* a_label, const char* a_shortcut = nullptr, bool a_enabled = true, bool a_checked = false);

	// A shortcut drawn as muted text. TODO(theme): use w::KeyCap when available.
	void KeyHint(const char* a_text);

	// A colour square and a label (legends).
	void Swatch(std::uint32_t a_colour, const char* a_label);

	// A checkbox row for popovers. TODO(theme): use w::Toggle when available.
	bool CheckItem(const char* a_label, bool* a_value, const char* a_tooltip = nullptr);

	// A muted block of mono lines with a copy icon at its right that copies `a_copy`.
	void RefBlock(const char* a_id, const std::string& a_lines, const std::string& a_copy);

	// A child with the theme's padding (BeginChild without borders has none).
	bool BeginPadded(const char* a_id, ImVec2 a_size = ImVec2(0, 0), ImGuiWindowFlags a_flags = ImGuiWindowFlags_None);
	void EndPadded();

	// A horizontal drag handle across the current window at the cursor (the drawer's top edge): dragging up
	// grows `a_size`. Returns true while dragged.
	bool SplitterTop(const char* a_id, float& a_size, float a_min, float a_max);

	// Flow layout for chips: the width a chip takes, and SameLine() when the next item of `a_width` still fits
	// before `a_rightX` (the row's right edge in screen space, taken before the first item).
	[[nodiscard]] float ChipWidth(const char* a_text, bool a_close = false);
	void FlowNext(float a_width, float a_rightX);

	// w::BeginPopover anchored at the button's right edge (the title row's right group: it would leave the window
	// otherwise). `a_label` nullptr: an icon button. End with w::EndPopover(). Fold into w::BeginPopover as an option.
	bool BeginPopoverRight(const char* a_id, const char* a_label, w::Icon a_icon, const char* a_tooltip = nullptr, const char* a_shortcut = nullptr);
	void OpenPopoverRight(const char* a_id);  // opens it on the next frame

	// A centred one-line message with a muted second line, in a fixed area (the viewport while loading).
	void CentredMessage(ImVec2 a_min, ImVec2 a_max, const char* a_text, const char* a_sub = nullptr);
}
