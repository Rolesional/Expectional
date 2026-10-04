#include "window_radar.hpp"
#include "window_radar_hud.hpp"
#include "window_radar_map_tex.hpp"
#include "window_radar_d3d9.hpp"
#include "../AnanbabanOverlay/expectional_ananbaban_overlay.hpp"
#include <string>
#include "globals.hpp"
#include "expectional_misc_runtime.hpp"
#include "../OSImGui/shade_imgui_settings.h"
#include "../OSImGui/os_imgui_menu.hpp"
#include "offsets_runtime.hpp"
#include "../Driver/driver.hpp"
#include "structs.hpp"

#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "../../Includes/Imgui/imgui.h"
#include "Protection/vxlang_per_tu.hpp"

struct MapRadarDef {
	const char* map_id;
	double pos_x;
	double pos_y;
	double scale;
};

static const MapRadarDef kMapTable[] = {
	{ "cs_office", -1838, 1858, 4.1 },
	{ "cs_italy", -2647, 2592, 4.6 },
	{ "de_ancient", -2953, 2164, 5.0 },
	{ "de_anubis", -2796, 3328, 5.22 },
	{ "de_cache", -2020, 2390, 5.54 },
	{ "de_dust2", -2476, 3239, 4.4 },
	{ "de_inferno", -2087, 3870, 4.9 },
	{ "de_mills", -4810, -320, 5.148 },
	{ "de_mirage", -3230, 1713, 5.0 },
	{ "de_nuke", -3453, 2887, 7.0 },
	{ "de_overpass", -4831, 1781, 5.2 },
	{ "de_thera", -85.609764, 2261.8025, 4.85 },
	{ "de_train", -2308, 2078, 4.082077 },
	{ "de_vertigo", -3168, 1762, 4.0 },
	
};

static void NormalizeRadarWorldName(std::string& s) {
	while (!s.empty() && (unsigned char)s.back() <= ' ')
		s.pop_back();
	while (!s.empty() && (unsigned char)s.front() <= ' ')
		s.erase(0, 1);
	const size_t slash = s.find_last_of("\\/");
	if (slash != std::string::npos && slash + 1 < s.size())
		s.erase(0, slash + 1);
	if (s.size() > 4 && s.compare(s.size() - 4, 4, ".bsp") == 0)
		s.resize(s.size() - 4);
}

static std::string ReadWorldNameForWindowRadar() {
	const uintptr_t eng = g_GameMem.engine_address();
	if (!eng || !offsets::dwNetworkGameClient)
		return "<empty>";
	const uintptr_t pNgc = g_GameMem.readv<uintptr_t>(eng + static_cast<uintptr_t>(offsets::dwNetworkGameClient));
	if (!pNgc || pNgc < 0x10000ULL)
		return "<empty>";
	constexpr uintptr_t kMapName = 0x218;
	constexpr uintptr_t kMapPath = 0x210;
	uintptr_t pStr = g_GameMem.readv<uintptr_t>(pNgc + kMapName);
	std::string raw = pStr ? g_GameMem.ReadString(pStr, 160) : std::string{};
	if (raw.empty()) {
		pStr = g_GameMem.readv<uintptr_t>(pNgc + kMapPath);
		raw = pStr ? g_GameMem.ReadString(pStr, 260) : std::string{};
	}
	NormalizeRadarWorldName(raw);
	if (raw.empty())
		return "<empty>";
	return raw;
}

static bool RadarKeySuffixAllDigits(const std::string& s) {
	if (s.empty())
		return false;
	for (unsigned char c : s) {
		if (!std::isdigit(c))
			return false;
	}
	return true;
}

static const MapRadarDef* LookupMapDef(const std::string& world) {
	if (world.empty() || world == "<empty>" || world.find("empty") != std::string::npos)
		return nullptr;
	std::string probe = world;
	for (;;) {
		for (const MapRadarDef& m : kMapTable) {
			if (_stricmp(m.map_id, probe.c_str()) == 0)
				return &m;
		}
		const size_t u = probe.rfind('_');
		if (u == std::string::npos || u < 3u)
			return nullptr;
		const std::string tail = probe.substr(u + 1);
		if (!RadarKeySuffixAllDigits(tail))
			return nullptr;
		probe.resize(u);
	}
}

static UE4Structs::Vector3 ReadEntityWorldPos(uintptr_t entity) {
	if (!entity)
		return {};
	if (offsets::m_pGameSceneNode && offsets::m_vecAbsOrigin) {
		const uintptr_t sn = g_GameMem.readv<uintptr_t>(entity + static_cast<uintptr_t>(offsets::m_pGameSceneNode));
		if (sn)
			return g_GameMem.readv<UE4Structs::Vector3>(sn + static_cast<uintptr_t>(offsets::m_vecAbsOrigin));
	}
	if (offsets::m_vecOrigin)
		return g_GameMem.readv<UE4Structs::Vector3>(entity + static_cast<uintptr_t>(offsets::m_vecOrigin));
	return {};
}

static float ReadPawnEyeYawDegrees(uintptr_t pawn) {
	if (!pawn || !offsets::m_angEyeAngles)
		return 0.f;
	return g_GameMem.readv<float>(pawn + static_cast<uintptr_t>(offsets::m_angEyeAngles) + 4);
}

static void WorldToMapPercent(const MapRadarDef& m, const UE4Structs::Vector3& p, float& outPctX, float& outPctY) {
	const double mapSize = m.scale * 1024.0;
	outPctX = static_cast<float>((p.x - m.pos_x) * 100.0 / mapSize);
	outPctY = static_cast<float>((p.y - m.pos_y) * 100.0 / -mapSize);
}

