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
/** cacheGame: entity list indeksi i (1..63). */
inline std::atomic<int> g_localControllerEntityIndex{ -1 };
/** m_hPlayerPawn dusuk bit — spotted mask (ananbaban) ile uyum. */
inline std::atomic<int> g_localPawnHandleIndex{ -1 };
/** cacheGame: yerel CCSPlayerController::m_iCompetitiveRankType (int8). -999 = okunamadi. */
inline std::atomic<int> g_cs2LocalCompetitiveRankType{ -999 };

inline bool isGlow = false;
inline bool isFOV = false;
inline bool noFlashEnabled = false;
inline bool noHandsEnabled = false;
inline float originalFOV = 68.0f;

namespace Settings
{
    /** false: oyun/RCS calisir; Home ile menu acilir. true iken RCS/Trigger vb. kasitli durdurulur. */
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
        /** ananbaban MenuConfig::AimDelay — ardışık aim hareketi arası min ms. */
        inline int aim_delay_ms = 1;
        inline float aim_fov_min = 0.4f;
        /** ananbaban AimControl::HumanizeVar — mikro titreme / eğri. */
        inline bool aim_humanize = false;
        /** 0–20. */
        inline int aim_humanize_strength = 15;
        /** Açıkken aimbot yalnızca görünür (BVH + spotted fallback) hedefe kilit. */
        inline bool aim_visible_only = false;
        inline bool aim_autowall = false;
        inline float aim_min_damage = 30.f;
        /** RCS standalone kapali iken aimbot ile spray uyumu kod icinde sabit (cfg yok). */
        /** 0 Hold, 1 Toggle, 2 Always on — aim tuşu. */
        inline int aim_key_mode = 0;
        inline bool trigger_reaction_enabled = true;
        inline float trigger_delay_min = 0.f;
        inline float trigger_delay_max = 0.f;
        /** ananbaban ShotDuration — son atıştan sonra min ms. */
        inline float trigger_shot_cooldown = 100.f;
        /** 0 Hold, 1 Toggle, 2 Always on — tetik tuşu. */
        inline int trigger_key_mode = 0;
        /** Sadece yerde / hız ~0 iken tetik (ananbaban StopedOnly). */
        inline bool trigger_stopped_only = false;
        inline bool trigger_ignore_flash = true;
        /** Acikken tetik BVH/spotted/crosshair gorunurluk; kapali = yalnizca crosshair entity. */
        inline bool trigger_visible_only = false;
        inline bool trigger_autowall = false;
        inline float trigger_min_damage = 30.f;
        inline bool trigger_head_only = false;
        /** BVH LOS saglandiktan + hedef stabil olduktan sonra ek bekleme (ms). */
        inline float trigger_ttd_delay_ms = 0.f;
        /** Mouse ile aim punch tersine; sens * % * mult (ananbaban benzeri). */
        inline bool rcs_enabled = false;
        /** Aimbot kapali / hedefsiz tam RCS testi (yalniz LMB + spray). */
        inline bool rcs_standalone = false;
        /** 0: ilk mermiden RCS; 1 = 2. mermiden (IsFiring>1 benzeri). */
        inline int rcs_after_bullet = 1;
        inline float rcs_scale_pct = 55.f;
        inline float rcs_sens_mult = 1.f;
        inline float rcs_smooth = 22.f;
        /** Aimbot hedef kilitliyken RCS % (88 ~ aim ile uyum). */
        inline float rcs_aim_blend_pct = 100.f;
        inline bool fov_circle = false;
        inline bool crosshair = false;
        inline bool penetration_crosshair = false;
        inline bool triggerbot = false;
        /** Bit 1=kafa, 2=boyun, 4=pelvis; coklu secim, hedef FOV'da en yakini secilir. */
        inline uint32_t hitbox_mask = 1u;

    };
    namespace rage_aimbot {

    }
    namespace Visuals
    {
        /** Ana player ESP acma/kapama — kapali iken tum player ESP ozellikleri calismiyor. */
        inline bool enablePlayerEsp = true;

        /** 0 Off, 1 full 2 corner 3 filled+outline (ananbaban). */
        inline int boxMode = 1;
        inline float boxRounding = 2.f;
        inline bool filledBox = false;
        inline bool filledGradient = false;
        inline bool filledVisBox = false;
        /** 0: snap to bottom-center, 1: to screen center, 2: line down to screen bottom. */
        inline int snaplineMode = 0;
        inline bool eyeRay = false;
        inline bool ammoBar = false;
        /** Sarjor cubugunun altindaki clip/max metni (weapon ESP'den bagimsiz konum). */
        inline bool ammoText = false;
        inline bool bombWorldEsp = false;
        inline bool bombCarrierEsp = false;
        inline bool showScoped = false;
        inline bool showBlind = false;
        /** Flash varken tum dusman ESP'sini gizle (ananbaban FlashCheck benzeri). */
        inline bool blindHideEsp = false;
        inline bool awpCrosshair = false;
        /** Catalyst projectile ESP: smoke suresi, inferno, ucusan molotov, decoy, HE/flash etiketi. */
        inline bool worldGrenades = false;
        /** Sadece yerel oyuncunun attigi projectile (smoke filtresi icin m_hThrower). */
        inline bool grenadeEspLocalOnly = false;
        /** Gomulu lineup listesi (grenade_lineups_embedded.hpp): yer halkasi + talimat + yakin nisan. */
        inline bool grenadeLineups = false;
        /** 0 = sinirsiz; >0 iken gozden stand noktasina bu mesafeden uzak lineuplar cizilmez (hammer birimi). */
        inline float grenadeLineupMaxDrawDistance = 0.f;
        /** Dunya grenade ESP'de Catalyst weapons font ikonlari (smoke/molly/...). */
        inline bool worldGrenadeIcons = true;
        /** Yerdeki silahlar (weapon entity tarama). */
        inline bool droppedWeaponEsp = false;
        inline bool droppedWeaponText = true;
        inline bool droppedWeaponIcons = true;
        /** C_Inferno ates noktalarindan convex hull (yayilim alani). */
        inline bool worldInfernoHull = true;
        /** Silah ESP'de weapons font ikon + metin (Catalyst text_and_icon). */
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
        /** Kapali = herkes ESP (DM/bot). Acik = sadece karsi takim (5v5). */
        inline bool enemiesOnly = false;
        /** Acikken ESP/renk sadece BVH LOS acikken; BVH yoksa filtre hedefi gorunmez sayilir. */
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
        /** 0: yalnizca inis kenari (en hizli); >0: ardisik tiklar arasi min ms. .cfg: misc.bhop_delay_ms */
        inline int bhop_delay_ms = 0;
        /** Aim / Trigger listesi ayri ImGui penceresi (imgui.ini ile konum). */
        inline bool keybind_list_window = true;
        inline bool radar = false;
        /** Her zaman acik (config yok); dunya ESP/BVH/trajectory worker ile FPS korunur. */
        inline bool save_fps = true;
        inline bool water = false;
        inline bool waterShowOverlayFps = true;
        inline bool waterShowGameFps = false;
        inline bool waterShowPing = true;
        /** 0 kapali, 1 neverlose, 2 skeet (ananbaban Sounds.h). */
        inline int hit_sound = 0;
        inline bool hit_marker = false;
        inline bool spectatorList = false;
        inline bool bombTimer = false;
        inline bool fovChanger = false;
        inline float fov = 0.0f;
        /** Valthrun cloud radar (WSS publish). */
        inline bool cloudRadar = false;
        inline char cloudRadarPublishUrl[288] = "wss://radar.valth.run/publish";
        /** Izleyici: expectional.dev/radar?session=... (iframe Valthrun UI). Sonunda / olmasin. */
        inline char cloudRadarViewerBase[192] = "https://expectional.dev/radar";
        /** ImGui penceresi: radar noktalari (seffaf); harita tablosu varsa konum, yoksa planar. */
        inline bool radarWindow = false;
        inline bool radarWindowDebugLog = false;
        /** Radar penceresi 4:3 en-boy; kapali = kare. */
        inline bool radarWindow43 = false;
        /** Radar harita karesi baslangic boyutu (1080p referans; oyun client cozunurlugu ile olceklenir). */
        inline float radarWindowMapPx = 320.f;
        /** Local oyuncu merkezli gorunum + harita zoom (1=tum harita, buyuk= daha yakin). */
        inline bool radarWindowFollowLocal = true;
        inline float radarWindowFollowZoom = 2.0f;
        /** Pencere radarinda oyuncu daire yari capi carpani (zoom formulu ile carpilir). */
        inline float radarWindowBlipScale = 0.62f;
        /** Bakis acisi ile haritayi dondur (CS2 minimap rotate). Follow+zoom acikken anlamli. */
        inline bool radarWindowRotateWithView = false;
        /** HUD minimap (CCSGO_HudRadar bellek) ile ayni dunya->radar; okuma basarisizsa harita tablosu / planar. */
        inline bool radarWindowHudMatch = false;
        /** CS2 kurulumundan panorama/resource overhead PNG/JPG (client.dll yolu). Yoksa plaka rengi. */
        inline bool radarWindowGameMapTex = true;
        /** Topluluk sunucularında harita yüklenmediğinde siyah arkaplanı gizleme toggle'ı. */
        inline bool radarWindowHideMapImage = false;
        /** Topluluk sunucularında harita yüklenmediğinde radar arkaplan saydamlığı (0-100). */
        inline int radarWindowTransparency = 0;

        /** Rank Revealer ImGui penceresi (konum imgui.ini ile). */
        inline bool rank_reveal_window = false;
        /** Rank Revealer: Steam envanter / Market fiyat ($) — kapali iken istek yok, kod yolu durur. */
        inline bool rank_reveal_inventory_enabled = false;
        /** Vote revealer; konum imgui.ini ile (Vote revealer##ExpectionalVk). */
        inline bool votekick_reveal_window = false;

        /** Sol ustte BVH LOS / mask debug metni. */
        inline bool debug_visible_check = false;
        /** Aim/Trigger sekmesinde widget birakinca aktif .cfg dosyasina otomatik kaydet. */
        inline bool autosave_config =  false;
        /** OBS / Discord ekran yakalamasinda overlay gorunmesin (WDA_EXCLUDEFROMCAPTURE, Win10 2004+). */
        inline bool obsBypass = false;
        /** Config: overlay ust FPS siniri. Kapali = sinirsiz; acik = overlayCustomFpsValue (min 60). */
        inline bool overlayCustomFps = false;
        inline int overlayCustomFpsValue = 144;
        /**
         * Worker thread kalitesi (eskiden menude secimliydi).
         * Artik UI'dan kaldirildi — batch IOCTL + classify cache optimizasyonlarindan
         * sonra "Balanced" (1) hem akici hem dusuk-CPU. Sabit tutulur.
         */
        inline constexpr int workerQuality = 1;

    }
    /** Render / ESP performans ayarlari (grenade_esp.hpp). */
    namespace render_opt {
        /** Dunya grenade entity taramasi kac karede bir (1–32). */
        inline int world_grenade_scan_period_frames = 4;
        /** Grenade helper fizik sim tick ust siniri (128–1024); worker BVH yolu. */
        inline int grenade_helper_max_ticks = 512;
        /** Overlay ust FPS siniri; 0 = sinirsiz (200+ hedef). >0 = max Hz (ornek 144). */
        inline unsigned overlay_max_hz = 0u;
    }
    /** Kategori bazli aim/trigger/RCS; General master switch kapali kategoriler General kullanir. */
    namespace weapon_cfg {
        /** 0=General, 1..5 kategori. */
        inline int editor_category_idx = 0;
        /** cat_custom[0]=Pistols .. [5]=Snipers — General'deki master switch. */
        inline bool cat_custom[6] = {};
    }
}

