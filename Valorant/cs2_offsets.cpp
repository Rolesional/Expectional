#include "Game/offsets_runtime.hpp"
#include "offsets_embed_resource.h"

#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Protection/vxlang_per_tu.hpp"

namespace {

static bool LoadFileToString(const wchar_t* filePath, std::string& out) {
	out.clear();
	if (!filePath || !filePath[0])
		return false;

	FILE* f = nullptr;
	if (_wfopen_s(&f, filePath, L"rb") != 0 || !f)
		return false;

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size <= 0) {
		fclose(f);
		return false;
	}

	std::vector<char> buf(size);
	if (fread(buf.data(), 1, size, f) != static_cast<size_t>(size)) {
		fclose(f);
		return false;
	}
	fclose(f);
	out.assign(buf.data(), buf.size());
	return !out.empty();
}

} 

namespace offsets {
std::ptrdiff_t dwEntityList = 0;
std::ptrdiff_t dwCHud = 0;
std::ptrdiff_t dwGameEntitySystem_highestEntityIndex = 0;
std::ptrdiff_t dwViewMatrix = 0;
std::ptrdiff_t dwViewAngles = 0;
std::ptrdiff_t dwLocalPlayerController = 0;
std::ptrdiff_t dwLocalPlayerPawn = 0;
std::ptrdiff_t dwPawnHealth = 0;
std::ptrdiff_t dwPlayerPawn = 0;
std::ptrdiff_t m_hObserverPawn = 0;
std::ptrdiff_t dwSanitizedName = 0;
std::ptrdiff_t m_bDormant = 0;
std::ptrdiff_t m_iTeamNum = 0;
std::ptrdiff_t m_vecOrigin = 0;
std::ptrdiff_t m_iHealth = 0;
std::ptrdiff_t m_bPawnIsAlive = 0;
std::ptrdiff_t m_fFlags = 0;
std::ptrdiff_t m_hOwnerEntity = 0;
std::ptrdiff_t m_flDetectedByEnemySensorTime = 0;
std::ptrdiff_t m_ArmorValue = 0;
std::ptrdiff_t m_iCompetitiveWins = 0;
std::ptrdiff_t m_steamID = 0;
std::ptrdiff_t m_iCompetitiveRanking = 0;
std::ptrdiff_t m_iCompetitiveRankType = 0;
std::ptrdiff_t m_iCompetitiveRankingPredicted_Win = 0;
std::ptrdiff_t m_iCompetitiveRankingPredicted_Loss = 0;
std::ptrdiff_t m_iCompetitiveRankingPredicted_Tie = 0;
std::ptrdiff_t m_pObserverServices = 0;
std::ptrdiff_t m_hObserverTarget = 0;
std::ptrdiff_t m_hController = 0;
std::ptrdiff_t m_pGameSceneNode = 0;
std::ptrdiff_t m_vecAbsOrigin = 0;
std::ptrdiff_t m_boneArrayFromScene = 0;
std::ptrdiff_t m_angEyeAngles = 0;
std::ptrdiff_t m_iIDEntIndex = 0;
std::ptrdiff_t m_bWaitForNoAttack = 0;
std::ptrdiff_t m_bIsScoped = 0;
std::ptrdiff_t m_flFlashDuration = 0;
std::ptrdiff_t m_flEmitSoundTime = 0;
std::ptrdiff_t m_vecAbsVelocity = 0;
std::ptrdiff_t m_entitySpottedState = 0;
std::ptrdiff_t dwGlobalVars = 0;
std::ptrdiff_t dwPlantedC4 = 0;
std::ptrdiff_t dwNetworkGameClient = 0;
std::ptrdiff_t dwBuildNumber = 0;
std::ptrdiff_t dwWindowWidth = 0;
std::ptrdiff_t dwWindowHeight = 0;
std::ptrdiff_t m_pWeaponServices = 0;
std::ptrdiff_t m_hMyWeapons = 0;
std::ptrdiff_t m_hActiveWeapon = 0;
std::ptrdiff_t m_WeaponEcon_AttributeManager = 0;
std::ptrdiff_t m_Econ_AttributeManager = 0;
std::ptrdiff_t m_AttributeContainer_Item = 0;
std::ptrdiff_t m_EconItemView_ItemDefinitionIndex = 0;
std::ptrdiff_t m_iClip1 = 0;
std::ptrdiff_t vote_m_iActiveIssueIndex = 0;
std::ptrdiff_t vote_m_iOnlyTeamToVote = 0;
std::ptrdiff_t vote_m_nVoteOptionCount = 0;
std::ptrdiff_t vote_m_nPotentialVotes = 0;
std::ptrdiff_t vote_m_bVotesDirty = 0;
std::ptrdiff_t vote_m_bTypeDirty = 0;
std::ptrdiff_t vote_m_bIsYesNoVote = 0;
std::ptrdiff_t controller_m_bCannotBeKicked = 0;
std::ptrdiff_t m_pBulletServices = 0;
std::ptrdiff_t m_totalHitsOnServer = 0;
std::ptrdiff_t m_iPing = 0;
std::ptrdiff_t m_iShotsFired = 0;
std::ptrdiff_t m_aimPunchAngle = 0;
std::ptrdiff_t m_pAimPunchServices = 0;
std::ptrdiff_t m_aimPunchUnpredictableRel = 0;
std::ptrdiff_t m_aimPunchPredictableRel = 0;
std::ptrdiff_t dwSensitivity = 0;
std::ptrdiff_t dwSensitivity_sensitivity = 0x58;
std::ptrdiff_t c4_m_flC4Blow = 0;
std::ptrdiff_t c4_m_nBombSite = 0;
std::ptrdiff_t c4_m_bBeingDefused = 0;
std::ptrdiff_t c4_m_flDefuseCountDown = 0;
std::ptrdiff_t c4_m_bBombDefused = 0;
std::ptrdiff_t grenade_m_hThrower = 0;
std::ptrdiff_t proj_m_nExplodeEffectTickBegin = 0;
std::ptrdiff_t smoke_m_nSmokeEffectTickBegin = 0;
std::ptrdiff_t smoke_m_bDidSmokeEffect = 0;
std::ptrdiff_t decoy_m_nDecoyShotTick = 0;
std::ptrdiff_t inferno_m_fireCount = 0;
std::ptrdiff_t inferno_m_firePositions = 0;
std::ptrdiff_t inferno_m_bFireIsBurning = 0;
std::ptrdiff_t inferno_m_nFireEffectTickBegin = 0;
std::ptrdiff_t nade_m_bPinPulled = 0;
std::ptrdiff_t nade_m_flThrowStrength = 0;
std::ptrdiff_t nade_m_fThrowTime = 0;
std::ptrdiff_t vdata_m_flThrowVelocity = 0;
std::ptrdiff_t entity_m_nSubclassID = 0;
std::uint32_t entity_controller_stride = 112;
}

