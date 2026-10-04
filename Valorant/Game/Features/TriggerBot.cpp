#include "TriggerBot.h"

#include "../globals.hpp"
#include "../weapon_runtime.hpp"
#include "../offsets_runtime.hpp"
#include "../entity_handle.hpp"
#include "../weapon_names.hpp"
#include "../esp_extras.hpp"
#include "../spotted_visibility.hpp"
#include "../catalyst_world_bvh.hpp"
#include "../ex_autowall.hpp"
#include "../../Overlay/menu.hpp"

#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <thread>

#include "Protection/vxlang_per_tu.hpp"

namespace {

using clock = std::chrono::steady_clock;
clock::time_point g_lastShot = clock::now();
clock::time_point g_targetFound = clock::now();
bool g_hasValidTarget = false;
uintptr_t g_ttdStablePawn = 0;
clock::time_point g_ttdStableSince{};
bool g_trigToggleArm = false;
bool g_trigKeyPrev = false;
long long g_sampledReactionMs = 0;
std::mt19937 g_trigDelayRng{std::random_device{}()};

static std::string ReadLocalWeaponName(uintptr_t pawn) {
	const uint16_t defIdx = ex_esp::ReadWeaponDefIndex(pawn);
	const char* nm = WeaponNameFromDefIndex(defIdx);
	return nm ? std::string(nm) : std::string("unknown");
}

static void ToLowerAscii(std::string& s) {
	for (char& c : s) {
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}
}

static bool IsThrowableOrKnifeC4(const std::string& w) {
	std::string n = w;
	ToLowerAscii(n);
	return n.find("smoke") != std::string::npos || n.find("flash") != std::string::npos ||
	    n.find("hegrenade") != std::string::npos || n == "hegrenade" ||
	    n.find("molotov") != std::string::npos || n.find("decoy") != std::string::npos ||
	    n.find("incgrenade") != std::string::npos || n.find("knife") != std::string::npos ||
	    IsKnifeCodeName(n.c_str()) ||
	    n == "c4" || n.find("zeus") != std::string::npos;
}

static void ExecuteShot() {
	if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0)
		return;
	g_lastShot = clock::now();
	
	std::uniform_int_distribution<int> d(12, 22);
	const int ms = d(g_trigDelayRng);
	mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
	mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
}

static UE4Structs::Vector3 TrigReadEye(uintptr_t localPawn) noexcept {
	if (!localPawn || !offsets::m_pGameSceneNode || !offsets::m_boneArrayFromScene)
		return {};
	const uintptr_t gs = g_GameMem.readv<uintptr_t>(localPawn + static_cast<uintptr_t>(offsets::m_pGameSceneNode));
	if (!gs) return {};
	const uint64_t ba = g_GameMem.readv<uint64_t>(gs + offsets::m_boneArrayFromScene);
	if (ba >= 0x10000ull) {
		const UE4Structs::Vector3 h = g_GameMem.readv<UE4Structs::Vector3>(ba + static_cast<uintptr_t>(7 * 0x20));
		if (h.x != 0.f || h.y != 0.f || h.z != 0.f) return h;
	}
	if (offsets::m_vecOrigin) {
		const UE4Structs::Vector3 o = g_GameMem.readv<UE4Structs::Vector3>(localPawn + static_cast<uintptr_t>(offsets::m_vecOrigin));
		return UE4Structs::Vector3(o.x, o.y, o.z + 75.f);
	}
	return {};
}

static UE4Structs::Vector3 TrigReadTargetHead(uintptr_t targetPawn) noexcept {
	if (!targetPawn) return {};
	if (offsets::m_pGameSceneNode && offsets::m_boneArrayFromScene) {
		const uintptr_t gs = g_GameMem.readv<uintptr_t>(targetPawn + static_cast<uintptr_t>(offsets::m_pGameSceneNode));
		if (gs) {
			const uint64_t ba = g_GameMem.readv<uint64_t>(gs + offsets::m_boneArrayFromScene);
			if (ba >= 0x10000ull) {
				const UE4Structs::Vector3 h = g_GameMem.readv<UE4Structs::Vector3>(ba + static_cast<uintptr_t>(7 * 0x20));
				if (h.x != 0.f || h.y != 0.f || h.z != 0.f) return h;
			}
		}
	}
	if (offsets::m_vecOrigin) {
		const UE4Structs::Vector3 o = g_GameMem.readv<UE4Structs::Vector3>(targetPawn + static_cast<uintptr_t>(offsets::m_vecOrigin));
		return UE4Structs::Vector3(o.x, o.y, o.z + 75.f);
	}
	return {};
}

static bool TrigCatalystVisibleOk(uintptr_t localPawn, uintptr_t targetPawn, uint32_t spotIdx) noexcept {
	if (!localPawn || !targetPawn)
		return false;
	if (ex_world_bvh::g_world_bvh.valid()) {
		const UE4Structs::Vector3 eye = TrigReadEye(localPawn);
		const UE4Structs::Vector3 head = TrigReadTargetHead(targetPawn);
		if (!eye.IsZero() && !head.IsZero()) {
			if (ex_autowall::IsVisibleCatalystStyle(eye, head))
				return true;
		}
	}
	return ExpectionalTriggerVisibilityOk(localPawn, targetPawn, spotIdx);
}

