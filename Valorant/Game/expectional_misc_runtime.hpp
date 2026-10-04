#pragma once

#include <Windows.h>
#include "../game/globals.hpp"

namespace ex_misc {

enum PollSlot : int {
	HitFeedback = 0,
	BombTimer = 1,
	Watermark = 2,
	RadarWorldName = 3,
	VoteKick = 4,
	FaceitNotify = 5,
	RadarBlipRefresh = 6,
};

inline unsigned& RenderFrame() noexcept
{
	static unsigned s_frame = 0;
	return s_frame;
}

inline void BeginRenderFrame() noexcept
{
	++RenderFrame();
}

inline bool EveryN(unsigned n) noexcept
{
	if (n <= 1u)
		return true;
	return (RenderFrame() % n) == 0u;
}

inline bool PollMs(DWORD interval_ms, PollSlot slot) noexcept
{
	static DWORD s_last[8]{};
	const int i = static_cast<int>(slot) & 7;
	const DWORD now = GetTickCount();
	if (now - s_last[i] < interval_ms)
		return false;
	s_last[i] = now;
	return true;
}

inline DWORD FastPollMs() noexcept
{
	return Settings::misc::save_fps ? 150u : 66u;
}

inline DWORD MediumPollMs() noexcept
{
	return Settings::misc::save_fps ? 280u : 100u;
}

inline DWORD SlowPollMs() noexcept
{
	return Settings::misc::save_fps ? 520u : 250u;
}

} 
