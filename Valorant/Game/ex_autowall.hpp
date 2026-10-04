#pragma once

#include "structs.hpp"
#include "offsets_runtime.hpp"
#include "catalyst_world_bvh.hpp"
#include "../Driver/driver.hpp"
#include "entity_handle.hpp"
#include "esp_extras.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ex_autowall {

using Vector3 = UE4Structs::Vector3;

namespace vdata_off {
constexpr std::ptrdiff_t kWeaponType = 0x520;
constexpr std::ptrdiff_t kDamage = 0x828;
constexpr std::ptrdiff_t kHeadshotMult = 0x82C;
constexpr std::ptrdiff_t kArmorRatio = 0x830;
constexpr std::ptrdiff_t kPenetration = 0x834;
constexpr std::ptrdiff_t kRange = 0x838;
constexpr std::ptrdiff_t kRangeModifier = 0x83C;
}

struct WeaponData {
    float damage = 0.f;
    float penetration = 0.f;
    float range_modifier = 0.f;
    float range = 0.f;
    float armor_ratio = 0.f;
    float headshot_multiplier = 0.f;
    int weapon_type = 0;
    bool valid = false;
};

inline bool IsWeaponValid(int weapon_type) noexcept {
    return weapon_type >= 1 && weapon_type <= 6;
}

inline uintptr_t ResolveActiveWeapon(uintptr_t local_pawn) noexcept {
    if (!local_pawn || !offsets::m_pWeaponServices || !offsets::m_hActiveWeapon)
        return 0;
    const uintptr_t ws = g_GameMem.readv<uintptr_t>(local_pawn + static_cast<uintptr_t>(offsets::m_pWeaponServices));
    if (!ws)
        return 0;
    const uint32_t h = g_GameMem.readv<uint32_t>(ws + static_cast<uintptr_t>(offsets::m_hActiveWeapon));
    if (!h || h == 0xFFFFFFFFu)
        return 0;
    return ex_entity::ResolveHandle(h);
}

inline uintptr_t ResolveWeaponVData(uintptr_t weapon) noexcept {
    if (!weapon || !offsets::entity_m_nSubclassID)
        return 0;
    return g_GameMem.readv<uintptr_t>(weapon + static_cast<uintptr_t>(offsets::entity_m_nSubclassID) + 0x8);
}

inline WeaponData ReadWeaponVData(uintptr_t local_pawn) noexcept {
    WeaponData out{};
    const uintptr_t wpn = ResolveActiveWeapon(local_pawn);
    if (!wpn)
        return out;
    const uintptr_t vdata = ResolveWeaponVData(wpn);
    if (vdata < 0x10000ull)
        return out;
    const int dmg_i = g_GameMem.readv<int>(vdata + vdata_off::kDamage);
    out.damage = static_cast<float>(dmg_i);
    out.headshot_multiplier = g_GameMem.readv<float>(vdata + vdata_off::kHeadshotMult);
    out.armor_ratio = g_GameMem.readv<float>(vdata + vdata_off::kArmorRatio);
    out.penetration = g_GameMem.readv<float>(vdata + vdata_off::kPenetration);
    out.range = g_GameMem.readv<float>(vdata + vdata_off::kRange);
    out.range_modifier = g_GameMem.readv<float>(vdata + vdata_off::kRangeModifier);
    out.weapon_type = g_GameMem.readv<int>(vdata + vdata_off::kWeaponType);
    if (!(out.damage > 0.f) || !(out.range > 0.f))
        return WeaponData{};
    if (!std::isfinite(out.range_modifier) || out.range_modifier <= 0.f || out.range_modifier > 1.f)
        out.range_modifier = 0.98f;
    if (!std::isfinite(out.penetration) || out.penetration < 0.f)
        out.penetration = 0.f;
    if (!std::isfinite(out.armor_ratio) || out.armor_ratio <= 0.f)
        out.armor_ratio = 1.f;
    if (!std::isfinite(out.headshot_multiplier) || out.headshot_multiplier <= 0.f)
        out.headshot_multiplier = 4.f;
    out.valid = true;
    return out;
}

