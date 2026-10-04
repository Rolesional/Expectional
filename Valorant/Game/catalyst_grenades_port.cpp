#include "catalyst_grenades_port.hpp"

#include "catalyst_world_bvh.hpp"
#include "globals.hpp"
#include "offsets_runtime.hpp"
#include "entity_handle.hpp"
#include "esp_extras.hpp"
#include "cat_mem_scan.hpp"
#include "grenade_esp.hpp"

#include "../../Includes/Imgui/imgui.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include <Windows.h>

namespace catalyst_grenades_port {
namespace {

using Vector3 = UE4Structs::Vector3;
using view_matrix_t = UE4Structs::view_matrix_t;
using namespace ex_esp::grenade_cat_detail;

constexpr float kTickInterval = 1.f / 64.f;
constexpr float kGravityScale = 0.4f;
constexpr float kElasticity = 0.45f;
constexpr int kTicksPerPoint = 4;
constexpr float kDefaultSvGravity = 800.f;
constexpr int kBvhTicksBudget = 16;

struct ThrowParams {
	std::uint16_t def_idx = 0;
	float strength = 1.f;
	Vector3 origin{};
	Vector3 velocity{};
};

struct Trajectory {
	std::vector<Vector3> points;
	Vector3 end_pos{};
};

bool read_throw_view(std::uintptr_t pawn, const Vector3& eye_fallback, Vector3& out_origin, Vector3& out_angles)
{
	Vector3 cvo{}, cva{};
	if (read_cview_render_origin_angles(cvo, cva)) {
		out_origin = cvo;
		out_angles = cva;
		return true;
	}
	out_origin = eye_fallback;
	if (out_origin.IsZero() && pawn)
		out_origin = ex_esp::ReadWorldPositionFromEntity(pawn);
	if (offsets::dwViewAngles && client) {
		const Vector3 va = g_GameMem.readv<Vector3>(
		    static_cast<std::uintptr_t>(client) + static_cast<std::uintptr_t>(offsets::dwViewAngles));
		if (std::isfinite(va.x) && std::isfinite(va.y)) {
			out_angles = va;
			if (!out_origin.IsZero())
				return true;
		}
	}
	if (pawn && offsets::m_angEyeAngles) {
		const Vector3 ea = g_GameMem.readv<Vector3>(pawn + static_cast<std::uintptr_t>(offsets::m_angEyeAngles));
		if (std::isfinite(ea.x)) {
			out_angles = ea;
			return !out_origin.IsZero();
		}
	}
	return false;
}

bool build_throw_params(std::uintptr_t weapon, std::uintptr_t pawn, float throw_velocity,
                        const Vector3& eye_fallback, ThrowParams& out)
{
	if (!weapon || !pawn)
		return false;

	std::uint16_t defIdx = ex_esp::ReadWeaponDefIndexFromWeaponEntity(weapon);
	if (!defIdx)
		defIdx = ex_esp::ReadWeaponDefIndex(pawn);
	if (defIdx < 43 || defIdx > 48)
		return false;

	out.def_idx = defIdx;
	out.strength = 1.f;
	if (offsets::nade_m_flThrowStrength && offsets::nade_m_bPinPulled) {
		const bool pin = g_GameMem.readv<bool>(weapon + static_cast<std::uintptr_t>(offsets::nade_m_bPinPulled));
		if (pin) {
			out.strength = std::clamp(
			    g_GameMem.readv<float>(weapon + static_cast<std::uintptr_t>(offsets::nade_m_flThrowStrength)), 0.f, 1.f);
			if (std::fabsf(out.strength - 0.5f) <= 0.1f)
				out.strength = 0.5f;
		}
	}

	Vector3 view_origin{}, view_angles{};
	if (!read_throw_view(pawn, eye_fallback, view_origin, view_angles))
		return false;

	Vector3 angles = view_angles;
	if (angles.x > 90.f)
		angles.x -= 360.f;
	else if (angles.x < -90.f)
		angles.x += 360.f;
	angles.x -= (90.f - std::fabsf(angles.x)) * 10.f / 90.f;

	Vector3 pawn_vel{};
	if (offsets::m_vecAbsVelocity)
		pawn_vel = g_GameMem.readv<Vector3>(pawn + static_cast<std::uintptr_t>(offsets::m_vecAbsVelocity));

	Vector3 eye_pos = view_origin;
	eye_pos.z += out.strength * 12.f - 12.f;
	Vector3 forward{};
	angles_to_directions(angles, &forward, nullptr, nullptr);

	if (ex_world_bvh::g_world_bvh.valid()) {
		const auto tr = ex_world_bvh::g_world_bvh.trace_ray(eye_pos, vec_add(eye_pos, vec_scale(forward, 22.f)));
		out.origin = tr.hit ? vec_sub(tr.end_pos, vec_scale(forward, 6.f)) : vec_add(eye_pos, vec_scale(forward, 16.f));
	} else {
		out.origin = vec_add(eye_pos, vec_scale(forward, 16.f));
	}

	const float throw_vel = std::clamp(throw_velocity * 0.9f, 15.f, 750.f);
	const float throw_speed = (out.strength * 0.7f + 0.3f) * throw_vel;
	out.velocity = vec_add(vec_scale(forward, throw_speed), vec_scale(pawn_vel, 1.25f));
	return true;
}

/** BVH yokken de gorunur — aninda, 0 trace. */
void build_fast_arc(const ThrowParams& tp, Trajectory& out)
{
	out.points.clear();
	out.points.push_back(tp.origin);
	const float g = kDefaultSvGravity * kGravityScale;
	const float horizon = 2.6f;
	const int steps = 18;
	for (int i = 1; i <= steps; ++i) {
		const float t = horizon * (static_cast<float>(i) / static_cast<float>(steps));
		const Vector3 p =
		    vec_add(vec_add(tp.origin, vec_scale(tp.velocity, t)), Vector3(0.f, 0.f, -0.5f * g * t * t));
		out.points.push_back(p);
	}
	out.end_pos = out.points.back();
}

void weapon_timing(std::uint16_t defIdx, float& detonate_time, float& velocity_threshold, bool& is_molotov)
{
	is_molotov = defIdx == 46 || defIdx == 48;
	switch (defIdx) {
	case 46:
	case 48:
		detonate_time = 2.f;
		velocity_threshold = 0.f;
		break;
	case 47:
		detonate_time = 2.f;
		velocity_threshold = 0.2f;
		break;
	default:
		detonate_time = 1.5f;
		velocity_threshold = 0.1f;
		break;
	}
}

bool should_detonate(std::uint16_t defIdx, const Vector3& vel, int tick, float detonate_time, float velocity_threshold)
{
	switch (defIdx) {
	case 45:
	case 47: {
		const float speed_2d = std::sqrtf(vel.x * vel.x + vel.y * vel.y);
		const int check_ticks = static_cast<int>(0.2f / kTickInterval);
		return speed_2d < velocity_threshold && check_ticks > 0 && (tick % check_ticks) == 0;
	}
	case 46:
	case 48:
		return static_cast<float>(tick) * kTickInterval > detonate_time;
	case 43:
	case 44:
		return static_cast<float>(tick - 8) * kTickInterval > detonate_time;
	default:
		return false;
	}
}

void resolve_collision(const ex_world_bvh::bvh::trace_result& trace, Vector3& pos, Vector3& vel)
{
	const float total_elasticity = std::clamp(kElasticity, 0.f, 0.9f);
	const float backoff = vec_dot(vel, trace.normal) * 2.f;
	Vector3 new_vel = vec_scale(vec_sub(vel, vec_scale(trace.normal, backoff)), total_elasticity);
	if (trace.normal.z > 0.7f) {
		const float speed_sqr = vec_len_sqr(new_vel);
		if (speed_sqr > 96000.f) {
			const float l = vec_dot(vec_normalized(new_vel), trace.normal);
			if (l > 0.5f)
				new_vel = vec_scale(new_vel, 1.5f - l);
		}
		if (speed_sqr < 400.f) {
			vel = Vector3{};
			return;
		}
	}
	vel = new_vel;
	const float remaining = 1.f - trace.fraction;
	if (remaining > 0.f) {
		const Vector3 slide_end = vec_add(pos, vec_scale(new_vel, remaining * kTickInterval));
		const auto post = ex_world_bvh::g_world_bvh.trace_ray(pos, slide_end);
		pos = post.end_pos;
	}
}

void step_simulation(Vector3& pos, Vector3& vel, ex_world_bvh::bvh::trace_result& trace)
{
	const float gravity = kDefaultSvGravity * kGravityScale;
	const float new_vel_z = vel.z - gravity * kTickInterval;
	const Vector3 move(vel.x * kTickInterval, vel.y * kTickInterval, (vel.z + new_vel_z) * 0.5f * kTickInterval);
	vel.z = new_vel_z;
	trace = ex_world_bvh::g_world_bvh.trace_ray(pos, vec_add(pos, move));
	pos = trace.end_pos;
	if (trace.hit)
		resolve_collision(trace, pos, vel);
}

bool build_bvh_arc(const ThrowParams& tp, Trajectory& out)
{
	if (!ex_world_bvh::g_world_bvh.valid())
		return false;

	float detonate_time = 1.5f;
	float velocity_threshold = 0.1f;
	bool is_molotov = false;
	weapon_timing(tp.def_idx, detonate_time, velocity_threshold, is_molotov);
	const float molotov_max_slope_z = std::cosf(45.f * 3.14159265f / 180.f);

	out.points.clear();
	Vector3 pos = tp.origin;
	Vector3 vel = tp.velocity;
	int bounce_count = 0;
	int tick_timer = 0;
	int end_tick = -1;

	out.points.push_back(pos);

	for (int tick = 0; tick < kBvhTicksBudget; ++tick) {
		if (tick_timer == 0 && tick > 0)
			out.points.push_back(pos);

		ex_world_bvh::bvh::trace_result trace{};
		step_simulation(pos, vel, trace);

		if (trace.hit) {
			++bounce_count;
			if (is_molotov && trace.normal.z >= molotov_max_slope_z) {
				end_tick = tick;
				out.end_pos = pos;
				break;
			}
		}

		const bool velocity_stopped =
		    std::fabsf(vel.x) < 20.f && std::fabsf(vel.y) < 20.f && vec_len_sqr(vel) < 400.f;
		if (should_detonate(tp.def_idx, vel, tick, detonate_time, velocity_threshold) || bounce_count > 20 ||
		    velocity_stopped) {
			end_tick = tick;
			out.end_pos = pos;
			break;
		}

		if (trace.hit || ++tick_timer >= kTicksPerPoint)
			tick_timer = 0;
	}

	if (end_tick < 0)
		out.end_pos = pos;

	if (vec_len_sqr(vec_sub(out.points.back(), out.end_pos)) > 1.f)
		out.points.push_back(out.end_pos);
	return out.points.size() >= 2;
}

bool w2s(const Vector3& pos, Vector3& out, const view_matrix_t& vm)
{
	const float(&m)[4][4] = vm.matrix;
	out.x = m[0][0] * pos.x + m[0][1] * pos.y + m[0][2] * pos.z + m[0][3];
	out.y = m[1][0] * pos.x + m[1][1] * pos.y + m[1][2] * pos.z + m[1][3];
	const float w = m[3][0] * pos.x + m[3][1] * pos.y + m[3][2] * pos.z + m[3][3];
	if (w < 0.01f)
		return false;
	const float inv = 1.f / w;
	out.x *= inv;
	out.y *= inv;
	out.z = w;
	return true;
}

void draw_trajectory(const view_matrix_t& vm, const Trajectory& traj)
{
	if (traj.points.size() < 2)
		return;

	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	const ImU32 lineCol = ImGui::ColorConvertFloat4ToU32(ImVec4(
		EspUiColors::grenade_traj_line[0], EspUiColors::grenade_traj_line[1], EspUiColors::grenade_traj_line[2], 1.f));
	const ImU32 detCol = ImGui::ColorConvertFloat4ToU32(ImVec4(
		EspUiColors::grenade_traj_marker[0], EspUiColors::grenade_traj_marker[1], EspUiColors::grenade_traj_marker[2], 1.f));

	for (std::size_t i = 0; i + 1 < traj.points.size(); ++i) {
		Vector3 s0{}, s1{};
		if (!w2s(traj.points[i], s0, vm) || !w2s(traj.points[i + 1], s1, vm))
			continue;
		if (s0.z < 0.01f || s1.z < 0.01f)
			continue;
		dl->AddLine(ImVec2(s0.x, s0.y), ImVec2(s1.x, s1.y), lineCol, 1.8f);
	}
	Vector3 es{};
	if (w2s(traj.end_pos, es, vm) && es.z > 0.01f)
		dl->AddCircleFilled(ImVec2(es.x, es.y), 5.f, detCol, 14);
}

}  // namespace

void OnRender(const view_matrix_t& vm, std::uintptr_t local_pawn, const Vector3& eye_fallback)
{
	if (!Settings::Visuals::grenadeHelper || !local_pawn || !offsets::m_pWeaponServices || !offsets::m_hActiveWeapon)
		return;

	const std::uint16_t def_idx = ex_esp::ReadWeaponDefIndex(local_pawn);
	if (def_idx < 43 || def_idx > 48)
		return;

	const std::uintptr_t ws =
	    g_GameMem.readv<std::uintptr_t>(local_pawn + static_cast<std::uintptr_t>(offsets::m_pWeaponServices));
	if (!ws)
		return;
	const std::uint32_t hw = g_GameMem.readv<std::uint32_t>(ws + static_cast<std::uintptr_t>(offsets::m_hActiveWeapon));
	const std::uintptr_t wpn = ex_entity::ResolveHandle(hw);
	if (!wpn)
		return;

	/** Pin cekilmeden sim/IOCTL yapma — Valve MM'de gereksiz lag. */
	if (offsets::nade_m_bPinPulled) {
		const bool pin = g_GameMem.readv<bool>(wpn + static_cast<std::uintptr_t>(offsets::nade_m_bPinPulled));
		if (!pin)
			return;
	}

	static std::uint32_t s_traj_hash = 0;
	static Trajectory s_traj_cache{};
	static DWORD s_traj_build_ms = 0;

	float throwVel = 750.f;
	if (offsets::entity_m_nSubclassID && offsets::vdata_m_flThrowVelocity) {
		const std::uintptr_t vdata =
		    g_GameMem.readv<std::uintptr_t>(wpn + static_cast<std::uintptr_t>(offsets::entity_m_nSubclassID) + 0x8);
		if (vdata)
			throwVel = g_GameMem.readv<float>(vdata + static_cast<std::uintptr_t>(offsets::vdata_m_flThrowVelocity));
	}
	throwVel = std::clamp(throwVel, 1.f, 10000.f);

	ThrowParams tp{};
	if (!build_throw_params(wpn, local_pawn, throwVel, eye_fallback, tp))
		return;

	/** Input hash — ayni nisan/pozisyonda yeniden sim yapma. */
	std::uint32_t h = static_cast<std::uint32_t>(tp.def_idx);
	auto qf = [](float f) -> std::uint32_t {
		return static_cast<std::uint32_t>(std::lround(f * 2.f)) & 0xFFFFu;
	};
	h = h * 31u + qf(tp.origin.x) + (qf(tp.origin.y) << 16);
	h = h * 31u + qf(tp.origin.z) + (static_cast<std::uint32_t>(std::lround(tp.strength * 8.f)) << 16);
	h = h * 31u + qf(tp.velocity.x) + (qf(tp.velocity.y) << 16);

	const DWORD now_ms = GetTickCount();
	if (h == s_traj_hash && s_traj_cache.points.size() >= 2u && (now_ms - s_traj_build_ms) < 48u) {
		draw_trajectory(vm, s_traj_cache);
		return;
	}

	Trajectory traj{};
	if (ex_world_bvh::g_world_bvh.valid())
		build_bvh_arc(tp, traj);
	if (traj.points.size() < 2)
		build_fast_arc(tp, traj);

	s_traj_hash = h;
	s_traj_cache = traj;
	s_traj_build_ms = now_ms;
	draw_trajectory(vm, traj);
}

}  // namespace catalyst_grenades_port
