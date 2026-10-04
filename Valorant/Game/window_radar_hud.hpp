#pragma once

struct ImDrawList;
struct ImVec2;

namespace external_hud_radar {

bool TickFromMemory();

bool TryDrawFrame(ImDrawList* dl, const ImVec2& rmin, const ImVec2& rmax, float mapW, float mapH, bool showDebugHud);

} 
