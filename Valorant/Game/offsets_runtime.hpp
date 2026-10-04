#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace offsets {
extern std::ptrdiff_t dwEntityList;

extern std::ptrdiff_t dwCHud;

extern std::ptrdiff_t dwGameEntitySystem_highestEntityIndex;
extern std::ptrdiff_t dwViewMatrix;

extern std::ptrdiff_t dwViewAngles;
extern std::ptrdiff_t dwLocalPlayerController;
extern std::ptrdiff_t dwLocalPlayerPawn;

extern std::ptrdiff_t dwPawnHealth;
extern std::ptrdiff_t dwPlayerPawn;

extern std::ptrdiff_t m_hObserverPawn;
extern std::ptrdiff_t dwSanitizedName;
extern std::ptrdiff_t m_bDormant;
extern std::ptrdiff_t m_iTeamNum;
extern std::ptrdiff_t m_vecOrigin;
extern std::ptrdiff_t m_iHealth;

extern std::ptrdiff_t m_bPawnIsAlive;

extern std::ptrdiff_t m_fFlags;

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

extern std::ptrdiff_t m_hController;

extern std::ptrdiff_t m_pGameSceneNode;
extern std::ptrdiff_t m_vecAbsOrigin;
extern std::ptrdiff_t m_boneArrayFromScene;
extern std::ptrdiff_t m_angEyeAngles;

extern std::ptrdiff_t m_iIDEntIndex;

extern std::ptrdiff_t m_bWaitForNoAttack;

extern std::ptrdiff_t m_bIsScoped;
extern std::ptrdiff_t m_flFlashDuration;

extern std::ptrdiff_t m_flEmitSoundTime;
extern std::ptrdiff_t m_vecAbsVelocity;

extern std::ptrdiff_t m_entitySpottedState;

extern std::ptrdiff_t dwGlobalVars;
extern std::ptrdiff_t dwPlantedC4;

extern std::ptrdiff_t dwNetworkGameClient;
extern std::ptrdiff_t dwBuildNumber;
extern std::ptrdiff_t dwWindowWidth;
extern std::ptrdiff_t dwWindowHeight;

extern std::ptrdiff_t m_iShotsFired;

extern std::ptrdiff_t m_aimPunchAngle;

extern std::ptrdiff_t m_pAimPunchServices;

extern std::ptrdiff_t m_aimPunchUnpredictableRel;

extern std::ptrdiff_t m_aimPunchPredictableRel;

extern std::ptrdiff_t dwSensitivity;
extern std::ptrdiff_t dwSensitivity_sensitivity;

extern std::ptrdiff_t m_pWeaponServices;

extern std::ptrdiff_t m_hMyWeapons;
extern std::ptrdiff_t m_hActiveWeapon;

extern std::ptrdiff_t m_WeaponEcon_AttributeManager;

extern std::ptrdiff_t m_Econ_AttributeManager;
extern std::ptrdiff_t m_AttributeContainer_Item;
extern std::ptrdiff_t m_EconItemView_ItemDefinitionIndex;

extern std::ptrdiff_t m_iClip1;

extern std::ptrdiff_t vote_m_iActiveIssueIndex;
extern std::ptrdiff_t vote_m_iOnlyTeamToVote;
extern std::ptrdiff_t vote_m_nVoteOptionCount;
extern std::ptrdiff_t vote_m_nPotentialVotes;
extern std::ptrdiff_t vote_m_bVotesDirty;
extern std::ptrdiff_t vote_m_bTypeDirty;
extern std::ptrdiff_t vote_m_bIsYesNoVote;

extern std::ptrdiff_t controller_m_bCannotBeKicked;

extern std::ptrdiff_t m_pBulletServices;

extern std::ptrdiff_t m_totalHitsOnServer;

extern std::ptrdiff_t m_iPing;

extern std::ptrdiff_t c4_m_flC4Blow;
extern std::ptrdiff_t c4_m_nBombSite;
extern std::ptrdiff_t c4_m_bBeingDefused;
extern std::ptrdiff_t c4_m_flDefuseCountDown;
extern std::ptrdiff_t c4_m_bBombDefused;

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

	bool ExpectionalLoadOffsetsFromLocalFiles(); 