static const uint8_t* ExpectionalGetSelfBase()
{
	
	HMODULE mod = nullptr;
	if (GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&ExpectionalGetSelfBase),
			&mod) && mod)
	{
		return reinterpret_cast<const uint8_t*>(mod);
	}

	MEMORY_BASIC_INFORMATION mbi{};
	if (VirtualQuery(reinterpret_cast<const void*>(&ExpectionalGetSelfBase), &mbi, sizeof(mbi)) == sizeof(mbi)
	    && mbi.AllocationBase)
	{
		return reinterpret_cast<const uint8_t*>(mbi.AllocationBase);
	}
	return nullptr;
}

static bool ExpectionalFindRcDataInPE(const uint8_t* base, int resourceId, const uint8_t*& outData, uint32_t& outSize)
{
	outData = nullptr;
	outSize = 0;
	if (!base) return false;
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
	const IMAGE_DATA_DIRECTORY& dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE];
	if (!dd.VirtualAddress || !dd.Size) return false;

	const uint8_t* resBase = base + dd.VirtualAddress;
	const auto* root = reinterpret_cast<const IMAGE_RESOURCE_DIRECTORY*>(resBase);

	auto entriesOf = [](const IMAGE_RESOURCE_DIRECTORY* d) {
		return reinterpret_cast<const IMAGE_RESOURCE_DIRECTORY_ENTRY*>(d + 1);
	};

	const auto* L1 = entriesOf(root);
	const int n1 = root->NumberOfNamedEntries + root->NumberOfIdEntries;
	for (int i = 0; i < n1; ++i) {
		
		if (L1[i].NameIsString) continue;
		if (L1[i].Id != static_cast<DWORD>(reinterpret_cast<uintptr_t>(RT_RCDATA))) continue;
		if (!L1[i].DataIsDirectory) continue;
		const auto* d2 = reinterpret_cast<const IMAGE_RESOURCE_DIRECTORY*>(resBase + L1[i].OffsetToDirectory);
		const auto* L2 = entriesOf(d2);
		const int n2 = d2->NumberOfNamedEntries + d2->NumberOfIdEntries;
		for (int j = 0; j < n2; ++j) {
			if (L2[j].NameIsString) continue;
			if (L2[j].Id != static_cast<DWORD>(resourceId)) continue;
			if (!L2[j].DataIsDirectory) continue;
			const auto* d3 = reinterpret_cast<const IMAGE_RESOURCE_DIRECTORY*>(resBase + L2[j].OffsetToDirectory);
			const auto* L3 = entriesOf(d3);
			const int n3 = d3->NumberOfNamedEntries + d3->NumberOfIdEntries;
			if (n3 <= 0) return false;
			
			if (L3[0].DataIsDirectory) return false;
			const auto* de = reinterpret_cast<const IMAGE_RESOURCE_DATA_ENTRY*>(resBase + L3[0].OffsetToData);
			outData = base + de->OffsetToData;
			outSize = de->Size;
			return outSize != 0;
		}
	}
	return false;
}

static bool LoadRcDataToString(int resourceId, std::string& out)
{
	out.clear();
	const uint8_t* base = ExpectionalGetSelfBase();
	const uint8_t* data = nullptr;
	uint32_t sz = 0;
	if (ExpectionalFindRcDataInPE(base, resourceId, data, sz) && data && sz) {
		out.assign(reinterpret_cast<const char*>(data), reinterpret_cast<const char*>(data) + sz);
		return !out.empty();
	}

	HMODULE mod = nullptr;
	GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&LoadRcDataToString),
		&mod);
	if (!mod) return false;
	HRSRC res = FindResourceW(mod, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
	if (!res) return false;
	const DWORD rsz = SizeofResource(mod, res);
	if (!rsz) return false;
	HGLOBAL hg = LoadResource(mod, res);
	if (!hg) return false;
	const void* p = LockResource(hg);
	if (!p) return false;
	out.assign(static_cast<const char*>(p), static_cast<const char*>(p) + rsz);
	return !out.empty();
}

static void TrimInPlace(std::string& s) {
    while (!s.empty() && (unsigned char)s.front() <= ' ') s.erase(0, 1);
    while (!s.empty() && (unsigned char)s.back() <= ' ') s.pop_back();
}

static std::ptrdiff_t ParseOffset(const std::string& content, const std::string& name) {
    const std::string searchKey = name + " = ";
    size_t pos = content.find(searchKey);
    if (pos == std::string::npos) return 0;
    pos += searchKey.length();
    while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t')) ++pos;
    const size_t end = content.find(';', pos);
    if (end == std::string::npos) return 0;
    std::string hexStr = content.substr(pos, end - pos);
    TrimInPlace(hexStr);
    const size_t slash = hexStr.find("//");
    if (slash != std::string::npos) hexStr.resize(slash);
    TrimInPlace(hexStr);
    if (hexStr.empty()) return 0;
    try {
        return static_cast<std::ptrdiff_t>(std::stoll(hexStr, nullptr, 0));
    } catch (...) {
        return 0;
    }
}

