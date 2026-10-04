#pragma once

#include <Windows.h>
#include <functional>
#include <d3d11.h>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d3d11.lib")

namespace ExpectionalOverlayWindow {
inline bool m_bInitialized = false;
inline int m_iWidth = 1920;
inline int m_iHeight = 1080;
inline unsigned int m_uRefreshRate = 60;
inline bool m_use_flip_model = false;

inline ID3D11Device* m_pDevice = nullptr;
inline ID3D11DeviceContext* m_pContext = nullptr;
inline IDXGISwapChain* m_pSwapChain = nullptr;
inline ID3D11RenderTargetView* m_pRenderTargetView = nullptr;

inline HWND m_hWnd = nullptr;
inline WNDCLASSEXW m_windowClass{};

bool Create(HWND game_hwnd);
bool RenderFrame(const std::function<void()>& frame_callback);
void Destroy();

void HandleWindowOrder(HWND game_hwnd);

unsigned GetTargetFrameHz() noexcept;
} 