static ImVec2 WorldToRadarDragonBurn(const UE4Structs::Vector3& localWorld, float localYawDeg,
	const UE4Structs::Vector3& targetWorld, float centerX, float centerY, float renderRangePx, float proportionWorld) {
	const float dx = localWorld.x - targetWorld.x;
	const float dy = localWorld.y - targetWorld.y;
	float dist = sqrtf(dx * dx + dy * dy);
	const float angleRad =
		(localYawDeg * (3.14159265f / 180.f)) - atan2f(targetWorld.y - localWorld.y, targetWorld.x - localWorld.x);
	const float scale = (2.f * renderRangePx) / (std::max)(proportionWorld, 1.f);
	dist *= scale;
	ImVec2 out;
	out.x = centerX + dist * sinf(angleRad);
	out.y = centerY - dist * cosf(angleRad);
	return out;
}

static ImVec2 RadarRotScreenPt(const ImVec2& p, const ImVec2& ctr, float yawDeg, float mapW, float mapH) {
	if (fabsf(yawDeg) < 0.02f)
		return p;
	const float rad = yawDeg * (3.14159265f / 180.f);
	const float dx = p.x - ctr.x, dy = p.y - ctr.y;
	
	const float denom = 0.5f * (std::min)(mapW, mapH);
	if (denom < 1e-3f)
		return p;
	const float nx = dx / denom;
	const float ny = dy / denom;
	const float c = cosf(rad), s = sinf(rad);
	const float rx = nx * c - ny * s;
	const float ry = nx * s + ny * c;
	return ImVec2(ctr.x + rx * (mapW * 0.5f), ctr.y + ry * (mapH * 0.5f));
}

static ImU32 RadarRgbF(const float* rgb, int a = 255) {
	return IM_COL32(
		(int)(std::clamp(rgb[0], 0.f, 1.f) * 255.f),
		(int)(std::clamp(rgb[1], 0.f, 1.f) * 255.f),
		(int)(std::clamp(rgb[2], 0.f, 1.f) * 255.f), a);
}

static ImVec2 RadarPercentToScreen(float pxPct, float pyPct, const ImVec2& uv0, const ImVec2& uv1, const ImVec2& rmin,
	float mapW, float mapH) {
	const float du = uv1.x - uv0.x;
	const float dv = uv1.y - uv0.y;
	const float duSafe = (std::max)(1e-6f, du);
	const float dvSafe = (std::max)(1e-6f, dv);
	const float su = (pxPct / 100.f - uv0.x) / duSafe;
	const float sv = (pyPct / 100.f - uv0.y) / dvSafe;
	return ImVec2(rmin.x + mapW * su, rmin.y + mapH * sv);
}

static void RadarDrawBlip(ImDrawList* dl, float pxPct, float pyPct, const ImVec2& uv0, const ImVec2& uv1, const ImVec2& rmin,
	float mapW, float mapH, ImU32 col, float r, bool ring, bool rotMap, const ImVec2& rotCtr, float rotYawDeg) {
	ImVec2 c = RadarPercentToScreen(pxPct, pyPct, uv0, uv1, rmin, mapW, mapH);
	if (rotMap)
		c = RadarRotScreenPt(c, rotCtr, rotYawDeg, mapW, mapH);
	dl->AddCircleFilled(c, r, col, 14);
	if (ring)
		dl->AddCircle(c, r + 1.75f, IM_COL32(0, 0, 0, 210), 14);
}

static void RadarDrawFacing(ImDrawList* dl, const MapRadarDef& def, uintptr_t pawn, float pxPct, float pyPct,
	const ImVec2& uv0, const ImVec2& uv1, const ImVec2& rmin, float mapW, float mapH, ImU32 lineCol, float lineLenWorld,
	float blipScreenR, bool rotMap, const ImVec2& rotCtr, float rotYawDeg) {
	if (!pawn || !offsets::m_angEyeAngles || lineLenWorld < 1.f)
		return;
	const float yawDeg = ReadPawnEyeYawDegrees(pawn);
	const float rad = yawDeg * (3.14159265f / 180.f);
	const float dx = cosf(rad) * lineLenWorld;
	const float dy = sinf(rad) * lineLenWorld;
	const UE4Structs::Vector3 base = ReadEntityWorldPos(pawn);
	UE4Structs::Vector3 tip = base;
	tip.x += dx;
	tip.y += dy;
	float qx = 0.f, qy = 0.f;
	WorldToMapPercent(def, tip, qx, qy);
	ImVec2 c = RadarPercentToScreen(pxPct, pyPct, uv0, uv1, rmin, mapW, mapH);
	ImVec2 tipScr = RadarPercentToScreen(qx, qy, uv0, uv1, rmin, mapW, mapH);
	if (rotMap) {
		c = RadarRotScreenPt(c, rotCtr, rotYawDeg, mapW, mapH);
		tipScr = RadarRotScreenPt(tipScr, rotCtr, rotYawDeg, mapW, mapH);
	}
	float ex = tipScr.x - c.x;
	float ey = tipScr.y - c.y;
	const float elen = sqrtf(ex * ex + ey * ey);
	if (elen < 0.5f)
		return;
	ex /= elen;
	ey /= elen;
	const float r = blipScreenR;
	const float tipLen = (std::max)(4.f, r * 1.05f);
	const float halfW = (std::max)(2.8f, r * 0.72f);
	const ImVec2 F = ImVec2(c.x + ex * r, c.y + ey * r);
	const ImVec2 T = ImVec2(F.x + ex * tipLen, F.y + ey * tipLen);
	const float ox = -ey;
	const float oy = ex;
	const ImVec2 L = ImVec2(F.x + ox * halfW, F.y + oy * halfW);
	const ImVec2 R = ImVec2(F.x - ox * halfW, F.y - oy * halfW);
	dl->AddTriangleFilled(T, L, R, lineCol);
	dl->AddTriangle(T, L, R, IM_COL32(0, 0, 0, 115), 1.f);
}

