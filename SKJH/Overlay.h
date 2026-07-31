#pragma once
/*
 * Overlay.h — 透明穿透叠加层 (移植自 KAKA 项目)
 *
 * 特性：
 *   - WS_EX_LAYERED + LWA_COLORKEY 黑色透明
 *   - 窗口标题伪装 "NVIDIA GeForce Overlay"
 *   - 类名伪装 "CEF-OSC-WIDGET"
 *   - 鼠标穿透动态切换 (游戏内穿透 / 菜单可交互)
 *   - D3D11 + ImGui 渲染
 */

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <functional>
#include <chrono>
#include <thread>
#include <mmsystem.h>
#include <algorithm>
#include <vector>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include "HoloUI.h"
#include "Font.h"

// ImGui 内部符号声明
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include <cstdio>
#include <cstdlib>
extern int g_lang;  // 语言选择 (0=中文, 1=英文), 实际定义在 main.h

// ═══════════════════════════════════════
//  统一错误退出：不恢复控制台或显示内部数据。
// ═══════════════════════════════════════
inline void WaitEnterAndExit(int code) {
    MessageBoxW(nullptr, L"程序启动失败，已退出。",
                L"SKJH", MB_OK | MB_ICONERROR);
    exit(code);
}

// ── 全局 D3D 设备 ──
inline ID3D11Device*            g_pd3dDevice        = nullptr;
inline ID3D11DeviceContext*     g_pd3dDeviceContext  = nullptr;
inline IDXGISwapChain*          g_pSwapChain         = nullptr;
inline ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;
inline HWND                     g_OverlayHwnd = nullptr;   // overlay 窗口句柄
inline int                      g_DisplayMode = 1;         // 0=扩展 1=复制（默认）
inline int                      g_CurMonitor  = 0;         // 0=主屏，1=首个副屏
inline bool                     g_OverlayVisible = true;

// 帧率限制 (由 main.h 定义, Overlay 读取)
extern bool g_FpsLimitEnabled;
extern int  g_FpsLimit;

using DrawFunc = std::function<void()>;

struct SKJH_MonitorRect {
    RECT rect{};
    bool primary = false;
};

inline std::vector<SKJH_MonitorRect> EnumerateMonitorRects() {
    std::vector<SKJH_MonitorRect> monitors;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
            auto* output = reinterpret_cast<
                std::vector<SKJH_MonitorRect>*>(context);
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info)) {
                output->push_back({
                    info.rcMonitor,
                    (info.dwFlags & MONITORINFOF_PRIMARY) != 0});
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitors));
    std::stable_sort(
        monitors.begin(), monitors.end(),
        [](const SKJH_MonitorRect& left,
           const SKJH_MonitorRect& right) {
            return left.primary && !right.primary;
        });
    return monitors;
}

inline bool TryGetMonitorRect(int index, RECT& rect) {
    const auto monitors = EnumerateMonitorRects();
    if (index < 0 || index >= static_cast<int>(monitors.size()))
        return false;
    rect = monitors[static_cast<size_t>(index)].rect;
    return rect.right > rect.left && rect.bottom > rect.top;
}

// index 0 始终是主屏，index 1 是第一个真实副屏。
inline RECT GetMonitorRect(int index) {
    RECT rect{};
    if (!TryGetMonitorRect(index, rect)) {
        rect.left = 0;
        rect.top = 0;
        rect.right = GetSystemMetrics(SM_CXSCREEN);
        rect.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    return rect;
}

// ======================== Overlay 工具函数 ========================

inline void CleanupDeviceD3D();

inline bool CreateDeviceD3D(HWND hwnd, int w, int h) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width  = w;
    sd.BufferDesc.Height = h;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count   = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
        D3D11_SDK_VERSION, &sd, &g_pSwapChain,
        &g_pd3dDevice, nullptr, &g_pd3dDeviceContext);
    if (FAILED(hr)) {
        CleanupDeviceD3D();
        return false;
    }

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (FAILED(hr) || !pBackBuffer) {
        CleanupDeviceD3D();
        return false;
    }
    hr = g_pd3dDevice->CreateRenderTargetView(
        pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
    if (FAILED(hr) || !g_mainRenderTargetView) {
        CleanupDeviceD3D();
        return false;
    }
    return true;
}

