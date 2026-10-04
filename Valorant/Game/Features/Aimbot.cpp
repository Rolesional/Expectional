#include "Aimbot.h"
#include "../structs.hpp"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <random>
#include "Protection/vxlang_per_tu.hpp"

namespace {
float g_aimAccumX = 0.f, g_aimAccumY = 0.f;
float g_aimFiltX = 0.f, g_aimFiltY = 0.f;
float g_prevHumanX = 0.f, g_prevHumanY = 0.f;
std::random_device g_rd;
std::mt19937 g_gen(g_rd());

float PixelFov(float cx, float cy, const UE4Structs::Vector3& screenPos) {
	const float dx = cx - screenPos.x;
	const float dy = cy - screenPos.y;
	return std::sqrt(dx * dx + dy * dy);
}

/** ananbaban AimControl::Humanize — ekran piksel farkına uygulanır. */
static std::pair<float, float> HumanizeDelta(float targetX, float targetY, int strengthInt) {
	const float strength = static_cast<float>(strengthInt);
	const float humanizationAmount = strength * 2.f / 100.f;
	if (humanizationAmount <= 0.f) {
		g_prevHumanX = targetX;
		g_prevHumanY = targetY;
		return { targetX, targetY };
	}

	std::uniform_real_distribution<float> jitterDist(-10.f, 10.f);
	std::uniform_real_distribution<float> microDist(-10.f, 10.f);
	std::uniform_real_distribution<float> smoothnessDist(0.4f, 10.f);
	std::uniform_real_distribution<float> reactionDist(0.f, 1.f);

	const float movementDistance = std::sqrt(targetX * targetX + targetY * targetY);
	const float microJitterX = microDist(g_gen) * (std::min)(movementDistance * 0.25f, 8.f) * humanizationAmount;
	const float microJitterY = microDist(g_gen) * (std::min)(movementDistance * 0.25f, 8.f) * humanizationAmount;
	const float jitterScale = (std::min)(movementDistance * 0.15f, 12.f) * humanizationAmount;
	const float jitterX = jitterDist(g_gen) * jitterScale;
	const float jitterY = jitterDist(g_gen) * jitterScale;
	const float perpX = -targetY * 0.35f * jitterDist(g_gen) * humanizationAmount;
	const float perpY = targetX * 0.35f * jitterDist(g_gen) * humanizationAmount;

	const float baseSmoothFactor = smoothnessDist(g_gen);
	const float smoothFactor = 1.f - ((1.f - baseSmoothFactor) * humanizationAmount);
	float smoothedX = targetX * smoothFactor + g_prevHumanX * (1.f - smoothFactor);
	float smoothedY = targetY * smoothFactor + g_prevHumanY * (1.f - smoothFactor);

	if (reactionDist(g_gen) < 0.15f * humanizationAmount) {
		smoothedX = g_prevHumanX;
		smoothedY = g_prevHumanY;
	}

	g_prevHumanX = targetX;
	g_prevHumanY = targetY;

	return {
		smoothedX + microJitterX + jitterX + perpX,
		smoothedY + microJitterY + jitterY + perpY
	};
}
} // namespace