static void ComputeRadarMapBgUv(const MapRadarDef* def, bool haveLocalWorld, bool wantFollow, float followZoomF,
	const UE4Structs::Vector3& localWorld, ImVec2& outUv0, ImVec2& outUv1) {
	outUv0 = ImVec2(0.f, 0.f);
	outUv1 = ImVec2(1.f, 1.f);
	
	if (!def || !wantFollow || !haveLocalWorld)
		return;
	UE4Structs::Vector3 localForRadar = localWorld;
	if (global_pawn)
		localForRadar = ReadEntityWorldPos(global_pawn);
	float lx = 50.f, ly = 50.f;
	WorldToMapPercent(*def, localForRadar, lx, ly);
	const float followZoomEff = (std::max)(1.05f, followZoomF);
	const float half = 0.5f / followZoomEff;
	outUv0.x = (lx / 100.f) - half;
	outUv1.x = (lx / 100.f) + half;
	outUv0.y = (ly / 100.f) - half;
	outUv1.y = (ly / 100.f) + half;
}

static ImVec2 WorldPlanarToScreenXY(const UE4Structs::Vector3& originWorld, const UE4Structs::Vector3& p,
	float rminx, float rminy, float mapW, float mapH, float halfSpanWorld, bool rotMap, float viewYawDeg) {
	const float hs = (std::max)(halfSpanWorld, 50.f);
	const float cx = rminx + mapW * 0.5f;
	const float cy = rminy + mapH * 0.5f;
	const float renderR = 0.5f * (std::max)(1.f, (std::min)(mapW, mapH));
	const float proportion = 2.f * hs;
	const float localYaw = rotMap ? viewYawDeg : 0.f;
	return WorldToRadarDragonBurn(originWorld, localYaw, p, cx, cy, renderR, proportion);
}

static void RadarDrawBlipAt(ImDrawList* dl, ImVec2 c, ImU32 col, float r, bool ring) {
	dl->AddCircleFilled(c, r, col, 14);
	if (ring)
		dl->AddCircle(c, r + 1.75f, IM_COL32(0, 0, 0, 210), 14);
}

static void RadarDrawFacingPlanar(ImDrawList* dl, uintptr_t pawn, const UE4Structs::Vector3& baseWorld,
	const ImVec2& baseScr, float lineLenWorld, float blipScreenR, bool rotMap, float viewYawDeg,
	const UE4Structs::Vector3& originWorld, float rminx, float rminy, float mapW, float mapH, float halfSpanWorld,
	ImU32 lineCol) {
	if (!pawn || !offsets::m_angEyeAngles || lineLenWorld < 1.f)
		return;
	const float yawDeg = ReadPawnEyeYawDegrees(pawn);
	const float rad = yawDeg * (3.14159265f / 180.f);
	const float dwx = cosf(rad) * lineLenWorld;
	const float dwy = sinf(rad) * lineLenWorld;
	UE4Structs::Vector3 tip = baseWorld;
	tip.x += dwx;
	tip.y += dwy;
	ImVec2 tipScr =
		WorldPlanarToScreenXY(originWorld, tip, rminx, rminy, mapW, mapH, halfSpanWorld, rotMap, viewYawDeg);
	float ex = tipScr.x - baseScr.x;
	float ey = tipScr.y - baseScr.y;
	const float elen = sqrtf(ex * ex + ey * ey);
	if (elen < 0.5f)
		return;
	ex /= elen;
	ey /= elen;
	const float r = blipScreenR;
	const float tipLen = (std::max)(4.f, r * 1.05f);
	const float halfW = (std::max)(2.8f, r * 0.72f);
	const ImVec2 F = ImVec2(baseScr.x + ex * r, baseScr.y + ey * r);
	const ImVec2 T = ImVec2(F.x + ex * tipLen, F.y + ey * tipLen);
	const float ox = -ey;
	const float oy = ex;
	const ImVec2 L = ImVec2(F.x + ox * halfW, F.y + oy * halfW);
	const ImVec2 R = ImVec2(F.x - ox * halfW, F.y - oy * halfW);
	dl->AddTriangleFilled(T, L, R, lineCol);
	dl->AddTriangle(T, L, R, IM_COL32(0, 0, 0, 115), 1.f);
}

static bool ReadPlantedBombWorld(UE4Structs::Vector3& out) {
	out = {};
	if (!client || !offsets::dwPlantedC4)
		return false;
	const uintptr_t plantedAddr = client + static_cast<uintptr_t>(offsets::dwPlantedC4);
	const bool planted = g_GameMem.readv<bool>(plantedAddr - 8);
	if (!planted)
		return false;
	uintptr_t bomb = g_GameMem.readv<uintptr_t>(plantedAddr);
	if (bomb)
		bomb = g_GameMem.readv<uintptr_t>(bomb);
	if (!bomb)
		return false;
	if (offsets::c4_m_bBombDefused) {
		if (g_GameMem.readv<bool>(bomb + static_cast<uintptr_t>(offsets::c4_m_bBombDefused)))
			return false;
	}
	out = ReadEntityWorldPos(bomb);
	return true;
}

static bool WindowRadarResolveBombDrawPos(UE4Structs::Vector3& outDraw) {
	static UE4Structs::Vector3 s_smooth{};
	static bool s_have = false;
	static int s_miss = 0;
	static DWORD s_lastGood = 0;

	UE4Structs::Vector3 raw{};
	const bool ok = ReadPlantedBombWorld(raw);
	const DWORD now = GetTickCount();

	if (ok) {
		if (!s_have)
			s_smooth = raw;
		else {
			const float a = 0.38f;
			s_smooth.x += (raw.x - s_smooth.x) * a;
			s_smooth.y += (raw.y - s_smooth.y) * a;
			s_smooth.z += (raw.z - s_smooth.z) * a;
		}
		s_have = true;
		s_miss = 0;
		s_lastGood = now;
		outDraw = s_smooth;
		return true;
	}

	if (s_have) {
		++s_miss;
		const bool holdTime = (now - s_lastGood) < 480u;
		const bool holdMiss = s_miss <= 10;
		if (holdTime && holdMiss) {
			outDraw = s_smooth;
			return true;
		}
		s_have = false;
		s_miss = 0;
	}
	return false;
}

