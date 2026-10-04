#pragma once

struct ImDrawList;
struct ImVec2;

/** Eskisurum: GitHub PNG + D3D9 texture (standalone EXE overlay). */
void WindowRadarD3d9_Tick(const char* map_id_utf8, const char* world_name_utf8);
void WindowRadarD3d9_DrawUnderBlips(ImDrawList* dl, const ImVec2& rmin, const ImVec2& rmax, const ImVec2& uv0,
	const ImVec2& uv1, bool rotate_with_view, float view_yaw_deg, float map_w, float map_h);
void WindowRadarD3d9_Shutdown();
bool WindowRadarD3d9_IsReady();
const char* WindowRadarD3d9_LastStatus();
