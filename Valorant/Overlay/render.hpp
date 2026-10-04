#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d9.h>

#include <dwmapi.h>
#include <ShlObj.h>
#include <filesystem>
#include <cstring>
#include <utility>

#pragma comment(lib, "Shell32.lib")

#ifndef GWLP_EXSTYLE
#if defined(GWL_EXSTYLE)
#define GWLP_EXSTYLE GWL_EXSTYLE
#else
#define GWLP_EXSTYLE (-20)
#endif
#endif

#include "../../Includes/Imgui/imgui_internal.h"
#include "../../Includes/Imgui/imgui.h"
#include "../../Includes/Imgui/imgui_impl_win32.h"
#include "../../Includes/Imgui/imgui_impl_dx9.h"
#include "../game/globals.hpp"
#include "../Game/structs.hpp"
#include "../Game/faceit_rank_query.hpp"
#include "../Game/expectional_paths.hpp"
#include "../Game/expectional_winio.hpp"

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "Dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT CALLBACK ExpectionalOverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return 1;
	return ::DefWindowProcA(hWnd, msg, wParam, lParam);
}

inline IDirect3D9Ex* p_Object = NULL;
extern IDirect3DDevice9Ex* p_Device;
inline D3DPRESENT_PARAMETERS p_Params = { NULL };

inline HWND MyWnd = NULL;
inline HWND GameWnd = NULL;
inline MSG Message = { NULL };

inline RECT GameRect = { NULL };

inline DWORD ScreenCenterX = 0;
inline DWORD ScreenCenterY = 0;

inline ULONG Width = static_cast<ULONG>(GetSystemMetrics(SM_CXSCREEN));
inline ULONG Height = static_cast<ULONG>(GetSystemMetrics(SM_CYSCREEN));

WPARAM main_loop();
void render();
auto get_process_wnd(uint32_t pid) -> HWND;
inline auto init_wndparams(HWND hWnd) -> HRESULT
{
	if (!hWnd || !IsWindow(hWnd)) {
		printf("> D3D: invalid overlay HWND.\n");
		return E_INVALIDARG;
	}

	if (FAILED(Direct3DCreate9Ex(D3D_SDK_VERSION, &p_Object))) {
		printf("> D3D: Direct3DCreate9Ex failed.\n");
		return E_FAIL;
	}

	ZeroMemory(&p_Params, sizeof(p_Params));
	p_Params.Windowed = TRUE;
	p_Params.SwapEffect = D3DSWAPEFFECT_DISCARD;
	p_Params.hDeviceWindow = hWnd;
	p_Params.BackBufferCount = 1;
	p_Params.MultiSampleType = D3DMULTISAMPLE_NONE;
	p_Params.MultiSampleQuality = 0;
	p_Params.BackBufferFormat = D3DFMT_A8R8G8B8;
	p_Params.BackBufferWidth = Width;
	p_Params.BackBufferHeight = Height;
	p_Params.EnableAutoDepthStencil = TRUE;
	p_Params.AutoDepthStencilFormat = D3DFMT_D16;
	p_Params.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

	IDirect3DDevice9Ex* devEx = nullptr;
	HRESULT hr = p_Object->CreateDeviceEx(
	    D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &p_Params, nullptr, &devEx);
	if (FAILED(hr) || !devEx) {
		if (devEx) {
			devEx->Release();
			devEx = nullptr;
		}
		hr = p_Object->CreateDeviceEx(
		    D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &p_Params, nullptr, &devEx);
	}
	if (FAILED(hr) || !devEx) {
		printf("> D3D: CreateDeviceEx failed (HRESULT=0x%08lX).\n", static_cast<unsigned long>(hr));
		if (p_Object) {
			p_Object->Release();
			p_Object = nullptr;
		}
		return FAILED(hr) ? hr : E_FAIL;
	}
	p_Device = devEx;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	{
		const std::wstring ini_path = ExpectionalPaths::GlobalImGuiIniPathWide();
		static std::string s_ini_utf8;
		if (!ini_path.empty()) {
			s_ini_utf8.clear();
			for (wchar_t c : ini_path) {
				if (c <= 0x7f)
					s_ini_utf8.push_back(static_cast<char>(c));
			}
			if (!s_ini_utf8.empty())
				io.IniFilename = s_ini_utf8.c_str();
			else
				io.IniFilename = nullptr;
		} else {
			io.IniFilename = nullptr;
		}
	}
	io.ImeWindowHandle = hWnd;

	ImGui_ImplWin32_Init(hWnd);
	ImGui_ImplDX9_Init(p_Device);
	return S_OK;
}