struct StaticStats {
    std::uint16_t def;
    float dmg;
    float hs;
    float armor;
    float pen;
    float range;
    float rmod;
    int wtype;
};

inline const StaticStats* FindStaticStats(std::uint16_t def) noexcept {
    static constexpr StaticStats kTable[] = {
        {  1,  53.f, 4.f, 1.50f, 2.5f, 4096.f, 0.81f, 1 },
        {  2,  38.f, 4.f, 1.00f, 1.0f, 4096.f, 0.85f, 1 },
        {  3,  32.f, 4.f, 1.30f, 1.5f, 4096.f, 0.85f, 1 },
        {  4,  28.f, 4.f, 0.80f, 1.0f, 4096.f, 0.85f, 1 },
        {  7,  36.f, 4.f, 1.55f, 2.5f, 8192.f, 0.98f, 3 },
        {  8,  33.f, 4.f, 1.40f, 2.5f, 8192.f, 0.98f, 3 },
        {  9, 115.f, 4.f, 1.90f, 3.0f, 8192.f, 0.99f, 5 },
        { 10,  30.f, 4.f, 1.40f, 2.0f, 8192.f, 0.98f, 3 },
        { 11,  80.f, 4.f, 1.50f, 2.5f, 8192.f, 0.98f, 5 },
        { 13,  30.f, 4.f, 1.40f, 2.0f, 8192.f, 0.98f, 3 },
        { 14,  32.f, 4.f, 1.30f, 2.0f, 8192.f, 0.98f, 6 },
        { 16,  33.f, 4.f, 1.40f, 2.5f, 8192.f, 0.98f, 3 },
        { 17,  29.f, 4.f, 1.10f, 1.0f, 4096.f, 0.87f, 2 },
        { 19,  26.f, 4.f, 1.00f, 1.0f, 4096.f, 0.84f, 2 },
        { 23,  27.f, 4.f, 1.10f, 1.0f, 4096.f, 0.87f, 2 },
        { 24,  35.f, 4.f, 1.30f, 2.0f, 4096.f, 0.85f, 2 },
        { 25,  20.f, 4.f, 0.80f, 1.0f, 3000.f, 0.70f, 4 },
        { 26,  27.f, 4.f, 1.00f, 1.0f, 4096.f, 0.85f, 2 },
        { 27,  30.f, 4.f, 0.80f, 1.0f, 3000.f, 0.75f, 4 },
        { 28,  35.f, 4.f, 1.30f, 2.0f, 8192.f, 0.98f, 6 },
        { 29,  32.f, 4.f, 0.80f, 1.0f, 3000.f, 0.75f, 4 },
        { 30,  33.f, 4.f, 1.00f, 1.0f, 4096.f, 0.85f, 1 },
        { 32,  35.f, 4.f, 1.00f, 1.0f, 4096.f, 0.85f, 1 },
        { 33,  29.f, 4.f, 1.10f, 1.0f, 4096.f, 0.87f, 2 },
        { 34,  25.f, 4.f, 1.00f, 1.0f, 4096.f, 0.87f, 2 },
        { 35,  26.f, 4.f, 0.80f, 1.0f, 3000.f, 0.75f, 4 },
        { 36,  35.f, 4.f, 1.00f, 1.0f, 4096.f, 0.85f, 1 },
        { 38,  80.f, 4.f, 1.50f, 2.5f, 8192.f, 0.98f, 5 },
        { 39,  30.f, 4.f, 1.40f, 2.5f, 8192.f, 0.98f, 3 },
        { 40,  88.f, 4.f, 1.70f, 2.5f, 8192.f, 0.98f, 5 },
        { 60,  38.f, 4.f, 1.40f, 2.5f, 8192.f, 0.98f, 3 },
        { 61,  35.f, 4.f, 1.00f, 1.0f, 4096.f, 0.85f, 1 },
        { 63,  31.f, 4.f, 1.00f, 1.0f, 4096.f, 0.85f, 1 },
        { 64,  86.f, 4.f, 1.50f, 2.0f, 4096.f, 0.81f, 1 },
    };
    for (const auto& s : kTable) {
        if (s.def == def)
            return &s;
    }
    return nullptr;
}

