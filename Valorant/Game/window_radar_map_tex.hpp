#pragma once

struct ImDrawList;
struct ImVec2;

bool WindowRadarMapTex_Tick(const char* panorama_map_id_utf8);
bool WindowRadarMapTex_IsReady();
void WindowRadarMapTex_DrawUnderBlips(ImDrawList* dl, const ImVec2& rmin, const ImVec2& rmax, const ImVec2& uv0,
	const ImVec2& uv1, bool rotate_with_view, float view_yaw_deg, float map_w, float map_h);
void WindowRadarMapTex_Shutdown();

const char* WindowRadarMapTex_LastStatus();

bool WindowRadarMapTex_QueryOverview(const char* panorama_map_id_utf8, double& out_pos_x, double& out_pos_y,
	double& out_scale);
bool WindowRadarMapTex_IsParsingMap();