static bool TryApplyInfernoOffsetsFromClassBlock(const std::string& clientContent) {
    using namespace offsets;
    static const char* kMarkers[] = {
        "public static class C_Inferno",
        "namespace C_Inferno",
        "class C_Inferno",
        "struct C_Inferno",
    };
    for (const char* mk : kMarkers) {
        size_t p = clientContent.find(mk);
        while (p != std::string::npos) {
            const size_t braceOpen = clientContent.find('{', p);
            if (braceOpen == std::string::npos)
                break;
            int depth = 1;
            size_t j = braceOpen + 1;
            for (; j < clientContent.size() && depth > 0; ++j) {
                if (clientContent[j] == '{')
                    ++depth;
                else if (clientContent[j] == '}')
                    --depth;
            }
            if (depth != 0)
                break;
            const std::string block = clientContent.substr(braceOpen, j - braceOpen);
            if (block.find("m_firePositions") == std::string::npos || block.find("m_fireCount") == std::string::npos) {
                p = clientContent.find(mk, p + 1);
                continue;
            }
            const std::ptrdiff_t fc = ParseOffset(block, "m_fireCount");
            const std::ptrdiff_t pos = ParseOffset(block, "m_firePositions");
            const std::ptrdiff_t burn = ParseOffset(block, "m_bFireIsBurning");
            const std::ptrdiff_t eff = ParseOffset(block, "m_nFireEffectTickBegin");
            if (fc && pos && fc > 0 && pos > 0) {
                inferno_m_fireCount = fc;
                inferno_m_firePositions = pos;
                if (burn)
                    inferno_m_bFireIsBurning = burn;
                if (eff)
                    inferno_m_nFireEffectTickBegin = eff;
                
                    (unsigned long long)inferno_m_fireCount,
                    (unsigned long long)inferno_m_firePositions,
                    (unsigned long long)inferno_m_bFireIsBurning,
                    (unsigned long long)inferno_m_nFireEffectTickBegin;
                return true;
            }
            p = clientContent.find(mk, p + 1);
        }
    }
    return false;
}

static bool TryApplyEconEntityOffsetsFromClassBlock(const std::string& clientContent) {
    using namespace offsets;
    static const char* kMarkers[] = {
        "public static class C_EconEntity",
        "namespace C_EconEntity",
        "class C_EconEntity",
        "struct C_EconEntity",
    };
    for (const char* mk : kMarkers) {
        size_t p = clientContent.find(mk);
        while (p != std::string::npos) {
            const size_t braceOpen = clientContent.find('{', p);
            if (braceOpen == std::string::npos)
                break;
            int depth = 1;
            size_t j = braceOpen + 1;
            for (; j < clientContent.size() && depth > 0; ++j) {
                if (clientContent[j] == '{')
                    ++depth;
                else if (clientContent[j] == '}')
                    --depth;
            }
            if (depth != 0)
                break;
            const std::string block = clientContent.substr(braceOpen, j - braceOpen);
            if (block.find("m_AttributeManager") == std::string::npos ||
                block.find("m_Item") == std::string::npos) {
                p = clientContent.find(mk, p + 1);
                continue;
            }
            const std::ptrdiff_t am = ParseOffset(block, "m_AttributeManager");
            if (am > 0 && am < 0x4000) {
                m_Econ_AttributeManager = am;
               
                    (unsigned long long)m_Econ_AttributeManager;
                return true;
            }
            p = clientContent.find(mk, p + 1);
        }
    }
    return false;
}

static bool TryApplyControllerRankFieldsFromDump(const std::string& clientContent) {
    using namespace offsets;
    auto parseBlock = [&](const char* mk) -> std::string {
        size_t p = clientContent.find(mk);
        if (p == std::string::npos)
            return {};
        const size_t braceOpen = clientContent.find('{', p);
        if (braceOpen == std::string::npos)
            return {};
        int depth = 1;
        size_t j = braceOpen + 1;
        for (; j < clientContent.size() && depth > 0; ++j) {
            if (clientContent[j] == '{')
                ++depth;
            else if (clientContent[j] == '}')
                --depth;
        }
        if (depth != 0)
            return {};
        return clientContent.substr(braceOpen, j - braceOpen);
    };
    const std::string baseBlock = parseBlock("namespace CBasePlayerController {");
    const std::string ctrlBlock = parseBlock("namespace CCSPlayerController {");
    bool ok = false;
    if (!baseBlock.empty() && baseBlock.find("m_steamID") != std::string::npos) {
        const std::ptrdiff_t sid = ParseOffset(baseBlock, "m_steamID");
        if (sid > 0 && sid < 0x2000) {
            m_steamID = sid;
            ok = true;
        }
    }
    if (!ctrlBlock.empty() && ctrlBlock.find("m_iCompetitiveRanking") != std::string::npos) {
        const std::ptrdiff_t rnk = ParseOffset(ctrlBlock, "m_iCompetitiveRanking");
        const std::ptrdiff_t wins = ParseOffset(ctrlBlock, "m_iCompetitiveWins");
        const std::ptrdiff_t rt = ParseOffset(ctrlBlock, "m_iCompetitiveRankType");
        const std::ptrdiff_t pw = ParseOffset(ctrlBlock, "m_iCompetitiveRankingPredicted_Win");
        const std::ptrdiff_t pl = ParseOffset(ctrlBlock, "m_iCompetitiveRankingPredicted_Loss");
        const std::ptrdiff_t pt = ParseOffset(ctrlBlock, "m_iCompetitiveRankingPredicted_Tie");
        if (rnk > 0 && rnk < 0x4000) {
            m_iCompetitiveRanking = rnk;
            ok = true;
        }
        if (wins > 0 && wins < 0x4000)
            m_iCompetitiveWins = wins;
        if (rt > 0 && rt < 0x4000)
            m_iCompetitiveRankType = rt;
        if (pw > 0 && pw < 0x4000)
            m_iCompetitiveRankingPredicted_Win = pw;
        if (pl > 0 && pl < 0x4000)
            m_iCompetitiveRankingPredicted_Loss = pl;
        if (pt > 0 && pt < 0x4000)
            m_iCompetitiveRankingPredicted_Tie = pt;
        if (ctrlBlock.find("m_bCannotBeKicked") != std::string::npos) {
            const std::ptrdiff_t cblk = ParseOffset(ctrlBlock, "m_bCannotBeKicked");
            if (cblk > 0 && cblk < 0x4000)
                controller_m_bCannotBeKicked = cblk;
        }
    }
    return ok;
}

