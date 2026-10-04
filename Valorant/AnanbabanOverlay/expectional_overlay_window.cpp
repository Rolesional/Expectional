/** CS2-External-Base overlay — UC #229070 Z-order (TOPMOST yok). */

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <windowsx.h>
#include <dxgi1_6.h>
#pragma comment(lib, "dxgi.lib")

#ifndef GWLP_EXSTYLE
#if defined(GWL_EXSTYLE)
#define GWLP_EXSTYLE GWL_EXSTYLE
#else
#define GWLP_EXSTYLE (-20)
#endif
#endif

#include "expectional_overlay_window.hpp"
#include "expectional_ananbaban_overlay.hpp"
#include "../../Includes/Imgui/imgui.h"
#include "../../Includes/Imgui/imgui_internal.h"
#include "../../Includes/Imgui/imgui_impl_win32.h"
#include "../../Includes/Imgui/imgui_impl_dx11.h"
#include "../game/globals.hpp"
#include "../Game/expectional_overlay_zorder.hpp"
#include "../Game/expectional_render_scheduler.hpp"
#include "../Overlay/shade_menu_config_stub.hpp"

#include <cstdio>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern HWND GameWnd;

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace {

HWND g_game_hwnd = nullptr;
bool g_should_quit = false;

struct HudPanelRect {
	ImVec2 pos{};
	ImVec2 size{};
	bool valid = false;
};

HudPanelRect g_radar_panel_rect;
HudPanelRect g_spec_panel_rect;

static void CacheHudPanelRects() noexcept
{
	g_radar_panel_rect.valid = false;
	g_spec_panel_rect.valid = false;
	if (Settings::misc::radarWindow) {
		if (ImGuiWindow* w = ImGui::FindWindowByName("Radar window##expectional_winrad")) {
			if (w->Active) {
				g_radar_panel_rect.pos = w->Pos;
				g_radar_panel_rect.size = w->Size;
				g_radar_panel_rect.valid = true;
			}
		}
	}
	if (Settings::misc::spectatorList) {
		if (ImGuiWindow* w = ImGui::FindWindowByName("Spectators")) {
			if (w->Active) {
				g_spec_panel_rect.pos = w->Pos;
				g_spec_panel_rect.size = w->Size;
				g_spec_panel_rect.valid = true;
			}
		}
	}
}

static bool ImGuiPointInHudPanel(const ImVec2& p, const HudPanelRect& r) noexcept
{
	if (!r.valid || r.size.x < 1.f || r.size.y < 1.f)
		return false;
	return p.x >= r.pos.x && p.x < r.pos.x + r.size.x && p.y >= r.pos.y && p.y < r.pos.y + r.size.y;
}

static bool HudPanelUnderMouse() noexcept
{
	const ImVec2 mp = ImGui::GetIO().MousePos;
	return ImGuiPointInHudPanel(mp, g_radar_panel_rect) || ImGuiPointInHudPanel(mp, g_spec_panel_rect);
}

static bool ScreenPointHitsHudPanel(HWND hwnd, LPARAM lparam) noexcept
{
	POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
	if (!ScreenToClient(hwnd, &pt))
		return false;
	const ImVec2 mp(static_cast<float>(pt.x), static_cast<float>(pt.y));
	return ImGuiPointInHudPanel(mp, g_radar_panel_rect) || ImGuiPointInHudPanel(mp, g_spec_panel_rect);
}

static HWND RefreshGameWndFromPid() noexcept
{
	HWND found = nullptr;
	if (processid)
		found = ExpectionalOverlayZOrder::GetProcessWnd(static_cast<DWORD>(processid));
	if (found && IsWindow(found))
		GameWnd = found;
	return GameWnd;
}

ExpectionalOverlayZOrder::Ipc g_overlay_ipc{};
bool g_overlay_ipc_ready = false;

static void EnsureOverlayIpc() noexcept
{
	if (g_overlay_ipc_ready)
		return;
	g_overlay_ipc = ExpectionalOverlayZOrder::OpenIpc(true);
	g_overlay_ipc_ready = g_overlay_ipc.shared != nullptr;
}

static void ShutdownOverlayIpc() noexcept
{
	if (!g_overlay_ipc_ready)
		return;
	if (g_overlay_ipc.shared) {
		g_overlay_ipc.shared->active = FALSE;
		g_overlay_ipc.shared->overlay_hwnd = nullptr;
		InterlockedIncrement(&g_overlay_ipc.shared->seq);
	}
	ExpectionalOverlayZOrder::CloseIpc(g_overlay_ipc);
	g_overlay_ipc_ready = false;
}

static void LogOverlayCreateFail(const char* step) noexcept
{
	fprintf(stderr, "> overlay Create fail @ %s (GetLastError=%lu)\n", step,
	    static_cast<unsigned long>(GetLastError()));
}

static void CleanupPartialCreate(HINSTANCE hInst) noexcept
{
	using namespace ExpectionalOverlayWindow;
	if (m_pRenderTargetView) {
		m_pRenderTargetView->Release();
		m_pRenderTargetView = nullptr;
	}
	if (m_pSwapChain) {
		m_pSwapChain->Release();
		m_pSwapChain = nullptr;
	}
	if (m_pContext) {
		m_pContext->Release();
		m_pContext = nullptr;
	}
	if (m_pDevice) {
		m_pDevice->Release();
		m_pDevice = nullptr;
	}
	if (m_hWnd) {
		DestroyWindow(m_hWnd);
		m_hWnd = nullptr;
	}
	if (m_windowClass.lpszClassName)
		UnregisterClassW(m_windowClass.lpszClassName, hInst);
}

/** Present(DO_NOT_WAIT) donguyu serbest birakir; monitor Hz ile sinirla. */
static void ApplyCaptureExclude(HWND hwnd, bool exclude) noexcept
{
	if (!hwnd || !IsWindow(hwnd))
		return;
	(void)SetWindowDisplayAffinity(hwnd, exclude ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
}

static void LimitOverlayFrameRate(unsigned target_hz) noexcept
{
	if (!target_hz)
		target_hz = 60;

	static LARGE_INTEGER freq{};
	static LARGE_INTEGER last{};
	static bool init = false;
	if (!init) {
		QueryPerformanceFrequency(&freq);
		QueryPerformanceCounter(&last);
		init = true;
		return;
	}

	const double target_sec = 1.0 / static_cast<double>(target_hz);
	LARGE_INTEGER now{};
	QueryPerformanceCounter(&now);
	const double elapsed =
	    static_cast<double>(now.QuadPart - last.QuadPart) / static_cast<double>(freq.QuadPart);
	if (elapsed < target_sec) {
		const DWORD sleep_ms = static_cast<DWORD>((target_sec - elapsed) * 1000.0);
		if (sleep_ms > 0 && sleep_ms < 50)
			Sleep(sleep_ms);
	}
	QueryPerformanceCounter(&last);
}

LRESULT CALLBACK WindowProcess(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam))
		return 0L;

	if (message == WM_DESTROY) {
		g_should_quit = true;
		PostQuitMessage(0);
		return 0L;
	}

	return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

namespace ExpectionalOverlayWindow {

void HandleWindowOrder(HWND /*game_hwnd*/)
{
	if (!m_hWnd || !IsWindow(m_hWnd))
		return;

	(void)RefreshGameWndFromPid();
	if (!GameWnd || !IsWindow(GameWnd))
		return;

	/** eskisurum main_loop: bounds + z-order (TOPMOST yok). */
	static RECT old_rc{};
	RECT rc{};
	POINT xy{};
	ZeroMemory(&rc, sizeof(rc));
	ZeroMemory(&xy, sizeof(xy));
	GetClientRect(GameWnd, &rc);
	ClientToScreen(GameWnd, &xy);

	const RECT screen_rc{ xy.x, xy.y, xy.x + rc.right, xy.y + rc.bottom };
	if (screen_rc.left != old_rc.left || screen_rc.right != old_rc.right ||
	    screen_rc.top != old_rc.top || screen_rc.bottom != old_rc.bottom) {
		old_rc = screen_rc;
		if (rc.right > 0 && rc.bottom > 0) {
			m_iWidth = rc.right;
			m_iHeight = rc.bottom;
			(void)SetWindowPos(m_hWnd, nullptr, xy.x, xy.y, rc.right, rc.bottom,
			    SWP_NOREDRAW | SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}

	const HWND hwnd_active = GetForegroundWindow();
	if (hwnd_active == GameWnd) {
		HWND insert = GetWindow(hwnd_active, GW_HWNDPREV);
		(void)SetWindowPos(m_hWnd, insert, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}

	EnsureOverlayIpc();

	static bool s_last_menu = false;
	const bool menu_open = Settings::bMenu;
	const bool menu_toggled = menu_open != s_last_menu;
	s_last_menu = menu_open;

	const HWND game_root = ExpectionalOverlayZOrder::ResolveGameRoot(GameWnd);
	ExpectionalOverlayZOrder::PublishIpc(
	    g_overlay_ipc, m_hWnd, GameWnd, game_root, menu_open, menu_toggled);
}

bool Create(HWND game_hwnd)
{
	if (m_bInitialized)
		return true;

	g_game_hwnd = game_hwnd;
	g_should_quit = false;

	m_iWidth = GetSystemMetrics(SM_CXSCREEN);
	m_iHeight = GetSystemMetrics(SM_CYSCREEN);

	const HINSTANCE hInst = GetModuleHandleW(nullptr);
	m_windowClass = {};
	m_windowClass.cbSize = sizeof(WNDCLASSEXW);
	m_windowClass.style = 0;
	m_windowClass.lpfnWndProc = WindowProcess;
	m_windowClass.hInstance = hInst;
	m_windowClass.lpszClassName = L"ExpectionalExternalBase";

	const ATOM atom = RegisterClassExW(&m_windowClass);
	if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
		LogOverlayCreateFail("RegisterClassExW");
		return false;
	}

	/** eskisurum: WS_POPUP + layered/transparent/toolwindow (TOPMOST yok). */
	constexpr DWORD kExStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW;
	constexpr DWORD kStyle = WS_POPUP | WS_VISIBLE;
	static const wchar_t kWndTitle[] = L"";

	m_hWnd = CreateWindowExW(kExStyle, m_windowClass.lpszClassName, kWndTitle, kStyle, 0, 0, m_iWidth,
	    m_iHeight, nullptr, nullptr, hInst, nullptr);

	if (!m_hWnd) {
		LogOverlayCreateFail("CreateWindowExW");
		if (m_windowClass.lpszClassName)
			UnregisterClassW(m_windowClass.lpszClassName, hInst);
		return false;
	}

	SetLayeredWindowAttributes(m_hWnd, RGB(0, 0, 0), 255, LWA_ALPHA);

	{
		const MARGINS margins{ -1, -1, -1, -1 };
		if (FAILED(DwmExtendFrameIntoClientArea(m_hWnd, &margins))) {
			LogOverlayCreateFail("DwmExtendFrameIntoClientArea");
			CleanupPartialCreate(hInst);
			return false;
		}
	}

	if (HDC hDC = GetDC(m_hWnd)) {
		m_uRefreshRate = static_cast<unsigned>(GetDeviceCaps(hDC, VREFRESH));
		ReleaseDC(m_hWnd, hDC);
	}
	if (!m_uRefreshRate)
		m_uRefreshRate = 60;

	/** Layered + DWM seffaf overlay: FLIP model siyah opak arka plan yapar — DISCARD + 1 buffer. */
	DXGI_SWAP_CHAIN_DESC swapChainDesc{};
	swapChainDesc.BufferDesc.RefreshRate.Numerator = m_uRefreshRate;
	swapChainDesc.BufferDesc.RefreshRate.Denominator = 1U;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1U;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 1U;
	swapChainDesc.OutputWindow = m_hWnd;
	swapChainDesc.Windowed = TRUE;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	constexpr D3D_FEATURE_LEVEL levels[2]{ D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
	D3D_FEATURE_LEVEL level{};

	/**
	 * BUYUK FPS KAZANC (laptop / hybrid GPU):
	 * Notepad Windows tarafindan "Power saving" GPU classification aliyor -> DXGI default
	 * adapter = integrated GPU (Intel UHD / AMD Vega). CS2 dGPU'da kosarken bizim overlay
	 * iGPU'da kalinca:
	 *   - cross-GPU DMA bandwidth bottleneck
	 *   - dGPU compositor surface'a iGPU'dan kopya = ek frame latency
	 *   - iGPU TDP zaten DUSUK -> overlay 30-40 FPS'te tikilir
	 * EXE'yi explorer/Steam launchada Windows otomatik "High performance" verir, dGPU secer.
	 *
	 * Cozum: IDXGIFactory6::EnumAdapterByGpuPreference(HIGH_PERFORMANCE) ile dGPU'yu
	 * elle bul, DRIVER_TYPE_UNKNOWN ile D3D11CreateDevice'a explicit adapter ver.
	 */
	IDXGIAdapter1* pHighPerfAdapter = nullptr;
	{
		IDXGIFactory6* factory6 = nullptr;
		if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory6), reinterpret_cast<void**>(&factory6))) && factory6) {
			(void)factory6->EnumAdapterByGpuPreference(
				0,
				DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
				__uuidof(IDXGIAdapter1),
				reinterpret_cast<void**>(&pHighPerfAdapter));
			factory6->Release();
		}
	}

	HRESULT hr = D3D11CreateDeviceAndSwapChain(
		pHighPerfAdapter,                                          /** dGPU varsa onu kullan */
		pHighPerfAdapter ? D3D_DRIVER_TYPE_UNKNOWN
		                 : D3D_DRIVER_TYPE_HARDWARE,
		nullptr, 0U, levels, 2U, D3D11_SDK_VERSION,
		&swapChainDesc, &m_pSwapChain, &m_pDevice, &level, &m_pContext);

	/** dGPU varsa olmadiysa default'a dus. */
	if (FAILED(hr) && pHighPerfAdapter) {
		pHighPerfAdapter->Release();
		pHighPerfAdapter = nullptr;
		hr = D3D11CreateDeviceAndSwapChain(
			nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0U, levels, 2U, D3D11_SDK_VERSION,
			&swapChainDesc, &m_pSwapChain, &m_pDevice, &level, &m_pContext);
	}

	if (pHighPerfAdapter) pHighPerfAdapter->Release();
	if (FAILED(hr)) {
		fprintf(stderr, "> overlay D3D11CreateDeviceAndSwapChain failed: 0x%08lX\n",
		    static_cast<unsigned long>(hr));
		CleanupPartialCreate(hInst);
		return false;
	}

	/** Not: SetMaximumFrameLatency(1) DISCARD swap-chain'de Present(DO_NOT_WAIT) ile race
	 *  yarattigi icin kaldirildi. DXGI varsayilan kuyrugu (3) kullaniyoruz. */

	ID3D11Texture2D* pBackBuffer = nullptr;
	m_pSwapChain->GetBuffer(0U, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer));
	if (!pBackBuffer) {
		LogOverlayCreateFail("SwapChain GetBuffer");
		CleanupPartialCreate(hInst);
		return false;
	}

	m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_pRenderTargetView);
	pBackBuffer->Release();

	ShowWindow(m_hWnd, SW_SHOWNOACTIVATE);
	UpdateWindow(m_hWnd);
	ApplyCaptureExclude(m_hWnd, Settings::misc::obsBypass);

	ImGui::CreateContext();
	ImGui_ImplWin32_Init(m_hWnd);
	ImGui_ImplDX11_Init(m_pDevice, m_pContext);

	HandleWindowOrder(g_game_hwnd);
	m_bInitialized = true;
	return true;
}

