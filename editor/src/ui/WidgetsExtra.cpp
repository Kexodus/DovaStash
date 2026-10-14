// WidgetsExtra.h: drawn with the theme tokens of Widgets.h (w::Th()) so they follow the theme pass.

#include "ui/WidgetsExtra.h"

#include <algorithm>
#include <cstdio>

#include <imgui_internal.h>

namespace dova::editor::ui::wx
{
	namespace
	{
		ImU32 WithAlpha(ImU32 a_col, int a_alpha)
		{
			return (a_col & 0x00FFFFFF) | (static_cast<ImU32>(a_alpha) << 24);
		}
	}

	void Text(Kind a_kind, const char* a_text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, a_kind == Kind::Neutral ? w::Th().muted : w::Colour(a_kind));
		ImGui::TextUnformatted(a_text);
		ImGui::PopStyleColor();
	}

	void TextWrapped(Kind a_kind, const char* a_text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, a_kind == Kind::Neutral ? w::Th().muted : w::Colour(a_kind));
		ImGui::TextWrapped("%s", a_text);
		ImGui::PopStyleColor();
	}

	void FieldLabel(const char* a_text)
	{
		ImGui::PushFont(w::Th().smallFont);
		ImGui::PushStyleColor(ImGuiCol_Text, w::Th().muted);
		ImGui::TextUnformatted(a_text);
		ImGui::PopStyleColor();
		ImGui::PopFont();
	}

	Kind ConfidenceKind(float a_confidence)
	{
		return a_confidence >= 0.75f ? Kind::Ok : a_confidence >= 0.5f ? Kind::Warn : Kind::Error;
	}

	bool HomeRow(const char* a_id, const char* a_name, const char* a_summary, const char* a_chip, Kind a_chipKind, bool a_selected, bool a_cursor, bool a_unsaved,
		bool a_dim)
	{
		const auto& t = w::Th();
		ImGui::PushID(a_id);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float  wdt = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
		const float  h = t.rowH;
		const bool   clicked = ImGui::InvisibleButton("row", ImVec2(wdt, h));
		const bool   hovered = ImGui::IsItemHovered();
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		const ImVec2 q(p.x + wdt, p.y + h);
		if (a_selected) {
			dl->AddRectFilled(p, q, WithAlpha(t.accent, 0x30), 3.0f * t.scale);
			dl->AddRectFilled(p, ImVec2(p.x + 2.0f * t.scale, q.y), t.accent);
		} else if (hovered) {
			dl->AddRectFilled(p, q, t.bg3, 3.0f * t.scale);
		}
		if (a_cursor) {
			dl->AddRect(p, q, t.accentDim, 3.0f * t.scale);
		}
		// Chip at the right.
		float right = q.x - t.pad * 0.5f;
		if (a_chip && *a_chip) {
			ImGui::PushFont(t.smallFont);
			const ImVec2 sz = ImGui::CalcTextSize(a_chip);
			const float  ch = 16.0f * t.scale;
			const float  cw = sz.x + 12.0f * t.scale;
			const ImVec2 c0(right - cw, p.y + (h - ch) * 0.5f);
			const ImU32  col = w::Colour(a_chipKind);
			dl->AddRectFilled(c0, ImVec2(c0.x + cw, c0.y + ch), t.bg2, ch * 0.5f);
			dl->AddRect(c0, ImVec2(c0.x + cw, c0.y + ch), col, ch * 0.5f);
			dl->AddText(ImVec2(c0.x + 6.0f * t.scale, c0.y + (ch - sz.y) * 0.5f), col, a_chip);
			ImGui::PopFont();
			right = c0.x - t.gap;
		}
		// Name, dot, summary (clipped before the chip).
		dl->PushClipRect(p, ImVec2(std::max(right, p.x + 1.0f), q.y), true);
		const float ty = p.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
		float       x = p.x + t.pad;
		dl->AddText(ImVec2(x, ty), a_dim ? t.muted : t.text, a_name);
		x += ImGui::CalcTextSize(a_name).x;
		if (a_unsaved) {
			const float r = 3.0f * t.scale;
			dl->AddCircleFilled(ImVec2(x + r + 3.0f * t.scale, p.y + h * 0.5f), r, t.warn);
			x += 2.0f * r + 4.0f * t.scale;
		}
		if (a_summary && *a_summary) {
			ImGui::PushFont(t.smallFont);
			const float sy = p.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
			dl->AddText(ImVec2(x + t.gap, sy), t.muted, a_summary);
			ImGui::PopFont();
		}
		dl->PopClipRect();
		ImGui::PopID();
		return clicked;
	}

	bool GroupHeader(const char* a_id, const char* a_label, const char* a_note, bool& a_open)
	{
		const auto& t = w::Th();
		ImGui::PushID(a_id);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float  wdt = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
		const float  h = t.rowH;
		const bool   clicked = ImGui::InvisibleButton("group", ImVec2(wdt, h));
		const bool   hovered = ImGui::IsItemHovered();
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		if (hovered) {
			dl->AddRectFilled(p, ImVec2(p.x + wdt, p.y + h), t.bg2, 3.0f * t.scale);
		}
		const float is = 12.0f * t.scale;
		w::DrawIcon(dl, a_open ? w::Icon::ChevronDown : w::Icon::ChevronRight, ImVec2(p.x + 2.0f * t.scale, p.y + (h - is) * 0.5f), is, t.muted);
		dl->PushClipRect(p, ImVec2(p.x + wdt, p.y + h), true);
		ImGui::PushFont(t.smallFont);
		const float ty = p.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
		dl->AddText(ImVec2(p.x + is + 6.0f * t.scale, ty), t.accent, a_label);
		if (a_note && *a_note) {
			const float x = p.x + is + 6.0f * t.scale + ImGui::CalcTextSize(a_label).x + t.gap;
			dl->AddText(ImVec2(x, ty), t.muted, a_note);
		}
		ImGui::PopFont();
		dl->PopClipRect();
		if (clicked) {
			a_open = !a_open;
		}
		ImGui::PopID();
		return clicked;
	}

	int Banner(const char* a_id, const char* a_text, const char* a_action)
	{
		const auto& t = w::Th();
		int         result = 0;
		ImGui::PushID(a_id);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float  wdt = ImGui::GetContentRegionAvail().x;
		const float  h = t.rowH + 2.0f * t.gap;
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(p, ImVec2(p.x + wdt, p.y + h), WithAlpha(t.accent, 0x28), 3.0f * t.scale);
		dl->AddRect(p, ImVec2(p.x + wdt, p.y + h), t.accentDim, 3.0f * t.scale);
		ImGui::SetCursorScreenPos(ImVec2(p.x + t.pad, p.y + t.gap + (t.rowH - ImGui::GetTextLineHeight()) * 0.5f));
		ImGui::TextUnformatted(a_text);
		ImGui::SameLine();
		ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, p.y + t.gap + (t.rowH - ImGui::GetFrameHeight()) * 0.5f));
		if (w::LinkButton(a_action)) {
			result = 1;
		}
		const float b = 16.0f * t.scale;
		ImGui::SetCursorScreenPos(ImVec2(p.x + wdt - b - t.gap, p.y + (h - b) * 0.5f));
		if (ImGui::InvisibleButton("close", ImVec2(b, b))) {
			result = 2;
		}
		w::DrawIcon(dl, w::Icon::Close, ImGui::GetItemRectMin(), b, ImGui::IsItemHovered() ? t.text : t.muted);
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + t.gap));
		ImGui::Dummy(ImVec2(0, 0));
		ImGui::PopID();
		return result;
	}

	void StatusProgress(const char* a_label, std::size_t a_done, std::size_t a_total, float a_width)
	{
		const auto& t = w::Th();
		char        text[256];
		if (a_total > 0) {
			std::snprintf(text, sizeof(text), "%s %zu/%zu", a_label, a_done, a_total);
		} else {
			std::snprintf(text, sizeof(text), "%s", a_label);
		}
		ImGui::PushFont(t.smallFont);
		const ImVec2 sz = ImGui::CalcTextSize(text);
		const float  wdt = std::max(a_width, sz.x);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float  lh = ImGui::GetTextLineHeight();
		ImGui::Dummy(ImVec2(wdt, lh));
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddText(p, t.muted, text);
		const float y = p.y + lh + 1.0f * t.scale;
		dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + wdt, y + 2.0f * t.scale), t.bg3);
		if (a_total > 0) {
			const float f = std::clamp(static_cast<float>(a_done) / static_cast<float>(a_total), 0.0f, 1.0f);
			dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + wdt * f, y + 2.0f * t.scale), t.accent);
		} else {
			// Indeterminate: a sliding segment.
			const float k = static_cast<float>(ImGui::GetTime() * 0.8 - static_cast<double>(static_cast<long long>(ImGui::GetTime() * 0.8)));
			dl->AddRectFilled(ImVec2(p.x + wdt * k * 0.75f, y), ImVec2(p.x + wdt * (k * 0.75f + 0.25f), y + 2.0f * t.scale), t.accent);
		}
		ImGui::PopFont();
	}

	bool MenuItem(const char* a_label, const char* a_shortcut, bool a_enabled, bool a_checked)
	{
		// TODO(theme): use w::MenuRow when available.
		return ImGui::MenuItem(a_label, a_shortcut, a_checked, a_enabled);
	}

	void KeyHint(const char* a_text)
	{
		// TODO(theme): use w::KeyCap when available.
		ImGui::PushFont(w::Th().smallFont);
		ImGui::PushStyleColor(ImGuiCol_Text, w::Th().muted);
		ImGui::TextUnformatted(a_text);
		ImGui::PopStyleColor();
		ImGui::PopFont();
	}

	void Swatch(std::uint32_t a_colour, const char* a_label)
	{
		const auto&  t = w::Th();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float  s = ImGui::GetFontSize() * 0.75f;
		const float  y = p.y + (ImGui::GetTextLineHeight() - s) * 0.5f;
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + s, y + s), a_colour, 2.0f * t.scale);
		ImGui::Dummy(ImVec2(s, ImGui::GetTextLineHeight()));
		ImGui::SameLine(0.0f, t.gap);
		ImGui::TextUnformatted(a_label);
	}

	bool CheckItem(const char* a_label, bool* a_value, const char* a_tooltip)
	{
		// TODO(theme): use w::Toggle when available.
		const bool changed = ImGui::Checkbox(a_label, a_value);
		w::Tip(a_tooltip);
		return changed;
	}

	void RefBlock(const char* a_id, const std::string& a_lines, const std::string& a_copy)
	{
		const auto& t = w::Th();
		ImGui::PushID(a_id);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float  wdt = ImGui::GetContentRegionAvail().x;
		const float  icon = t.toolbarH - 4.0f * t.scale;
		ImGui::PushFont(t.mono);
		const ImVec2 sz = ImGui::CalcTextSize(a_lines.c_str(), nullptr, false, wdt - icon - 2.0f * t.pad);
		ImGui::PopFont();
		const float h = std::max(sz.y + 2.0f * t.gap, icon);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(p, ImVec2(p.x + wdt, p.y + h), t.bg2, 3.0f * t.scale);
		ImGui::PushFont(t.mono);
		dl->AddText(t.mono, t.mono->LegacySize, ImVec2(p.x + t.pad, p.y + t.gap), t.muted, a_lines.c_str(), nullptr, wdt - icon - 2.0f * t.pad);
		ImGui::PopFont();
		ImGui::SetCursorScreenPos(ImVec2(p.x + wdt - icon, p.y));
		if (w::IconButton("copy", w::Icon::Copy, "Copy")) {
			ImGui::SetClipboardText(a_copy.c_str());
		}
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
		ImGui::Dummy(ImVec2(wdt, 1.0f));
		ImGui::PopID();
	}

	bool BeginPadded(const char* a_id, ImVec2 a_size, ImGuiWindowFlags a_flags)
	{
		const auto& t = w::Th();
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(t.pad, t.gap));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
		const bool open = ImGui::BeginChild(a_id, a_size, ImGuiChildFlags_AlwaysUseWindowPadding, a_flags);
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
		return open;
	}

	void EndPadded()
	{
		ImGui::EndChild();
	}

	bool SplitterTop(const char* a_id, float& a_size, float a_min, float a_max)
	{
		const auto&  t = w::Th();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float  wdt = ImGui::GetContentRegionAvail().x;
		const float  h = 5.0f * t.scale;
		ImGui::InvisibleButton(a_id, ImVec2(std::max(wdt, 1.0f), h));
		const bool active = ImGui::IsItemActive();
		if (ImGui::IsItemHovered() || active) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
			ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + wdt, p.y + h), t.accentDim);
		} else {
			ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + wdt, p.y), t.line);
		}
		if (active) {
			a_size = std::clamp(a_size - ImGui::GetIO().MouseDelta.y, a_min, a_max);
		}
		return active;
	}

	void CentredMessage(ImVec2 a_min, ImVec2 a_max, const char* a_text, const char* a_sub)
	{
		const auto& t = w::Th();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImGui::PushFont(t.heading);
		const ImVec2 sz = ImGui::CalcTextSize(a_text);
		const float  cx = (a_min.x + a_max.x) * 0.5f;
		const float  cy = (a_min.y + a_max.y) * 0.5f;
		dl->AddText(ImVec2(cx - sz.x * 0.5f, cy - sz.y), t.muted, a_text);
		ImGui::PopFont();
		if (a_sub && *a_sub) {
			ImGui::PushFont(t.smallFont);
			const ImVec2 s2 = ImGui::CalcTextSize(a_sub);
			dl->AddText(ImVec2(cx - s2.x * 0.5f, cy + t.gap), t.muted, a_sub);
			ImGui::PopFont();
		}
	}
}