static bool TryApplyVoteControllerOffsetsFromClassBlock(const std::string& clientContent)
{
    using namespace offsets;
    static const char* mk = "namespace C_VoteController {";
    size_t p = clientContent.find(mk);
    if (p == std::string::npos)
        return false;
    const size_t braceOpen = clientContent.find('{', p);
    if (braceOpen == std::string::npos)
        return false;
    int depth = 1;
    size_t j = braceOpen + 1;
    for (; j < clientContent.size() && depth > 0; ++j) {
        if (clientContent[j] == '{')
            ++depth;
        else if (clientContent[j] == '}')
            --depth;
    }
    if (depth != 0)
        return false;
    const std::string block = clientContent.substr(braceOpen, j - braceOpen);
    if (block.find("m_iActiveIssueIndex") == std::string::npos)
        return false;
    const std::ptrdiff_t ai = ParseOffset(block, "m_iActiveIssueIndex");
    const std::ptrdiff_t ot = ParseOffset(block, "m_iOnlyTeamToVote");
    const std::ptrdiff_t vc = ParseOffset(block, "m_nVoteOptionCount");
    const std::ptrdiff_t pv = ParseOffset(block, "m_nPotentialVotes");
    const std::ptrdiff_t bd = ParseOffset(block, "m_bVotesDirty");
    const std::ptrdiff_t td = ParseOffset(block, "m_bTypeDirty");
    const std::ptrdiff_t yn = ParseOffset(block, "m_bIsYesNoVote");
    if (ai <= 0 || ai >= 0x4000)
        return false;
    vote_m_iActiveIssueIndex = ai;
    if (ot > 0 && ot < 0x4000)
        vote_m_iOnlyTeamToVote = ot;
    if (vc > 0 && vc < 0x4000)
        vote_m_nVoteOptionCount = vc;
    if (pv > 0 && pv < 0x4000)
        vote_m_nPotentialVotes = pv;
    if (bd > 0 && bd < 0x4000)
        vote_m_bVotesDirty = bd;
    if (td > 0 && td < 0x4000)
        vote_m_bTypeDirty = td;
    if (yn > 0 && yn < 0x4000)
        vote_m_bIsYesNoVote = yn;
    return true;
}

void ApplyFallbackOffsets() {
    using namespace offsets;
    dwEntityList = 0x254EE60;
    dwGameEntitySystem_highestEntityIndex = 0x2090;
    dwViewMatrix = 0x23A9340;
    dwViewAngles = 0x23B9C78;
    dwLocalPlayerPawn = 0x23A4238;
    dwLocalPlayerController = 0x237EBA0;
    m_vecOrigin = 0x13B8;
    m_iTeamNum = 0x3E7;
    dwPlayerPawn = 0x914;
    m_hObserverPawn = 0x918;
    m_iHealth = 0x34C;
    m_bPawnIsAlive = 0x91C;
    m_pGameSceneNode = 0x330;
    m_vecAbsOrigin = 0xC8;
    {
        const std::ptrdiff_t m_modelState = 0x140;
        m_boneArrayFromScene = m_modelState + 0x80;
    }
    m_angEyeAngles = 0x3340;
    m_iIDEntIndex = 0x341C;
    m_bWaitForNoAttack = 0x1C90;
    m_bIsScoped = 0x1C70;
    m_flFlashDuration = 0x1428;
    m_flEmitSoundTime = 0x1C78;
    m_vecAbsVelocity = 0x3F8;
    m_entitySpottedState = 0x1C58;
    m_ArmorValue = 0x1C9C;
    entity_controller_stride = 112;
    m_pObserverServices = 0x1220;
    m_hObserverTarget = 0x4C;
    m_hController = 0x13D0;
    dwGlobalVars = 0x208FD60;
    dwPlantedC4 = 0x236E678;
    dwNetworkGameClient = 0x90D4B0;
    dwBuildNumber = 0x60F594;
    dwWindowWidth = 0x9118D0;
    dwWindowHeight = 0x9118D4;
    m_pWeaponServices = 0x1208;
    m_hMyWeapons = 0x48;
    m_hActiveWeapon = 0x60;
    m_WeaponEcon_AttributeManager = 0x11A8;
    m_Econ_AttributeManager = m_WeaponEcon_AttributeManager;
    m_AttributeContainer_Item = 0x50;
    m_EconItemView_ItemDefinitionIndex = 0x1BA;
    m_iClip1 = 0x1700;
    vote_m_iActiveIssueIndex = 0x610;
    vote_m_iOnlyTeamToVote = 0x614;
    vote_m_nVoteOptionCount = 0x618;
    vote_m_nPotentialVotes = 0x62C;
    vote_m_bVotesDirty = 0x630;
    vote_m_bTypeDirty = 0x631;
    vote_m_bIsYesNoVote = 0x632;
    controller_m_bCannotBeKicked = 0x8E8;
    m_pBulletServices = 0x1490;
    m_totalHitsOnServer = 0x48;
    m_iPing = 0x830;
    c4_m_flC4Blow = 0x11D0;
    c4_m_nBombSite = 0x11A4;
    c4_m_bBeingDefused = 0x11DC;
    c4_m_flDefuseCountDown = 0x11F0;
    c4_m_bBombDefused = 0x11F4;
    m_fFlags = 0x3F4;
    m_hOwnerEntity = 0x520;
    m_steamID = 0x780;
    m_iCompetitiveRanking = 0x888;
    m_iCompetitiveWins = 0x88C;
    m_iCompetitiveRankType = 0x890;
    m_iCompetitiveRankingPredicted_Win = 0x894;
    m_iCompetitiveRankingPredicted_Loss = 0x898;
    m_iCompetitiveRankingPredicted_Tie = 0x89C;
    m_iShotsFired = 0x1C84;
    m_pAimPunchServices = 0x14B8;
    m_aimPunchUnpredictableRel = 0xA4;
    m_aimPunchPredictableRel = 0x50;
    m_aimPunchAngle = 0x16CC;
    
    inferno_m_firePositions = 0x1020;
    inferno_m_bFireIsBurning = 0x1620;
    inferno_m_fireCount = 0x1960;
    inferno_m_nFireEffectTickBegin = 0x1974;
}