bool RenderFrame(const std::function<void()>& frame_callback)
{
	if (!m_bInitialized)
		return false;

	MSG message{};
	while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE)) {
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}

	if (message.message == WM_QUIT || g_should_quit)
		return false;

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	ExpectionalOverlayImGuiIniPollSave();

	static bool s_last_capture = false;
	const bool exclude_capture = Settings::misc::obsBypass;
	MenuConfig::BypassOBS = exclude_capture;
	if (exclude_capture != s_last_capture) {
		ApplyCaptureExclude(m_hWnd, exclude_capture);
		s_last_capture = exclude_capture;
	}

	if (frame_callback)
		frame_callback();

	ImGui::Render();

	constexpr float flColor[4] = { 0.f, 0.f, 0.f, 0.f };
	m_pContext->OMSetRenderTargets(1U, &m_pRenderTargetView, nullptr);
	m_pContext->ClearRenderTargetView(m_pRenderTargetView, flColor);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	m_pSwapChain->Present(0U, DXGI_PRESENT_DO_NOT_WAIT);

	/** eskisurum main_loop: bounds + z-order her kare. */
	HandleWindowOrder(g_game_hwnd);

	{
		const unsigned maxHz = ex_sched::OverlayTargetHz();
		if (maxHz > 0u)
			LimitOverlayFrameRate(maxHz);
	}

	return true;
}

void Destroy()
{
	if (!m_bInitialized)
		return;

	ExpectionalOverlayImGuiIniSaveOnShutdown();
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	if (m_pRenderTargetView) {
		m_pRenderTargetView->Release();
		m_pRenderTargetView = nullptr;
	}
	if (m_pSwapChain) {
		m_pSwapChain->Release();
		m_pSwapChain = nullptr;
	}
	if (m_pContext) {
		m_pContext->Release();
		m_pContext = nullptr;
	}
	if (m_pDevice) {
		m_pDevice->Release();
		m_pDevice = nullptr;
	}

	if (m_hWnd)
		DestroyWindow(m_hWnd);

	if (m_windowClass.lpszClassName)
		UnregisterClassW(m_windowClass.lpszClassName, m_windowClass.hInstance);

	m_hWnd = nullptr;
	m_bInitialized = false;
	g_game_hwnd = nullptr;
	ShutdownOverlayIpc();
}

unsigned GetTargetFrameHz() noexcept
{
	const unsigned saveCap = ex_sched::OverlayTargetHz();
	if (saveCap > 0u)
		return saveCap;
	return m_uRefreshRate ? m_uRefreshRate : 60u;
}

} // namespace ExpectionalOverlayWindow
