// Win32 window + Direct3D 11 device + Dear ImGui backends, after Dear ImGui's example_win32_directx11.

#include <cstdio>
#include <filesystem>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "Util.h"
#include "render/Image.h"
#include "ui/App.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace dova::editor::ui
{
	namespace
	{
		ID3D11Device*           g_device = nullptr;
		ID3D11DeviceContext*    g_context = nullptr;
		IDXGISwapChain*         g_swapChain = nullptr;
		ID3D11RenderTargetView* g_rtv = nullptr;
		bool                    g_occluded = false;
		UINT                    g_resizeW = 0, g_resizeH = 0;

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

		void CleanupDevice()
		{
			CleanupRenderTarget();
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
			g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
			if (!back) {
				return false;
			}
			unsigned   w = 0, h = 0;
			const auto pixels = render::ReadTexture(g_device, g_context, back, w, h);
			back->Release();
			return !pixels.empty() && render::SaveImage(a_path, w, h, pixels);
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
			case WM_SYSCOMMAND:
				if ((a_wparam & 0xfff0) == SC_KEYMENU) {
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

	int RunGui(const WorkspaceOptions& a_options, const GuiOptions& a_gui)
	{
		const auto& a_screenshot = a_gui.screenshot;
		ImGui_ImplWin32_EnableDpiAwareness();
		const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

		WNDCLASSEXW wc{ sizeof(wc), CS_CLASSDC, WndProc, 0, 0, ::GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr, L"DovaStashEditor", nullptr };
		::RegisterClassExW(&wc);
		HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Dova Stash Editor", WS_OVERLAPPEDWINDOW, 80, 60, static_cast<int>(1600 * scale),
			static_cast<int>(950 * scale), nullptr, nullptr, wc.hInstance, nullptr);
		if (!CreateDevice(hwnd)) {
			CleanupDevice();
			::UnregisterClassW(wc.lpszClassName, wc.hInstance);
			::MessageBoxW(nullptr, L"Direct3D 11 is not available.", L"Dova Stash Editor", MB_ICONERROR);
			return 1;
		}
		::ShowWindow(hwnd, SW_SHOWDEFAULT);
		::UpdateWindow(hwnd);

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.IniFilename = nullptr;  // the layout is fixed; nothing to remember yet
		ImGui::StyleColorsDark();
		ImGuiStyle& style = ImGui::GetStyle();
		style.ScaleAllSizes(scale);
		style.FontScaleDpi = scale;
		std::error_code ec;
		if (std::filesystem::exists(L"C:\\Windows\\Fonts\\segoeui.ttf", ec)) {
			io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 17.0f);
		}
		ImGui_ImplWin32_Init(hwnd);
		ImGui_ImplDX11_Init(g_device, g_context);

		{
			EditorApp app(a_options, a_gui, g_device, g_context);
			bool      done = false;
			int       idleFrames = 0;
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
				if (g_resizeW != 0 && g_resizeH != 0) {
					CleanupRenderTarget();
					g_swapChain->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
					g_resizeW = g_resizeH = 0;
					CreateRenderTarget();
				}

				ImGui_ImplDX11_NewFrame();
				ImGui_ImplWin32_NewFrame();
				ImGui::NewFrame();
				app.Frame();
				ImGui::Render();
				const float clear[4] = { 0.08f, 0.08f, 0.09f, 1.0f };
				g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
				g_context->ClearRenderTargetView(g_rtv, clear);
				ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
				if (a_screenshot) {
					idleFrames = app.Idle() ? idleFrames + 1 : 0;
					if (idleFrames > 90) {  // frames for layout (tables, fit) after loading, and a settled frame rate
						SaveBackBuffer(PathFromUtf8(*a_screenshot));
						done = true;
					}
				}
				g_occluded = g_swapChain->Present(1, 0) == DXGI_STATUS_OCCLUDED;
			}
		}

		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		CleanupDevice();
		::DestroyWindow(hwnd);
		::UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return 0;
	}
}