static float EstimatePlanarFollowHalfSpanWorld(const UE4Structs::Vector3& origin) {
	std::vector<UE4Structs::CS2Entity> snapshot;
	{
		std::lock_guard<std::mutex> lk(g_PlayerListMutex);
		snapshot = UE4Structs::PlayerList;
	}
	constexpr float pad = 300.f;
	float m = 580.f;
	for (const UE4Structs::CS2Entity& e : snapshot) {
		if (!e.Actor)
			continue;
		const int hp = g_GameMem.readv<int>(e.Actor + offsets::m_iHealth);
		if (hp <= 0 || hp > 100)
			continue;
		if (offsets::m_bPawnIsAlive && e.Controller) {
			const int alive = g_GameMem.readv<int>(e.Controller + static_cast<uintptr_t>(offsets::m_bPawnIsAlive));
			if (alive != 1)
				continue;
		}
		const bool isLocal = global_pawn && e.Actor == global_pawn;
		const UE4Structs::Vector3 p = ReadEntityWorldPos(e.Actor);
		if (!isLocal && p.length2d() < 8.f && std::fabs(p.z) < 8.f)
			continue;
		const float dx = p.x - origin.x;
		const float dy = p.y - origin.y;
		const float d = sqrtf(dx * dx + dy * dy) + pad;
		m = (std::max)(m, d);
	}
	UE4Structs::Vector3 bombRaw{};
	if (ReadPlantedBombWorld(bombRaw)) {
		const float dx = bombRaw.x - origin.x;
		const float dy = bombRaw.y - origin.y;
		m = (std::max)(m, sqrtf(dx * dx + dy * dy) + pad);
	}
	return (std::min)(m, 4000.f);
}

struct WindowRadarBlipSnap {
	uintptr_t actor = 0;
	uintptr_t controller = 0;
	UE4Structs::Vector3 world{};
	int hp = 0;
	int team = 0;
	bool is_local = false;
};

static void CollectWindowRadarBlips(std::vector<WindowRadarBlipSnap>& out) {
	out.clear();
	std::vector<UE4Structs::CS2Entity> snapshot;
	{
		std::lock_guard<std::mutex> lk(g_PlayerListMutex);
		snapshot = UE4Structs::PlayerList;
	}
	out.reserve(snapshot.size());
	for (const UE4Structs::CS2Entity& e : snapshot) {
		if (!e.Actor)
			continue;
		int hp = g_GameMem.readv<int>(e.Actor + offsets::m_iHealth);
		if (hp <= 0 || hp > 100)
			continue;
		if (offsets::m_bPawnIsAlive && e.Controller) {
			const int alive = g_GameMem.readv<int>(e.Controller + static_cast<uintptr_t>(offsets::m_bPawnIsAlive));
			if (alive != 1)
				continue;
		}
		const bool isLocal = global_pawn && e.Actor == global_pawn;
		UE4Structs::Vector3 ori = ReadEntityWorldPos(e.Actor);
		if (!isLocal && ori.length2d() < 8.f && std::fabs(ori.z) < 8.f)
			continue;
		WindowRadarBlipSnap b{};
		b.actor = e.Actor;
		b.controller = e.Controller;
		b.world = ori;
		b.hp = hp;
		b.team = g_GameMem.readv<int>(e.Actor + offsets::m_iTeamNum) & 0xFF;
		b.is_local = isLocal;
		out.push_back(b);
	}
}

static void RadarWindowAspectConstraint(ImGuiSizeCallbackData* d) {
	const bool* p43 = static_cast<const bool*>(d->UserData);
	const bool is43 = p43 && *p43;
	float x = d->DesiredSize.x;
	float y = d->DesiredSize.y;
	if (is43) {
		constexpr float k43 = 4.f / 3.f;
		
		if (y * k43 > x)
			x = y * k43;
		else
			y = x / k43;
		x = (std::max)(260.f, (std::min)(x, 1280.f));
		y = (std::max)(196.f, (std::min)(y, 960.f));
		if (x > y * k43 + 0.5f)
			x = y * k43;
		else
			y = x / k43;
	} else {
		float s = (std::max)(x, y);
		if (s < 200.f)
			s = 200.f;
		if (s > 920.f)
			s = 920.f;
		x = y = s;
	}
	d->DesiredSize.x = x;
	d->DesiredSize.y = y;
}

static void DbgLogThrottle(const char* msg) {
	if (!Settings::misc::radarWindowDebugLog)
		return;
	static DWORD s_last = 0;
	const DWORD now = GetTickCount();
	if (now - s_last < 2000)
		return;
	s_last = now;
	OutputDebugStringA("[window_radar] ");
	OutputDebugStringA(msg);
	OutputDebugStringA("\n");
}

static void WindowRadarColorSwatch(float* rgb3, const char* caption, const ImVec2& sz) {
	ImGui::BeginGroup();
	ImGui::PushID(caption);
	const ImVec4 v(rgb3[0], rgb3[1], rgb3[2], 1.f);
	if (ImGui::ColorButton("##swatch", v, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, sz))
		ImGui::OpenPopup("##picker");
	if (ImGui::BeginPopup("##picker")) {
		ImGui::SetNextItemWidth(240.f);
		ImGui::ColorPicker3("##cp", rgb3, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview);
		ImGui::EndPopup();
	}
	const float tw = ImGui::CalcTextSize(caption).x;
	const float pad = (std::max)(0.f, (sz.x - tw) * 0.5f);
	if (pad > 0.f)
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
	ImGui::TextUnformatted(caption);
	ImGui::PopID();
	ImGui::EndGroup();
}

