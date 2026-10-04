#pragma once
#include <Windows.h>
#include <atomic>
#include <mutex>
#include <random>
#include "offsets_runtime.hpp"

struct ImFont;
inline ImFont* g_WeaponsIconFont = nullptr;

inline std::mutex g_PlayerListMutex;

inline HWND Entryhwnd = NULL;
inline int processid = 0;
inline RECT Rect{};

inline ULONG64 client = 0;
inline uintptr_t global_pawn = 0;

inline std::atomic<int> g_localControllerEntityIndex{ -1 };

inline std::atomic<int> g_localPawnHandleIndex{ -1 };

inline std::atomic<int> g_cs2LocalCompetitiveRankType{ -999 };

inline bool isGlow = false;
inline bool isFOV = false;
inline bool noFlashEnabled = false;
inline bool noHandsEnabled = false;
inline float originalFOV = 68.0f;

namespace Settings
{
    
    inline bool bMenu = true;
    inline bool bAimbot = true;
    inline bool bVisuals = true;
    inline bool bMisc = true;
    inline bool bConfig = true;
    namespace aimbot 
    {
        inline bool aimbot = false;
        inline float aim_fov = 0.0f;
        inline float smooth = 10.f;
        
        inline int aim_delay_ms = 1;
        inline float aim_fov_min = 0.4f;
        
        inline bool aim_humanize = false;
        
        inline int aim_humanize_strength = 15;
        
        inline bool aim_visible_only = false;
        inline bool aim_autowall = false;
        inline float aim_min_damage = 30.f;
        
        inline int aim_key_mode = 0;
        inline bool trigger_reaction_enabled = true;
        inline float trigger_delay_min = 0.f;
        inline float trigger_delay_max = 0.f;
        
        inline float trigger_shot_cooldown = 100.f;
        
        inline int trigger_key_mode = 0;
        
        inline bool trigger_stopped_only = false;
        inline bool trigger_ignore_flash = true;
        
        inline bool trigger_visible_only = false;
        inline bool trigger_autowall = false;
        inline float trigger_min_damage = 30.f;
        inline bool trigger_head_only = false;
        
        inline float trigger_ttd_delay_ms = 0.f;
        
        inline bool rcs_enabled = false;
        
        inline bool rcs_standalone = false;
        
        inline int rcs_after_bullet = 1;
        inline float rcs_scale_pct = 55.f;
        inline float rcs_sens_mult = 1.f;
        inline float rcs_smooth = 22.f;
        
        inline float rcs_aim_blend_pct = 100.f;
        inline bool fov_circle = false;
        inline bool crosshair = false;
        inline bool penetration_crosshair = false;
        inline bool triggerbot = false;
        
        inline uint32_t hitbox_mask = 1u;

    };
    namespace rage_aimbot {

    }
    namespace Visuals
    {
        
        inline bool enablePlayerEsp = true;

        inline int boxMode = 1;
        inline float boxRounding = 2.f;
        inline bool filledBox = false;
        inline bool filledGradient = false;
        inline bool filledVisBox = false;
        
        inline int snaplineMode = 0;
        inline bool eyeRay = false;
        inline bool ammoBar = false;
        
        inline bool ammoText = false;
        inline bool bombWorldEsp = false;
        inline bool bombCarrierEsp = false;
        inline bool showScoped = false;
        inline bool showBlind = false;
        
        inline bool blindHideEsp = false;
        inline bool awpCrosshair = false;
        
        inline bool worldGrenades = false;
        
        inline bool grenadeEspLocalOnly = false;
        
        inline bool grenadeLineups = false;
        
        inline float grenadeLineupMaxDrawDistance = 0.f;
        
        inline bool worldGrenadeIcons = true;
        
        inline bool droppedWeaponEsp = false;
        inline bool droppedWeaponText = true;
        inline bool droppedWeaponIcons = true;
        
        inline bool worldInfernoHull = true;
        
        inline bool weaponEspIcon = true;