inline WeaponData ReadWeaponData(uintptr_t local_pawn) noexcept {
    const WeaponData vd = ReadWeaponVData(local_pawn);
    if (vd.valid && vd.damage >= 1.f && vd.damage <= 200.f &&
        vd.range >= 100.f && vd.range <= 20000.f &&
        vd.range_modifier > 0.3f && vd.range_modifier <= 1.f &&
        vd.penetration >= 0.f && vd.penetration <= 5.f)
        return vd;
    WeaponData out{};
    if (!local_pawn)
        return out;
    const std::uint16_t def = ex_esp::ReadWeaponDefIndex(local_pawn);
    const StaticStats* s = def ? FindStaticStats(def) : nullptr;
    if (!s)
        return out;
    out.damage = s->dmg;
    out.headshot_multiplier = s->hs;
    out.armor_ratio = s->armor;
    out.penetration = s->pen;
    out.range = s->range;
    out.range_modifier = s->rmod;
    out.weapon_type = s->wtype;
    out.valid = true;
    return out;
}

inline void ScaleDamage(int hitgroup, int armor, bool has_helmet, int team,
    float armor_ratio, float headshot_multiplier, float& damage) noexcept {
    constexpr float kHeadScale = 1.0f;
    constexpr float kBodyScale = 1.0f;
    const bool is_ct = (team == 3);
    (void)is_ct;
    const float head_scale = kHeadScale;
    const float body_scale = kBodyScale;

    switch (hitgroup) {
    case 1: damage *= headshot_multiplier * head_scale; break;
    case 2:
    case 4:
    case 5:
    case 8: damage *= body_scale; break;
    case 3: damage *= 1.25f * body_scale; break;
    case 6:
    case 7: damage *= 0.75f * body_scale; break;
    default: break;
    }

    const bool is_head = (hitgroup == 1);
    const bool is_armored = (hitgroup >= 1 && hitgroup <= 5) || (hitgroup == 8);
    if (armor <= 0 || !is_armored || (is_head && !has_helmet)) {
        damage = std::floor(damage);
        return;
    }
    constexpr float kArmorBonus = 0.5f;
    const float armor_ratio_scaled = armor_ratio * 0.5f;
    float damage_to_health = damage * armor_ratio_scaled;
    const float damage_to_armor = (damage - damage_to_health) * kArmorBonus;
    if (damage_to_armor > static_cast<float>(armor))
        damage_to_health = damage - (static_cast<float>(armor) / kArmorBonus);
    damage = std::floor(damage_to_health);
}

inline float GetMaxDamage(int hitgroup, int armor, bool has_helmet, int team, const WeaponData& wd) noexcept {
    if (!wd.valid || wd.damage <= 0.f)
        return 0.f;
    float dmg = wd.damage;
    ScaleDamage(hitgroup, armor, has_helmet, team, wd.armor_ratio, wd.headshot_multiplier, dmg);
    return dmg;
}

inline float ApplyRangeFalloff(float damage, float dist, const WeaponData& wd) noexcept {
    if (damage <= 0.f || wd.range_modifier <= 0.f)
        return damage;
    if (!(dist > 0.f))
        return damage;
    return damage * std::pow(wd.range_modifier, dist / 500.0f);
}

inline bool IsVisibleCatalystStyle(const Vector3& eye, const Vector3& target) noexcept {
    if (!ex_world_bvh::g_world_bvh.valid())
        return false;
    const auto tr = ex_world_bvh::g_world_bvh.trace_ray(eye, target);
    return !tr.hit || tr.fraction > 0.97f;
}

