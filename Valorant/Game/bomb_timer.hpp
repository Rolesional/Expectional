#pragma once

#include "globals.hpp"
#include "expectional_misc_runtime.hpp"
#include "../Driver/driver.hpp"
#include "offsets_runtime.hpp"
#include "../../Includes/Imgui/imgui.h"
#include "../OSImGui/os_imgui_menu.hpp"

#include <chrono>
#include <cmath>
#include <sstream>
#include <string>

namespace bomb_timer {

struct BombUiState {
	bool valid = false;
	bool is_planted = false;
	bool show_window = false;
	uintptr_t bomb = 0;
	float remaining = 0.f;
	int site = 0;
	bool defusing = false;
	float defuse_rem = -1.f;
};

inline void RefreshBombUiState(BombUiState& out) {
	out = {};
	if (!Settings::misc::bombTimer || !client || !offsets::dwPlantedC4)
		return;

	const uintptr_t plantedAddr = client + static_cast<uintptr_t>(offsets::dwPlantedC4);
	out.is_planted = g_GameMem.readv<bool>(plantedAddr - 8);
	out.show_window = Settings::bMenu || out.is_planted;
	if (!out.show_window)
		return;

	out.valid = true;
	if (!out.is_planted)
		return;

	out.bomb = g_GameMem.readv<uintptr_t>(plantedAddr);
	if (out.bomb && offsets::c4_m_bBombDefused) {
		if (g_GameMem.readv<bool>(out.bomb + static_cast<uintptr_t>(offsets::c4_m_bBombDefused)))
			out.bomb = 0;
	}
	if (!out.bomb)
		return;

	if (offsets::c4_m_nBombSite)
		out.site = g_GameMem.readv<int>(out.bomb + static_cast<uintptr_t>(offsets::c4_m_nBombSite));
	if (offsets::c4_m_bBeingDefused)
		out.defusing = g_GameMem.readv<bool>(out.bomb + static_cast<uintptr_t>(offsets::c4_m_bBeingDefused));

	if (offsets::c4_m_flC4Blow && offsets::dwGlobalVars) {
		const uintptr_t gv = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwGlobalVars));
		if (gv) {
			const float cur = g_GameMem.readv<float>(gv + 0x30);
			const float blow = g_GameMem.readv<float>(out.bomb + static_cast<uintptr_t>(offsets::c4_m_flC4Blow));
			if (blow > 0.001f && cur >= blow - 0.05f)
				out.remaining = 0.f;
			else if (blow > cur && blow - cur < 120.f)
				out.remaining = blow - cur;
		}
	}
	if (out.remaining < 0.f)
		out.remaining = 0.f;
	if (out.remaining > 99.f)
		out.remaining = 99.f;

	if (out.defusing && offsets::c4_m_flDefuseCountDown && offsets::dwGlobalVars) {
		const uintptr_t gv = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwGlobalVars));
		if (gv) {
			const float cur = g_GameMem.readv<float>(gv + 0x30);
			const float defEnd = g_GameMem.readv<float>(out.bomb + static_cast<uintptr_t>(offsets::c4_m_flDefuseCountDown));
			out.defuse_rem = defEnd - cur;
		}
	}
}

