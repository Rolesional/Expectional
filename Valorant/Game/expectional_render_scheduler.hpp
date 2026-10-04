#pragma once
/**
 * save_fps: maliyet yalnizca KAPALI/AGIR opsiyonel ozelliklere (acilinca ucuz).
 * Oyuncu ESP (box/bones/health...) her kare tam hiz — throttle yok.
 */

#include "catalyst_world_bvh.hpp"
#include "expectional_misc_runtime.hpp"
#include "globals.hpp"

namespace ex_sched {

inline bool SaveFpsGameplay() noexcept
{
	return Settings::misc::save_fps && !Settings::bMenu;
}

inline int HeavyWorldScanFeatureCount() noexcept
{
	int n = 0;
	if (Settings::Visuals::worldGrenades || Settings::Visuals::worldInfernoHull)
		++n;
	if (Settings::Visuals::bombWorldEsp)
		++n;
	if (Settings::Visuals::droppedWeaponEsp)
		++n;
	return n;
}

/** espLoop dunya taramasi: smoke/molly etiketleri (inferno hull ayri DrawWorldGrenadeEsp). */
inline bool NeedsResponsiveOverlayIo() noexcept
{
	return Settings::Visuals::worldGrenades;
}

/** 0 = overlay sinirsiz; misc.overlayCustomFps aciksa min 60 Hz ust sinir. */
inline unsigned OverlayTargetHz() noexcept
{
	if (!Settings::misc::overlayCustomFps)
		return 0u;
	const int v = Settings::misc::overlayCustomFpsValue;
	return static_cast<unsigned>(v < 60 ? 60 : v);
}

/** Legacy: dunya IOCTL artik cacheGame'de; espLoop her kare tarama yapmaz. */
inline bool RunEspWorldScanThisFrame() noexcept
{
	(void)SaveFpsGameplay();
	return false;
}

/** Legacy: grenade trajectory kaldirildi. */
inline bool RunGrenadeHelperThisFrame() noexcept
{
	return false;
}

/** Dunya nade + molotov hull: her overlay karesi (git-gel onleme). IOCTL tarama ayri seyrek. */
inline bool RunWorldGrenadeOverlayThisFrame() noexcept
{
	return Settings::Visuals::worldGrenades || Settings::Visuals::worldInfernoHull;
}

/** Lineup HUD + aim marker her kare (map poll ayri seyrek). */
inline bool RunGrenadeLineupsThisFrame() noexcept
{
	return Settings::Visuals::grenadeLineups;
}

inline bool RunGrenadeLineupMapPollThisFrame() noexcept
{
	if (!Settings::Visuals::grenadeLineups)
		return false;
	if (!SaveFpsGameplay())
		return true;
	return ex_misc::EveryN(4u);
}

/** BVH: mesh tuketici acikken; yuklu iken tekrar parse etme (grenade lag onleyici). */
inline bool RunBvhWorldParseThisFrame(bool need_bvh) noexcept
{
	if (!need_bvh)
		return false;
	if (ex_world_bvh::g_world_bvh.valid())
		return false;
	if (!SaveFpsGameplay())
		return true;
	return ex_misc::EveryN(3u);
}

inline bool RunRadarBlipRefreshThisFrame() noexcept
{
	/** Radar penceresi acikken blip konumu her kare taze (git-gel onleme). */
	if (Settings::misc::radarWindow)
		return true;
	if (!SaveFpsGameplay())
		return true;
	return ex_misc::EveryN(3u);
}

} // namespace ex_sched
