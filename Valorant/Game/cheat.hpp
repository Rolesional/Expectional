#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "..\Overlay\render.hpp" 
#include "entity_handle.hpp"
#include "hit_feedback.hpp"
#include "..\Overlay\menu.hpp"
#include "weapon_names.hpp"
#include "Features/Aimbot.h"
#include "Features/TriggerBot.h"
#include "Features/RCS.hpp"
#include "weapon_runtime.hpp"
#include "esp_layout.hpp"
#include "spotted_visibility.hpp"
#include "grenade_lineup.hpp"
#include "catalyst_world_bvh.hpp"
#include "ex_autowall.hpp"
#include "../Protection/vxlang_scope.hpp"
#include "tri_loader.hpp"

using namespace ColorStructs;

namespace {

inline void ExpectionalAnanbabanVBar(int x, int y, int w, int h, float proportion, ImU32 fillCol) {
	const float p = std::clamp(proportion, 0.f, 1.f);
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	const float x0 = static_cast<float>(x);
	const float y0 = static_cast<float>(y);
	const float x1 = static_cast<float>(x + w);
	const float y1 = static_cast<float>(y + h);
	const ImU32 bg = IM_COL32(0, 0, 0, 220);
	dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), bg, 0.f);
	const int fh = static_cast<int>(static_cast<float>(h) * p + 0.5f);
	if (fh > 0)
		dl->AddRectFilled(ImVec2(x0, y1 - static_cast<float>(fh)), ImVec2(x1, y1), fillCol, 0.f);
	dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(45, 45, 45, 220), 0.f, 0, 1.f);
}
}

class Cham
{
public:
	uint8_t red_;
	uint8_t green_;
	uint8_t blue_;
	uint8_t alpha_;

	Cham() = default;
	constexpr Cham(uint8_t new_red, uint8_t new_green, uint8_t new_blue, uint8_t new_alpha) : red_(new_red), green_(new_green), blue_(new_blue), alpha_(new_alpha)
	{
		
	}
};

namespace cs2_bones {
	enum : int {
		PELVIS = 1, SPINE1 = 3, SPINE2 = 4, NECK = 6, HEAD = 7,
		SHOULDER_L = 9, ELBOW_L = 10, HAND_L = 11,
		SHOULDER_R = 13, ELBOW_R = 14, HAND_R = 15,
		HIP_L = 17, KNEE_L = 18, FOOT_HEEL_L = 19,
		HIP_R = 20, KNEE_R = 21, FOOT_HEEL_R = 22, CHEST = 23,
	};
	struct BoneLink { int a, b; };
	static constexpr BoneLink kSkeleton[] = {
		{ PELVIS, SPINE1 }, { SPINE1, SPINE2 }, { SPINE2, CHEST }, { CHEST, NECK }, { NECK, HEAD },
		{ NECK, SHOULDER_L }, { SHOULDER_L, ELBOW_L }, { ELBOW_L, HAND_L },
		{ NECK, SHOULDER_R }, { SHOULDER_R, ELBOW_R }, { ELBOW_R, HAND_R },
		{ PELVIS, HIP_L }, { HIP_L, KNEE_L }, { KNEE_L, FOOT_HEEL_L },
		{ PELVIS, HIP_R }, { HIP_R, KNEE_R }, { KNEE_R, FOOT_HEEL_R },
	};
	static constexpr size_t kSkeletonCount = sizeof(kSkeleton) / sizeof(kSkeleton[0]);
}

bool w2s(const Vector3& pos, Vector3& out, view_matrix_t matrix)
{
	out.x = matrix[0][0] * pos.x + matrix[0][1] * pos.y + matrix[0][2] * pos.z + matrix[0][3];
	out.y = matrix[1][0] * pos.x + matrix[1][1] * pos.y + matrix[1][2] * pos.z + matrix[1][3];

	float w = matrix[3][0] * pos.x + matrix[3][1] * pos.y + matrix[3][2] * pos.z + matrix[3][3];

	if (w < 0.01f)
		return false;

	float inv_w = 1.f / w;
	out.x *= inv_w;
	out.y *= inv_w;

	const ImGuiIO& io = ImGui::GetIO();
	float sw = io.DisplaySize.x;
	float sh = io.DisplaySize.y;
	if (sw < 1.f) sw = (float)GetSystemMetrics(SM_CXSCREEN);
	if (sh < 1.f) sh = (float)GetSystemMetrics(SM_CYSCREEN);

	float x = sw * 0.5f;
	float y = sh * 0.5f;

	x += 0.5f * out.x * sw + 0.5f;
	y -= 0.5f * out.y * sh + 0.5f;

	out.x = x;
	out.y = y;
	out.z = w;

	return true;
}

namespace colors
{
	float chamscol[3] = { 1.0f , 1.0f , 1.0f };
	float espcol[3] = { 1.0f , 1.0f , 1.0f };
	float skelcol[3] = { 1.0f , 1.0f , 1.0f };
	float crosscol[3] = { 1.0f , 10.0f , 0.0f };

	float orecol[3] = { 1.0f , 1.0f , 1.0f };
	float collcol[3] = { 1.0f , 1.0f , 1.0f };
	float cratecol[3] = { 1.0f , 1.0f , 1.0f };
	float scientistcol[3] = { 1.0f , 1.0f , 1.0f };
	float itemscol[3] = { 1.0f , 1.0f , 1.0f };
}

#include "esp_extras.hpp"
#include "grenade_esp.hpp"
#include "catalyst_player_esp.hpp"
#include "expectional_esp_budget.hpp"
#include "expectional_reveal_workers.hpp"
#include "expectional_render_scheduler.hpp"
#include "rank_reveal_cache.hpp"

inline std::string ExpectionalReadWeaponName(uintptr_t pawn) {
	const uint16_t defIdx = ex_esp::ReadWeaponDefIndex(pawn);
	const char* nm = WeaponNameFromDefIndex(defIdx);
	return nm ? std::string(nm) : std::string("unknown");
}

inline CS2Entity ExpectionalBuildRankRevealEntry(uintptr_t player, uintptr_t pawn, int entity_i, std::uint32_t playerpawn_full)
{
	CS2Entity E{};
	E.Actor = pawn;
	E.Controller = player;
	E.pawn_handle_low = playerpawn_full & 0x7FFFu;
	E.entity_index = entity_i;
	if (offsets::m_iHealth)
		E.health = g_GameMem.readv<int>(pawn + static_cast<uintptr_t>(offsets::m_iHealth));
	if (offsets::m_ArmorValue)
		E.armor = g_GameMem.readv<int>(pawn + static_cast<uintptr_t>(offsets::m_ArmorValue));
	if (offsets::m_iTeamNum && pawn)
		E.team_num = g_GameMem.readv<int>(pawn + static_cast<uintptr_t>(offsets::m_iTeamNum)) & 0xFF;
	if (player && offsets::dwSanitizedName) {
		std::string raw = g_GameMem.ReadString(player + offsets::dwSanitizedName, 32);
		const size_t z = raw.find('\0');
		if (z != std::string::npos) raw.resize(z);
		while (!raw.empty() && (unsigned char)raw.back() <= ' ') raw.pop_back();
		E.name = std::move(raw);
	}
	if (player && offsets::m_steamID) {
		const std::uint64_t sid = g_GameMem.readv<std::uint64_t>(player + static_cast<uintptr_t>(offsets::m_steamID));
		if (sid > 17ull)
			E.steam_id64 = sid;
	}
	if (player) {
		if (offsets::m_iCompetitiveRanking)
			E.competitive_ranking = g_GameMem.readv<int>(player + static_cast<uintptr_t>(offsets::m_iCompetitiveRanking));
		if (offsets::m_iCompetitiveWins)
			E.competitive_wins = g_GameMem.readv<int>(player + static_cast<uintptr_t>(offsets::m_iCompetitiveWins));
		if (offsets::m_iCompetitiveRankType)
			E.competitive_rank_type = g_GameMem.readv<int>(player + static_cast<uintptr_t>(offsets::m_iCompetitiveRankType));
		if (offsets::m_iCompetitiveRankingPredicted_Win)
			E.rank_pred_win = g_GameMem.readv<int>(player + static_cast<uintptr_t>(offsets::m_iCompetitiveRankingPredicted_Win));
		if (offsets::m_iCompetitiveRankingPredicted_Loss)
			E.rank_pred_loss = g_GameMem.readv<int>(player + static_cast<uintptr_t>(offsets::m_iCompetitiveRankingPredicted_Loss));
		if (offsets::m_iCompetitiveRankingPredicted_Tie)
			E.rank_pred_tie = g_GameMem.readv<int>(player + static_cast<uintptr_t>(offsets::m_iCompetitiveRankingPredicted_Tie));
	}
	return E;
}

