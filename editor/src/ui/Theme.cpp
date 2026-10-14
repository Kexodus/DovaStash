// The theme (docs/EDITOR-UX.md §3): tokens, the embedded fonts and the ImGui style. Widgets.cpp draws with
// them; everything else goes through Widgets.h.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <imgui.h>
#include <imgui_internal.h>

#include "ui/ThemeDetail.h"
#include "ui/Widgets.h"

namespace dova::editor::ui::w
{
	using detail::Rgb;
	using detail::V4;

	namespace
	{
		Theme                g_theme{};
		bool                 g_themeApplied{ false };
		float                g_autoText{ 1.0f };
		float                g_textSetting{ 0.0f };
		std::optional<float> g_pendingText;

		// Resource ids in res/DovaStashEditor.rc.
		constexpr int kCinzelResource = 101;
		constexpr int kJostResource = 102;

		void InitTokens(float a_scale)
		{
			// The art set's palette (presskit/README.md, Style): black, white and neutral grays only, every
			// gray with R = G = B. Surfaces step through the darkest grays; text uses the set's five.
			g_theme.bg0 = Rgb(0x000000);  // ink
			g_theme.bg1 = Rgb(0x0e0e0e);
			g_theme.bg2 = Rgb(0x1e1e1e);  // gray-5: faint frames
			g_theme.bg3 = Rgb(0x2a2a2a);
			g_theme.line = Rgb(0x3c3c3c);  // gray-4: rules
			g_theme.text = Rgb(0xd4d4d4);  // gray-1: secondary text (titles and marks are paper)
			g_theme.muted = Rgb(0x9c9c9c);  // gray-2: captions
			g_theme.accent = Rgb(0xffffff);  // paper: primary marks, the active control, the selected row
			g_theme.accentDim = Rgb(0x9c9c9c);
			g_theme.select = Rgb(0xffffff);
			// Status hues stay for pills, dots and status text: they encode state (drafted, shipped, yours,
			// saved, a confidence, an error), as the viewport's data colours do.
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
			g_theme.radius = 3.0f * a_scale;
			g_theme.iconSize = 16.0f * a_scale;
			g_theme.chipH = 18.0f * a_scale;
			g_theme.stripW = 28.0f * a_scale;
			g_theme.stroke = 1.5f * a_scale;
			g_theme.shadow = IM_COL32(0, 0, 0, 102);
		}

		// ---- Fonts ----

		std::span<const std::byte> Resource(int a_id)
		{
			const HMODULE module = ::GetModuleHandleW(nullptr);
			const HRSRC   res = ::FindResourceW(module, MAKEINTRESOURCEW(a_id), RT_RCDATA);
			if (!res) {
				return {};
			}
			const HGLOBAL handle = ::LoadResource(module, res);
			const void*   data = handle ? ::LockResource(handle) : nullptr;
			if (!data) {
				return {};
			}
			return { static_cast<const std::byte*>(data), ::SizeofResource(module, res) };
		}

		std::filesystem::path FontsDir()
		{
			wchar_t buf[MAX_PATH]{};
			const UINT n = ::GetWindowsDirectoryW(buf, MAX_PATH);
			return std::filesystem::path(std::wstring(buf, n)) / L"Fonts";
		}

		// A system font, read once and kept for the process (the atlas reads it whenever it bakes a size).
		std::span<const std::byte> SystemFont(const wchar_t* a_file)
		{
			static std::vector<std::pair<std::wstring, std::vector<std::byte>>> s_cache;
			for (const auto& [name, data] : s_cache) {
				if (name == a_file) {
					return data;
				}
			}
			std::vector<std::byte> data;
			std::vector<std::filesystem::path> candidates{ FontsDir() / a_file };
			wchar_t local[MAX_PATH]{};
			if (const DWORD n = ::GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH); n > 0 && n < MAX_PATH) {
				candidates.push_back(std::filesystem::path(local) / L"Microsoft" / L"Windows" / L"Fonts" / a_file);
			}
			for (const auto& path : candidates) {
				std::ifstream in(path, std::ios::binary);
				if (!in) {
					continue;
				}
				in.seekg(0, std::ios::end);
				const auto size = static_cast<std::size_t>(in.tellg());
				in.seekg(0);
				data.resize(size);
				in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
				if (!in) {
					data.clear();
					continue;
				}
				break;
			}
			s_cache.emplace_back(a_file, std::move(data));
			return s_cache.back().second;
		}

