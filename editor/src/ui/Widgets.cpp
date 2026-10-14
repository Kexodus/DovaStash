// Plain-ImGui implementation of Widgets.h: the fallback the theme pass replaces piece by piece. Every
// function works (nothing here is a stub that draws nothing), it just does not look like the spec yet.

#include "ui/Widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>

#include <imgui_internal.h>

namespace dova::editor::ui::w
{
	namespace
	{
		Theme g_theme{};
		bool  g_themeApplied{ false };

		ImU32 Rgb(std::uint32_t a_hex, int a_alpha = 255)
		{
			return IM_COL32((a_hex >> 16) & 0xFF, (a_hex >> 8) & 0xFF, a_hex & 0xFF, a_alpha);
		}

		ImVec4 V4(ImU32 a_col)
		{
			return ImGui::ColorConvertU32ToFloat4(a_col);
		}

		std::unordered_map<std::string, bool>& PopoverRequests()
		{
			static std::unordered_map<std::string, bool> s;
			return s;
		}

		void InitTokens(float a_scale)
		{
			g_theme.bg0 = Rgb(0x111113);
			g_theme.bg1 = Rgb(0x19191d);
			g_theme.bg2 = Rgb(0x222228);
			g_theme.bg3 = Rgb(0x2b2b33);
			g_theme.line = Rgb(0x33333c);
			g_theme.text = Rgb(0xe8e2d6);
			g_theme.muted = Rgb(0x9b968d);
			g_theme.accent = Rgb(0xc9a84c);
			g_theme.accentDim = Rgb(0x8a7334);
			g_theme.select = Rgb(0xffd84a);
			g_theme.ok = Rgb(0x7fbf7f);
			g_theme.warn = Rgb(0xe0b04a);
			g_theme.error = Rgb(0xd9634c);
			g_theme.info = Rgb(0x6fa8dc);
			g_theme.scale = a_scale;
			g_theme.rowH = 24.0f * a_scale;
			g_theme.inputH = 26.0f * a_scale;
			g_theme.toolbarH = 30.0f * a_scale;
			g_theme.titleH = 38.0f * a_scale;
			g_theme.statusH = 24.0f * a_scale;
			g_theme.pad = 8.0f * a_scale;
			g_theme.gap = 6.0f * a_scale;
		}
	}

	void ApplyTheme(float a_dpiScale)
	{
		InitTokens(a_dpiScale);
		ImGuiIO& io = ImGui::GetIO();
		ImGui::StyleColorsDark();
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 0.0f;
		style.FrameRounding = 3.0f;
		style.PopupRounding = 3.0f;
		style.ChildRounding = 3.0f;
		style.TabRounding = 3.0f;
		style.GrabRounding = 3.0f;
		style.FrameBorderSize = 1.0f;
		style.WindowBorderSize = 0.0f;
		style.ScaleAllSizes(a_dpiScale);
		style.FontScaleDpi = a_dpiScale;
		ImVec4* c = style.Colors;
		c[ImGuiCol_WindowBg] = V4(g_theme.bg0);
		c[ImGuiCol_ChildBg] = V4(g_theme.bg1);
		c[ImGuiCol_PopupBg] = V4(g_theme.bg2);
		c[ImGuiCol_Border] = V4(g_theme.line);
		c[ImGuiCol_FrameBg] = V4(g_theme.bg2);
		c[ImGuiCol_FrameBgHovered] = V4(g_theme.bg3);
		c[ImGuiCol_FrameBgActive] = V4(g_theme.bg3);
		c[ImGuiCol_Text] = V4(g_theme.text);
		c[ImGuiCol_TextDisabled] = V4(g_theme.muted);
		c[ImGuiCol_Button] = V4(g_theme.bg2);
		c[ImGuiCol_ButtonHovered] = V4(g_theme.bg3);
		c[ImGuiCol_ButtonActive] = V4(g_theme.accentDim);
		c[ImGuiCol_Header] = V4(g_theme.bg3);
		c[ImGuiCol_HeaderHovered] = V4(g_theme.bg3);
		c[ImGuiCol_HeaderActive] = V4(g_theme.accentDim);
		c[ImGuiCol_Tab] = V4(g_theme.bg1);
		c[ImGuiCol_TabHovered] = V4(g_theme.bg3);
		c[ImGuiCol_TabSelected] = V4(g_theme.bg2);
		c[ImGuiCol_TitleBg] = V4(g_theme.bg0);
		c[ImGuiCol_TitleBgActive] = V4(g_theme.bg0);
		c[ImGuiCol_CheckMark] = V4(g_theme.accent);
		c[ImGuiCol_SliderGrab] = V4(g_theme.accentDim);
		c[ImGuiCol_SliderGrabActive] = V4(g_theme.accent);
		c[ImGuiCol_Separator] = V4(g_theme.line);
		c[ImGuiCol_TableHeaderBg] = V4(g_theme.bg2);
		c[ImGuiCol_TableBorderLight] = V4(g_theme.line);
		c[ImGuiCol_TableBorderStrong] = V4(g_theme.line);
		c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.02f);
		c[ImGuiCol_ScrollbarBg] = V4(g_theme.bg0);
		c[ImGuiCol_ScrollbarGrab] = V4(g_theme.bg3);
		c[ImGuiCol_NavCursor] = V4(g_theme.accent);

