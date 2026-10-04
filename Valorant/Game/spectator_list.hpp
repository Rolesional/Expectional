#pragma once

#include "globals.hpp"
#include "../Driver/driver.hpp"
#include "../../Includes/Imgui/imgui.h"
#include "../OSImGui/os_imgui_menu.hpp"
#include "entity_handle.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace spectator_list {

inline std::atomic<bool> g_scan_ready{ false };

/** Sunucuya bagli (signOn >= 5); lobby/menu'de agir entity taramasi yapma. */
inline bool Spectator_NetworkAllowsHeavyScan() noexcept
{
	const uintptr_t eng = g_GameMem.engine_address();
	if (!eng || !offsets::dwNetworkGameClient)
		return false;
	const uintptr_t ngc = g_GameMem.readv<uintptr_t>(eng + static_cast<uintptr_t>(offsets::dwNetworkGameClient));
	if (!ngc || ngc < 0x10000ULL)
		return false;
	const int signOn = g_GameMem.readv<int>(ngc + 0x230);
	return signOn >= 5;
}

/** Local pawn + entity list hazir olmadan spectator taramasi baslamasin. */
inline bool Spectator_LocalPawnReadyForScan() noexcept
{
	if (!client || !offsets::dwLocalPlayerPawn || !offsets::dwEntityList)
		return false;
	if (!Spectator_NetworkAllowsHeavyScan())
		return false;

	const uintptr_t local_pawn =
		g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwLocalPlayerPawn));
	if (!local_pawn || local_pawn < 0x10000ULL)
		return false;

	const uintptr_t entity_list =
		g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwEntityList));
	if (!entity_list || entity_list < 0x10000ULL)
		return false;

	return true;
}

inline bool Spectator_ReadyForScan() noexcept
{
	return Spectator_LocalPawnReadyForScan();
}

/** Eski: m_hObserverTarget -> dogrudan izlenen pawn (ananbaban). */
inline uintptr_t ResolveObserverTargetPawn(uintptr_t pawn) {
	if (!pawn || !offsets::m_pObserverServices || !offsets::m_hObserverTarget)
		return 0;
	const uintptr_t obs = g_GameMem.readv<uintptr_t>(pawn + static_cast<uintptr_t>(offsets::m_pObserverServices));
	uint32_t h = 0;
	if (obs)
		h = g_GameMem.readv<uint32_t>(obs + static_cast<uintptr_t>(offsets::m_hObserverTarget));
	if (!h)
		h = g_GameMem.readv<uint32_t>(pawn + static_cast<uintptr_t>(offsets::m_pObserverServices + offsets::m_hObserverTarget));
	if (!h)
		return 0;
	return ex_entity::ResolveHandle(h);
}

inline uintptr_t ResolveObserverTargetPawnFlat(uintptr_t pawn) {
	return ResolveObserverTargetPawn(pawn);
}

/**
 * SDK onizleme: observer_target = m_hObserverTarget.get(); target_controller = observer_target->m_hController.get();
 * Izlenen CCSPlayerController adresi (entity list pointer).
 */
inline uintptr_t ResolveSpectatedController(uintptr_t spectator_pawn) {
	if (!spectator_pawn || !offsets::m_pObserverServices || !offsets::m_hObserverTarget)
		return 0;
	const uintptr_t obs = g_GameMem.readv<uintptr_t>(spectator_pawn + static_cast<uintptr_t>(offsets::m_pObserverServices));
	uint32_t hObs = 0;
	if (obs)
		hObs = g_GameMem.readv<uint32_t>(obs + static_cast<uintptr_t>(offsets::m_hObserverTarget));
	if (!hObs)
		hObs = g_GameMem.readv<uint32_t>(
			spectator_pawn + static_cast<uintptr_t>(offsets::m_pObserverServices + offsets::m_hObserverTarget));
	if (!hObs)
		return 0;
	const uintptr_t obs_proxy = ex_entity::ResolveHandle(hObs);
	if (!obs_proxy)
		return 0;
	if (!offsets::m_hController)
		return 0;
	const uint32_t hCtrl = g_GameMem.readv<uint32_t>(obs_proxy + static_cast<uintptr_t>(offsets::m_hController));
	if (!hCtrl)
		return 0;
	return ex_entity::ResolveHandle(hCtrl);
}

inline bool SpecBothInPlay(int hp) {
	return hp > 0 && hp <= 100;
}

/** ananbaban CEntity::IsAlive: m_bPawnIsAlive==1 && hp>0 (controller + pawn). */
inline bool DragonEntityAlive(uintptr_t controller, uintptr_t pawn) {
	if (!pawn)
		return false;
	const int hp = g_GameMem.readv<int>(pawn + offsets::m_iHealth);
	if (hp <= 0)
		return false;
	if (!offsets::m_bPawnIsAlive || !controller)
		return false;
	const int st = g_GameMem.readv<int>(controller + static_cast<uintptr_t>(offsets::m_bPawnIsAlive));
	return st == 1;
}

