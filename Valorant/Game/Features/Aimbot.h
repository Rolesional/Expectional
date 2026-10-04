#pragma once
#include "../weapon_runtime.hpp"

namespace UE4Structs {
	struct Vector3;
}

/** ananbaban `AimControl` benzeri: aday secimi + yumusak mouse hareketi. */
namespace AimControl {
	void ResetSmoothState();
	void ConsiderTargetScreen(float screenCx, float screenCy, const UE4Structs::Vector3& aimScreen,
		float aimFovLimit, float& bestFov, UE4Structs::Vector3& bestScreen, bool& haveBest,
		const LegitCombatSettings& cfg);
	/** ananbaban AimBot: goz-hedef rel vektorunu punch ile dondur (RCS mouse ile cift kompanzasyon olmasin). */
	void ApplyAnanbabanRcsToRelative(UE4Structs::Vector3& relWorld, float punchX, float punchY,
		float scaleX, float scaleY);
	/**
	 * spray_rcs_coop: yumusatma / dikey takip azaltma (klasik ortak hedef).
	 * rcs_ananbaban_merge: hedef zaten punch ile duzeltildi — spray yumusatmayi uygulama.
	 */
	bool RunLegitMouse(float screenCx, float screenCy, bool aimKeyHeld, bool haveBest,
		const UE4Structs::Vector3& bestAimScreen, const LegitCombatSettings& cfg,
		int shots_fired, bool spray_rcs_coop, bool rcs_ananbaban_merge);
}
