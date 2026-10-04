#pragma once
/**
 * Catalyst (external) dunya mesh BVH + ray trace — gorunurluk LOS icin.
 * Implementasyon: catalyst_world_bvh.cpp (Kaynak: Catalyst systems::bvh)
 */
#include "structs.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <shared_mutex>
#include <vector>

namespace ex_world_bvh {

/** structs.hpp: Vector3 yalnizca UE4Structs icinde; MSVC bos isim ararken uyeler cozulmez. */
using Vector3 = UE4Structs::Vector3;

class bvh {
public:
	struct surface_info {
		float penetration{};
		std::uint16_t surface_type{};
		std::uint8_t global_index = 255;
	};

	struct global_surface_entry {
		float unk_00{};
		float unk_04{};
		float penetration_mod{};
		float unk_0C{};
		float unk_10{};
		std::uint16_t surface_type{};
		std::uint16_t pad{};
		std::uint8_t pad2[8]{};
	};

	struct triangle {
		Vector3 v0{};
		Vector3 v1{};
		Vector3 v2{};
		surface_info surface{};
	};

	struct trace_result {
		bool hit{};
		float fraction{};
		float distance{};
		Vector3 end_pos{};
		Vector3 normal{};
		surface_info surface{};
		std::int32_t triangle_index = -1;
	};

	struct hit_entry {
		float distance{};
		float fraction{};
		Vector3 position{};
		Vector3 normal{};
		surface_info surface{};
		std::int32_t triangle_index = -1;
		bool is_enter = true;
	};

	struct penetration_segment {
		float enter_fraction{};
		float exit_fraction{};
		float enter_distance{};
		float exit_distance{};
		Vector3 enter_pos{};
		Vector3 exit_pos{};
		surface_info enter_surface{};
		surface_info exit_surface{};
		float thickness{};
		float min_pen_mod{};
	};

	void parse();
	void clear();

	void load_triangles( std::vector<triangle> triangles )
	{
		for ( auto& t : triangles )
		{
			if ( !( t.surface.penetration > 0.f ) )
				t.surface.penetration = 1.0f;
		}
		std::unique_lock lock( m_mutex );
		m_triangles = std::move( triangles );
		rebuild_accel_unlocked( );
	}

	[[nodiscard]] trace_result trace_ray(const Vector3& start, const Vector3& end,
		std::int32_t exclude_tri = -1) const;
	[[nodiscard]] std::vector<hit_entry> trace_ray_all(const Vector3& start, const Vector3& end) const;
	[[nodiscard]] std::vector<penetration_segment> build_segments(const std::vector<hit_entry>& hits,
		float ray_length) const;

	[[nodiscard]] const std::vector<triangle>& triangles() const;
	[[nodiscard]] std::size_t count() const;
	[[nodiscard]] bool valid() const;

private:
	struct aabb {
		float mins[3]{ 1e12f, 1e12f, 1e12f };
		float maxs[3]{ -1e12f, -1e12f, -1e12f };

		void expand(const Vector3& p);
		void expand(const aabb& o);
		[[nodiscard]] int longest_axis() const;
		[[nodiscard]] bool intersects_ray(const float origin[3], const float inv_dir[3], float max_t) const;
	};

	struct bvh_node {
		aabb bounds{};
		std::int32_t left = -1;
		std::int32_t right = -1;
		std::int32_t tri_start{};
		std::int32_t tri_count{};
	};

	/** m_mutex unique_lock altinda cagir. */
	void rebuild_accel_unlocked();
	std::int32_t build_recursive(std::int32_t start, std::int32_t end, std::int32_t depth);

	std::vector<triangle> m_triangles{};
	mutable std::shared_mutex m_mutex{};

	std::vector<bvh_node> m_nodes{};
	std::vector<std::int32_t> m_indices{};
	std::vector<aabb> m_tri_bounds{};
	std::vector<float> m_centroids{};

	static constexpr auto k_max_leaf_tris = 8;
	static constexpr auto k_max_depth = 48;
};

inline bvh g_world_bvh{};

/** Tek seferlik arka plan .tri yukleme thread'i (VPK export yalnizca dosya yoksa). */
void TickWorldBvhParse();

/** Visible check + grenade trajectory: ayni g_world_bvh mesh'i. */
inline bool WorldMeshReady() noexcept
{
	return g_world_bvh.valid() && g_world_bvh.count() > 0;
}

void EnsureWorldBvhLoadThread() noexcept;

/** Mevcut harita .tri dosyasini aninda yukle (tekrar VPK parse etmez). */
bool TryLoadWorldMeshNow() noexcept;

/** Debug accessors (sol ust paneli). */
uintptr_t DbgClientModule();
uintptr_t DbgVphysModule();
uintptr_t DbgPatternTrace();
uintptr_t DbgPatternSurf();
uintptr_t DbgVphys2World();
int       DbgBodyCount();
uint32_t  DbgWorldVtableFound();
uint32_t  DbgParseAttempts();
uint32_t  DbgParseSuccess();
int       DbgLastExtracted();

/**
 * Mesh yuklu varsayimiyla LOS (valid()/shared_lock YOK — esp basinda mesh_ready set edilir).
 */
inline bool LosClearToImpl(const Vector3& eye_world, const Vector3& target_world) {
	const float dx = target_world.x - eye_world.x;
	const float dy = target_world.y - eye_world.y;
	const float dz = target_world.z - eye_world.z;
	const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
	if (len < 4.f)
		return true;
	const float inv = 1.f / len;
	const float fx = dx * inv;
	const float fy = dy * inv;
	const float fz = dz * inv;
	/** Oyuncu ic geometri / yanlis bone sapmalari icin guvenli bosluk (hammer birim). */
	const float k_from_eye = 18.f;
	const float k_before_target = 38.f;
	if (len <= k_from_eye + k_before_target + 12.f)
		return true;
	const Vector3 ray_start(
		eye_world.x + fx * k_from_eye,
		eye_world.y + fy * k_from_eye,
		eye_world.z + fz * k_from_eye);
	const Vector3 ray_end(
		target_world.x - fx * k_before_target,
		target_world.y - fy * k_before_target,
		target_world.z - fz * k_before_target);
	return !g_world_bvh.trace_ray(ray_start, ray_end).hit;
}

/**
 * Gozden hedefe LOS: ray'de isabet yoksa gorunur.
 * Yerel capsule / hedef govdesine takilmayi azaltmak icin bas ve son padlenir.
 */
inline bool LosClearTo(const Vector3& eye_world, const Vector3& target_world) {
	if (!g_world_bvh.valid())
		return false;
	return LosClearToImpl(eye_world, target_world);
}

} // namespace ex_world_bvh
