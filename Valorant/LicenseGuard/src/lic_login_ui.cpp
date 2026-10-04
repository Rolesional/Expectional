// =============================================================================
// LicenseGuard manual login UI — username + password (account auth)
// =============================================================================

#include "../include/lic_login_ui.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include <d3d9.h>

#include "../../../Includes/Imgui/imgui.h"
#include "../../../Includes/Imgui/imgui_impl_dx9.h"
#include "../../../Includes/Imgui/imgui_impl_win32.h"

#pragma comment(lib, "d3d9.lib")

#include "../include/lic_guard_vxlang.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg,
                                                              WPARAM wParam,
                                                              LPARAM lParam);

namespace lic {
namespace {

struct UiState {
  std::atomic<bool> auth_in_progress{false};
  std::atomic<bool> auth_done{false};
  bool auth_success = false;
  std::string auth_error;
  std::mutex result_mutex;

  std::atomic<bool> close_requested{false};
  bool user_cancelled = false;

  char input_user[64] = {0};
  char input_pass[128] = {0};
};

UiState* g_ui = nullptr;
HWND g_hwnd = nullptr;
LPDIRECT3D9 g_d3d = nullptr;
LPDIRECT3DDEVICE9 g_device = nullptr;
D3DPRESENT_PARAMETERS g_pp = {};

LRESULT WINAPI LicLoginWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;

  switch (msg) {
    case WM_SIZE:
      if (g_device != nullptr && wParam != SIZE_MINIMIZED) {
        g_pp.BackBufferWidth = LOWORD(lParam);
        g_pp.BackBufferHeight = HIWORD(lParam);
        ImGui_ImplDX9_InvalidateDeviceObjects();
        g_device->Reset(&g_pp);
        ImGui_ImplDX9_CreateDeviceObjects();
      }
      return 0;
    case WM_SYSCOMMAND:
      if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
      break;
    case WM_CLOSE:
      if (g_ui) {
        g_ui->user_cancelled = true;
        g_ui->close_requested.store(true);
      }
      return 0;
    case WM_DESTROY:
      ::PostQuitMessage(0);
      return 0;
  }
  return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool CreateDevice(HWND hWnd) {
  g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
  if (!g_d3d) return false;

  ZeroMemory(&g_pp, sizeof(g_pp));
  g_pp.Windowed = TRUE;
  g_pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  g_pp.BackBufferFormat = D3DFMT_UNKNOWN;
  g_pp.EnableAutoDepthStencil = TRUE;
  g_pp.AutoDepthStencilFormat = D3DFMT_D16;
  g_pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

  HRESULT hr = g_d3d->CreateDevice(
      D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
      D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_pp, &g_device);
  if (FAILED(hr) || !g_device) {
    hr = g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
                             D3DCREATE_SOFTWARE_VERTEXPROCESSING, &g_pp,
                             &g_device);
  }
  if (FAILED(hr) || !g_device) {
    g_d3d->Release();
    g_d3d = nullptr;
    return false;
  }
  return true;
}

void DestroyDevice() {
  if (g_device) {
    g_device->Release();
    g_device = nullptr;
  }
  if (g_d3d) {
    g_d3d->Release();
    g_d3d = nullptr;
  }
}

void ApplyExpectionalMenuStyle() {
  ImGuiStyle* style = &ImGui::GetStyle();
  ImVec4* Colors = style->Colors;
  Colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
  Colors[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
  Colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  Colors[ImGuiCol_PopupBg] = ImVec4(0.19f, 0.19f, 0.19f, 0.92f);
  Colors[ImGuiCol_Border] = ImVec4(0.19f, 0.19f, 0.19f, 0.59f);
  Colors[ImGuiCol_FrameBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
  Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
  Colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
  Colors[ImGuiCol_Button] = ImVec4(0.15f, 0.15f, 0.15f, 0.54f);
  Colors[ImGuiCol_ButtonHovered] = ImVec4(0.29f, 0.29f, 0.29f, 0.54f);
  Colors[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.32f, 0.33f, 1.00f);
  Colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.22f, 0.22f, 1.00f);
  style->WindowPadding = ImVec2(8.00f, 8.00f);
  style->FramePadding = ImVec2(5.00f, 2.00f);
  style->ItemSpacing = ImVec2(2.00f, 3.00f);
  style->WindowRounding = 0;
  style->FrameRounding = 0;
  style->WindowTitleAlign = ImVec2(0.5f, 0.5f);
}

std::string TrimAscii(const char* in) {
  std::string s(in ? in : "");
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' ||
                        s.back() == '\n'))
    s.pop_back();
  return s;
}

}  // namespace