inline auto cleanup_d3d() -> void
{
	if (p_Device != NULL) {
		p_Device->Release();
		p_Device = NULL;
	}
	if (p_Object != NULL) {
		p_Object->Release();
		p_Object = NULL;
	}
}

inline auto setup_window() -> void
{
	WNDCLASSEXA wcex{};
	wcex.cbSize = sizeof(WNDCLASSEXA);
	wcex.lpfnWndProc = ExpectionalOverlayWndProc;
	wcex.hInstance = GetModuleHandleA(nullptr);
	wcex.hIcon = LoadIconA(nullptr, MAKEINTRESOURCEA(IDI_APPLICATION));
	wcex.hCursor = LoadCursorA(nullptr, MAKEINTRESOURCEA(IDC_ARROW));
	wcex.lpszClassName = "Overlay";

	GetWindowRect(GetDesktopWindow(), &Rect);
	const int deskW = (Rect.right - Rect.left) > 0 ? (Rect.right - Rect.left) : 1280;
	const int deskH = (Rect.bottom - Rect.top) > 0 ? (Rect.bottom - Rect.top) : 720;
	Width = static_cast<ULONG>(deskW);
	Height = static_cast<ULONG>(deskH);

	RegisterClassExA(&wcex);

	MyWnd = CreateWindowExA(0, "Overlay", "Overlay", WS_POPUP, Rect.left, Rect.top, deskW, deskH, nullptr, nullptr,
	    wcex.hInstance, nullptr);
	if (MyWnd == NULL) {
		printf("> CreateWindowExA failed (%lu).\n", static_cast<unsigned long>(GetLastError()));
		return;
	}
	SetWindowLongA(MyWnd, GWL_EXSTYLE, WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW);
	SetLayeredWindowAttributes(MyWnd, RGB(0, 0, 0), 255, LWA_ALPHA);

	const MARGINS margin{ -1, -1, -1, -1 };
	DwmExtendFrameIntoClientArea(MyWnd, &margin);

	ShowWindow(MyWnd, SW_SHOW);
	UpdateWindow(MyWnd);
}

inline bool ExpectionalForegroundIsCs2Family() noexcept
{
	if (!GameWnd || !IsWindow(GameWnd))
		return false;
	HWND fg = ::GetForegroundWindow();
	if (!fg)
		return false;
	if (fg == GameWnd)
		return true;
	if (::IsChild(GameWnd, fg))
		return true;
	DWORD pidGame = 0, pidFg = 0;
	::GetWindowThreadProcessId(GameWnd, &pidGame);
	::GetWindowThreadProcessId(fg, &pidFg);
	return pidGame != 0 && pidGame == pidFg;
}

inline void ExpectionalSyncScreenCenterFromImGui() noexcept
{
	const ImGuiIO& io = ImGui::GetIO();
	float sw = io.DisplaySize.x;
	float sh = io.DisplaySize.y;
	if (sw < 1.f || sh < 1.f) {
		sw = static_cast<float>(GetSystemMetrics(SM_CXSCREEN));
		sh = static_cast<float>(GetSystemMetrics(SM_CYSCREEN));
	}
	Width = static_cast<ULONG>(sw + 0.5f);
	Height = static_cast<ULONG>(sh + 0.5f);
	ScreenCenterX = static_cast<DWORD>(sw * 0.5f + 0.5f);
	ScreenCenterY = static_cast<DWORD>(sh * 0.5f + 0.5f);
}

auto get_process_wnd(uint32_t pid) -> HWND
{
	if (pid == 0)
		return NULL;

	std::pair<HWND, uint32_t> params = { 0, pid };
	BOOL bResult = EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
		auto pParams = (std::pair<HWND, uint32_t>*)(lParam);
		uint32_t processId = 0;

		if (GetWindowThreadProcessId(hwnd, reinterpret_cast<LPDWORD>(&processId)) && processId == pParams->second) {
			SetLastError((uint32_t)-1);
			pParams->first = hwnd;
			return FALSE;
		}

		return TRUE;

		}, (LPARAM)&params);

	if (!bResult && GetLastError() == -1 && params.first)
		return params.first;

	return NULL;
}

using namespace ColorStructs;

void DrawFilledRect(int x, int y, int w, int h, RGBA* color)
{
	ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), ImGui::ColorConvertFloat4ToU32(ImVec4(color->R / 255.0, color->G / 255.0, color->B / 255.0, color->A / 255.0)), 0, 0);
}