void window_radar_render_menu_misc() {
	ImGui::TextUnformatted("Window Radar");
	ImGui::Checkbox("Radar window##winrad", &Settings::misc::radarWindow);
	if (Settings::misc::radarWindowMapPx < 120.f)
		Settings::misc::radarWindowMapPx = 120.f;
	if (Settings::misc::radarWindowMapPx > 900.f)
		Settings::misc::radarWindowMapPx = 900.f;
	if (Settings::misc::radarWindowBlipScale < 0.28f)
		Settings::misc::radarWindowBlipScale = 0.28f;
	if (Settings::misc::radarWindowBlipScale > 1.4f)
		Settings::misc::radarWindowBlipScale = 1.4f;
	ImGui::SliderFloat("Circle Size##rwblip", &Settings::misc::radarWindowBlipScale, 0.28f, 1.4f, "%.2f");
	ImGui::Checkbox("Local Follow + Zoom##rwfollow", &Settings::misc::radarWindowFollowLocal);
	if (Settings::misc::radarWindowFollowLocal) {
		if (Settings::misc::radarWindowFollowZoom < 1.05f)
			Settings::misc::radarWindowFollowZoom = 1.05f;
		if (Settings::misc::radarWindowFollowZoom > 10.f)
			Settings::misc::radarWindowFollowZoom = 10.f;
		ImGui::SliderFloat("Zoom##rwzoom", &Settings::misc::radarWindowFollowZoom, 1.05f, 9.f, "%.2f");
	}
	ImGui::Checkbox("Rotating Radar##rwrot", &Settings::misc::radarWindowRotateWithView);
	ImGui::Checkbox("Hide Map Image##rwhidemap", &Settings::misc::radarWindowHideMapImage);
	ImGui::SliderInt("Background Transparency##rwtransp", &Settings::misc::radarWindowTransparency, 0, 100, "%d%%");
	
	Settings::misc::radarWindowGameMapTex = true;
	Settings::misc::radarWindowHudMatch = false;
	Settings::misc::radarWindowDebugLog = false;
	ImGui::TextUnformatted("Circle Colors");
	const ImVec2 swatch_sz(58.f, 58.f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(18.f, 8.f));
	ImGui::BeginGroup();
	WindowRadarColorSwatch(EspUiColors::radar_team, "Team", swatch_sz);
	ImGui::SameLine();
	WindowRadarColorSwatch(EspUiColors::radar_enemy, "Enemy", swatch_sz);
	ImGui::SameLine();
	WindowRadarColorSwatch(EspUiColors::radar_local, "Local", swatch_sz);
	ImGui::EndGroup();
	ImGui::PopStyleVar();
}

