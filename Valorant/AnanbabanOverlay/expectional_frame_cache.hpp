#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include "../../Includes/Imgui/imgui.h"
#include "../game/globals.hpp"

struct ExpectionalFrameCache {
	static inline ImDrawList* bg = nullptr;
	static inline ImDrawList* fg = nullptr;
	static inline float sw = 1920.f;
	static inline float sh = 1080.f;
	static inline float cx = 960.f;
	static inline float cy = 540.f;
	static inline DWORD center_x = 960;
	static inline DWORD center_y = 540;
	static inline bool menu = false;

	static void Begin() noexcept
	{
		const ImGuiIO& io = ImGui::GetIO();
		menu = Settings::bMenu;
		bg = ImGui::GetBackgroundDrawList();
		fg = ImGui::GetForegroundDrawList();

		sw = io.DisplaySize.x;
		sh = io.DisplaySize.y;
		if (sw < 1.f || sh < 1.f) {
			sw = static_cast<float>(GetSystemMetrics(SM_CXSCREEN));
			sh = static_cast<float>(GetSystemMetrics(SM_CYSCREEN));
		}
		cx = sw * 0.5f;
		cy = sh * 0.5f;
		center_x = static_cast<DWORD>(cx);
		center_y = static_cast<DWORD>(cy);
	}

	static void SyncScreenGlobals() noexcept;
};

inline ImDrawList* ExBgDrawList() noexcept
{
	return ExpectionalFrameCache::bg ? ExpectionalFrameCache::bg : ImGui::GetBackgroundDrawList();
}

inline ImDrawList* ExFgDrawList() noexcept
{
	return ExpectionalFrameCache::fg ? ExpectionalFrameCache::fg : ImGui::GetForegroundDrawList();
}
