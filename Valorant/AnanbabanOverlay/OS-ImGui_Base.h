#pragma once
#include "OS-ImGui_Struct.h"
#include "OS-ImGui_Exception.h"
#include <string>
#include <functional>
#include <string>
#include <codecvt>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

namespace OSImGui
{
	class D3DDevice
	{
	public:
		ID3D11Device* g_pd3dDevice = nullptr;
		ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
		IDXGISwapChain* g_pSwapChain = nullptr;
		ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
		
		UINT m_swapChainFlags = 0u;
		
		UINT m_presentFlags = 0u;
		
		bool m_useFlipModel = false;
		HANDLE m_frameLatencyWaitable = nullptr;
		bool CreateDeviceD3D(HWND hWnd);
		void CleanupDeviceD3D();
		void CreateRenderTarget();
		void CleanupRenderTarget();
	};

	inline D3DDevice g_Device;

	enum WindowType { NEW, ATTACH };

	class WindowData
	{
	public:
		HWND hWnd = NULL;
		HINSTANCE hInstance = nullptr;
		std::string Name;
		std::wstring wName;
		std::string ClassName;
		std::wstring wClassName;
		ImVec2 Pos;
		ImVec2 Size;
		ImColor BgColor{ 255, 255, 255 };
	};

	class OSImGui_Base
	{
	public:
		std::function<void()> CallBackFn = nullptr;
		bool EndFlag = false;
		WindowData Window;
		WindowData DestWindow;
		virtual void NewWindow(std::string WindowName, ImVec2 WindowSize, std::function<void()> CallBack) = 0;
		virtual void Quit() { EndFlag = true; }
		virtual bool CreateMyWindow() = 0;
		virtual void MainLoop() {}
		bool InitImGui(ID3D11Device* device, ID3D11DeviceContext* device_context);
		void CleanImGui();
		std::wstring StringToWstring(std::string& str);
	};
}