		// `a_data` must outlive the atlas (a resource or SystemFont's cache): the atlas does not own it.
		ImFont* AddFont(const char* a_name, std::span<const std::byte> a_data, float a_px, bool a_symbols)
		{
			if (a_data.empty()) {
				return nullptr;
			}
			ImGuiIO&     io = ImGui::GetIO();
			ImFontConfig cfg;
			cfg.FontDataOwnedByAtlas = false;
			std::snprintf(cfg.Name, sizeof(cfg.Name), "%s %.0fpx", a_name, a_px);
			ImFont* font = io.Fonts->AddFontFromMemoryTTF(const_cast<std::byte*>(a_data.data()), static_cast<int>(a_data.size()), a_px, &cfg);
			if (font && a_symbols) {
				// Jost and Cinzel lack ◆ ● ▾ ▴ and arrows: Segoe UI Symbol fills in (only for glyphs the main font lacks).
				const auto symbols = SystemFont(L"seguisym.ttf");
				if (!symbols.empty()) {
					ImFontConfig merge;
					merge.MergeMode = true;
					merge.FontDataOwnedByAtlas = false;
					std::snprintf(merge.Name, sizeof(merge.Name), "Segoe UI Symbol %.0fpx", a_px);
					io.Fonts->AddFontFromMemoryTTF(const_cast<std::byte*>(symbols.data()), static_cast<int>(symbols.size()), a_px, &merge);
				}
			}
			return font;
		}

		void LoadFonts(float a_scale)
		{
			ImGui::GetIO().Fonts->Clear();  // a second call (new text size or DPI) starts the atlas over
			auto jost = Resource(kJostResource);
			auto cinzel = Resource(kCinzelResource);
			const char* jostName = "Jost";
			const char* cinzelName = "Cinzel";
			if (jost.empty()) {  // built without the resources: the system UI font keeps the sizes
				jost = SystemFont(L"segoeui.ttf");
				jostName = "Segoe UI";
			}
			if (cinzel.empty()) {
				cinzel = jost;
				cinzelName = jostName;
			}
			// The default font first: ImGui falls back to the first font added.
			g_theme.body = AddFont(jostName, jost, 15.0f * a_scale, true);
			g_theme.heading = AddFont(jostName, jost, 16.0f * a_scale, true);
			g_theme.smallFont = AddFont(jostName, jost, 13.0f * a_scale, true);
			g_theme.title = AddFont(cinzelName, cinzel, 22.0f * a_scale, true);
			auto mono = SystemFont(L"CascadiaMono.ttf");
			const char* monoName = "Cascadia Mono";
			if (mono.empty()) {
				mono = SystemFont(L"consola.ttf");
				monoName = "Consolas";
			}
			g_theme.mono = AddFont(monoName, mono, 13.0f * a_scale, false);

			ImGuiIO& io = ImGui::GetIO();
			if (!g_theme.body) {
				ImFontConfig cfg;
				cfg.SizePixels = 15.0f * a_scale;
				g_theme.body = io.Fonts->AddFontDefault(&cfg);
			}
			if (!g_theme.heading) {
				g_theme.heading = g_theme.body;
			}
			if (!g_theme.smallFont) {
				g_theme.smallFont = g_theme.body;
			}
			if (!g_theme.title) {
				g_theme.title = g_theme.heading;
			}
			if (!g_theme.mono) {
				g_theme.mono = g_theme.smallFont;
			}
			io.FontDefault = g_theme.body;
		}

		// ---- Popup rim ----