/** ananbaban tarzi ESP paleti (menu + esp_extras). .cfg: EspUi.* satirlari (r,g,b veya r,g,b,a). */
namespace EspUiColors {
	inline float skel_col[3] = { 1.f, 1.f, 1.f };
	/** Skeleton visible (LOS clear) rengi — esp_visible_only ile birlikte calisir. */
	inline float skel_vis_col[3] = { 0.25f, 0.85f, 0.45f };
	inline float espcol[3] = { 1.f, 1.f, 1.f };
	inline float vis_espcol[3] = { 0.25f, 0.85f, 0.45f };
	inline float esp_fill_col[3] = { 0.23f, 0.28f, 0.55f };
	inline float esp_fill2_col[3] = { 0.45f, 0.22f, 0.55f };
	inline float bomb_esp_col[3] = { 1.f, 0.45f, 0.2f };
	/** Grenade helper: tam opak cizgi / iniş isareti (alpha sabit 255). */
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
	/** Snapline: BVH LOS kapali / BVH yokken tek renk. */
	inline float snapline_esp[3] = { 1.f, 1.f, 1.f };
	/** Snapline: BVH LOS acik (kutu renklerinden bagimsiz). */
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
	/** Head ESP dolgu: RGBA 0..1 (varsayilan ~40/255 alpha). */
	inline float head_circle_fill[4] = { 1.f, 1.f, 1.f, 40.f / 255.f };
	/** Head ESP visible (LOS clear) dolgu. */
	inline float head_circle_vis_fill[4] = { 0.4f, 1.f, 0.55f, 60.f / 255.f };
	/** Aimbot FOV dairesi (overlay). */
	inline float aim_fov_circle[3] = { 250.f / 255.f, 92.f / 255.f, 1.f };
	inline float pen_crosshair_yes[3] = { 50.f / 255.f, 1.f, 50.f / 255.f };
	inline float pen_crosshair_no[3] = { 1.f, 50.f / 255.f, 50.f / 255.f };
	/** Pencere radar: takim / dusman / local renkleri (liste tum oyunculari tasir; ESP Team Check ayri filtreler). */
	inline float radar_team[3] = { 50.f / 255.f, 1.f, 90.f / 255.f };
	inline float radar_enemy[3] = { 1.f, 64.f / 255.f, 64.f / 255.f };
	inline float radar_local[3] = { 120.f / 255.f, 1.f, 120.f / 255.f };
}