namespace AimControl {

void ApplyAnanbabanRcsToRelative(UE4Structs::Vector3& rel, float punchX, float punchY,
	float scaleX, float scaleY) {
	const float distXY = std::sqrt(rel.x * rel.x + rel.y * rel.y);
	if (distXY < 1e-5f)
		return;
	const float radX = punchX * scaleX / 360.f * static_cast<float>(M_PI);
	const float sinX = sinf(radX);
	const float cosX = cosf(radX);
	const float z = rel.z * cosX + distXY * sinX;
	const float d = (distXY * cosX - rel.z * sinX) / distXY;
	const float radY = -punchY * scaleY / 360.f * static_cast<float>(M_PI);
	const float sinY = sinf(radY);
	const float cosY = cosf(radY);
	const float x = (rel.x * cosY - rel.y * sinY) * d;
	const float y = (rel.x * sinY + rel.y * cosY) * d;
	rel.x = x;
	rel.y = y;
	rel.z = z;
}

void ResetSmoothState() {
	g_aimAccumX = g_aimAccumY = 0.f;
	g_aimFiltX = g_aimFiltY = 0.f;
	g_prevHumanX = g_prevHumanY = 0.f;
}

void ConsiderTargetScreen(float screenCx, float screenCy, const UE4Structs::Vector3& aimScreen,
	float aimFovLimit, float& bestFov, UE4Structs::Vector3& bestScreen, bool& haveBest,
	const LegitCombatSettings& cfg) {
	const float cand = PixelFov(screenCx, screenCy, aimScreen);
	const float dead = cfg.aim_fov_min;
	if (cand <= dead)
		return;
	if (cand < aimFovLimit && cand < bestFov) {
		bestFov = cand;
		bestScreen = aimScreen;
		haveBest = true;
	}
}

bool RunLegitMouse(float screenCx, float screenCy, bool aimKeyHeld, bool haveBest,
	const UE4Structs::Vector3& bestAimScreen, const LegitCombatSettings& cfg,
	int shots_fired, bool spray_rcs_coop, bool rcs_ananbaban_merge) {
	static int s_prevShots = 0;
	static ULONGLONG s_lastShotInc = 0;
	static bool s_holdView = false;

	if (!aimKeyHeld) {
		ResetSmoothState();
		s_holdView = false;
		s_prevShots = 0;
		s_lastShotInc = 0;
		return false;
	}

	const ULONGLONG nowHold = GetTickCount64();
	if (shots_fired > s_prevShots)
		s_lastShotInc = nowHold;
	if (cfg.aimbot && cfg.rcs_enabled) {
		const int afterHold = (std::max)(0, cfg.rcs_after_bullet);
		if (s_prevShots > afterHold && shots_fired < s_prevShots)
			s_holdView = true;
		if (s_prevShots > afterHold && shots_fired <= s_prevShots && s_lastShotInc != 0
			&& nowHold - s_lastShotInc > 130)
			s_holdView = true;
		if (shots_fired > s_prevShots && shots_fired > afterHold)
			s_holdView = false;
	}
	s_prevShots = shots_fired;
	if (s_holdView) {
		ResetSmoothState();
		return false;
	}

	UE4Structs::Vector3 aimScreen = bestAimScreen;

	if (!cfg.aimbot || !haveBest) {
		g_aimFiltX *= 0.55f;
		g_aimFiltY *= 0.55f;
		g_aimAccumX *= 0.55f;
		g_aimAccumY *= 0.55f;
		return false;
	}

	const int aimDelay = cfg.aim_delay_ms;
	static ULONGLONG s_lastAimTick = 0;
	if (aimDelay > 0) {
		const ULONGLONG now = GetTickCount64();
		if (now - s_lastAimTick < static_cast<ULONGLONG>(aimDelay))
			return false;
	}

	const int after = (std::max)(0, cfg.rcs_after_bullet);
	float spray_t = 0.f;
	if (!rcs_ananbaban_merge && spray_rcs_coop && shots_fired > after)
		spray_t = std::clamp(static_cast<float>(shots_fired - after) / 22.f, 0.f, 1.f);

	float dx = aimScreen.x - screenCx;
	/** CS2 ekran Y asagi artar; +dy hedefi asagi indirir (kafa ustunde kalma). */
	float aimTargetY = aimScreen.y;
	/** Sıkma sırasında nişanı bilerek aşağı kaydırma. RCS zaten aşağı çekiyor. */
	if (rcs_ananbaban_merge && cfg.aimbot && !cfg.rcs_standalone && shots_fired <= after)
		aimTargetY += 9.f;
	float dy = aimTargetY - screenCy;
	if (cfg.rcs_enabled && shots_fired > after && dy > 0.f)
		dy = 0.f;

	if (cfg.aim_humanize && cfg.aim_humanize_strength > 0) {
		int hs = (std::min)(20, (std::max)(0, cfg.aim_humanize_strength));
		if (spray_t > 0.f)
			hs = (std::max)(0, static_cast<int>(static_cast<float>(hs) * (1.f - 0.5f * spray_t)));
		const auto h = HumanizeDelta(dx, dy, hs);
		dx = h.first;
		dy = h.second;
		if (cfg.rcs_enabled && shots_fired > after && dy > 0.f)
			dy = 0.f;
	}

	/**
	 * ananbaban / Catalyst tarzi spray: hedef nokta aim ile secilir, dikey recoil RCS (punch) toplar.
	 * Dikeyde hitbox'a kitlenmek RCS ile carpisir — spray'de Y takibini kapat, sadece X takip.
	 */
	if (spray_t > 0.f) {
		const float vyKeep = 1.f - spray_t;
		dy *= vyKeep;
		g_aimFiltY *= (std::max)(0.12f, vyKeep);
	}

	float s = std::clamp(cfg.smooth, 1.f, 120.f);
	if (spray_t > 0.f)
		s *= (1.f + 0.45f * spray_t);

	const float filtA = std::clamp(14.f / s, 0.05f, 0.38f);
	g_aimFiltX = g_aimFiltX * (1.f - filtA) + dx * filtA;
	g_aimFiltY = g_aimFiltY * (1.f - filtA) + dy * filtA;
	dx = g_aimFiltX;
	dy = g_aimFiltY;
	const float dist = std::sqrt(dx * dx + dy * dy);
	float minMovePx = 0.4f;
	if (spray_t > 0.f)
		minMovePx += spray_t * 1.2f;
	if (dist < minMovePx)
		return false;

	constexpr float refPx = 200.f;
	const float ramp = std::clamp(std::pow(dist / refPx, 0.55f), 0.18f, 1.f);
	const float k = (1.f / s) * ramp;
	float stepX = dx * k;
	float stepY = dy * k;
	float cap = fmaxf(4.f, dist * 0.35f);
	if (spray_t > 0.f)
		cap *= (1.f - 0.18f * spray_t);
	const float mag = std::sqrt(stepX * stepX + stepY * stepY);
	if (mag > cap && mag > 1e-4f) {
		const float sc = cap / mag;
		stepX *= sc;
		stepY *= sc;
	}
	g_aimAccumX += stepX;
	g_aimAccumY += stepY;
	const LONG mx = static_cast<LONG>(truncf(g_aimAccumX));
	const LONG my = static_cast<LONG>(truncf(g_aimAccumY));
	g_aimAccumX -= static_cast<float>(mx);
	g_aimAccumY -= static_cast<float>(my);
	if (mx != 0 || my != 0) {
		mouse_event(MOUSEEVENTF_MOVE, mx, my, 0, 0);
		if (aimDelay > 0)
			s_lastAimTick = GetTickCount64();
		return true;
	}
	return false;
}

} // namespace AimControl
