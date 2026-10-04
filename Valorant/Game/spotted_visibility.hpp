#pragma once
#include <cstdint>

namespace UE4Structs {
struct Vector3;
}

/** EntitySpottedState_t::m_bSpottedByMask (uint32[2]) @ +0xC */
uint64_t ReadSpottedMask(uintptr_t pawn);

/** Schema: m_bSpotted @ +8 */
bool ReadSpottedBool(uintptr_t pawn);

/**
 * BVH hazirsa: oyun spotted maskesi yerine mesh LOS (goz -> hedef birkac kemik) — engine maskesi gecikmeli.
 * BVH yoksa: ananbaban mask (targetMask & (1<<localIdx)) || (localMask & (1<<targetIdx)) + m_bSpotted fallback.
 */
bool SpottedLikeAnanbaban(uintptr_t localPawn, uintptr_t targetPawn, uint32_t targetSpotIndexA, uint32_t targetSpotIndexB);

/** Yalnizca mask yolu (BVH yok); maskeler cagrida bir kez okunmus olmali. */
bool SpottedLikeAnanbabanWithMasks(uintptr_t localPawn, uintptr_t targetPawn, uint64_t localMask, uint64_t targetMask,
    uint32_t targetSpotIndexA, uint32_t targetSpotIndexB);

/**
 * Yerel pawn + m_iIDEntIndex -> entity cozumu; crosshair entity zinciri.
 */
bool CrosshairEntityIsTargetPawn(uintptr_t localPawn, uintptr_t targetPawn);

/** Dunya BVH: gozden hedef noktaya LOS (duvar yok). Gecerli BVH yoksa false (espLoop'ta LOS renkleri + spotted fallback ayri). */
bool ExpectionalBvhVisible(const UE4Structs::Vector3& local_eye_world, const UE4Structs::Vector3& target_point_world);

/** Trigger / LOS: SpottedLikeAnanbaban (BVH mesh veya mask fallback). */
bool ExpectionalLosOrSpottedToTarget(uintptr_t localPawn, uintptr_t targetPawn, uint32_t targetSpotIndex);

/** Yerel HEAD ~ goz, hedef HEAD; BVH LOS. */
bool ExpectionalBvhVisibleBetweenPawns(uintptr_t localPawn, uintptr_t targetPawn);

/** Yerel goz -> hedef HEAD / NECK / CHEST / PELVIS; herhangi birine BVH LOS aciksa true. */
bool ExpectionalBvhAnyHitboxVisibleBetweenPawns(uintptr_t localPawn, uintptr_t targetPawn);

/**
 * BVH LOS + kisa gecikme (save_fps / kemik jitter'da tek kare flip yutmak icin).
 * stable_key: hedef pawn; 0 ise hysteresis yok.
 */
bool ExpectionalBvhVisibleSmoothed(uintptr_t stable_key, const UE4Structs::Vector3& local_eye_world,
    const UE4Structs::Vector3& target_point_world);

/** Multi-hitbox LOS + save_fps smoothing (aim visible). */
bool ExpectionalBvhAnyHitboxVisibleBetweenPawnsSmoothed(uintptr_t localPawn, uintptr_t targetPawn);

/** espLoop basinda: save_fps iken kare basina sinirli taze BVH trace. */
void ExpectionalBvhBudgetBeginFrame() noexcept;

/** Tetik gorunurluk: BVH LOS, spotted mask, sis icin crosshair-on-target fallback. */
bool ExpectionalTriggerVisibilityOk(uintptr_t localPawn, uintptr_t targetPawn, uint32_t spotIndex) noexcept;

/** Crosshair sis / smokegrenade projectile uzerinde mi. */
bool ExpectionalCrosshairIsSmokeEntity(uintptr_t localPawn) noexcept;

/** Crosshair gecerli ama oyuncu pawn degil (sis, nade, world). */
bool ExpectionalCrosshairIsNonPlayerEntity(uintptr_t localPawn, uintptr_t crossPawn) noexcept;

/** Yerel pawn aktif sis volumu icinde mi (m_bWaitForNoAttack bypass icin). */
bool ExpectionalLocalInsideActiveSmoke(uintptr_t localPawn) noexcept;

/** Hedef pawn aktif sis volumu icinde mi. */
bool ExpectionalPawnInsideActiveSmoke(uintptr_t targetPawn) noexcept;

/**
 * Yalnizca crosshair sis entity: nişan hattinda dusman; BVH duvar keser, sis mesh yok.
 */
uintptr_t ExpectionalFindTriggerTargetBehindSmoke(uintptr_t localPawn, int localTeam, uint32_t* outEntitySlot) noexcept;

/** Normal: iIDEntIndex -> dusman. Sis: yalniz crosshair sis + dar nişan + BVH/spotted. */
uintptr_t ExpectionalResolveTriggerTargetPawn(uintptr_t localPawn, int localTeam, int entIndex, uint32_t* outEntitySlot) noexcept;

/** g_visDbg: ornek dusman (ilk gecerli oyuncu) + crosshair / offset ozeti. */
void ExpectionalFillVisDebug(uintptr_t localPawn, uintptr_t targetPawn, uint32_t pawn_low, int entity_i, const char* nameUtf8);
