#include "RCS.hpp"
#include "../globals.hpp"
#include "../offsets_runtime.hpp"
#include "../structs.hpp"
#include "../weapon_runtime.hpp"
#include "../../Driver/driver.hpp"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include "Protection/vxlang_per_tu.hpp"

namespace {

enum class WpnKind : uint8_t { None, Rifle, Smg, Pistol, Heavy };

constexpr std::ptrdiff_t kAimPunchCacheUtlRel = 0x88;

static void ExpectionalMouseMoveRel(int dx, int dy) {
	if (dx == 0 && dy == 0)
		return;
	mouse_event(MOUSEEVENTF_MOVE, dx, dy, 0, 0);
}

static float ReadGameSensitivity() {
	if (!client || !offsets::dwSensitivity)
		return 1.0f;
	const uintptr_t sens = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwSensitivity));
	if (!sens)
		return 1.0f;
	const std::ptrdiff_t off = offsets::dwSensitivity_sensitivity ? offsets::dwSensitivity_sensitivity : 0x50;
	const float v = g_GameMem.readv<float>(sens + static_cast<uintptr_t>(off));
	if (v < 0.01f || v > 100.f)
		return 1.0f;
	return v;
}

static WpnKind ClassifyWeapon(const std::string& w) {
	if (w.empty() || w == "unknown")
		return WpnKind::Rifle;
	if (w == "awp" || w == "ssg08" || w == "scar20" || w == "g3sg1")
		return WpnKind::None;
	if (w == "ct_knife" || w == "t_knife" || w == "c4" || w == "zeus")
		return WpnKind::None;
	if (w == "flashbang" || w == "hegrenade" || w == "smokegrenade" || w == "molotov" ||
	    w == "decoy" || w == "incgrenade")
		return WpnKind::None;
	if (w == "p90" || w == "bizon" || w == "mac10" || w == "mp7" || w == "mp9" ||
	    w == "ump45" || w == "mp5sd")
		return WpnKind::Smg;
	if (w == "deagle" || w == "elite" || w == "fiveseven" || w == "glock" || w == "p2000" ||
	    w == "p250" || w == "revolver" || w == "tec9" || w == "usp")
		return WpnKind::Pistol;
	if (w == "m249" || w == "negev" || w == "mag7" || w == "nova" || w == "sawedoff" ||
	    w == "xm1014")
		return WpnKind::Heavy;
	return WpnKind::Rifle;
}

static void PatternStep(WpnKind k, int patternIdx, float* pitchStep, float* yawStep) {
	static constexpr float kRiflePitch[] = {
		0.26f, 0.24f, 0.22f, 0.20f, 0.18f, 0.17f, 0.16f, 0.15f, 0.14f, 0.13f,
		0.13f, 0.12f, 0.12f, 0.11f, 0.11f, 0.10f, 0.10f, 0.09f, 0.09f, 0.09f,
		0.09f, 0.09f, 0.08f, 0.08f, 0.08f, 0.08f, 0.08f, 0.08f, 0.07f, 0.07f,
		0.07f, 0.07f, 0.07f, 0.07f, 0.06f, 0.06f, 0.06f, 0.06f, 0.06f, 0.06f,
	};
	static constexpr float kRifleYaw[] = {
		-0.02f, 0.03f, -0.04f, 0.05f, -0.03f, 0.04f, -0.05f, 0.03f, -0.04f, 0.05f,
		-0.03f, 0.04f, -0.05f, 0.03f, -0.04f, 0.04f, -0.03f, 0.05f, -0.04f, 0.03f,
		-0.05f, 0.04f, -0.03f, 0.05f, -0.04f, 0.03f, -0.05f, 0.04f, -0.03f, 0.05f,
		-0.04f, 0.03f, -0.05f, 0.04f, -0.03f, 0.05f, -0.04f, 0.03f, -0.05f, 0.04f,
	};
	static constexpr int kN = static_cast<int>(sizeof(kRiflePitch) / sizeof(kRiflePitch[0]));
	const int i = (std::max)(0, patternIdx);
	const int idx = (i < kN) ? i : (kN - 1);

	float p = kRiflePitch[idx];
	float y = kRifleYaw[idx];
	if (k == WpnKind::Smg) {
		p *= 0.72f;
		y *= 0.85f;
	} else if (k == WpnKind::Pistol) {
		p *= 0.55f;
		y *= 0.7f;
	} else if (k == WpnKind::Heavy) {
		p *= 1.15f;
		y *= 1.1f;
	}
	*pitchStep = p;
	*yawStep = y;
}