void DrawFilledRect2(int x, int y, int w, int h, ImColor color)
{
	ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), color, 0, 0);
}

void DrawNormalBox(int x, int y, int w, int h, int borderPx, RGBA* color)
{
	DrawFilledRect(x + borderPx, y, w, borderPx, color); 
	DrawFilledRect(x + w - w + borderPx, y, w, borderPx, color); 
	DrawFilledRect(x, y, borderPx, h, color); 
	DrawFilledRect(x, y + h - h + borderPx * 2, borderPx, h, color); 
	DrawFilledRect(x + borderPx, y + h + borderPx, w, borderPx, color); 
	DrawFilledRect(x + w - w + borderPx, y + h + borderPx, w, borderPx, color); 
	DrawFilledRect(x + w + borderPx, y, borderPx, h, color);
	DrawFilledRect(x + w + borderPx, y + h - h + borderPx * 2, borderPx, h, color);
}
using namespace UE4Structs;

auto Draw2DBox(Vector3 RootPosition, float Width, float Height, RGBA* Color) -> void
{
	DrawNormalBox(RootPosition.x - Width / 2, RootPosition.y - Height / 2, Width, Height, Settings::Visuals::BoxWidth, Color);
}

void DrawRect(int x, int y, int w, int h, RGBA* color, int thickness)
{
	ImGui::GetBackgroundDrawList()->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), ImGui::ColorConvertFloat4ToU32(ImVec4(color->R / 255.0, color->G / 255.0, color->B / 255.0, color->A / 255.0)), 0, 0, thickness);
}

auto DrawDistance(Vector3 Location, float Distance) -> void
{
	char dist[64];
	sprintf_s(dist, "%.fm", Distance);

	ImVec2 TextSize = ImGui::CalcTextSize(dist);
	ImGui::GetBackgroundDrawList()->AddText(ImVec2(Location.x - TextSize.x / 2, Location.y - TextSize.y / 2), ImGui::GetColorU32({ 255, 255, 255, 255 }), dist);
}

auto DrawTracers(Vector3 Target, ImColor Color) -> void
{
	ImGui::GetBackgroundDrawList()->AddLine(
		ImVec2(ScreenCenterX, Height),
		ImVec2(Target.x, Target.y),
		Color,
		0.1f
	);
}

auto DrawHealthBar(Vector3 RootPosition, float Width, float Height, float Health, float RelativeDistance) -> void
{
	auto HPBoxWidth = 1 / RelativeDistance;

	auto HPBox_X = RootPosition.x - Width / 2 - 5 - HPBoxWidth;
	auto HPBox_Y = RootPosition.y - Height / 2 + (Height - Height * (Health / 100));

	int HPBoxHeight = Height * (Health / 100);

	DrawFilledRect(HPBox_X, HPBox_Y, HPBoxWidth, HPBoxHeight, &ColorStructs::Col.green);
	DrawRect(HPBox_X - 1, HPBox_Y - 1, HPBoxWidth + 2, HPBoxHeight + 2, &ColorStructs::Col.black, 1);
}

void DrawRightProgressBar(int x, int y, int w, int h, int thick, int m_health)
{
	int G = (255 * m_health / 100);
	int R = 255 - G;
	RGBA healthcol = { R, G, 0, 255 };

	DrawFilledRect(x + (w / 2) - 25, y, thick, (h)*m_health / 100, &healthcol);
}
void DrawArmor(int x, int y, int w, int h, int thick, int armor)
{
	
	DrawFilledRect(x + (w / 2) - 25, y, thick, (h)*armor / 100, &Col.lightblue);
}
void DrawCrossNazi(int buyukluk, DWORD color)
{
	const int crosspozisyon = static_cast<int>(ScreenCenterX);
	const int crosspozisyony = static_cast<int>(ScreenCenterY);
	ImGui::GetBackgroundDrawList()->AddLine(ImVec2((float)(crosspozisyon), (float)(crosspozisyony - buyukluk)), ImVec2((float)crosspozisyon, (float)(crosspozisyony + buyukluk)), ImColor(color));
	ImGui::GetBackgroundDrawList()->AddLine(ImVec2((float)(crosspozisyon - buyukluk), (float)crosspozisyony), ImVec2((float)(crosspozisyon + buyukluk), (float)crosspozisyony), ImColor(color));
	ImGui::GetBackgroundDrawList()->AddLine(ImVec2((float)crosspozisyon, (float)(crosspozisyony + buyukluk)), ImVec2((float)(crosspozisyon - buyukluk), (float)(crosspozisyony + buyukluk)), ImColor(color));
	ImGui::GetBackgroundDrawList()->AddLine(ImVec2((float)crosspozisyon, (float)(crosspozisyony - buyukluk)), ImVec2((float)(crosspozisyon + buyukluk), (float)(crosspozisyony - buyukluk)), ImColor(color));
	ImGui::GetBackgroundDrawList()->AddLine(ImVec2((float)(crosspozisyon - buyukluk), (float)crosspozisyony), ImVec2((float)(crosspozisyon - buyukluk), (float)(crosspozisyony - buyukluk)), ImColor(color));
	ImGui::GetBackgroundDrawList()->AddLine(ImVec2((float)(crosspozisyon + buyukluk), (float)crosspozisyony), ImVec2((float)(crosspozisyon + buyukluk), (float)(crosspozisyony + buyukluk)), ImColor(color));
}