		// The 1 px darker rim under every popup, menu and tooltip (EDITOR-UX.md §3.3), drawn after the frame's
		// windows are submitted so plain ImGui popups (combos, context menus) get it too.
		void DrawRims(ImGuiContext* a_ctx, ImGuiContextHook*)
		{
			ImGuiContext& g = *a_ctx;
			const Theme&  t = g_theme;
			for (ImGuiWindow* win : g.Windows) {
				if (!win->Active || win->Hidden || !(win->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip))) {
					continue;
				}
				const ImVec2 mn = win->Pos;
				const ImVec2 mx(win->Pos.x + win->Size.x, win->Pos.y + win->Size.y);
				const float  r = (win->Flags & ImGuiWindowFlags_Tooltip) ? g.Style.WindowRounding : g.Style.PopupRounding;
				ImDrawList*  dl = win->DrawList;
				dl->PushClipRect(ImVec2(mn.x - 2.0f, mx.y), ImVec2(mx.x + 2.0f, mx.y + 3.0f), false);
				dl->AddRect(ImVec2(mn.x, mn.y + 1.0f), ImVec2(mx.x, mx.y + 1.0f), t.shadow, r, 0, 1.0f);
				dl->AddRect(ImVec2(mn.x, mn.y + 2.0f), ImVec2(mx.x, mx.y + 2.0f), WithAlpha(t.shadow, 0.18f), r, 0, 1.0f);
				dl->PopClipRect();
			}
		}

		void InstallRimHook()
		{
			ImGuiContextHook hook{};
			hook.Type = ImGuiContextHookType_RenderPre;
			hook.Callback = DrawRims;
			ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
		}

		// ---- Style ----

		void ApplyStyle(float a_scale)
		{
			const Theme& t = g_theme;
			ImGuiStyle&  s = ImGui::GetStyle();
			s = ImGuiStyle();
			// Metrics at 100 % (EDITOR-UX.md §3.3), scaled below.
			s.WindowPadding = ImVec2(8, 8);
			s.WindowRounding = 3;
			s.WindowBorderSize = 0;
			s.WindowMinSize = ImVec2(24, 24);
			s.ChildRounding = 3;
			s.ChildBorderSize = 1;
			s.PopupRounding = 3;
			s.PopupBorderSize = 1;
			s.FramePadding = ImVec2(8, 5);
			s.FrameRounding = 3;
			s.FrameBorderSize = 1;
			s.ItemSpacing = ImVec2(8, 6);
			s.ItemInnerSpacing = ImVec2(6, 4);
			s.CellPadding = ImVec2(8, 4);
			s.IndentSpacing = 16;
			s.ColumnsMinSpacing = 6;
			s.ScrollbarSize = 8;
			s.ScrollbarRounding = 4;
			s.ScrollbarPadding = 1;
			s.GrabMinSize = 10;
			s.GrabRounding = 3;
			s.ImageRounding = 3;
			s.TabRounding = 3;
			s.TabBorderSize = 0;
			s.TabBarBorderSize = 1;
			s.TabBarOverlineSize = 2;
			s.TreeLinesSize = 1;
			s.MenuItemRounding = 3;
			s.SeparatorTextBorderSize = 1;
			s.SeparatorTextPadding = ImVec2(8, 3);
			s.DisplaySafeAreaPadding = ImVec2(4, 4);
			s.ButtonTextAlign = ImVec2(0.5f, 0.5f);
			s.HoverDelayShort = 0.25f;
			s.HoverDelayNormal = 0.5f;
			s.ScaleAllSizes(a_scale);
			// Exact heights: inputs 26 px and table rows 24 px around the 15 px body font (no truncation).
			const float body = 15.0f * a_scale;
			s.FramePadding.y = std::max(0.0f, (t.inputH - body) * 0.5f);
			s.CellPadding.y = std::max(0.0f, (t.rowH - body) * 0.5f);
			s.FontSizeBase = body;
			s.FontScaleMain = 1.0f;
			s.FontScaleDpi = 1.0f;  // the fonts are loaded at their scaled sizes

			const ImVec4 none(0, 0, 0, 0);
			ImVec4*      c = s.Colors;
			c[ImGuiCol_Text] = V4(t.text);
			c[ImGuiCol_TextDisabled] = V4(Rgb(0x6a6a6a));  // gray-3
			c[ImGuiCol_WindowBg] = V4(t.bg0);
			c[ImGuiCol_ChildBg] = none;
			c[ImGuiCol_PopupBg] = V4(t.bg2);
			c[ImGuiCol_Border] = V4(t.line);
			c[ImGuiCol_BorderShadow] = none;
			c[ImGuiCol_FrameBg] = V4(t.bg2);
			c[ImGuiCol_FrameBgHovered] = V4(t.bg3);
			c[ImGuiCol_FrameBgActive] = V4(detail::Mix(t.accent, t.bg3, 0.06f));
			c[ImGuiCol_TitleBg] = V4(t.bg0);
			c[ImGuiCol_TitleBgActive] = V4(t.bg1);
			c[ImGuiCol_TitleBgCollapsed] = V4(t.bg0);
			c[ImGuiCol_MenuBarBg] = V4(t.bg1);
			c[ImGuiCol_ScrollbarBg] = none;
			c[ImGuiCol_ScrollbarGrab] = V4(Rgb(0x3c3c3c));
			c[ImGuiCol_ScrollbarGrabHovered] = V4(Rgb(0x6a6a6a));
			c[ImGuiCol_ScrollbarGrabActive] = V4(t.accentDim);
			c[ImGuiCol_CheckMark] = V4(t.accent);
			c[ImGuiCol_CheckboxSelectedBg] = V4(t.bg2);
			c[ImGuiCol_SliderGrab] = V4(t.accentDim);
			c[ImGuiCol_SliderGrabActive] = V4(t.accent);
			c[ImGuiCol_Button] = V4(t.bg2);
			c[ImGuiCol_ButtonHovered] = V4(t.bg3);
			c[ImGuiCol_ButtonActive] = V4(detail::Mix(t.accentDim, t.bg3, 0.35f));
			c[ImGuiCol_Header] = V4(WithAlpha(t.accent, 0.14f));
			c[ImGuiCol_HeaderHovered] = V4(t.bg3);
			c[ImGuiCol_HeaderActive] = V4(WithAlpha(t.accent, 0.20f));
			c[ImGuiCol_Separator] = V4(t.line);
			c[ImGuiCol_SeparatorHovered] = V4(t.accentDim);
			c[ImGuiCol_SeparatorActive] = V4(t.accent);
			c[ImGuiCol_ResizeGrip] = none;
			c[ImGuiCol_ResizeGripHovered] = V4(t.accentDim);
			c[ImGuiCol_ResizeGripActive] = V4(t.accent);
			c[ImGuiCol_InputTextCursor] = V4(t.text);
			c[ImGuiCol_TabHovered] = V4(t.bg3);
			c[ImGuiCol_Tab] = none;
			c[ImGuiCol_TabSelected] = V4(t.bg2);
			c[ImGuiCol_TabSelectedOverline] = V4(t.accent);
			c[ImGuiCol_TabDimmed] = none;
			c[ImGuiCol_TabDimmedSelected] = V4(t.bg2);
			c[ImGuiCol_TabDimmedSelectedOverline] = V4(t.accentDim);
			c[ImGuiCol_PlotLines] = V4(t.muted);
			c[ImGuiCol_PlotLinesHovered] = V4(t.accent);
			c[ImGuiCol_PlotHistogram] = V4(t.accent);
			c[ImGuiCol_PlotHistogramHovered] = V4(t.select);
			c[ImGuiCol_TableHeaderBg] = V4(t.bg2);
			c[ImGuiCol_TableBorderStrong] = V4(t.line);
			c[ImGuiCol_TableBorderLight] = V4(detail::Mix(t.line, t.bg1, 0.7f));
			c[ImGuiCol_TableRowBg] = none;
			c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.018f);
			c[ImGuiCol_TextLink] = V4(t.accent);
			c[ImGuiCol_TextSelectedBg] = V4(WithAlpha(t.accent, 0.30f));
			c[ImGuiCol_TreeLines] = V4(t.line);
			c[ImGuiCol_DragDropTarget] = V4(t.accent);
			c[ImGuiCol_DragDropTargetBg] = V4(WithAlpha(t.accent, 0.10f));
			c[ImGuiCol_UnsavedMarker] = V4(t.accent);
			c[ImGuiCol_NavCursor] = V4(t.accent);
			c[ImGuiCol_NavWindowingHighlight] = V4(WithAlpha(t.accent, 0.70f));
			c[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.5f);
			c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
		}
	}

	void ApplyTheme(float a_dpiScale, float a_textScale)
	{
		const float dpi = a_dpiScale > 0.0f ? a_dpiScale : 1.0f;
		const float text = a_textScale > 0.0f ? a_textScale : 1.0f;
		const float scale = dpi * text;
		InitTokens(scale);
		g_theme.dpi = dpi;
		g_theme.textScale = text;
		ApplyStyle(scale);
		LoadFonts(scale);
		if (!g_themeApplied) {
			InstallRimHook();
		}
		g_themeApplied = true;
	}

	void RequestTextScale(float a_setting)
	{
		g_textSetting = a_setting > 0.0f ? std::clamp(a_setting, 0.5f, 4.0f) : 0.0f;
		g_pendingText = g_textSetting > 0.0f ? g_textSetting : g_autoText;
	}

	std::optional<float> TakePendingTextScale()
	{
		return std::exchange(g_pendingText, std::nullopt);
	}

	float AutoTextScale()
	{
		return g_autoText;
	}

	void SetAutoTextScale(float a_scale)
	{
		g_autoText = a_scale > 0.0f ? a_scale : 1.0f;
	}

	float TextScaleSetting()
	{
		return g_textSetting;
	}

	const Theme& Th()
	{
		if (!g_themeApplied && g_theme.bg0 == 0) {
			InitTokens(1.0f);
		}
		return g_theme;
	}

	ImFont* FontOf(Font a_font)
	{
		const Theme& t = Th();
		ImFont*      f = nullptr;
		switch (a_font) {
		case Font::Title:
			f = t.title;
			break;
		case Font::Heading:
			f = t.heading;
			break;
		case Font::Small:
			f = t.smallFont;
			break;
		case Font::Mono:
			f = t.mono;
			break;
		default:
			f = t.body;
			break;
		}
		return f ? f : ImGui::GetFont();
	}

	void PushFont(Font a_font)
	{
		ImFont* f = FontOf(a_font);
		ImGui::PushFont(f, f->LegacySize);
	}

	void PopFont()
	{
		ImGui::PopFont();
	}

	ImU32 WithAlpha(ImU32 a_colour, float a_alpha)
	{
		const float base = static_cast<float>((a_colour >> IM_COL32_A_SHIFT) & 0xFF);
		const int   a = static_cast<int>(base * std::clamp(a_alpha, 0.0f, 1.0f) + 0.5f);
		return (a_colour & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
	}
}
