#include "grenade_trajectory.hpp"

#include "catalyst_world_bvh.hpp"
#include "esp_extras.hpp"
#include "expectional_misc_runtime.hpp"
#include "globals.hpp"
#include "grenade_esp.hpp"
#include "offsets_runtime.hpp"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace expectional_trajectory {

using Vector3 = UE4Structs::Vector3;
using view_matrix_t = UE4Structs::view_matrix_t;
namespace cat = ex_esp::grenade_cat_detail;

namespace {

struct DrawCache {
	std::mutex mtx;
	bool active = false;
	bool bvh_ready = false;
	std::uint32_t draw_hash = 0;
	std::uint32_t lookup_hash = 0;
	std::uint16_t def_idx = 0;
	std::vector<Vector3> points;
	Vector3 end_pos{};
};

DrawCache g_draw;
std::atomic<bool> g_worker_on{false};

std::uintptr_t ResolveActiveWeapon(std::uintptr_t local_pawn)
{
	if (!local_pawn || !offsets::m_pWeaponServices || !offsets::m_hActiveWeapon)
		return 0;
	const std::uintptr_t ws =
	    g_GameMem.readv<std::uintptr_t>(local_pawn + static_cast<std::uintptr_t>(offsets::m_pWeaponServices));
	if (!ws)
		return 0;
	const std::uint32_t hw =
	    g_GameMem.readv<std::uint32_t>(ws + static_cast<std::uintptr_t>(offsets::m_hActiveWeapon));
	return ex_entity::ResolveHandle(hw);
}

float ReadThrowVelocity(std::uintptr_t weapon)
{
	float throw_vel = 750.f;
	if (weapon && offsets::entity_m_nSubclassID && offsets::vdata_m_flThrowVelocity) {
		const std::uintptr_t vdata = g_GameMem.readv<std::uintptr_t>(
		    weapon + static_cast<std::uintptr_t>(offsets::entity_m_nSubclassID) + 0x8);
		if (vdata)
			throw_vel = g_GameMem.readv<float>(vdata + static_cast<std::uintptr_t>(offsets::vdata_m_flThrowVelocity));
	}
	return std::clamp(throw_vel, 1.f, 10000.f);
}

std::uint32_t TrajectorySimLookupHash(std::uint16_t def_idx, float strength, const Vector3& angles,
                                      const Vector3& origin, const Vector3& pawn_vel)
{
	
	const float ang_q = 1.f;
	auto qang = [ang_q](float f) -> std::uint32_t {
		return static_cast<std::uint32_t>(std::lround(f * ang_q)) & 0xFFFFu;
	};
	auto qpos = [](float f) -> std::uint32_t {
		return static_cast<std::uint32_t>(std::lround(f * 2.f)) & 0xFFFFu;
	};
	std::uint32_t h = static_cast<std::uint32_t>(def_idx);
	h = h * 31u + qang(angles.x) + (qang(angles.y) << 16);
	h = h * 31u + qpos(origin.x) + (qpos(origin.y) << 16);
	h = h * 31u + qpos(origin.z) + (static_cast<std::uint32_t>(std::lround(strength * 8.f)) << 16);
	h = h * 31u + qpos(pawn_vel.x) + (qpos(pawn_vel.y) << 16);
	return h;
}

void PublishBvhTraj(std::uint32_t draw_hash, std::uint32_t lookup_hash, std::uint16_t def_idx,
                    const cat::CatalystTrajectory& traj)
{
	std::lock_guard<std::mutex> lk(g_draw.mtx);
	if (!traj.valid || traj.points.size() < 2) {
		g_draw.active = false;
		g_draw.bvh_ready = false;
		g_draw.points.clear();
		return;
	}
	g_draw.active = true;
	g_draw.bvh_ready = true;
	g_draw.draw_hash = draw_hash;
	g_draw.lookup_hash = lookup_hash;
	g_draw.def_idx = def_idx;
	g_draw.points = traj.points;
	g_draw.end_pos = traj.end_pos;
}

void ClearDraw()
{
	std::lock_guard<std::mutex> lk(g_draw.mtx);
	g_draw.active = false;
	g_draw.bvh_ready = false;
	g_draw.points.clear();
}

void TrajectoryWorkerMain()
{
	static std::uint32_t s_ready_lookup = 0;
	static std::uint16_t s_ready_def = 0;
	static DWORD s_last_sim_ms = 0;
	static DWORD s_mesh_poll_ms = 0;
	static bool s_mesh_ever_ready = false;

	for (;;) {
		if (!Settings::Visuals::grenadeHelper) {
			ClearDraw();
			s_ready_lookup = 0;
			Sleep(200);
			continue;
		}

		const std::uintptr_t local_pawn = global_pawn;
		if (!local_pawn || !client) {
			ClearDraw();
			Sleep(120);
			continue;
		}

		const DWORD now_ms = GetTickCount();
		const bool mesh_ready_now = ex_world_bvh::WorldMeshReady();
		if (!mesh_ready_now) {
			s_mesh_ever_ready = false;
			if (now_ms - s_mesh_poll_ms >= 1200u) {
				s_mesh_poll_ms = now_ms;
				ex_world_bvh::EnsureWorldBvhLoadThread();
				(void)ex_world_bvh::TryLoadWorldMeshNow();
			}
			Sleep(80);
			continue;
		}
		if (!s_mesh_ever_ready)
			s_mesh_ever_ready = true;

		const std::uint16_t def_idx = ex_esp::ReadWeaponDefIndex(local_pawn);
		if (def_idx < 43 || def_idx > 48) {
			ClearDraw();
			s_ready_lookup = 0;
			Sleep(250);
			continue;
		}

		const std::uintptr_t weapon = ResolveActiveWeapon(local_pawn);
		cat::CatalystUpdatePinHoldEdge(weapon);
		if (!cat::CatalystCanPredict(weapon, def_idx)) {
			ClearDraw();
			s_ready_lookup = 0;
			Sleep(120);
			continue;
		}

		const int wq_sleep = std::clamp(Settings::misc::workerQuality, 0, 2);
		const DWORD sleep_ms = (wq_sleep == 0) ? 30u : (wq_sleep == 2) ? 18u : 22u;
		Sleep(sleep_ms);

		const float throw_vel = ReadThrowVelocity(weapon);
		Vector3 view_origin{};
		Vector3 view_angles{};
		if (!cat::read_cview_render_origin_angles(view_origin, view_angles)) {
			if (!cat::GatherGrenadeThrowView(local_pawn, Vector3{}, view_origin, view_angles))
				continue;
		}

		Vector3 origin{};
		Vector3 velocity{};
		if (!cat::CatalystSetupThrow(weapon, local_pawn, throw_vel, view_origin, view_angles, origin, velocity))
			continue;

		Vector3 pawn_vel{};
		if (offsets::m_vecAbsVelocity && local_pawn)
			pawn_vel = g_GameMem.readv<Vector3>(local_pawn + static_cast<std::uintptr_t>(offsets::m_vecAbsVelocity));

		float strength = 1.f;
		bool pin_pulled = false;
		if (offsets::nade_m_flThrowStrength && offsets::nade_m_bPinPulled && weapon) {
			pin_pulled = g_GameMem.readv<bool>(weapon + static_cast<std::uintptr_t>(offsets::nade_m_bPinPulled));
			if (pin_pulled) {
				strength = std::clamp(
				    g_GameMem.readv<float>(weapon + static_cast<std::uintptr_t>(offsets::nade_m_flThrowStrength)), 0.f, 1.f);
				if (std::fabsf(strength - 0.5f) <= 0.1f)
					strength = 0.5f;
			}
		}

		const std::uint32_t draw_hash =
		    cat::HashTrajectoryInputs(def_idx, strength, view_angles, view_origin, pawn_vel);
		const std::uint32_t lookup_hash =
		    TrajectorySimLookupHash(def_idx, strength, view_angles, view_origin, pawn_vel);

		if (lookup_hash == s_ready_lookup && def_idx == s_ready_def) {
			{
				std::lock_guard<std::mutex> lk(g_draw.mtx);
				if (g_draw.active && g_draw.bvh_ready)
					g_draw.draw_hash = draw_hash;
			}
			Sleep(60);
			continue;
		}

		const int wq = std::clamp(Settings::misc::workerQuality, 0, 2);
		const DWORD throttle_ms_pin = (wq == 0) ? 100u : (wq == 2) ? 50u : 65u;
		const DWORD throttle_ms_hold = (wq == 0) ? 250u : (wq == 2) ? 140u : 180u;
		const DWORD throttle_ms = pin_pulled ? throttle_ms_pin : throttle_ms_hold;
		if (now_ms - s_last_sim_ms < throttle_ms && s_ready_lookup != 0)
			continue;

		cat::CatalystTrajectory traj{};
		
		const int sim_cap = (wq == 0) ? 140 : (wq == 2) ? 260 : 190;
		cat::CatalystSimulateFull(def_idx, origin, velocity, traj, sim_cap);
		if (!traj.valid || traj.points.size() < 2)
			continue;

		PublishBvhTraj(draw_hash, lookup_hash, def_idx, traj);
		s_ready_lookup = lookup_hash;
		s_ready_def = def_idx;
		s_last_sim_ms = now_ms;
	}
}

void EnsureWorker()
{
	if (g_worker_on.exchange(true))
		return;
	std::thread t(TrajectoryWorkerMain);
	const int wq = std::clamp(Settings::misc::workerQuality, 0, 2);
	const int prio = (wq == 0) ? THREAD_PRIORITY_LOWEST
	               : (wq == 2) ? THREAD_PRIORITY_NORMAL
	               :             THREAD_PRIORITY_BELOW_NORMAL;
	SetThreadPriority(t.native_handle(), prio);
	t.detach();
}

}  

void DrawFrame(const view_matrix_t& vm, std::uintptr_t local_pawn, const Vector3& eye_world)
{
	(void)local_pawn;
	(void)eye_world;
	if (!Settings::Visuals::grenadeHelper)
		return;

	EnsureWorker();

	std::vector<Vector3> pts;
	Vector3 end{};
	std::uint32_t hash = 0;
	{
		std::lock_guard<std::mutex> lk(g_draw.mtx);
		if (!g_draw.active || !g_draw.bvh_ready || g_draw.points.size() < 2)
			return;
		pts = g_draw.points;
		end = g_draw.end_pos;
		hash = g_draw.draw_hash;
	}

	cat::DrawTrajectoryPolylineCached(vm, pts, end, hash);
}

}  
