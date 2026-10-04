#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// CS2: offsets.hpp + client_dll.hpp exe icinde RCDATA (Valorant\\offsets\\ → offsets_embed.rc ile link).
namespace offsets {
extern std::ptrdiff_t dwEntityList;
/** client.dll + dwCHud -> CCSGO_Hud* veya CHud* (dump’a gore; 0 ise pattern). */
extern std::ptrdiff_t dwCHud;
/** CGameEntitySystem icinde maksimum entity indeksi (int); client+dwEntityList = GES ise entity_list+this */
extern std::ptrdiff_t dwGameEntitySystem_highestEntityIndex;
extern std::ptrdiff_t dwViewMatrix;
/** client.dll + dwViewAngles -> QAngle (dis hedefleme / nade helper icin m_angEyeAngles yerine). */
extern std::ptrdiff_t dwViewAngles;
extern std::ptrdiff_t dwLocalPlayerController;
extern std::ptrdiff_t dwLocalPlayerPawn;

extern std::ptrdiff_t dwPawnHealth;
extern std::ptrdiff_t dwPlayerPawn;
/** CCSPlayerController::m_hObserverPawn — spectate chain bu pawn uzerinden (m_pObserverServices). */
extern std::ptrdiff_t m_hObserverPawn;
extern std::ptrdiff_t dwSanitizedName;
extern std::ptrdiff_t m_bDormant;
extern std::ptrdiff_t m_iTeamNum;
extern std::ptrdiff_t m_vecOrigin;
extern std::ptrdiff_t m_iHealth;
/** CCSPlayerController::m_bPawnIsAlive (ananbaban Offset.Entity.IsAlive) */
extern std::ptrdiff_t m_bPawnIsAlive;
/** C_BaseEntity::m_fFlags (FL_ONGROUND = bit 0) — bunnyhop icin okuma. */
extern std::ptrdiff_t m_fFlags;
/** C_BaseEntity::m_hOwnerEntity — tasinan silah/C4 hangi entity'e bagli. */
extern std::ptrdiff_t m_hOwnerEntity;
extern std::ptrdiff_t m_flDetectedByEnemySensorTime;
extern std::ptrdiff_t m_ArmorValue;
extern std::ptrdiff_t m_iCompetitiveWins;
extern std::ptrdiff_t m_steamID;
extern std::ptrdiff_t m_iCompetitiveRanking;
extern std::ptrdiff_t m_iCompetitiveRankType;
extern std::ptrdiff_t m_iCompetitiveRankingPredicted_Win;
extern std::ptrdiff_t m_iCompetitiveRankingPredicted_Loss;
extern std::ptrdiff_t m_iCompetitiveRankingPredicted_Tie;

extern std::ptrdiff_t m_pObserverServices;
extern std::ptrdiff_t m_hObserverTarget;
/** C_BasePlayerPawn::m_hController — m_hObserverTarget cozulen proxy uzerinden izlenen CCSPlayerController */
extern std::ptrdiff_t m_hController;

extern std::ptrdiff_t m_pGameSceneNode;
extern std::ptrdiff_t m_vecAbsOrigin;
extern std::ptrdiff_t m_boneArrayFromScene;
extern std::ptrdiff_t m_angEyeAngles;

/** Crosshair entity (ananbaban TriggerBot iIDEntIndex). */
extern std::ptrdiff_t m_iIDEntIndex;
/** Silah ataga hazir (bekleme yok). */
extern std::ptrdiff_t m_bWaitForNoAttack;

/** ananbaban: scope / flash / hız / TTD (spotted). */
extern std::ptrdiff_t m_bIsScoped;
extern std::ptrdiff_t m_flFlashDuration;
/** C_CSPlayerPawn::m_flEmitSoundTime — ananbaban Sound ESP. */
extern std::ptrdiff_t m_flEmitSoundTime;
extern std::ptrdiff_t m_vecAbsVelocity;
/** C_CSPlayerPawn::m_entitySpottedState; maske = +state + 0xC (EntitySpottedState_t::m_bSpottedByMask). */
extern std::ptrdiff_t m_entitySpottedState;

/** client.dll + dwGlobalVars -> CGlobalVars (ananbaban CurrentTime +0x30). */
extern std::ptrdiff_t dwGlobalVars;
extern std::ptrdiff_t dwPlantedC4;

/** engine2.dll + dwNetworkGameClient -> CNetworkGameClient* (a2x / Rolesional offsets.hpp). */
extern std::ptrdiff_t dwNetworkGameClient;
extern std::ptrdiff_t dwBuildNumber;
extern std::ptrdiff_t dwWindowWidth;
extern std::ptrdiff_t dwWindowHeight;

/** RCS: C_CSPlayerPawn / CCSPlayerPawnBase */
extern std::ptrdiff_t m_iShotsFired;
/** Eski dump: QAngle dogrudan pawn uzerinde; yoksa 0. */
extern std::ptrdiff_t m_aimPunchAngle;
/** CS2: C_CSPlayerPawn::m_pAimPunchServices — RCS punch icin pointer zinciri. */
extern std::ptrdiff_t m_pAimPunchServices;
/** CCSPlayer_AimPunchServices::m_unpredictableBaseAngle (QAngle) — punchAddr = *services + rel. */
extern std::ptrdiff_t m_aimPunchUnpredictableRel;
/** CCSPlayer_AimPunchServices::m_predictableBaseAngle — client RCS icin tercih. */
extern std::ptrdiff_t m_aimPunchPredictableRel;
/** client.dll + dwSensitivity -> isaretci; +dwSensitivity_sensitivity icinde float (a2x / ananbaban). */
extern std::ptrdiff_t dwSensitivity;
extern std::ptrdiff_t dwSensitivity_sensitivity;

/** C_BasePlayerPawn::m_pWeaponServices — WeaponServices uzerinde m_hMyWeapons + m_hActiveWeapon. */
extern std::ptrdiff_t m_pWeaponServices;
/** CPlayer_WeaponServices::m_hMyWeapons (envanter silahlari). */
extern std::ptrdiff_t m_hMyWeapons;
extern std::ptrdiff_t m_hActiveWeapon;
/** Silah varligi C_EconEntity + AttributeContainer + Item + ItemDefIndex (dump; m_AttributeManager global parse guvenilmez). */
extern std::ptrdiff_t m_WeaponEcon_AttributeManager;
/** C_EconEntity::m_AttributeManager — yerdeki silah icin bazen 0x1180 yerine bu taban gecerli (Catalyst SCHEMA). */
extern std::ptrdiff_t m_Econ_AttributeManager;
extern std::ptrdiff_t m_AttributeContainer_Item;
extern std::ptrdiff_t m_EconItemView_ItemDefinitionIndex;
/** C_BasePlayerWeapon::m_iClip1 — aktif silah cephane (ananbaban AmmoBar). */
extern std::ptrdiff_t m_iClip1;
/** C_VoteController — client_dll / cs2-sdk (oylama sayilari). */
extern std::ptrdiff_t vote_m_iActiveIssueIndex;
extern std::ptrdiff_t vote_m_iOnlyTeamToVote;
extern std::ptrdiff_t vote_m_nVoteOptionCount;
extern std::ptrdiff_t vote_m_nPotentialVotes;
extern std::ptrdiff_t vote_m_bVotesDirty;
extern std::ptrdiff_t vote_m_bTypeDirty;
extern std::ptrdiff_t vote_m_bIsYesNoVote;
/** CCSPlayerController::m_bCannotBeKicked — kick adayi ipucu (deneysel). */
extern std::ptrdiff_t controller_m_bCannotBeKicked;

/** C_CSPlayerPawn::m_pBulletServices — hitsound / hitmarker icin. */
extern std::ptrdiff_t m_pBulletServices;
/** CCSPlayer_BulletServices::m_totalHitsOnServer */
extern std::ptrdiff_t m_totalHitsOnServer;
/** CCSPlayerController::m_iPing — watermark. */
extern std::ptrdiff_t m_iPing;

/** C_PlantedC4 */
extern std::ptrdiff_t c4_m_flC4Blow;
extern std::ptrdiff_t c4_m_nBombSite;
extern std::ptrdiff_t c4_m_bBeingDefused;
extern std::ptrdiff_t c4_m_flDefuseCountDown;
extern std::ptrdiff_t c4_m_bBombDefused;

/** Catalyst tarzi nade / projectile (client_dll.hpp parse; 0 = devre disi). */
extern std::ptrdiff_t grenade_m_hThrower;
extern std::ptrdiff_t proj_m_nExplodeEffectTickBegin;
extern std::ptrdiff_t smoke_m_nSmokeEffectTickBegin;
extern std::ptrdiff_t smoke_m_bDidSmokeEffect;
extern std::ptrdiff_t decoy_m_nDecoyShotTick;
extern std::ptrdiff_t inferno_m_fireCount;
extern std::ptrdiff_t inferno_m_firePositions;
extern std::ptrdiff_t inferno_m_bFireIsBurning;
extern std::ptrdiff_t inferno_m_nFireEffectTickBegin;
extern std::ptrdiff_t nade_m_bPinPulled;
extern std::ptrdiff_t nade_m_flThrowStrength;
extern std::ptrdiff_t nade_m_fThrowTime;
extern std::ptrdiff_t vdata_m_flThrowVelocity;
extern std::ptrdiff_t entity_m_nSubclassID;

extern std::uint32_t entity_controller_stride;
}

	void ApplyFallbackOffsets();
	bool ExpectionalLoadOffsetsFromLocalFiles(); /** Exe yanindaki offsets/ klasoründen yukler, ag baglantisi YAPMAZ. */
	bool ExpectionalLoadOffsetsFromWeb(); /** Dosyadan yukler (isim tarihi web mirror ile uyumlu). */
	bool HttpGetExpectionalDev(const wchar_t* path, std::string& bodyOut);