inline void CleanupDeviceD3D() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
    if (g_pSwapChain)          { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext)   { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice)          { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

// 鼠标穿透切换
inline void ClickThrough(bool through) {
    if (!g_OverlayHwnd) return;
    LONG ex = GetWindowLong(g_OverlayHwnd, GWL_EXSTYLE);
    if (through)
        ex |= WS_EX_TRANSPARENT;
    else
        ex &= ~WS_EX_TRANSPARENT;
    SetWindowLong(g_OverlayHwnd, GWL_EXSTYLE, ex);
}

// 移动窗口到指定显示器
inline void MoveToMonitor(int idx) {
    RECT r{};
    if (!TryGetMonitorRect(idx, r)) {
        idx = 0;
        r = GetMonitorRect(0);
    }
    SetWindowPos(g_OverlayHwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER);
    g_CurMonitor = idx;
}

inline int CurrentMonitor() { return g_CurMonitor; }

inline RECT GetDisplayModeRect(int mode, int* monitorIndex = nullptr) {
    const int requestedMonitor = mode == 0 ? 1 : 0;
    RECT rect{};
    int resolvedMonitor = requestedMonitor;
    if (!TryGetMonitorRect(requestedMonitor, rect)) {
        rect = GetMonitorRect(0);
        resolvedMonitor = 0;
    }
    if (monitorIndex) *monitorIndex = resolvedMonitor;
    return rect;
}

// 可在 UI 中即时切换复制/扩展模式。SetWindowPos 会触发 WM_SIZE，
// 交换链由窗口过程同步调整，不需要重建 D3D 设备。
inline bool SetOverlayDisplayMode(int mode) {
    if (mode != 0 && mode != 1) return false;
    if (mode == 0) {
        RECT secondary{};
        if (!TryGetMonitorRect(1, secondary)) return false;
    }
    int monitorIndex = 0;
    const RECT rect = GetDisplayModeRect(mode, &monitorIndex);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) return false;

    g_DisplayMode = mode;
    g_CurMonitor = monitorIndex;
    if (!g_OverlayHwnd) return true;
    const BOOL moved = SetWindowPos(
        g_OverlayHwnd, HWND_TOPMOST, rect.left, rect.top, width, height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (moved) {
        g_OverlayVisible = true;
        ShowWindow(g_OverlayHwnd, SW_SHOWNOACTIVATE);
    }
    return moved != FALSE;
}

inline void BeginDraw() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

// ======================== Dark Purple Tech UI Theme ========================
inline void SetupImGuiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Text]                   = ImVec4(0.878f, 0.878f, 0.878f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.627f, 0.627f, 0.722f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.051f, 0.051f, 0.102f, 0.97f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.071f, 0.063f, 0.149f, 0.60f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.059f, 0.039f, 0.102f, 0.96f);
    colors[ImGuiCol_Border]                 = ImVec4(0.486f, 0.227f, 0.929f, 0.55f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.10f, 0.04f, 0.20f, 0.30f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.176f, 0.106f, 0.306f, 0.65f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.290f, 0.176f, 0.478f, 0.45f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.420f, 0.247f, 0.627f, 0.70f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.071f, 0.063f, 0.149f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.176f, 0.106f, 0.306f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.05f, 0.04f, 0.10f, 0.60f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.059f, 0.050f, 0.130f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.039f, 0.039f, 0.080f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.486f, 0.227f, 0.929f, 0.90f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.655f, 0.545f, 0.980f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.769f, 0.710f, 0.992f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.655f, 0.545f, 0.980f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.486f, 0.227f, 0.929f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.769f, 0.710f, 0.992f, 1.00f);
    colors[ImGuiCol_Button]                 = ImVec4(0.290f, 0.176f, 0.478f, 0.50f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.486f, 0.227f, 0.929f, 0.90f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.655f, 0.545f, 0.980f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.102f, 0.063f, 0.180f, 0.40f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.486f, 0.227f, 0.929f, 0.70f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.655f, 0.545f, 0.980f, 1.00f);
    colors[ImGuiCol_Separator]              = ImVec4(0.290f, 0.176f, 0.478f, 0.45f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.655f, 0.545f, 0.980f, 0.80f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.769f, 0.710f, 0.992f, 1.00f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.486f, 0.227f, 0.929f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.655f, 0.545f, 0.980f, 0.70f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.769f, 0.710f, 0.992f, 1.00f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.176f, 0.106f, 0.306f, 0.85f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.486f, 0.227f, 0.929f, 0.80f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.655f, 0.545f, 0.980f, 1.00f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.06f, 0.05f, 0.12f, 0.90f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.137f, 0.086f, 0.247f, 1.00f);

    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 5.0f;

    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.TabBorderSize     = 0.0f;

    style.WindowPadding      = ImVec2(12.0f, 12.0f);
    style.FramePadding       = ImVec2(8.0f, 5.0f);
    style.ItemSpacing        = ImVec2(10.0f, 7.0f);
    style.ItemInnerSpacing   = ImVec2(7.0f, 6.0f);
    style.ScrollbarSize      = 14.0f;
    style.GrabMinSize        = 10.0f;
    style.WindowMinSize      = ImVec2(200.0f, 200.0f);

    style.AntiAliasedLines       = true;
    style.AntiAliasedFill        = true;
    style.AntiAliasedLinesUseTex = true;
    style.CurveTessellationTol   = 1.25f;
    style.CircleTessellationMaxError = 0.30f;
}

inline void EndDraw() {
    static const float clear[4] = {0,0,0,0};
    ImGui::Render();
    if (!g_pd3dDeviceContext || !g_mainRenderTargetView || !g_pSwapChain)
        return;
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_pSwapChain->Present(0, 0);  // vsync=0
}

