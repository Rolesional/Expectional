#pragma once

#include <chrono>
#include <cstdio>
#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

#include "../../Includes/Imgui/imgui.h"
#include "../OSImGui/os_imgui_menu.hpp"
#include "globals.hpp"
#include "expectional_misc_runtime.hpp"
#include "../Driver/driver.hpp"
#include "offsets_runtime.hpp"
#include "Sounds.h"

namespace ex_hit_feedback {

class HitMarker {
public:
	static constexpr float SIZE = 10.f;
	static constexpr float GAP = 3.f;

	HitMarker(float alpha, std::chrono::steady_clock::time_point startTime)
		: _alpha(alpha), _startTime(startTime) {}

	void DrawAt(float cx, float cy) const {
		if (_alpha <= 0.f)
			return;
		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		const int ai = static_cast<int>(_alpha);
		const ImU32 col = IM_COL32(255, 255, 255, ai);
		const int outlineA = ai < 220 ? ai : 220;
		const ImU32 outline = IM_COL32(0, 0, 0, outlineA);

		auto L = [&](float x1, float y1, float x2, float y2, float th, ImU32 c) {
			dl->AddLine(ImVec2(cx + x1, cy + y1), ImVec2(cx + x2, cy + y2), c, th);
		};

		L(-SIZE, -SIZE, -GAP, -GAP, 2.4f, outline);
		L(-SIZE, SIZE, -GAP, GAP, 2.4f, outline);
		L(SIZE, -SIZE, GAP, -GAP, 2.4f, outline);
		L(SIZE, SIZE, GAP, GAP, 2.4f, outline);
		L(-SIZE, -SIZE, -GAP, -GAP, 1.4f, col);
		L(-SIZE, SIZE, -GAP, GAP, 1.4f, col);
		L(SIZE, -SIZE, GAP, -GAP, 1.4f, col);
		L(SIZE, SIZE, GAP, GAP, 1.4f, col);
	}

	void Update() {
		if (_alpha <= 0.f)
			return;
		const auto now = std::chrono::steady_clock::now();
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - _startTime).count();
		if (ms >= 500)
			_alpha = 0.f;
		else
			_alpha = 255.f * (1.f - static_cast<float>(ms) / 500.f);

		const ImGuiIO& io = ImGui::GetIO();
		DrawAt(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
	}

private:
	float _alpha;
	std::chrono::steady_clock::time_point _startTime;
};

inline void PlayHitSound(int kind) noexcept {
	switch (kind) {
	case 1:
		PlaySoundA(reinterpret_cast<char*>(neverlose_sound), nullptr, SND_ASYNC | SND_MEMORY);
		break;
	case 2:
		PlaySoundA(reinterpret_cast<char*>(skeet_sound), nullptr, SND_ASYNC | SND_MEMORY);
		break;
	default:
		break;
	}
}

inline void Tick(uintptr_t local_pawn, uintptr_t local_controller) noexcept {
	(void)local_controller;
	static HitMarker marker(0.f, std::chrono::steady_clock::now());
	static bool have_prev = false;
	static int prev_total = 0;

	if ((!Settings::misc::hit_sound && !Settings::misc::hit_marker) || Settings::bMenu) {
		marker.Update();
		return;
	}
	if (!local_pawn || !offsets::m_pBulletServices || !offsets::m_totalHitsOnServer) {
		marker.Update();
		return;
	}

	if (!ex_misc::PollMs(ex_misc::FastPollMs(), ex_misc::PollSlot::HitFeedback)) {
		marker.Update();
		return;
	}

	const int team = g_GameMem.readv<int>(local_pawn + static_cast<uintptr_t>(offsets::m_iTeamNum));
	if (team == 0) {
		marker.Update();
		return;
	}
	const int hp = g_GameMem.readv<int>(local_pawn + static_cast<uintptr_t>(offsets::m_iHealth));
	if (hp <= 0) {
		marker.Update();
		return;
	}

	const uintptr_t bs = g_GameMem.readv<uintptr_t>(local_pawn + static_cast<uintptr_t>(offsets::m_pBulletServices));
	if (!bs) {
		marker.Update();
		return;
	}

	const int total = g_GameMem.readv<int>(bs + static_cast<uintptr_t>(offsets::m_totalHitsOnServer));

	if (!have_prev) {
		have_prev = true;
		prev_total = total;
	} else if (total != prev_total) {
		if (!(total == 0 && prev_total != 0)) {
			if (Settings::misc::hit_sound)
				PlayHitSound(Settings::misc::hit_sound);
			if (Settings::misc::hit_marker)
				marker = HitMarker(255.f, std::chrono::steady_clock::now());
		}
		prev_total = total;
	}

	marker.Update();
}

/** CGlobalVars: realtime + framecount — yaklasik oyun client FPS. */
inline int SampleGameFpsSmoothed() noexcept
{
	static float smooth = 0.f;
	if (!client || !offsets::dwGlobalVars)
		return (smooth >= 8.f) ? static_cast<int>(smooth + 0.5f) : 0;
	const uintptr_t gv = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwGlobalVars));
	if (!gv)
		return (smooth >= 8.f) ? static_cast<int>(smooth + 0.5f) : 0;
	const float rt = g_GameMem.readv<float>(gv + 0x0);
	const int fc = g_GameMem.readv<int>(gv + 0x4);
	static float prev_rt = -1.f;
	static int prev_fc = -1;
	if (prev_rt < 0.f) {
		prev_rt = rt;
		prev_fc = fc;
		return 0;
	}
	const float drt = rt - prev_rt;
	const int dfc = fc - prev_fc;
	prev_rt = rt;
	prev_fc = fc;
	if (drt > 1e-5f && dfc > 0) {
		const float inst = static_cast<float>(dfc) / drt;
		if (inst > 8.f && inst < 512.f)
			smooth = smooth * 0.88f + inst * 0.12f;
	}
	return (smooth >= 8.f) ? static_cast<int>(smooth + 0.5f) : 0;
}