auto cacheGame() -> void
{
	
	while (true)
	{
		std::vector<CS2Entity> temp;
		const bool need_rank_cache = ExpectionalRankRevealWantsCacheRefresh();
		std::vector<CS2Entity> temp_rank;
		if (need_rank_cache)
			temp_rank.reserve(48);

		uintptr_t dwLocalPlayerPawn = g_GameMem.readv<uintptr_t>(client + offsets::dwLocalPlayerPawn);
		if (dwLocalPlayerPawn)
			global_pawn = dwLocalPlayerPawn;
		else
			dwLocalPlayerPawn = global_pawn;

		{
			int lrt = -999;
			if (client && offsets::dwLocalPlayerController && offsets::m_iCompetitiveRankType) {
				const uintptr_t lc = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwLocalPlayerController));
				if (lc) {
					const int8_t rt = g_GameMem.readv<int8_t>(lc + static_cast<uintptr_t>(offsets::m_iCompetitiveRankType));
					lrt = static_cast<int>(rt);
				}
			}
			g_cs2LocalCompetitiveRankType.store(lrt, std::memory_order_relaxed);
		}

		g_localControllerEntityIndex.store(-1, std::memory_order_relaxed);
		g_localPawnHandleIndex.store(-1, std::memory_order_relaxed);
		uintptr_t entity_list = g_GameMem.readv<uintptr_t>(client + offsets::dwEntityList);
		if (!entity_list || entity_list < 0x10000ULL)
			entity_list = g_GameMem.readv<uintptr_t>(client + offsets::dwEntityList);
		if (!entity_list || entity_list < 0x10000ULL) {
			Sleep(50);
			continue;
		}

		const uintptr_t kEntStride = static_cast<uintptr_t>(
		    offsets::entity_controller_stride ? offsets::entity_controller_stride : 112u);
		int scan_max = 64;
		if (offsets::dwGameEntitySystem_highestEntityIndex) {
			const int hi = g_GameMem.readv<int>(
			    entity_list + static_cast<uintptr_t>(offsets::dwGameEntitySystem_highestEntityIndex));
			if (hi >= 64 && hi < 8192)
				scan_max = (std::min)(hi + 1, 128);
		}
		for (int i = 1; i < scan_max; ++i) {
			const uintptr_t player = ex_entity::ResolveEntityFromListSlot(i);
			if (!player)
				continue;

			const std::uint32_t playerpawn = g_GameMem.readv<std::uint32_t>(player + offsets::dwPlayerPawn);
			uintptr_t pCSPlayerPawn = ex_entity::ResolveHandle(playerpawn);
			if (!pCSPlayerPawn) {
				const uintptr_t list_entry2 = g_GameMem.readv<uintptr_t>(
				    entity_list + 0x8 * ((playerpawn & 0x7FFF) >> 9) + 16);
				if (list_entry2)
					pCSPlayerPawn = g_GameMem.readv<uintptr_t>(list_entry2 + kEntStride * (playerpawn & 0x1FF));
			}
			if (!pCSPlayerPawn || pCSPlayerPawn < 0x10000ULL)
				continue;

			const int teamNum = g_GameMem.readv<int>(pCSPlayerPawn + offsets::m_iTeamNum);
			if (teamNum == 0)
				continue;

			if (pCSPlayerPawn == dwLocalPlayerPawn) {
				g_localControllerEntityIndex.store(i, std::memory_order_relaxed);
				g_localPawnHandleIndex.store(static_cast<int>(playerpawn & 0x7FFFu), std::memory_order_relaxed);
				if (need_rank_cache)
					temp_rank.push_back(ExpectionalBuildRankRevealEntry(player, pCSPlayerPawn, i, playerpawn));
				continue;
			}

			const int csPlayerHealth = g_GameMem.readv<int>(pCSPlayerPawn + offsets::m_iHealth);
			if (need_rank_cache)
				temp_rank.push_back(ExpectionalBuildRankRevealEntry(player, pCSPlayerPawn, i, playerpawn));

			if (csPlayerHealth <= 0 || csPlayerHealth > 100) continue;

			if (offsets::m_bPawnIsAlive) {
				const int alive = g_GameMem.readv<int>(player + static_cast<uintptr_t>(offsets::m_bPawnIsAlive));
				if (alive == 0) continue;
			}
			if (offsets::m_pGameSceneNode && offsets::m_bDormant) {
				const uintptr_t sn = g_GameMem.readv<uintptr_t>(pCSPlayerPawn + static_cast<uintptr_t>(offsets::m_pGameSceneNode));
				if (sn) {
					const uint8_t dorm = g_GameMem.readv<uint8_t>(sn + static_cast<uintptr_t>(offsets::m_bDormant));
					if (dorm) continue;
				}
			}
			Vector3 ori = ex_esp::ReadWorldPositionFromEntity(pCSPlayerPawn);
			if (ori.length2d() < 1.f && std::fabs(ori.z) < 1.f && offsets::m_vecOrigin)
				ori = g_GameMem.readv<Vector3>(pCSPlayerPawn + offsets::m_vecOrigin);
			if (ori.length2d() < 1.f && std::fabs(ori.z) < 1.f)
				continue;

			const int armorHealth = g_GameMem.readv<int>(pCSPlayerPawn + offsets::m_ArmorValue);

			CS2Entity Entities;
			Entities.Actor = pCSPlayerPawn;
			Entities.Controller = player;
			Entities.pawn_handle_low = playerpawn & 0x7FFFu;
			Entities.entity_index = i;
			Entities.health = csPlayerHealth;
			Entities.armor = armorHealth;
			Entities.world_x = ori.x;
			Entities.world_y = ori.y;
			Entities.world_z = ori.z;
			Entities.has_world_origin = true;
			Entities.team_num = teamNum & 0xFF;
			Entities.weapon_def_index = ex_esp::ReadWeaponDefIndex(pCSPlayerPawn);
			if (offsets::m_angEyeAngles)
				Entities.eye_yaw_deg =
				    g_GameMem.readv<float>(pCSPlayerPawn + static_cast<uintptr_t>(offsets::m_angEyeAngles) + 4);
			if (player && offsets::dwSanitizedName) {
				std::string raw = g_GameMem.ReadString(player + offsets::dwSanitizedName, 32);
				const size_t z = raw.find('\0');
				if (z != std::string::npos) raw.resize(z);
				while (!raw.empty() && (unsigned char)raw.back() <= ' ') raw.pop_back();
				Entities.name = std::move(raw);
			}
			if (Settings::Visuals::weaponEsp || Settings::Visuals::weaponEspIcon || Settings::Visuals::bombCarrierEsp)
				Entities.weapon = ExpectionalReadWeaponName(pCSPlayerPawn);
			Entities.has_c4 = ex_esp::PawnInventoryContainsWeaponDef(pCSPlayerPawn, 49);
			if (player && offsets::m_steamID) {
				const std::uint64_t sid = g_GameMem.readv<std::uint64_t>(player + static_cast<uintptr_t>(offsets::m_steamID));
				if (sid > 17ull)
					Entities.steam_id64 = sid;
			}
			if (need_rank_cache && player) {
				if (offsets::m_iCompetitiveRanking)
					Entities.competitive_ranking =
					    g_GameMem.readv<int>(player + static_cast<uintptr_t>(offsets::m_iCompetitiveRanking));
				if (offsets::m_iCompetitiveWins)
					Entities.competitive_wins =
					    g_GameMem.readv<int>(player + static_cast<uintptr_t>(offsets::m_iCompetitiveWins));
				if (offsets::m_iCompetitiveRankType)
					Entities.competitive_rank_type =
					    g_GameMem.readv<int>(player + static_cast<uintptr_t>(offsets::m_iCompetitiveRankType));
				if (offsets::m_iCompetitiveRankingPredicted_Win)
					Entities.rank_pred_win =
					    g_GameMem.readv<int>(player + static_cast<uintptr_t>(offsets::m_iCompetitiveRankingPredicted_Win));
				if (offsets::m_iCompetitiveRankingPredicted_Loss)
					Entities.rank_pred_loss =
					    g_GameMem.readv<int>(player + static_cast<uintptr_t>(offsets::m_iCompetitiveRankingPredicted_Loss));
				if (offsets::m_iCompetitiveRankingPredicted_Tie)
					Entities.rank_pred_tie =
					    g_GameMem.readv<int>(player + static_cast<uintptr_t>(offsets::m_iCompetitiveRankingPredicted_Tie));
			}
			temp.push_back(Entities);
		}

		if (Settings::Visuals::bombCarrierEsp) {
			for (auto& ent : temp)
				if (!ent.has_c4)
					ent.has_c4 = ex_esp::PawnInventoryContainsWeaponDef(ent.Actor, 49);
			ex_esp::ApplyCachedC4CarrierFlags(temp);
		}
		
		{
			const DWORD now_w = GetTickCount();
			const int combo = ex_esp::ConcurrentHeavyWorldKernelScanFeatures();
			static DWORD s_last_bomb_ms = 0;
			static DWORD s_last_drop_ms = 0;
			static DWORD s_last_nade_ms = 0;
			static unsigned s_drop_rebuild = 0;
			const int wq = std::clamp(Settings::misc::workerQuality, 0, 2);

			if (Settings::Visuals::bombWorldEsp || Settings::Visuals::bombCarrierEsp) {
				
				DWORD iv = (wq == 0) ? 70u : (wq == 2) ? 22u : 38u;
				if (combo >= 3) iv = iv * 6u / 5u;
				if (now_w - s_last_bomb_ms >= iv) {
					s_last_bomb_ms = now_w;
					ex_esp::TickBombWorldEspCache(dwLocalPlayerPawn);
				}
			}
			if (Settings::Visuals::droppedWeaponEsp) {
				
				DWORD iv = (wq == 0) ? 140u : (wq == 2) ? 55u : 90u;
				if (combo >= 3) iv = iv * 5u / 4u;
				if (now_w - s_last_drop_ms >= iv) {
					s_last_drop_ms = now_w;
					(void)s_drop_rebuild;
					ex_esp::TickDroppedWeaponEspCache(dwLocalPlayerPawn, true);
				}
			}
			if (ex_esp::WorldGrenadeEspActive()) {
				
				DWORD iv = (wq == 0) ? 50u : (wq == 2) ? 16u : 28u;
				if (combo >= 3) iv = iv * 5u / 4u;
				if (now_w - s_last_nade_ms >= iv) {
					s_last_nade_ms = now_w;
					ex_esp::TickWorldGrenadeEspDataCache();
				}
			}
		}

		{
			std::lock_guard<std::mutex> lk(g_PlayerListMutex);
			PlayerList = std::move(temp);
		}
		if (need_rank_cache)
			rank_reveal_cache::MergeScannedPlayers(temp_rank, ExpectionalRankRevealUiVisible());
		Sleep(16);
	}
}