static bool IsEnemyPlayerPawn(uintptr_t pawn, uintptr_t localPawn, int localTeam) noexcept {
	if (!pawn || pawn == localPawn)
		return false;
	const int hp = g_GameMem.readv<int>(pawn + offsets::m_iHealth);
	if (hp <= 0 || hp > 100)
		return false;
	const int team = g_GameMem.readv<int>(pawn + offsets::m_iTeamNum) & 0xFF;
	const int lt = localTeam & 0xFF;
	if (Settings::Visuals::enemiesOnly && team == lt)
		return false;
	return true;
}

} 

namespace TriggerBot {

void Run(int localTeam, const LegitCombatSettings& cfg, const std::vector<UE4Structs::CS2Entity>& players) {
	if (!global_pawn)
		return;

	const int tk = hotkeys::triggerkey.load();
	const bool tkDown = tk > 0 && ((GetAsyncKeyState(tk) & 0x8000) != 0);
	
	if (!Settings::bMenu && cfg.triggerbot && cfg.trigger_key_mode == 1 && tkDown && !g_trigKeyPrev)
		g_trigToggleArm = !g_trigToggleArm;
	g_trigKeyPrev = tkDown;

	if (!cfg.triggerbot || Settings::bMenu)
		return;

	const auto clearTarget = []() {
		g_hasValidTarget = false;
		g_targetFound = clock::now();
		g_ttdStablePawn = 0;
	};

	uint32_t trigSpotIdx = 0;
	int entIndex = -1;
	if (offsets::m_iIDEntIndex)
		entIndex = g_GameMem.readv<int>(global_pawn + static_cast<uintptr_t>(offsets::m_iIDEntIndex));

	uintptr_t targetPawn =
	    ExpectionalResolveTriggerTargetPawn(global_pawn, localTeam, entIndex, &trigSpotIdx);
	uintptr_t targetCtrl = 0;
	bool fromScan = false;

	if (!IsEnemyPlayerPawn(targetPawn, global_pawn, localTeam) && cfg.trigger_autowall &&
	    ex_world_bvh::g_world_bvh.valid()) {
		const UE4Structs::Vector3 eye = ex_autowall::ReadEyePos(global_pawn);
		if (!eye.IsZero() && offsets::m_angEyeAngles) {
			const UE4Structs::Vector3 va =
			    g_GameMem.readv<UE4Structs::Vector3>(global_pawn + static_cast<uintptr_t>(offsets::m_angEyeAngles));
			if (va.x > -89.f && va.x < 89.f) {
				const UE4Structs::Vector3 fwd = ex_autowall::AnglesToForward(va);
				const auto sh = ex_autowall::ScanEnemyOnRay(eye, fwd, players, global_pawn, localTeam, cfg.trigger_head_only);
				if (sh.found && IsEnemyPlayerPawn(sh.pawn, global_pawn, localTeam)) {
					targetPawn = sh.pawn;
					targetCtrl = sh.controller;
					trigSpotIdx = sh.spot_idx;
					fromScan = true;
				}
			}
		}
	}

	if (!IsEnemyPlayerPawn(targetPawn, global_pawn, localTeam)) {
		clearTarget();
		return;
	}

	if (offsets::m_bWaitForNoAttack) {
		const bool waitNo = g_GameMem.readv<bool>(global_pawn + static_cast<uintptr_t>(offsets::m_bWaitForNoAttack));
		if (waitNo) {
			const bool crossOnTarget = CrosshairEntityIsTargetPawn(global_pawn, targetPawn);
			const bool inSmoke = ExpectionalLocalInsideActiveSmoke(global_pawn);
			const bool targetInSmoke = ExpectionalPawnInsideActiveSmoke(targetPawn);
			if (!crossOnTarget && !inSmoke && !targetInSmoke && !fromScan) {
				clearTarget();
				return;
			}
		}
	}

	const std::string wpn = ReadLocalWeaponName(global_pawn);
	if (IsThrowableOrKnifeC4(wpn)) {
		clearTarget();
		return;
	}

	if (cfg.trigger_stopped_only && offsets::m_vecAbsVelocity) {
		const float vx = g_GameMem.readv<float>(global_pawn + static_cast<uintptr_t>(offsets::m_vecAbsVelocity));
		const float vy = g_GameMem.readv<float>(global_pawn + static_cast<uintptr_t>(offsets::m_vecAbsVelocity) + 4);
		const float spd = std::sqrt(vx * vx + vy * vy);
		if (spd > 1.5f) {
			clearTarget();
			return;
		}
	}

	if (!cfg.trigger_ignore_flash && offsets::m_flFlashDuration) {
		const float flash = g_GameMem.readv<float>(global_pawn + static_cast<uintptr_t>(offsets::m_flFlashDuration));
		if (flash > 0.01f) {
			clearTarget();
			return;
		}
	}

	const auto now = clock::now();

	bool trigFireKey = false;
	long long sinceShot = 0;
	long long sinceFound = 0;
	long long shotCd = 0;

	{
		const int tkm = cfg.trigger_key_mode;
		if (tkm == 0)
			trigFireKey = tkDown;
		else if (tkm == 1)
			trigFireKey = g_trigToggleArm;
		else if (tkm == 2)
			trigFireKey = true;
		else
			trigFireKey = tkDown;
	}

	if (targetPawn != g_ttdStablePawn) {
		g_ttdStablePawn = targetPawn;
		g_ttdStableSince = now;
	}

	const long long stableMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_ttdStableSince).count();
	const long long needTtd = static_cast<long long>(std::clamp(cfg.trigger_ttd_delay_ms, 0.f, 400.f));
	if (stableMs >= needTtd) {
	if (!g_hasValidTarget) {
		g_targetFound = clock::now();
		g_hasValidTarget = true;
		if (cfg.trigger_reaction_enabled) {
			float mn = (std::min)(cfg.trigger_delay_min, cfg.trigger_delay_max);
			float mx = (std::max)(cfg.trigger_delay_min, cfg.trigger_delay_max);
			mn = std::clamp(mn, 0.f, 300.f);
			mx = std::clamp(mx, 0.f, 300.f);
			std::uniform_real_distribution<float> dist(mn, mx);
			g_sampledReactionMs = static_cast<long long>(std::llround(dist(g_trigDelayRng)));
		} else {
			g_sampledReactionMs = 0;
		}
	}

	sinceShot = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastShot).count();
	sinceFound = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_targetFound).count();
	shotCd = static_cast<long long>(std::clamp(cfg.trigger_shot_cooldown, 1.f, 1000.f));

	const long long needReact = cfg.trigger_autowall ? 0LL : g_sampledReactionMs;
	if (trigFireKey && sinceShot >= shotCd && sinceFound >= needReact) {
		bool mayFire = true;
		const bool needEval = cfg.trigger_visible_only || cfg.trigger_autowall ||
		    ExpectionalCrosshairIsSmokeEntity(global_pawn);
		if (needEval) {
			const ex_autowall::WeaponData twd = ex_autowall::ReadWeaponData(global_pawn);
			const UE4Structs::Vector3 tEye = ex_autowall::ReadEyePos(global_pawn);
			UE4Structs::Vector3 tFwd{ 0.f, 0.f, 0.f };
			bool hasFwd = false;
			if (cfg.trigger_head_only && offsets::m_angEyeAngles && !tEye.IsZero()) {
				const UE4Structs::Vector3 va = g_GameMem.readv<UE4Structs::Vector3>(
				    global_pawn + static_cast<uintptr_t>(offsets::m_angEyeAngles));
				if (va.x > -89.f && va.x < 89.f) {
					tFwd = ex_autowall::AnglesToForward(va);
					hasFwd = true;
				}
			}
			const auto boneOk = [&]() -> bool {
				if (cfg.trigger_head_only) {
					const UE4Structs::Vector3 hp = ex_autowall::ReadBonePoint(targetPawn, 7, 75.f);
					if (hp.IsZero())
						return false;
					const int armor = ex_autowall::ReadTargetArmor(targetPawn);
					const bool helmet = ex_autowall::ReadTargetHelmet(targetPawn, targetCtrl);
					int team = 0;
					if (offsets::m_iTeamNum)
						team = g_GameMem.readv<int>(targetPawn + offsets::m_iTeamNum) & 0xFF;
					return ex_autowall::PointFireOk(tEye, hp, 1, armor, helmet, team, twd,
					    cfg.trigger_autowall, cfg.trigger_min_damage);
				}
				return ex_autowall::AnyBoneFireOk(tEye, targetPawn, targetCtrl, twd,
				    cfg.trigger_autowall, cfg.trigger_min_damage);
			};
			const auto headRayOk = [&]() -> bool {
				if (!cfg.trigger_head_only)
					return true;
				if (!hasFwd || tEye.IsZero())
					return false;
				const UE4Structs::Vector3 hp = ex_autowall::ReadBonePoint(targetPawn, 7, 75.f);
				if (hp.IsZero())
					return false;
				float dummy = 0.f;
				return ex_autowall::RayHitSphere(tEye, tFwd, hp, ex_autowall::SphereRadiusFor(0), dummy);
			};
			if (cfg.trigger_visible_only || ExpectionalCrosshairIsSmokeEntity(global_pawn)) {
				mayFire = TrigCatalystVisibleOk(global_pawn, targetPawn, trigSpotIdx);
				if (mayFire && cfg.trigger_autowall)
					mayFire = boneOk();
				else if (!mayFire && cfg.trigger_autowall && ex_world_bvh::g_world_bvh.valid())
					mayFire = boneOk();
			} else {
				mayFire = boneOk();
			}
			if (mayFire && !headRayOk())
				mayFire = false;
		}
		if (mayFire)
			ExecuteShot();
	}
	}
}

bool ToggleArmForUi() {
	return g_trigToggleArm;
}

} 