        inline bool bSnaplines = false;
        inline bool bDistance = false;
        inline bool bBox = true;
        inline bool healthBar = false;
        inline bool healthText = false;
        inline bool headcircle = false;
        inline bool bones = false;
        inline bool glow = false;
        inline bool distance = false;
        inline bool armor = false;
        inline bool names = false;
        inline bool weaponEsp = false;
        
        inline bool enemiesOnly = false;
        
        inline bool esp_visible_only = false;
        inline bool noflash = false;
        inline bool nohands = false;
        inline bool chams = false;
        inline bool ragdoll = false;
        inline bool nightmode = false;

        inline bool box = false;

        inline const char* boxStyle[] =
        {
            "Off",
            "2D Box",
            "Corner",
            "Filled+Outline",
        };

        inline float BoxWidth = 1.0f;
    }
    namespace misc {
        inline bool bhop = false;
        
        inline int bhop_delay_ms = 0;
        
        inline bool keybind_list_window = true;
        inline bool radar = false;
        
        inline bool save_fps = true;
        inline bool water = false;
        inline bool waterShowOverlayFps = true;
        inline bool waterShowGameFps = false;
        inline bool waterShowPing = true;
        
        inline int hit_sound = 0;
        inline bool hit_marker = false;
        inline bool spectatorList = false;
        inline bool bombTimer = false;
        inline bool fovChanger = false;
        inline float fov = 0.0f;
        
        inline bool cloudRadar = false;
        inline char cloudRadarPublishUrl[288] = "wss://radar.valth.run/publish";
        
        inline char cloudRadarViewerBase[192] = "https://expectional.dev/radar";
        
        inline bool radarWindow = false;
        inline bool radarWindowDebugLog = false;
        
        inline bool radarWindow43 = false;
        
        inline float radarWindowMapPx = 320.f;
        
        inline bool radarWindowFollowLocal = true;
        inline float radarWindowFollowZoom = 2.0f;
        
        inline float radarWindowBlipScale = 0.62f;
        
        inline bool radarWindowRotateWithView = false;
        
        inline bool radarWindowHudMatch = false;
        
        inline bool radarWindowGameMapTex = true;
        
        inline bool radarWindowHideMapImage = false;
        
        inline int radarWindowTransparency = 0;

        inline bool rank_reveal_window = false;
        
        inline bool rank_reveal_inventory_enabled = false;
        
        inline bool votekick_reveal_window = false;

        inline bool debug_visible_check = false;
        
        inline bool autosave_config =  false;
        
        inline bool obsBypass = false;
        
        inline bool overlayCustomFps = false;
        inline int overlayCustomFpsValue = 144;
        
        inline constexpr int workerQuality = 1;

    }
    
    namespace render_opt {
        
        inline int world_grenade_scan_period_frames = 4;
        
        inline int grenade_helper_max_ticks = 512;
        
        inline unsigned overlay_max_hz = 0u;
    }
    
    namespace weapon_cfg {
        
        inline int editor_category_idx = 0;
        
        inline bool cat_custom[6] = {};
    }
}

namespace EspUiColors {
	inline float skel_col[3] = { 1.f, 1.f, 1.f };
	
	inline float skel_vis_col[3] = { 0.25f, 0.85f, 0.45f };
	inline float espcol[3] = { 1.f, 1.f, 1.f };
	inline float vis_espcol[3] = { 0.25f, 0.85f, 0.45f };
	inline float esp_fill_col[3] = { 0.23f, 0.28f, 0.55f };
	inline float esp_fill2_col[3] = { 0.45f, 0.22f, 0.55f };
	inline float bomb_esp_col[3] = { 1.f, 0.45f, 0.2f };
	
