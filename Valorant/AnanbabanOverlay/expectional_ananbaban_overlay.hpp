#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <functional>

extern ID3D11Device* g_ExpectionalMainDX11Device;

bool ExpectionalAnanbabanOverlayRun(HWND gameHwnd, const std::function<void()>& frameCallback);

void ExpectionalOverlayImGuiIniPollSave();
void ExpectionalOverlayImGuiIniSaveOnShutdown();