inline int ReadTargetArmor(uintptr_t pawn) noexcept {
    if (!pawn || !offsets::m_ArmorValue)
        return 0;
    const int a = g_GameMem.readv<int>(pawn + static_cast<uintptr_t>(offsets::m_ArmorValue));
    return (a < 0 || a > 100) ? 0 : a;
}

inline bool ReadTargetHelmet(uintptr_t pawn, uintptr_t controller) noexcept {
    if (pawn) {
        constexpr std::ptrdiff_t kItemServices = 0x1210;
        const uintptr_t svc = g_GameMem.readv<uintptr_t>(pawn + static_cast<uintptr_t>(kItemServices));
        if (svc >= 0x10000ull) {
            const uint8_t h = g_GameMem.readv<uint8_t>(svc + 0x49);
            if (h == 0 || h == 1)
                return h != 0;
        }
    }
    if (controller) {
        constexpr std::ptrdiff_t kPawnHasHelmet = 0x929;
        const uint8_t h = g_GameMem.readv<uint8_t>(controller + static_cast<uintptr_t>(kPawnHasHelmet));
        if (h == 0 || h == 1)
            return h != 0;
    }
    return false;
}

struct PenResult {
    float damage = 0.f;
    bool penetrated = false;
    bool ok = false;
};

inline PenResult RunPenetration(const Vector3& eye, const Vector3& target, int hitgroup,
    int armor, bool has_helmet, int team, const WeaponData& wd) noexcept {
    PenResult out{};
    if (!wd.valid || wd.damage <= 0.f || wd.range <= 0.f)
        return out;
    const float dx = target.x - eye.x;
    const float dy = target.y - eye.y;
    const float dz = target.z - eye.z;
    const float dist_to_target = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(dist_to_target > 0.f) || dist_to_target > wd.range)
        return out;
    if (!ex_world_bvh::g_world_bvh.valid())
        return out;

    const float inv = 1.f / dist_to_target;
    const Vector3 dir(dx * inv, dy * inv, dz * inv);
    const Vector3 ray_end(eye.x + dir.x * wd.range, eye.y + dir.y * wd.range, eye.z + dir.z * wd.range);

    const auto all_hits = ex_world_bvh::g_world_bvh.trace_ray_all(eye, ray_end);
    const auto segments = ex_world_bvh::g_world_bvh.build_segments(all_hits, wd.range);

    float current_damage = wd.damage;
    int penetration_count = 4;

    const float first_wall = segments.empty() ? wd.range : segments[0].enter_distance;
    auto damage_at_target = [&]() -> float {
        float d = current_damage * std::pow(wd.range_modifier, dist_to_target / 500.0f);
        if (d < 1.f)
            return 0.f;
        ScaleDamage(hitgroup, armor, has_helmet, team, wd.armor_ratio, wd.headshot_multiplier, d);
        return d;
    };

    if (dist_to_target < first_wall) {
        const float d = damage_at_target();
        if (d < 1.f)
            return out;
        out.damage = d;
        out.penetrated = false;
        out.ok = true;
        return out;
    }

    for (size_t si = 0; si < segments.size(); ++si) {
        const auto& seg = segments[si];
        if (seg.enter_distance >= dist_to_target)
            break;

        float pen_mod = seg.min_pen_mod;
        const auto enter_type = seg.enter_surface.surface_type;
        const auto exit_type = seg.exit_surface.surface_type;
        if (enter_type != exit_type)
            pen_mod = (std::min)(pen_mod, seg.exit_surface.penetration);
        const float thickness = seg.thickness;
        if (seg.exit_distance > 3000.0f || pen_mod < 0.1f)
            penetration_count = 0;
        if (penetration_count <= 0)
            return out;

        float damage_modifier = 0.16f;
        if (pen_mod >= 0.1f && enter_type == exit_type) {
            if (((enter_type - 85) & 0xFFFFFFFD) == 0)
                pen_mod = 3.0f;
            else if (enter_type == 76)
                pen_mod = 2.0f;
            if (thickness < 6.0f) {
                if (enter_type == 71 || enter_type == 89) {
                    damage_modifier = 0.05f;
                    pen_mod = 3.0f;
                }
            }
        }
        const float inv_pen = 1.0f / pen_mod;
        const float base_loss = damage_modifier * current_damage;
        const float pen_loss = (std::max)(0.0f, (3.0f / (wd.penetration > 0.f ? wd.penetration : 0.001f)) * 1.25f) * (inv_pen * 3.0f);
        const float dist_loss = (thickness * thickness * inv_pen) / 24.0f;
        current_damage -= (base_loss + pen_loss + dist_loss);
        if (current_damage < 1.f)
            return out;
        --penetration_count;

        const float next_wall = (si + 1 < segments.size()) ? segments[si + 1].enter_distance : wd.range;
        if (dist_to_target < next_wall) {
            const float d = damage_at_target();
            if (d < 1.f)
                return out;
            out.damage = d;
            out.penetrated = true;
            out.ok = true;
            return out;
        }
    }
    return out;
}