inline void ExpectionalCs2BhopJump(bool wantDown, bool reedge) noexcept
{
	static bool s_down = false;
	if (!wantDown && !s_down)
		return;
	if (wantDown && s_down && !reedge)
		return;
	const WPARAM vk = VK_F24;
	HWND game = (GameWnd && IsWindow(GameWnd)) ? GameWnd : FindWindowA(nullptr, "Counter-Strike 2");
	if (!game || !IsWindow(game))
		return;
	const UINT sc = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
	LPARAM lpDn = static_cast<LPARAM>(1u | ((static_cast<UINT_PTR>(sc & 0xFFu)) << 16));
	const LPARAM lpUp = lpDn | (static_cast<LPARAM>(3u) << 30);
	(void)PostMessageW(game, WM_KEYUP, vk, lpUp);
	if (!wantDown) {
		s_down = false;
		return;
	}
	(void)PostMessageW(game, WM_KEYDOWN, vk, lpDn);
	s_down = true;
}

inline void ExpectionalBhopTick()
{
	if (!Settings::misc::bhop || !global_pawn || !offsets::m_fFlags || !offsets::m_iTeamNum) {
		ExpectionalCs2BhopJump(false, false);
		return;
	}
	if (Settings::bMenu) {
		ExpectionalCs2BhopJump(false, false);
		return;
	}
	const int team = g_GameMem.readv<int>(global_pawn + offsets::m_iTeamNum);
	if (team == 0) {
		ExpectionalCs2BhopJump(false, false);
		return;
	}

	const bool spacePressed = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
	const uint32_t fl = g_GameMem.readv<uint32_t>(global_pawn + static_cast<uintptr_t>(offsets::m_fFlags));
	const bool onGround = (fl & 1u) != 0;
	
	constexpr std::ptrdiff_t kMoveTypeOff = 0x525;
	const uint8_t moveType = g_GameMem.readv<uint8_t>(global_pawn + kMoveTypeOff);
	float velZ = 0.f;
	if (offsets::m_vecAbsVelocity)
		velZ = g_GameMem.readv<float>(global_pawn + static_cast<uintptr_t>(offsets::m_vecAbsVelocity) + 8);

	static bool s_prevOnGround = false;
	static bool s_prevSpaceHeld = false;
	static ULONGLONG s_airSince = 0;
	if (!spacePressed) {
		s_prevOnGround = onGround;
		s_prevSpaceHeld = false;
		if (!onGround)
			s_airSince = GetTickCount64();
		else
			s_airSince = 0;
		ExpectionalCs2BhopJump(false, false);
		return;
	}
	if (moveType == 9) {
		s_prevOnGround = onGround;
		s_prevSpaceHeld = true;
		ExpectionalCs2BhopJump(false, false);
		return;
	}

	const ULONGLONG now = GetTickCount64();
	if (!onGround && (s_prevOnGround || s_airSince == 0))
		s_airSince = now;

	const bool spaceEdge = !s_prevSpaceHeld;
	s_prevSpaceHeld = true;
	const bool landedEdge = onGround && !s_prevOnGround;
	s_prevOnGround = onGround;

	bool stairStep = false;
	if (landedEdge) {
		const ULONGLONG airMs = (s_airSince != 0) ? (now - s_airSince) : 0;
		if (airMs < 90)
			stairStep = true;
	}
	if (onGround && velZ > 28.f)
		stairStep = true;
	if (onGround)
		s_airSince = 0;

	const bool shouldPulse = !stairStep && (landedEdge || (spaceEdge && onGround));
	if (!shouldPulse) {
		if (stairStep)
			ExpectionalCs2BhopJump(false, false);
		return;
	}

	const int dms = Settings::misc::bhop_delay_ms;
	if (dms > 0) {
		static ULONGLONG s_lastMs = 0;
		const ULONGLONG now = GetTickCount64();
		if (now - s_lastMs < static_cast<ULONGLONG>(dms))
			return;
		s_lastMs = now;
	}

	ExpectionalCs2BhopJump(true, true);
}

inline bool ExpectionalShouldRunEspLoop() noexcept
{
	if (!client || !global_pawn)
		return false;
	const LegitCombatSettings combat = WeaponRuntime::Active();
	const ExpectionalEspBudget budget = ExpectionalEspBudget::Build(combat);
	if (budget.any_player_visual || budget.any_combat)
		return true;
	return false;
}

void ExpectionalGrenadeOverlayFrame(const UE4Structs::view_matrix_t* vm_pre = nullptr)
{
	if (!global_pawn || !client || !offsets::dwViewMatrix)
		return;
	if (!Settings::Visuals::grenadeLineups)
		return;

	static Vector3 s_cachedEye{};
	Vector3 localEyeWorld = s_cachedEye;
	if (Settings::Visuals::grenadeLineups) {
		
		localEyeWorld = Vector3{};
		if (offsets::m_pGameSceneNode) {
			const uintptr_t lgs =
			    g_GameMem.readv<uintptr_t>(global_pawn + static_cast<uintptr_t>(offsets::m_pGameSceneNode));
			if (lgs && offsets::m_boneArrayFromScene) {
				const uint64_t localBoneArray = g_GameMem.readv<uint64_t>(lgs + offsets::m_boneArrayFromScene);
				if (localBoneArray)
					localEyeWorld = g_GameMem.readv<Vector3>(
					    localBoneArray + static_cast<uintptr_t>(cs2_bones::HEAD * 0x20));
			}
		}
		if (localEyeWorld.IsZero() && offsets::m_vecOrigin) {
			const Vector3 o = ex_esp::ReadWorldPositionFromEntity(global_pawn);
			localEyeWorld = Vector3(o.x, o.y, o.z + 64.f);
		}
		s_cachedEye = localEyeWorld;
	} else if (!s_cachedEye.IsZero()) {
		localEyeWorld = s_cachedEye;
	}

	view_matrix_t vm_local{};
	const view_matrix_t& vm = vm_pre ? *vm_pre
	                                 : (vm_local = g_GameMem.readv<view_matrix_t>(client + offsets::dwViewMatrix), vm_local);
	if (ex_sched::RunGrenadeLineupsThisFrame())
		ExpectionalGrenadeLineupRender(vm, global_pawn, localEyeWorld);
}

inline void ExpectionalDrawWorldPickupOverlayFrame(const UE4Structs::view_matrix_t* vm_pre = nullptr)
{
	if (!client || !offsets::dwViewMatrix)
		return;
	if (!Settings::Visuals::bombWorldEsp && !Settings::Visuals::droppedWeaponEsp)
		return;
	view_matrix_t vm_local{};
	const view_matrix_t& vm = vm_pre ? *vm_pre
	                                 : (vm_local = g_GameMem.readv<view_matrix_t>(client + offsets::dwViewMatrix), vm_local);
	if (Settings::Visuals::bombWorldEsp)
		ex_esp::DrawBombWorldEsp(vm);
	if (Settings::Visuals::droppedWeaponEsp)
		ex_esp::DrawDroppedWeaponsWorldEsp(vm);
}