static int g_rcsZeroStreak = 0;
static int g_estPrevShots = 0;
static int g_rcsLastShots = 0;
static float g_oldPx = 0.f;
static float g_oldPy = 0.f;
static char g_lastPunchSrc = 0;
static double g_rcsAccumMouseX = 0.0;
static double g_rcsAccumMouseY = 0.0;
static double g_rcsLpDx = 0.0;
static double g_rcsLpDy = 0.0;
static ULONGLONG g_rcsLastShotTick = 0;

static double g_holdDown = 0.0;

static int ClampI(int v, int lo, int hi) {
	return (std::max)(lo, (std::min)(hi, v));
}

static void RcsSmoothReset() {
	g_rcsLpDx = g_rcsLpDy = 0.0;
}

static void RcsAccumulateSmoothed(double rawMx, double rawMy, float rcs_smooth) {
	const float sm = (std::max)(rcs_smooth, 2.f);
	
	const float a = std::clamp(16.f / sm, 0.05f, 0.92f);
	const double ad = static_cast<double>(a);
	g_rcsLpDx = g_rcsLpDx * (1.0 - ad) + rawMx * ad;
	g_rcsLpDy = g_rcsLpDy * (1.0 - ad) + rawMy * ad;
	g_rcsAccumMouseX += g_rcsLpDx;
	g_rcsAccumMouseY += g_rcsLpDy;
}

static void RcsEmitAccumulatedMouse() {
	
	constexpr double kMaxDebt = 16.0;
	g_rcsAccumMouseX = std::clamp(g_rcsAccumMouseX, -kMaxDebt, kMaxDebt);
	g_rcsAccumMouseY = std::clamp(g_rcsAccumMouseY, -kMaxDebt, kMaxDebt);

	const LONG mx = static_cast<LONG>(std::trunc(g_rcsAccumMouseX));
	const LONG my = static_cast<LONG>(std::trunc(g_rcsAccumMouseY));
	g_rcsAccumMouseX -= static_cast<double>(mx);
	g_rcsAccumMouseY -= static_cast<double>(my);

	constexpr int kMaxStep = 12;
	const int mxs = ClampI(static_cast<int>(mx), -kMaxStep, kMaxStep);
	const int mys = ClampI(static_cast<int>(my), -kMaxStep, kMaxStep);
	if (mxs != mx)
		g_rcsAccumMouseX += static_cast<double>(mx - mxs);
	if (mys != my)
		g_rcsAccumMouseY += static_cast<double>(my - mys);
	g_rcsAccumMouseX = std::clamp(g_rcsAccumMouseX, -kMaxDebt, kMaxDebt);
	g_rcsAccumMouseY = std::clamp(g_rcsAccumMouseY, -kMaxDebt, kMaxDebt);
	if (mxs != 0 || mys != 0)
		ExpectionalMouseMoveRel(mxs, mys);
}

static bool TrivialPunch(float x, float y) {
	return std::fabs(x) + std::fabs(y) < 1e-6f;
}

static bool ReadVec2(uintptr_t addr, float* ox, float* oy) {
	if (!addr)
		return false;
	*ox = g_GameMem.readv<float>(addr);
	*oy = g_GameMem.readv<float>(addr + 4u);
	return true;
}