inline LRESULT WINAPI OverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg) {
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_SIZE:
        if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
            const UINT width = LOWORD(lParam);
            const UINT height = HIWORD(lParam);
            if (width == 0 || height == 0) return 0;
            if (g_pd3dDeviceContext)
                g_pd3dDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
            if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
            if (g_pSwapChain &&
                SUCCEEDED(g_pSwapChain->ResizeBuffers(
                    0, width, height, DXGI_FORMAT_UNKNOWN, 0))) {
                ID3D11Texture2D* pBackBuffer = nullptr;
                if (SUCCEEDED(g_pSwapChain->GetBuffer(
                        0, IID_PPV_ARGS(&pBackBuffer)))) {
                    g_pd3dDevice->CreateRenderTargetView(
                        pBackBuffer, nullptr, &g_mainRenderTargetView);
                    pBackBuffer->Release();
                }
            }
        }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

inline void RequestOverlayExit() {
    if (g_OverlayHwnd) PostMessageW(g_OverlayHwnd, WM_CLOSE, 0, 0);
}

// ======================== Overlay 初始化 & 主循环 ========================
// mode: 0=扩展(副屏黑底, F6切换显示器)  1=复制(主屏黑底, F6显示/隐藏)
inline int OverlayRun(DrawFunc draw, int mode) {
    if (mode != 0 && mode != 1) mode = 1;
    if (mode == 0) {
        RECT secondary{};
        if (!TryGetMonitorRect(1, secondary)) mode = 1;
    }
    g_DisplayMode = mode;

    int initialMonitor = 0;
    RECT mr = GetDisplayModeRect(mode, &initialMonitor);
    g_CurMonitor = initialMonitor;
    int sw = mr.right - mr.left, sh = mr.bottom - mr.top;
    if (sw <= 0 || sh <= 0) { sw = GetSystemMetrics(SM_CXSCREEN); sh = GetSystemMetrics(SM_CYSCREEN); }

    // 注册窗口类
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = OverlayWndProc;
    wc.hbrBackground = CreateSolidBrush(RGB(0,0,0));
    wc.lpszClassName = L"CEF-OSC-WIDGET";
    RegisterClassExW(&wc);

    DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST;

    g_OverlayHwnd = CreateWindowExW(exStyle,
        L"CEF-OSC-WIDGET", L"NVIDIA GeForce Overlay", WS_POPUP,
        mr.left, mr.top, sw, sh, nullptr, nullptr, nullptr, nullptr);
    if (!g_OverlayHwnd) WaitEnterAndExit(1);

    ShowWindow(g_OverlayHwnd, SW_SHOWNOACTIVATE);

    if (!CreateDeviceD3D(g_OverlayHwnd, sw, sh)) {
        CleanupDeviceD3D();
        DestroyWindow(g_OverlayHwnd);
        WaitEnterAndExit(1);
    }

    // ImGui 初始化
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    // The shipped overlay is a production surface.  Keep ImGui's recovery
    // diagnostics out of the hidden console and prevent its programmer-only
    // ID conflict popup from appearing over the game.
    io.ConfigErrorRecoveryEnableDebugLog = false;
    io.ConfigDebugIsDebuggerPresent = false;
    io.ConfigDebugHighlightIdConflicts = false;
    io.ConfigDebugHighlightIdConflictsShowItemPicker = false;
    ImFontConfig fontConfig{};
    fontConfig.FontDataOwnedByAtlas = false;
    ImFont* embeddedFont = io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned int*>(Font_data), Font_size, 17.0f,
        &fontConfig, io.Fonts->GetGlyphRangesChineseFull());
    if (!embeddedFont) {
        // A damaged/unsupported embedded atlas must not leave an empty font
        // stack, which makes the entire UI appear blank.
        io.Fonts->Clear();
        io.Fonts->AddFontDefault();
    }

    ImGui_ImplWin32_Init(g_OverlayHwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // 应用赛博朋克 UI 主题
    SetupImGuiStyle();

    g_OverlayVisible = true;

    // 提高系统定时器精度到 1ms (默认 15.6ms, 会导致 sleep_for 精度极差)
    timeBeginPeriod(1);

    // ── 主循环 ──
    bool running = true;
    while (running) {
        auto frameStart = std::chrono::steady_clock::now();

        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        BeginDraw();
        draw();
        EndDraw();

        // 帧率限制: 混合策略 = 粗睡 + 自旋精等, 突破 Windows 15.6ms 定时器限制
        if (g_FpsLimitEnabled && g_FpsLimit > 0) {
            auto targetTime = std::chrono::microseconds(1000000 / g_FpsLimit);
            auto elapsed = std::chrono::steady_clock::now() - frameStart;
            if (elapsed < targetTime) {
                auto remaining = targetTime - elapsed;
                // 粗睡: 留 2ms 余量给自旋, 避免睡过头
                auto coarseSleep = remaining - std::chrono::microseconds(2000);
                if (coarseSleep > std::chrono::microseconds(0))
                    std::this_thread::sleep_for(coarseSleep);
                // 自旋精等: busy-wait 填补最后 ~2ms, 精度到微秒级
                while (std::chrono::steady_clock::now() - frameStart < targetTime)
                    std::this_thread::yield();
            }
        }
    }

    timeEndPeriod(1);

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(g_OverlayHwnd);
    UnregisterClassW(L"CEF-OSC-WIDGET", nullptr);
    return 0;
}
