#pragma once
#include <cstdint>
#include "../weapon_runtime.hpp"

namespace RCS {
void ResetState();
/** Punch tabanini su anki degere ceker, bekleyen asagi hareketi siler. */
void SyncBaseline(uintptr_t local_pawn);
/** Aim punch okuma (ananbaban merge / ESP). */
bool TryReadAimPunch(uintptr_t local_pawn, float* apx, float* apy);
/**
 * rcs_may_run: standalone veya aimbot+tus+(FOV kilidi | kisa grace).
 * aimbot_hard_lock: FOV icinde hedef — aim_blend_pct ile RCS gucu carpilir (standalone haric).
 * spray_rcs_coop: RCS acik + standalone degil + LMB spray — aimbot ile otomatik uyum (ayar yok).
 * ananbaban_merge: spray + hedef varken punch mouse yok — aim hedefi ananbaban rotasyonu ile duzeltilir.
 */
void Tick(uintptr_t local_pawn, const LegitCombatSettings& cfg, bool rcs_may_run,
	bool aimbot_hard_lock, bool spray_rcs_coop, bool ananbaban_merge);
}