inline uintptr_t PawnFromControllerRow(uintptr_t entity_list, uintptr_t controller) {
	if (!entity_list || !controller || !offsets::dwPlayerPawn)
		return 0;
	const uint32_t playerpawn = g_GameMem.readv<uint32_t>(controller + static_cast<uintptr_t>(offsets::dwPlayerPawn));
	if (!playerpawn)
		return 0;
	const uintptr_t stride = static_cast<uintptr_t>(offsets::entity_controller_stride ? offsets::entity_controller_stride : 112u);
	const uintptr_t list_entry2 = g_GameMem.readv<uintptr_t>(entity_list + 0x8 * ((playerpawn & 0x7FFF) >> 9) + 16);
	if (!list_entry2)
		return 0;
	return g_GameMem.readv<uintptr_t>(list_entry2 + stride * (playerpawn & 0x1FF));
}

/** CCSPlayerController::m_hObserverPawn — olum/spectate aktif pawn (C_CSObserverPawn). */
inline uintptr_t ObserverPawnFromControllerRow(uintptr_t entity_list, uintptr_t controller) {
	if (!entity_list || !controller || !offsets::m_hObserverPawn)
		return 0;
	const uint32_t hObsPawn = g_GameMem.readv<uint32_t>(controller + static_cast<uintptr_t>(offsets::m_hObserverPawn));
	if (!hObsPawn)
		return 0;
	const uintptr_t stride = static_cast<uintptr_t>(offsets::entity_controller_stride ? offsets::entity_controller_stride : 112u);
	const uintptr_t list_entry2 = g_GameMem.readv<uintptr_t>(entity_list + 0x8 * ((hObsPawn & 0x7FFF) >> 9) + 16);
	if (!list_entry2)
		return 0;
	return g_GameMem.readv<uintptr_t>(list_entry2 + stride * (hObsPawn & 0x1FF));
}

/**
 * m_hPlayerPawn ceset / proxy kalir; gercek izleme `m_hObserverPawn` uzerinde.
 * Canliyken observer pawn genelde bos veya oyuncu pawn'i kullanilir.
 */
inline uintptr_t PawnForObserverChain(uintptr_t entity_list, uintptr_t controller, uintptr_t player_pawn) {
	if (!player_pawn)
		return 0;
	const uintptr_t obs_pawn = ObserverPawnFromControllerRow(entity_list, controller);
	if (!obs_pawn)
		return player_pawn;
	if (offsets::m_bPawnIsAlive && controller) {
		if (DragonEntityAlive(controller, player_pawn))
			return player_pawn;
		return obs_pawn;
	}
	if (obs_pawn != player_pawn)
		return obs_pawn;
	return player_pawn;
}

inline uintptr_t ControllerFromPawn(uintptr_t pawn) {
	if (!pawn || !offsets::m_hController)
		return 0;
	const uint32_t h = g_GameMem.readv<uint32_t>(pawn + static_cast<uintptr_t>(offsets::m_hController));
	if (!h)
		return 0;
	return ex_entity::ResolveHandle(h);
}

inline std::mutex g_mutex;
inline std::vector<std::string> g_names;