	inline float grenade_traj_line[3] = { 200.f / 255.f, 220.f / 255.f, 1.f };
	inline float grenade_traj_marker[3] = { 1.f, 200.f / 255.f, 90.f / 255.f };
	inline float name_esp[3] = { 1.f, 1.f, 1.f };
	inline float name_vis_esp[3] = { 0.7f, 1.f, 0.8f };
	inline float weapon_esp[3] = { 1.f, 1.f, 1.f };
	inline float weapon_vis_esp[3] = { 0.7f, 1.f, 0.8f };
	inline float distance_esp[3] = { 0.f, 160.f / 255.f, 160.f / 255.f };
	inline float distance_vis_esp[3] = { 0.4f, 230.f / 255.f, 200.f / 255.f };
	inline float bomb_carrier_tag[3] = { 1.f, 120.f / 255.f, 60.f / 255.f };
	inline float armor_bar[3] = { 0.f, 128.f / 255.f, 1.f };
	inline float scoped_label[3] = { 200.f / 255.f, 200.f / 255.f, 230.f / 255.f };
	inline float blind_label[3] = { 1.f, 220.f / 255.f, 80.f / 255.f };
	inline float eye_ray[3] = { 0.f, 200.f / 255.f, 200.f / 255.f };
	inline float sniper_crosshair[3] = { 32.f / 255.f, 178.f / 255.f, 170.f / 255.f };
	
	inline float snapline_esp[3] = { 1.f, 1.f, 1.f };
	
	inline float snapline_vis_los[3] = { 0.35f, 0.95f, 1.f };
	inline float ammo_text_esp[3] = { 210.f / 255.f, 205.f / 255.f, 230.f / 255.f };
	inline float dropped_weapon_esp[3] = { 0.85f, 0.9f, 1.f };
	inline float health_bar_high[3] = { 95.f / 255.f, 230.f / 255.f, 130.f / 255.f };
	inline float health_bar_low[3] = { 28.f / 255.f, 95.f / 255.f, 48.f / 255.f };
	inline float health_bar_value_text[3] = { 235.f / 255.f, 235.f / 255.f, 245.f / 255.f };
	inline float health_text_damaged[3] = { 1.f, 30.f / 255.f, 30.f / 255.f };
	inline float health_text_full[3] = { 30.f / 255.f, 1.f, 40.f / 255.f };
	inline float ammo_bar_hi[3] = { 240.f / 255.f, 200.f / 255.f, 70.f / 255.f };
	inline float ammo_bar_lo[3] = { 1.f, 245.f / 255.f, 160.f / 255.f };
	
	inline float head_circle_fill[4] = { 1.f, 1.f, 1.f, 40.f / 255.f };
	
	inline float head_circle_vis_fill[4] = { 0.4f, 1.f, 0.55f, 60.f / 255.f };
	
	inline float aim_fov_circle[3] = { 250.f / 255.f, 92.f / 255.f, 1.f };
	inline float pen_crosshair_yes[3] = { 50.f / 255.f, 1.f, 50.f / 255.f };
	inline float pen_crosshair_no[3] = { 1.f, 50.f / 255.f, 50.f / 255.f };
	
	inline float radar_team[3] = { 50.f / 255.f, 1.f, 90.f / 255.f };
	inline float radar_enemy[3] = { 1.f, 64.f / 255.f, 64.f / 255.f };
	inline float radar_local[3] = { 120.f / 255.f, 1.f, 120.f / 255.f };
}

namespace ExpectionalCombatUiState {
	inline bool aim_toggle_arm = false;
	inline bool aim_key_prev = false;
}

inline char ExpectionalActiveCfgName[96] = "default";

struct ExpectionalVisDbg {
    bool valid = false;
    int id_ent_read = -999;
    uintptr_t crosshair_pawn = 0;
    int local_pawn_h = -1;
    int local_ctrl_i = -1;
    int off_spotted = 0;
    int off_id_ent = 0;
    uint64_t sample_tgt_mask = 0;
    uint64_t sample_loc_mask = 0;
    int sample_pawn_low = 0;
    int sample_entity_i = 0;
    
    bool sample_bvh_los = false;
    
    bool bvh_ready = false;
    int bvh_triangle_count = 0;
    char sample_name[40]{};
};
inline ExpectionalVisDbg g_visDbg{};

inline void ExpectionalRandomConsoleTitleW()
{
	static std::mt19937 rng{ std::random_device{}() };
	std::uniform_int_distribution<unsigned> dist(0x10000000u, 0xFFFFFFFEu);
	wchar_t buf[112];
	swprintf_s(buf, L"Windows Session Environment Broker [%08X]", dist(rng));
	SetConsoleTitleW(buf);
}