inline void ExpectionalDrawWorldGrenadeOverlayFrame(const UE4Structs::view_matrix_t* vm_pre = nullptr)
{
	if (!client || !offsets::dwViewMatrix)
		return;
	if (!Settings::Visuals::worldGrenades && !Settings::Visuals::worldInfernoHull)
		return;
	view_matrix_t vm_local{};
	const view_matrix_t& vm = vm_pre ? *vm_pre
	                                 : (vm_local = g_GameMem.readv<view_matrix_t>(client + offsets::dwViewMatrix), vm_local);
	ex_esp::DrawWorldGrenadeEsp(vm);
}

void espLoop(const UE4Structs::view_matrix_t* vm_pre = nullptr)
{
	EX_VL_PROTECT_BEGIN;
	{
		bool need_bvh = ExpectionalEspBudget::NeedWorldBvhMesh();
		if (global_pawn) {
			const LegitCombatSettings c0 = WeaponRuntime::Active();
			need_bvh = need_bvh || (c0.aimbot && c0.aim_visible_only) || c0.triggerbot || c0.penetration_crosshair || c0.aim_autowall || c0.trigger_autowall;
		}
		if (need_bvh)
			ex_world_bvh::EnsureWorldBvhLoadThread();
	}

	ImColor Red = { 250, 92, 255, 255 };
	auto ESPColor = ImColor(255, 255, 255);

	uintptr_t local_ctrl_wm = 0;
	if (client && offsets::dwLocalPlayerController)
		local_ctrl_wm = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwLocalPlayerController));
	ex_hit_feedback::Tick(global_pawn, local_ctrl_wm);

	if (global_pawn) {
		{
			static uint32_t s_weapon_refresh_fr = 0;
			if (!Settings::misc::save_fps || (s_weapon_refresh_fr++ % 3u) == 0u)
				WeaponRuntime::Refresh(global_pawn);
		}
		LegitCombatSettings combat = WeaponRuntime::Active();
		const ExpectionalEspBudget budget = ExpectionalEspBudget::Build(combat);

		const int ak = hotkeys::aimkey.load();
		const bool akDown = ak > 0 && ((GetAsyncKeyState(ak) & 0x8000) != 0);
		if (!Settings::bMenu && combat.aim_key_mode == 1 && akDown && !ExpectionalCombatUiState::aim_key_prev)
			ExpectionalCombatUiState::aim_toggle_arm = !ExpectionalCombatUiState::aim_toggle_arm;
		ExpectionalCombatUiState::aim_key_prev = akDown;

		bool aimKeyHeld = false;
		if (combat.aimbot) {
			const int amode = combat.aim_key_mode;
			if (amode == 0)
				aimKeyHeld = akDown;
			else if (amode == 1)
				aimKeyHeld = ExpectionalCombatUiState::aim_toggle_arm;
			else if (amode == 2)
				aimKeyHeld = true;
			else
				aimKeyHeld = akDown;
		}
		const bool lmb_coop = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
		int shots_coop = 0;
		if (global_pawn && offsets::m_iShotsFired)
			shots_coop = g_GameMem.readv<int>(global_pawn + static_cast<uintptr_t>(offsets::m_iShotsFired));
		float aimCx = static_cast<float>(ScreenCenterX);
		float aimCy = static_cast<float>(ScreenCenterY);

		const bool spray_rcs_coop = combat.rcs_enabled && !combat.rcs_standalone &&
			lmb_coop && shots_coop > combat.rcs_after_bullet;

		float dbPunchX = 0.f, dbPunchY = 0.f;
		const bool dbPunchOk = spray_rcs_coop && combat.aimbot && !combat.rcs_standalone && aimKeyHeld &&
			global_pawn && RCS::TryReadAimPunch(global_pawn, &dbPunchX, &dbPunchY);

		if (dbPunchOk) {
			const float dbScale = (combat.rcs_scale_pct / 100.f) * 1.4f;
			const float fovY = 90.f; 
			const float fovX = 90.f * (static_cast<float>(ScreenCenterX) / static_cast<float>(ScreenCenterY));
			const float pixelsPerDegreeY = (ScreenCenterY * 2.f) / fovY;
			const float pixelsPerDegreeX = (ScreenCenterX * 2.f) / fovX;

			aimCy += dbPunchX * dbScale * pixelsPerDegreeY;
			
			aimCx -= dbPunchY * dbScale * pixelsPerDegreeX;
		}

		if (combat.fov_circle) {
			const ImU32 fovRing = ImGui::ColorConvertFloat4ToU32(ImVec4(
				EspUiColors::aim_fov_circle[0], EspUiColors::aim_fov_circle[1], EspUiColors::aim_fov_circle[2], 1.f));
			ImGui::GetBackgroundDrawList()->AddCircle(
				ImVec2(aimCx, aimCy), combat.aim_fov, fovRing, budget.fov_circle_segments);
		}

		std::vector<CS2Entity> snapshot;
		{
			std::lock_guard<std::mutex> lk(g_PlayerListMutex);
			snapshot = PlayerList;
		}
		float bestAimFov = 1e30f;
		Vector3 bestAimScreen{};
		bool haveBestAim = false;
		uintptr_t bestAimPawn = 0;
		static uintptr_t s_stickyAimPawn = 0;
		static int s_stickyAimLost = 0;
		const bool sprayAimLock = spray_rcs_coop && aimKeyHeld && combat.aimbot && !combat.rcs_standalone;
		const float aimFovLimit = combat.aim_fov;

		float localFlashForHide = 0.f;
		if (global_pawn && Settings::Visuals::blindHideEsp && offsets::m_flFlashDuration)
			localFlashForHide = g_GameMem.readv<float>(global_pawn + static_cast<uintptr_t>(offsets::m_flFlashDuration));
		const bool blindHideAll = Settings::Visuals::blindHideEsp && localFlashForHide > 0.12f;

		Vector3 localOriginForDist{};
		if (global_pawn)
			localOriginForDist = ex_esp::ReadWorldPositionFromEntity(global_pawn);

		Vector3 localEyeWorld{};
		if (budget.need_local_eye && global_pawn && offsets::m_vecOrigin) {
			const uintptr_t lgs = offsets::m_pGameSceneNode
				? g_GameMem.readv<uintptr_t>(global_pawn + static_cast<uintptr_t>(offsets::m_pGameSceneNode))
				: 0;
			uint64_t localBoneArray = 0;
			if (lgs && offsets::m_boneArrayFromScene)
				localBoneArray = g_GameMem.readv<uint64_t>(lgs + offsets::m_boneArrayFromScene);
			if (localBoneArray)
				localEyeWorld = g_GameMem.readv<Vector3>(localBoneArray + static_cast<uintptr_t>(cs2_bones::HEAD * 0x20));
			else
				localEyeWorld = Vector3(localOriginForDist.x, localOriginForDist.y, localOriginForDist.z + 75.f);
		}

		view_matrix_t vmNow{};
		if (vm_pre)
			vmNow = *vm_pre;
		else if (client && offsets::dwViewMatrix)
			vmNow = g_GameMem.readv<view_matrix_t>(client + offsets::dwViewMatrix);
		view_matrix_t view_matrix = vmNow;
		const float playerPredictSec = 0.f;

		const bool bvhLosReadyFrame = ex_world_bvh::g_world_bvh.valid();
		ExpectionalBvhBudgetBeginFrame();
		ex_autowall::WeaponData aw_wd{};
		if (global_pawn && (combat.aim_autowall || combat.penetration_crosshair))
			aw_wd = ex_autowall::ReadWeaponData(global_pawn);
		
		const bool aimSpotReadsFrame =
		    combat.aim_visible_only && global_pawn && offsets::m_entitySpottedState != 0 && !bvhLosReadyFrame;
		const uint64_t aimLocalSpotMask = aimSpotReadsFrame ? ReadSpottedMask(global_pawn) : 0ull;
		const bool aimWantsBvhLos = budget.need_bvh_aim && combat.aimbot && bvhLosReadyFrame;
		bool visDbgFilled = false;
		if (budget.need_player_loop)
		for (const CS2Entity& CachePlayers : snapshot)
		{
			
			uintptr_t pawnAddr = CachePlayers.Actor;
			if (CachePlayers.Controller && offsets::dwPlayerPawn) {
				const std::uint32_t hPawn = g_GameMem.readv<std::uint32_t>(
				    CachePlayers.Controller + static_cast<uintptr_t>(offsets::dwPlayerPawn));
				const uintptr_t curPawn = ex_entity::ResolveHandle(hPawn);
				if (curPawn >= 0x10000ull)
					pawnAddr = curPawn;
				else
					continue;
			}

			const int liveHp = g_GameMem.readv<int>(pawnAddr + offsets::m_iHealth);
			if (liveHp <= 0 || liveHp > 100) continue;
			if (offsets::m_bPawnIsAlive && CachePlayers.Controller) {
				const int alive = g_GameMem.readv<int>(CachePlayers.Controller + static_cast<uintptr_t>(offsets::m_bPawnIsAlive));
				if (alive != 1) continue;
			}

			if (Settings::Visuals::enemiesOnly && global_pawn && offsets::m_iTeamNum) {
				const int lt = g_GameMem.readv<int>(global_pawn + offsets::m_iTeamNum) & 0xFF;
				const int pt = g_GameMem.readv<int>(pawnAddr + offsets::m_iTeamNum) & 0xFF;
				if (pt == lt)
					continue;
			}

			if (Settings::misc::debug_visible_check && global_pawn && !visDbgFilled) {
				ExpectionalFillVisDebug(global_pawn, pawnAddr, CachePlayers.pawn_handle_low,
					CachePlayers.entity_index,
					CachePlayers.name.empty() ? "?" : CachePlayers.name.c_str());
				visDbgFilled = true;
			}

			const uint64_t gamescene = g_GameMem.readv<uint64_t>(pawnAddr + offsets::m_pGameSceneNode);
			Vector3 origin{};
			if (gamescene >= 0x10000ull && offsets::m_vecAbsOrigin)
				origin = g_GameMem.readv<Vector3>(gamescene + static_cast<uintptr_t>(offsets::m_vecAbsOrigin));
			else if (offsets::m_vecOrigin)
				origin = g_GameMem.readv<Vector3>(pawnAddr + static_cast<uintptr_t>(offsets::m_vecOrigin));

			if (origin.length2d() < 4.f && std::fabs(origin.z) < 4.f)
				continue;

			Vector3 entVel{};
			if (playerPredictSec > 0.f && offsets::m_vecAbsVelocity) {
				entVel = g_GameMem.readv<Vector3>(pawnAddr + static_cast<uintptr_t>(offsets::m_vecAbsVelocity));
				const float vlen2 = entVel.x * entVel.x + entVel.y * entVel.y + entVel.z * entVel.z;
				
				if (vlen2 < 1024.f * 1024.f) {
					origin.x += entVel.x * playerPredictSec;
					origin.y += entVel.y * playerPredictSec;
					origin.z += entVel.z * playerPredictSec;
				} else {
					entVel = Vector3{};
				}
			}

			Vector3 screenpos;
			if (!w2s(origin, screenpos, view_matrix))
				continue;
			if (screenpos.z < 0.01f)
				continue;

			uint64_t bonearray = 0;
			if (gamescene >= 0x10000ull && offsets::m_boneArrayFromScene)
				bonearray = g_GameMem.readv<uint64_t>(gamescene + offsets::m_boneArrayFromScene);

			const bool boneBufOk = (bonearray >= 0x10000ull);
			Vector3 boneWs[24]{};
			uint32_t boneGot = 0;
			auto readBoneCached = [&](int idx) -> Vector3 {
				if (!boneBufOk || idx < 0 || idx >= 24)
					return {};
				const uint32_t bit = 1u << static_cast<unsigned>(idx);
				if (boneGot & bit)
					return boneWs[idx];
				boneWs[idx] = g_GameMem.readv<Vector3>(bonearray + static_cast<uintptr_t>(idx * 0x20));
				boneGot |= bit;
				return boneWs[idx];
			};

			auto applyPredict = [&](Vector3 v) -> Vector3 {
				if (playerPredictSec > 0.f && (entVel.x != 0.f || entVel.y != 0.f || entVel.z != 0.f)) {
					v.x += entVel.x * playerPredictSec;
					v.y += entVel.y * playerPredictSec;
					v.z += entVel.z * playerPredictSec;
				}
				return v;
			};
			auto isZeroV = [](const Vector3& v) { return v.x == 0.f && v.y == 0.f && v.z == 0.f; };
			auto readBoneOrOriginRel = [&](int idx, float zOff) -> Vector3 {
				if (!boneBufOk) return Vector3(origin.x, origin.y, origin.z + zOff);
				Vector3 v = readBoneCached(idx);
				if (isZeroV(v)) return Vector3(origin.x, origin.y, origin.z + zOff);
				return v;
			};
			auto readBonePred = [&](int idx) -> Vector3 {
				if (!boneBufOk) return {};
				Vector3 v = readBoneCached(idx);
				if (isZeroV(v)) return {};
				return applyPredict(v);
			};
			const uint32_t hbMaskEarly = (combat.hitbox_mask & 7u) ? (combat.hitbox_mask & 7u) : 1u;
			const bool need_neck_bone = (hbMaskEarly & 2u) != 0 || budget.need_bone_skeleton;
			const bool need_pelvis_bone = (hbMaskEarly & 4u) != 0 || budget.need_bone_skeleton;
			const Vector3 headWorldRaw = readBoneOrOriginRel(cs2_bones::HEAD, 75.f);
			const Vector3 neckWorldRaw = need_neck_bone ? readBoneOrOriginRel(cs2_bones::NECK, 63.f) : headWorldRaw;
			const Vector3 pelvisWorldRaw = need_pelvis_bone ? readBoneOrOriginRel(cs2_bones::PELVIS, 36.f) : headWorldRaw;
			const Vector3 headWorld = applyPredict(headWorldRaw);
			const Vector3 neckWorld = need_neck_bone ? applyPredict(neckWorldRaw) : headWorld;
			const Vector3 pelvisWorld = need_pelvis_bone ? applyPredict(pelvisWorldRaw) : headWorld;

			Vector3 screenhead;
			if (!w2s(headWorld, screenhead, view_matrix))
				screenhead = headWorld.world_to_screen(view_matrix);
			if (screenhead.z < 0.01f)
				continue;

			const bool aimBvhMultiOk = aimWantsBvhLos && global_pawn
			    ? ExpectionalBvhAnyHitboxVisibleBetweenPawnsSmoothed(global_pawn, pawnAddr)
			    : false;
			const uint64_t aimTargetSpotMask = aimSpotReadsFrame ? ReadSpottedMask(pawnAddr) : 0ull;

			const uint32_t hbMask = (combat.hitbox_mask & 7u) ? (combat.hitbox_mask & 7u) : 1u;
			auto considerAimBone = [&](const Vector3& aimWorld, int bone_idx) {
				Vector3 aimWorldUse = aimWorld;
				Vector3 aimScreen;
				
				if (!w2s(aimWorldUse, aimScreen, vmNow))
					aimScreen = aimWorldUse.world_to_screen(vmNow);
				bool aimVisOk = true;
				if (combat.aim_visible_only && global_pawn) {
					aimVisOk = false;
					if (combat.aim_autowall && aw_wd.valid && bvhLosReadyFrame) {
						if (ex_autowall::IsVisibleCatalystStyle(localEyeWorld, aimWorld))
							aimVisOk = true;
						else {
							const int hg = ex_autowall::HitgroupForBoneIdx(bone_idx);
							const int armor = (CachePlayers.armor > 0) ? CachePlayers.armor : ex_autowall::ReadTargetArmor(pawnAddr);
							const bool helmet = ex_autowall::ReadTargetHelmet(pawnAddr, CachePlayers.Controller);
							const int team = CachePlayers.team_num;
							const auto pen = ex_autowall::RunPenetration(localEyeWorld, aimWorld, hg, armor, helmet, team, aw_wd);
							if (pen.ok && pen.damage >= combat.aim_min_damage)
								aimVisOk = true;
						}
					} else if (bvhLosReadyFrame &&
					    ExpectionalBvhVisibleSmoothed(pawnAddr, localEyeWorld, aimWorld))
						aimVisOk = true;
					else if (aimBvhMultiOk)
						aimVisOk = true;
					else if (aimSpotReadsFrame &&
					    SpottedLikeAnanbabanWithMasks(global_pawn, pawnAddr, aimLocalSpotMask, aimTargetSpotMask,
					        static_cast<uint32_t>(CachePlayers.entity_index),
					        static_cast<uint32_t>(CachePlayers.entity_index)))
						aimVisOk = true;
				}
				if (combat.aimbot && aimVisOk) {
					const bool stickyPawn = sprayAimLock && s_stickyAimPawn && pawnAddr == s_stickyAimPawn;
					const float fovUse = stickyPawn ? (aimFovLimit * 3.5f + 50.f) : aimFovLimit;
					const float deadUse = (sprayAimLock || stickyPawn) ? 0.f : combat.aim_fov_min;
					const float cand = std::sqrt((aimCx - aimScreen.x) * (aimCx - aimScreen.x) +
					                             (aimCy - aimScreen.y) * (aimCy - aimScreen.y));
					if (cand > deadUse && cand < fovUse && cand < bestAimFov) {
						bestAimFov = cand;
						bestAimScreen = aimScreen;
						haveBestAim = true;
						bestAimPawn = pawnAddr;
					}
				}
			};
			if (hbMask & 1u)
				considerAimBone(headWorldRaw, 7);
			if (hbMask & 2u)
				considerAimBone(neckWorldRaw, 6);
			if (hbMask & 4u)
				considerAimBone(pelvisWorldRaw, 1);

			const Vector3 projectHead = headWorld.world_to_screen(view_matrix);

			const float rawH = fabsf(screenpos.y - screenhead.y);
			catalyst_esp::ScreenBounds catBounds{};
			if (budget.need_cat_bounds && boneBufOk)
				catBounds = catalyst_esp::ComputeScreenBounds(readBonePred, view_matrix);
			const bool useCatBounds = budget.need_cat_bounds && boneBufOk && catBounds.valid;

			float rx = 0.f, ry = 0.f, rw = 0.f, rh = 0.f, rcx = 0.f;
			if (useCatBounds) {
				rx = std::floor(catBounds.minX);
				ry = std::floor(catBounds.minY);
				rw = std::floor(catBounds.width());
				rh = std::floor(catBounds.height());
				rcx = rx + rw * 0.5f;
				if (rw < 4.f || rh < 4.f)
					continue;
			} else {
				if (rawH < 4.f)
					continue;
				const float spanY = rawH;
				const float boxH = spanY * 1.09f;
				const float boxW = boxH * 0.6f;
				const float topRef = fminf(screenhead.y, screenpos.y);
				const float boxTop = topRef - boxH * 0.08f;
				const float boxLeft = screenhead.x - boxW * 0.5f;
				rx = std::floor(boxLeft + 0.5f);
				ry = std::floor(boxTop + 0.5f);
				rw = std::floor(boxW + 0.5f);
				rh = std::floor(boxH + 0.5f);
				rcx = boxLeft + boxW * 0.5f;
			}

			const bool wantHideOccluded = Settings::Visuals::esp_visible_only && global_pawn;
			bool espLosClear = false;
			if (budget.need_bvh_esp_los && global_pawn && bvhLosReadyFrame)
				espLosClear = ExpectionalBvhVisibleSmoothed(pawnAddr, localEyeWorld, headWorld);
			
			const bool allowDraw = Settings::Visuals::enablePlayerEsp &&
			    (!wantHideOccluded || (bvhLosReadyFrame && espLosClear)) && !blindHideAll;
			
			if (Settings::Visuals::enablePlayerEsp && Settings::Visuals::bombCarrierEsp && CachePlayers.has_c4 && !blindHideAll) {
				ImDrawList* bgC4 = ImGui::GetBackgroundDrawList();
				float labelTopC4 = ry - 2.f;
				const ImVec2 cs = ImGui::CalcTextSize("[C4]");
				labelTopC4 -= cs.y;
				const ImVec2 c4p = EspLayout::Shift(EspLayout::C4, rcx, labelTopC4);
				const ImU32 c4Col = ImGui::ColorConvertFloat4ToU32(ImVec4(
					EspUiColors::bomb_carrier_tag[0], EspUiColors::bomb_carrier_tag[1],
					EspUiColors::bomb_carrier_tag[2], 1.f));
				ex_esp::StrokeTextBg(bgC4, "[C4]", c4p.x, c4p.y, c4Col);
			}

			if (Settings::Visuals::eyeRay && allowDraw && offsets::m_angEyeAngles) {
				const Vector3 ang = g_GameMem.readv<Vector3>(pawnAddr + static_cast<uintptr_t>(offsets::m_angEyeAngles));
				const ImU32 eyeCol = ImGui::ColorConvertFloat4ToU32(ImVec4(
					EspUiColors::eye_ray[0], EspUiColors::eye_ray[1], EspUiColors::eye_ray[2], 1.f));
				ex_esp::DrawEyeRay(view_matrix, headWorld, ang, 80.f, eyeCol, 1.6f);
			}

			if (allowDraw) {
				ImDrawList* bg = ImGui::GetBackgroundDrawList();
				
				const bool useLosColors = budget.need_bvh_esp_los && global_pawn && bvhLosReadyFrame;
				
				const auto pickRgb3 = [&](const float* hid, const float* vis) -> ImVec4 {
					if (useLosColors && espLosClear)
						return ImVec4(vis[0], vis[1], vis[2], 1.f);
					return ImVec4(hid[0], hid[1], hid[2], 1.f);
				};
				const auto pickRgba4 = [&](const float* hid, const float* vis) -> ImVec4 {
					if (useLosColors && espLosClear)
						return ImVec4(vis[0], vis[1], vis[2], vis[3]);
					return ImVec4(hid[0], hid[1], hid[2], hid[3]);
				};

				const ImU32 outlineIm = ImGui::ColorConvertFloat4ToU32(
				    pickRgb3(EspUiColors::espcol, EspUiColors::vis_espcol));
				const ImU32 snapDrawCol = ImGui::ColorConvertFloat4ToU32(
				    pickRgb3(EspUiColors::snapline_esp, EspUiColors::snapline_vis_los));
				const ImU32 skelCol = ImGui::ColorConvertFloat4ToU32(
				    pickRgb3(EspUiColors::skel_col, EspUiColors::skel_vis_col));
				const ImU32 nameCol = ImGui::ColorConvertFloat4ToU32(
				    pickRgb3(EspUiColors::name_esp, EspUiColors::name_vis_esp));
				const ImU32 wpnCol = ImGui::ColorConvertFloat4ToU32(
				    pickRgb3(EspUiColors::weapon_esp, EspUiColors::weapon_vis_esp));
				const ImU32 distCol = ImGui::ColorConvertFloat4ToU32(
				    pickRgb3(EspUiColors::distance_esp, EspUiColors::distance_vis_esp));
				const ImU32 headFillCol = ImGui::ColorConvertFloat4ToU32(
				    pickRgba4(EspUiColors::head_circle_fill, EspUiColors::head_circle_vis_fill));

				const float hpNorm = std::clamp(static_cast<float>(liveHp) / 100.f, 0.f, 1.f);
				const ImVec4 hd(EspUiColors::health_text_damaged[0], EspUiColors::health_text_damaged[1],
					EspUiColors::health_text_damaged[2], 1.f);
				const ImVec4 hf(EspUiColors::health_text_full[0], EspUiColors::health_text_full[1],
					EspUiColors::health_text_full[2], 1.f);
				const ImU32 hpColU32 = ImGui::ColorConvertFloat4ToU32(ImVec4(
					hd.x * (1.f - hpNorm) + hf.x * hpNorm,
					hd.y * (1.f - hpNorm) + hf.y * hpNorm,
					hd.z * (1.f - hpNorm) + hf.z * hpNorm,
					1.f));
				const std::string hpTextStr = std::to_string(static_cast<int32_t>(liveHp));

				catalyst_esp::DrawOffsets off{};

				{
					float labelTop = ry - 2.f;
					if (Settings::Visuals::bombCarrierEsp && CachePlayers.has_c4 && !blindHideAll)
						labelTop -= ImGui::CalcTextSize("[C4]").y + 2.f;
					if (Settings::Visuals::names && !CachePlayers.name.empty()) {
						const ImVec2 ns = ImGui::CalcTextSize(CachePlayers.name.c_str());
						labelTop -= ns.y;
						const ImVec2 p = EspLayout::Shift(EspLayout::Name, rcx, labelTop);
						ex_esp::StrokeTextBg(bg, CachePlayers.name.c_str(), p.x, p.y, nameCol);
					}
				}

				if (Settings::Visuals::bBox && Settings::Visuals::boxMode != 0) {
					const bool wantFill = Settings::Visuals::filledBox || Settings::Visuals::boxMode == 3;
					const bool outline = true;
					const int bm = (Settings::Visuals::boxMode == 3) ? 1 : Settings::Visuals::boxMode;
					catalyst_esp::DrawCatalystBox(bg, rx, ry, rw, rh, outlineIm, wantFill, outline, bm);
				}

				if (Settings::Visuals::bSnaplines)
					ex_esp::DrawSnapLine(rcx, ry, ry + rh, snapDrawCol, 1.4f);

				if (Settings::Visuals::headcircle) {
					const float rad = useCatBounds ? (rh * 0.08f) : (rawH / 10.f);
					const ImVec2 hs = EspLayout::Shift(EspLayout::Head, projectHead.x, projectHead.y);
					bg->AddCircle(hs, rad, outlineIm, 0, 1.6f);
					bg->AddCircleFilled(hs, rad, headFillCol, 32);
				}

				if (Settings::Visuals::healthBar) {
					const ImVec2 hp = EspLayout::Shift(EspLayout::HealthBar, rx, ry);
					catalyst_esp::DrawHealthBarLeft(bg, hp.x, hp.y, rh, liveHp, off,
						liveHp < 100 || Settings::Visuals::healthText);
				}

				if (Settings::Visuals::armor && CachePlayers.armor > 0) {
					const float ap = (std::min)(CachePlayers.armor, 100) / 100.f;
					const ImVec2 ar = EspLayout::Shift(EspLayout::Armor, std::floor(rx - off.left - 4.f - 4.f), ry);
					const ImU32 armorCol = ImGui::ColorConvertFloat4ToU32(ImVec4(
						EspUiColors::armor_bar[0], EspUiColors::armor_bar[1], EspUiColors::armor_bar[2], 1.f));
					ExpectionalAnanbabanVBar(static_cast<int>(ar.x), static_cast<int>(ar.y), 4, static_cast<int>(rh), ap, armorCol);
					off.left += 4.f + 4.f;
				}

				if ((Settings::Visuals::ammoBar || Settings::Visuals::ammoText) && offsets::m_iClip1) {
					const int clip = ex_esp::ReadClipFromActiveWeapon(pawnAddr);
					const int reserveGuess = (clip >= 0) ? (clip + 30) : 30;
					const int maxAmmo = (std::max)(reserveGuess, clip);
					const ImU32 ammoTxtCol = ImGui::ColorConvertFloat4ToU32(ImVec4(
						EspUiColors::ammo_text_esp[0], EspUiColors::ammo_text_esp[1], EspUiColors::ammo_text_esp[2], 1.f));
					const ImVec2 am = EspLayout::Shift(EspLayout::Ammo, rx, ry + rh);
					catalyst_esp::DrawAmmoBarBottom(bg, am.x, am.y, rw, rh, clip, maxAmmo, off,
						Settings::Visuals::ammoBar, Settings::Visuals::ammoText, ammoTxtCol);
				}

				if ((Settings::Visuals::weaponEsp || Settings::Visuals::weaponEspIcon) && !CachePlayers.weapon.empty()) {
					const ImVec2 wp = EspLayout::Shift(EspLayout::Weapon, rx, ry + rh);
					catalyst_esp::DrawWeaponGlyphAndOrText(bg, wp.x, wp.y, rw, CachePlayers.weapon.c_str(), wpnCol, off,
						Settings::Visuals::weaponEspIcon, Settings::Visuals::weaponEsp);
				}

				{
					float fx = rx + rw + 4.f + off.right;
					float fy = ry;
					float fmaxW = 0.f;
					if (Settings::Visuals::distance) {
						const float dx = origin.x - localOriginForDist.x;
						const float dy = origin.y - localOriginForDist.y;
						const float dz = origin.z - localOriginForDist.z;
						const int dm = (int)(std::sqrt(dx * dx + dy * dy + dz * dz) * 0.01f);
						char buf[32];
						snprintf(buf, sizeof buf, "%dm", dm);
						const ImVec2 p = EspLayout::Shift(EspLayout::Distance, fx, fy);
						ex_esp::StrokeTextBgLeft(bg, buf, p.x, p.y, distCol);
						const ImVec2 dsz = ImGui::CalcTextSize(buf);
						fy += dsz.y + 3.f;
						fmaxW = (std::max)(fmaxW, dsz.x);
					}
					if (Settings::Visuals::healthText) {
						const bool hpOnBar = Settings::Visuals::healthBar;
						if (!hpOnBar) {
							const ImVec2 p = EspLayout::Shift(EspLayout::HealthText, fx, fy);
							ex_esp::StrokeTextBgLeft(bg, hpTextStr.c_str(), p.x, p.y, hpColU32);
							const ImVec2 hpsz = ImGui::CalcTextSize(hpTextStr.c_str());
							fy += hpsz.y + 3.f;
							fmaxW = (std::max)(fmaxW, hpsz.x);
						}
					}
					if (Settings::Visuals::showScoped && offsets::m_bIsScoped) {
						const bool sc = g_GameMem.readv<bool>(pawnAddr + static_cast<uintptr_t>(offsets::m_bIsScoped));
						if (sc) {
							const ImU32 zCol = ImGui::ColorConvertFloat4ToU32(ImVec4(
								EspUiColors::scoped_label[0], EspUiColors::scoped_label[1], EspUiColors::scoped_label[2], 1.f));
							const ImVec2 p = EspLayout::Shift(EspLayout::Zoom, fx, fy);
							ex_esp::StrokeTextBgLeft(bg, "zoom", p.x, p.y, zCol);
							const ImVec2 zs = ImGui::CalcTextSize("zoom");
							fy += zs.y + 2.f;
							fmaxW = (std::max)(fmaxW, zs.x);
						}
					}
					if (Settings::Visuals::showBlind && offsets::m_flFlashDuration) {
						const float fd = g_GameMem.readv<float>(pawnAddr + static_cast<uintptr_t>(offsets::m_flFlashDuration));
						if (fd > 0.05f) {
							const ImU32 blindCol = ImGui::ColorConvertFloat4ToU32(ImVec4(
								EspUiColors::blind_label[0], EspUiColors::blind_label[1], EspUiColors::blind_label[2], 1.f));
							const ImVec2 p = EspLayout::Shift(EspLayout::Flashed, fx, fy);
							ex_esp::StrokeTextBgLeft(bg, "flashed", p.x, p.y, blindCol);
							const ImVec2 fsz = ImGui::CalcTextSize("flashed");
							fy += fsz.y + 2.f;
							fmaxW = (std::max)(fmaxW, fsz.x);
						}
					}
					off.right += fmaxW + 4.f;
				}

				if (budget.need_bone_skeleton && boneBufOk) {
					const ImVec2 boneShift = EspLayout::Get(EspLayout::Bones);
					for (size_t li = 0; li < cs2_bones::kSkeletonCount; ++li) {
						const cs2_bones::BoneLink& L = cs2_bones::kSkeleton[li];
						const Vector3 wa = readBoneCached(L.a);
						const Vector3 wb = readBoneCached(L.b);
						
						if (isZeroV(wa) || isZeroV(wb))
							continue;
						Vector3 sa, sb;
						if (!w2s(wa, sa, view_matrix) || !w2s(wb, sb, view_matrix))
							continue;
						bg->AddLine(ImVec2(sa.x + boneShift.x, sa.y + boneShift.y), ImVec2(sb.x + boneShift.x, sb.y + boneShift.y), skelCol);
					}
				}
			}
		}

		if (Settings::misc::debug_visible_check && global_pawn && !visDbgFilled)
			ExpectionalFillVisDebug(global_pawn, 0, 0, 0, nullptr);

		if (Settings::misc::debug_visible_check && global_pawn && g_visDbg.valid) {
			ImDrawList* dl = ImGui::GetForegroundDrawList();
			const ExpectionalVisDbg& v = g_visDbg;
			float y = 34.f;
			const ImU32 c = IM_COL32(110, 255, 160, 255);
			char b[320];
			snprintf(b, sizeof b, "VISDBG spotted_off=0x%x idEnt_off=0x%x", (unsigned)v.off_spotted, (unsigned)v.off_id_ent);
			dl->AddText(ImVec2(8.f, y), c, b); y += 14.f;
			snprintf(b, sizeof b, "local pawnH=%d ctrlI=%d | m_iIDEntIndex read=%d", v.local_pawn_h, v.local_ctrl_i, v.id_ent_read);
			dl->AddText(ImVec2(8.f, y), c, b); y += 14.f;
			snprintf(b, sizeof b, "crosshair_pawn=%p", (void*)v.crosshair_pawn);
			dl->AddText(ImVec2(8.f, y), c, b); y += 14.f;
			snprintf(b, sizeof b, "sample enemy: %s pawnLow=%d entI=%d", v.sample_name, v.sample_pawn_low, v.sample_entity_i);
			dl->AddText(ImVec2(8.f, y), c, b); y += 14.f;
			snprintf(b, sizeof b, "mask tgt=%016llx loc=%016llx", (unsigned long long)v.sample_tgt_mask, (unsigned long long)v.sample_loc_mask);
			dl->AddText(ImVec2(8.f, y), c, b); y += 14.f;
			snprintf(b, sizeof b, "BVH LOS sample: %s | BVH ready=%s tris=%d",
				v.sample_bvh_los ? "Y" : "N",
				v.bvh_ready ? "Y" : "N",
				v.bvh_triangle_count);
			dl->AddText(ImVec2(8.f, y), c, b); y += 14.f;
			const ImU32 c2 = IM_COL32(255, 220, 90, 255);
			snprintf(b, sizeof b, "BVH client=%p vphys2=%p",
				(void*)ex_world_bvh::DbgClientModule(), (void*)ex_world_bvh::DbgVphysModule());
			dl->AddText(ImVec2(8.f, y), c2, b); y += 14.f;
			snprintf(b, sizeof b, "BVH %s tris=%d",
				tri_loader::g_load_source.empty() ? "E:nomap" : tri_loader::g_load_source.c_str(),
				(int)ex_world_bvh::g_world_bvh.count());
			dl->AddText(ImVec2(8.f, y), c2, b); y += 14.f;
			snprintf(b, sizeof b, "BVH pat trace=%p surface=%p",
				(void*)ex_world_bvh::DbgPatternTrace(), (void*)ex_world_bvh::DbgPatternSurf());
			dl->AddText(ImVec2(8.f, y), c2, b); y += 14.f;
			snprintf(b, sizeof b, "BVH parse attempts=%u success=%u last=%d",
				ex_world_bvh::DbgParseAttempts(), ex_world_bvh::DbgParseSuccess(),
				ex_world_bvh::DbgLastExtracted());
			dl->AddText(ImVec2(8.f, y), c2, b);
		}

		if (Settings::Visuals::enablePlayerEsp && Settings::Visuals::awpCrosshair && global_pawn && offsets::m_bIsScoped) {
			const uint16_t wd = ex_esp::ReadWeaponDefIndex(global_pawn);
			if (ex_esp::IsSniperDef(wd)) {
				const bool sc = g_GameMem.readv<bool>(global_pawn + static_cast<uintptr_t>(offsets::m_bIsScoped));
				if (!sc) {
					const ImU32 axCol = ImGui::ColorConvertFloat4ToU32(ImVec4(
						EspUiColors::sniper_crosshair[0], EspUiColors::sniper_crosshair[1],
						EspUiColors::sniper_crosshair[2], 1.f));
					ex_esp::DrawSniperCrosshairCenter(axCol, 4.f, 9.f, 1.25f);
				}
			}
		}

		if (haveBestAim) {
			s_stickyAimPawn = bestAimPawn;
			s_stickyAimLost = 0;
		} else if (sprayAimLock && s_stickyAimPawn) {
			
			const int stickyHp = g_GameMem.readv<int>(s_stickyAimPawn + offsets::m_iHealth);
			if (stickyHp <= 0) {
				s_stickyAimPawn = 0;
				s_stickyAimLost = 0;
				AimControl::ResetSmoothState();
			} else {
				s_stickyAimLost++;
				if (s_stickyAimLost > 48)
					s_stickyAimPawn = 0;
			}
		} else {
			s_stickyAimPawn = 0;
			s_stickyAimLost = 0;
		}

		static int s_rcsAimGraceMiss = 0;
		constexpr int kRcsAimGraceFrames = 48;
		const bool aim_has_target = haveBestAim || (sprayAimLock && s_stickyAimPawn);
		static bool s_rcsHadTarget = false;
		if (s_rcsHadTarget && !aim_has_target)
			RCS::SyncBaseline(global_pawn);
		s_rcsHadTarget = aim_has_target;
		const bool aim_hard_lock = combat.aimbot && aimKeyHeld && aim_has_target;
		if (aim_hard_lock)
			s_rcsAimGraceMiss = 0;
		else if (combat.aimbot && aimKeyHeld)
			s_rcsAimGraceMiss = (std::min)(s_rcsAimGraceMiss + 1, 1000);
		else
			s_rcsAimGraceMiss = 0;
		
		if (!aim_has_target && combat.aimbot && aimKeyHeld && !combat.rcs_standalone)
			s_rcsAimGraceMiss = kRcsAimGraceFrames;
		const bool aim_rcs_lane = combat.aimbot && aimKeyHeld &&
			(aim_hard_lock || s_rcsAimGraceMiss < kRcsAimGraceFrames);
		const bool rcs_may_run = combat.rcs_standalone || aim_rcs_lane;

		const bool ananbaban_rcs_merge = spray_rcs_coop && combat.aimbot && !combat.rcs_standalone &&
			aimKeyHeld && aim_has_target;

		const bool rcs_vis_gate = combat.aim_visible_only && !aim_has_target && !combat.rcs_standalone;

		if (combat.penetration_crosshair && global_pawn && aw_wd.valid && bvhLosReadyFrame && offsets::m_angEyeAngles) {
			const Vector3 vang = g_GameMem.readv<Vector3>(global_pawn + static_cast<uintptr_t>(offsets::m_angEyeAngles));
			if (vang.x > -89.f && vang.x < 89.f) {
				const Vector3 fwd = ex_autowall::AnglesToForward(vang);
				const Vector3 ray_end(localEyeWorld.x + fwd.x * aw_wd.range, localEyeWorld.y + fwd.y * aw_wd.range, localEyeWorld.z + fwd.z * aw_wd.range);
				const auto wall_hit = ex_world_bvh::g_world_bvh.trace_ray(localEyeWorld, ray_end);
				if (wall_hit.hit) {
					bool can = false;
					bool enemyFirst = false;
					if (!localEyeWorld.IsZero() && offsets::m_iTeamNum) {
						const int ltPen = g_GameMem.readv<int>(global_pawn + offsets::m_iTeamNum);
						const auto shPen = ex_autowall::ScanEnemyOnRay(localEyeWorld, fwd, snapshot, global_pawn, ltPen, false);
						if (shPen.found && shPen.t < wall_hit.distance) {
							enemyFirst = true;
							can = ex_autowall::AnyBoneFireOk(localEyeWorld, shPen.pawn, shPen.controller,
							    aw_wd, combat.aim_autowall, combat.aim_min_damage);
						}
					}
					float pen_dmg = 0.f;
					if (!enemyFirst)
						can = ex_autowall::CanPenetrateDirection(localEyeWorld, fwd, aw_wd, pen_dmg);
					const float* col = can ? EspUiColors::pen_crosshair_yes : EspUiColors::pen_crosshair_no;
					const ImU32 c = IM_COL32((int)(col[0] * 255.f), (int)(col[1] * 255.f), (int)(col[2] * 255.f), 255);
					const ImU32 cfill = IM_COL32((int)(col[0] * 255.f), (int)(col[1] * 255.f), (int)(col[2] * 255.f), 90);
					ImDrawList* pdl = ImGui::GetBackgroundDrawList();
					{
						float nx = wall_hit.normal.x, ny = wall_hit.normal.y, nz = wall_hit.normal.z;
						const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
						if (nl > 1e-6f) { nx /= nl; ny /= nl; nz /= nl; }
						else { nx = 0.f; ny = 0.f; nz = 1.f; }
						float rx = 0.f, ry = 0.f, rz = 1.f;
						if (nz >= 0.9f || nz <= -0.9f) { rx = 1.f; ry = 0.f; rz = 0.f; }
						const float d = rx * nx + ry * ny + rz * nz;
						float tx = rx - nx * d, ty = ry - ny * d, tz = rz - nz * d;
						const float tl = std::sqrt(tx * tx + ty * ty + tz * tz);
						if (tl > 1e-6f) { tx /= tl; ty /= tl; tz /= tl; }
						else { tx = 1.f; ty = 0.f; tz = 0.f; }
						const float bx = ny * tz - nz * ty, by = nz * tx - nx * tz, bz = nx * ty - ny * tx;
						const float qx = wall_hit.end_pos.x + nx * 0.05f;
						const float qy = wall_hit.end_pos.y + ny * 0.05f;
						const float qz = wall_hit.end_pos.z + nz * 0.05f;
						constexpr float qhs = 3.5f;
						const float corns[4][3] = {
							{ qx - tx * qhs - bx * qhs, qy - ty * qhs - by * qhs, qz - tz * qhs - bz * qhs },
							{ qx + tx * qhs - bx * qhs, qy + ty * qhs - by * qhs, qz + tz * qhs - bz * qhs },
							{ qx + tx * qhs + bx * qhs, qy + ty * qhs + by * qhs, qz + tz * qhs + bz * qhs },
							{ qx - tx * qhs + bx * qhs, qy - ty * qhs + by * qhs, qz - tz * qhs + bz * qhs },
						};
						ImVec2 sp[4];
						bool qok = true;
						for (int qi = 0; qi < 4; ++qi) {
							const Vector3 wp(corns[qi][0], corns[qi][1], corns[qi][2]);
							Vector3 ss;
							if (!w2s(wp, ss, view_matrix) || ss.z < 0.01f) { qok = false; break; }
							sp[qi] = ImVec2(ss.x, ss.y);
						}
						if (qok) {
							pdl->AddQuadFilled(sp[0], sp[1], sp[2], sp[3], cfill);
							pdl->AddQuad(sp[0], sp[1], sp[2], sp[3], c, 1.5f);
						}
					}
					const float cx = aimCx, cy = aimCy;
					const float gap = 4.f, arm = 7.f;
					pdl->AddLine(ImVec2(cx - gap - arm, cy), ImVec2(cx - gap, cy), c, 1.5f);
					pdl->AddLine(ImVec2(cx + gap, cy), ImVec2(cx + gap + arm, cy), c, 1.5f);
					pdl->AddLine(ImVec2(cx, cy - gap - arm), ImVec2(cx, cy - gap), c, 1.5f);
					pdl->AddLine(ImVec2(cx, cy + gap), ImVec2(cx, cy + gap + arm), c, 1.5f);
				}
			}
		}
		RCS::Tick(global_pawn, combat, rcs_may_run && !rcs_vis_gate, aim_hard_lock, spray_rcs_coop, ananbaban_rcs_merge);
		AimControl::RunLegitMouse(aimCx, aimCy, aimKeyHeld, haveBestAim, bestAimScreen, combat,
			shots_coop, spray_rcs_coop, ananbaban_rcs_merge);
		if (combat.triggerbot)
			TriggerBot::Run(g_GameMem.readv<int>(global_pawn + offsets::m_iTeamNum), combat, snapshot);
	}
	EX_VL_PROTECT_END;
}