inline int64_t NowMs() {
	using namespace std::chrono;
	return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

/**
 * Toggle: Settings::misc::bombTimer
 * Gorunurluk: (menu acik) VEYA (bomba kurulu). Menu acik + kurulu degil -> "C4 not planted".
 * Menu kapali + kurulu degil -> pencere yok.
 */
inline void draw_window() {
	static bool s_wasPlanted = false;
	static int64_t s_plantWallMs = 0;
	static BombUiState s_ui{};

	if (!Settings::misc::bombTimer || !client || !offsets::dwPlantedC4) {
		s_wasPlanted = false;
		s_plantWallMs = 0;
		s_ui = {};
		return;
	}

	/** Kurulu bomba: save_fps poll atlanir — sayac her kare guncellenir. */
	bool plantedNow = false;
	if (offsets::dwPlantedC4)
		plantedNow = g_GameMem.readv<bool>(client + static_cast<uintptr_t>(offsets::dwPlantedC4) - 8);
	if (Settings::bMenu || plantedNow || ex_misc::PollMs(ex_misc::FastPollMs(), ex_misc::PollSlot::BombTimer))
		RefreshBombUiState(s_ui);

	if (!s_ui.valid || !s_ui.show_window)
		return;

	const bool isBombPlanted = s_ui.is_planted;
	const uintptr_t bomb = s_ui.bomb;
	const int64_t tms = NowMs();

	if (isBombPlanted && !s_wasPlanted)
		s_plantWallMs = tms;
	s_wasPlanted = isBombPlanted;
	if (!isBombPlanted)
		s_plantWallMs = 0;

	if (!isBombPlanted || !bomb) {
		if (!Settings::bMenu)
			return;
	}

	ImGuiIO& io = ImGui::GetIO();
	const float winW = 300.f;
	ImGui::SetNextWindowSizeConstraints(ImVec2(winW, 68.f), ImVec2(winW, 500.f));
	ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - winW) * 0.5f, 28.f), ImGuiCond_FirstUseEver);

	const ImGuiWindowFlags fl = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize;
	ExpectionalOsMenu_HudStyleScope hudStyle;
	if (!ImGui::Begin("Bomb Timer", nullptr, fl)) {
		ImGui::End();
		return;
	}

	const float barW = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 2.f;

	if (!isBombPlanted) {
		const char* msg = "C4 not planted";
		const float tw = ImGui::CalcTextSize(msg).x;
		const float centerX = (ImGui::GetWindowWidth() - tw) * 0.5f;
		ImGui::SetCursorPosX(centerX > 0.f ? centerX : 0.f);
		ImGui::TextUnformatted(msg);
		ImGui::ProgressBar(0.f, ImVec2(barW, 14.f), "");
		ImGui::End();
		return;
	}

	if (!bomb) {
		const char* msg = "Bomb data unavailable";
		const float tw = ImGui::CalcTextSize(msg).x;
		const float centerX = (ImGui::GetWindowWidth() - tw) * 0.5f;
		ImGui::SetCursorPosX(centerX > 0.f ? centerX : 0.f);
		ImGui::TextUnformatted(msg);
		ImGui::ProgressBar(0.f, ImVec2(barW, 14.f), "");
		ImGui::End();
		return;
	}


	float remaining = s_ui.remaining;
	if (remaining <= 0.f && s_plantWallMs > 0)
		remaining = (40000.f - static_cast<float>(tms - s_plantWallMs)) / 1000.f;
	if (remaining < 0.f)
		remaining = 0.f;
	if (remaining > 99.f)
		remaining = 99.f;

	const int site = s_ui.site;
	const bool defusing = s_ui.defusing;

	std::ostringstream ss;
	ss.setf(std::ios::fixed);
	ss.precision(2);
	ss << "Bomb site " << (site == 0 ? "A" : "B") << ": " << remaining << " s";
	if (defusing && s_ui.defuse_rem > 0.f && s_ui.defuse_rem < 12.f)
		ss << "  |  Defuse " << s_ui.defuse_rem << " s";
	const std::string line = std::move(ss).str();
	const float tw = ImGui::CalcTextSize(line.c_str()).x;
	const float centerX = (ImGui::GetWindowWidth() - tw) * 0.5f;
	ImGui::SetCursorPosX(centerX > 0.f ? centerX : 0.f);
	ImGui::TextUnformatted(line.c_str());

	const float frac = remaining <= 0.f ? 0.f : (remaining >= 40.f ? 1.f : (remaining / 40.f));
	ImGui::ProgressBar(frac, ImVec2(barW, 14.f), "");
	ImGui::End();
}

} // namespace bomb_timer