		if (std::filesystem::exists(L"C:\\Windows\\Fonts\\segoeui.ttf")) {
			g_theme.body = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 17.0f);
			g_theme.heading = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 19.0f);
			g_theme.title = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 24.0f);
			g_theme.smallFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 15.0f);
		}
		if (std::filesystem::exists(L"C:\\Windows\\Fonts\\consola.ttf")) {
			g_theme.mono = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 15.0f);
		}
		if (!g_theme.body) {
			g_theme.body = io.Fonts->AddFontDefault();
		}
		if (!g_theme.heading) {
			g_theme.heading = g_theme.body;
		}
		if (!g_theme.title) {
			g_theme.title = g_theme.heading;
		}
		if (!g_theme.smallFont) {
			g_theme.smallFont = g_theme.body;
		}
		if (!g_theme.mono) {
			g_theme.mono = g_theme.smallFont;
		}
		io.FontDefault = g_theme.body;
		g_themeApplied = true;
	}

	const Theme& Th()
	{
		if (!g_themeApplied) {
			InitTokens(1.0f);
		}
		return g_theme;
	}

	// ---- Text ----

	void Title(const char* a_text)
	{
		ImGui::PushFont(Th().title);
		ImGui::TextUnformatted(a_text);
		ImGui::PopFont();
	}

	void Heading(const char* a_text)
	{
		ImGui::PushFont(Th().heading);
		ImGui::TextUnformatted(a_text);
		ImGui::PopFont();
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
		ImGui::PushFont(Th().smallFont);
		ImGui::TextUnformatted(a_text);
		ImGui::PopFont();
	}

	void Mono(const char* a_text)
	{
		ImGui::PushFont(Th().mono);
		ImGui::TextUnformatted(a_text);
		ImGui::PopFont();
	}

	void SectionHead(const char* a_text)
	{
		const Theme& t = Th();
		ImGui::Spacing();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImVec2      p = ImGui::GetCursorScreenPos();
		const float avail = ImGui::GetContentRegionAvail().x;
		ImGui::PushFont(t.smallFont);
		const ImVec2 size = ImGui::CalcTextSize(a_text);
		const float  h = size.y;
		const float  d = 3.0f * t.scale;
		const float  cy = p.y + h * 0.5f;
		dl->AddQuadFilled(ImVec2(p.x, cy - d), ImVec2(p.x + d, cy), ImVec2(p.x, cy + d), ImVec2(p.x - d, cy), t.accentDim);
		dl->AddLine(ImVec2(p.x + d + 4 * t.scale, cy), ImVec2(p.x + d + 10 * t.scale, cy), t.line);
		dl->AddText(ImVec2(p.x + d + 14 * t.scale, p.y), t.muted, a_text);
		const float x0 = p.x + d + 18 * t.scale + size.x;
		if (x0 < p.x + avail) {
			dl->AddLine(ImVec2(x0, cy), ImVec2(p.x + avail, cy), t.line);
		}
		ImGui::PopFont();
		ImGui::Dummy(ImVec2(avail, h));
		ImGui::Spacing();
	}

	void Tip(const char* a_text)
	{
		if (a_text && *a_text && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
			ImGui::SetTooltip("%s", a_text);
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

	namespace
	{
		bool ChipImpl(const char* a_text, Kind a_kind, bool a_active, bool a_button, bool a_close, bool* a_closeClicked)
		{
			const Theme& t = Th();
			const ImU32  col = Colour(a_kind);
			ImGui::PushFont(t.smallFont);
			const ImVec2 size = ImGui::CalcTextSize(a_text);
			const float  h = 18.0f * t.scale;
			const float  padX = 7.0f * t.scale;
			const float  closeW = a_close ? h : 0.0f;
			const ImVec2 full(size.x + 2 * padX + closeW, h);
			const ImVec2 p = ImGui::GetCursorScreenPos();
			ImGui::InvisibleButton(a_text, full);
			const bool hovered = ImGui::IsItemHovered();
			const bool clicked = ImGui::IsItemClicked();
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImU32 fill = a_active ? (col & 0x00FFFFFF) | 0x40000000 : (hovered && a_button ? t.bg3 : t.bg2);
			dl->AddRectFilled(p, ImVec2(p.x + full.x, p.y + full.y), fill, h * 0.5f);
			dl->AddRect(p, ImVec2(p.x + full.x, p.y + full.y), col, h * 0.5f);
			dl->AddText(ImVec2(p.x + padX, p.y + (h - size.y) * 0.5f), col, a_text);
			bool closeHit = false;
			if (a_close) {
				const ImVec2 c(p.x + full.x - h * 0.5f, p.y + h * 0.5f);
				const float  r = 3.0f * t.scale;
				dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, 1.0f);
				dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), col, 1.0f);
				const ImVec2 m = ImGui::GetIO().MousePos;
				closeHit = clicked && m.x >= p.x + full.x - h;
			}
			ImGui::PopFont();
			if (a_closeClicked) {
				*a_closeClicked = closeHit;
			}
			return clicked;
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
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float  h = ImGui::GetTextLineHeight();
		ImGui::Dummy(ImVec2(2 * r + 2 * t.scale, h));
		ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r, p.y + h * 0.5f), r, Colour(a_kind));
		Tip(a_tooltip);
	}

	// ---- Icons ----

	void DrawIcon(ImDrawList* a_list, Icon a_icon, ImVec2 a_min, float a_size, ImU32 a_colour)
	{
		const float s = a_size;
		const float th = std::max(1.0f, s / 10.0f);
		const ImVec2 c(a_min.x + s * 0.5f, a_min.y + s * 0.5f);
		auto L = [&](float x0, float y0, float x1, float y1) {
			a_list->AddLine(ImVec2(a_min.x + x0 * s, a_min.y + y0 * s), ImVec2(a_min.x + x1 * s, a_min.y + y1 * s), a_colour, th);
		};
		switch (a_icon) {
		case Icon::Menu:
			L(0.15f, 0.25f, 0.85f, 0.25f);
			L(0.15f, 0.5f, 0.85f, 0.5f);
			L(0.15f, 0.75f, 0.85f, 0.75f);
			break;
		case Icon::Search:
			a_list->AddCircle(ImVec2(a_min.x + 0.42f * s, a_min.y + 0.42f * s), 0.26f * s, a_colour, 0, th);
			L(0.62f, 0.62f, 0.88f, 0.88f);
			break;
		case Icon::Filter:
			L(0.1f, 0.2f, 0.9f, 0.2f);
			L(0.1f, 0.2f, 0.45f, 0.55f);
			L(0.9f, 0.2f, 0.55f, 0.55f);
			L(0.45f, 0.55f, 0.45f, 0.9f);
			L(0.55f, 0.55f, 0.55f, 0.8f);
			break;
		case Icon::Layers:
			for (int i = 0; i < 3; ++i) {
				const float y = 0.3f + 0.2f * static_cast<float>(i);
				L(0.1f, y, 0.5f, y - 0.2f);
				L(0.5f, y - 0.2f, 0.9f, y);
				L(0.1f, y, 0.5f, y + 0.2f);
				L(0.5f, y + 0.2f, 0.9f, y);
			}
			break;
		case Icon::Palette:
			a_list->AddCircle(c, 0.4f * s, a_colour, 0, th);
			a_list->AddCircleFilled(ImVec2(c.x - 0.15f * s, c.y - 0.12f * s), 0.07f * s, a_colour);
			a_list->AddCircleFilled(ImVec2(c.x + 0.12f * s, c.y - 0.15f * s), 0.07f * s, a_colour);
			a_list->AddCircleFilled(ImVec2(c.x + 0.18f * s, c.y + 0.12f * s), 0.07f * s, a_colour);
			break;
		case Icon::Camera:
			a_list->AddRect(ImVec2(a_min.x + 0.1f * s, a_min.y + 0.3f * s), ImVec2(a_min.x + 0.7f * s, a_min.y + 0.75f * s), a_colour, 0, 0, th);
			L(0.7f, 0.45f, 0.9f, 0.35f);
			L(0.9f, 0.35f, 0.9f, 0.7f);
			L(0.9f, 0.7f, 0.7f, 0.6f);
			break;
		case Icon::Settings:
			a_list->AddCircle(c, 0.32f * s, a_colour, 8, th);
			a_list->AddCircle(c, 0.12f * s, a_colour, 0, th);
			for (int i = 0; i < 8; ++i) {
				const float a = static_cast<float>(i) * 3.14159265f / 4.0f;
				a_list->AddLine(ImVec2(c.x + std::cos(a) * 0.32f * s, c.y + std::sin(a) * 0.32f * s), ImVec2(c.x + std::cos(a) * 0.45f * s, c.y + std::sin(a) * 0.45f * s), a_colour, th);
			}
			break;
		case Icon::ChevronLeft:
			L(0.6f, 0.2f, 0.35f, 0.5f);
			L(0.35f, 0.5f, 0.6f, 0.8f);
			break;
		case Icon::ChevronRight:
			L(0.4f, 0.2f, 0.65f, 0.5f);
			L(0.65f, 0.5f, 0.4f, 0.8f);
			break;
		case Icon::ChevronDown:
			L(0.2f, 0.4f, 0.5f, 0.65f);
			L(0.5f, 0.65f, 0.8f, 0.4f);
			break;
		case Icon::ChevronUp:
			L(0.2f, 0.6f, 0.5f, 0.35f);
			L(0.5f, 0.35f, 0.8f, 0.6f);
			break;
		case Icon::Check:
			L(0.15f, 0.55f, 0.4f, 0.8f);
			L(0.4f, 0.8f, 0.85f, 0.25f);
			break;
		case Icon::Warning:
			a_list->AddTriangle(ImVec2(c.x, a_min.y + 0.1f * s), ImVec2(a_min.x + 0.1f * s, a_min.y + 0.9f * s), ImVec2(a_min.x + 0.9f * s, a_min.y + 0.9f * s), a_colour, th);
			L(0.5f, 0.38f, 0.5f, 0.62f);
			a_list->AddCircleFilled(ImVec2(c.x, a_min.y + 0.76f * s), th * 0.8f, a_colour);
			break;
		case Icon::Error:
			a_list->AddCircle(c, 0.4f * s, a_colour, 0, th);
			L(0.32f, 0.32f, 0.68f, 0.68f);
			L(0.32f, 0.68f, 0.68f, 0.32f);
			break;
		case Icon::Info:
			a_list->AddCircle(c, 0.4f * s, a_colour, 0, th);
			L(0.5f, 0.45f, 0.5f, 0.72f);
			a_list->AddCircleFilled(ImVec2(c.x, a_min.y + 0.3f * s), th * 0.8f, a_colour);
			break;
		case Icon::Undo:
			a_list->PathArcTo(ImVec2(c.x, c.y + 0.05f * s), 0.32f * s, 3.5f, 6.9f, 12);
			a_list->PathStroke(a_colour, 0, th);
			L(0.12f, 0.3f, 0.12f, 0.55f);
			L(0.12f, 0.55f, 0.37f, 0.55f);
			break;
		case Icon::Redo:
			a_list->PathArcTo(ImVec2(c.x, c.y + 0.05f * s), 0.32f * s, 2.9f - 3.14159265f, 2.9f, 12);
			a_list->PathStroke(a_colour, 0, th);
			L(0.88f, 0.3f, 0.88f, 0.55f);
			L(0.88f, 0.55f, 0.63f, 0.55f);
			break;
		case Icon::Save:
			a_list->AddRect(ImVec2(a_min.x + 0.15f * s, a_min.y + 0.15f * s), ImVec2(a_min.x + 0.85f * s, a_min.y + 0.85f * s), a_colour, 0, 0, th);
			a_list->AddRect(ImVec2(a_min.x + 0.3f * s, a_min.y + 0.15f * s), ImVec2(a_min.x + 0.7f * s, a_min.y + 0.4f * s), a_colour, 0, 0, th);
			L(0.3f, 0.85f, 0.3f, 0.6f);
			L(0.3f, 0.6f, 0.7f, 0.6f);
			L(0.7f, 0.6f, 0.7f, 0.85f);
			break;
		case Icon::Close:
			L(0.25f, 0.25f, 0.75f, 0.75f);
			L(0.25f, 0.75f, 0.75f, 0.25f);
			break;
		case Icon::Pin:
			L(0.5f, 0.55f, 0.5f, 0.9f);
			a_list->AddRect(ImVec2(a_min.x + 0.3f * s, a_min.y + 0.15f * s), ImVec2(a_min.x + 0.7f * s, a_min.y + 0.55f * s), a_colour, 0, 0, th);
			break;
		case Icon::Copy:
			a_list->AddRect(ImVec2(a_min.x + 0.3f * s, a_min.y + 0.3f * s), ImVec2(a_min.x + 0.85f * s, a_min.y + 0.85f * s), a_colour, 0, 0, th);
			L(0.15f, 0.65f, 0.15f, 0.15f);
			L(0.15f, 0.15f, 0.65f, 0.15f);
			break;
		case Icon::Home:
			L(0.1f, 0.5f, 0.5f, 0.15f);
			L(0.5f, 0.15f, 0.9f, 0.5f);
			L(0.22f, 0.42f, 0.22f, 0.85f);
			L(0.22f, 0.85f, 0.78f, 0.85f);
			L(0.78f, 0.85f, 0.78f, 0.42f);
			break;
		case Icon::Cube:
			L(0.5f, 0.1f, 0.9f, 0.3f);
			L(0.9f, 0.3f, 0.9f, 0.7f);
			L(0.9f, 0.7f, 0.5f, 0.9f);
			L(0.5f, 0.9f, 0.1f, 0.7f);
			L(0.1f, 0.7f, 0.1f, 0.3f);
			L(0.1f, 0.3f, 0.5f, 0.1f);
			L(0.1f, 0.3f, 0.5f, 0.5f);
			L(0.5f, 0.5f, 0.9f, 0.3f);
			L(0.5f, 0.5f, 0.5f, 0.9f);
			break;
		case Icon::Plan:
			a_list->AddRect(ImVec2(a_min.x + 0.12f * s, a_min.y + 0.12f * s), ImVec2(a_min.x + 0.88f * s, a_min.y + 0.88f * s), a_colour, 0, 0, th);
			L(0.12f, 0.5f, 0.55f, 0.5f);
			L(0.55f, 0.12f, 0.55f, 0.88f);
			break;
		case Icon::Draft:
			L(0.5f, 0.1f, 0.5f, 0.9f);
			L(0.1f, 0.5f, 0.9f, 0.5f);
			L(0.25f, 0.25f, 0.75f, 0.75f);
			L(0.25f, 0.75f, 0.75f, 0.25f);
			break;
		case Icon::Plus:
			L(0.5f, 0.2f, 0.5f, 0.8f);
			L(0.2f, 0.5f, 0.8f, 0.5f);
			break;
		case Icon::Minus:
			L(0.2f, 0.5f, 0.8f, 0.5f);
			break;
		case Icon::Emblem:
			a_list->AddQuadFilled(ImVec2(c.x, a_min.y + 0.1f * s), ImVec2(a_min.x + 0.9f * s, c.y), ImVec2(c.x, a_min.y + 0.9f * s), ImVec2(a_min.x + 0.1f * s, c.y), a_colour);
			a_list->AddQuadFilled(ImVec2(c.x, a_min.y + 0.3f * s), ImVec2(a_min.x + 0.7f * s, c.y), ImVec2(c.x, a_min.y + 0.7f * s), ImVec2(a_min.x + 0.3f * s, c.y), Th().bg0);
			break;
		}
	}

	bool IconButton(const char* a_id, Icon a_icon, const char* a_tooltip, const char* a_shortcut, bool a_active)
	{
		const Theme& t = Th();
		const float  h = t.toolbarH - 4.0f * t.scale;
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const bool   clicked = ImGui::InvisibleButton(a_id, ImVec2(h, h));
		const bool   hovered = ImGui::IsItemHovered();
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		if (hovered || a_active) {
			dl->AddRectFilled(p, ImVec2(p.x + h, p.y + h), hovered ? t.bg3 : t.bg2, 3.0f);
		}
		const float is = 16.0f * t.scale;
		DrawIcon(dl, a_icon, ImVec2(p.x + (h - is) * 0.5f, p.y + (h - is) * 0.5f), is, a_active ? t.accent : t.text);
		if (a_tooltip && *a_tooltip) {
			std::string tip = a_tooltip;
			if (a_shortcut && *a_shortcut) {
				tip += "  ";
				tip += a_shortcut;
			}
			Tip(tip.c_str());
		}
		return clicked;
	}

	// ---- Buttons ----

	bool ToolButton(const char* a_label, bool a_active, const char* a_tooltip, const char* a_shortcut)
	{
		const Theme& t = Th();
		if (a_active) {
			ImGui::PushStyleColor(ImGuiCol_Text, t.accent);
		}
		const bool clicked = ImGui::Button(a_label);
		if (a_active) {
			ImGui::PopStyleColor();
			const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
			ImGui::GetWindowDrawList()->AddLine(ImVec2(mn.x + 4, mx.y - 1), ImVec2(mx.x - 4, mx.y - 1), t.accent, 2.0f * t.scale);
		}
		if (a_tooltip && *a_tooltip) {
			std::string tip = a_tooltip;
			if (a_shortcut && *a_shortcut) {
				tip += "  ";
				tip += a_shortcut;
			}
			Tip(tip.c_str());
		} else if (a_shortcut && *a_shortcut) {
			Tip(a_shortcut);
		}
		return clicked;
	}

	bool PrimaryButton(const char* a_label, bool a_enabled, const char* a_disabledTip)
	{
		const Theme& t = Th();
		ImGui::BeginDisabled(!a_enabled);
		ImGui::PushStyleColor(ImGuiCol_Button, t.accent);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.select);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.accentDim);
		ImGui::PushStyleColor(ImGuiCol_Text, t.bg0);
		const bool clicked = ImGui::Button(a_label);
		ImGui::PopStyleColor(4);
		ImGui::EndDisabled();
		if (!a_enabled && a_disabledTip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip("%s", a_disabledTip);
		}
		return clicked;
	}

	bool LinkButton(const char* a_label)
	{
		const Theme& t = Th();
		ImGui::PushStyleColor(ImGuiCol_Text, t.accent);
		ImGui::TextUnformatted(a_label);
		ImGui::PopStyleColor();
		const bool hovered = ImGui::IsItemHovered();
		if (hovered) {
			const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
			ImGui::GetWindowDrawList()->AddLine(ImVec2(mn.x, mx.y), ImVec2(mx.x, mx.y), t.accent);
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		}
		return hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	}

	bool Segmented(const char* a_id, int& a_value, std::span<const char* const> a_labels, std::span<const char* const> a_tooltips)
	{
		bool changed = false;
		ImGui::PushID(a_id);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.0f, ImGui::GetStyle().ItemSpacing.y));
		for (std::size_t i = 0; i < a_labels.size(); ++i) {
			if (i > 0) {
				ImGui::SameLine();
			}
			const bool active = static_cast<int>(i) == a_value;
			if (ToolButton(a_labels[i], active, i < a_tooltips.size() ? a_tooltips[i] : nullptr)) {
				if (!active) {
					a_value = static_cast<int>(i);
					changed = true;
				}
			}
		}
		ImGui::PopStyleVar();
		ImGui::PopID();
		return changed;
	}

	// ---- Inputs ----

	bool SearchBox(const char* a_id, char* a_buf, std::size_t a_size, const char* a_hint, float a_width)
	{
		const Theme& t = Th();
		ImGui::PushID(a_id);
		const float  w = a_width > 0.0f ? a_width : ImGui::GetContentRegionAvail().x;
		const float  iconW = 20.0f * t.scale;
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(ImVec2(p.x + iconW, p.y));
		ImGui::SetNextItemWidth(w - iconW - (a_buf[0] ? 22.0f * t.scale : 0.0f));
		bool changed = ImGui::InputTextWithHint("##s", a_hint, a_buf, a_size);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float h = ImGui::GetItemRectSize().y;
		dl->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + iconW + 2, p.y + h), t.bg2, 3.0f);
		DrawIcon(dl, Icon::Search, ImVec2(p.x + 3 * t.scale, p.y + (h - 14 * t.scale) * 0.5f), 14 * t.scale, t.muted);
		if (a_buf[0]) {
			ImGui::SameLine(0.0f, 2.0f);
			if (IconButton("clear", Icon::Close, "Clear")) {
				a_buf[0] = '\0';
				changed = true;
			}
		}
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

	// ---- Popovers ----

	bool BeginPopover(const char* a_id, const char* a_label, Icon a_icon, int a_badge, const char* a_tooltip, const char* a_shortcut)
	{
		const Theme& t = Th();
		ImGui::PushID(a_id);
		std::string label = a_label ? a_label : "";
		if (!label.empty()) {
			label += "  v";
		}
		bool clicked = false;
		if (!label.empty()) {
			clicked = ToolButton(label.c_str(), false, a_tooltip, a_shortcut);
		} else {
			clicked = IconButton("btn", a_icon, a_tooltip, a_shortcut);
		}
		if (a_badge > 0) {
			const ImVec2 mx = ImGui::GetItemRectMax();
			ImDrawList*  dl = ImGui::GetWindowDrawList();
			const float  r = 6.0f * t.scale;
			dl->AddCircleFilled(ImVec2(mx.x - r, ImGui::GetItemRectMin().y + r), r, t.accent);
			ImGui::PushFont(t.smallFont);
			const std::string n = std::to_string(a_badge);
			const ImVec2      sz = ImGui::CalcTextSize(n.c_str());
			dl->AddText(ImVec2(mx.x - r - sz.x * 0.5f, ImGui::GetItemRectMin().y + r - sz.y * 0.5f), t.bg0, n.c_str());
			ImGui::PopFont();
		}
		auto& req = PopoverRequests();
		if (clicked || req[a_id]) {
			req[a_id] = false;
			ImGui::OpenPopup("pop");
		}
		ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 2.0f * t.scale));
		const bool open = ImGui::BeginPopup("pop");
		if (!open) {
			ImGui::PopID();
		}
		return open;
	}

	void EndPopover()
	{
		ImGui::EndPopup();
		ImGui::PopID();
	}

	void OpenPopover(const char* a_id)
	{
		PopoverRequests()[a_id] = true;
	}

	// ---- Layout ----

	namespace
	{
		struct RailState
		{
			Side   side;
			float* size;
			bool*  open;
			float  minSize, maxSize;
			bool   drawn;
		};
		RailState g_rail{};
	}

	bool BeginRail(const char* a_id, Side a_side, float& a_size, bool& a_open, float a_minSize, float a_maxSize, const char* a_collapsedText)
	{
		const Theme& t = Th();
		g_rail = { a_side, &a_size, &a_open, a_minSize, a_maxSize, false };
		const float strip = 28.0f * t.scale;
		const bool  horizontal = a_side != Side::Bottom;
		if (!a_open) {
			const ImVec2 size = horizontal ? ImVec2(strip, 0.0f) : ImVec2(0.0f, strip);
			ImGui::BeginChild(a_id, size, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
			const Icon icon = a_side == Side::Left ? Icon::ChevronRight : a_side == Side::Right ? Icon::ChevronLeft : Icon::ChevronUp;
			if (IconButton("expand", icon, a_collapsedText ? a_collapsedText : "Show", nullptr)) {
				a_open = true;
			}
			if (a_collapsedText && horizontal) {
				ImGui::PushFont(t.smallFont);
				ImGui::PushStyleColor(ImGuiCol_Text, t.muted);
				// vertical text: one glyph per line is unreadable; show the first word only
				std::string word = a_collapsedText;
				if (const auto sp = word.find(' '); sp != std::string::npos) {
					word.resize(sp);
				}
				for (char ch : word) {
					const char s[2]{ ch, 0 };
					ImGui::SetCursorPosX((strip - ImGui::CalcTextSize(s).x) * 0.5f);
					ImGui::TextUnformatted(s);
				}
				ImGui::PopStyleColor();
				ImGui::PopFont();
			}
			ImGui::EndChild();
			return false;
		}
		a_size = std::clamp(a_size, a_minSize, a_maxSize);
		const ImVec2 size = horizontal ? ImVec2(a_size, 0.0f) : ImVec2(0.0f, a_size);
		ImGui::BeginChild(a_id, size, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
		// Collapse handle in the top corner, resize handle on the inner edge.
		{
			const ImVec2 mn = ImGui::GetWindowPos();
			const ImVec2 mx(mn.x + ImGui::GetWindowSize().x, mn.y + ImGui::GetWindowSize().y);
			const float  hs = 6.0f * t.scale;
			ImVec2       hmin, hmax;
			switch (a_side) {
			case Side::Left:
				hmin = ImVec2(mx.x - hs, mn.y);
				hmax = ImVec2(mx.x, mx.y);
				break;
			case Side::Right:
				hmin = ImVec2(mn.x, mn.y);
				hmax = ImVec2(mn.x + hs, mx.y);
				break;
			default:
				hmin = ImVec2(mn.x, mn.y);
				hmax = ImVec2(mx.x, mn.y + hs);
				break;
			}
			ImGui::PushID("resize");
			ImGui::SetCursorScreenPos(hmin);
			ImGui::InvisibleButton("h", ImVec2(std::max(1.0f, hmax.x - hmin.x), std::max(1.0f, hmax.y - hmin.y)));
			if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
				ImGui::SetMouseCursor(horizontal ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
				ImGui::GetWindowDrawList()->AddRectFilled(hmin, hmax, t.accentDim);
			}
			if (ImGui::IsItemActive()) {
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
			ImGui::PopID();
			ImGui::SetCursorScreenPos(mn);
		}
		ImGui::BeginChild("content", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None);
		g_rail.drawn = true;
		return true;
	}

	void EndRail()
	{
		ImGui::EndChild();  // content
		// collapse handle, drawn last so it sits over the content
		const Theme& t = Th();
		const ImVec2 mn = ImGui::GetWindowPos();
		const ImVec2 sz = ImGui::GetWindowSize();
		const float  b = 18.0f * t.scale;
		ImVec2       p;
		Icon         icon;
		switch (g_rail.side) {
		case Side::Left:
			p = ImVec2(mn.x + sz.x - b - 8 * t.scale, mn.y + sz.y - b - 4 * t.scale);
			icon = Icon::ChevronLeft;
			break;
		case Side::Right:
			p = ImVec2(mn.x + 8 * t.scale, mn.y + sz.y - b - 4 * t.scale);
			icon = Icon::ChevronRight;
			break;
		default:
			p = ImVec2(mn.x + sz.x - b - 8 * t.scale, mn.y + 8 * t.scale);
			icon = Icon::ChevronDown;
			break;
		}
		ImGui::SetCursorScreenPos(p);
		if (ImGui::InvisibleButton("collapse", ImVec2(b, b))) {
			*g_rail.open = false;
		}
		DrawIcon(ImGui::GetWindowDrawList(), icon, p, b, ImGui::IsItemHovered() ? t.accent : t.muted);
		Tip("Hide");
		ImGui::EndChild();
	}

	bool BeginOverlay(const char* a_id, ImVec2 a_pos, float a_alpha)
	{
		const Theme& t = Th();
		const ImVec2 origin = ImGui::GetWindowPos();
		ImGui::SetCursorScreenPos(ImVec2(origin.x + a_pos.x, origin.y + a_pos.y));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertFloat4ToU32(ImVec4(V4(t.bg1).x, V4(t.bg1).y, V4(t.bg1).z, a_alpha)));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f * t.scale);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(t.gap, 3.0f * t.scale));
		ImGui::BeginChild(a_id, ImVec2(0, 0), ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
		return true;
	}

	void EndOverlay()
	{
		ImGui::EndChild();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();
	}

	void HintLine(const char* a_text, ImVec2 a_viewportMin, ImVec2 a_viewportMax)
	{
		const Theme& t = Th();
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		ImGui::PushFont(t.smallFont);
		const ImVec2 sz = ImGui::CalcTextSize(a_text);
		const float  h = sz.y + 6 * t.scale;
		dl->AddRectFilled(ImVec2(a_viewportMin.x, a_viewportMax.y - h), a_viewportMax, IM_COL32(0, 0, 0, 110));
		dl->AddText(ImVec2(a_viewportMin.x + t.pad, a_viewportMax.y - h + 3 * t.scale), t.muted, a_text);
		ImGui::PopFont();
	}

	bool Row(const char* a_id, const char* a_label, bool a_selected, const char* a_right, Kind a_rightKind)
	{
		const Theme& t = Th();
		ImGui::PushID(a_id);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float  w = ImGui::GetContentRegionAvail().x;
		const bool   clicked = ImGui::InvisibleButton("row", ImVec2(std::max(w, 1.0f), t.rowH));
		const bool   hovered = ImGui::IsItemHovered();
		ImDrawList*  dl = ImGui::GetWindowDrawList();
		if (a_selected) {
			dl->AddRectFilled(p, ImVec2(p.x + w, p.y + t.rowH), (t.accent & 0x00FFFFFF) | 0x30000000, 3.0f);
			dl->AddRectFilled(p, ImVec2(p.x + 2 * t.scale, p.y + t.rowH), t.accent);
		} else if (hovered) {
			dl->AddRectFilled(p, ImVec2(p.x + w, p.y + t.rowH), t.bg3, 3.0f);
		}
		const float ty = p.y + (t.rowH - ImGui::GetTextLineHeight()) * 0.5f;
		dl->AddText(ImVec2(p.x + t.pad, ty), t.text, a_label);
		if (a_right && *a_right) {
			ImGui::PushFont(t.smallFont);
			const ImVec2 sz = ImGui::CalcTextSize(a_right);
			dl->AddText(ImVec2(p.x + w - sz.x - t.pad, p.y + (t.rowH - sz.y) * 0.5f), Colour(a_rightKind), a_right);
			ImGui::PopFont();
		}
		ImGui::PopID();
		return clicked;
	}

	void EmptyState(const char* a_heading, const char* a_text)
	{
		const Theme& t = Th();
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		ImGui::Dummy(ImVec2(0, std::max(0.0f, avail.y * 0.3f)));
		ImGui::PushFont(t.heading);
		const float hw = ImGui::CalcTextSize(a_heading).x;
		ImGui::SetCursorPosX(std::max(0.0f, (avail.x - hw) * 0.5f));
		ImGui::PushStyleColor(ImGuiCol_Text, t.muted);
		ImGui::TextUnformatted(a_heading);
		ImGui::PopStyleColor();
		ImGui::PopFont();
		if (a_text && *a_text) {
			const float tw = std::min(ImGui::CalcTextSize(a_text).x, avail.x - 2 * t.pad);
			ImGui::SetCursorPosX(std::max(0.0f, (avail.x - tw) * 0.5f));
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tw);
			MutedWrapped(a_text);
			ImGui::PopTextWrapPos();
		}
	}

	bool Card(const char* a_id, const char* a_title, const char* a_text, const char* a_shortcut, float a_width)
	{
		const Theme& t = Th();
		ImGui::PushID(a_id);
		const float  w = a_width > 0.0f ? a_width : ImGui::GetContentRegionAvail().x;
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::PushFont(t.smallFont);
		const ImVec2 bodySz = ImGui::CalcTextSize(a_text, nullptr, false, w - 2 * t.pad);
		ImGui::PopFont();
		const float h = t.pad * 2 + ImGui::GetTextLineHeight() * 1.3f + bodySz.y + t.gap;
		const bool  clicked = ImGui::InvisibleButton("card", ImVec2(w, h));
		const bool  hovered = ImGui::IsItemHovered();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), hovered ? t.bg3 : t.bg2, 4.0f * t.scale);
		dl->AddRect(p, ImVec2(p.x + w, p.y + h), hovered ? t.accent : t.line, 4.0f * t.scale);
		ImGui::PushFont(t.heading);
		dl->AddText(t.heading, t.heading->LegacySize, ImVec2(p.x + t.pad, p.y + t.pad), t.text, a_title);
		ImGui::PopFont();
		if (a_shortcut && *a_shortcut) {
			ImGui::PushFont(t.smallFont);
			const ImVec2 sz = ImGui::CalcTextSize(a_shortcut);
			dl->AddText(ImVec2(p.x + w - sz.x - t.pad, p.y + t.pad + 2 * t.scale), t.muted, a_shortcut);
			ImGui::PopFont();
		}
		ImGui::PushFont(t.smallFont);
		dl->AddText(t.smallFont, t.smallFont->LegacySize, ImVec2(p.x + t.pad, p.y + t.pad + ImGui::GetTextLineHeight() * 1.3f + t.gap), t.muted, a_text, nullptr, w - 2 * t.pad);
		ImGui::PopFont();
		if (hovered) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		}
		ImGui::PopID();
		return clicked;
	}

	// ---- Helpers ----

	float TextWidth(const char* a_text)
	{
		return ImGui::CalcTextSize(a_text).x;
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