inline bool CanPenetrateDirection(const Vector3& eye, const Vector3& dir, const WeaponData& wd, float& out_damage) noexcept {
    out_damage = 0.f;
    if (!wd.valid || wd.damage <= 0.f || wd.range <= 0.f || wd.penetration <= 0.f)
        return false;
    if (!ex_world_bvh::g_world_bvh.valid())
        return false;
    const Vector3 ray_end(eye.x + dir.x * wd.range, eye.y + dir.y * wd.range, eye.z + dir.z * wd.range);
    const auto first_hit = ex_world_bvh::g_world_bvh.trace_ray(eye, ray_end);
    if (!first_hit.hit)
        return false;
    const auto all_hits = ex_world_bvh::g_world_bvh.trace_ray_all(eye, ray_end);
    const auto segments = ex_world_bvh::g_world_bvh.build_segments(all_hits, wd.range);
    if (segments.empty()) {
        if (first_hit.surface.penetration >= 0.1f) {
            out_damage = wd.damage;
            return true;
        }
        return false;
    }
    const auto& seg = segments[0];
    float pen_mod = seg.min_pen_mod;
    if (seg.enter_surface.surface_type != seg.exit_surface.surface_type)
        pen_mod = (std::min)(pen_mod, seg.exit_surface.penetration);
    if (seg.exit_distance > 3000.0f || pen_mod < 0.1f)
        return false;
    float damage_modifier = 0.16f;
    const auto enter_type = seg.enter_surface.surface_type;
    const auto exit_type = seg.exit_surface.surface_type;
    if (pen_mod >= 0.1f && enter_type == exit_type) {
        if (((enter_type - 85) & 0xFFFFFFFD) == 0)
            pen_mod = 3.0f;
        else if (enter_type == 76)
            pen_mod = 2.0f;
        if (seg.thickness < 6.0f && (enter_type == 71 || enter_type == 89)) {
            damage_modifier = 0.05f;
            pen_mod = 3.0f;
        }
    }
    const float inv_pen = 1.0f / pen_mod;
    const float base_loss = damage_modifier * wd.damage;
    const float pen_loss = (std::max)(0.0f, (3.0f / wd.penetration) * 1.25f) * (inv_pen * 3.0f);
    const float dist_loss = (seg.thickness * seg.thickness * inv_pen) / 24.0f;
    const float remaining = wd.damage - (base_loss + pen_loss + dist_loss);
    if (remaining < 1.f)
        return false;
    out_damage = remaining;
    return true;
}

