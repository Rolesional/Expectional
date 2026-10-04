#include "expectional_frame_cache.hpp"
#include "../Overlay/render.hpp"

void ExpectionalFrameCache::SyncScreenGlobals() noexcept
{
	Width = static_cast<ULONG>(sw);
	Height = static_cast<ULONG>(sh);
	ScreenCenterX = center_x;
	ScreenCenterY = center_y;
}