inline void cache_loop() {
	for (;;) {
		if (!Settings::misc::spectatorList) {
			g_scan_ready.store(false, std::memory_order_relaxed);
			{
				std::lock_guard<std::mutex> lk(g_mutex);
				g_names.clear();
			}
			Sleep(500);
			continue;
		}

		/** Menu acikken agir entity taramasi yapma — driver mutex + ImGui input cakismasi. */
		if (Settings::bMenu) {
			Sleep(450);
			continue;
		}

		std::set<std::string> uniq;

		if (!Spectator_ReadyForScan()) {
			g_scan_ready.store(false, std::memory_order_relaxed);
			std::lock_guard<std::mutex> lk(g_mutex);
			g_names.clear();
			Sleep(400);
			continue;
		}

		g_scan_ready.store(true, std::memory_order_relaxed);

		uintptr_t local_pawn = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwLocalPlayerPawn));
		uintptr_t local_controller = 0;
		if (offsets::dwLocalPlayerController)
			local_controller = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwLocalPlayerController));
		if (!local_controller && local_pawn)
			local_controller = ControllerFromPawn(local_pawn);

		const uintptr_t entity_list = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwEntityList));

		const int local_hp = g_GameMem.readv<int>(local_pawn + offsets::m_iHealth);
		const uintptr_t local_row_pawn = local_controller ? PawnFromControllerRow(entity_list, local_controller) : 0;
		const uintptr_t local_dragon_pawn = local_row_pawn ? local_row_pawn : local_pawn;
		const bool local_alive_dragon = offsets::m_bPawnIsAlive
			? DragonEntityAlive(local_controller, local_dragon_pawn)
			: SpecBothInPlay(local_hp);

		uintptr_t local_chain_pawn = local_pawn;
		if (local_row_pawn)
			local_chain_pawn = PawnForObserverChain(entity_list, local_controller, local_row_pawn);

		uintptr_t local_spectated_ctrl = 0;
		if (!local_alive_dragon)
			local_spectated_ctrl = ResolveSpectatedController(local_chain_pawn);

		uintptr_t spectated_pawn_legacy = 0;
		if (!local_alive_dragon)
			spectated_pawn_legacy = ResolveObserverTargetPawnFlat(local_chain_pawn);

		int i_max = 64;
		if (offsets::dwGameEntitySystem_highestEntityIndex) {
			const int hi = g_GameMem.readv<int>(entity_list + static_cast<uintptr_t>(offsets::dwGameEntitySystem_highestEntityIndex));
			if (hi >= 64 && hi < 8192)
				i_max = (std::min)(hi + 8, 96);
		}

		const uintptr_t kEntStride = 112u;

		for (int i = 1; i < i_max; ++i) {
			const uintptr_t list_entry = g_GameMem.readv<uintptr_t>(entity_list + 8ull * (static_cast<uintptr_t>(i & 0x7FFF) >> 9) + 16);
			if (!list_entry)
				continue;

			const uintptr_t player = g_GameMem.readv<uintptr_t>(list_entry + kEntStride * (i & 0x1FF));
			if (!player)
				continue;

			if (local_controller && player == local_controller)
				continue;

			const uintptr_t pawn_addr = PawnFromControllerRow(entity_list, player);
			if (!pawn_addr)
				continue;
			if (!local_controller && pawn_addr == local_pawn)
				continue;

			const int ent_hp = g_GameMem.readv<int>(pawn_addr + offsets::m_iHealth);
			bool skip_both_alive = false;
			if (offsets::m_bPawnIsAlive) {
				skip_both_alive = DragonEntityAlive(local_controller, local_pawn) && DragonEntityAlive(player, pawn_addr);
			}
			if (skip_both_alive) {
				continue;
			}

			const uintptr_t chain_pawn = PawnForObserverChain(entity_list, player, pawn_addr);

			bool watching_us = false;

			const uintptr_t tgt_ctrl = ResolveSpectatedController(chain_pawn);
			if (tgt_ctrl) {
				if (local_controller && tgt_ctrl == local_controller)
					watching_us = true;
				if (!watching_us && local_spectated_ctrl && tgt_ctrl == local_spectated_ctrl)
					watching_us = true;
			}

			if (!watching_us) {
				const uintptr_t leg = ResolveObserverTargetPawnFlat(chain_pawn);
				if (leg == local_pawn || (spectated_pawn_legacy && leg == spectated_pawn_legacy))
					watching_us = true;
			}

			if (!watching_us)
				continue;

			std::string raw;
			if (offsets::dwSanitizedName) {
				raw = g_GameMem.ReadString(player + static_cast<uintptr_t>(offsets::dwSanitizedName), 64);
				const size_t z = raw.find('\0');
				if (z != std::string::npos)
					raw.resize(z);
				while (!raw.empty() && (unsigned char)raw.back() <= ' ')
					raw.pop_back();
			}
			if (raw.empty())
				raw = "Unknown";
			uniq.insert(std::move(raw));
		}

		{
			std::vector<std::string> next(uniq.begin(), uniq.end());
			std::lock_guard<std::mutex> lk(g_mutex);
			g_names = std::move(next);
		}
		const DWORD sleep_ms = Settings::misc::save_fps
		    ? (uniq.empty() ? 700u : 480u)
		    : (uniq.empty() ? 520u : 320u);
		Sleep(sleep_ms);
	}
}

inline void draw_window() {
	if (!Settings::misc::spectatorList)
		return;

	std::vector<std::string> copy;
	{
		std::lock_guard<std::mutex> lk(g_mutex);
		copy = g_names;
	}

	const bool menuOpen = Settings::bMenu;
	const bool hasSpecs = !copy.empty();
	if (!hasSpecs && !menuOpen)
		return;

	/** En dar: "Spectators" basligi kadar; en genis: en uzun satir (Empty / oyuncu adi). */
	const float title_w = ImGui::CalcTextSize("Spectators").x;
	float line_w = 0.f;
	if (menuOpen && copy.empty())
		line_w = ImGui::CalcTextSize("Empty").x;
	else {
		for (const auto& s : copy)
			line_w = (std::max)(line_w, ImGui::CalcTextSize(s.c_str()).x);
	}
	const float content_w = (std::max)(title_w, line_w);
	ImGui::SetNextWindowContentSize(ImVec2(content_w, 0.f));

	ImGui::SetNextWindowPos(ImVec2(48.f, 160.f), ImGuiCond_FirstUseEver);

	ImGuiWindowFlags fl = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus;

	ExpectionalOsMenu_HudStyleScope hudStyle;
	if (ImGui::Begin("Spectators", nullptr, fl)) {
		ExpectionalHudDragBar("##spec_dragbar", 20.f);
		if (menuOpen && copy.empty())
			ImGui::TextUnformatted("Empty");
		else {
			for (const auto& s : copy)
				ImGui::TextUnformatted(s.c_str());
		}
	}
	ImGui::End();
}

} // namespace spectator_list
