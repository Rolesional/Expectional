#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <functional>

/** ananbaban OS-ImGui D3D11 ana cihazı (harita PNG SRV, vb.). */
extern ID3D11Device* g_ExpectionalMainDX11Device;

/**
 * CS2-External-Base Window.cpp overlay: CreateWindowInBand + D3D11 + ImGui_ImplDX11 (TOPMOST yok).
 * gameHwnd: cs2 penceresi; frameCallback içinde mevcut render()/drawmenu akışı çalışır.
 */
bool ExpectionalAnanbabanOverlayRun(HWND gameHwnd, const std::function<void()>& frameCallback);

void ExpectionalOverlayImGuiIniPollSave();
void ExpectionalOverlayImGuiIniSaveOnShutdown();
