#include "window_radar_hud.hpp"
#include "cat_mem_scan.hpp"
#include "globals.hpp"
#include "offsets_runtime.hpp"
#include "structs.hpp"
#include "../Driver/driver.hpp"

#include "../../Includes/Imgui/imgui.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace external_hud_radar {
namespace {

constexpr std::uintptr_t kHudElementCountOff = 0x264;
constexpr std::uintptr_t kHudElementDataOff = 0x268;
constexpr std::size_t kHudElementStride = 0x18;
constexpr std::intptr_t kHudRadarBaseDelta = -32;

constexpr std::uintptr_t kRadarIsRound = 0x60;
constexpr std::uintptr_t kRadarMapTexPos = 0x190;
constexpr std::uintptr_t kRadarVisMax = 0x19C;
constexpr std::uintptr_t kRadarVis = 0x1A0;
constexpr std::uintptr_t kRadarMapTexScaleInv = 0x1B4;
constexpr std::uintptr_t kRadarMaxVisSq = 0x1B8;
constexpr std::uintptr_t kRadarOriginTexDiff = 0x1D0;
constexpr std::uintptr_t kRadarRoundScale = 0x17F8C;

struct HudElementEntry {
	std::uint8_t pad[0x10]{};
	std::uintptr_t string_ptr{};
	std::uintptr_t element_ptr{};
};

struct HudRadarMem {
	bool valid = false;
	bool is_round = false;
	UE4Structs::Vector3 map_texture_position{};
	float visibility_size_max = 0.f;
	float visibility_size = 0.f;
	float map_texture_scale = 1.f;
	float max_visibility_squared = 0.f;
	UE4Structs::Vector3 origin_texture_position_difference{};
	float radar_scale = 1.f;
};

HudRadarMem g_hud{};
std::uintptr_t g_chud_pattern_slot = 0;
std::uintptr_t g_chud_pattern_client_module = 0;

bool FloatOk(float f) {
	return std::isfinite(f) && f > 0.f && f < 1e12f;
}

std::uintptr_t ResolveCHudPointer() {
	const std::uintptr_t cl = static_cast<std::uintptr_t>(client);
	if (!cl)
		return 0;
	if (offsets::dwCHud) {
		const std::uintptr_t p =
			g_GameMem.readv<std::uintptr_t>(cl + static_cast<std::uintptr_t>(offsets::dwCHud));
		if (p > 0x10000ULL && p < 0x7FFFFFFFFFFFULL)
			return p;
	}
	const std::uintptr_t mod = cat_mem::client_module();
	if (!mod)
		return 0;
	if (mod != g_chud_pattern_client_module) {
		g_chud_pattern_client_module = mod;
		g_chud_pattern_slot = 0;
	}
	if (!g_chud_pattern_slot) {
		const std::uintptr_t hit = cat_mem::find_pattern(mod,
			"C6 81 BC 0B 00 00 00 48 8B 0D ? ? ? ? 48 85 C9");
		if (!hit)
			return 0;
		g_chud_pattern_slot = cat_mem::resolve_rip(hit + 7, 3, 7);
	}
	if (!g_chud_pattern_slot)
		return 0;
	const std::uintptr_t chud = g_GameMem.readv<std::uintptr_t>(g_chud_pattern_slot);
	if (chud > 0x10000ULL && chud < 0x7FFFFFFFFFFFULL)
		return chud;
	return 0;
}

std::uintptr_t FindHudElement(std::uintptr_t chud, const char* want_name) {
	if (!chud || !want_name)
		return 0;
	const std::int32_t count_raw = g_GameMem.readv<std::int32_t>(chud + kHudElementCountOff);
	const std::int32_t count = count_raw & 0x7FFFFFFF;
	const std::uintptr_t data = g_GameMem.readv<std::uintptr_t>(chud + kHudElementDataOff);
	if (count <= 0 || count > 4096 || !data || data < 0x10000ULL)
		return 0;
	for (std::int32_t i = 0; i < count; ++i) {
		const std::uintptr_t row = data + static_cast<std::uintptr_t>(kHudElementStride) * static_cast<std::uintptr_t>(i);
		const HudElementEntry el = g_GameMem.readv<HudElementEntry>(row);
		if (!el.string_ptr || !el.element_ptr)
			continue;
		const std::string name = g_GameMem.ReadString(el.string_ptr, 127);
		if (_stricmp(name.c_str(), want_name) == 0)
			return el.element_ptr;
	}
	return 0;
}

void UpdateHudRadarFromElement(std::uintptr_t element_address) {
	g_hud = {};
	if (!element_address)
		return;
	const std::uintptr_t base = static_cast<std::uintptr_t>(
		static_cast<std::intptr_t>(element_address) + static_cast<std::intptr_t>(kHudRadarBaseDelta));
	g_hud.is_round = g_GameMem.readv<bool>(base + kRadarIsRound);
	g_hud.map_texture_position = g_GameMem.readv<UE4Structs::Vector3>(base + kRadarMapTexPos);
	g_hud.visibility_size_max = g_GameMem.readv<float>(base + kRadarVisMax);
	g_hud.visibility_size = g_GameMem.readv<float>(base + kRadarVis);
	const float inv_scale = g_GameMem.readv<float>(base + kRadarMapTexScaleInv);
	g_hud.map_texture_scale = (FloatOk(inv_scale)) ? (1.f / inv_scale) : 1.f;
	g_hud.max_visibility_squared = g_GameMem.readv<float>(base + kRadarMaxVisSq);
	g_hud.origin_texture_position_difference =
		g_GameMem.readv<UE4Structs::Vector3>(base + kRadarOriginTexDiff);
	float rs = g_GameMem.readv<float>(base + kRadarRoundScale);
	if (!FloatOk(rs) || rs > 4.f)
		rs = g_hud.is_round ? 1.f : (g_hud.visibility_size / (std::max)(g_hud.visibility_size_max, 1e-6f));
	g_hud.radar_scale = rs;
	if (!FloatOk(g_hud.map_texture_scale) || g_hud.map_texture_scale <= 0.f)
		return;
	if (!FloatOk(g_hud.visibility_size_max))
		return;
	g_hud.valid = true;
}

void TranslateToRadar(const UE4Structs::Vector3& position, float yaw, float radar_to_texture_scale,
	ImVec2& out, bool* out_of_bounds) {
	const float dx = position.x - g_hud.map_texture_position.x;
	const float dy = g_hud.map_texture_position.y - position.y;
	const float px = dx * radar_to_texture_scale - g_hud.origin_texture_position_difference.x;
	const float py = dy * radar_to_texture_scale - g_hud.origin_texture_position_difference.y;
	const float dist_sq = px * px + py * py;
	float x = px, y = py;
	if (g_hud.is_round) {
		const float cos_yaw = std::cos(yaw);
		const float sin_yaw = std::sin(yaw);
		if (dist_sq >= g_hud.max_visibility_squared && g_hud.max_visibility_squared > 0.f) {
			const float scale = std::sqrtf(g_hud.max_visibility_squared / dist_sq);
			x = -px * scale * cos_yaw + py * scale * sin_yaw;
			y = -px * scale * sin_yaw - py * scale * cos_yaw;
		} else {
			x = -px * cos_yaw + py * sin_yaw;
			y = -px * sin_yaw - py * cos_yaw;
		}
	}
	if (out_of_bounds)
		*out_of_bounds = g_hud.is_round && dist_sq >= g_hud.max_visibility_squared && g_hud.max_visibility_squared > 0.f;
	out = ImVec2(x, y);
}

static ImU32 RadarRgbF(const float* rgb, int a = 255) {
	return IM_COL32(
		(int)(std::clamp(rgb[0], 0.f, 1.f) * 255.f),
		(int)(std::clamp(rgb[1], 0.f, 1.f) * 255.f),
		(int)(std::clamp(rgb[2], 0.f, 1.f) * 255.f), a);
}

static float ReadPawnEyeYawDegrees(uintptr_t pawn) {
	if (!pawn || !offsets::m_angEyeAngles)
		return 0.f;
	return g_GameMem.readv<float>(pawn + static_cast<uintptr_t>(offsets::m_angEyeAngles) + 4);
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

} 

bool TickFromMemory() {
	const std::uintptr_t chud = ResolveCHudPointer();
	if (!chud) {
		g_hud.valid = false;
		return false;
	}
	const std::uintptr_t el = FindHudElement(chud, "CCSGO_HudRadar");
	if (!el) {
		g_hud.valid = false;
		return false;
	}
	UpdateHudRadarFromElement(el);
	return g_hud.valid;
}

bool TryDrawFrame(ImDrawList* dl, const ImVec2& rmin, const ImVec2& rmax, float mapW, float mapH, bool showDebugHud) {
	if (!dl || mapW < 8.f || mapH < 8.f)
		return false;

	std::vector<UE4Structs::CS2Entity> snapshot;
	{
		std::lock_guard<std::mutex> lk(g_PlayerListMutex);
		snapshot = UE4Structs::PlayerList;
	}
	
	if (!TickFromMemory())
		return false;

	const float hud_scaling = 1.f;
	const float cl_hud_radar_scale = 1.f;
	const float radar_hud_scaling = cl_hud_radar_scale * hud_scaling;
	
	const ImGuiIO& io = ImGui::GetIO();
	const float screen_w = (std::max)(320.f, io.DisplaySize.x);
	const float screen_h = (std::max)(240.f, io.DisplaySize.y);
	constexpr float kSafeZoneX = 1.f;
	constexpr float kSafeZoneY = 1.f;
	const float hud_padding = hud_scaling * 5.f;
	const float radar_padding_x = screen_w * 0.5f * (1.f - kSafeZoneX) + hud_padding;
	const float radar_padding_y = screen_h * 0.5f * (1.f - kSafeZoneY) + hud_padding;
	const float radar_size = 290.f * radar_hud_scaling;
	const float ref_cx = radar_padding_x + radar_size * 0.5f;
	const float ref_cy = radar_padding_y + radar_size * 0.5f;
	const float ref_left = ref_cx - radar_size * 0.5f;
	const float ref_top = ref_cy - radar_size * 0.5f;

	const float side = (std::min)(mapW, mapH);
	const float ox = rmin.x + (mapW - side) * 0.5f;
	const float oy = rmin.y + (mapH - side) * 0.5f;

	constexpr float pi = 3.14159265f;
	const bool cl_radar_rotate = Settings::misc::radarWindowRotateWithView;
	const float view_yaw_deg = global_pawn ? ReadPawnEyeYawDegrees(global_pawn) : 0.f;
	const float yaw = cl_radar_rotate ? (view_yaw_deg * (pi / 180.f) + pi * 0.5f) : pi;

	const float radar_scale = g_hud.is_round ? g_hud.radar_scale
											   : (g_hud.visibility_size / (std::max)(g_hud.visibility_size_max, 1e-6f));
	const float radar_to_texture_scale = radar_scale / g_hud.map_texture_scale;

	const float icon_scale_min = (std::max)(0.1f, (std::min)(1.25f, Settings::misc::radarWindowBlipScale));
	float dot_radius = std::clamp(radar_scale, 0.f, 1.f) * (1.25f - icon_scale_min) + icon_scale_min;
	dot_radius *= 7.5f * radar_hud_scaling * (side / (std::max)(radar_size, 1.f));

	if (showDebugHud) {
		char buf[288];
		snprintf(buf, sizeof buf, "HUD radar: round=%d ref=(%.0f,%.0f) sz=%.0f side=%.0f",
			(int)g_hud.is_round, (double)ref_cx, (double)ref_cy, (double)radar_size, (double)side);
		dl->AddText(ImVec2(rmin.x + 4.f, rmin.y + 4.f), IM_COL32(200, 220, 255, 220), buf);
	}

	UE4Structs::Vector3 localWorld{};
	bool haveLocal = false;
	if (global_pawn) {
		localWorld = ReadEntityWorldPos(global_pawn);
		haveLocal = true;
	}

	int localTeam = 0;
	if (global_pawn && offsets::m_iTeamNum)
		localTeam = g_GameMem.readv<int>(global_pawn + static_cast<uintptr_t>(offsets::m_iTeamNum)) & 0xFF;

	auto draw_one = [&](const UE4Structs::Vector3& world, ImU32 col, bool ring) {
		bool oob = false;
		ImVec2 pos{};
		TranslateToRadar(world, yaw, radar_to_texture_scale, pos, &oob);
		
		const float sx_ref = ref_cx + pos.x * radar_hud_scaling;
		const float sy_ref = ref_cy + pos.y * radar_hud_scaling;
		const float u = (sx_ref - ref_left) / (std::max)(radar_size, 1.f);
		const float v = (sy_ref - ref_top) / (std::max)(radar_size, 1.f);
		const float sx = ox + u * side;
		const float sy = oy + v * side;
		if (sx < ox - 2.f || sx > ox + side + 2.f || sy < oy - 2.f || sy > oy + side + 2.f)
			return;
		if (oob) {
			const float mx = ox + side * 0.5f;
			const float my = oy + side * 0.5f;
			float rdx = sx - mx;
			float rdy = sy - my;
			const float len = std::sqrt(rdx * rdx + rdy * rdy);
			if (len < 1e-4f)
				return;
			rdx /= len;
			rdy /= len;
			const ImVec2 dir(rdx, rdy);
			const ImVec2 tip = ImVec2(sx, sy) + ImVec2(dir.x - dir.x * dot_radius, dir.y - dir.y * dot_radius);
			const ImVec2 tri[3] = {
				ImVec2(tip.x + dir.y * dot_radius, tip.y - dir.x * dot_radius),
				ImVec2(tip.x + 2.f * dir.x * dot_radius, tip.y + 2.f * dir.y * dot_radius),
				ImVec2(tip.x - dir.y * dot_radius, tip.y + dir.x * dot_radius),
			};
			dl->AddConvexPolyFilled(tri, 3, col);
		} else {
			dl->AddCircleFilled(ImVec2(sx, sy), dot_radius, col, 14);
			if (ring)
				dl->AddCircle(ImVec2(sx, sy), dot_radius + 1.75f, IM_COL32(0, 0, 0, 210), 14);
		}
	};

	struct HudBlip {
		UE4Structs::Vector3 world{};
		ImU32 col{};
		bool ring{};
	};
	std::vector<HudBlip> blips;
	blips.reserve(snapshot.size() + 4u);

	bool drewLocal = false;
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
		const bool isLocalPawn = (global_pawn && e.Actor == global_pawn);
		const UE4Structs::Vector3 ori = ReadEntityWorldPos(e.Actor);
		if (!isLocalPawn && ori.length2d() < 8.f && std::fabs(ori.z) < 8.f)
			continue;
		const int team = g_GameMem.readv<int>(e.Actor + offsets::m_iTeamNum) & 0xFF;
		ImU32 col = RadarRgbF(EspUiColors::radar_enemy);
		if (isLocalPawn) {
			col = RadarRgbF(EspUiColors::radar_local);
			drewLocal = true;
		} else if (team == localTeam)
			col = RadarRgbF(EspUiColors::radar_team);
		blips.push_back({ ori, col, isLocalPawn });
	}
	if (haveLocal && global_pawn && !drewLocal)
		blips.push_back({ localWorld, RadarRgbF(EspUiColors::radar_local), true });

	UE4Structs::Vector3 bombPos{};
	if (ReadPlantedBombWorld(bombPos))
		blips.push_back({ bombPos, IM_COL32(255, 90, 40, 255), true });

	for (const HudBlip& b : blips)
		draw_one(b.world, b.col, b.ring);

	return true;
}

} 