static bool ReadBestAimPunch(uintptr_t local_pawn, float* apx, float* apy, char* src) {
	*apx = 0.f;
	*apy = 0.f;
	*src = '?';
	if (!local_pawn)
		return false;

	if (offsets::m_pAimPunchServices) {
		const uintptr_t svc = g_GameMem.readv<uintptr_t>(local_pawn + static_cast<uintptr_t>(offsets::m_pAimPunchServices));
		if (svc) {
			const uintptr_t cacheBase = svc + static_cast<uintptr_t>(kAimPunchCacheUtlRel);
			const uint64_t cnt = g_GameMem.readv<uint64_t>(cacheBase);
			const uint64_t dat = g_GameMem.readv<uint64_t>(cacheBase + 8u);
			if (cnt > 0 && cnt < 512u && dat != 0) {
				const uintptr_t last = static_cast<uintptr_t>(dat) +
					static_cast<uintptr_t>((cnt - 1u) * static_cast<uint64_t>(sizeof(UE4Structs::Vector3)));
				const UE4Structs::Vector3 v = g_GameMem.readv<UE4Structs::Vector3>(last);
				if (!TrivialPunch(v.x, v.y)) {
					*apx = v.x;
					*apy = v.y;
					*src = 'C';
					return true;
				}
			}

			float sx = 0.f;
			float sy = 0.f;
			if (offsets::m_aimPunchPredictableRel &&
			    ReadVec2(svc + static_cast<uintptr_t>(offsets::m_aimPunchPredictableRel), &sx, &sy) &&
			    !TrivialPunch(sx, sy)) {
				*apx = sx;
				*apy = sy;
				*src = 'P';
				return true;
			}
			if (offsets::m_aimPunchUnpredictableRel &&
			    ReadVec2(svc + static_cast<uintptr_t>(offsets::m_aimPunchUnpredictableRel), &sx, &sy) &&
			    !TrivialPunch(sx, sy)) {
				*apx = sx;
				*apy = sy;
				*src = 'U';
				return true;
			}
		}
	}

	if (offsets::m_aimPunchAngle) {
		const uintptr_t a = local_pawn + static_cast<uintptr_t>(offsets::m_aimPunchAngle);
		float lx = 0.f;
		float ly = 0.f;
		if (ReadVec2(a, &lx, &ly) && !TrivialPunch(lx, ly)) {
			*apx = lx;
			*apy = ly;
			*src = 'L';
			return true;
		}
	}

	return false;
}

static void ApplyPunchDeltaRcs(const LegitCombatSettings& cfg, float apx, float apy, char punchSrc,
	float aim_blend_mult, bool shotAdvanced) {
	const float sens = (std::max)(ReadGameSensitivity(), 0.01f);
	const float pathSoft = (punchSrc == 'C') ? 0.95f : (punchSrc == 'L') ? 0.82f : 0.55f;
	const float strength =
		2.f * (cfg.rcs_scale_pct / 100.f) * cfg.rcs_sens_mult * pathSoft * aim_blend_mult;

	if (g_lastPunchSrc != 0 && g_lastPunchSrc != punchSrc) {
		g_oldPx = apx;
		g_oldPy = apy;
		g_lastPunchSrc = punchSrc;
		RcsSmoothReset();
		return;
	}
	g_lastPunchSrc = punchSrc;

	const float oldMag = g_oldPx * g_oldPx + g_oldPy * g_oldPy;
	const float newMag = apx * apx + apy * apy;
	if (newMag < oldMag) {
		g_oldPx = apx;
		g_oldPy = apy;
		g_rcsAccumMouseX = g_rcsAccumMouseY = 0.0;
		RcsSmoothReset();
		return;
	}

	const float delta_x = -(apx - g_oldPx);
	const float delta_y = -(apy - g_oldPy);

	constexpr float kMaxPunchDeltaDeg = 6.f;
	if (std::fabs(delta_x) > kMaxPunchDeltaDeg || std::fabs(delta_y) > kMaxPunchDeltaDeg) {
		g_oldPx = apx;
		g_oldPy = apy;
		
		RcsSmoothReset();
		return;
	}

	double mxF = static_cast<double>(delta_y) * static_cast<double>(strength) /
		(static_cast<double>(sens) * -0.022);
	double myF = static_cast<double>(delta_x) * static_cast<double>(strength) /
		(static_cast<double>(sens) * 0.022);
	
	constexpr double kMaxRaw = 28.0;
	mxF = std::clamp(mxF, -kMaxRaw, kMaxRaw);
	myF = std::clamp(myF, -kMaxRaw, kMaxRaw);
	if (shotAdvanced && g_holdDown > 0.0) {
		myF += g_holdDown;
		g_holdDown = 0.0;
		myF = std::clamp(myF, -kMaxRaw, kMaxRaw);
	}
	
	if (!shotAdvanced && myF > 0.0) {
		g_holdDown = std::clamp(g_holdDown + myF, 0.0, kMaxRaw);
		myF = 0.0;
		g_rcsLpDy = 0.0;
		if (g_rcsAccumMouseY > 0.0)
			g_rcsAccumMouseY = 0.0;
	}

	RcsAccumulateSmoothed(mxF, myF, cfg.rcs_smooth);
	RcsEmitAccumulatedMouse();

	g_oldPx = apx;
	g_oldPy = apy;
}

} 