static bool TryApplyFromDump(const std::string& offsetsContent, const std::string& clientContent, const char* sourceTag) {
    const std::ptrdiff_t t_dwEntityList = ParseOffset(offsetsContent, "dwEntityList");
    std::ptrdiff_t t_gesHi = ParseOffset(offsetsContent, "dwGameEntitySystem_highestEntityIndex");
    if (!t_gesHi) t_gesHi = 0x2090;
    const std::ptrdiff_t t_dwViewMatrix = ParseOffset(offsetsContent, "dwViewMatrix");
    const std::ptrdiff_t t_dwViewAngles = ParseOffset(offsetsContent, "dwViewAngles");
    const std::ptrdiff_t t_dwLocalPlayerPawn = ParseOffset(offsetsContent, "dwLocalPlayerPawn");
    const std::ptrdiff_t t_dwLocalPlayerController = ParseOffset(offsetsContent, "dwLocalPlayerController");
    const std::ptrdiff_t t_dwCHud = ParseOffset(offsetsContent, "dwCHud");

    const std::ptrdiff_t t_m_iHealth = ParseOffset(clientContent, "m_iHealth");
    const std::ptrdiff_t t_m_bPawnIsAlive = ParseOffset(clientContent, "m_bPawnIsAlive");
    const std::ptrdiff_t t_m_iTeamNum = ParseOffset(clientContent, "m_iTeamNum");
    const std::ptrdiff_t t_m_vecOrigin = ParseOffset(clientContent, "m_vOldOrigin");
    const std::ptrdiff_t t_m_hPlayerPawn = ParseOffset(clientContent, "m_hPlayerPawn");
    const std::ptrdiff_t t_m_hPawn = ParseOffset(clientContent, "m_hPawn");
    const std::ptrdiff_t t_dwPlayerPawn = t_m_hPlayerPawn ? t_m_hPlayerPawn : t_m_hPawn;
    const std::ptrdiff_t t_m_hObserverPawn = ParseOffset(clientContent, "m_hObserverPawn");
    const std::ptrdiff_t t_iszName = ParseOffset(clientContent, "m_iszPlayerName");
    const std::ptrdiff_t t_sSanName = ParseOffset(clientContent, "m_sSanitizedPlayerName");
    const std::ptrdiff_t t_dwSanitizedName = t_iszName ? t_iszName : t_sSanName;
    const std::ptrdiff_t t_m_pGameSceneNode = ParseOffset(clientContent, "m_pGameSceneNode");
    const std::ptrdiff_t t_m_vecAbsOrigin = ParseOffset(clientContent, "m_vecAbsOrigin");
    const std::ptrdiff_t t_m_angEyeAngles = ParseOffset(clientContent, "m_angEyeAngles");
    const std::ptrdiff_t t_m_iIDEntIndex = ParseOffset(clientContent, "m_iIDEntIndex");
    const std::ptrdiff_t t_m_bWaitForNoAttack = ParseOffset(clientContent, "m_bWaitForNoAttack");
    const std::ptrdiff_t t_m_bIsScoped = ParseOffset(clientContent, "m_bIsScoped");
    const std::ptrdiff_t t_m_flFlashDuration = ParseOffset(clientContent, "m_flFlashDuration");
    const std::ptrdiff_t t_m_flEmitSoundTime = ParseOffset(clientContent, "m_flEmitSoundTime");
    const std::ptrdiff_t t_m_vecAbsVelocity = ParseOffset(clientContent, "m_vecAbsVelocity");
    const std::ptrdiff_t t_m_entitySpottedState = ParseOffset(clientContent, "m_entitySpottedState");
    const std::ptrdiff_t t_m_modelState = ParseOffset(clientContent, "m_modelState");
    const std::ptrdiff_t t_m_boneArrayFromScene = t_m_modelState ? (t_m_modelState + 0x80) : 0;
    const std::ptrdiff_t t_m_ArmorValue = ParseOffset(clientContent, "m_ArmorValue");
    const std::ptrdiff_t t_m_bDormant = ParseOffset(clientContent, "m_bDormant");
    const std::ptrdiff_t t_m_fFlags = ParseOffset(clientContent, "m_fFlags");
    const std::ptrdiff_t t_m_hOwnerEntity = ParseOffset(clientContent, "m_hOwnerEntity");
    const std::ptrdiff_t t_m_pObserverServices = ParseOffset(clientContent, "m_pObserverServices");
    const std::ptrdiff_t t_m_hObserverTarget = ParseOffset(clientContent, "m_hObserverTarget");
    const std::ptrdiff_t t_m_hController = ParseOffset(clientContent, "m_hController");

    const std::ptrdiff_t t_dwPlantedC4 = ParseOffset(offsetsContent, "dwPlantedC4");
    const std::ptrdiff_t t_dwGlobalVars = ParseOffset(offsetsContent, "dwGlobalVars");
    const std::ptrdiff_t t_dwNetworkGameClient = ParseOffset(offsetsContent, "dwNetworkGameClient");
    const std::ptrdiff_t t_dwBuildNumber = ParseOffset(offsetsContent, "dwBuildNumber");
    const std::ptrdiff_t t_dwWindowWidth = ParseOffset(offsetsContent, "dwWindowWidth");
    const std::ptrdiff_t t_dwWindowHeight = ParseOffset(offsetsContent, "dwWindowHeight");
    const std::ptrdiff_t t_m_pWeaponServices = ParseOffset(clientContent, "m_pWeaponServices");
    const std::ptrdiff_t t_m_hMyWeapons = ParseOffset(clientContent, "m_hMyWeapons");
    const std::ptrdiff_t t_m_hActiveWeapon = ParseOffset(clientContent, "m_hActiveWeapon");
    const std::ptrdiff_t t_m_Item = ParseOffset(clientContent, "m_Item");
    const std::ptrdiff_t t_m_iItemDefinitionIndex = ParseOffset(clientContent, "m_iItemDefinitionIndex");
    const std::ptrdiff_t t_m_iClip1 = ParseOffset(clientContent, "m_iClip1");
    const std::ptrdiff_t t_m_pBulletServices = ParseOffset(clientContent, "m_pBulletServices");
    const std::ptrdiff_t t_m_totalHitsOnServer = ParseOffset(clientContent, "m_totalHitsOnServer");
    const std::ptrdiff_t t_m_iPing = ParseOffset(clientContent, "m_iPing");
    const std::ptrdiff_t t_m_iShotsFired = ParseOffset(clientContent, "m_iShotsFired");
    const std::ptrdiff_t t_m_aimPunchAngle = ParseOffset(clientContent, "m_aimPunchAngle");
    const std::ptrdiff_t t_m_pAimPunchServices = ParseOffset(clientContent, "m_pAimPunchServices");
    const std::ptrdiff_t t_m_aimPunchUnpredictableRel = ParseOffset(clientContent, "m_unpredictableBaseAngle");
    const std::ptrdiff_t t_m_aimPunchPredictableRel = ParseOffset(clientContent, "m_predictableBaseAngle");
    const std::ptrdiff_t t_dwSensitivity = ParseOffset(offsetsContent, "dwSensitivity");
    const std::ptrdiff_t t_dwSensVal = ParseOffset(offsetsContent, "dwSensitivity_sensitivity");

    const std::ptrdiff_t t_m_flC4Blow = ParseOffset(clientContent, "m_flC4Blow");
    const std::ptrdiff_t t_m_nBombSite = ParseOffset(clientContent, "m_nBombSite");
    const std::ptrdiff_t t_m_bBeingDefused = ParseOffset(clientContent, "m_bBeingDefused");
    const std::ptrdiff_t t_m_flDefuseCountDown = ParseOffset(clientContent, "m_flDefuseCountDown");
    const std::ptrdiff_t t_m_bBombDefused = ParseOffset(clientContent, "m_bBombDefused");

    const bool valid = t_dwEntityList && t_dwViewMatrix && t_dwLocalPlayerPawn && t_m_iHealth && t_m_iTeamNum &&
        t_m_vecOrigin && t_dwPlayerPawn && t_m_pGameSceneNode && t_m_boneArrayFromScene;

    if (!valid) return false;

    using namespace offsets;
    dwEntityList = t_dwEntityList;
    dwGameEntitySystem_highestEntityIndex = t_gesHi;
    dwViewMatrix = t_dwViewMatrix;
    if (t_dwViewAngles)
        dwViewAngles = t_dwViewAngles;
    dwLocalPlayerPawn = t_dwLocalPlayerPawn;
    dwLocalPlayerController = t_dwLocalPlayerController;
    if (t_dwCHud)
        dwCHud = t_dwCHud;
    m_iHealth = t_m_iHealth;
    if (t_m_bPawnIsAlive) m_bPawnIsAlive = t_m_bPawnIsAlive;
    m_iTeamNum = t_m_iTeamNum;
    m_vecOrigin = t_m_vecOrigin;
    dwPlayerPawn = t_dwPlayerPawn;
    if (t_m_hObserverPawn) m_hObserverPawn = t_m_hObserverPawn;
    if (!m_hObserverPawn) m_hObserverPawn = 0x908;
    dwSanitizedName = t_dwSanitizedName;
    m_pGameSceneNode = t_m_pGameSceneNode;
    m_vecAbsOrigin = t_m_vecAbsOrigin;
    m_boneArrayFromScene = t_m_boneArrayFromScene;
    if (t_m_angEyeAngles) m_angEyeAngles = t_m_angEyeAngles;
    if (!m_angEyeAngles) m_angEyeAngles = 0x3340;
    if (t_m_iIDEntIndex) m_iIDEntIndex = t_m_iIDEntIndex;
    if (!m_iIDEntIndex) m_iIDEntIndex = 0x341C;
    if (t_m_bWaitForNoAttack) m_bWaitForNoAttack = t_m_bWaitForNoAttack;
    if (!m_bWaitForNoAttack) m_bWaitForNoAttack = 0x1C90;
    if (t_m_bIsScoped) m_bIsScoped = t_m_bIsScoped;
    if (t_m_flFlashDuration) m_flFlashDuration = t_m_flFlashDuration;
    if (t_m_flEmitSoundTime) m_flEmitSoundTime = t_m_flEmitSoundTime;
    if (t_m_vecAbsVelocity) m_vecAbsVelocity = t_m_vecAbsVelocity;
    if (t_m_entitySpottedState) m_entitySpottedState = t_m_entitySpottedState;
    if (!m_bIsScoped) m_bIsScoped = 0x1C70;
    if (!m_flFlashDuration) m_flFlashDuration = 0x1428;
    if (!m_flEmitSoundTime) m_flEmitSoundTime = 0x1C78;
    if (!m_vecAbsVelocity) m_vecAbsVelocity = 0x3F8;
    if (!m_entitySpottedState) m_entitySpottedState = 0x1C58;
    m_ArmorValue = t_m_ArmorValue;
    m_bDormant = t_m_bDormant;
    if (t_m_fFlags) m_fFlags = t_m_fFlags;
    if (t_m_hOwnerEntity) m_hOwnerEntity = t_m_hOwnerEntity;
    if (t_m_pObserverServices) m_pObserverServices = t_m_pObserverServices;
    if (t_m_hObserverTarget) m_hObserverTarget = t_m_hObserverTarget;
    if (t_m_hController) m_hController = t_m_hController;
    if (!m_pObserverServices) m_pObserverServices = 0x1220;
    if (!m_hObserverTarget) m_hObserverTarget = 0x4C;
    if (!m_hController) m_hController = 0x13D0;
    entity_controller_stride = 112;

    if (t_dwPlantedC4) dwPlantedC4 = t_dwPlantedC4;
    if (t_dwGlobalVars) dwGlobalVars = t_dwGlobalVars;
    if (t_dwNetworkGameClient) dwNetworkGameClient = t_dwNetworkGameClient;
    if (!dwNetworkGameClient) dwNetworkGameClient = 0x90D4B0;
    if (t_dwBuildNumber) dwBuildNumber = t_dwBuildNumber;
    if (!dwBuildNumber) dwBuildNumber = 0x60F594;
    if (t_dwWindowWidth) dwWindowWidth = t_dwWindowWidth;
    if (!dwWindowWidth) dwWindowWidth = 0x9118D0;
    if (t_dwWindowHeight) dwWindowHeight = t_dwWindowHeight;
    if (!dwWindowHeight) dwWindowHeight = 0x9118D4;
    if (t_m_pWeaponServices) m_pWeaponServices = t_m_pWeaponServices;
    if (t_m_hMyWeapons) m_hMyWeapons = t_m_hMyWeapons;
    if (t_m_hActiveWeapon) m_hActiveWeapon = t_m_hActiveWeapon;
    m_WeaponEcon_AttributeManager = 0x11A8;
    if (t_m_Item) m_AttributeContainer_Item = t_m_Item;
    if (t_m_iItemDefinitionIndex) m_EconItemView_ItemDefinitionIndex = t_m_iItemDefinitionIndex;
    if (t_m_iClip1) m_iClip1 = t_m_iClip1;
    if (t_m_pBulletServices) m_pBulletServices = t_m_pBulletServices;
    if (t_m_totalHitsOnServer) m_totalHitsOnServer = t_m_totalHitsOnServer;
    if (t_m_iPing) m_iPing = t_m_iPing;
    if (!m_pBulletServices) m_pBulletServices = 0x1490;
    if (!m_totalHitsOnServer) m_totalHitsOnServer = 0x48;
    if (!m_iPing) m_iPing = 0x830;
    if (t_m_flC4Blow) c4_m_flC4Blow = t_m_flC4Blow;
    if (t_m_nBombSite) c4_m_nBombSite = t_m_nBombSite;
    if (t_m_bBeingDefused) c4_m_bBeingDefused = t_m_bBeingDefused;
    if (t_m_flDefuseCountDown) c4_m_flDefuseCountDown = t_m_flDefuseCountDown;
    if (t_m_bBombDefused) c4_m_bBombDefused = t_m_bBombDefused;
    if (!m_pWeaponServices) m_pWeaponServices = 0x1208;
    if (!m_hMyWeapons) m_hMyWeapons = 0x48;
    if (!m_hActiveWeapon) m_hActiveWeapon = 0x60;
    if (!m_AttributeContainer_Item) m_AttributeContainer_Item = 0x50;
    if (!m_EconItemView_ItemDefinitionIndex) m_EconItemView_ItemDefinitionIndex = 0x1BA;
    if (!m_iClip1) m_iClip1 = 0x1700;
    if (!c4_m_flC4Blow) c4_m_flC4Blow = 0x11D0;
    if (!c4_m_nBombSite) c4_m_nBombSite = 0x11A4;
    if (!c4_m_bBeingDefused) c4_m_bBeingDefused = 0x11DC;
    if (!c4_m_flDefuseCountDown) c4_m_flDefuseCountDown = 0x11F0;
    if (!c4_m_bBombDefused) c4_m_bBombDefused = 0x11F4;
    if (!m_fFlags) m_fFlags = 0x3F4;
    if (!m_hOwnerEntity) m_hOwnerEntity = 0x520;
    
    if (!m_steamID) m_steamID = 0x780;
    if (!m_iCompetitiveRanking) m_iCompetitiveRanking = 0x888;
    if (!m_iCompetitiveWins) m_iCompetitiveWins = 0x88C;
    if (!m_iCompetitiveRankType) m_iCompetitiveRankType = 0x890;
    if (!m_iCompetitiveRankingPredicted_Win) m_iCompetitiveRankingPredicted_Win = 0x894;
    if (!m_iCompetitiveRankingPredicted_Loss) m_iCompetitiveRankingPredicted_Loss = 0x898;
    if (!m_iCompetitiveRankingPredicted_Tie) m_iCompetitiveRankingPredicted_Tie = 0x89C;

    const std::ptrdiff_t t_grenade_m_hThrower = ParseOffset(clientContent, "m_hThrower");
    const std::ptrdiff_t t_proj_explode_tick = ParseOffset(clientContent, "m_nExplodeEffectTickBegin");
    const std::ptrdiff_t t_smoke_tick = ParseOffset(clientContent, "m_nSmokeEffectTickBegin");
    const std::ptrdiff_t t_smoke_did = ParseOffset(clientContent, "m_bDidSmokeEffect");
    const std::ptrdiff_t t_decoy_shot = ParseOffset(clientContent, "m_nDecoyShotTick");
    const std::ptrdiff_t t_inferno_fc = ParseOffset(clientContent, "m_fireCount");
    const std::ptrdiff_t t_inferno_pos = ParseOffset(clientContent, "m_firePositions");
    const std::ptrdiff_t t_inferno_burn = ParseOffset(clientContent, "m_bFireIsBurning");
    const std::ptrdiff_t t_inferno_eff = ParseOffset(clientContent, "m_nFireEffectTickBegin");
    const std::ptrdiff_t t_nade_pin = ParseOffset(clientContent, "m_bPinPulled");
    const std::ptrdiff_t t_nade_str = ParseOffset(clientContent, "m_flThrowStrength");
    const std::ptrdiff_t t_nade_throw_t = ParseOffset(clientContent, "m_fThrowTime");
    const std::ptrdiff_t t_vdata_throwvel = ParseOffset(clientContent, "m_flThrowVelocity");
    const std::ptrdiff_t t_subclass = ParseOffset(clientContent, "m_nSubclassID");

    if (t_grenade_m_hThrower) grenade_m_hThrower = t_grenade_m_hThrower;
    if (t_proj_explode_tick) proj_m_nExplodeEffectTickBegin = t_proj_explode_tick;
    if (t_smoke_tick) smoke_m_nSmokeEffectTickBegin = t_smoke_tick;
    if (t_smoke_did) smoke_m_bDidSmokeEffect = t_smoke_did;
    if (t_decoy_shot) decoy_m_nDecoyShotTick = t_decoy_shot;
    if (t_inferno_fc) inferno_m_fireCount = t_inferno_fc;
    if (t_inferno_pos) inferno_m_firePositions = t_inferno_pos;
    if (t_inferno_burn) inferno_m_bFireIsBurning = t_inferno_burn;
    if (t_inferno_eff) inferno_m_nFireEffectTickBegin = t_inferno_eff;
    
    TryApplyInfernoOffsetsFromClassBlock(clientContent);
    TryApplyEconEntityOffsetsFromClassBlock(clientContent);
    TryApplyVoteControllerOffsetsFromClassBlock(clientContent);
    if (!vote_m_iActiveIssueIndex) {
        vote_m_iActiveIssueIndex = 0x610;
        vote_m_iOnlyTeamToVote = 0x614;
        vote_m_nVoteOptionCount = 0x618;
        vote_m_nPotentialVotes = 0x62C;
        vote_m_bVotesDirty = 0x630;
        vote_m_bTypeDirty = 0x631;
        vote_m_bIsYesNoVote = 0x632;
    }
    TryApplyControllerRankFieldsFromDump(clientContent);
    if (!controller_m_bCannotBeKicked)
        controller_m_bCannotBeKicked = 0x8E8;
    if (!m_Econ_AttributeManager)
        m_Econ_AttributeManager = m_WeaponEcon_AttributeManager;

    if (t_nade_pin) nade_m_bPinPulled = t_nade_pin;
    if (t_nade_str) nade_m_flThrowStrength = t_nade_str;
    if (t_nade_throw_t) nade_m_fThrowTime = t_nade_throw_t;
    if (t_vdata_throwvel) vdata_m_flThrowVelocity = t_vdata_throwvel;
    if (t_subclass) entity_m_nSubclassID = t_subclass;

    if (t_m_iShotsFired) m_iShotsFired = t_m_iShotsFired;
    if (t_m_aimPunchAngle) m_aimPunchAngle = t_m_aimPunchAngle;
    if (t_m_pAimPunchServices) m_pAimPunchServices = t_m_pAimPunchServices;
    if (t_m_aimPunchUnpredictableRel) m_aimPunchUnpredictableRel = t_m_aimPunchUnpredictableRel;
    if (t_m_aimPunchPredictableRel) m_aimPunchPredictableRel = t_m_aimPunchPredictableRel;
    if (!m_aimPunchAngle && m_pAimPunchServices && !m_aimPunchUnpredictableRel)
        m_aimPunchUnpredictableRel = 0xA4;
    if (!m_aimPunchAngle && m_pAimPunchServices && !m_aimPunchPredictableRel)
        m_aimPunchPredictableRel = 0x50;
    if (!m_aimPunchAngle)
        m_aimPunchAngle = 0x16CC;
    if (t_dwSensitivity) dwSensitivity = t_dwSensitivity;
    if (t_dwSensVal) dwSensitivity_sensitivity = t_dwSensVal;
    if (!dwSensitivity_sensitivity) dwSensitivity_sensitivity = 0x58;

    printf("[offsets] OK: %s | dwEntityList=0x%llX dwLocalPawn=0x%llX viewMatrix=0x%llX\n",
        sourceTag,
        static_cast<unsigned long long>(dwEntityList),
        static_cast<unsigned long long>(dwLocalPlayerPawn),
        static_cast<unsigned long long>(dwViewMatrix));
    return true;
}

bool ExpectionalLoadOffsetsFromLocalFiles()
{
	wchar_t exePath[MAX_PATH] = {};
	if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH))
		return false;

	wchar_t* lastSlash = wcsrchr(exePath, L'\\');
	if (!lastSlash)
		return false;

	*lastSlash = L'\0';

	wchar_t offsetsPath[MAX_PATH] = {};
	wchar_t clientPath[MAX_PATH] = {};
	if (swprintf_s(offsetsPath, MAX_PATH, L"%s\\offsets\\offsets.hpp", exePath) <= 0 ||
	    swprintf_s(clientPath, MAX_PATH, L"%s\\offsets\\client_dll.hpp", exePath) <= 0) {
		return false;
	}

	std::string offsetsContent;
	std::string clientContent;

	if (!LoadFileToString(offsetsPath, offsetsContent) ||
	    !LoadFileToString(clientPath, clientContent)) {
		return false;
	}

	if (TryApplyFromDump(offsetsContent, clientContent, "local offsets folder")) {
		printf("[offsets] Successfully loaded from local offsets folder: %S\\offsets\\\n", exePath);
		return true;
	}

	return false;
}

