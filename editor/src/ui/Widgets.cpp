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
#include <string_view>
#include <unordered_map>
#include <utility>
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

		// Where a label's trailing count starts ("Checks 2" -> "2"), nullptr when the label ends in a word.
		const char* CountTail(const char* a_label, const char* a_end)
		{
			const char* p = a_end;
			while (p > a_label && p[-1] >= '0' && p[-1] <= '9') {
				--p;
			}
			return p < a_end && p > a_label && p[-1] == ' ' ? p : nullptr;
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
			int         badgeOf{ -1 };          // >= 0 and not `badge`: "badge of badgeOf"
			bool        badgeNeutral{ false };  // a count (muted) instead of an alert (accent)
			bool        badgeZero{ false };     // draw "0" too
			const char* tooltip{ nullptr };
			const char* shortcut{ nullptr };
		};

		// The flat button's metrics, shared by FlatButton and FlatWidth (a row reserving room for it).
		struct FlatLayout
		{
			const char* label{ nullptr };
			const char* end{ nullptr };
			bool        hasText{ false };
			float       padX{ 0.0f };
			float       chevW{ 0.0f };
			ImVec2      ts{ 0, 0 };
			char        badge[16]{};
			float       badgeW{ 0.0f };
			float       badgeH{ 0.0f };
			float       badgePx{ 0.0f };
			ImFont*     badgeFont{ nullptr };
			float       w{ 0.0f };
		};

		FlatLayout MeasureFlat(const FlatSpec& a_s)
		{
			const Theme& t = Th();
			FlatLayout   l;
			l.label = a_s.label ? a_s.label : "";
			l.end = LabelEnd(l.label);
			l.hasText = l.end > l.label;
			ImFont*     font = ImGui::GetFont();
			const float h = t.inputH;
			const float is = t.iconSize;
			l.padX = l.hasText ? 8.0f * t.scale : (h - is) * 0.5f;  // a label 8 px in: a tab's text on its row's keyline
			l.chevW = 9.0f * t.scale;
			l.ts = l.hasText ? TextSize(font, l.label, l.end) : ImVec2(0, 0);
			l.badgeFont = t.smallFont ? t.smallFont : font;
			l.badgePx = kBadgePx * t.scale;
			l.badgeH = l.badgePx;
			if (a_s.badge > 0 || (a_s.badgeZero && a_s.badge == 0)) {
				if (a_s.badgeOf >= 0 && a_s.badgeOf != a_s.badge) {
					std::snprintf(l.badge, sizeof(l.badge), "%d of %d", a_s.badge, a_s.badgeOf);
				} else {
					std::snprintf(l.badge, sizeof(l.badge), "%d", a_s.badge);
				}
				// Plain text after the label, no pill (EDITOR-UX.md §3.5).
				l.badgeW = std::round(l.badgeFont->CalcTextSizeA(l.badgePx, FLT_MAX, 0.0f, l.badge).x);
			}
			float w = l.padX * 2.0f;
			if (a_s.hasIcon) {
				w += is;
			}
			if (l.hasText) {
				w += l.ts.x + (a_s.hasIcon ? 6.0f * t.scale : 0.0f);
			}
			if (l.badgeW > 0.0f) {
				w += l.badgeW + 5.0f * t.scale;
			}
			if (a_s.chevron) {
				w += l.chevW + 5.0f * t.scale;
			}
			l.w = std::round(w);
			return l;
		}

		bool FlatButton(const FlatSpec& a_s)
		{
			const Theme&      t = Th();
			const FlatLayout  l = MeasureFlat(a_s);
			const char* const label = l.label;
			const char* const end = l.end;
			const bool        hasText = l.hasText;
			ImFont*           font = ImGui::GetFont();
			const float       h = t.inputH;
			const float       is = t.iconSize;
			const float       padX = l.padX;
			const ImVec2      ts = l.ts;
			const char* const badge = l.badge;
			const float       badgeW = l.badgeW;
			ImFont*           badgeFont = l.badgeFont;
			const float       badgePx = l.badgePx;
			Hit               hit;
			if (!Item(hasText ? label : a_s.id, ImVec2(l.w, h), hit, ImGui::GetStyle().FramePadding.y)) {
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
				// The count as quiet text after the label: muted (text when the tab is active), accent for an alert.
				x += 5.0f * t.scale;
				const ImVec2 bs = badgeFont->CalcTextSizeA(badgePx, FLT_MAX, 0.0f, badge);
				dl->AddText(badgeFont, badgePx, ImVec2(std::floor(x), std::floor(bb.Min.y + (h - bs.y) * 0.5f)),
					a_s.badgeNeutral ? (a_s.active ? t.text : t.muted) : t.accent, badge);
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
			// A chip to switch (ChipButton): on shows a check before the text, off is quiet.
			const bool   toggle = a_button && !a_close;
			const bool   check = toggle && a_active;
			const float  checkS = std::round(12.0f * t.scale);
			const float  checkW = check ? checkS + 4.0f * t.scale : 0.0f;
			const ImVec2 size(std::round(ts.x + 2.0f * padX + checkW + closeW - (a_close ? 3.0f * t.scale : 0.0f)), h);
			// Centre the chip on a framed line (an input): text tops align with SameLine'd frames and text.
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
			// On reads by the check, the full border and the brighter text: the fill stays gray-5.
			ImU32        fill = t.bg2;
			if (a_button && hit.hovered) {
				fill = t.bg3;
			}
			const float r = h * 0.5f;
			dl->AddRectFilled(bb.Min, bb.Max, fill, r);
			ImU32 border = WithAlpha(col, 0.8f);
			ImU32 textCol = a_kind == Kind::Neutral ? (a_active ? t.text : t.muted) : col;
			if (toggle) {
				border = a_active || hit.hovered ? col : WithAlpha(col, 0.5f);
				textCol = a_active ? t.text : t.muted;
			}
			dl->AddRect(bb.Min, bb.Max, border, r, 0, 1.0f);
			float tx = bb.Min.x + padX;
			if (check) {
				DrawIcon(dl, Icon::Check, ImVec2(tx - 1.0f * t.scale, std::round(bb.Min.y + (h - checkS) * 0.5f)), checkS, col);
				tx += checkW;
			}
			Text(dl, font, ImVec2(tx, bb.Min.y + (h - ts.y) * 0.5f), textCol, a_text, end);
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
		double                 g_layoutClickUntil = -1.0;  // LayoutClicked: a double click's second click is taken until then
		bool                   g_layoutClickHeld = false;   // that second click is down: its release is taken too
	}

	// ---- Text ----

	float Wordmark(ImDrawList* a_dl, ImVec2 a_pos, float a_px, ImU32 a_col, const char* a_text)
	{
		// Cinzel capitals tracked 0.30 em, as the art set's wordmark; width without drawing when a_dl is null.
		const Theme& t = Th();
		ImFont*      font = t.title ? t.title : ImGui::GetFont();
		const float  px = a_px > 0.0f ? a_px : font->LegacySize;
		const float  tracking = std::round(px * 0.30f);
		ImFontBaked* baked = font->GetFontBaked(px);
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
			if (c < 0x250) {
				c = static_cast<unsigned int>(std::towupper(static_cast<wint_t>(c)));
			}
			if (!first) {
				x += tracking;
			}
			first = false;
			if (a_dl) {
				font->RenderChar(a_dl, px, ImVec2(x, std::floor(a_pos.y)), a_col, static_cast<ImWchar>(c));
			}
			x += baked->GetCharAdvance(static_cast<ImWchar>(c));
		}
		return x - std::floor(a_pos.x);
	}

	void Title(const char* a_text)
	{
		const Theme& t = Th();
		ImFont*      font = t.title ? t.title : ImGui::GetFont();
		const float  px = font->LegacySize;
		const float  w = Wordmark(nullptr, ImVec2(0, 0), px, 0, a_text);
		ImRect       bb;
		if (!Static(ImVec2(w, px), bb, 0.0f)) {
			return;
		}
		Wordmark(ImGui::GetWindowDrawList(), bb.Min, px, t.accent, a_text);
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

	void Mono(const char* a_text, ImU32 a_colour)
	{
		// The mono line is shorter than the line it sits in (a table row, a label after SameLine): it goes on that
		// line's baseline, in a line at least as tall, so a form ref stays level with the text beside it.
		ImGuiWindow* win = ImGui::GetCurrentWindow();
		if (win->SkipItems) {
			return;
		}
		ImFont*      font = FontOf(Font::Mono);
		const float  px = font->LegacySize;
		const float  lineH = ImGui::GetTextLineHeight();
		const float  dy = std::max(0.0f, std::round(ImGui::GetFontBaked()->Ascent - font->GetFontBaked(px)->Ascent));
		const ImVec2 pos(win->DC.CursorPos.x, LineTextTop());
		const float  wrapX = win->DC.TextWrapPos;
		const float  wrap = wrapX >= 0.0f ? ImGui::CalcWrapWidthForPos(pos, wrapX) : 0.0f;
		const ImVec2 ts = font->CalcTextSizeA(px, FLT_MAX, wrap, a_text);
		const ImVec2 size(ts.x, std::max(lineH, dy + ts.y));
		ImGui::ItemSize(size, 0.0f);
		if (!ImGui::ItemAdd(ImRect(pos, pos + size), 0)) {
			return;
		}
		win->DrawList->AddText(font, px, detail::Floor(ImVec2(pos.x, pos.y + dy)), a_colour ? a_colour : ImGui::GetColorU32(ImGuiCol_Text), a_text, nullptr, wrap);
	}

	// Where the last section head left the cursor, so a field label straight under it adds no space of its own: a
	// head groups with what it heads.
	static const ImGuiWindow* g_headWindow = nullptr;
	static float              g_headEndY = -1.0f;
	static int                g_headFrame = -1;

	void SectionHead(const char* a_text, const char* a_tooltip, const char* a_count)
	{
		const Theme& t = Th();
		ImFont*      font = t.smallFont ? t.smallFont : ImGui::GetFont();
		const float  tracking = std::round(font->LegacySize * 0.24f);  // the art set's caps tracking
		const float  avail = ImGui::GetContentRegionAvail().x;
		const float  h = std::round(font->LegacySize + 2.0f * t.scale);
		// A head parts its section from the one above; as a window's first item the window's padding is that space.
		const ImGuiWindow* win = ImGui::GetCurrentWindowRead();
		if (win->DC.CursorPos.y > win->DC.CursorStartPos.y + 0.5f) {
			ImGui::Dummy(ImVec2(0.0f, 4.0f * t.scale));
		}
		ImRect bb;
		if (!Static(ImVec2(std::max(avail, 1.0f), h), bb, 0.0f)) {
			ImGui::Dummy(ImVec2(0.0f, 2.0f * t.scale));
			g_headWindow = win;
			g_headEndY = win->DC.CursorPos.y;
			g_headFrame = ImGui::GetFrameCount();
			return;
		}
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		const float  cy = std::floor(bb.Min.y + h * 0.5f) + 0.5f;
		const float  tx = bb.Min.x;
		const bool   hasCount = a_count && *a_count;
		const float  px = kBadgePx * t.scale;
		const ImVec2 cs = hasCount ? font->CalcTextSizeA(px, FLT_MAX, 0.0f, a_count) : ImVec2();
		// Too long for the body (a long count at a large text size): the hairline goes first, then the capitals close
		// up to a third of their tracking, then the info mark goes and the head itself carries the tooltip. The words
		// are the last to clip.
		const float wide = Tracked(nullptr, font, ImVec2(), 0, a_text, tracking, true);
		const float gaps = tracking > 0.0f ? (wide - Tracked(nullptr, font, ImVec2(), 0, a_text, 0.0f, true)) / tracking : 0.0f;
		const float countW = hasCount ? 6.0f * t.scale + cs.x : 0.0f;
		auto        fit = [&](float a_rest) {  // the tracking that fits beside a_rest, a third of the usual at least
			const float over = wide + a_rest - avail;
			return over <= 0.0f || gaps <= 0.0f ? tracking : std::max(std::round(tracking / 3.0f), std::floor(tracking - over / gaps));
		};
		bool  mark = a_tooltip && *a_tooltip;
		float track = fit(countW + (mark ? 18.0f * t.scale : 0.0f));
		if (mark && wide - gaps * (tracking - track) + countW + 18.0f * t.scale > avail) {
			mark = false;
			track = fit(countW);
		}
		const float tw = Tracked(dl, font, ImVec2(tx, bb.Min.y + (h - font->LegacySize) * 0.5f), t.muted, a_text, track, true);
		float       x0 = std::round(tx + tw + 8.0f * t.scale);
		if (hasCount) {
			// The count as the drawer's tabs draw theirs: small, muted, not tracked.
			dl->AddText(font, px, ImVec2(std::floor(x0 - 2.0f * t.scale), std::floor(bb.Min.y + (h - cs.y) * 0.5f)), t.muted, a_count);
			x0 = std::round(x0 - 2.0f * t.scale + cs.x + 8.0f * t.scale);
		}
		if (a_tooltip && *a_tooltip && !mark) {
			if (ImGui::IsMouseHoveringRect(bb.Min, bb.Max) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
				ImGui::SetTooltip("%s", a_tooltip);
			}
		} else if (a_tooltip && *a_tooltip) {
			const float  is = std::round(12.0f * t.scale);
			const ImVec2 ip(x0 - 2.0f * t.scale, std::round(bb.Min.y + (h - is) * 0.5f));
			const bool   over = ImGui::IsMouseHoveringRect(bb.Min, ImVec2(ip.x + is, bb.Max.y)) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
			DrawIcon(dl, Icon::Info, ip, is, over ? t.text : t.muted);
			if (over) {
				ImGui::SetTooltip("%s", a_tooltip);
			}
			x0 = ip.x + is + 6.0f * t.scale;
		}
		if (x0 < bb.Max.x) {
			dl->AddRectFilled(ImVec2(x0, std::floor(cy)), ImVec2(bb.Max.x, std::floor(cy) + 1.0f), t.line);
		}
		ImGui::Dummy(ImVec2(0.0f, 2.0f * t.scale));
		g_headWindow = win;
		g_headEndY = win->DC.CursorPos.y;
		g_headFrame = ImGui::GetFrameCount();
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
		ImGui::Dummy(ImVec2(0.0f, a_px > 0.0f ? a_px : 8.0f * Th().scale));  // 4 + 8 + 4: a 16 px step on the grid
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
		// A count is plain text in its colour, never a filled pill (EDITOR-UX.md §3.5).
		const float  px = kBadgePx * t.scale;
		const ImVec2 ts = font->CalcTextSizeA(px, FLT_MAX, 0.0f, a_text);
		const float  h = std::round(ts.y);
		const float  w = std::round(ts.x);
		ImRect       bb;
		if (!Static(ImVec2(w, h), bb, (h - ImGui::GetFontSize()) * 0.5f)) {
			return;
		}
		ImGui::GetWindowDrawList()->AddText(font, px, detail::Floor(bb.Min), a_kind == Kind::Neutral ? t.muted : Colour(a_kind), a_text);
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

	bool TabButton(const char* a_label, bool a_active, int a_count, const char* a_tooltip, const char* a_shortcut, int a_of)
	{
		FlatSpec s;
		s.label = a_label;
		s.active = a_active;
		s.badge = a_count;
		s.badgeOf = a_of;
		s.badgeNeutral = true;
		s.badgeZero = a_count == 0;
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
			const ImU32 fill = hit.held ? t.accentDim : hit.hovered ? t.text : t.accent;
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
		const ImU32 col = hovered ? t.text : t.accent;
		Text(dl, font, bb.Min, col, a_label, end);
		if (hovered) {
			const float y = std::floor(bb.Max.y);
			dl->AddRectFilled(ImVec2(bb.Min.x, y - 1.0f), ImVec2(bb.Max.x, y), col);
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		}
		return pressed;
	}

	namespace
	{
		// Button's look: bg2, a 1 px line border, hover bg3; the label centred, or wrapped from the left in a fixed width.
		bool FramedButton(const char* a_label, const Icon* a_icon, Kind a_kind, const char* a_tooltip, const char* a_shortcut, float a_width)
		{
			const Theme& t = Th();
			ImFont*      font = ImGui::GetFont();
			const char*  end = LabelEnd(a_label);
			const float  padX = 12.0f * t.scale;
			const float  is = t.iconSize;
			const float  iconW = a_icon ? is + 6.0f * t.scale : 0.0f;
			const ImVec2 one = TextSize(font, a_label, end);
			const float  w = a_width > 0.0f ? std::round(a_width) : std::round(one.x + iconW + 2.0f * padX);
			const float  wrap = std::max(1.0f, w - 2.0f * padX - iconW);
			const bool   wrapped = one.x > wrap + 0.5f;
			const ImVec2 ts = wrapped ? font->CalcTextSizeA(font->LegacySize, FLT_MAX, wrap, a_label, end) : one;
			const float  h = std::max(t.inputH, std::round(ts.y + t.inputH - one.y));
			const bool   disabled = (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
			Hit          hit;
			if (!Item(a_label, ImVec2(w, h), hit, ImGui::GetStyle().FramePadding.y)) {
				return false;
			}
			ImDrawList*   dl = ImGui::GetWindowDrawList();
			const ImRect& bb = hit.bb;
			dl->AddRectFilled(bb.Min, bb.Max, !disabled && (hit.hovered || hit.held) ? t.bg3 : t.bg2, t.radius);
			dl->AddRect(bb.Min, bb.Max, t.line, t.radius, 0, 1.0f);
			const ImU32 col = disabled ? (t.disabled ? t.disabled : t.muted) : a_kind == Kind::Error ? t.error : t.text;
			float       x = wrapped ? bb.Min.x + padX : std::round(bb.Min.x + (w - iconW - ts.x) * 0.5f);
			if (a_icon) {
				DrawIcon(dl, *a_icon, ImVec2(x, std::round(bb.Min.y + (h - is) * 0.5f)), is, col);
				x += iconW;
			}
			dl->AddText(font, font->LegacySize, detail::Floor(ImVec2(x, bb.Min.y + (h - ts.y) * 0.5f)), col, a_label, end, wrapped ? wrap : 0.0f);
			TipShortcut(a_tooltip, a_shortcut);
			return hit.pressed && !disabled;
		}
	}

	bool Button(const char* a_label, const char* a_tooltip, const char* a_shortcut, Kind a_kind, float a_width)
	{
		return FramedButton(a_label, nullptr, a_kind, a_tooltip, a_shortcut, a_width);
	}

	bool Button(const char* a_label, Icon a_icon, Kind a_kind, const char* a_tooltip, float a_width)
	{
		return FramedButton(a_label, &a_icon, a_kind, a_tooltip, nullptr, a_width);
	}

	bool BeginCombo(const char* a_id, const char* a_preview, ImGuiComboFlags a_flags)
	{
		const Theme& t = Th();
		// The frame's place, taken before the combo opens its popup (then the current window is the popup's).
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const ImVec2 q(p.x + ImGui::CalcItemWidth(), p.y + ImGui::GetFrameHeight());
		const bool   open = ImGui::BeginCombo(a_id, a_preview, a_flags | ImGuiComboFlags_NoArrowButton);
		const bool   hovered = !open && ImGui::IsItemHovered();
		const bool   hot = open || hovered;
		if (!ImGui::GetCurrentWindowRead()->SkipItems || open) {
			const float cs = std::round(11.0f * t.scale);
			const float x = std::round(q.x - t.pad - cs);
			// A long preview runs to the frame's end: the chevron gets its own patch of the frame.
			dl->AddRectFilled(ImVec2(x - 4.0f * t.scale, p.y + 1.0f), ImVec2(q.x - 1.0f, q.y - 1.0f),
				ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), t.radius, ImDrawFlags_RoundCornersRight);
			DrawIcon(dl, Icon::ChevronDown, ImVec2(x, std::round(p.y + (q.y - p.y - cs) * 0.5f)), cs, hot ? t.text : t.muted);
		}
		return open;
	}

	void EndCombo()
	{
		ImGui::EndCombo();
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
		// 12 px each side of a label; a narrow panel (the inspector at a large text size) gets less, down to 4 px.
		float text = 0.0f;
		for (const char* l : a_labels) {
			text += TextSize(font, l, LabelEnd(l)).x;
		}
		const float        n = static_cast<float>(a_labels.size());
		const float        padX = std::clamp((ImGui::GetContentRegionAvail().x - text) / (2.0f * n), 4.0f * t.scale, 12.0f * t.scale);
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
				const ImVec2 tp(hit.bb.Min.x + (widths[i] - ts.x) * 0.5f, hit.bb.Min.y + (h - ts.y) * 0.5f);
				// A label that ends in a number ("Checks 2") carries an alert count: the figure in the warn colour
				// whatever the segment's state, the same hue the status row gives that warning (§3.5).
				const char* count = CountTail(a_labels[i], end);
				if (count) {
					Text(dl, font, tp, col, a_labels[i], count);
					Text(dl, font, ImVec2(tp.x + TextSize(font, a_labels[i], count).x, tp.y), t.warn, count, end);
				} else {
					Text(dl, font, tp, col, a_labels[i], end);
				}
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
		ImGui::PushStyleColor(ImGuiCol_NavCursor, IM_COL32(0, 0, 0, 0));  // the box's own border is the focus ring
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, ImGui::GetStyle().FramePadding.y));
		// A hint wider than the box drops its last ", part" until it fits ("Home, mod, plugin"), else "Search": never cut mid-word.
		std::string hint = a_hint ? a_hint : "";
		const float hintW = w - iconW - clearW;
		while (!hint.empty() && ImGui::CalcTextSize(hint.c_str()).x > hintW) {
			const auto comma = hint.rfind(',');
			if (comma == std::string::npos) {
				hint = ImGui::CalcTextSize("Search").x <= hintW ? "Search" : "";
				break;
			}
			hint.resize(comma);
		}
		bool changed = ImGui::InputTextWithHint("##s", hint.c_str(), a_buf, a_size, ImGuiInputTextFlags_EscapeClearsAll);
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(5);
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
		// The shared body of Check and Radio: a 16 px mark and the label, centred in a 20 px row (with the 4 px item
		// spacing, a list of them steps 24 px, on the grid).
		bool MarkItem(const char* a_label, bool a_on, bool a_round)
		{
			const Theme& t = Th();
			ImFont*      font = ImGui::GetFont();
			const char*  end = LabelEnd(a_label);
			const ImVec2 ts = TextSize(font, a_label, end);
			const float  box = std::round(16.0f * t.scale);
			const float  h = std::max(ts.y, std::round(20.0f * t.scale));
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

	bool Check(const char* a_label, bool& a_value, const char* a_tooltip)
	{
		const bool pressed = MarkItem(a_label, a_value, false);
		Tip(a_tooltip);
		if (pressed) {
			a_value = !a_value;
			ImGui::MarkItemEdited(ImGui::GetItemID());
		}
		return pressed;
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

	bool Slider(const char* a_label, float& a_value, float a_min, float a_max, const char* a_format, bool a_log, const char* a_tooltip)
	{
		// Label left, value right, then a thin track with a round grab: drag or click it, or roll the wheel over it.
		const Theme& t = Th();
		ImGui::PushID(a_label);
		const float lh = ImGui::GetTextLineHeight();
		const float trackH = std::round(14.0f * t.scale);
		const float w = std::max(ImGui::CalcItemWidth(), 40.0f * t.scale);
		char        value[64]{};
		std::snprintf(value, sizeof(value), a_format ? a_format : "%.2f", static_cast<double>(a_value));
		ImRect head;
		if (!Static(ImVec2(w, lh), head)) {
			ImGui::PopID();
			return false;
		}
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImFont*     font = t.smallFont ? t.smallFont : ImGui::GetFont();
		Text(dl, font, ImVec2(head.Min.x, std::round(head.Min.y + (lh - TextSize(font, a_label).y) * 0.5f)), t.muted, a_label);
		const ImVec2 vs = TextSize(font, value);
		Text(dl, font, ImVec2(std::round(head.Max.x - vs.x), std::round(head.Min.y + (lh - vs.y) * 0.5f)), t.text, value);
		Hit hit;
		if (!Item("track", ImVec2(w, trackH), hit, 0.0f)) {
			ImGui::PopID();
			return false;
		}
		Tip(a_tooltip);
		const float lo = a_log ? std::log(std::max(a_min, 1e-6f)) : a_min;
		const float hi = a_log ? std::log(std::max(a_max, 1e-6f)) : a_max;
		auto        toT = [&](float v) { return std::clamp(((a_log ? std::log(std::max(v, 1e-6f)) : v) - lo) / std::max(hi - lo, 1e-6f), 0.0f, 1.0f); };
		auto        fromT = [&](float f) {
            const float v = lo + std::clamp(f, 0.0f, 1.0f) * (hi - lo);
            return std::clamp(a_log ? std::exp(v) : v, a_min, a_max);
		};
		const float r = std::round(5.0f * t.scale);
		const float x0 = hit.bb.Min.x + r, x1 = hit.bb.Max.x - r;
		bool        changed = false;
		if (hit.held) {
			const float nv = fromT((ImGui::GetIO().MousePos.x - x0) / std::max(x1 - x0, 1.0f));
			changed = nv != a_value;
			a_value = nv;
		} else if (hit.hovered && ImGui::GetIO().MouseWheel != 0.0f) {
			a_value = fromT(toT(a_value) + ImGui::GetIO().MouseWheel * 0.02f);
			changed = true;
		}
		if (changed) {
			ImGui::MarkItemEdited(hit.id);
		}
		const float cy = std::round(hit.bb.GetCenter().y);
		const float gx = std::round(x0 + toT(a_value) * (x1 - x0));
		const bool  hot = hit.hovered || hit.held;
		dl->AddRectFilled(ImVec2(x0, cy - 1.0f), ImVec2(x1, cy + 1.0f), t.line, 1.0f);  // the empty track in the hairline, so the range shows
		dl->AddRectFilled(ImVec2(x0, cy - 1.0f), ImVec2(gx, cy + 1.0f), hot ? t.accent : t.accentDim, 1.0f);
		dl->AddCircleFilled(ImVec2(gx, cy), r, t.bg1, 20);
		dl->AddCircle(ImVec2(gx, cy), r, hot ? t.accent : t.accentDim, 20, 1.5f);
		ImGui::PopID();
		return changed;
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
		// The art set's divider: the emblem as a small bullet with a hairline rule on each side.
		const Theme& t = Th();
		const float  avail = ImGui::GetContentRegionAvail().x;
		const float  w = std::min(avail, a_width > 0.0f ? a_width : 160.0f * t.scale);
		const float  e = std::round(14.0f * t.scale);
		const float  h = e + 2.0f * t.scale;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, std::floor((avail - w) * 0.5f)));
		ImRect bb;
		if (!Static(ImVec2(w, h), bb, 0.0f)) {
			return;
		}
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		const ImVec2 c(std::floor(bb.GetCenter().x), std::floor(bb.GetCenter().y));
		const float  gap = 10.0f * t.scale;
		const float  y = std::floor(c.y);
		dl->AddRectFilled(ImVec2(bb.Min.x, y), ImVec2(c.x - e * 0.5f - gap, y + 1.0f), t.line);
		dl->AddRectFilled(ImVec2(c.x + e * 0.5f + gap, y), ImVec2(bb.Max.x, y + 1.0f), t.line);
		DrawIcon(dl, Icon::Emblem, ImVec2(c.x - e * 0.5f, c.y - e * 0.5f), e, t.muted);
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

	namespace
	{
		FlatSpec PopoverSpec(const char* a_label, Icon a_icon, int a_badge)
		{
			const bool hasLabel = a_label && *a_label;
			FlatSpec   s;
			s.label = hasLabel ? a_label : nullptr;
			s.id = "btn";
			s.hasIcon = !hasLabel || a_icon != Icon::ChevronDown;
			s.icon = a_icon;
			s.chevron = hasLabel;
			s.badge = a_badge;
			return s;
		}
	}

	float PopoverWidth(const char* a_label, Icon a_icon, int a_badge)
	{
		return MeasureFlat(PopoverSpec(a_label, a_icon, a_badge)).w;
	}

	bool BeginPopover(const char* a_id, const char* a_label, Icon a_icon, int a_badge, const char* a_tooltip, const char* a_shortcut)
	{
		const Theme& t = Th();
		ImGui::PushID(a_id);
		const bool wasOpen = ImGui::IsPopupOpen("pop");
		FlatSpec   s = PopoverSpec(a_label, a_icon, a_badge);
		s.open = wasOpen;
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
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(t.pad, t.pad));  // on the grid, like every other padding
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
				LayoutClicked();
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
		// Resize handle on the inner edge. The right rail's content starts just past its 1 px edge, so its padding
		// matches the window edge's 16 px; the handle lies under the content's left padding and takes the mouse there
		// through FlattenChildren. The other rails keep their content beside the handle.
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
		const ImGuiID resizeId = ImGui::GetID("resize");
		bool          hov = false, held = false;
		if (ImGui::ItemAdd(handle, resizeId)) {
			ImGui::ButtonBehavior(handle, resizeId, &hov, &held, ImGuiButtonFlags_FlattenChildren);
		}
		const bool active = held;
		const bool hot = active || hov;
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
			cmin.x += 1.0f;
			csize.x -= 1.0f;
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
			// Level with the footer's Rescan line, the row it belongs to.
			p = ImVec2(mn.x + sz.x - b - m - sb, mn.y + sz.y - b - 16.0f * t.scale);
			icon = Icon::ChevronLeft;
			break;
		case Side::Right:
			// Level with the rail's chevron and the footer's Rescan line: one keyline across the bottom corners.
			p = ImVec2(mn.x + m + 4.0f * t.scale, mn.y + sz.y - b - 16.0f * t.scale);
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
			LayoutClicked();
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
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, t.radius);
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
		Rim(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), Th().radius);
	}

	void HintLine(const char* a_text, ImVec2 a_viewportMin, ImVec2 a_viewportMax)
	{
		if (!a_text || !*a_text) {
			return;
		}
		const Theme& t = Th();
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		ImFont*      font = t.smallFont ? t.smallFont : ImGui::GetFont();
		ImVec2 ts = TextSize(font, a_text);
		// The plate's edges sit 8 px in from the viewport's left and bottom, the floating toolbar's inset.
		const ImVec2 p(a_viewportMin.x + t.pad + t.pad * 0.5f, a_viewportMax.y - ts.y - t.pad - t.pad * 0.5f);
		// A narrow view drops whole "Key: action" pairs from the end rather than cutting a word; only a single pair
		// too long for the view ends in "...".
		const float      right = a_viewportMax.x - t.pad - t.pad * 0.5f;
		const char*      end = a_text + std::strlen(a_text);
		std::string_view line(a_text, end);
		constexpr std::string_view kSep = " \xC2\xB7 ";  // " · "
		while (p.x + ts.x > right) {
			const auto cut = line.rfind(kSep);
			if (cut == std::string_view::npos) {
				break;
			}
			line = line.substr(0, cut);
			end = a_text + line.size();
			ts = TextSize(font, a_text, end);
		}
		{
			// A dark plate behind the text alone, so it reads over a bright scene (the viewport's colour: bg0 in a dark
			// theme, and dark in a light one, as the text on it is light).
			const ImVec2 b0 = detail::Floor(p - ImVec2(t.pad * 0.5f, t.pad * 0.5f));
			const ImVec2 b1 = detail::Floor(ImVec2(std::min(p.x + ts.x, right) + t.pad * 0.5f, p.y + ts.y + t.pad * 0.5f));
			dl->AddRectFilled(b0, b1, WithAlpha(t.viewport ? t.viewport : t.bg0, 0.75f), t.radius);
			Rim(dl, b0, b1, t.radius);
		}
		if (p.x + ts.x > right) {
			ImGui::PushFont(font, font->LegacySize);
			for (const auto& [offset, col] : { std::pair{ 1.0f, IM_COL32(0, 0, 0, 160) }, std::pair{ 0.0f, t.viewportText } }) {
				ImGui::PushStyleColor(ImGuiCol_Text, col);
				ImGui::RenderTextEllipsis(dl, detail::Floor(p + ImVec2(0, offset)), ImVec2(right, a_viewportMax.y + offset), right, a_text, end, &ts);
				ImGui::PopStyleColor();
			}
			ImGui::PopFont();
			return;
		}
		Text(dl, font, p + ImVec2(0, 1), IM_COL32(0, 0, 0, 160), a_text, end);
		Text(dl, font, p, t.viewportText, a_text, end);
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
			dl->AddRectFilled(bb.Min, bb.Max, t.line, t.radius);  // a step of the ramp, hovered or not: the bar marks it
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

	int CardBodyLines(const char* a_text, float a_width)
	{
		const Theme& t = Th();
		ImFont*      smallF = t.smallFont ? t.smallFont : ImGui::GetFont();
		if (!a_text || !*a_text) {
			return 0;
		}
		const float h = smallF->CalcTextSizeA(smallF->LegacySize, FLT_MAX, std::round(a_width) - 24.0f * t.scale, a_text).y;  // a card's 12 px sides
		return std::max(1, static_cast<int>(std::ceil(h / smallF->LegacySize - 0.01f)));
	}

	bool Card(const char* a_id, const char* a_title, const char* a_text, const char* a_shortcut, float a_width, int a_bodyLines)
	{
		const Theme& t = Th();
		ImFont*      heading = t.heading ? t.heading : ImGui::GetFont();
		ImFont*      smallF = t.smallFont ? t.smallFont : ImGui::GetFont();
		const float  w = std::round(a_width > 0.0f ? a_width : ImGui::GetContentRegionAvail().x);
		const float  pad = 12.0f * t.scale;
		const ImVec2 bodySz = (a_text && *a_text) ? smallF->CalcTextSizeA(smallF->LegacySize, FLT_MAX, w - 2.0f * pad, a_text) : ImVec2(0, 0);
		const float  titleH = heading->LegacySize;
		// The body takes a_bodyLines lines' room even when it fills fewer, so a row of cards shares one bottom edge.
		const float  bodyH = bodySz.y > 0.0f ? std::max(bodySz.y, static_cast<float>(a_bodyLines) * smallF->LegacySize) : 0.0f;
		const float  h = std::round(pad * 2.0f + titleH + (bodyH > 0.0f ? 4.0f * t.scale + bodyH : 0.0f));
		Hit          hit;
		if (!Item(a_id, ImVec2(w, h), hit, pad)) {
			return false;
		}
		ImDrawList*   dl = ImGui::GetWindowDrawList();
		const ImRect& bb = hit.bb;
		const float   r = t.radius;
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

	// ---- Workspace pieces ----

	void Coloured(Kind a_kind, const char* a_text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, a_kind == Kind::Neutral ? Th().muted : Colour(a_kind));
		ImGui::TextUnformatted(a_text);
		ImGui::PopStyleColor();
	}

	void ColouredWrapped(Kind a_kind, const char* a_text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, a_kind == Kind::Neutral ? Th().muted : Colour(a_kind));
		ImGui::TextWrapped("%s", a_text);
		ImGui::PopStyleColor();
	}

	void FieldLabel(const char* a_text)
	{
		// 4 px more above than below (8 against 4, with the item spacing), so a label reads with its own field
		// and not with the control above. Not after SameLine (two labels side by side share one line), and not
		// as a window's first item (the window's padding is the space there), and not straight under a section head
		// (the head groups with its fields).
		const ImGuiWindow* win = ImGui::GetCurrentWindowRead();
		const bool         underHead = win == g_headWindow && g_headFrame == ImGui::GetFrameCount() && win->DC.CursorPos.y == g_headEndY;
		if (!win->DC.IsSameLine && !underHead && win->DC.CursorPos.y > win->DC.CursorStartPos.y + 0.5f) {
			ImGui::Dummy(ImVec2(0.0f, 4.0f * Th().scale));
		}
		PushFont(Font::Small);
		ImGui::PushStyleColor(ImGuiCol_Text, Th().muted);
		ImGui::TextUnformatted(a_text);
		ImGui::PopStyleColor();
		PopFont();
	}

	Kind ConfidenceKind(float a_confidence)
	{
		return a_confidence >= 0.75f ? Kind::Ok : a_confidence >= 0.5f ? Kind::Warn : Kind::Error;
	}

	namespace
	{
		// A status at the right of a rail row: a 6 px dot in its colour, then the word in muted; a Neutral one is the
		// word alone (EDITOR-UX.md §3.5: no pill per row). `a_dl` null: the width only.
		float StatusW(const char* a_text, Kind a_kind)
		{
			const Theme& t = Th();
			const float  dot = a_kind == Kind::Neutral ? 0.0f : 6.0f * t.scale + 6.0f * t.scale;
			return std::round(dot + TextSize(t.smallFont ? t.smallFont : ImGui::GetFont(), a_text).x);
		}

		void DrawStatusAt(ImDrawList* a_dl, ImVec2 a_min, float a_rowH, const char* a_text, Kind a_kind)
		{
			const Theme& t = Th();
			ImFont*      font = t.smallFont ? t.smallFont : ImGui::GetFont();
			const ImVec2 ts = TextSize(font, a_text);
			float        x = std::floor(a_min.x);
			if (a_kind != Kind::Neutral) {
				const float r = 3.0f * t.scale;
				a_dl->AddCircleFilled(ImVec2(x + r, std::floor(a_min.y + a_rowH * 0.5f) + 0.5f), r, Colour(a_kind), 16);
				x += 2.0f * r + 6.0f * t.scale;
			}
			Text(a_dl, font, ImVec2(x, std::floor(a_min.y + (a_rowH - ts.y) * 0.5f)), t.muted, a_text);
		}
	}

	namespace
	{
		// Where a home row's name starts: the row's padding, or under a GroupHeader the group label's keyline (its 12 px
		// chevron and 6 px gap), so a home reads as its mod's child.
		float HomeRowTextX(float a_rowX, bool a_inGroup)
		{
			const Theme& t = Th();
			return a_rowX + (a_inGroup ? std::round(18.0f * t.scale) : t.pad);
		}

		// A row left to decide alone: the status word would cut its name. The name and the summary's shortest form
		// must fit before the word, else the dot (the summary keeps its room).
		bool HomeRowNarrow(const char* a_name, const char* a_chip, Kind a_chipKind, bool a_unsaved, const char* a_summary, const char* a_summaryShort,
			const char* a_summaryShortest, bool a_inGroup)
		{
			if (!a_chip || !*a_chip) {
				return false;
			}
			const Theme& t = Th();
			const float  x = ImGui::GetCursorScreenPos().x;
			const float  right = x + std::max(ImGui::GetContentRegionAvail().x, 1.0f) - t.pad;
			const char*  shortest = a_summaryShortest && *a_summaryShortest ? a_summaryShortest : a_summaryShort && *a_summaryShort ? a_summaryShort : a_summary;
			float        need = TextSize(ImGui::GetFont(), a_name).x + (a_unsaved ? 10.0f * t.scale : 0.0f);
			if (shortest && *shortest) {
				need += t.gap + TextSize(t.smallFont ? t.smallFont : ImGui::GetFont(), shortest).x;
			}
			return HomeRowTextX(x, a_inGroup) + need > right - StatusW(a_chip, a_chipKind) - t.gap;
		}
	}

	bool HomeRow(const char* a_id, const char* a_name, const char* a_summary, const char* a_summaryShort, const char* a_chip, Kind a_chipKind, bool a_selected,
		bool a_cursor, bool a_unsaved, bool a_dim, const char* a_summaryShortest, bool a_inGroup, int a_narrow)
	{
		const Theme& t = Th();
		ImGui::PushID(a_id);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float  wdt = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
		const float  h = t.rowH;
		// A name the status word would cut (a large text size, a narrow rail): the word goes and the dot stays, a
		// quiet "none" goes altogether (the row's tooltip says it). A list decides it once for all its rows.
		const bool   narrow = a_narrow < 0 ? HomeRowNarrow(a_name, a_chip, a_chipKind, a_unsaved, a_summary, a_summaryShort, a_summaryShortest, a_inGroup)
		                                   : a_narrow > 0 && a_chip && *a_chip;
		const bool   clicked = ImGui::InvisibleButton("row", ImVec2(wdt, h));
		const bool   hovered = ImGui::IsItemHovered();
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		const ImVec2 q(p.x + wdt, p.y + h);
		if (a_selected) {
			dl->AddRectFilled(p, q, t.line, t.radius);  // a step of the ramp, hovered or not: the bar marks it
			dl->AddRectFilled(p, ImVec2(p.x + std::round(2.0f * t.scale), q.y), t.accent);
		} else if (hovered) {
			dl->AddRectFilled(p, q, t.bg3, t.radius);
		}
		if (a_cursor && !a_selected) {
			dl->AddRect(p, q, t.accentDim, t.radius);
		}
		float right = q.x - t.pad;
		if (narrow) {
			if (a_chipKind != Kind::Neutral) {
				const float r = 3.0f * t.scale;
				dl->AddCircleFilled(ImVec2(right - r, std::floor(p.y + h * 0.5f) + 0.5f), r, Colour(a_chipKind), 16);
				right -= 2.0f * r + t.gap;
			}
		} else if (a_chip && *a_chip) {
			const float sw = StatusW(a_chip, a_chipKind);
			DrawStatusAt(dl, ImVec2(right - sw, p.y), h, a_chip, a_chipKind);
			right -= sw + t.gap;
		}
		// Name, dot, summary (the short form, then an ellipsis, when it does not fit before the chip).
		const float maxX = std::max(right, p.x + 1.0f);
		dl->PushClipRect(p, ImVec2(maxX, q.y), true);
		const ImVec2 ns = TextSize(ImGui::GetFont(), a_name);
		float        x = HomeRowTextX(p.x, a_inGroup);
		ImGui::PushStyleColor(ImGuiCol_Text, a_dim ? t.muted : t.text);
		ImGui::RenderTextEllipsis(dl, ImVec2(x, std::floor(p.y + (h - ns.y) * 0.5f)), ImVec2(maxX, q.y), maxX, a_name, nullptr, &ns);
		ImGui::PopStyleColor();
		x += ns.x;
		if (a_unsaved) {
			const float r = 3.0f * t.scale;
			dl->AddCircleFilled(ImVec2(x + r + 3.0f * t.scale, p.y + h * 0.5f), r, t.warn, 16);
			x += 2.0f * r + 4.0f * t.scale;
		}
		if (a_summary && *a_summary && x + t.gap < maxX) {
			PushFont(Font::Small);
			const char* text = a_summary;
			for (const char* shorter : { a_summaryShort, a_summaryShortest }) {
				if (ImGui::CalcTextSize(text).x > maxX - x - t.gap && shorter && *shorter) {
					text = shorter;
				}
			}
			const ImVec2 ss = ImGui::CalcTextSize(text);
			// Only whole: a summary cut to an ellipsis says nothing (the row's tooltip has it).
			if (x + t.gap + ss.x <= maxX) {
				ImGui::PushStyleColor(ImGuiCol_Text, t.muted);
				ImGui::RenderTextEllipsis(dl, ImVec2(x + t.gap, std::floor(p.y + (h - ss.y) * 0.5f)), ImVec2(maxX, q.y), maxX, text, nullptr, &ss);
				ImGui::PopStyleColor();
			}
			PopFont();
		}
		dl->PopClipRect();
		ImGui::PopID();
		return clicked;
	}

	bool GroupHeader(const char* a_id, const char* a_label, const char* a_note, bool& a_open, const char* a_noteShort)
	{
		const Theme& t = Th();
		ImGui::PushID(a_id);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float  wdt = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
		const float  h = t.rowH;
		const bool   clicked = ImGui::InvisibleButton("group", ImVec2(wdt, h));
		const bool   hovered = ImGui::IsItemHovered();
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		if (hovered) {
			dl->AddRectFilled(p, ImVec2(p.x + wdt, p.y + h), t.bg2, t.radius);
		}
		const float is = std::round(12.0f * t.scale);
		DrawIcon(dl, a_open ? Icon::ChevronDown : Icon::ChevronRight, detail::Floor(ImVec2(p.x + 2.0f * t.scale, p.y + (h - is) * 0.5f)), is, t.muted);
		// Label, then the note (Nexus id and count). The label is what the author scans for: the full note only beside
		// the whole label, else `a_noteShort` (the count) while the label keeps about 12 characters, else none. A long
		// label gets an ellipsis only after that.
		PushFont(Font::Small);
		ImVec2      ls = ImGui::CalcTextSize(a_label);
		const float x = p.x + is + 6.0f * t.scale;
		const float ty = std::floor(p.y + (h - ls.y) * 0.5f);
		std::string_view head(a_label);
		if (head.size() > 12) {
			std::size_t cut = 12;
			while (cut > 0 && (static_cast<unsigned char>(head[cut]) & 0xC0) == 0x80) {
				--cut;  // not inside a UTF-8 character
			}
			head = head.substr(0, cut);
		}
		const float keep = std::min(ls.x, ImGui::CalcTextSize(head.data(), head.data() + head.size()).x + ImGui::CalcTextSize("...").x);
		const float right = p.x + wdt - t.pad * 0.5f;
		const char* note = nullptr;
		float       nw = 0.0f;
		for (const char* option : { a_note, a_noteShort }) {
			if (option && *option) {
				const float width = ImGui::CalcTextSize(option).x + t.gap;
				if (right - width - x >= (option == a_note && a_noteShort ? ls.x : keep)) {
					note = option;
					nw = width;
					break;
				}
			}
		}
		const float maxX = std::max(x + 1.0f, right - nw);
		dl->PushClipRect(p, ImVec2(p.x + wdt, p.y + h), true);
		ImGui::PushStyleColor(ImGuiCol_Text, t.accent);
		ImGui::RenderTextEllipsis(dl, ImVec2(x, ty), ImVec2(maxX, p.y + h), maxX, a_label, nullptr, &ls);
		ImGui::PopStyleColor();
		if (note) {
			Text(dl, ImGui::GetFont(), ImVec2(std::min(x + ls.x, maxX) + t.gap, ty), t.muted, note);
		}
		dl->PopClipRect();
		PopFont();
		if (clicked) {
			a_open = !a_open;
		}
		ImGui::PopID();
		return clicked;
	}

	int Banner(const char* a_id, const char* a_text, const char* a_action)
	{
		const Theme& t = Th();
		int          result = 0;
		ImGui::PushID(a_id);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float  wdt = ImGui::GetContentRegionAvail().x;
		const float  h = t.rowH + 2.0f * t.gap;
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(p, ImVec2(p.x + wdt, p.y + h), t.bg3, t.radius);  // the hover token: a surface from the ramp
		dl->AddRect(p, ImVec2(p.x + wdt, p.y + h), t.accentDim, t.radius);
		ImGui::SetCursorScreenPos(ImVec2(p.x + t.pad, p.y + t.gap + (t.rowH - ImGui::GetTextLineHeight()) * 0.5f));
		ImGui::TextUnformatted(a_text);
		ImGui::SameLine(0.0f, t.gap);
		if (LinkButton(a_action)) {
			result = 1;
		}
		const float b = t.iconSize;
		// The text's inset, mirrored: the glyph is drawn 4/16 in from its box, so the box moves out by that bearing.
		ImGui::SetCursorScreenPos(ImVec2(p.x + wdt - b - t.pad + std::floor(b * 4.0f / 16.0f), p.y + (h - b) * 0.5f));
		if (ImGui::InvisibleButton("close", ImVec2(b, b))) {
			result = 2;
		}
		DrawIcon(dl, Icon::Close, ImGui::GetItemRectMin(), b, ImGui::IsItemHovered() ? t.text : t.muted);
		Tip("Close");
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 4.0f * t.scale));  // with the item spacing, 8 px to what follows
		ImGui::Dummy(ImVec2(0, 0));
		ImGui::PopID();
		return result;
	}

	void Swatch(std::uint32_t a_colour, const char* a_label)
	{
		const Theme& t = Th();
		// Check's measures (a 16 px square, an 8 px gap, a 20 px row), so a legend under radios shares their keyline.
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float  s = std::round(16.0f * t.scale);
		const float  h = std::max(ImGui::GetTextLineHeight(), std::round(20.0f * t.scale));
		const float  y = std::round(p.y + (h - s) * 0.5f);
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + s, y + s), a_colour, t.radius);
		ImGui::Dummy(ImVec2(s, h));
		ImGui::SameLine(0.0f, 8.0f * t.scale);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::round((h - ImGui::GetTextLineHeight()) * 0.5f));
		ImGui::TextUnformatted(a_label);
	}

	void RefBlock(const char* a_id, const std::string& a_lines, const std::string& a_copy)
	{
		const Theme& t = Th();
		ImGui::PushID(a_id);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float  wdt = ImGui::GetContentRegionAvail().x;
		const float  icon = t.inputH;
		ImFont*      mono = t.mono ? t.mono : ImGui::GetFont();
		const float  wrap = std::max(wdt - icon - 2.0f * t.pad, 1.0f);
		const ImVec2 sz = mono->CalcTextSizeA(mono->LegacySize, FLT_MAX, wrap, a_lines.c_str());
		const float  h = std::max(sz.y + 2.0f * t.gap, icon);
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(p, ImVec2(p.x + wdt, p.y + h), t.bg2, t.radius);
		dl->AddText(mono, mono->LegacySize, detail::Floor(ImVec2(p.x + t.pad, p.y + t.gap)), t.muted, a_lines.c_str(), nullptr, wrap);
		ImGui::SetCursorScreenPos(ImVec2(p.x + wdt - icon, p.y));
		if (IconButton("copy", Icon::Copy, "Copy")) {
			ImGui::SetClipboardText(a_copy.c_str());
		}
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
		ImGui::Dummy(ImVec2(wdt, 1.0f));
		ImGui::PopID();
	}

	bool BeginPadded(const char* a_id, ImVec2 a_size, ImGuiWindowFlags a_flags)
	{
		const Theme& t = Th();
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(t.pad, 4.0f * t.scale));  // 4 px on top: the first row on the grid
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
		const Theme& t = Th();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float  wdt = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
		const float  h = std::round(5.0f * t.scale);
		ImGui::InvisibleButton(a_id, ImVec2(wdt, h));
		const bool  active = ImGui::IsItemActive();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		if (ImGui::IsItemHovered() || active) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
			dl->AddRectFilled(p, ImVec2(p.x + wdt, p.y + std::round(2.0f * t.scale)), active ? t.accent : t.accentDim);
		} else {
			dl->AddRectFilled(p, ImVec2(p.x + wdt, p.y + 1.0f), t.line);
		}
		if (active) {
			a_size = std::clamp(a_size - ImGui::GetIO().MouseDelta.y, a_min, a_max);
		}
		return active;
	}

	void LayoutClicked()
	{
		const ImGuiIO& io = ImGui::GetIO();
		g_layoutClickUntil = io.MouseClickedTime[ImGuiMouseButton_Left] + static_cast<double>(io.MouseDoubleClickTime);
	}

	void ClaimLayoutDoubleClick()
	{
		const ImGuiIO& io = ImGui::GetIO();
		const ImGuiID  owner = ImGui::GetID("##layoutclick");
		if (g_layoutClickHeld) {
			// The release too: the views take a release over them as a click (View3D, Plan), wherever it was pressed.
			if (io.MouseReleased[ImGuiMouseButton_Left]) {
				ImGui::SetKeyOwner(ImGuiKey_MouseLeft, owner, ImGuiInputFlags_LockThisFrame);
			}
			g_layoutClickHeld = io.MouseDown[ImGuiMouseButton_Left];
			return;
		}
		if (g_layoutClickUntil < 0.0) {
			return;
		}
		if (io.MouseClicked[ImGuiMouseButton_Left] && io.MouseClickedCount[ImGuiMouseButton_Left] == 2) {
			ImGui::SetKeyOwner(ImGuiKey_MouseLeft, owner, ImGuiInputFlags_LockUntilRelease);  // no item sees the press
			g_layoutClickUntil = -1.0;
			g_layoutClickHeld = true;
		} else if (ImGui::GetTime() > g_layoutClickUntil) {
			g_layoutClickUntil = -1.0;
		}
	}

	float ChipWidth(const char* a_text, bool a_close, bool a_active)
	{
		const Theme& t = Th();
		// ChipImpl's size: the text, 8 px each side, the check of a ChipButton that is on, the close x.
		float w = TextSize(t.smallFont ? t.smallFont : ImGui::GetFont(), a_text, LabelEnd(a_text)).x + 16.0f * t.scale;
		if (a_active && !a_close) {
			w += std::round(12.0f * t.scale) + 4.0f * t.scale;
		}
		return std::round(a_close ? w + t.chipH - 5.0f * t.scale : w);
	}

	void FlowNext(float a_width, float a_rightX)
	{
		const float gap = 4.0f * Th().scale;
		if (ImGui::GetItemRectMax().x + gap + a_width <= a_rightX) {
			ImGui::SameLine(0.0f, gap);
		}
	}

	void CentredMessage(ImVec2 a_min, ImVec2 a_max, const char* a_text, const char* a_sub)
	{
		const Theme& t = Th();
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		ImFont*      heading = t.heading ? t.heading : ImGui::GetFont();
		ImFont*      smallF = t.smallFont ? t.smallFont : ImGui::GetFont();
		const ImVec2 sz = TextSize(heading, a_text);
		const float  cx = (a_min.x + a_max.x) * 0.5f;
		const float  cy = (a_min.y + a_max.y) * 0.5f;
		Text(dl, heading, ImVec2(cx - sz.x * 0.5f, cy - sz.y), t.muted, a_text);
		if (a_sub && *a_sub) {
			const ImVec2 s2 = TextSize(smallF, a_sub);
			Text(dl, smallF, ImVec2(cx - s2.x * 0.5f, cy + t.gap), t.muted, a_sub);
		}
	}
}
