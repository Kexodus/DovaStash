// The widgets of Widgets.h, drawn to docs/EDITOR-UX.md §3 with the theme's tokens (Theme.cpp) and icons
// (Icons.cpp). Every widget is a real ImGui item (ItemSize/ItemAdd/ButtonBehavior), so keyboard navigation,
// SameLine, tooltips and clipping behave as with ImGui's own widgets.

#define IMGUI_DEFINE_MATH_OPERATORS
#include "ui/Widgets.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui_internal.h>

#include "ui/ThemeDetail.h"

namespace dova::editor::ui::w
{
	using detail::Mix;

	namespace
	{
		std::unordered_map<std::string, bool>& PopoverRequests()
		{
			static std::unordered_map<std::string, bool> s;
			return s;
		}

		struct Hit
		{
			ImRect  bb;
			ImGuiID id{ 0 };
			bool    hovered{ false };
			bool    held{ false };
			bool    pressed{ false };
		};

		// Adds a clickable item of `a_size` at the cursor. `a_baseline`: the text's offset from the item's top
		// (aligns SameLine text). False when clipped (nothing to draw).
		bool Item(const char* a_id, ImVec2 a_size, Hit& a_hit, float a_baseline = -1.0f, ImGuiButtonFlags a_flags = 0)
		{
			ImGuiWindow* win = ImGui::GetCurrentWindow();
			a_hit = {};
			if (win->SkipItems) {
				return false;
			}
			a_hit.id = win->GetID(a_id);
			a_hit.bb = ImRect(win->DC.CursorPos, win->DC.CursorPos + a_size);
			ImGui::ItemSize(a_size, a_baseline);
			if (!ImGui::ItemAdd(a_hit.bb, a_hit.id)) {
				return false;
			}
			a_hit.pressed = ImGui::ButtonBehavior(a_hit.bb, a_hit.id, &a_hit.hovered, &a_hit.held, a_flags);
			return true;
		}

		// A display-only item (no id, no interaction).
		bool Static(ImVec2 a_size, ImRect& a_bb, float a_baseline = -1.0f)
		{
			ImGuiWindow* win = ImGui::GetCurrentWindow();
			if (win->SkipItems) {
				return false;
			}
			a_bb = ImRect(win->DC.CursorPos, win->DC.CursorPos + a_size);
			ImGui::ItemSize(a_size, a_baseline);
			return ImGui::ItemAdd(a_bb, 0);
		}

		// The y of a text line's top on the current line (aligned like ImGui::Text).
		float LineTextTop()
		{
			ImGuiWindow* win = ImGui::GetCurrentWindow();
			return win->DC.CursorPos.y + win->DC.CurrLineTextBaseOffset;
		}

		const char* LabelEnd(const char* a_label)
		{
			return ImGui::FindRenderedTextEnd(a_label);
		}

		ImVec2 TextSize(ImFont* a_font, const char* a_text, const char* a_end = nullptr)
		{
			return a_font->CalcTextSizeA(a_font->LegacySize, FLT_MAX, 0.0f, a_text, a_end);
		}

		void Text(ImDrawList* a_dl, ImFont* a_font, ImVec2 a_pos, ImU32 a_col, const char* a_text, const char* a_end = nullptr)
		{
			a_dl->AddText(a_font, a_font->LegacySize, ImVec2(std::floor(a_pos.x), std::floor(a_pos.y)), a_col, a_text, a_end);
		}

		// Letter-spaced text (section heads); `a_upper` maps letters to capitals. Returns the width; draws when
		// `a_dl` is set.
		float Tracked(ImDrawList* a_dl, ImFont* a_font, ImVec2 a_pos, ImU32 a_col, const char* a_text, float a_tracking, bool a_upper)
		{
			const float  size = a_font->LegacySize;
			ImFontBaked* baked = a_font->GetFontBaked(size);
			float        x = std::floor(a_pos.x);
			const char*  s = a_text;
			const char*  end = a_text + std::strlen(a_text);
			bool         first = true;
			while (s < end) {
				unsigned int c = 0;
				s += ImTextCharFromUtf8(&c, s, end);
				if (c == 0) {
					break;
				}
				if (a_upper && c < 0x250) {
					c = static_cast<unsigned int>(std::towupper(static_cast<wint_t>(c)));
				}
				if (!first) {
					x += a_tracking;
				}
				first = false;
				if (a_dl) {
					a_font->RenderChar(a_dl, size, ImVec2(x, std::floor(a_pos.y)), a_col, static_cast<ImWchar>(c));
				}
				x += baked->GetCharAdvance(static_cast<ImWchar>(c));
			}
			return x - std::floor(a_pos.x);
		}

		// Text rotated a quarter turn (reads bottom to top), centred on `a_centre`.
		void TextVertical(ImDrawList* a_dl, ImFont* a_font, ImVec2 a_centre, ImU32 a_col, const char* a_text)
		{
			const ImVec2 ts = TextSize(a_font, a_text);
			// Glyphs outside the clip rect would be culled before the rotation: clip to the square the text turns in.
			const float half = std::max(ts.x, ts.y) * 0.5f + 2.0f;
			a_dl->PushClipRect(ImVec2(a_centre.x - half, a_centre.y - half), ImVec2(a_centre.x + half, a_centre.y + half), false);
			const int v0 = a_dl->VtxBuffer.Size;
			Text(a_dl, a_font, ImVec2(a_centre.x - ts.x * 0.5f, a_centre.y - ts.y * 0.5f), a_col, a_text);
			for (int i = v0; i < a_dl->VtxBuffer.Size; ++i) {
				ImDrawVert& v = a_dl->VtxBuffer[i];
				const float dx = v.pos.x - a_centre.x, dy = v.pos.y - a_centre.y;
				v.pos = ImVec2(a_centre.x + dy, a_centre.y - dx);
			}
			a_dl->PopClipRect();
		}

		// The rim under floating things: a 1 px line at 40 % black, a fainter one below it.
		void Rim(ImDrawList* a_dl, ImVec2 a_min, ImVec2 a_max, float a_rounding)
		{
			const Theme& t = Th();
			a_dl->PushClipRect(ImVec2(a_min.x - 2.0f, a_max.y), ImVec2(a_max.x + 2.0f, a_max.y + 3.0f), false);
			a_dl->AddRect(ImVec2(a_min.x, a_min.y + 1.0f), ImVec2(a_max.x, a_max.y + 1.0f), t.shadow, a_rounding, 0, 1.0f);
			a_dl->AddRect(ImVec2(a_min.x, a_min.y + 2.0f), ImVec2(a_max.x, a_max.y + 2.0f), WithAlpha(t.shadow, 0.18f), a_rounding, 0, 1.0f);
			a_dl->PopClipRect();
		}

		void Tooltip(const char* a_tooltip, const char* a_shortcut)
		{
			TipShortcut(a_tooltip, a_shortcut);
		}

		// The flat button behind ToolButton, IconTextButton, IconButton and the popover trigger.
		struct FlatSpec
		{
			const char* label{ nullptr };  // may hold "##id"
			const char* id{ nullptr };     // when label is empty
			bool        hasIcon{ false };
			Icon        icon{ Icon::Menu };
			bool        active{ false };   // a mode that is on: accent + underline
			bool        open{ false };     // its popover is open
			bool        chevron{ false };
			int         badge{ 0 };
			const char* tooltip{ nullptr };
			const char* shortcut{ nullptr };
		};