inline Vector3 AnglesToForward(const Vector3& ang_deg) noexcept {
    constexpr float kDeg = 3.14159265f / 180.f;
    const float sp = std::sin(ang_deg.x * kDeg);
    const float cp = std::cos(ang_deg.x * kDeg);
    const float sy = std::sin(ang_deg.y * kDeg);
    const float cy = std::cos(ang_deg.y * kDeg);
    return Vector3(cp * cy, cp * sy, -sp);
}

inline int HitgroupForBoneIdx(int bone_idx) noexcept {
    if (bone_idx == 7)
        return 1;
    if (bone_idx == 6)
        return 2;
    return 3;
}

inline Vector3 ReadEyePos(uintptr_t pawn) noexcept {
    if (pawn && offsets::m_pGameSceneNode && offsets::m_boneArrayFromScene) {
        const uintptr_t gs = g_GameMem.readv<uintptr_t>(pawn + static_cast<uintptr_t>(offsets::m_pGameSceneNode));
        if (gs >= 0x10000ull) {
            const uint64_t ba = g_GameMem.readv<uint64_t>(gs + static_cast<uintptr_t>(offsets::m_boneArrayFromScene));
            if (ba >= 0x10000ull) {
                const Vector3 h = g_GameMem.readv<Vector3>(ba + static_cast<uintptr_t>(7 * 0x20));
                if (h.x != 0.f || h.y != 0.f || h.z != 0.f)
                    return h;
            }
        }
    }
    if (pawn && offsets::m_vecOrigin) {
        const Vector3 o = g_GameMem.readv<Vector3>(pawn + static_cast<uintptr_t>(offsets::m_vecOrigin));
        return Vector3(o.x, o.y, o.z + 75.f);
    }
    return {};
}

inline Vector3 ReadBonePoint(uintptr_t pawn, int bone_idx, float fallback_z) noexcept {
    if (pawn && offsets::m_pGameSceneNode && offsets::m_boneArrayFromScene && bone_idx >= 0 && bone_idx < 64) {
        const uintptr_t gs = g_GameMem.readv<uintptr_t>(pawn + static_cast<uintptr_t>(offsets::m_pGameSceneNode));
        if (gs >= 0x10000ull) {
            const uint64_t ba = g_GameMem.readv<uint64_t>(gs + static_cast<uintptr_t>(offsets::m_boneArrayFromScene));
            if (ba >= 0x10000ull) {
                const Vector3 v = g_GameMem.readv<Vector3>(ba + static_cast<uintptr_t>(bone_idx * 0x20));
                if (v.x != 0.f || v.y != 0.f || v.z != 0.f)
                    return v;
            }
        }
    }
    if (pawn && offsets::m_vecOrigin) {
        const Vector3 o = g_GameMem.readv<Vector3>(pawn + static_cast<uintptr_t>(offsets::m_vecOrigin));
        return Vector3(o.x, o.y, o.z + fallback_z);
    }
    return {};
}

inline void ReadAimSpheres(uintptr_t pawn, Vector3 out_pts[3]) noexcept {
    out_pts[0] = ReadBonePoint(pawn, 7, 75.f);
    out_pts[1] = ReadBonePoint(pawn, 6, 63.f);
    out_pts[2] = ReadBonePoint(pawn, 1, 36.f);
}

inline float SphereRadiusFor(int slot) noexcept {
    return slot == 0 ? 5.0f : (slot == 1 ? 4.5f : 6.0f);
}

inline bool RayHitSphere(const Vector3& eye, const Vector3& dir, const Vector3& c, float r, float& out_t) noexcept {
    const float ox = c.x - eye.x, oy = c.y - eye.y, oz = c.z - eye.z;
    const float tca = ox * dir.x + oy * dir.y + oz * dir.z;
    if (tca < 0.f)
        return false;
    const float d2 = ox * ox + oy * oy + oz * oz - tca * tca;
    const float r2 = r * r;
    if (d2 > r2)
        return false;
    const float thc = std::sqrt(r2 - d2);
    float t = tca - thc;
    if (t < 0.f)
        t = tca + thc;
    if (t <= 0.f)
        return false;
    out_t = t;
    return true;
}

