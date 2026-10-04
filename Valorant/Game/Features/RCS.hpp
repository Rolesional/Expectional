#pragma once
#include <cstdint>
#include "../weapon_runtime.hpp"

namespace RCS {
void ResetState();

void SyncBaseline(uintptr_t local_pawn);

bool TryReadAimPunch(uintptr_t local_pawn, float* apx, float* apy);

void Tick(uintptr_t local_pawn, const LegitCombatSettings& cfg, bool rcs_may_run,
	bool aimbot_hard_lock, bool spray_rcs_coop, bool ananbaban_merge);
}