namespace RCS {

bool TryReadAimPunch(uintptr_t local_pawn, float* apx, float* apy) {
	char ps = '?';
	return ReadBestAimPunch(local_pawn, apx, apy, &ps);
}

void ResetState() {
	g_rcsZeroStreak = 0;
	g_estPrevShots = 0;
	g_rcsLastShots = 0;
	g_oldPx = g_oldPy = 0.f;
	g_lastPunchSrc = 0;
	g_holdDown = 0.0;
	g_rcsAccumMouseX = g_rcsAccumMouseY = 0.0;
	RcsSmoothReset();
}

void SyncBaseline(uintptr_t local_pawn) {
	g_rcsAccumMouseX = g_rcsAccumMouseY = 0.0;
	g_holdDown = 0.0;
	RcsSmoothReset();
	char ps = '?';
	float px = 0.f;
	float py = 0.f;
	if (ReadBestAimPunch(local_pawn, &px, &py, &ps)) {
		g_oldPx = px;
		g_oldPy = py;
		g_lastPunchSrc = ps;
	}
}

void Tick(uintptr_t local_pawn, const LegitCombatSettings& cfg, bool rcs_may_run,
	bool aimbot_hard_lock, bool spray_rcs_coop, bool ananbaban_merge) {
	if (!cfg.rcs_enabled || !local_pawn || Settings::bMenu)
		return;
	if (!offsets::m_iShotsFired)
		return;

	const float aim_blend_mult = 1.f;

	const bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	const int shots = g_GameMem.readv<int>(local_pawn + static_cast<uintptr_t>(offsets::m_iShotsFired));

	if (shots <= 0) {
		if (g_estPrevShots == 0 && g_rcsLastShots == 0) {
			g_rcsZeroStreak = 0;
			return;
		}
		++g_rcsZeroStreak;
		if (g_rcsZeroStreak < 4)
			return;
		g_rcsZeroStreak = 0;
		g_estPrevShots = 0;
		g_rcsLastShots = 0;
		g_rcsAccumMouseX = g_rcsAccumMouseY = 0.0;
		RcsSmoothReset();
		char ps = '?';
		float px = 0.f;
		float py = 0.f;
		if (ReadBestAimPunch(local_pawn, &px, &py, &ps)) {
			g_oldPx = px;
			g_oldPy = py;
			g_lastPunchSrc = ps;
		}
		return;
	}
	g_rcsZeroStreak = 0;

	if (!lmb || !rcs_may_run) {
		g_estPrevShots = shots;
		g_rcsLastShots = shots;
		
		g_rcsAccumMouseX = g_rcsAccumMouseY = 0.0;
		RcsSmoothReset();
		char ps = '?';
		float px = 0.f;
		float py = 0.f;
		if (ReadBestAimPunch(local_pawn, &px, &py, &ps)) {
			g_oldPx = px;
			g_oldPy = py;
			g_lastPunchSrc = ps;
		}
		return;
	}

	const int after = (std::max)(0, cfg.rcs_after_bullet);
	if (shots <= after) {
		char ps = '?';
		float px = 0.f;
		float py = 0.f;
		if (ReadBestAimPunch(local_pawn, &px, &py, &ps)) {
			g_oldPx = px;
			g_oldPy = py;
			g_lastPunchSrc = ps;
		}
		g_estPrevShots = shots;
		g_rcsLastShots = shots;
		g_holdDown = 0.0;
		g_rcsAccumMouseX = g_rcsAccumMouseY = 0.0;
		RcsSmoothReset();
		return;
	}

	if (shots < g_rcsLastShots && g_rcsLastShots > 0) {
		char ps = '?';
		float px = 0.f;
		float py = 0.f;
		if (ReadBestAimPunch(local_pawn, &px, &py, &ps)) {
			g_oldPx = px;
			g_oldPy = py;
			g_lastPunchSrc = ps;
		}
		g_rcsLastShots = shots;
		g_estPrevShots = shots;
		g_holdDown = 0.0;
		g_rcsAccumMouseX = g_rcsAccumMouseY = 0.0;
		RcsSmoothReset();
		return;
	}
	const ULONGLONG nowTick = GetTickCount64();
	const bool shotAdvanced = shots > g_rcsLastShots;
	if (shotAdvanced)
		g_rcsLastShotTick = nowTick;
	g_rcsLastShots = shots;

	char punchSrc = '?';
	float apx = 0.f;
	float apy = 0.f;
	const bool havePunch = ReadBestAimPunch(local_pawn, &apx, &apy, &punchSrc);

	constexpr ULONGLONG kSprayIdleMs = 140;
	if (nowTick - g_rcsLastShotTick > kSprayIdleMs) {
		g_holdDown = 0.0;
		g_rcsAccumMouseX = g_rcsAccumMouseY = 0.0;
		RcsSmoothReset();
		if (havePunch) {
			g_oldPx = apx;
			g_oldPy = apy;
			g_lastPunchSrc = punchSrc;
		}
		g_estPrevShots = shots;
		return;
	}

	if (havePunch) {
		if (ananbaban_merge) {
			g_rcsAccumMouseX = g_rcsAccumMouseY = 0.0;
			RcsSmoothReset();
			g_oldPx = apx;
			g_oldPy = apy;
			g_lastPunchSrc = punchSrc;
			g_estPrevShots = shots;
			return;
		}
		ApplyPunchDeltaRcs(cfg, apx, apy, punchSrc, aim_blend_mult, shotAdvanced);
		g_estPrevShots = shots;
		return;
	}

	if (shots < g_estPrevShots) {
		g_estPrevShots = shots;
		return;
	}

	if (shots == g_estPrevShots)
		return;

	const WpnKind wk = ClassifyWeapon(WeaponRuntime::ActiveWeaponKey());
	if (wk == WpnKind::None) {
		g_estPrevShots = shots;
		return;
	}

	const float sens = (std::max)(ReadGameSensitivity(), 0.01f);
	const float strength = cfg.rcs_scale_pct / 100.f * cfg.rcs_sens_mult * aim_blend_mult;

	double sumMx = 0.0;
	double sumMy = 0.0;
	for (int s = g_estPrevShots + 1; s <= shots; ++s) {
		const int bulletIdx0 = s - 1;
		if (bulletIdx0 < after)
			continue;
		const int patIdx = bulletIdx0 - after;
		float ps = 0.f;
		float ys = 0.f;
		PatternStep(wk, patIdx, &ps, &ys);
		sumMx += static_cast<double>(ys) * static_cast<double>(strength) /
			(static_cast<double>(sens) * -0.022);
		sumMy += static_cast<double>(ps) * static_cast<double>(strength) /
			(static_cast<double>(sens) * 0.022);
	}
	g_estPrevShots = shots;

	RcsAccumulateSmoothed(sumMx, sumMy, cfg.rcs_smooth);
	RcsEmitAccumulatedMouse();
}

} 
