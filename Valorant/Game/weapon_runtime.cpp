#include "weapon_runtime.hpp"
#include "globals.hpp"
#include "offsets_runtime.hpp"
#include "entity_handle.hpp"
#include "weapon_names.hpp"
#include "esp_extras.hpp"
#include "Features/RCS.hpp"
#include "config_io.hpp"
#include "../Driver/driver.hpp"

#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <ostream>
#include <string>
#include <unordered_map>

namespace {

std::mutex g_mtx;
LegitCombatSettings g_active{};
std::string g_lastWeapon;
std::unordered_map<std::string, LegitCombatSettings> g_profiles;
LegitCombatSettings g_menu_global_snapshot{};
bool g_menu_global_snapshot_valid = false;

static const char* kCategoryKeys[] = {
	"general", "pistols", "heavy_pistols", "smg", "heavy", "rifles", "snipers"
};
static const char* kCategoryLabels[] = {
	"General", "Pistols", "Heavy Pistols", "SMG", "Heavy", "Rifles", "Snipers"
};

inline constexpr int kCategoryCustomSwitchCount =
    static_cast<int>(WeaponRuntime::WeaponCategory::Count) - 1;

static bool CategoryCustomEnabled(WeaponRuntime::WeaponCategory cat) {
	if (cat == WeaponRuntime::WeaponCategory::General)
		return false;
	const int i = static_cast<int>(cat) - 1;
	if (i < 0 || i >= kCategoryCustomSwitchCount)
		return false;
	return Settings::weapon_cfg::cat_custom[static_cast<size_t>(i)];
}

static LegitCombatSettings FromGlobals() {
	LegitCombatSettings s{};
	s.aimbot = Settings::aimbot::aimbot;
	s.triggerbot = Settings::aimbot::triggerbot;
	s.rcs_enabled = Settings::aimbot::rcs_enabled;
	s.rcs_standalone = Settings::aimbot::rcs_standalone;
	s.aim_fov = Settings::aimbot::aim_fov;
	s.aim_fov_min = Settings::aimbot::aim_fov_min;
	s.smooth = Settings::aimbot::smooth;
	s.aim_delay_ms = Settings::aimbot::aim_delay_ms;
	s.aim_humanize = Settings::aimbot::aim_humanize;
	s.aim_humanize_strength = Settings::aimbot::aim_humanize_strength;
	s.aim_visible_only = Settings::aimbot::aim_visible_only;
	s.aim_autowall = Settings::aimbot::aim_autowall;
	s.aim_min_damage = Settings::aimbot::aim_min_damage;
	s.aim_key_mode = Settings::aimbot::aim_key_mode;
	s.hitbox_mask = Settings::aimbot::hitbox_mask;
	s.fov_circle = Settings::aimbot::fov_circle;
	s.crosshair = Settings::aimbot::crosshair;
	s.penetration_crosshair = Settings::aimbot::penetration_crosshair;
	s.trigger_reaction_enabled = Settings::aimbot::trigger_reaction_enabled;
	s.trigger_delay_min = Settings::aimbot::trigger_delay_min;
	s.trigger_delay_max = Settings::aimbot::trigger_delay_max;
	s.trigger_shot_cooldown = Settings::aimbot::trigger_shot_cooldown;
	s.trigger_ttd_delay_ms = Settings::aimbot::trigger_ttd_delay_ms;
	s.trigger_key_mode = Settings::aimbot::trigger_key_mode;
	s.trigger_stopped_only = Settings::aimbot::trigger_stopped_only;
	s.trigger_ignore_flash = Settings::aimbot::trigger_ignore_flash;
	s.trigger_visible_only = Settings::aimbot::trigger_visible_only;
	s.trigger_autowall = Settings::aimbot::trigger_autowall;
	s.trigger_min_damage = Settings::aimbot::trigger_min_damage;
	s.trigger_head_only = Settings::aimbot::trigger_head_only;
	s.rcs_after_bullet = Settings::aimbot::rcs_after_bullet;
	s.rcs_scale_pct = Settings::aimbot::rcs_scale_pct;
	s.rcs_sens_mult = Settings::aimbot::rcs_sens_mult;
	s.rcs_smooth = Settings::aimbot::rcs_smooth;
	s.rcs_aim_blend_pct = Settings::aimbot::rcs_aim_blend_pct;
	return s;
}

static void ToGlobals(const LegitCombatSettings& s) {
	Settings::aimbot::aimbot = s.aimbot;
	Settings::aimbot::triggerbot = s.triggerbot;
	Settings::aimbot::rcs_enabled = s.rcs_enabled;
	Settings::aimbot::rcs_standalone = s.rcs_standalone;
	Settings::aimbot::aim_fov = s.aim_fov;
	Settings::aimbot::aim_fov_min = s.aim_fov_min;
	Settings::aimbot::smooth = s.smooth;
	Settings::aimbot::aim_delay_ms = s.aim_delay_ms;
	Settings::aimbot::aim_humanize = s.aim_humanize;
	Settings::aimbot::aim_humanize_strength = s.aim_humanize_strength;
	Settings::aimbot::aim_visible_only = s.aim_visible_only;
	Settings::aimbot::aim_autowall = s.aim_autowall;
	Settings::aimbot::aim_min_damage = s.aim_min_damage;
	Settings::aimbot::aim_key_mode = s.aim_key_mode;
	Settings::aimbot::hitbox_mask = s.hitbox_mask;
	Settings::aimbot::fov_circle = s.fov_circle;
	Settings::aimbot::crosshair = s.crosshair;
	Settings::aimbot::penetration_crosshair = s.penetration_crosshair;
	Settings::aimbot::trigger_reaction_enabled = s.trigger_reaction_enabled;
	Settings::aimbot::trigger_delay_min = s.trigger_delay_min;
	Settings::aimbot::trigger_delay_max = s.trigger_delay_max;
	Settings::aimbot::trigger_shot_cooldown = s.trigger_shot_cooldown;
	Settings::aimbot::trigger_ttd_delay_ms = s.trigger_ttd_delay_ms;
	Settings::aimbot::trigger_key_mode = s.trigger_key_mode;
	Settings::aimbot::trigger_stopped_only = s.trigger_stopped_only;
	Settings::aimbot::trigger_ignore_flash = s.trigger_ignore_flash;
	Settings::aimbot::trigger_visible_only = s.trigger_visible_only;
	Settings::aimbot::trigger_autowall = s.trigger_autowall;
	Settings::aimbot::trigger_min_damage = s.trigger_min_damage;
	Settings::aimbot::trigger_head_only = s.trigger_head_only;
	Settings::aimbot::rcs_after_bullet = s.rcs_after_bullet;
	Settings::aimbot::rcs_scale_pct = s.rcs_scale_pct;
	Settings::aimbot::rcs_sens_mult = s.rcs_sens_mult;
	Settings::aimbot::rcs_smooth = s.rcs_smooth;
	Settings::aimbot::rcs_aim_blend_pct = s.rcs_aim_blend_pct;
}

static void MergeCategoryProfile(LegitCombatSettings& dst, const LegitCombatSettings& src) {
	dst.aimbot = src.aimbot;
	dst.aim_fov = src.aim_fov;
	dst.aim_fov_min = src.aim_fov_min;
	dst.smooth = src.smooth;
	dst.aim_delay_ms = src.aim_delay_ms;
	dst.aim_humanize = src.aim_humanize;
	dst.aim_humanize_strength = src.aim_humanize_strength;
	dst.aim_visible_only = src.aim_visible_only;
	dst.aim_autowall = src.aim_autowall;
	dst.aim_min_damage = src.aim_min_damage;
	dst.aim_key_mode = src.aim_key_mode;
	dst.hitbox_mask = src.hitbox_mask;
	dst.fov_circle = src.fov_circle;
	dst.crosshair = src.crosshair;
	dst.penetration_crosshair = src.penetration_crosshair;
	dst.rcs_enabled = src.rcs_enabled;
	dst.rcs_standalone = src.rcs_standalone;
	dst.rcs_after_bullet = src.rcs_after_bullet;
	dst.rcs_scale_pct = src.rcs_scale_pct;
	dst.rcs_sens_mult = src.rcs_sens_mult;
	dst.rcs_smooth = src.rcs_smooth;
	dst.rcs_aim_blend_pct = src.rcs_aim_blend_pct;
	dst.triggerbot = src.triggerbot;
	dst.trigger_reaction_enabled = src.trigger_reaction_enabled;
	dst.trigger_delay_min = src.trigger_delay_min;
	dst.trigger_delay_max = src.trigger_delay_max;
	dst.trigger_shot_cooldown = src.trigger_shot_cooldown;
	dst.trigger_ttd_delay_ms = src.trigger_ttd_delay_ms;
	dst.trigger_key_mode = src.trigger_key_mode;
	dst.trigger_stopped_only = src.trigger_stopped_only;
	dst.trigger_ignore_flash = src.trigger_ignore_flash;
	dst.trigger_visible_only = src.trigger_visible_only;
	dst.trigger_autowall = src.trigger_autowall;
	dst.trigger_min_damage = src.trigger_min_damage;
	dst.trigger_head_only = src.trigger_head_only;
}

static std::string ReadWeaponKey(uintptr_t pawn) {
	const uint16_t defIdx = ex_esp::ReadWeaponDefIndex(pawn);
	const char* nm = WeaponNameFromDefIndex(defIdx);
	return nm ? std::string(nm) : std::string("unknown");
}

static WeaponRuntime::WeaponCategory CategoryFromKey(const std::string& key) {
	if (key == "pistols") return WeaponRuntime::WeaponCategory::Pistols;
	if (key == "heavy_pistols") return WeaponRuntime::WeaponCategory::HeavyPistols;
	if (key == "smg") return WeaponRuntime::WeaponCategory::Smg;
	if (key == "heavy") return WeaponRuntime::WeaponCategory::Heavy;
	if (key == "rifles") return WeaponRuntime::WeaponCategory::Rifles;
	if (key == "snipers") return WeaponRuntime::WeaponCategory::Snipers;
	return WeaponRuntime::WeaponCategory::General;
}

static void WriteBoolLine(std::ostream& o, const char* k, bool v) { o << k << '=' << (v ? 1 : 0) << '\n'; }

} 

