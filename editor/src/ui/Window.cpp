// Win32 window + Direct3D 11 device + Dear ImGui backends, after Dear ImGui's example_win32_directx11.

#include <cstdio>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>

#include <imgui.h>
#include <imgui_internal.h>  // DebugLogBuf: ImGui's recoverable errors into our log
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "Log.h"
#include "Util.h"
#include "Workspace.h"
#include "data/Assets.h"
#include "home/Cell.h"
#include "render/Scene.h"
#include "render/Image.h"
#include "ui/App.h"
#include "ui/Art.h"
#include "ui/Widgets.h"
#include "ui/WidgetsDemo.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace dova::editor::ui
{
	namespace
	{
		ID3D11Device*           g_device = nullptr;
		ID3D11DeviceContext*    g_context = nullptr;
		IDXGISwapChain*         g_swapChain = nullptr;
		ID3D11RenderTargetView* g_rtv = nullptr;
		ID3D11Texture2D*        g_offscreen = nullptr;  // a screenshot run without a desktop: the frame, no swap chain
		bool                    g_occluded = false;
		UINT                    g_resizeW = 0, g_resizeH = 0;
		EditorApp*              g_app = nullptr;
		bool                    g_unclamped = false;  // --screenshot: the window may be larger than the screen
		float                   g_dpi = 1.0f;         // the window's monitor; WM_DPICHANGED updates it
		bool                    g_dpiChanged = false;

		// Text size when the setting is Auto: at 100 % Windows scaling a 1440p or 4K monitor still shows the
		// body text too small to read at arm's length; Windows scaling above 100 % already handles it.
		float AutoTextScaleFor(HMONITOR a_monitor, float a_dpi)
		{
			if (a_dpi > 1.05f) {
				return 1.0f;
			}
			MONITORINFO mi{ sizeof(mi) };
			if (!::GetMonitorInfoW(a_monitor, &mi)) {
				return 1.0f;
			}
			const int h = mi.rcMonitor.bottom - mi.rcMonitor.top;
			return h >= 2000 ? 1.5f : h >= 1300 ? 1.25f : 1.0f;
		}

		void CreateRenderTarget()
		{
			ID3D11Texture2D* back = nullptr;
			g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
			if (back) {
				g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
				back->Release();
			}
		}

		void CleanupRenderTarget()
		{
			if (g_rtv) {
				g_rtv->Release();
				g_rtv = nullptr;
			}
		}

		bool CreateDevice(HWND a_hwnd)
		{
			DXGI_SWAP_CHAIN_DESC sd{};
			sd.BufferCount = 2;
			sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			sd.BufferDesc.RefreshRate = { 60, 1 };
			sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
			sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			sd.OutputWindow = a_hwnd;
			sd.SampleDesc.Count = 1;
			sd.Windowed = TRUE;
			sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
			const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
			D3D_FEATURE_LEVEL       level{};
			HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swapChain,
				&g_device, &level, &g_context);
			if (hr == DXGI_ERROR_UNSUPPORTED) {
				hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device,
					&level, &g_context);
			}
			if (hr != S_OK) {
				return false;
			}
			CreateRenderTarget();
			return true;
		}

		// No interactive desktop (session 0): the device alone, the frames drawn into a texture of the window's size.
		bool CreateOffscreenDevice(UINT a_width, UINT a_height)
		{
			const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
			D3D_FEATURE_LEVEL       level{};
			HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &g_device, &level, &g_context);
			if (FAILED(hr)) {
				hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &g_device, &level, &g_context);
			}
			if (FAILED(hr)) {
				return false;
			}
			D3D11_TEXTURE2D_DESC td{};
			td.Width = a_width;
			td.Height = a_height;
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_RENDER_TARGET;
			return SUCCEEDED(g_device->CreateTexture2D(&td, nullptr, &g_offscreen)) && SUCCEEDED(g_device->CreateRenderTargetView(g_offscreen, nullptr, &g_rtv));
		}

		void CleanupDevice()
		{
			CleanupRenderTarget();
			if (g_offscreen) {
				g_offscreen->Release();
				g_offscreen = nullptr;
			}
			if (g_swapChain) {
				g_swapChain->Release();
			}
			if (g_context) {
				g_context->Release();
			}
			if (g_device) {
				g_device->Release();
			}
		}

		// Reads back the frame just rendered (before Present) and writes it (PNG, or BMP for .bmp).
		bool SaveBackBuffer(const std::filesystem::path& a_path)
		{
			ID3D11Texture2D* back = nullptr;
			if (g_offscreen) {
				back = g_offscreen;
				back->AddRef();
			} else {
				g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
			}
			if (!back) {
				return false;
			}
			unsigned   w = 0, h = 0;
			const auto pixels = render::ReadTexture(g_device, g_context, back, w, h);
			back->Release();
			return !pixels.empty() && render::SaveImage(a_path, w, h, pixels);
		}

		std::wstring EnvVar(const wchar_t* a_name)
		{
			wchar_t     buf[64]{};
			const DWORD n = ::GetEnvironmentVariableW(a_name, buf, 64);
			return n > 0 && n < 64 ? std::wstring(buf, n) : std::wstring();
		}

		LRESULT WINAPI WndProc(HWND a_hwnd, UINT a_msg, WPARAM a_wparam, LPARAM a_lparam)
		{
			if (ImGui_ImplWin32_WndProcHandler(a_hwnd, a_msg, a_wparam, a_lparam)) {
				return true;
			}
			switch (a_msg) {
			case WM_SIZE:
				if (a_wparam == SIZE_MINIMIZED) {
					return 0;
				}
				g_resizeW = LOWORD(a_lparam);
				g_resizeH = HIWORD(a_lparam);
				return 0;
			case WM_DPICHANGED: {
				// Moved to a monitor with another scale: take Windows' suggested size, rebuild fonts next frame.
				g_dpi = static_cast<float>(HIWORD(a_wparam)) / 96.0f;
				g_dpiChanged = true;
				const RECT* r = reinterpret_cast<const RECT*>(a_lparam);
				::SetWindowPos(a_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
				return 0;
			}
			case WM_GETMINMAXINFO:
				if (g_unclamped) {
					// Screenshots keep their size on a small (or virtual) display.
					auto* mmi = reinterpret_cast<MINMAXINFO*>(a_lparam);
					mmi->ptMaxTrackSize = POINT{ 16384, 16384 };
					return 0;
				}
				break;
			case WM_SYSCOMMAND:
				if ((a_wparam & 0xfff0) == SC_KEYMENU) {
					return 0;
				}
				break;
			case WM_CLOSE:
				// The editor asks first when something is not saved (EditorApp::RequestClose).
				if (g_app) {
					g_app->RequestClose();
					return 0;
				}
				break;
			case WM_DESTROY:
				::PostQuitMessage(0);
				return 0;
			default:
				break;
			}
			return ::DefWindowProcW(a_hwnd, a_msg, a_wparam, a_lparam);
		}
	}

	namespace
	{
		// The process's window station is the interactive one (WSF_VISIBLE): a window can be shown and presented.
		// Session 0 (a service, some agents' shells) has none: a window there never gets a frame on screen.
		bool DesktopVisible()
		{
			USEROBJECTFLAGS flags{};
			DWORD           needed = 0;
			const HWINSTA   station = ::GetProcessWindowStation();
			return station && ::GetUserObjectInformationW(station, UOI_FLAGS, &flags, sizeof(flags), &needed) && (flags.dwFlags & WSF_VISIBLE) != 0;
		}

		// An unattended run (--screenshot): an error goes to stderr and the log; nobody could dismiss a message box.
		void Fail(bool a_unattended, const char* a_text)
		{
			Log("error: {}", a_text);
			if (a_unattended) {
				std::fprintf(stderr, "error: %s\n", a_text);
				std::fflush(stderr);
			} else {
				::MessageBoxA(nullptr, a_text, "Dova Stash Editor", MB_ICONERROR);
			}
		}
	}

	int RunGui(const WorkspaceOptions& a_options, const GuiOptions& a_gui)
	{
		const auto& a_screenshot = a_gui.screenshot;
		// Without an interactive desktop (session 0) a screenshot run draws off-screen: a message-only window for the
		// Win32 backend, the frames in a texture of --window size, saved as the window would be. A window is refused.
		bool offscreen = false;
		{
			DWORD session = 0;
			::ProcessIdToSessionId(::GetCurrentProcessId(), &session);
			const bool visible = DesktopVisible();
			Log("window: starting, session {}, desktop visible {}{}", session, visible ? "yes" : "no", a_screenshot ? ", screenshot " + *a_screenshot : std::string{});
			if (!visible && !a_screenshot) {
				std::fputs("No interactive desktop: run from the user's session, or add --screenshot\n", stderr);
				std::fflush(stderr);
				Log("window: no interactive desktop, exit 3");
				return 3;
			}
			offscreen = !visible;
		}
		const auto target = a_gui.window.value_or(std::pair{ 1600, 900 });
		// A screenshot run's deadline (--timeout, 180 s): then it saves what it has and exits 4. A run that stops drawing
		// (a hung present, a blocked call, a hang before the first frame) is ended by the watchdog thread 30 s later.
		const auto        started = std::chrono::steady_clock::now();
		const auto        deadline = started + std::chrono::seconds(std::max(a_gui.timeout, 1));
		std::atomic<bool> finished{ false };
		std::jthread      watchdog;
		if (a_screenshot) {
			watchdog = std::jthread([deadline, &finished](std::stop_token a_stop) {
				while (!a_stop.stop_requested() && !finished) {
					if (std::chrono::steady_clock::now() > deadline + std::chrono::seconds(30)) {
						Log("screenshot: no frame for 30 s after the deadline, exit 4");
						std::fputs("error: screenshot timed out (the window stopped drawing)\n", stderr);
						std::fflush(stderr);
						::TerminateProcess(::GetCurrentProcess(), 4);
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(250));
				}
			});
		}
		ImGui_ImplWin32_EnableDpiAwareness();
		const HMONITOR primary = ::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
		g_dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(primary);
		// DOVASTASH_UI_SCALE=1.5 tries another DPI without changing the monitor's; DOVASTASH_WIDGETS_DEMO=1 draws
		// the widget gallery (ui/WidgetsDemo.cpp) instead of the editor.
		bool fixedScale = false;
		if (const std::wstring v = EnvVar(L"DOVASTASH_UI_SCALE"); !v.empty()) {
			if (const float s = std::wcstof(v.c_str(), nullptr); s >= 0.5f && s <= 4.0f) {
				g_dpi = s;
				fixedScale = true;
			}
		}
		// Text size: --text-size for this run, else the remembered setting (ui.textScale in state.json), or Auto from
		// the monitor. Screenshots and DOVASTASH_UI_SCALE runs keep 100 % (unless --text-size) so their pixels mean the
		// same on every machine.
		w::SetAutoTextScale((a_screenshot && !a_gui.textSize) || fixedScale ? 1.0f : AutoTextScaleFor(primary, g_dpi));
		// The theme: --theme for this run, else the remembered one (screenshots: Dova Stash unless --theme).
		float       textSetting = 0.0f;
		std::string theme = a_gui.theme.value_or(std::string{});
		if (!a_screenshot) {
			EditorState saved;
			saved.Load();
			if (!fixedScale) {
				textSetting = saved.ui.textScale;
			}
			if (!a_gui.theme) {
				theme = saved.ui.theme;
			}
		}
		if (a_gui.textSize) {
			textSetting = *a_gui.textSize;
		}
		w::SetTheme(theme);
		w::RequestTextScale(textSetting);
		float scale = g_dpi * *w::TakePendingTextScale();
		const bool demo = EnvVar(L"DOVASTASH_WIDGETS_DEMO") == L"1";
		g_unclamped = a_screenshot.has_value();

		// Title bar and taskbar use the exe's own icon (res/DovaStashEditor.rc, resource 1).
		const auto  instance = ::GetModuleHandleW(nullptr);
		const auto  icon = static_cast<HICON>(::LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
		const auto  iconSmall = static_cast<HICON>(::LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0));
		WNDCLASSEXW wc{ sizeof(wc), CS_CLASSDC, WndProc, 0, 0, instance, icon, nullptr, nullptr, nullptr, L"DovaStashEditor", iconSmall };
		::RegisterClassExW(&wc);
		HWND hwnd = offscreen ? ::CreateWindowExW(0, wc.lpszClassName, L"Dova Stash Editor", 0, 0, 0, target.first, target.second, HWND_MESSAGE, nullptr, wc.hInstance, nullptr)
		                      : ::CreateWindowW(wc.lpszClassName, L"Dova Stash Editor", WS_OVERLAPPEDWINDOW, 80, 60, static_cast<int>(1600 * scale),
								  static_cast<int>(950 * scale), nullptr, nullptr, wc.hInstance, nullptr);
		if (offscreen) {
			Log("window: off-screen, {}x{}", target.first, target.second);
		}
		if (!hwnd || !(offscreen ? CreateOffscreenDevice(static_cast<UINT>(target.first), static_cast<UINT>(target.second)) : CreateDevice(hwnd))) {
			CleanupDevice();
			::UnregisterClassW(wc.lpszClassName, wc.hInstance);
			Fail(a_screenshot.has_value(), "Direct3D 11 is not available.");
			return 1;
		}
		if (!offscreen) {
			::ShowWindow(hwnd, SW_SHOWDEFAULT);
			::UpdateWindow(hwnd);
		}

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		// ImGui's recoverable errors (a misused API: the red "Message from Dear ImGui" tooltip) go to its debug log
		// too; the loop below copies each into editor.log once, and a screenshot run exits 5 on any (docs/EDITOR.md).
		io.ConfigErrorRecoveryEnableDebugLog = true;
		ImGuiContext& imgui = *ImGui::GetCurrentContext();
		imgui.DebugLogFlags = (imgui.DebugLogFlags | ImGuiDebugLogFlags_EventError) & ~ImGuiDebugLogFlags_OutputToTTY;  // ours, not the console
		io.IniFilename = nullptr;  // the layout is fixed; nothing to remember yet
		w::ApplyTheme(g_dpi, scale / g_dpi);  // style, colours and fonts (Widgets.h, docs/EDITOR-UX.md §3)
		ImGui_ImplWin32_Init(hwnd);
		ImGui_ImplDX11_Init(g_device, g_context);

		int rc = 0;
		{
			EditorApp app(a_options, a_gui, g_device, g_context);
			g_app = &app;
			bool      timedOut = false;
			std::set<std::string> imguiErrors;  // each logged once
			bool      done = false;
			int       idleFrames = 0;
			int       frames = 0;
			// The gallery shows the game-read art too: its own Assets over the same workspace.
			// The gallery shows the game-read art and mesh thumbnails too: its own Assets and a scene of
			// Breezehome over the same workspace.
			std::jthread      demoLoad;
			std::atomic<bool> demoLoaded{ false };
			if (demo) {
				w::SetDemoDevice(g_device, g_context);
				demoLoad = std::jthread([options = a_options, device = g_device, &demoLoaded] {
					Workspace   ws;
					std::string error;
					if (ws.Open(options, error)) {
						auto assets = std::make_shared<Assets>(ws.Files().Roots(), Assets::ArchiveList(ws));
						LoadArt(assets, device);
						std::vector<std::string> errors;
						auto                     lo = ws.NewLoadOrder();
						for (const char* p : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" }) {
							lo->AddWithMasters(p, errors);
						}
						const auto key = lo->FindCellByEditorId("WhiterunBreezehome");
						if (const auto cell = key ? home::LoadCell(*lo, *key) : std::nullopt) {
							auto scene = std::make_shared<render::Scene>(device, assets, render::BuildSceneDesc(*cell));
							scene->Start();
							w::SetDemoScene(scene);
							scene->Wait();
							for (int i = 0; i < 1200 && !scene->Progress().finished; ++i) {
								std::this_thread::sleep_for(std::chrono::milliseconds(50));
							}
						}
					}
					demoLoaded = true;
				});
			}
			while (!done) {
				MSG msg;
				while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
					::TranslateMessage(&msg);
					::DispatchMessageW(&msg);
					if (msg.message == WM_QUIT) {
						done = true;
					}
				}
				if (done) {
					break;
				}
				if (!a_screenshot && g_occluded && g_swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
					::Sleep(10);
					continue;
				}
				g_occluded = false;
				if (offscreen) {
					g_resizeW = g_resizeH = 0;  // the texture keeps --window's size
				}
				if (g_resizeW != 0 && g_resizeH != 0) {
					CleanupRenderTarget();
					g_swapChain->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
					g_resizeW = g_resizeH = 0;
					CreateRenderTarget();
				}

				// A new theme (Setup, the palette): colours and style between frames, the fonts stay.
				if (const auto name = w::TakePendingTheme()) {
					w::SetTheme(*name);
					w::ApplyColours();
				}
				// A new text size (Setup, Ctrl+= / Ctrl+-) or DPI: fonts and sizes are rebuilt between frames.
				if (const auto text = w::TakePendingTextScale(); text || g_dpiChanged) {
					g_dpiChanged = false;
					w::ApplyTheme(g_dpi, text ? *text : w::Th().textScale);
					ImGui_ImplDX11_InvalidateDeviceObjects();
					ImGui_ImplDX11_CreateDeviceObjects();
				}
				ImGui_ImplDX11_NewFrame();
				ImGui_ImplWin32_NewFrame();
				if (offscreen) {
					io.DisplaySize = ImVec2(static_cast<float>(target.first), static_cast<float>(target.second));
				}
				ImGui::NewFrame();
				if (demo) {
					w::DrawDemo();
				} else {
					app.Frame();
				}
				ImGui::Render();
				// ImGui's errors this frame, from its debug log (then cleared, so it never grows).
				if (ImGuiContext& g = *ImGui::GetCurrentContext(); !g.DebugLogBuf.empty()) {
					std::string_view buf(g.DebugLogBuf.c_str(), static_cast<std::size_t>(g.DebugLogBuf.size()));
					while (!buf.empty()) {
						const auto eol = buf.find('\n');
						std::string_view line = buf.substr(0, eol);
						buf = eol == std::string_view::npos ? std::string_view{} : buf.substr(eol + 1);
						// "[01264] [imgui-error] In window ...": without ImGui's frame number, so each error logs once.
						if (const auto at = line.find("[imgui-error] "); at != std::string_view::npos) {
							line.remove_prefix(at + 14);
							if (!line.starts_with("(current settings") && imguiErrors.emplace(line).second) {
								Log("imgui error: {}", line);
							}
						}
					}
					g.DebugLogBuf.clear();
					g.DebugLogIndex.clear();
				}
				const ImVec4 bg0 = ImGui::ColorConvertU32ToFloat4(w::Th().bg0);
				const float  clear[4] = { bg0.x, bg0.y, bg0.z, 1.0f };
				g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
				g_context->ClearRenderTargetView(g_rtv, clear);
				ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
				if (a_screenshot) {
					// The gallery does not run the editor's frame (its loading would never settle): it waits for its
					// art and scene instead, a minute at most.
					const bool ready = demo ? (demoLoaded || frames > 3600) : app.Idle();
					idleFrames = ready ? idleFrames + 1 : 0;
					if (idleFrames > 90) {  // frames for layout (tables, fit) after loading, and a settled frame rate
						SaveBackBuffer(PathFromUtf8(*a_screenshot));
						done = true;
					} else if (std::chrono::steady_clock::now() > deadline) {
						const auto what = demo ? std::string("the gallery") : app.WaitingFor();
						Log("screenshot timed out waiting for {} after {} s: saving what it has, exit 4", what, a_gui.timeout);
						std::fprintf(stderr, "error: screenshot timed out waiting for %s\n", what.c_str());
						std::fflush(stderr);
						SaveBackBuffer(PathFromUtf8(*a_screenshot));
						timedOut = true;
						done = true;
					}
				}
				++frames;
				if (offscreen) {
					g_context->Flush();
					::Sleep(16);  // about the frame rate a presented window keeps
				} else {
					g_occluded = g_swapChain->Present(1, 0) == DXGI_STATUS_OCCLUDED;
				}
				if (app.ShouldQuit()) {
					done = true;
				}
			}
			finished = true;
			if (watchdog.joinable()) {
				watchdog.request_stop();
				watchdog.join();
			}
			rc = timedOut ? 4 : a_screenshot && (app.HomeMissing() || app.Failed()) ? 1 : a_screenshot && !imguiErrors.empty() ? 5 : 0;
			if (a_screenshot && !imguiErrors.empty()) {
				std::fprintf(stderr, "error: %zu Dear ImGui error(s), see the log\n", imguiErrors.size());
				std::fflush(stderr);
			}
			g_app = nullptr;
			if (demoLoad.joinable()) {
				demoLoad.join();
			}
			w::ReleaseDemo();
		}
		ReleaseArt();

		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		CleanupDevice();
		::DestroyWindow(hwnd);
		::UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return rc;
	}
}