inline void WatermarkAppendStat(char* out, size_t cap, bool& first, const char* label, int value) noexcept
{
	if (!out || cap == 0 || !label)
		return;
	char chunk[48]{};
	if (value > 0)
		snprintf(chunk, sizeof chunk, "%s %d", label, value);
	else
		snprintf(chunk, sizeof chunk, "%s --", label);
	if (!first) {
		const size_t len = strlen(out);
		if (len + 3 < cap)
			snprintf(out + len, cap - len, " | ");
	}
	const size_t len = strlen(out);
	if (len < cap)
		snprintf(out + len, cap - len, "%s", chunk);
	first = false;
}

inline void DrawWatermarkWindow() noexcept {
	if (!Settings::misc::water)
		return;

	struct WaterCache {
		bool allow = false;
		int ping = 0;
		int game_fps = 0;
	};
	static WaterCache s_cache{};

	const bool menuOpen = Settings::bMenu;
	if (ex_misc::PollMs(ex_misc::MediumPollMs(), ex_misc::PollSlot::Watermark) || menuOpen) {
		bool allow = menuOpen;
		const uintptr_t local_pawn = global_pawn;
		if (local_pawn && offsets::m_iTeamNum) {
			const int team = g_GameMem.readv<int>(local_pawn + static_cast<uintptr_t>(offsets::m_iTeamNum));
			if (team != 0)
				allow = true;
		}
		s_cache.allow = allow;
		if (!allow) {
			s_cache.ping = 0;
			return;
		}
		uintptr_t local_ctrl = 0;
		if (client && offsets::dwLocalPlayerController)
			local_ctrl = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwLocalPlayerController));
		s_cache.ping = 0;
		if (local_ctrl && offsets::m_iPing)
			s_cache.ping = g_GameMem.readv<int>(local_ctrl + static_cast<uintptr_t>(offsets::m_iPing));
		s_cache.game_fps = SampleGameFpsSmoothed();
	}
	if (!s_cache.allow)
		return;

	ImGuiIO& io = ImGui::GetIO();
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 18.f, 14.f), ImGuiCond_FirstUseEver, ImVec2(1.f, 0.f));

	const ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar;

	ExpectionalOsMenu_HudStyleScope hudStyle;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 5.f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 2.f));
	if (!ImGui::Begin("##ExpectionalWatermark", nullptr, fl)) {
		ImGui::PopStyleVar(2);
		ImGui::End();
		return;
	}

	const bool showOverlayFps = Settings::misc::waterShowOverlayFps;
	const bool showGameFps = Settings::misc::waterShowGameFps;
	const bool showPing = Settings::misc::waterShowPing;
	const bool bothFps = showOverlayFps && showGameFps;

	char line[256]{};
	strncpy_s(line, "expectional.dev", _TRUNCATE);
	bool first = false;
	if (showOverlayFps || showGameFps || showPing) {
		const int overlayFps = (io.Framerate >= 1.f) ? static_cast<int>(io.Framerate + 0.5f) : 0;
		if (showOverlayFps)
			WatermarkAppendStat(line, sizeof line, first, bothFps ? "O FPS:" : "FPS:", overlayFps);
		if (showGameFps)
			WatermarkAppendStat(line, sizeof line, first, bothFps ? "G FPS:" : "FPS:", s_cache.game_fps);
		if (showPing)
			WatermarkAppendStat(line, sizeof line, first, "Ping:", s_cache.ping);
	}
	ImGui::TextUnformatted(line);

	ImGui::PopStyleVar(2);
	ImGui::End();
}

} // namespace ex_hit_feedback