inline bool PointFireOk(const Vector3& eye, const Vector3& pt, int hitgroup,
    int armor, bool has_helmet, int team, const WeaponData& wd, bool use_autowall, float min_damage) noexcept {
    if (!wd.valid)
        return true;
    if (!ex_world_bvh::g_world_bvh.valid())
        return true;
    if (IsVisibleCatalystStyle(eye, pt))
        return true;
    if (!use_autowall)
        return false;
    const auto pen = RunPenetration(eye, pt, hitgroup, armor, has_helmet, team, wd);
    return pen.ok && pen.damage >= min_damage;
}

inline bool AnyBoneFireOk(const Vector3& eye, uintptr_t pawn, uintptr_t controller,
    const WeaponData& wd, bool use_autowall, float min_damage) noexcept {
    Vector3 pts[3];
    ReadAimSpheres(pawn, pts);
    const int armor = ReadTargetArmor(pawn);
    const bool helmet = ReadTargetHelmet(pawn, controller);
    int team = 0;
    if (offsets::m_iTeamNum)
        team = g_GameMem.readv<int>(pawn + offsets::m_iTeamNum) & 0xFF;
    static const int kHg[3] = { 1, 2, 3 };
    for (int i = 0; i < 3; ++i) {
        if (pts[i].IsZero())
            continue;
        if (PointFireOk(eye, pts[i], kHg[i], armor, helmet, team, wd, use_autowall, min_damage))
            return true;
    }
    return false;
}

struct ScanHit {
    bool found = false;
    uintptr_t pawn = 0;
    uintptr_t controller = 0;
    std::uint32_t spot_idx = 0;
    float t = 0.f;
};

inline ScanHit ScanEnemyOnRay(const Vector3& eye, const Vector3& dir,
    const std::vector<UE4Structs::CS2Entity>& players, uintptr_t local_pawn, int local_team,
    bool head_only = false) noexcept {
    ScanHit out{};
    if (!local_pawn || !offsets::m_iHealth || !offsets::m_iTeamNum)
        return out;
    float best_t = 1e30f;
    for (const auto& e : players) {
        uintptr_t pawn = e.Actor;
        if (e.Controller && offsets::dwPlayerPawn) {
            const std::uint32_t h = g_GameMem.readv<std::uint32_t>(e.Controller + static_cast<uintptr_t>(offsets::dwPlayerPawn));
            const uintptr_t cur = ex_entity::ResolveHandle(h);
            if (cur >= 0x10000ull)
                pawn = cur;
            else
                continue;
        }
        if (!pawn || pawn == local_pawn)
            continue;
        const int hp = g_GameMem.readv<int>(pawn + offsets::m_iHealth);
        if (hp <= 0 || hp > 100)
            continue;
        if (offsets::m_bPawnIsAlive && e.Controller) {
            if (g_GameMem.readv<int>(e.Controller + static_cast<uintptr_t>(offsets::m_bPawnIsAlive)) != 1)
                continue;
        }
        const int pt = g_GameMem.readv<int>(pawn + offsets::m_iTeamNum) & 0xFF;
        if (Settings::Visuals::enemiesOnly && pt == (local_team & 0xFF))
            continue;
        Vector3 pts[3];
        ReadAimSpheres(pawn, pts);
        const int i_end = head_only ? 1 : 3;
        for (int i = 0; i < i_end; ++i) {
            if (pts[i].IsZero())
                continue;
            float t = 0.f;
            if (!RayHitSphere(eye, dir, pts[i], SphereRadiusFor(i), t))
                continue;
            if (t < best_t) {
                best_t = t;
                out.found = true;
                out.pawn = pawn;
                out.controller = e.Controller;
                out.spot_idx = static_cast<std::uint32_t>(e.entity_index);
                out.t = t;
            }
        }
    }
    return out;
}

}