		bool FlatButton(const FlatSpec& a_s)
		{
			const Theme& t = Th();
			const char*  label = a_s.label ? a_s.label : "";
			const char*  end = LabelEnd(label);
			const bool   hasText = end > label;
			ImFont*      font = ImGui::GetFont();
			const float  h = t.inputH;
			const float  is = t.iconSize;
			const float  padX = hasText ? 10.0f * t.scale : (h - is) * 0.5f;
			const float  chevW = 9.0f * t.scale;
			const ImVec2 ts = hasText ? TextSize(font, label, end) : ImVec2(0, 0);
			char         badge[16]{};
			float        badgeW = 0.0f;
			ImFont*      badgeFont = t.smallFont ? t.smallFont : font;
			const float  badgePx = 11.0f * t.scale;
			const float  badgeH = 15.0f * t.scale;
			if (a_s.badge > 0) {
				std::snprintf(badge, sizeof(badge), "%d", a_s.badge);
				badgeW = std::max(badgeH, badgeFont->CalcTextSizeA(badgePx, FLT_MAX, 0.0f, badge).x + 8.0f * t.scale);
			}
			float w = padX * 2.0f;
			if (a_s.hasIcon) {
				w += is;
			}
			if (hasText) {
				w += ts.x + (a_s.hasIcon ? 6.0f * t.scale : 0.0f);
			}
			if (badgeW > 0.0f) {
				w += badgeW + 5.0f * t.scale;
			}
			if (a_s.chevron) {
				w += chevW + 5.0f * t.scale;
			}
			Hit hit;
			if (!Item(hasText ? label : a_s.id, ImVec2(std::round(w), h), hit, ImGui::GetStyle().FramePadding.y)) {
				return false;
			}
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImRect& bb = hit.bb;
			if (hit.held || a_s.open) {
				dl->AddRectFilled(bb.Min, bb.Max, t.bg3, t.radius);
			} else if (hit.hovered) {
				dl->AddRectFilled(bb.Min, bb.Max, t.bg3, t.radius);
			} else if (a_s.active) {
				dl->AddRectFilled(bb.Min, bb.Max, t.bg2, t.radius);
			}
			const ImU32 textCol = a_s.active ? t.accent : t.text;
			const ImU32 iconCol = a_s.active ? t.accent : (hit.hovered || a_s.open ? t.text : t.muted);
			float       x = bb.Min.x + padX;
			if (a_s.hasIcon) {
				DrawIcon(dl, a_s.icon, ImVec2(x, bb.Min.y + (h - is) * 0.5f), is, iconCol);
				x += is + (hasText ? 6.0f * t.scale : 0.0f);
			}
			if (hasText) {
				Text(dl, font, ImVec2(x, bb.Min.y + (h - ts.y) * 0.5f), textCol, label, end);
				x += ts.x;
			}
			if (badgeW > 0.0f) {
				x += 5.0f * t.scale;
				const ImVec2 bmin(x, std::round(bb.Min.y + (h - badgeH) * 0.5f));
				dl->AddRectFilled(bmin, ImVec2(bmin.x + badgeW, bmin.y + badgeH), t.accent, badgeH * 0.5f);
				const ImVec2 bs = badgeFont->CalcTextSizeA(badgePx, FLT_MAX, 0.0f, badge);
				dl->AddText(badgeFont, badgePx, ImVec2(std::floor(bmin.x + (badgeW - bs.x) * 0.5f), std::floor(bmin.y + (badgeH - bs.y) * 0.5f)), t.bg0, badge);
				x += badgeW;
			}
			if (a_s.chevron) {
				x += 5.0f * t.scale;
				DrawIcon(dl, a_s.open ? Icon::ChevronUp : Icon::ChevronDown, ImVec2(x - 1.0f * t.scale, bb.Min.y + (h - 11.0f * t.scale) * 0.5f), 11.0f * t.scale,
					hit.hovered || a_s.open ? t.text : t.muted);
			}
			if (a_s.active) {
				const float inset = std::min(padX, 8.0f * t.scale);
				dl->AddRectFilled(ImVec2(bb.Min.x + inset, bb.Max.y - 2.0f * t.scale), ImVec2(bb.Max.x - inset, bb.Max.y), t.accent, 1.0f * t.scale);
			}
			if (!a_s.open) {
				Tooltip(a_s.tooltip, a_s.shortcut);
			}
			return hit.pressed;
		}

		bool ChipImpl(const char* a_text, Kind a_kind, bool a_active, bool a_button, bool a_close, bool* a_closeClicked)
		{
			const Theme& t = Th();
			const ImU32  col = Colour(a_kind);
			ImFont*      font = t.smallFont ? t.smallFont : ImGui::GetFont();
			const char*  end = LabelEnd(a_text);
			const ImVec2 ts = TextSize(font, a_text, end);
			const float  h = t.chipH;
			const float  padX = 8.0f * t.scale;
			const float  closeW = a_close ? h - 2.0f * t.scale : 0.0f;
			const ImVec2 size(std::round(ts.x + 2.0f * padX + closeW - (a_close ? 3.0f * t.scale : 0.0f)), h);
			// Centre the chip on a framed line (26 px): text tops align with SameLine'd frames and text.
			const float baseline = (h - ts.y) * 0.5f;
			Hit         hit;
			ImRect      bb;
			if (a_button || a_close) {
				if (!Item(a_text, size, hit, baseline)) {
					return false;
				}
				bb = hit.bb;
			} else if (!Static(size, bb, baseline)) {
				return false;
			}
			ImDrawList*  dl = ImGui::GetWindowDrawList();
			const ImVec2 mouse = ImGui::GetIO().MousePos;
			const bool   overClose = a_close && hit.hovered && mouse.x >= bb.Max.x - closeW - 1.0f;
			ImU32        fill = t.bg2;
			if (a_active) {
				fill = Mix(col, t.bg2, 0.25f);
			} else if (a_button && hit.hovered) {
				fill = t.bg3;
			}
			const float r = h * 0.5f;
			dl->AddRectFilled(bb.Min, bb.Max, fill, r);
			dl->AddRect(bb.Min, bb.Max, a_button && hit.hovered && !a_active ? col : WithAlpha(col, 0.8f), r, 0, 1.0f);
			const ImU32 textCol = a_kind == Kind::Neutral ? (a_active ? t.text : t.muted) : col;
			Text(dl, font, ImVec2(bb.Min.x + padX, bb.Min.y + (h - ts.y) * 0.5f), textCol, a_text, end);
			bool closeHit = false;
			if (a_close) {
				const ImVec2 c(std::round(bb.Max.x - closeW * 0.5f - 2.0f * t.scale), std::round(bb.Min.y + h * 0.5f));
				if (overClose) {
					dl->AddCircleFilled(c, 6.5f * t.scale, WithAlpha(col, 0.28f), 16);
				}
				const float is = 10.0f * t.scale;
				DrawIcon(dl, Icon::Close, ImVec2(c.x - is * 0.5f, c.y - is * 0.5f), is, overClose ? t.text : textCol);
				closeHit = hit.pressed && overClose;
				if (overClose) {
					ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
				}
			}
			if (a_closeClicked) {
				*a_closeClicked = closeHit;
			}
			return hit.pressed;
		}

		// ---- Rails ----

		struct RailState
		{
			Side   side;
			float* size;
			bool*  open;
			float  minSize, maxSize;
		};
		std::vector<RailState> g_rails;
		int                    g_overlayDepth = 0;
	}

	// ---- Text ----

	void Title(const char* a_text)
	{
		PushFont(Font::Title);
		ImGui::TextUnformatted(a_text);
		PopFont();
	}

	void Heading(const char* a_text)
	{
		PushFont(Font::Heading);
		ImGui::TextUnformatted(a_text);
		PopFont();
	}

