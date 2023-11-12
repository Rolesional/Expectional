#pragma once
#include <Windows.h>

HWND Entryhwnd = NULL;
int processid = 0;
RECT Rect;

ULONG64 client = 0;
uintptr_t global_pawn = 0;

bool isGlow = false;
bool isFOV = 0;
bool noFlashEnabled = false;
bool noHandsEnabled = false;
float originalFOV = 68.0f; 

namespace offsets
{
    std::ptrdiff_t dwEntityList = 0x17ADAE0;
    std::ptrdiff_t dwViewMatrix = 0x180C0E0;
    std::ptrdiff_t dwLocalPlayerController = 0x17FCDB8;
    std::ptrdiff_t dwLocalPlayerPawn = 0x16B9388;

    std::ptrdiff_t dwPawnHealth = 0x7C8;
    std::ptrdiff_t dwPlayerPawn = 0x7BC; //m_hPlayerPawn client.dll.rs
    std::ptrdiff_t dwSanitizedName = 0x720;
    std::ptrdiff_t m_bDormant = 0xE7;
    std::ptrdiff_t m_iTeamNum = 0x3bf;
    std::ptrdiff_t m_vecOrigin = 0x1224; //m_vOldOrigin client.dll.rs
    std::ptrdiff_t m_iHealth = 0x32C; //m_iHealth client.dll.rs
    std::ptrdiff_t  m_flDetectedByEnemySensorTime = 0x13DC;
    std::ptrdiff_t   m_ArmorValue = 0x1508;
    std::ptrdiff_t   m_iCompetitiveWins = 0x744;
}

namespace Settings
{
    bool bMenu = true;
    bool bAimbot = true;
    bool bVisuals = true;
    bool bMisc = true;
    bool bConfig = true;
    namespace aimbot 
    {
        bool aimbot = false;
        inline float aim_fov = 0.0f;
        inline float smooth = 0.0f;
        inline float trigger_delay = 0.0f;
        bool fov_circle = false;
        bool crosshair = false;
        bool triggerbot = false;
        inline  int selectedhitbox = 0;
        inline  const char* hitbox[] = { ("Head") ,("Neck"), ("Pelvis") };



    };
    namespace rage_aimbot {

    }
    namespace Visuals
    {
        int boxMode = 0;
         bool bSnaplines = false;
         bool bDistance = false;
         bool bBox = true;
         bool bHealth = false;
         bool headcircle = false;
         bool bones = false;
         bool glow = false;
         bool distance = false;
         bool armor = false;
         bool names = false;
         bool noflash = false;
         bool nohands = false;
         bool chams = false;
         bool ragdoll = false;
         bool nightmode = false;

         bool box = false;
         inline  int healthtype = 0;
         inline  const char* healtht[] = { ("Text") ,("Bar"), ("Both") };

         static const char* boxStyle[] =
         {
             "Off",
             "Box",
             "Corner",
         };

         float BoxWidth = 1.0f;
    }
    namespace misc {
        bool bhop = false;
        bool radar = false;
        bool save_fps = false;
        bool water = false;
        bool fovChanger = false;
        inline float fov = 0.0f;

    }
}