void window_radar_render_frame() {
	if (!Settings::misc::radarWindow) {
		WindowRadarD3d9_Shutdown();
		WindowRadarMapTex_Shutdown();
		return;
	}

	static std::string s_cached_world;
	static DWORD s_world_poll_tick = 0;
	const DWORD nowWorld = GetTickCount();
	if (s_cached_world.empty() || (nowWorld - s_world_poll_tick) > (Settings::misc::save_fps ? 1500u : 800u)) {
		s_cached_world = ReadWorldNameForWindowRadar();
		s_world_poll_tick = nowWorld;
	}
	const std::string& world = s_cached_world;
	const MapRadarDef* def = LookupMapDef(world);

	static MapRadarDef s_runtimeDef{};
	static std::string s_runtimeDefId;
	static bool s_runtimeDefValid = false;
	static std::string s_runtimeWorld;
	static DWORD s_runtimeProbeTick = 0;
	const DWORD nowTickRadar = GetTickCount();
	
	if (!def && !world.empty() && world != "<empty>") {
		if (world != s_runtimeWorld) {
			s_runtimeWorld = world;
			s_runtimeDefValid = false;
			s_runtimeProbeTick = 0;
		}
		
		if (!s_runtimeDefValid && (nowTickRadar - s_runtimeProbeTick) > 750u) {
			s_runtimeProbeTick = nowTickRadar;
			double rt_px = 0.0, rt_py = 0.0, rt_sc = 0.0;
			if (WindowRadarMapTex_QueryOverview(world.c_str(), rt_px, rt_py, rt_sc)) {
				s_runtimeDefId = world;
				s_runtimeDef.map_id = s_runtimeDefId.c_str();
				s_runtimeDef.pos_x = rt_px;
				s_runtimeDef.pos_y = rt_py;
				s_runtimeDef.scale = rt_sc;
				s_runtimeDefValid = true;
			}
		}
		if (s_runtimeDefValid)
			def = &s_runtimeDef;
	} else {
		s_runtimeDefValid = false;
		s_runtimeWorld.clear();
	}

	char dbg[320];
	snprintf(dbg, sizeof dbg, "world=%s map=%s", world.c_str(), def ? def->map_id : "(planar)");
	DbgLogThrottle(dbg);

	const bool showHud = Settings::misc::radarWindowDebugLog ||
		(Settings::misc::radarWindowGameMapTex && def &&
			((g_ExpectionalMainDX11Device && !WindowRadarMapTex_IsReady()) ||
				(!g_ExpectionalMainDX11Device && !WindowRadarD3d9_IsReady())));
	const ImGuiIO& io = ImGui::GetIO();
	const float screenRef = (std::max)(220.f, (std::min)(io.DisplaySize.x, io.DisplaySize.y));
	const float resScale = screenRef / 1080.f;
	const float scaledMapBase = Settings::misc::radarWindowMapPx * resScale;
	const float initSide = (std::max)(160.f, (std::min)(920.f, scaledMapBase));
	const float chrome = 8.f + (showHud ? 48.f : 0.f);
	const bool r43 = Settings::misc::radarWindow43;
	ImVec2 initWin;
	if (r43) {
		
		const float outerH = initSide + chrome;
		const float outerW = outerH * (4.f / 3.f);
		initWin = ImVec2(outerW, outerH);
	} else {
		const float s = initSide + chrome;
		initWin = ImVec2(s, s);
	}
	if (r43)
		ImGui::SetNextWindowSizeConstraints(ImVec2(260.f, 196.f), ImVec2(1280.f, 960.f), RadarWindowAspectConstraint,
			&Settings::misc::radarWindow43);
	else
		ImGui::SetNextWindowSizeConstraints(ImVec2(200.f, 200.f), ImVec2(920.f, 920.f), RadarWindowAspectConstraint,
			&Settings::misc::radarWindow43);
	ImGui::SetNextWindowSize(initWin, ImGuiCond_FirstUseEver);

	ImVec4 bgCol = c::background::filling;
	bgCol.w = static_cast<float>(Settings::misc::radarWindowTransparency) / 100.f;
	ImGui::PushStyleColor(ImGuiCol_WindowBg, bgCol);
	ImGui::PushStyleColor(ImGuiCol_Border, c::background::stroke);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, c::background::rounding * 0.65f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.45f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, showHud ? ImVec2(10.f, 8.f) : ImVec2(6.f, 6.f));

	ExpectionalOsMenu_UiFontScope uiFont;

	const ImGuiWindowFlags kWinFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoCollapse;

	if (ImGui::Begin("Radar window##expectional_winrad", &Settings::misc::radarWindow, kWinFlags)) {
		UE4Structs::Vector3 localWorld{};
		bool haveLocalWorld = false;
		if (global_pawn) {
			localWorld = ReadEntityWorldPos(global_pawn);
			haveLocalWorld = true;
		}

		const float avail = ImGui::GetContentRegionAvail().x;
		const float availY = ImGui::GetContentRegionAvail().y;
		float bw = 0.f, bh = 0.f;
		if (r43) {
			constexpr float k43 = 4.f / 3.f;
			bh = floorf((std::min)(availY, avail / k43) + 0.5f);
			bw = floorf(bh * k43 + 0.5f);
			if (bw > avail + 0.5f) {
				bw = floorf(avail + 0.5f);
				bh = floorf(bw / k43 + 0.5f);
			}
		} else {
			const float side = floorf((std::min)(avail, availY) + 0.5f);
			bw = bh = (std::max)(64.f, side);
		}
		
		static float s_last_bw = 0.f, s_last_bh = 0.f;
		if (s_last_bw > 0.f && std::fabsf(bw - s_last_bw) <= 1.f && std::fabsf(bh - s_last_bh) <= 1.f) {
			bw = s_last_bw;
			bh = s_last_bh;
		} else {
			s_last_bw = bw;
			s_last_bh = bh;
		}

		ImGui::InvisibleButton("##radarmap", ImVec2(avail, availY));
		ExpectionalHudDragFromLastItem("##radarmap_drag");
		const ImVec2 itemMin = ImGui::GetItemRectMin();
		const ImVec2 itemMax = ImGui::GetItemRectMax();
		const float slackX = (itemMax.x - itemMin.x) - bw;
		const float slackY = (itemMax.y - itemMin.y) - bh;
		const ImVec2 rmin(itemMin.x + slackX * 0.5f, itemMin.y + slackY * 0.5f);
		const ImVec2 rmax(rmin.x + bw, rmin.y + bh);
		const float mapW = rmax.x - rmin.x;
		const float mapH = rmax.y - rmin.y;
		const ImVec2 rotCtr((rmin.x + rmax.x) * 0.5f, (rmin.y + rmax.y) * 0.5f);
		ImDrawList* dl = ImGui::GetWindowDrawList();

		const bool wantFollow = Settings::misc::radarWindowFollowLocal && global_pawn && haveLocalWorld;
		const float followZoomF = wantFollow ? (std::max)(1.05f, Settings::misc::radarWindowFollowZoom) : 1.f;
		ImVec2 map_uv0(0.f, 0.f), map_uv1(1.f, 1.f);
		ComputeRadarMapBgUv(def, haveLocalWorld, wantFollow, followZoomF, localWorld, map_uv0, map_uv1);
		{
			std::string mapTexId;
			if (def && def->map_id && def->map_id[0])
				mapTexId = def->map_id;
			else if (!world.empty() && world != "<empty>")
				mapTexId = world;
			const char* texKey = (Settings::misc::radarWindowGameMapTex && !mapTexId.empty()) ? mapTexId.c_str() : nullptr;
			if (g_ExpectionalMainDX11Device && texKey)
				WindowRadarMapTex_Tick(texKey);
			else if (texKey)
				WindowRadarD3d9_Tick(texKey, world.c_str());
		}

		const bool rotMap = Settings::misc::radarWindowRotateWithView && wantFollow && global_pawn;
		
		const float viewYawPlanar = rotMap ? ReadPawnEyeYawDegrees(global_pawn) : 0.f;
		const float mapRef = (mapW + mapH) * 0.5f;
		const float blipScl = (std::max)(0.28f, (std::min)(1.4f, Settings::misc::radarWindowBlipScale));
		const float baseR = 3.2f * (mapRef / 280.f) * (0.45f + 0.55f * followZoomF);
		const float blipR = (std::min)(12.f, (std::max)(2.3f, baseR * blipScl));
		const float lineWorld = 48.f + 58.f * followZoomF;
		const float bombR = blipR * 1.14f;
		
		const float mapWorldUnits = def ? static_cast<float>(def->scale * 1024.0) : 0.f;
		float halfSpanDragon;
		if (mapWorldUnits > 1.f) {
			if (wantFollow)
				halfSpanDragon = mapWorldUnits / (2.f * followZoomF);
			else
				halfSpanDragon = mapWorldUnits * 0.5f;
		} else if (wantFollow && haveLocalWorld) {
			const UE4Structs::Vector3 origin =
				global_pawn ? ReadEntityWorldPos(global_pawn) : localWorld;
			halfSpanDragon = EstimatePlanarFollowHalfSpanWorld(origin) / followZoomF;
		} else {
			halfSpanDragon = 2600.f / (std::max)(1.2f, followZoomF * 0.65f);
		}

		dl->PushClipRect(rmin, rmax, true);
		if (!Settings::misc::radarWindowHideMapImage) {
			if (g_ExpectionalMainDX11Device)
				WindowRadarMapTex_DrawUnderBlips(dl, rmin, rmax, map_uv0, map_uv1, rotMap, viewYawPlanar, mapW, mapH);
			else
				WindowRadarD3d9_DrawUnderBlips(dl, rmin, rmax, map_uv0, map_uv1, rotMap, viewYawPlanar, mapW, mapH);
		}

		const bool drewHudMatch =
			Settings::misc::radarWindowHudMatch && external_hud_radar::TryDrawFrame(dl, rmin, rmax, mapW, mapH, showHud);
		if (!drewHudMatch) {

		std::vector<WindowRadarBlipSnap> radarBlips;
		CollectWindowRadarBlips(radarBlips);
		int localTeamRadar = 0;
		if (global_pawn && offsets::m_iTeamNum)
			localTeamRadar = g_GameMem.readv<int>(global_pawn + static_cast<uintptr_t>(offsets::m_iTeamNum)) & 0xFF;

		if (def && haveLocalWorld) {
			UE4Structs::Vector3 localForRadar = localWorld;
			if (global_pawn && wantFollow)
				localForRadar = ReadEntityWorldPos(global_pawn);

			float lx = 50.f, ly = 50.f;
			WorldToMapPercent(*def, localForRadar, lx, ly);

			ImVec2 uv0(0.f, 0.f), uv1(1.f, 1.f);
			const float followZoomEff = wantFollow ? followZoomF : 1.f;
			if (wantFollow) {
				const float half = 0.5f / followZoomEff;
				uv0.x = (lx / 100.f) - half;
				uv1.x = (lx / 100.f) + half;
				uv0.y = (ly / 100.f) - half;
				uv1.y = (ly / 100.f) + half;
			}

			UE4Structs::Vector3 bombPos;
			const bool haveBomb = WindowRadarResolveBombDrawPos(bombPos);
			if (haveBomb) {
				float bx, by;
				WorldToMapPercent(*def, bombPos, bx, by);
				const bool clipFull = !wantFollow;
				if (rotMap) {
					const ImVec2 bc = WorldPlanarToScreenXY(localForRadar, bombPos, rmin.x, rmin.y, mapW, mapH,
						halfSpanDragon, true, viewYawPlanar);
					if (!clipFull || (bc.x >= rmin.x - 6.f && bc.x <= rmax.x + 6.f && bc.y >= rmin.y - 6.f &&
										  bc.y <= rmax.y + 6.f))
						RadarDrawBlipAt(dl, bc, IM_COL32(255, 90, 40, 255), bombR, true);
				} else if (!clipFull || (bx >= -5.f && bx <= 105.f && by >= -5.f && by <= 105.f)) {
					RadarDrawBlip(dl, bx, by, uv0, uv1, rmin, mapW, mapH, IM_COL32(255, 90, 40, 255), bombR, true,
						false, rotCtr, 0.f);
				}
			}

			bool drewLocal = false;
			for (const WindowRadarBlipSnap& e : radarBlips) {
				const bool isLocalPawn = e.is_local;
				const UE4Structs::Vector3& ori = e.world;
				float px, py;
				WorldToMapPercent(*def, ori, px, py);
				const bool clipFull = !wantFollow;
				if (!rotMap && clipFull && (px < -8.f || px > 108.f || py < -8.f || py > 108.f))
					continue;
				ImU32 col = RadarRgbF(EspUiColors::radar_enemy);
				if (isLocalPawn) {
					col = RadarRgbF(EspUiColors::radar_local);
					drewLocal = true;
				} else if (e.team == localTeamRadar)
					col = RadarRgbF(EspUiColors::radar_team);
				ImU32 viewCol = RadarRgbF(EspUiColors::radar_enemy, 228);
				if (isLocalPawn)
					viewCol = RadarRgbF(EspUiColors::radar_local, 255);
				else if (e.team == localTeamRadar)
					viewCol = RadarRgbF(EspUiColors::radar_team, 228);
				if (rotMap) {
					const ImVec2 c = WorldPlanarToScreenXY(localForRadar, ori, rmin.x, rmin.y, mapW, mapH, halfSpanDragon,
						true, viewYawPlanar);
					if (c.x < rmin.x - 10.f || c.x > rmax.x + 10.f || c.y < rmin.y - 10.f || c.y > rmax.y + 10.f)
						continue;
					RadarDrawBlipAt(dl, c, col, blipR, isLocalPawn);
					RadarDrawFacingPlanar(dl, e.actor, ori, c, lineWorld, blipR, true, viewYawPlanar, localForRadar,
						rmin.x, rmin.y, mapW, mapH, halfSpanDragon, viewCol);
				} else {
					RadarDrawBlip(dl, px, py, uv0, uv1, rmin, mapW, mapH, col, blipR, isLocalPawn, false, rotCtr, 0.f);
					RadarDrawFacing(dl, *def, e.actor, px, py, uv0, uv1, rmin, mapW, mapH, viewCol, lineWorld, blipR,
						false, rotCtr, 0.f);
				}
			}

			if (global_pawn && !drewLocal && haveLocalWorld) {
				float px, py;
				WorldToMapPercent(*def, localForRadar, px, py);
				if (rotMap) {
					const ImVec2 c = WorldPlanarToScreenXY(localForRadar, localForRadar, rmin.x, rmin.y, mapW, mapH,
						halfSpanDragon, true, viewYawPlanar);
					RadarDrawBlipAt(dl, c, RadarRgbF(EspUiColors::radar_local), blipR, true);
					RadarDrawFacingPlanar(dl, global_pawn, localForRadar, c, lineWorld, blipR, true, viewYawPlanar,
						localForRadar, rmin.x, rmin.y, mapW, mapH, halfSpanDragon,
						RadarRgbF(EspUiColors::radar_local, 255));
				} else {
					RadarDrawBlip(dl, px, py, uv0, uv1, rmin, mapW, mapH, RadarRgbF(EspUiColors::radar_local), blipR,
						true, false, rotCtr, 0.f);
					RadarDrawFacing(dl, *def, global_pawn, px, py, uv0, uv1, rmin, mapW, mapH,
						RadarRgbF(EspUiColors::radar_local, 255), lineWorld, blipR, false, rotCtr, 0.f);
				}
			}
		} else if (def && !haveLocalWorld) {
			ImVec2 uv0(0.f, 0.f), uv1(1.f, 1.f);
			for (const WindowRadarBlipSnap& e : radarBlips) {
				const bool isLocalPawn = e.is_local;
				const UE4Structs::Vector3& ori = e.world;
				float px, py;
				WorldToMapPercent(*def, ori, px, py);
				if (px < -8.f || px > 108.f || py < -8.f || py > 108.f)
					continue;
				ImU32 col = RadarRgbF(EspUiColors::radar_enemy);
				if (isLocalPawn)
					col = RadarRgbF(EspUiColors::radar_local);
				else if (e.team == localTeamRadar)
					col = RadarRgbF(EspUiColors::radar_team);
				RadarDrawBlip(dl, px, py, uv0, uv1, rmin, mapW, mapH, col, blipR, isLocalPawn, false, rotCtr, 0.f);
				ImU32 viewCol = RadarRgbF(EspUiColors::radar_enemy, 228);
				if (isLocalPawn)
					viewCol = RadarRgbF(EspUiColors::radar_local, 255);
				else if (e.team == localTeamRadar)
					viewCol = RadarRgbF(EspUiColors::radar_team, 228);
				RadarDrawFacing(dl, *def, e.actor, px, py, uv0, uv1, rmin, mapW, mapH, viewCol, lineWorld, blipR,
					false, rotCtr, 0.f);
			}
		} else if (haveLocalWorld) {
			UE4Structs::Vector3 localForRadar = localWorld;
			if (global_pawn && wantFollow)
				localForRadar = ReadEntityWorldPos(global_pawn);

			UE4Structs::Vector3 bombPos;
			const bool haveBomb = WindowRadarResolveBombDrawPos(bombPos);
			if (haveBomb) {
				ImVec2 bc = WorldPlanarToScreenXY(localForRadar, bombPos, rmin.x, rmin.y, mapW, mapH, halfSpanDragon, rotMap,
					viewYawPlanar);
				if (bc.x >= rmin.x - 6.f && bc.x <= rmax.x + 6.f && bc.y >= rmin.y - 6.f && bc.y <= rmax.y + 6.f)
					RadarDrawBlipAt(dl, bc, IM_COL32(255, 90, 40, 255), bombR, true);
			}

			bool drewLocal = false;
			for (const WindowRadarBlipSnap& e : radarBlips) {
				const bool isLocalPawn = e.is_local;
				const UE4Structs::Vector3& ori = e.world;
				ImVec2 c = WorldPlanarToScreenXY(localForRadar, ori, rmin.x, rmin.y, mapW, mapH, halfSpanDragon, rotMap, viewYawPlanar);
				if (c.x < rmin.x - 10.f || c.x > rmax.x + 10.f || c.y < rmin.y - 10.f || c.y > rmax.y + 10.f)
					continue;
				ImU32 col = RadarRgbF(EspUiColors::radar_enemy);
				if (isLocalPawn) {
					col = RadarRgbF(EspUiColors::radar_local);
					drewLocal = true;
				} else if (e.team == localTeamRadar)
					col = RadarRgbF(EspUiColors::radar_team);
				RadarDrawBlipAt(dl, c, col, blipR, isLocalPawn);
				ImU32 viewCol = RadarRgbF(EspUiColors::radar_enemy, 228);
				if (isLocalPawn)
					viewCol = RadarRgbF(EspUiColors::radar_local, 255);
				else if (e.team == localTeamRadar)
					viewCol = RadarRgbF(EspUiColors::radar_team, 228);
				RadarDrawFacingPlanar(dl, e.actor, ori, c, lineWorld, blipR, rotMap, viewYawPlanar, localForRadar, rmin.x,
					rmin.y, mapW, mapH, halfSpanDragon, viewCol);
			}
			if (global_pawn && !drewLocal) {
				ImVec2 c = WorldPlanarToScreenXY(localForRadar, localForRadar, rmin.x, rmin.y, mapW, mapH, halfSpanDragon, rotMap,
					viewYawPlanar);
				RadarDrawBlipAt(dl, c, RadarRgbF(EspUiColors::radar_local), blipR, true);
				RadarDrawFacingPlanar(dl, global_pawn, localForRadar, c, lineWorld, blipR, rotMap, viewYawPlanar, localForRadar,
					rmin.x, rmin.y, mapW, mapH, halfSpanDragon, RadarRgbF(EspUiColors::radar_local, 255));
			}
		}

		} 

		dl->PopClipRect();
	}
	ImGui::End();
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(2);
}