/** espLoop icinde aim toggle (toggle modu); Misc keybind listesi icin. */
namespace ExpectionalCombatUiState {
	inline bool aim_toggle_arm = false;
	inline bool aim_key_prev = false;
}

/** Config sekmesinde son yuklenen/kaydedilen ad (autosave). */
inline char ExpectionalActiveCfgName[96] = "default";

/** espLoop tarafindan doldurulur; debug_visible_check acikken cizilir. */
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
    /** Ornek dusman icin BVH LOS (padlenmis isin). */
    bool sample_bvh_los = false;
    /** Son kare BVH durumu (debug). */
    bool bvh_ready = false;
    int bvh_triangle_count = 0;
    char sample_name[40]{};
};
inline ExpectionalVisDbg g_visDbg{};

/**
 * Konsol penceresi basligi — rastgele "Windows Session Environment Broker [...]"
 * (gorev izleyicide daha az belirgin).
 */
inline void ExpectionalRandomConsoleTitleW()
{
	static std::mt19937 rng{ std::random_device{}() };
	std::uniform_int_distribution<unsigned> dist(0x10000000u, 0xFFFFFFFEu);
	wchar_t buf[112];
	swprintf_s(buf, L"Windows Session Environment Broker [%08X]", dist(rng));
	SetConsoleTitleW(buf);
}