namespace WeaponRuntime {

int CategoryCount() {
	return static_cast<int>(WeaponCategory::Count);
}

const char* CategoryKey(int index) {
	if (index < 0 || index >= CategoryCount())
		return "general";
	return kCategoryKeys[index];
}

const char* CategoryLabel(int index) {
	if (index < 0 || index >= CategoryCount())
		return "General";
	return kCategoryLabels[index];
}

WeaponCategory ClassifyWeaponKey(const std::string& w) {
	if (w == "glock" || w == "p2000" || w == "p250" || w == "fiveseven" || w == "tec9" || w == "usp" ||
	    w == "elite" || w == "cz75a")
		return WeaponCategory::Pistols;
	if (w == "deagle" || w == "revolver")
		return WeaponCategory::HeavyPistols;
	if (w == "p90" || w == "bizon" || w == "mac10" || w == "mp7" || w == "mp9" || w == "ump45" || w == "mp5sd")
		return WeaponCategory::Smg;
	if (w == "m249" || w == "negev" || w == "mag7" || w == "nova" || w == "sawedoff" || w == "xm1014")
		return WeaponCategory::Heavy;
	if (w == "ak47" || w == "aug" || w == "famas" || w == "galilar" || w == "m4a1" || w == "m4a4" || w == "sg556")
		return WeaponCategory::Rifles;
	if (w == "awp" || w == "ssg08" || w == "scar20" || w == "g3sg1")
		return WeaponCategory::Snipers;
	return WeaponCategory::General;
}

WeaponCategory ActiveCategory() {
	std::lock_guard<std::mutex> lk(g_mtx);
	return ClassifyWeaponKey(g_lastWeapon);
}

void EditorSyncGlobalSnapshotFromSettings() {
	g_menu_global_snapshot = FromGlobals();
	g_menu_global_snapshot_valid = true;
}

void EditorPrepareGlobalsForConfigSave() {
	if (Settings::weapon_cfg::editor_category_idx == 0)
		EditorSyncGlobalSnapshotFromSettings();
	else if (g_menu_global_snapshot_valid)
		ToGlobals(g_menu_global_snapshot);
}

void NotifyMenuCombatSettingEdited() {
	if (Settings::weapon_cfg::editor_category_idx == 0)
		EditorSyncGlobalSnapshotFromSettings();
	if (!Settings::misc::autosave_config)
		return;
	ExpectionalSaveActiveConfig();
}

void OnMenuCategoryChanged(int prev_menu_idx, int new_menu_idx) {
	if (prev_menu_idx < 0 || prev_menu_idx == new_menu_idx)
		return;
	if (prev_menu_idx == 0)
		EditorSyncGlobalSnapshotFromSettings();
	else if (g_menu_global_snapshot_valid)
		ToGlobals(g_menu_global_snapshot);

	if (new_menu_idx == 0 && g_menu_global_snapshot_valid)
		ToGlobals(g_menu_global_snapshot);
	else if (new_menu_idx > 0 && new_menu_idx < CategoryCount())
		EnsureEditorCategoryProfile(static_cast<WeaponCategory>(new_menu_idx));
}

void EnsureEditorCategoryProfile(WeaponCategory cat) {
	if (cat == WeaponCategory::General)
		return;
	const char* ck = CategoryKey(static_cast<int>(cat));
	if (!ck || !ck[0])
		return;
	std::lock_guard<std::mutex> lk(g_mtx);
	if (g_profiles.find(ck) == g_profiles.end())
		g_profiles[ck] = FromGlobals();
}

LegitCombatSettings* MutEditorProfileRow() {
	const int sel = Settings::weapon_cfg::editor_category_idx;
	if (sel <= 0)
		return nullptr;
	if (sel >= CategoryCount())
		return nullptr;
	const char* ck = CategoryKey(sel);
	std::lock_guard<std::mutex> lk(g_mtx);
	auto it = g_profiles.find(ck);
	if (it == g_profiles.end()) {
		g_profiles[ck] = FromGlobals();
		it = g_profiles.find(ck);
	}
	return &it->second;
}

void Refresh(uintptr_t local_pawn) {
	const std::string w = ReadWeaponKey(local_pawn);
	std::lock_guard<std::mutex> lk(g_mtx);
	if (w != g_lastWeapon) {
		g_lastWeapon = w;
		RCS::ResetState();
	}
	g_active = FromGlobals();
	const WeaponCategory cat = ClassifyWeaponKey(w);
	if (cat == WeaponCategory::General || !CategoryCustomEnabled(cat))
		return;
	const char* ck = CategoryKey(static_cast<int>(cat));
	const auto it = g_profiles.find(ck);
	if (it == g_profiles.end())
		return;
	MergeCategoryProfile(g_active, it->second);
}

LegitCombatSettings Active() {
	std::lock_guard<std::mutex> lk(g_mtx);
	return g_active;
}

std::string ActiveWeaponKey() {
	std::lock_guard<std::mutex> lk(g_mtx);
	return g_lastWeapon;
}

void SaveGlobalsToCategoryProfile(WeaponCategory cat) {
	if (cat == WeaponCategory::General)
		return;
	const char* ck = CategoryKey(static_cast<int>(cat));
	if (!ck || !ck[0])
		return;
	std::lock_guard<std::mutex> lk(g_mtx);
	g_profiles[ck] = FromGlobals();
}

void ApplyCategoryProfileToGlobals(WeaponCategory cat) {
	if (cat == WeaponCategory::General)
		return;
	const char* ck = CategoryKey(static_cast<int>(cat));
	std::lock_guard<std::mutex> lk(g_mtx);
	const auto it = g_profiles.find(ck);
	if (it != g_profiles.end())
		ToGlobals(it->second);
}

void DeleteCategoryProfile(WeaponCategory cat) {
	if (cat == WeaponCategory::General)
		return;
	const char* ck = CategoryKey(static_cast<int>(cat));
	std::lock_guard<std::mutex> lk(g_mtx);
	g_profiles.erase(ck);
}

bool HasCategoryProfile(WeaponCategory cat) {
	if (cat == WeaponCategory::General)
		return false;
	const char* ck = CategoryKey(static_cast<int>(cat));
	std::lock_guard<std::mutex> lk(g_mtx);
	return g_profiles.find(ck) != g_profiles.end();
}

void SerializeWeaponProfiles(std::ostream& o) {
	std::lock_guard<std::mutex> lk(g_mtx);
	o << "weapon_cfg.editor_category_idx=" << Settings::weapon_cfg::editor_category_idx << '\n';
	for (int i = 0; i < kCategoryCustomSwitchCount; ++i) {
		const std::string k = std::string("weapon_cfg.cat_custom_") + CategoryKey(i + 1);
		WriteBoolLine(o, k.c_str(), Settings::weapon_cfg::cat_custom[static_cast<size_t>(i)]);
	}
	for (const auto& kv : g_profiles) {
		const std::string& p = kv.first;
		if (CategoryFromKey(p) == WeaponRuntime::WeaponCategory::General)
			continue;
		const LegitCombatSettings& s = kv.second;
		const auto W = [&](const char* field, auto v) {
			o << "weapon." << p << "." << field << '=' << v << '\n';
		};
		W("aimbot", s.aimbot ? 1 : 0);
		W("triggerbot", s.triggerbot ? 1 : 0);
		W("rcs_enabled", s.rcs_enabled ? 1 : 0);
		W("rcs_standalone", s.rcs_standalone ? 1 : 0);
		W("aim_fov", s.aim_fov);
		W("aim_fov_min", s.aim_fov_min);
		W("smooth", s.smooth);
		W("aim_delay_ms", s.aim_delay_ms);
		W("aim_humanize", s.aim_humanize ? 1 : 0);
		W("aim_humanize_strength", s.aim_humanize_strength);
		W("aim_visible_only", s.aim_visible_only ? 1 : 0);
		W("aim_autowall", s.aim_autowall ? 1 : 0);
		W("aim_min_damage", s.aim_min_damage);
		W("aim_key_mode", s.aim_key_mode);
		W("hitbox_mask", s.hitbox_mask);
		W("fov_circle", s.fov_circle ? 1 : 0);
		W("crosshair", s.crosshair ? 1 : 0);
		W("penetration_crosshair", s.penetration_crosshair ? 1 : 0);
		W("trigger_reaction_enabled", s.trigger_reaction_enabled ? 1 : 0);
		W("trigger_delay_min", s.trigger_delay_min);
		W("trigger_delay_max", s.trigger_delay_max);
		W("trigger_shot_cooldown", s.trigger_shot_cooldown);
		W("trigger_ttd_delay_ms", s.trigger_ttd_delay_ms);
		W("trigger_key_mode", s.trigger_key_mode);
		W("trigger_stopped_only", s.trigger_stopped_only ? 1 : 0);
		W("trigger_ignore_flash", s.trigger_ignore_flash ? 1 : 0);
		W("trigger_visible_only", s.trigger_visible_only ? 1 : 0);
		W("trigger_autowall", s.trigger_autowall ? 1 : 0);
		W("trigger_min_damage", s.trigger_min_damage);
		W("trigger_head_only", s.trigger_head_only ? 1 : 0);
		W("rcs_after_bullet", s.rcs_after_bullet);
		W("rcs_scale_pct", s.rcs_scale_pct);
		W("rcs_sens_mult", s.rcs_sens_mult);
		W("rcs_smooth", s.rcs_smooth);
		W("rcs_aim_blend_pct", s.rcs_aim_blend_pct);
	}
}

void ClearWeaponProfiles() {
	std::lock_guard<std::mutex> lk(g_mtx);
	g_profiles.clear();
}

void LoadWeaponProfileLine(const std::string& key, const std::string& val) {
	if (key == "weapon_cfg.editor_category_idx") {
		try {
			int v = std::stoi(val);
			if (v < 0) v = 0;
			if (v >= CategoryCount()) v = CategoryCount() - 1;
			Settings::weapon_cfg::editor_category_idx = v;
		} catch (...) {}
		return;
	}
	if (key == "weapon_cfg.editor_weapon_idx") {
		Settings::weapon_cfg::editor_category_idx = 0;
		return;
	}
	for (int i = 0; i < kCategoryCustomSwitchCount; ++i) {
		const std::string catKey = std::string("weapon_cfg.cat_custom_") + CategoryKey(i + 1);
		if (key == catKey) {
			Settings::weapon_cfg::cat_custom[static_cast<size_t>(i)] = (val == "1" || val == "true");
			return;
		}
	}
	if (key == "weapon_cfg.per_weapon_aim" || key == "weapon_cfg.per_weapon_trigger" || key == "weapon_cfg.per_weapon")
		return;

	const std::string pref = "weapon.";
	if (key.compare(0, pref.size(), pref) != 0)
		return;
	const size_t d1 = key.find('.', pref.size());
	if (d1 == std::string::npos) return;
	std::string wname = key.substr(pref.size(), d1 - pref.size());
	const std::string field = key.substr(d1 + 1);

	static const std::unordered_map<std::string, std::string> kLegacyWeaponToCat = {
		{"glock", "pistols"}, {"p2000", "pistols"}, {"p250", "pistols"}, {"fiveseven", "pistols"},
		{"tec9", "pistols"}, {"usp", "pistols"}, {"elite", "pistols"}, {"cz75a", "pistols"},
		{"deagle", "heavy_pistols"}, {"revolver", "heavy_pistols"},
		{"p90", "smg"}, {"bizon", "smg"}, {"mac10", "smg"}, {"mp7", "smg"}, {"mp9", "smg"},
		{"ump45", "smg"}, {"mp5sd", "smg"},
		{"m249", "heavy"}, {"negev", "heavy"}, {"mag7", "heavy"}, {"nova", "heavy"},
		{"sawedoff", "heavy"}, {"xm1014", "heavy"},
		{"ak47", "rifles"}, {"aug", "rifles"}, {"famas", "rifles"}, {"galilar", "rifles"},
		{"m4a1", "rifles"}, {"m4a4", "rifles"}, {"sg556", "rifles"},
		{"awp", "snipers"}, {"ssg08", "snipers"}, {"scar20", "snipers"}, {"g3sg1", "snipers"},
	};
	if (CategoryFromKey(wname) == WeaponRuntime::WeaponCategory::General) {
		const auto mapIt = kLegacyWeaponToCat.find(wname);
		if (mapIt != kLegacyWeaponToCat.end())
			wname = mapIt->second;
		else
			return;
	}

	LegitCombatSettings s{};
	{
		std::lock_guard<std::mutex> lk(g_mtx);
		const auto it = g_profiles.find(wname);
		if (it != g_profiles.end())
			s = it->second;
		else
			s = FromGlobals();
	}
	auto pb = [&](const char* f, bool& b) {
		if (field == f) b = (val == "1" || val == "true");
	};
	auto pf = [&](const char* f, float& x) {
		if (field == f) x = std::stof(val);
	};
	auto pi = [&](const char* f, int& x) {
		if (field == f) x = std::stoi(val);
	};
	try {
		pb("aimbot", s.aimbot);
		pb("triggerbot", s.triggerbot);
		pb("rcs_enabled", s.rcs_enabled);
		pb("rcs_standalone", s.rcs_standalone);
		pf("aim_fov", s.aim_fov);
		pf("aim_fov_min", s.aim_fov_min);
		pf("smooth", s.smooth);
		pi("aim_delay_ms", s.aim_delay_ms);
		pb("aim_humanize", s.aim_humanize);
		pi("aim_humanize_strength", s.aim_humanize_strength);
		pb("aim_visible_only", s.aim_visible_only);
		pb("aim_autowall", s.aim_autowall);
		pf("aim_min_damage", s.aim_min_damage);
		pi("aim_key_mode", s.aim_key_mode);
		if (field == "hitbox_mask")
			s.hitbox_mask = static_cast<uint32_t>(std::stoul(val));
		else if (field == "selectedhitbox") {
			const int v = (std::clamp)(std::stoi(val), 0, 2);
			s.hitbox_mask = (1u << v);
		}
		pb("fov_circle", s.fov_circle);
		pb("crosshair", s.crosshair);
		pb("penetration_crosshair", s.penetration_crosshair);
		pb("trigger_reaction_enabled", s.trigger_reaction_enabled);
		pf("trigger_delay_min", s.trigger_delay_min);
		pf("trigger_delay_max", s.trigger_delay_max);
		pf("trigger_shot_cooldown", s.trigger_shot_cooldown);
		pf("trigger_ttd_delay_ms", s.trigger_ttd_delay_ms);
		pi("trigger_key_mode", s.trigger_key_mode);
		pb("trigger_stopped_only", s.trigger_stopped_only);
		pb("trigger_ignore_flash", s.trigger_ignore_flash);
		pb("trigger_visible_only", s.trigger_visible_only);
		pb("trigger_autowall", s.trigger_autowall);
		pf("trigger_min_damage", s.trigger_min_damage);
		pb("trigger_head_only", s.trigger_head_only);
		pi("rcs_after_bullet", s.rcs_after_bullet);
		pf("rcs_scale_pct", s.rcs_scale_pct);
		pf("rcs_sens_mult", s.rcs_sens_mult);
		pf("rcs_smooth", s.rcs_smooth);
		pf("rcs_aim_blend_pct", s.rcs_aim_blend_pct);
		if (field == "aim_vis_mode") {
			const int v = std::stoi(val);
			if (v != 0)
				s.aim_visible_only = true;
		}
		if (field == "trigger_vis_mode") {
			const int v = std::stoi(val);
			if (v != 0)
				s.trigger_visible_only = true;
		}
		if (field == "trigger_always" && (val == "1" || val == "true"))
			s.trigger_key_mode = 2;
		if (field == "trigger_ttd_spotted" && (val == "1" || val == "true"))
			s.trigger_visible_only = true;
		if (field == "trigger_delay") {
			const float x = std::stof(val);
			s.trigger_delay_min = x;
			s.trigger_delay_max = x;
		}
		s.aim_key_mode = (std::clamp)(s.aim_key_mode, 0, 2);
		s.trigger_key_mode = (std::clamp)(s.trigger_key_mode, 0, 2);
	} catch (...) {
		return;
	}
	std::lock_guard<std::mutex> lk(g_mtx);
	g_profiles[wname] = s;
}

} 
