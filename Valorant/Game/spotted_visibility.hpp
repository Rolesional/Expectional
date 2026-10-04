#pragma once
#include <cstdint>

namespace UE4Structs {
struct Vector3;
}

uint64_t ReadSpottedMask(uintptr_t pawn);

bool ReadSpottedBool(uintptr_t pawn);

bool SpottedLikeAnanbaban(uintptr_t localPawn, uintptr_t targetPawn, uint32_t targetSpotIndexA, uint32_t targetSpotIndexB);

bool SpottedLikeAnanbabanWithMasks(uintptr_t localPawn, uintptr_t targetPawn, uint64_t localMask, uint64_t targetMask,
    uint32_t targetSpotIndexA, uint32_t targetSpotIndexB);

bool CrosshairEntityIsTargetPawn(uintptr_t localPawn, uintptr_t targetPawn);

bool ExpectionalBvhVisible(const UE4Structs::Vector3& local_eye_world, const UE4Structs::Vector3& target_point_world);

bool ExpectionalLosOrSpottedToTarget(uintptr_t localPawn, uintptr_t targetPawn, uint32_t targetSpotIndex);

bool ExpectionalBvhVisibleBetweenPawns(uintptr_t localPawn, uintptr_t targetPawn);

bool ExpectionalBvhAnyHitboxVisibleBetweenPawns(uintptr_t localPawn, uintptr_t targetPawn);

bool ExpectionalBvhVisibleSmoothed(uintptr_t stable_key, const UE4Structs::Vector3& local_eye_world,
    const UE4Structs::Vector3& target_point_world);

bool ExpectionalBvhAnyHitboxVisibleBetweenPawnsSmoothed(uintptr_t localPawn, uintptr_t targetPawn);

void ExpectionalBvhBudgetBeginFrame() noexcept;

bool ExpectionalTriggerVisibilityOk(uintptr_t localPawn, uintptr_t targetPawn, uint32_t spotIndex) noexcept;

bool ExpectionalCrosshairIsSmokeEntity(uintptr_t localPawn) noexcept;

bool ExpectionalCrosshairIsNonPlayerEntity(uintptr_t localPawn, uintptr_t crossPawn) noexcept;

bool ExpectionalLocalInsideActiveSmoke(uintptr_t localPawn) noexcept;

bool ExpectionalPawnInsideActiveSmoke(uintptr_t targetPawn) noexcept;

uintptr_t ExpectionalFindTriggerTargetBehindSmoke(uintptr_t localPawn, int localTeam, uint32_t* outEntitySlot) noexcept;

uintptr_t ExpectionalResolveTriggerTargetPawn(uintptr_t localPawn, int localTeam, int entIndex, uint32_t* outEntitySlot) noexcept;

void ExpectionalFillVisDebug(uintptr_t localPawn, uintptr_t targetPawn, uint32_t pawn_low, int entity_i, const char* nameUtf8);