	void Muted(const char* a_text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, Th().muted);
		ImGui::TextUnformatted(a_text);
		ImGui::PopStyleColor();
	}

	void MutedWrapped(const char* a_text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, Th().muted);
		ImGui::TextWrapped("%s", a_text);
		ImGui::PopStyleColor();
	}

	void Small(const char* a_text)
	{
		PushFont(Font::Small);
		ImGui::TextUnformatted(a_text);
		PopFont();
	}

	void Mono(const char* a_text)
	{
		PushFont(Font::Mono);
		ImGui::TextUnformatted(a_text);
		PopFont();
	}

	void SectionHead(const char* a_text)
	{
		const Theme& t = Th();
		ImFont*      font = t.smallFont ? t.smallFont : ImGui::GetFont();
		const float  tracking = 1.1f * t.scale;
		const float  avail = ImGui::GetContentRegionAvail().x;
		const float  h = std::round(font->LegacySize + 2.0f * t.scale);
		ImGui::Dummy(ImVec2(0.0f, 4.0f * t.scale));
		ImRect bb;
		if (!Static(ImVec2(std::max(avail, 1.0f), h), bb, 0.0f)) {
			ImGui::Dummy(ImVec2(0.0f, 2.0f * t.scale));
			return;
		}
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		const float  cy = std::floor(bb.Min.y + h * 0.5f) + 0.5f;
		const float  d = 3.5f * t.scale;
		const float  dx = std::floor(bb.Min.x + d) + 0.5f;
		dl->AddQuadFilled(ImVec2(dx, cy - d), ImVec2(dx + d, cy), ImVec2(dx, cy + d), ImVec2(dx - d, cy), t.accentDim);
		const float tx = bb.Min.x + 2.0f * d + 6.0f * t.scale;
		const float tw = Tracked(dl, font, ImVec2(tx, bb.Min.y + (h - font->LegacySize) * 0.5f), t.muted, a_text, tracking, true);
		const float x0 = std::round(tx + tw + 8.0f * t.scale);
		if (x0 < bb.Max.x) {
			dl->AddRectFilled(ImVec2(x0, std::floor(cy)), ImVec2(bb.Max.x, std::floor(cy) + 1.0f), t.line);
		}
		ImGui::Dummy(ImVec2(0.0f, 2.0f * t.scale));
	}

	void Tip(const char* a_text)
	{
		if (a_text && *a_text && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip("%s", a_text);
		}
	}

	void TipShortcut(const char* a_text, const char* a_shortcut)
	{
		const bool hasText = a_text && *a_text;
		const bool hasKey = a_shortcut && *a_shortcut;
		if (!hasText && !hasKey) {
			return;
		}
		if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
			return;
		}
		if (ImGui::BeginTooltip()) {
			if (hasText) {
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(a_text);
			}
			if (hasKey) {
				if (hasText) {
					ImGui::SameLine(0.0f, Th().pad);
				}
				KeyCap(a_shortcut);
			}
			ImGui::EndTooltip();
		}
	}

	void Spacer(float a_px)
	{
		ImGui::Dummy(ImVec2(0.0f, a_px > 0.0f ? a_px : Th().gap));
	}

	// ---- Chips ----

	ImU32 Colour(Kind a_kind)
	{
		const Theme& t = Th();
		switch (a_kind) {
		case Kind::Ok:
			return t.ok;
		case Kind::Warn:
			return t.warn;
		case Kind::Error:
			return t.error;
		case Kind::Accent:
			return t.accent;
		case Kind::Info:
			return t.info;
		default:
			return t.muted;
		}
	}

	void Chip(const char* a_text, Kind a_kind)
	{
		ChipImpl(a_text, a_kind, false, false, false, nullptr);
	}

	bool ChipButton(const char* a_text, Kind a_kind, bool a_active)
	{
		return ChipImpl(a_text, a_kind, a_active, true, false, nullptr);
	}

	bool ChipClose(const char* a_text, Kind a_kind)
	{
		bool close = false;
		ChipImpl(a_text, a_kind, false, false, true, &close);
		return close;
	}

	void StatusDot(Kind a_kind, const char* a_tooltip)
	{
		const Theme& t = Th();
		const float  r = 3.0f * t.scale;
		const float  top = LineTextTop();
		const float  lh = ImGui::GetTextLineHeight();
		ImRect       bb;
		if (!Static(ImVec2(std::round(2.0f * r + 2.0f * t.scale), lh), bb, 0.0f)) {
			return;
		}
		ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(bb.Min.x + r + 1.0f * t.scale, top + lh * 0.5f + 0.5f * t.scale), r, Colour(a_kind), 16);
		if (a_tooltip && *a_tooltip && ImGui::IsMouseHoveringRect(bb.Min, ImVec2(bb.Max.x, bb.Min.y + lh))) {
			ImGui::SetTooltip("%s", a_tooltip);
		}
	}

	void Badge(const char* a_text, Kind a_kind)
	{
		const Theme& t = Th();
		ImFont*      font = t.smallFont ? t.smallFont : ImGui::GetFont();
		const float  px = 11.0f * t.scale;
		const float  h = 15.0f * t.scale;
		const ImVec2 ts = font->CalcTextSizeA(px, FLT_MAX, 0.0f, a_text);
		const float  w = std::round(std::max(h, ts.x + 8.0f * t.scale));
		ImRect       bb;
		if (!Static(ImVec2(w, h), bb, (h - ImGui::GetFontSize()) * 0.5f)) {
			return;
		}
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const bool  neutral = a_kind == Kind::Neutral;
		dl->AddRectFilled(bb.Min, bb.Max, neutral ? t.bg3 : Colour(a_kind), h * 0.5f);
		dl->AddText(font, px, ImVec2(std::floor(bb.Min.x + (w - ts.x) * 0.5f), std::floor(bb.Min.y + (h - ts.y) * 0.5f)), neutral ? t.text : t.bg0, a_text);
	}

	// ---- Icons ----

	bool IconButton(const char* a_id, Icon a_icon, const char* a_tooltip, const char* a_shortcut, bool a_active)
	{
		FlatSpec s;
		s.id = a_id;
		s.hasIcon = true;
		s.icon = a_icon;
		s.active = a_active;
		s.tooltip = a_tooltip;
		s.shortcut = a_shortcut;
		return FlatButton(s);
	}

	void IconInline(Icon a_icon, ImU32 a_colour, const char* a_tooltip)
	{
		const Theme& t = Th();
		const float  top = LineTextTop();
		const float  lh = ImGui::GetTextLineHeight();
		ImRect       bb;
		if (!Static(ImVec2(t.iconSize, lh), bb, 0.0f)) {
			return;
		}
		DrawIcon(ImGui::GetWindowDrawList(), a_icon, ImVec2(bb.Min.x, std::round(top + (lh - t.iconSize) * 0.5f)), t.iconSize, a_colour ? a_colour : t.text);
		if (a_tooltip && *a_tooltip && ImGui::IsMouseHoveringRect(bb.Min, bb.Max)) {
			ImGui::SetTooltip("%s", a_tooltip);
		}
	}

	// ---- Buttons ----

	bool ToolButton(const char* a_label, bool a_active, const char* a_tooltip, const char* a_shortcut)
	{
		FlatSpec s;
		s.label = a_label;
		s.active = a_active;
		s.tooltip = a_tooltip;
		s.shortcut = a_shortcut;
		return FlatButton(s);
	}

	bool IconTextButton(const char* a_label, Icon a_icon, bool a_active, const char* a_tooltip, const char* a_shortcut)
	{
		FlatSpec s;
		s.label = a_label;
		s.hasIcon = true;
		s.icon = a_icon;
		s.active = a_active;
		s.tooltip = a_tooltip;
		s.shortcut = a_shortcut;
		return FlatButton(s);
	}

	bool PrimaryButton(const char* a_label, bool a_enabled, const char* a_disabledTip)
	{
		const Theme& t = Th();
		ImFont*      font = ImGui::GetFont();
		const char*  end = LabelEnd(a_label);
		const ImVec2 ts = TextSize(font, a_label, end);
		const float  h = t.inputH;
		const ImVec2 size(std::round(ts.x + 28.0f * t.scale), h);
		ImGui::PushItemFlag(ImGuiItemFlags_Disabled, !a_enabled);
		Hit        hit;
		const bool visible = Item(a_label, size, hit, ImGui::GetStyle().FramePadding.y);
		ImGui::PopItemFlag();
		if (!visible) {
			return false;
		}
		ImDrawList*   dl = ImGui::GetWindowDrawList();
		const ImRect& bb = hit.bb;
		if (a_enabled) {
			const ImU32 fill = hit.held ? Mix(t.accent, t.accentDim, 0.5f) : hit.hovered ? t.select : t.accent;
			dl->AddRectFilled(bb.Min, bb.Max, fill, t.radius);
			Text(dl, font, ImVec2(bb.Min.x + (size.x - ts.x) * 0.5f, bb.Min.y + (h - ts.y) * 0.5f), t.bg0, a_label, end);
		} else {
			dl->AddRectFilled(bb.Min, bb.Max, t.bg2, t.radius);
			dl->AddRect(bb.Min, bb.Max, t.line, t.radius);
			Text(dl, font, ImVec2(bb.Min.x + (size.x - ts.x) * 0.5f, bb.Min.y + (h - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), a_label, end);
			if (a_disabledTip && *a_disabledTip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
				ImGui::SetTooltip("%s", a_disabledTip);
			}
		}
		return a_enabled && hit.pressed;
	}

	bool LinkButton(const char* a_label)
	{
		const Theme& t = Th();
		ImGuiWindow* win = ImGui::GetCurrentWindow();
		if (win->SkipItems) {
			return false;
		}
		ImFont*      font = ImGui::GetFont();
		const char*  end = LabelEnd(a_label);
		const ImVec2 ts = TextSize(font, a_label, end);
		const ImVec2 pos(win->DC.CursorPos.x, LineTextTop());
		const ImRect bb(pos, pos + ts);
		const ImGuiID id = win->GetID(a_label);
		ImGui::ItemSize(ts, 0.0f);
		if (!ImGui::ItemAdd(bb, id)) {
			return false;
		}
		bool       hovered = false, held = false;
		const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImU32 col = hovered ? t.select : t.accent;
		Text(dl, font, bb.Min, col, a_label, end);
		if (hovered) {
			const float y = std::floor(bb.Max.y);
			dl->AddRectFilled(ImVec2(bb.Min.x, y - 1.0f), ImVec2(bb.Max.x, y), col);
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		}
		return pressed;
	}

	bool Segmented(const char* a_id, int& a_value, std::span<const char* const> a_labels, std::span<const char* const> a_tooltips)
	{
		const Theme& t = Th();
		if (a_labels.empty()) {
			return false;
		}
		ImGuiWindow* win = ImGui::GetCurrentWindow();
		if (win->SkipItems) {
			return false;
		}
		ImFont*            font = ImGui::GetFont();
		const float        h = t.inputH;
		const float        padX = 12.0f * t.scale;
		std::vector<float> widths;
		float              total = 0.0f;
		for (const char* l : a_labels) {
			const float w = std::round(TextSize(font, l, LabelEnd(l)).x + 2.0f * padX);
			widths.push_back(w);
			total += w;
		}
		ImGui::PushID(a_id);
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		const ImVec2 origin = win->DC.CursorPos;
		const ImRect group(origin, origin + ImVec2(total, h));
		dl->AddRectFilled(group.Min, group.Max, t.bg2, t.radius);
		bool  changed = false;
		float x = origin.x;
		for (std::size_t i = 0; i < a_labels.size(); ++i) {
			if (i > 0) {
				ImGui::SameLine(0.0f, 0.0f);
			}
			const bool active = static_cast<int>(i) == a_value;
			Hit        hit;
			ImGui::PushID(static_cast<int>(i));
			const bool visible = Item("seg", ImVec2(widths[i], h), hit, ImGui::GetStyle().FramePadding.y);
			ImGui::PopID();
			if (visible) {
				const ImDrawFlags corners = a_labels.size() == 1 ? ImDrawFlags_RoundCornersAll
				                            : i == 0                ? ImDrawFlags_RoundCornersLeft
				                            : i + 1 == a_labels.size() ? ImDrawFlags_RoundCornersRight
				                                                       : ImDrawFlags_RoundCornersNone;
				if (active) {
					dl->AddRectFilled(hit.bb.Min, hit.bb.Max, t.bg3, t.radius, corners);
				} else if (hit.hovered) {
					dl->AddRectFilled(hit.bb.Min, hit.bb.Max, Mix(t.bg3, t.bg2, 0.5f), t.radius, corners);
				}
				const char*  end = LabelEnd(a_labels[i]);
				const ImVec2 ts = TextSize(font, a_labels[i], end);
				const ImU32  col = active ? t.accent : hit.hovered ? t.text : t.muted;
				Text(dl, font, ImVec2(hit.bb.Min.x + (widths[i] - ts.x) * 0.5f, hit.bb.Min.y + (h - ts.y) * 0.5f), col, a_labels[i], end);
				if (i < a_tooltips.size()) {
					Tip(a_tooltips[i]);
				}
				if (hit.pressed && !active) {
					a_value = static_cast<int>(i);
					changed = true;
				}
			}
			x += widths[i];
			// Divider between two inactive segments.
			if (i + 1 < a_labels.size() && !active && static_cast<int>(i + 1) != a_value) {
				const float dx = std::floor(x);
				dl->AddRectFilled(ImVec2(dx, origin.y + 6.0f * t.scale), ImVec2(dx + 1.0f, origin.y + h - 6.0f * t.scale), t.line);
			}
		}
		dl->AddRect(group.Min, group.Max, t.line, t.radius, 0, 1.0f);
		ImGui::PopID();
		return changed;
	}

	// ---- Inputs ----

	bool SearchBox(const char* a_id, char* a_buf, std::size_t a_size, const char* a_hint, float a_width)
	{
		const Theme& t = Th();
		ImGuiWindow* win = ImGui::GetCurrentWindow();
		if (win->SkipItems) {
			return false;
		}
		ImGui::PushID(a_id);
		const float  w = std::round(a_width > 0.0f ? a_width : ImGui::GetContentRegionAvail().x);
		const float  h = t.inputH;
		const float  is = 14.0f * t.scale;
		const float  iconW = std::round(8.0f * t.scale + is + 6.0f * t.scale);
		const float  clearW = h;
		const ImVec2 p = win->DC.CursorPos;
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(p, p + ImVec2(w, h), t.bg2, t.radius);
		ImGui::SetCursorScreenPos(ImVec2(p.x + iconW, p.y));
		ImGui::SetNextItemWidth(std::max(1.0f, w - iconW - clearW));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_TextDisabled, t.muted);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, ImGui::GetStyle().FramePadding.y));
		bool changed = ImGui::InputTextWithHint("##s", a_hint ? a_hint : "", a_buf, a_size, ImGuiInputTextFlags_EscapeClearsAll);
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(4);
		const bool active = ImGui::IsItemActive();
		const bool hovered = ImGui::IsItemHovered();
		DrawIcon(dl, Icon::Search, ImVec2(p.x + 8.0f * t.scale, std::round(p.y + (h - is) * 0.5f)), is, active || a_buf[0] ? t.text : t.muted);
		ImGui::SameLine(0.0f, 0.0f);
		if (a_buf[0]) {
			Hit hit;
			if (Item("clear", ImVec2(clearW, h), hit, ImGui::GetStyle().FramePadding.y)) {
				const ImVec2 c = hit.bb.GetCenter();
				if (hit.hovered) {
					dl->AddCircleFilled(c, 8.0f * t.scale, t.bg3, 16);
				}
				const float cs = 10.0f * t.scale;
				DrawIcon(dl, Icon::Close, ImVec2(std::round(c.x - cs * 0.5f), std::round(c.y - cs * 0.5f)), cs, hit.hovered ? t.text : t.muted);
				if (hit.pressed) {
					a_buf[0] = '\0';
					changed = true;
				}
				Tip("Clear  Esc");
			}
		} else {
			ImGui::Dummy(ImVec2(clearW, h));
		}
		const ImU32 border = active ? t.accentDim : hovered ? Mix(t.muted, t.line, 0.35f) : t.line;
		dl->AddRect(p, p + ImVec2(w, h), border, t.radius, 0, 1.0f);
		ImGui::PopID();
		return changed;
	}

	bool SearchBox(const char* a_id, std::string& a_text, const char* a_hint, float a_width)
	{
		char buf[512]{};
		std::snprintf(buf, sizeof(buf), "%s", a_text.c_str());
		const bool changed = SearchBox(a_id, buf, sizeof(buf), a_hint, a_width);
		if (changed) {
			a_text = buf;
		}
		return changed;
	}

	bool Toggle(const char* a_label, bool& a_value)
	{
		const Theme& t = Th();
		ImFont*      font = ImGui::GetFont();
		const char*  end = LabelEnd(a_label);
		const ImVec2 ts = TextSize(font, a_label, end);
		const float  trackW = 26.0f * t.scale, trackH = 14.0f * t.scale;
		const float  h = std::max(ts.y, trackH);
		const float  gap = ts.x > 0.0f ? 8.0f * t.scale : 0.0f;
		Hit          hit;
		if (!Item(a_label, ImVec2(std::round(trackW + gap + ts.x), h), hit, (h - ts.y) * 0.5f)) {
			return false;
		}
		if (hit.pressed) {
			a_value = !a_value;
			ImGui::MarkItemEdited(hit.id);
		}
		// Knob position eases between off and on.
		ImGuiStorage* st = ImGui::GetStateStorage();
		float*        pos = st->GetFloatRef(hit.id, a_value ? 1.0f : 0.0f);
		const float   target = a_value ? 1.0f : 0.0f;
		*pos = ImLinearSweep(*pos, target, ImGui::GetIO().DeltaTime * 8.0f);
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		const ImVec2 tmin(hit.bb.Min.x, std::round(hit.bb.Min.y + (h - trackH) * 0.5f));
		const ImVec2 tmax(tmin.x + trackW, tmin.y + trackH);
		const ImU32  trackOff = hit.hovered ? Mix(t.line, t.bg3, 0.5f) : t.bg3;
		dl->AddRectFilled(tmin, tmax, Mix(t.accent, trackOff, *pos), trackH * 0.5f);
		if (*pos < 0.5f) {
			dl->AddRect(tmin, tmax, t.line, trackH * 0.5f);
		}
		const float r = trackH * 0.5f - 2.0f * t.scale;
		const float cx = tmin.x + trackH * 0.5f + (trackW - trackH) * *pos;
		dl->AddCircleFilled(ImVec2(cx, tmin.y + trackH * 0.5f), r, Mix(t.bg0, t.muted, *pos), 16);
		if (ts.x > 0.0f) {
			Text(dl, font, ImVec2(tmax.x + gap, hit.bb.Min.y + (h - ts.y) * 0.5f), t.text, a_label, end);
		}
		return hit.pressed;
	}

	namespace
	{
		// The shared body of Check and Radio: a 16 px mark and the label, one line high.
		bool MarkItem(const char* a_label, bool a_on, bool a_round)
		{
			const Theme& t = Th();
			ImFont*      font = ImGui::GetFont();
			const char*  end = LabelEnd(a_label);
			const ImVec2 ts = TextSize(font, a_label, end);
			const float  box = std::round(16.0f * t.scale);
			const float  h = std::max(ts.y, box);
			const float  gap = ts.x > 0.0f ? 8.0f * t.scale : 0.0f;
			Hit          hit;
			if (!Item(a_label, ImVec2(std::round(box + gap + ts.x), h), hit, (h - ts.y) * 0.5f)) {
				return false;
			}
			ImDrawList*  dl = ImGui::GetWindowDrawList();
			const ImVec2 bmin(hit.bb.Min.x, std::round(hit.bb.Min.y + (h - box) * 0.5f));
			const ImVec2 bmax(bmin.x + box, bmin.y + box);
			const float  r = a_round ? box * 0.5f : t.radius;
			dl->AddRectFilled(bmin, bmax, hit.hovered || hit.held ? t.bg3 : t.bg2, r);
			dl->AddRect(bmin, bmax, a_on ? t.accentDim : hit.hovered ? Mix(t.muted, t.line, 0.5f) : Mix(t.muted, t.line, 0.25f), r, 0, 1.0f);
			if (a_on) {
				if (a_round) {
					dl->AddCircleFilled(ImVec2(bmin.x + box * 0.5f, bmin.y + box * 0.5f), 4.0f * t.scale, t.accent, 16);
				} else {
					const float is = 12.0f * t.scale;
					DrawIcon(dl, Icon::Check, ImVec2(std::round(bmin.x + (box - is) * 0.5f), std::round(bmin.y + (box - is) * 0.5f)), is, t.accent);
				}
			}
			if (ts.x > 0.0f) {
				Text(dl, font, ImVec2(bmax.x + gap, hit.bb.Min.y + (h - ts.y) * 0.5f), t.text, a_label, end);
			}
			return hit.pressed;
		}
	}

	bool Check(const char* a_label, bool& a_value)
	{
		if (MarkItem(a_label, a_value, false)) {
			a_value = !a_value;
			ImGui::MarkItemEdited(ImGui::GetItemID());
			return true;
		}
		return false;
	}

	bool Radio(const char* a_label, int& a_value, int a_option)
	{
		if (MarkItem(a_label, a_value == a_option, true) && a_value != a_option) {
			a_value = a_option;
			ImGui::MarkItemEdited(ImGui::GetItemID());
			return true;
		}
		return false;
	}

	void Progress(const char* a_label, std::size_t a_done, std::size_t a_total, float a_width)
	{
		const Theme& t = Th();
		ImFont*      font = t.smallFont ? t.smallFont : ImGui::GetFont();
		char         text[160]{};
		if (a_total > 0) {
			std::snprintf(text, sizeof(text), "%s %zu/%zu", a_label ? a_label : "", a_done, a_total);
		} else {
			std::snprintf(text, sizeof(text), "%s", a_label ? a_label : "");
		}
		const ImVec2 ts = TextSize(font, text);
		const float  barW = std::round(a_width > 0.0f ? a_width : 80.0f * t.scale);
		const float  gap = 8.0f * t.scale;
		const float  lh = ImGui::GetTextLineHeight();
		const float  h = std::max(lh, ts.y);
		const float  top = LineTextTop();
		ImRect       bb;
		if (!Static(ImVec2(std::round(ts.x + gap + barW), h), bb, 0.0f)) {
			return;
		}
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float cy = std::round(top + lh * 0.5f);
		Text(dl, font, ImVec2(bb.Min.x, cy - ts.y * 0.5f), t.muted, text);
		const float  bh = 3.0f * t.scale;
		const ImVec2 bmin(std::round(bb.Min.x + ts.x + gap), std::round(cy - bh * 0.5f));
		const ImVec2 bmax(bmin.x + barW, bmin.y + bh);
		dl->AddRectFilled(bmin, bmax, t.bg3, bh * 0.5f);
		if (a_total > 0) {
			const float f = std::clamp(static_cast<float>(a_done) / static_cast<float>(a_total), 0.0f, 1.0f);
			if (f > 0.0f) {
				dl->AddRectFilled(bmin, ImVec2(bmin.x + std::max(bh, barW * f), bmax.y), t.accent, bh * 0.5f);
			}
		} else {
			// Indeterminate: a segment sweeping across.
			const float seg = barW * 0.3f;
			const float ph = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.8f, 1.0f);
			const float x0 = bmin.x - seg + (barW + seg) * ph;
			dl->PushClipRect(bmin, bmax, true);
			dl->AddRectFilled(ImVec2(x0, bmin.y), ImVec2(x0 + seg, bmax.y), t.accent, bh * 0.5f);
			dl->PopClipRect();
		}
	}

	void KeyCap(const char* a_key)
	{
		const Theme& t = Th();
		ImFont*      font = t.smallFont ? t.smallFont : ImGui::GetFont();
		const ImVec2 ts = TextSize(font, a_key);
		const float  h = t.chipH;
		const float  w = std::round(std::max(h, ts.x + 10.0f * t.scale));
		ImRect       bb;
		if (!Static(ImVec2(w, h), bb, std::max(0.0f, (h - ImGui::GetFontSize()) * 0.5f))) {
			return;
		}
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		const float  r = t.radius;
		const ImVec2 mn = detail::Floor(bb.Min), mx = detail::Floor(bb.Max);
		dl->AddRectFilled(mn, mx, t.line, r);                                                     // the border, 2 px at the bottom
		dl->AddRectFilled(mn + ImVec2(1, 1), mx - ImVec2(1, std::round(2.0f * t.scale)), t.bg2, std::max(0.0f, r - 1.0f));
		Text(dl, font, ImVec2(mn.x + (w - ts.x) * 0.5f, mn.y + (h - 1.0f * t.scale - ts.y) * 0.5f), t.text, a_key);
	}

	bool MenuRow(const char* a_label, const char* a_shortcut, bool a_enabled)
	{
		const Theme& t = Th();
		ImGuiWindow* win = ImGui::GetCurrentWindow();
		if (win->SkipItems) {
			return false;
		}
		ImFont*      font = ImGui::GetFont();
		ImFont*      smallF = t.smallFont ? t.smallFont : font;
		const char*  end = LabelEnd(a_label);
		const ImVec2 ts = TextSize(font, a_label, end);
		const bool   hasKey = a_shortcut && *a_shortcut;
		const ImVec2 ks = hasKey ? TextSize(smallF, a_shortcut) : ImVec2(0, 0);
		const float  padX = 8.0f * t.scale;
		const float  want = ts.x + 2.0f * padX + (hasKey ? ks.x + 24.0f * t.scale : 0.0f);
		const float  full = std::max(want, ImGui::GetContentRegionAvail().x);
		const float  h = t.rowH;
		ImGui::PushItemFlag(ImGuiItemFlags_Disabled, !a_enabled);
		// Registers the wanted width (auto-sized popups grow to it) but hit-tests the full row.
		const ImGuiID id = win->GetID(a_label);
		const ImVec2  pos = win->DC.CursorPos;
		const ImRect  bb(pos, pos + ImVec2(full, h));
		ImGui::ItemSize(ImVec2(want, h), (h - ts.y) * 0.5f);
		bool pressed = false, hovered = false, held = false;
		if (ImGui::ItemAdd(bb, id)) {
			pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_PressedOnRelease);
			ImDrawList* dl = ImGui::GetWindowDrawList();
			if (a_enabled && (hovered || held)) {
				dl->AddRectFilled(bb.Min, bb.Max, t.bg3, t.radius);
			}
			const ImU32 col = a_enabled ? t.text : ImGui::GetColorU32(ImGuiCol_TextDisabled);
			Text(dl, font, ImVec2(bb.Min.x + padX, bb.Min.y + (h - ts.y) * 0.5f), col, a_label, end);
			if (hasKey) {
				Text(dl, smallF, ImVec2(bb.Max.x - padX - ks.x, bb.Min.y + (h - ks.y) * 0.5f), t.muted, a_shortcut);
			}
		}
		ImGui::PopItemFlag();
		if (pressed && a_enabled) {
			ImGui::CloseCurrentPopup();
			return true;
		}
		return false;
	}

	void ToolbarDivider()
	{
		const Theme& t = Th();
		const float  h = t.inputH;
		ImRect       bb;
		if (!Static(ImVec2(std::round(1.0f + 6.0f * t.scale), h), bb, ImGui::GetStyle().FramePadding.y)) {
			return;
		}
		const float x = std::floor(bb.GetCenter().x);
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, bb.Min.y + 6.0f * t.scale), ImVec2(x + 1.0f, bb.Max.y - 6.0f * t.scale), t.line);
	}

	void Ornament(float a_width)
	{
		const Theme& t = Th();
		const float  avail = ImGui::GetContentRegionAvail().x;
		const float  w = std::min(avail, a_width > 0.0f ? a_width : 160.0f * t.scale);
		const float  h = 12.0f * t.scale;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, std::floor((avail - w) * 0.5f)));
		ImRect bb;
		if (!Static(ImVec2(w, h), bb, 0.0f)) {
			return;
		}
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		const ImVec2 c(std::floor(bb.GetCenter().x) + 0.5f, std::floor(bb.GetCenter().y) + 0.5f);
		const float  d = 3.5f * t.scale;
		const float  gap = 7.0f * t.scale;
		const float  y = std::floor(c.y);
		// The lines fade out towards their ends.
		const ImU32 lineCol = t.line, clear = WithAlpha(t.line, 0.0f);
		dl->AddRectFilledMultiColor(ImVec2(bb.Min.x, y), ImVec2(c.x - d - gap, y + 1.0f), clear, lineCol, lineCol, clear);
		dl->AddRectFilledMultiColor(ImVec2(c.x + d + gap, y), ImVec2(bb.Max.x, y + 1.0f), lineCol, clear, clear, lineCol);
		dl->AddQuadFilled(ImVec2(c.x, c.y - d), ImVec2(c.x + d, c.y), ImVec2(c.x, c.y + d), ImVec2(c.x - d, c.y), t.accentDim);
		const float e = 1.6f * t.scale;
		dl->AddQuadFilled(ImVec2(c.x - d - gap * 0.5f, c.y - e), ImVec2(c.x - d - gap * 0.5f + e, c.y), ImVec2(c.x - d - gap * 0.5f, c.y + e),
			ImVec2(c.x - d - gap * 0.5f - e, c.y), t.line);
		dl->AddQuadFilled(ImVec2(c.x + d + gap * 0.5f, c.y - e), ImVec2(c.x + d + gap * 0.5f + e, c.y), ImVec2(c.x + d + gap * 0.5f, c.y + e),
			ImVec2(c.x + d + gap * 0.5f - e, c.y), t.line);
	}

	void TextWithMuted(const char* a_text, const char* a_muted)
	{
		ImGui::TextUnformatted(a_text);
		if (a_muted && *a_muted) {
			ImGui::SameLine(0.0f, Th().gap);
			Muted(a_muted);
		}
	}

	// ---- Popovers ----

	bool BeginPopover(const char* a_id, const char* a_label, Icon a_icon, int a_badge, const char* a_tooltip, const char* a_shortcut)
	{
		const Theme& t = Th();
		ImGui::PushID(a_id);
		const bool wasOpen = ImGui::IsPopupOpen("pop");
		const bool hasLabel = a_label && *a_label;
		FlatSpec   s;
		s.label = hasLabel ? a_label : nullptr;
		s.id = "btn";
		s.hasIcon = !hasLabel || a_icon != Icon::ChevronDown;
		s.icon = a_icon;
		s.open = wasOpen;
		s.chevron = hasLabel;
		s.badge = a_badge;
		s.tooltip = a_tooltip;
		s.shortcut = a_shortcut;
		const bool clicked = FlatButton(s);
		const ImRect bb(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		auto& req = PopoverRequests();
		auto  it = req.find(a_id);
		const bool requested = it != req.end() && it->second;
		if (requested) {
			it->second = false;
		}
		if ((clicked && !wasOpen) || requested) {
			ImGui::OpenPopup("pop");
		}
		// Under the button, left-aligned; right-aligned when the button sits in the right half of the screen.
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		const bool           right = bb.GetCenter().x > vp->WorkPos.x + vp->WorkSize.x * 0.5f;
		ImGui::SetNextWindowPos(ImVec2(right ? bb.Max.x : bb.Min.x, bb.Max.y + 4.0f * t.scale), ImGuiCond_Always, ImVec2(right ? 1.0f : 0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f * t.scale, 6.0f * t.scale));
		const bool open = ImGui::BeginPopup("pop", ImGuiWindowFlags_NoMove);
		ImGui::PopStyleVar();
		if (!open) {
			ImGui::PopID();
			return false;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * t.scale, 4.0f * t.scale));
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::GetIO().WantTextInput) {
			ImGui::CloseCurrentPopup();
		}
		return true;
	}

	void EndPopover()
	{
		ImGui::PopStyleVar();  // ItemSpacing
		ImGui::EndPopup();
		ImGui::PopID();
	}

	void OpenPopover(const char* a_id)
	{
		PopoverRequests()[a_id] = true;
	}

	// ---- Layout ----

	bool BeginRail(const char* a_id, Side a_side, float& a_size, bool& a_open, float a_minSize, float a_maxSize, const char* a_collapsedText)
	{
		const Theme& t = Th();
		const bool   horizontal = a_side != Side::Bottom;
		ImGui::PushStyleColor(ImGuiCol_ChildBg, t.bg1);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		if (!a_open) {
			const float  strip = t.stripW;
			const ImVec2 size = horizontal ? ImVec2(strip, 0.0f) : ImVec2(0.0f, strip);
			ImGui::BeginChild(a_id, size, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			ImGui::PopStyleVar(3);
			ImGui::PopStyleColor();
			const ImVec2 mn = ImGui::GetWindowPos();
			const ImVec2 sz = ImGui::GetWindowSize();
			ImDrawList*  dl = ImGui::GetWindowDrawList();
			ImGui::SetCursorScreenPos(mn);
			const bool clicked = ImGui::InvisibleButton("expand", sz);
			const bool hovered = ImGui::IsItemHovered();
			if (hovered) {
				dl->AddRectFilled(mn, mn + sz, t.bg2);
			}
			// The inner edge.
			switch (a_side) {
			case Side::Left:
				dl->AddRectFilled(ImVec2(mn.x + sz.x - 1.0f, mn.y), mn + sz, t.line);
				break;
			case Side::Right:
				dl->AddRectFilled(mn, ImVec2(mn.x + 1.0f, mn.y + sz.y), t.line);
				break;
			default:
				dl->AddRectFilled(mn, ImVec2(mn.x + sz.x, mn.y + 1.0f), t.line);
				break;
			}
			const float  b = t.iconSize;
			const Icon   icon = a_side == Side::Left ? Icon::ChevronRight : a_side == Side::Right ? Icon::ChevronLeft : Icon::ChevronUp;
			const ImVec2 ip = horizontal ? ImVec2(std::round(mn.x + (sz.x - b) * 0.5f), std::round(mn.y + sz.y - b - 8.0f * t.scale))
			                             : ImVec2(std::round(mn.x + sz.x - b - 10.0f * t.scale), std::round(mn.y + (sz.y - b) * 0.5f));
			DrawIcon(dl, icon, ip, b, hovered ? t.accent : t.muted);
			if (a_collapsedText && *a_collapsedText) {
				ImFont* font = t.smallFont ? t.smallFont : ImGui::GetFont();
				const ImU32 col = hovered ? t.text : t.muted;
				if (horizontal) {
					// The whole text when it fits the strip's height, else its first word.
					std::string text = a_collapsedText;
					if (TextSize(font, text.c_str()).x > sz.y - 3.0f * b) {
						if (const auto sp = text.find(' '); sp != std::string::npos) {
							text.resize(sp);
						}
					}
					TextVertical(dl, font, ImVec2(std::round(mn.x + sz.x * 0.5f), std::round(mn.y + sz.y * 0.5f)), col, text.c_str());
				} else {
					const ImVec2 ts = TextSize(font, a_collapsedText);
					Text(dl, font, ImVec2(mn.x + t.pad, mn.y + (sz.y - ts.y) * 0.5f), col, a_collapsedText);
				}
			}
			if (clicked) {
				a_open = true;
			}
			TipShortcut("Show", nullptr);
			ImGui::EndChild();
			return false;
		}
		a_size = std::clamp(a_size, a_minSize, a_maxSize);
		const ImVec2 size = horizontal ? ImVec2(std::round(a_size), 0.0f) : ImVec2(0.0f, std::round(a_size));
		ImGui::BeginChild(a_id, size, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor();
		g_rails.push_back({ a_side, &a_size, &a_open, a_minSize, a_maxSize });
		const ImVec2 mn = ImGui::GetWindowPos();
		const ImVec2 sz = ImGui::GetWindowSize();
		const ImVec2 mx = mn + sz;
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		// Resize handle on the inner edge, outside the content child so it can be hovered.
		const float hs = std::round(5.0f * t.scale);
		ImRect      handle, edge;
		switch (a_side) {
		case Side::Left:
			handle = ImRect(ImVec2(mx.x - hs, mn.y), mx);
			edge = ImRect(ImVec2(mx.x - 1.0f, mn.y), mx);
			break;
		case Side::Right:
			handle = ImRect(mn, ImVec2(mn.x + hs, mx.y));
			edge = ImRect(mn, ImVec2(mn.x + 1.0f, mx.y));
			break;
		default:
			handle = ImRect(mn, ImVec2(mx.x, mn.y + hs));
			edge = ImRect(mn, ImVec2(mx.x, mn.y + 1.0f));
			break;
		}
		ImGui::SetCursorScreenPos(handle.Min);
		ImGui::InvisibleButton("resize", ImMax(handle.GetSize(), ImVec2(1, 1)));
		const bool active = ImGui::IsItemActive();
		const bool hot = active || ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone);
		if (hot) {
			ImGui::SetMouseCursor(horizontal ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
		}
		if (active) {
			const ImVec2 d = ImGui::GetIO().MouseDelta;
			switch (a_side) {
			case Side::Left:
				a_size += d.x;
				break;
			case Side::Right:
				a_size -= d.x;
				break;
			default:
				a_size -= d.y;
				break;
			}
			a_size = std::clamp(a_size, a_minSize, a_maxSize);
		}
		if (hot) {
			const float th = std::round(2.0f * t.scale);
			const ImRect bar = a_side == Side::Left    ? ImRect(ImVec2(mx.x - th, mn.y), mx)
			                   : a_side == Side::Right ? ImRect(mn, ImVec2(mn.x + th, mx.y))
			                                           : ImRect(mn, ImVec2(mx.x, mn.y + th));
			dl->AddRectFilled(bar.Min, bar.Max, active ? t.accent : t.accentDim);
		} else {
			dl->AddRectFilled(edge.Min, edge.Max, t.line);
		}
		// The content, beside the handle.
		ImVec2 cmin = mn, csize = sz;
		switch (a_side) {
		case Side::Left:
			csize.x -= hs;
			break;
		case Side::Right:
			cmin.x += hs;
			csize.x -= hs;
			break;
		default:
			cmin.y += hs;
			csize.y -= hs;
			break;
		}
		ImGui::SetCursorScreenPos(cmin);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(t.pad, t.pad));
		ImGui::BeginChild("content", ImMax(csize, ImVec2(1, 1)), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_None);
		ImGui::PopStyleVar();
		return true;
	}

	void EndRail()
	{
		const Theme& t = Th();
		ImGui::EndChild();  // content
		if (g_rails.empty()) {
			ImGui::EndChild();
			return;
		}
		const RailState rail = g_rails.back();
		g_rails.pop_back();
		// The collapse chevron in the outer corner, in its own child so it sits above the content.
		const ImVec2 mn = ImGui::GetWindowPos();
		const ImVec2 sz = ImGui::GetWindowSize();
		const float  b = std::round(22.0f * t.scale);
		const float  m = 6.0f * t.scale;
		const float  sb = ImGui::GetStyle().ScrollbarSize;
		ImVec2       p;
		Icon         icon;
		switch (rail.side) {
		case Side::Left:
			p = ImVec2(mn.x + sz.x - b - m - sb, mn.y + sz.y - b - m);
			icon = Icon::ChevronLeft;
			break;
		case Side::Right:
			p = ImVec2(mn.x + m + 4.0f * t.scale, mn.y + sz.y - b - m);
			icon = Icon::ChevronRight;
			break;
		default:
			p = ImVec2(mn.x + sz.x - b - m - sb, mn.y + m + 4.0f * t.scale);
			icon = Icon::ChevronDown;
			break;
		}
		p = detail::Floor(p);
		ImGui::SetCursorScreenPos(p);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::BeginChild("collapse", ImVec2(b, b), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
		ImGui::PopStyleVar();
		const bool  clicked = ImGui::InvisibleButton("c", ImVec2(b, b));
		const bool  hovered = ImGui::IsItemHovered();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(p, p + ImVec2(b, b), hovered ? t.bg3 : WithAlpha(t.bg1, 0.85f), t.radius);
		const float is = 14.0f * t.scale;
		DrawIcon(dl, icon, ImVec2(std::round(p.x + (b - is) * 0.5f), std::round(p.y + (b - is) * 0.5f)), is, hovered ? t.accent : t.muted);
		TipShortcut("Hide", nullptr);
		ImGui::EndChild();
		if (clicked) {
			*rail.open = false;
		}
		ImGui::EndChild();  // the rail
	}

	bool BeginOverlay(const char* a_id, ImVec2 a_pos, float a_alpha)
	{
		const Theme& t = Th();
		const ImVec2 origin = ImGui::GetWindowPos();
		ImGui::SetCursorScreenPos(detail::Floor(ImVec2(origin.x + a_pos.x, origin.y + a_pos.y)));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, WithAlpha(t.bg1, a_alpha));
		ImGui::PushStyleColor(ImGuiCol_Border, t.line);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f * t.scale);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f * t.scale, std::round((t.toolbarH - t.inputH) * 0.5f)));
		ImGui::BeginChild(a_id, ImVec2(0, 0), ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor(2);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f * t.scale, ImGui::GetStyle().ItemSpacing.y));
		++g_overlayDepth;
		return true;
	}

	void EndOverlay()
	{
		ImGui::PopStyleVar();  // ItemSpacing
		ImGui::EndChild();
		--g_overlayDepth;
		Rim(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), 4.0f * Th().scale);
	}

	void HintLine(const char* a_text, ImVec2 a_viewportMin, ImVec2 a_viewportMax)
	{
		if (!a_text || !*a_text) {
			return;
		}
		const Theme& t = Th();
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		ImFont*      font = t.smallFont ? t.smallFont : ImGui::GetFont();
		const ImVec2 ts = TextSize(font, a_text);
		const float  h = std::round(ts.y + 14.0f * t.scale);
		const float  y0 = a_viewportMax.y - h;
		// A soft fade from the view into a darker band so the text reads over any scene.
		dl->AddRectFilledMultiColor(ImVec2(a_viewportMin.x, y0), a_viewportMax, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 120), IM_COL32(0, 0, 0, 120));
		const ImVec2 p(a_viewportMin.x + t.pad + 2.0f * t.scale, a_viewportMax.y - ts.y - 6.0f * t.scale);
		Text(dl, font, p + ImVec2(0, 1), IM_COL32(0, 0, 0, 160), a_text);
		Text(dl, font, p, t.muted, a_text);
	}

	bool Row(const char* a_id, const char* a_label, bool a_selected, const char* a_right, Kind a_rightKind)
	{
		const Theme& t = Th();
		ImGuiWindow* win = ImGui::GetCurrentWindow();
		if (win->SkipItems) {
			return false;
		}
		ImFont*      font = ImGui::GetFont();
		const char*  label = a_label ? a_label : "";
		const ImVec2 ts = TextSize(font, label);
		const float  h = t.rowH;
		const float  w = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
		const ImGuiID id = win->GetID(a_id);
		const ImVec2  pos = win->DC.CursorPos;
		const ImRect  bb(pos, pos + ImVec2(w, h));
		// The layout width ends after the label so SameLine() content lands inside the row.
		const float layoutW = *label ? t.pad + ts.x : t.pad - ImGui::GetStyle().ItemSpacing.x;
		ImGui::ItemSize(ImVec2(std::max(1.0f, layoutW), h), (h - ImGui::GetFontSize()) * 0.5f);
		ImGui::SetNextItemAllowOverlap();
		if (!ImGui::ItemAdd(bb, id)) {
			return false;
		}
		bool       hovered = false, held = false;
		const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_AllowOverlap);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		if (a_selected) {
			dl->AddRectFilled(bb.Min, bb.Max, WithAlpha(t.accent, hovered ? 0.26f : 0.20f), t.radius);
			dl->AddRectFilled(bb.Min, ImVec2(bb.Min.x + std::round(2.0f * t.scale), bb.Max.y), t.accent);
		} else if (hovered || held) {
			dl->AddRectFilled(bb.Min, bb.Max, t.bg3, t.radius);
		}
		float rightX = bb.Max.x;
		if (a_right && *a_right) {
			ImFont*      smallF = t.smallFont ? t.smallFont : font;
			const ImVec2 rs = TextSize(smallF, a_right);
			rightX = bb.Max.x - t.pad - rs.x;
			Text(dl, smallF, ImVec2(rightX, bb.Min.y + (h - rs.y) * 0.5f), Colour(a_rightKind), a_right);
			rightX -= t.pad;
		}
		if (*label) {
			const ImVec2 tp(bb.Min.x + t.pad, std::floor(bb.Min.y + (h - ts.y) * 0.5f));
			ImGui::PushStyleColor(ImGuiCol_Text, t.text);
			ImGui::RenderTextEllipsis(dl, tp, ImVec2(rightX, bb.Max.y), rightX, label, nullptr, &ts);
			ImGui::PopStyleColor();
		}
		return pressed;
	}

	void EmptyState(const char* a_heading, const char* a_text)
	{
		const Theme& t = Th();
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		ImFont*      heading = t.heading ? t.heading : ImGui::GetFont();
		float        blockH = heading->LegacySize + 12.0f * t.scale + 18.0f * t.scale;
		const float  wrap = std::min(avail.x - 2.0f * t.pad, 360.0f * t.scale);
		if (a_text && *a_text) {
			blockH += ImGui::CalcTextSize(a_text, nullptr, false, wrap).y + t.gap;
		}
		ImGui::Dummy(ImVec2(0, std::max(0.0f, std::floor((avail.y - blockH) * 0.42f))));
		Ornament(120.0f * t.scale);
		ImGui::Dummy(ImVec2(0, 6.0f * t.scale));
		PushFont(Font::Heading);
		const float hw = ImGui::CalcTextSize(a_heading).x;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, std::floor((avail.x - hw) * 0.5f)));
		ImGui::PushStyleColor(ImGuiCol_Text, t.text);
		ImGui::TextUnformatted(a_heading);
		ImGui::PopStyleColor();
		PopFont();
		if (a_text && *a_text) {
			const float tw = std::min(ImGui::CalcTextSize(a_text).x, wrap);
			const float x0 = ImGui::GetCursorPosX() + std::max(0.0f, std::floor((avail.x - tw) * 0.5f));
			ImGui::SetCursorPosX(x0);
			ImGui::PushTextWrapPos(x0 + tw);
			ImGui::PushStyleColor(ImGuiCol_Text, t.muted);
			ImGui::TextWrapped("%s", a_text);
			ImGui::PopStyleColor();
			ImGui::PopTextWrapPos();
		}
	}

	bool Card(const char* a_id, const char* a_title, const char* a_text, const char* a_shortcut, float a_width)
	{
		const Theme& t = Th();
		ImFont*      heading = t.heading ? t.heading : ImGui::GetFont();
		ImFont*      smallF = t.smallFont ? t.smallFont : ImGui::GetFont();
		const float  w = std::round(a_width > 0.0f ? a_width : ImGui::GetContentRegionAvail().x);
		const float  pad = 12.0f * t.scale;
		const ImVec2 bodySz = (a_text && *a_text) ? smallF->CalcTextSizeA(smallF->LegacySize, FLT_MAX, w - 2.0f * pad, a_text) : ImVec2(0, 0);
		const float  titleH = heading->LegacySize;
		const float  h = std::round(pad * 2.0f + titleH + (bodySz.y > 0.0f ? 4.0f * t.scale + bodySz.y : 0.0f));
		Hit          hit;
		if (!Item(a_id, ImVec2(w, h), hit, pad)) {
			return false;
		}
		ImDrawList*   dl = ImGui::GetWindowDrawList();
		const ImRect& bb = hit.bb;
		const float   r = 4.0f * t.scale;
		dl->AddRectFilled(bb.Min, bb.Max, hit.held ? t.bg3 : hit.hovered ? Mix(t.bg3, t.bg2, 0.45f) : t.bg2, r);
		dl->AddRect(bb.Min, bb.Max, hit.hovered ? t.accent : t.line, r, 0, 1.0f);
		Text(dl, heading, ImVec2(bb.Min.x + pad, bb.Min.y + pad), t.text, a_title);
		if (a_shortcut && *a_shortcut) {
			const ImVec2 ks = TextSize(smallF, a_shortcut);
			Text(dl, smallF, ImVec2(bb.Max.x - pad - ks.x, bb.Min.y + pad + (titleH - ks.y) * 0.5f), t.muted, a_shortcut);
		}
		if (bodySz.y > 0.0f) {
			dl->AddText(smallF, smallF->LegacySize, detail::Floor(ImVec2(bb.Min.x + pad, bb.Min.y + pad + titleH + 4.0f * t.scale)), t.muted, a_text, nullptr, w - 2.0f * pad);
		}
		if (hit.hovered) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		}
		return hit.pressed;
	}

	// ---- Helpers ----

	float TextWidth(const char* a_text)
	{
		return ImGui::CalcTextSize(a_text, nullptr, true).x;
	}

	bool Shortcut(ImGuiKey a_key, bool a_ctrl, bool a_shift)
	{
		const ImGuiIO& io = ImGui::GetIO();
		if (io.WantTextInput) {
			return false;
		}
		if (io.KeyCtrl != a_ctrl || io.KeyShift != a_shift) {
			return false;
		}
		return ImGui::IsKeyPressed(a_key, false);
	}

	bool BeginPanel(const char* a_id, ImVec2 a_size)
	{
		ImGui::PushStyleColor(ImGuiCol_ChildBg, Th().bg1);
		const bool open = ImGui::BeginChild(a_id, a_size, ImGuiChildFlags_None, ImGuiWindowFlags_None);
		ImGui::PopStyleColor();
		return open;
	}

	void EndPanel()
	{
		ImGui::EndChild();
	}
}