LIC_VL_NOINLINE
bool ShowLoginDialog(const std::string& product_display_name,
                     LoginAttemptFn attempt) {
  LicGuardTuAnchor(0x4C4F4749u);
  LIC_VL_AUTH_VM_OPEN;
  bool lic_result = false;
  do {
  UiState ui;
  g_ui = &ui;

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_CLASSDC;
  wc.lpfnWndProc = LicLoginWndProc;
  wc.hInstance = ::GetModuleHandleW(nullptr);
  wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
  wc.lpszClassName = L"LicLoginWnd";
  ::RegisterClassExW(&wc);

  const int W = 400;
  const int H = 300;
  RECT desk = {};
  ::GetClientRect(::GetDesktopWindow(), &desk);
  int X = ((desk.right - desk.left) - W) / 2;
  int Y = ((desk.bottom - desk.top) - H) / 2;
  if (X < 0) X = 0;
  if (Y < 0) Y = 0;

  g_hwnd = ::CreateWindowExW(
      WS_EX_APPWINDOW, wc.lpszClassName, L"", WS_POPUP | WS_VISIBLE,
      X, Y, W, H, nullptr, nullptr, wc.hInstance, nullptr);
  if (!g_hwnd) {
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    g_ui = nullptr;
    break;
  }

  if (!CreateDevice(g_hwnd)) {
    ::DestroyWindow(g_hwnd);
    g_hwnd = nullptr;
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    g_ui = nullptr;
    break;
  }

  ::ShowWindow(g_hwnd, SW_SHOWDEFAULT);
  ::UpdateWindow(g_hwnd);
  ::SetForegroundWindow(g_hwnd);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  ApplyExpectionalMenuStyle();
  ImGui_ImplWin32_Init(g_hwnd);
  ImGui_ImplDX9_Init(g_device);

  std::thread worker;
  auto launch_attempt = [&](LoginCredentials creds) {
    if (worker.joinable()) worker.join();
    ui.auth_in_progress.store(true);
    ui.auth_done.store(false);
    worker = std::thread([&, creds]() {
      auto [ok, err] = attempt(creds);
      {
        std::lock_guard<std::mutex> lk(ui.result_mutex);
        ui.auth_success = ok;
        ui.auth_error = std::move(err);
      }
      ui.auth_done.store(true);
      ui.auth_in_progress.store(false);
    });
  };

  bool result = false;
  bool focus_user = true;
  const std::string win_title =
      product_display_name.empty() ? "Login" : product_display_name;

  MSG msg;
  while (!ui.close_requested.load()) {
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      ::TranslateMessage(&msg);
      ::DispatchMessageW(&msg);
      if (msg.message == WM_QUIT) {
        ui.user_cancelled = true;
        ui.close_requested.store(true);
      }
    }
    if (ui.close_requested.load()) break;

    if (ui.auth_done.load()) {
      ui.auth_done.store(false);
      bool ok;
      {
        std::lock_guard<std::mutex> lk(ui.result_mutex);
        ok = ui.auth_success;
      }
      if (ok) {
        result = true;
        ui.close_requested.store(true);
        break;
      }
      focus_user = true;
    }

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const float top_pad = 10.f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, top_pad), ImGuiCond_Always,
                            ImVec2(0.5f, 0.f));
    ImGui::SetNextWindowSize(ImVec2(360.f, 0.f), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.f, 10.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.f, 10.f));

    ImGui::Begin(win_title.c_str(), nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::TextWrapped(
        "Welcome To Expectional.");
    ImGui::Spacing();

    if (focus_user) {
      ImGui::SetKeyboardFocusHere();
      focus_user = false;
    }

    const bool busy = ui.auth_in_progress.load();
    ImGui::PushItemWidth(-1.f);

    ImGuiInputTextFlags user_flags = ImGuiInputTextFlags_CharsNoBlank;
    if (busy) user_flags |= ImGuiInputTextFlags_ReadOnly;
    ImGui::InputText("Username", ui.input_user, sizeof(ui.input_user), user_flags);

    ImGuiInputTextFlags pass_flags =
        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue;
    if (busy) pass_flags |= ImGuiInputTextFlags_ReadOnly;
    const bool pressed_enter =
        ImGui::InputText("Password", ui.input_pass, sizeof(ui.input_pass), pass_flags);

    ImGui::PopItemWidth();
    ImGui::Spacing();

    {
      const float btn_w = (ImGui::GetContentRegionAvail().x - 10.f) * 0.5f;
      const float btn_h = 34.f;
      ImGui::BeginDisabled(busy);
      const bool clicked_login = ImGui::Button("Login", ImVec2(btn_w, btn_h));
      ImGui::EndDisabled();
      ImGui::SameLine(0.f, 10.f);
      ImGui::BeginDisabled(busy);
      const bool clicked_cancel = ImGui::Button("Cancel", ImVec2(btn_w, btn_h));
      ImGui::EndDisabled();

      if ((clicked_login || pressed_enter) && !busy) {
        LoginCredentials creds;
        creds.username = TrimAscii(ui.input_user);
        creds.password = TrimAscii(ui.input_pass);
        if (!creds.username.empty() && creds.password.size() >= 8) {
          launch_attempt(std::move(creds));
        }
      }
      if (clicked_cancel) {
        ui.user_cancelled = true;
        ui.close_requested.store(true);
      }
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::EndFrame();

    const D3DCOLOR clearbg = D3DCOLOR_RGBA(38, 38, 38, 255);
    g_device->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    g_device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clearbg, 1.0f, 0);
    if (g_device->BeginScene() >= 0) {
      ImGui::Render();
      ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
      g_device->EndScene();
    }
    HRESULT pres = g_device->Present(nullptr, nullptr, nullptr, nullptr);
    if (pres == D3DERR_DEVICELOST &&
        g_device->TestCooperativeLevel() == D3DERR_DEVICENOTRESET) {
      ImGui_ImplDX9_InvalidateDeviceObjects();
      g_device->Reset(&g_pp);
      ImGui_ImplDX9_CreateDeviceObjects();
    }
  }

  if (worker.joinable()) worker.join();

  ImGui_ImplDX9_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  DestroyDevice();
  if (g_hwnd) {
    ::DestroyWindow(g_hwnd);
    g_hwnd = nullptr;
  }
  ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
  g_ui = nullptr;

  lic_result = result;
  if (ui.user_cancelled && !result) lic_result = false;
  } while (0);
  LIC_VL_AUTH_VM_CLOSE;
  return lic_result;
}

}  // namespace lic
