#pragma once

#include <cstdint>
#include <ostream>
#include <string>

/** Aim + trigger + RCS — kategori profili veya General (Settings). */
struct LegitCombatSettings {
	bool aimbot = false;
	bool triggerbot = false;
	bool rcs_enabled = false;
	bool rcs_standalone = false;
	float aim_fov = 40.f;
	float aim_fov_min = 0.4f;
	float smooth = 10.f;
	int aim_delay_ms = 1;
	bool aim_humanize = false;
	int aim_humanize_strength = 15;
	bool aim_visible_only = false;
	bool aim_autowall = false;
	float aim_min_damage = 30.f;
	int aim_key_mode = 0;
	uint32_t hitbox_mask = 1u;
	bool fov_circle = false;
	bool crosshair = false;
	bool penetration_crosshair = false;
	bool trigger_reaction_enabled = true;
	float trigger_delay_min = 0.f;
	float trigger_delay_max = 0.f;
	float trigger_shot_cooldown = 100.f;
	float trigger_ttd_delay_ms = 0.f;
	int trigger_key_mode = 0;
	bool trigger_stopped_only = false;
	bool trigger_ignore_flash = true;
	bool trigger_visible_only = false;
	bool trigger_autowall = false;
	float trigger_min_damage = 30.f;
	bool trigger_head_only = false;
	int rcs_after_bullet = 1;
	float rcs_scale_pct = 100.f;
	float rcs_sens_mult = 1.f;
	float rcs_smooth = 22.f;
	/** Eski cfg uyumu; menu yok — tek Strength % kullanilir. */
	float rcs_aim_blend_pct = 100.f;
};

namespace WeaponRuntime {

enum class WeaponCategory : int {
	General = 0,
	Pistols = 1,
	HeavyPistols = 2,
	Smg = 3,
	Heavy = 4,
	Rifles = 5,
	Snipers = 6,
	Count = 7
};

void Refresh(uintptr_t local_pawn);
LegitCombatSettings Active();

void SaveGlobalsToCategoryProfile(WeaponCategory cat);
void ApplyCategoryProfileToGlobals(WeaponCategory cat);
void DeleteCategoryProfile(WeaponCategory cat);
bool HasCategoryProfile(WeaponCategory cat);

std::string ActiveWeaponKey();
WeaponCategory ClassifyWeaponKey(const std::string& weapon_key);
WeaponCategory ActiveCategory();

void SerializeWeaponProfiles(std::ostream& o);
void LoadWeaponProfileLine(const std::string& key, const std::string& val);
void ClearWeaponProfiles();

void EditorSyncGlobalSnapshotFromSettings();
void EditorPrepareGlobalsForConfigSave();
void NotifyMenuCombatSettingEdited();
void OnMenuCategoryChanged(int prev_idx, int new_idx);
void EnsureEditorCategoryProfile(WeaponCategory cat);

/** 0=General -> nullptr (Settings::aimbot); diger kategoriler -> profil satiri. */
LegitCombatSettings* MutEditorProfileRow();

int CategoryCount();
const char* CategoryKey(int index);
const char* CategoryLabel(int index);

/** Geriye uyumluluk — eski menu cagrilari. */
inline LegitCombatSettings* MutEditorProfileRowForAim() { return MutEditorProfileRow(); }
inline LegitCombatSettings* MutEditorProfileRowForTrigger() { return MutEditorProfileRow(); }

} // namespace WeaponRuntime
