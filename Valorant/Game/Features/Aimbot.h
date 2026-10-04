#pragma once
#include "../weapon_runtime.hpp"

namespace UE4Structs {
	struct Vector3;
}

namespace AimControl {
	void ResetSmoothState();
	void ConsiderTargetScreen(float screenCx, float screenCy, const UE4Structs::Vector3& aimScreen,
		float aimFovLimit, float& bestFov, UE4Structs::Vector3& bestScreen, bool& haveBest,
		const LegitCombatSettings& cfg);
	
	void ApplyAnanbabanRcsToRelative(UE4Structs::Vector3& relWorld, float punchX, float punchY,
		float scaleX, float scaleY);
	
	bool RunLegitMouse(float screenCx, float screenCy, bool aimKeyHeld, bool haveBest,
		const UE4Structs::Vector3& bestAimScreen, const LegitCombatSettings& cfg,
		int shots_fired, bool spray_rcs_coop, bool rcs_ananbaban_merge);
}
