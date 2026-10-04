#pragma once

#include "globals.hpp"
#include "weapon_runtime.hpp"

struct ExpectionalEspBudget {
	bool save_fps = false;

	bool any_player_visual = false;
	bool any_other_visual = false;
	bool any_combat = false;
	bool any_world_scan = false;

	bool need_player_loop = false;
	bool need_velocity_predict = true;
	bool need_bone_skeleton = false;
	bool need_bone_aim = false;
	bool need_cat_bounds = false;
	bool need_local_eye = false;
	bool need_bvh_esp_los = false;
	bool need_bvh_aim = false;

	bool player_detail_reads = true;
	bool run_world_scan = true;

	int fov_circle_segments = 96;
	unsigned equip_rebuild_stride = 8u;

	static bool AnyPlayerVisualEnabled() noexcept
	{
		using namespace Settings::Visuals;
		if (!enablePlayerEsp)
			return false;
		return bBox || bones || bSnaplines || headcircle || healthBar || healthText || armor ||
		    ammoBar || ammoText || weaponEsp || weaponEspIcon || names || distance ||
		    filledBox || eyeRay || showScoped || showBlind || bombCarrierEsp ||
		    esp_visible_only || blindHideEsp || awpCrosshair;
	}

	static bool AnyOtherVisualEnabled() noexcept
	{
		using namespace Settings::Visuals;
		return droppedWeaponEsp || bombWorldEsp || worldGrenades || worldInfernoHull || grenadeLineups;
	}

	static bool AnyWorldFeatureEnabled() noexcept
	{
		return AnyOtherVisualEnabled();
	}

	static bool AnyEspLosColorFeature() noexcept
	{
		using namespace Settings::Visuals;
		return bBox || bSnaplines || headcircle || filledVisBox ||
		       bones || names || weaponEsp || distance;
	}

	static bool NeedWorldBvhMesh() noexcept
	{
		using namespace Settings::Visuals;
		return esp_visible_only || AnyEspLosColorFeature();
	}

	static bool NeedGrenadeTrajectoryBvh() noexcept
	{
		return false;
	}

	static ExpectionalEspBudget Build(const LegitCombatSettings& combat) noexcept
	{
		ExpectionalEspBudget b{};
		b.save_fps = Settings::misc::save_fps;

		b.any_player_visual = AnyPlayerVisualEnabled();
		b.any_other_visual = AnyOtherVisualEnabled();
		b.any_combat = combat.aimbot || combat.triggerbot || combat.rcs_enabled || combat.fov_circle ||
		    combat.crosshair || combat.penetration_crosshair;
		b.any_world_scan = b.any_other_visual;

		b.need_player_loop = b.any_player_visual || b.any_combat;
		b.need_bone_skeleton = Settings::Visuals::bones;
		b.need_bone_aim = combat.aimbot != 0;
		
		b.need_cat_bounds = Settings::Visuals::bBox && Settings::Visuals::boxMode != 0;
		b.need_bvh_esp_los = Settings::Visuals::esp_visible_only || AnyEspLosColorFeature();
		b.need_bvh_aim = (combat.aimbot && combat.aim_visible_only) || combat.penetration_crosshair ||
		    (combat.triggerbot && (combat.trigger_visible_only || combat.trigger_autowall));
		b.need_local_eye = b.need_bvh_esp_los || b.need_bvh_aim || Settings::Visuals::eyeRay ||
		    Settings::Visuals::grenadeLineups || combat.penetration_crosshair;

		b.need_velocity_predict = false;

		b.player_detail_reads = !b.save_fps;
		b.run_world_scan = true;

		if (b.save_fps) {
			b.fov_circle_segments = 48;
			b.equip_rebuild_stride = b.any_other_visual ? 8u : 20u;
		}

		return b;
	}
};