inline void ExpectionalRestoreForegroundToGame() noexcept
{
	if (!GameWnd || !IsWindow(GameWnd) || !MyWnd || !IsWindow(MyWnd))
		return;
	if (Settings::bMenu)
		return;
	if (::GetForegroundWindow() != MyWnd)
		return;

	DWORD game_tid = GetWindowThreadProcessId(GameWnd, nullptr);
	DWORD our_tid = GetCurrentThreadId();
	if (game_tid && our_tid != game_tid)
		(void)AttachThreadInput(our_tid, game_tid, TRUE);
	(void)SetFocus(GameWnd);
	if (game_tid && our_tid != game_tid)
		(void)AttachThreadInput(our_tid, game_tid, FALSE);
}

inline void ExpectionalClearImGuiInputForGameplay() noexcept
{
	if (Settings::bMenu)
		return;
	ImGuiIO& io = ImGui::GetIO();
	io.ClearInputKeys();
	io.ClearInputCharacters();
	ImGui::SetNextFrameWantCaptureKeyboard(false);

	if (MyWnd && ::GetForegroundWindow() == MyWnd)
		ExpectionalRestoreForegroundToGame();
}

inline void ExpectionalSyncOverlayForMenu(bool menuOpen)
{
	if (!MyWnd || !IsWindow(MyWnd))
		return;
	static bool s_wasOpen = false;
	LONG_PTR ex = ::GetWindowLongPtrW(MyWnd, GWLP_EXSTYLE);
	if (menuOpen) {
		if (ex & WS_EX_TRANSPARENT) {
			ex &= ~WS_EX_TRANSPARENT;
			::SetWindowLongPtrW(MyWnd, GWLP_EXSTYLE, ex);
			::SetWindowPos(MyWnd, nullptr, 0, 0, 0, 0,
			    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
		}
		if (!s_wasOpen && GameWnd && IsWindow(GameWnd)) {
			HWND fg = GetForegroundWindow();
			DWORD fgTid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
			DWORD ourTid = GetCurrentThreadId();
			BOOL att = FALSE;
			if (fgTid && ourTid != fgTid)
				att = AttachThreadInput(fgTid, ourTid, TRUE);
			BringWindowToTop(MyWnd);
			SetForegroundWindow(MyWnd);
			SetActiveWindow(MyWnd);
			if (att)
				AttachThreadInput(fgTid, ourTid, FALSE);
		}
	} else {
		if (!(ex & WS_EX_TRANSPARENT)) {
			ex |= WS_EX_TRANSPARENT;
			::SetWindowLongPtrW(MyWnd, GWLP_EXSTYLE, ex);
			::SetWindowPos(MyWnd, nullptr, 0, 0, 0, 0,
			    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
		}
	}
	s_wasOpen = menuOpen;
}

void DrawCornerBox(int x, int y, int w, int h, int borderPx, RGBA* color)
{
	DrawFilledRect(x + borderPx, y, w / 3, borderPx, color); 
	DrawFilledRect(x + w - w / 3 + borderPx, y, w / 3, borderPx, color); 
	DrawFilledRect(x, y, borderPx, h / 3, color); 
	DrawFilledRect(x, y + h - h / 3 + borderPx * 2, borderPx, h / 3, color); 
	DrawFilledRect(x + borderPx, y + h + borderPx, w / 3, borderPx, color); 
	DrawFilledRect(x + w - w / 3 + borderPx, y + h + borderPx, w / 3, borderPx, color); 
	DrawFilledRect(x + w + borderPx, y, borderPx, h / 3, color);
	DrawFilledRect(x + w + borderPx, y + h - h / 3 + borderPx * 2, borderPx, h / 3, color);
}