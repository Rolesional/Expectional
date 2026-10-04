#pragma once

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

inline bool NeedsResponsiveOverlayIo() noexcept
{
	return Settings::Visuals::worldGrenades;
}

inline unsigned OverlayTargetHz() noexcept
{
	if (!Settings::misc::overlayCustomFps)
		return 0u;
	const int v = Settings::misc::overlayCustomFpsValue;
	return static_cast<unsigned>(v < 60 ? 60 : v);
}

inline bool RunEspWorldScanThisFrame() noexcept
{
	(void)SaveFpsGameplay();
	return false;
}

inline bool RunGrenadeHelperThisFrame() noexcept
{
	return false;
}

inline bool RunWorldGrenadeOverlayThisFrame() noexcept
{
	return Settings::Visuals::worldGrenades || Settings::Visuals::worldInfernoHull;
}

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
	
	if (Settings::misc::radarWindow)
		return true;
	if (!SaveFpsGameplay())
		return true;
	return ex_misc::EveryN(3u);
}

} 
