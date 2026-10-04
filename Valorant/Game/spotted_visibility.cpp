#include "spotted_visibility.hpp"
#include "catalyst_world_bvh.hpp"
#include "entity_handle.hpp"
#include "globals.hpp"
#include "offsets_runtime.hpp"
#include "structs.hpp"
#include "../Driver/driver.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include "Protection/vxlang_per_tu.hpp"

namespace {

constexpr int kVisSmoothStep = 72;

std::unordered_map<uintptr_t, int>& VisSmoothMap() {
	static std::unordered_map<uintptr_t, int> s_map;
	return s_map;
}

bool ApplyVisSmooth(uintptr_t key, bool raw_visible) {
	if (!key)
		return raw_visible;
	auto& slot = VisSmoothMap()[key];
	const int target = raw_visible ? 255 : 0;
	if (target > slot)
		slot = (std::min)(255, slot + kVisSmoothStep);
	else if (target < slot)
		slot = (std::max)(0, slot - kVisSmoothStep);
	return slot >= 128;
}

void PruneVisSmoothIfHuge() {
	auto& m = VisSmoothMap();
	if (m.size() > 96u)
		m.clear();
}

struct RawBvhSlot {
	bool value = false;
	unsigned gen = 0;
};

std::unordered_map<uintptr_t, RawBvhSlot>& RawBvhCache() {
	static std::unordered_map<uintptr_t, RawBvhSlot> s_map;
	return s_map;
}

unsigned& BvhBudgetGen() {
	static unsigned s_gen = 0;
	return s_gen;
}

int& BvhFreshRayBudget() {
	static int s_left = 0;
	return s_left;
}

int& BvhFreshMultiBudget() {
	static int s_multi = 0;
	return s_multi;
}

template<typename Fn>
bool QueryRawBvhCached(uintptr_t key, bool multi_slot, Fn&& compute) {
	if (!Settings::misc::save_fps)
		return compute();
	auto& slot = RawBvhCache()[key];
	const unsigned gen = BvhBudgetGen();
	if (slot.gen != 0 && gen - slot.gen <= 4u)
		return slot.value;
	int& budget = multi_slot ? BvhFreshMultiBudget() : BvhFreshRayBudget();
	if (budget <= 0) {
		if (slot.gen != 0)
			return slot.value;
		return false;
	}
	--budget;
	slot.value = compute();
	slot.gen = gen;
	return slot.value;
}

} 

void ExpectionalBvhBudgetBeginFrame() noexcept
{
	++BvhBudgetGen();
	if (!Settings::misc::save_fps) {
		BvhFreshRayBudget() = 64;
		BvhFreshMultiBudget() = 32;
		return;
	}
	BvhFreshRayBudget() = 8;
	BvhFreshMultiBudget() = 3;
	if (RawBvhCache().size() > 80u)
		RawBvhCache().clear();
}

namespace {

constexpr int kCs2HeadBone = 7;

UE4Structs::Vector3 ReadPawnOrigin(uintptr_t pawn) {
	if (!pawn || !offsets::m_vecOrigin)
		return {};
	return g_GameMem.readv<UE4Structs::Vector3>(pawn + static_cast<uintptr_t>(offsets::m_vecOrigin));
}

UE4Structs::Vector3 ReadPawnBoneWorld(uintptr_t pawn, int boneIdx, float fallbackZAdd) {
	const UE4Structs::Vector3 origin = ReadPawnOrigin(pawn);
	if (!pawn)
		return {};
	const uintptr_t gs = offsets::m_pGameSceneNode
		? g_GameMem.readv<uintptr_t>(pawn + static_cast<uintptr_t>(offsets::m_pGameSceneNode))
		: 0;
	uint64_t ba = 0;
	if (gs && offsets::m_boneArrayFromScene)
		ba = g_GameMem.readv<uint64_t>(gs + offsets::m_boneArrayFromScene);
	if (ba)
		return g_GameMem.readv<UE4Structs::Vector3>(ba + static_cast<uintptr_t>(boneIdx * 0x20));
	return UE4Structs::Vector3(origin.x, origin.y, origin.z + fallbackZAdd);
}

bool ReadLocalEyeWorld(uintptr_t localPawn, UE4Structs::Vector3& out) {
	if (!localPawn || !offsets::m_vecOrigin)
		return false;
	out = ReadPawnBoneWorld(localPawn, kCs2HeadBone, 75.f);
	return true;
}

bool ReadTargetHeadWorld(uintptr_t targetPawn, UE4Structs::Vector3& out) {
	if (!targetPawn || !offsets::m_vecOrigin)
		return false;
	out = ReadPawnBoneWorld(targetPawn, kCs2HeadBone, 75.f);
	return true;
}

} 

static bool SchemaNameIsSmoke(const char* cn) noexcept {
	if (!cn || !cn[0])
		return false;
	auto lower = [](unsigned char c) { return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + 32u) : c; };
	auto streq = [&](const char* lit) {
		for (const char* a = cn, *b = lit; *a || *b; ++a, ++b) {
			if (lower(static_cast<unsigned char>(*a)) != lower(static_cast<unsigned char>(*b)))
				return false;
		}
		return true;
	};
	auto substr = [&](const char* needle) {
		if (!needle || !needle[0])
			return false;
		for (const char* h = cn; *h; ++h) {
			const char* hp = h;
			const char* np = needle;
			while (*hp && *np && lower(static_cast<unsigned char>(*hp)) == lower(static_cast<unsigned char>(*np))) {
				++hp;
				++np;
			}
			if (!*np)
				return true;
		}
		return false;
	};
	if (streq("C_SmokeGrenadeProjectile"))
		return true;
	return substr("smoke") && (substr("projectile") || substr("grenade"));
}

static bool EntityPtrIsSmoke(uintptr_t ent) noexcept {
	if (!ent)
		return false;
	char cn[96]{};
	const uintptr_t identity = g_GameMem.readv<uintptr_t>(ent + 0x10);
	if (identity) {
		for (uintptr_t symOff : { 0x20ul, 0x18ul }) {
			const uintptr_t namePtr = g_GameMem.readv<uintptr_t>(identity + symOff);
			if (!namePtr)
				continue;
			std::string s = g_GameMem.ReadString(namePtr, sizeof(cn) - 1);
			if (!s.empty() && SchemaNameIsSmoke(s.c_str()))
				return true;
		}
	}
	return false;
}

static UE4Structs::Vector3 ViewAnglesToForward(const UE4Structs::Vector3& ang) noexcept {
	constexpr float kDeg = 3.14159265f / 180.f;
	const float sp = std::sin(ang.x * kDeg);
	const float cp = std::cos(ang.x * kDeg);
	const float sy = std::sin(ang.y * kDeg);
	const float cy = std::cos(ang.y * kDeg);
	return UE4Structs::Vector3(cp * cy, cp * sy, -sp);
}

static float AimDirDot(const UE4Structs::Vector3& eye, const UE4Structs::Vector3& fwd, const UE4Structs::Vector3& pt) noexcept {
	const float dx = pt.x - eye.x;
	const float dy = pt.y - eye.y;
	const float dz = pt.z - eye.z;
	const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
	if (len < 1.f)
		return -1.f;
	return (dx * fwd.x + dy * fwd.y + dz * fwd.z) / len;
}

static uintptr_t GameEntityByIndex(uintptr_t entity_list, int i) {
	if (!entity_list || i < 0 || i >= 8192)
		return 0;
	const uintptr_t list_entry = g_GameMem.readv<uintptr_t>(
	    entity_list + 8ull * (static_cast<uintptr_t>(i & 0x7FFF) >> 9) + 16);
	if (!list_entry)
		return 0;
	const uintptr_t stride =
	    static_cast<uintptr_t>(offsets::entity_controller_stride ? offsets::entity_controller_stride : 112u);
	return g_GameMem.readv<uintptr_t>(list_entry + stride * (i & 0x1FF));
}

static bool IsEnemyPawnQuick(uintptr_t pawn, uintptr_t localPawn, int localTeam) noexcept {
	if (!pawn || pawn == localPawn || !offsets::m_iHealth || !offsets::m_iTeamNum)
		return false;
	const int hp = g_GameMem.readv<int>(pawn + offsets::m_iHealth);
	if (hp <= 0 || hp > 100)
		return false;
	const int team = g_GameMem.readv<int>(pawn + offsets::m_iTeamNum) & 0xFF;
	if (Settings::Visuals::enemiesOnly && team == (localTeam & 0xFF))
		return false;
	return true;
}

uint64_t ReadSpottedMask(uintptr_t pawn) {
	if (!pawn || !offsets::m_entitySpottedState)
		return 0;
	const uintptr_t addr = pawn + static_cast<uintptr_t>(offsets::m_entitySpottedState) + 0xC;
	const uint32_t lo = g_GameMem.readv<uint32_t>(addr);
	const uint32_t hi = g_GameMem.readv<uint32_t>(addr + 4);
	return (static_cast<uint64_t>(hi) << 32) | static_cast<uint64_t>(lo);
}

bool ReadSpottedBool(uintptr_t pawn) {
	if (!pawn || !offsets::m_entitySpottedState)
		return false;
	const std::uint8_t b = g_GameMem.readv<std::uint8_t>(pawn + static_cast<uintptr_t>(offsets::m_entitySpottedState) + 8);
	return b != 0;
}

bool SpottedLikeAnanbabanWithMasks(uintptr_t localPawn, uintptr_t targetPawn, uint64_t localMask, uint64_t targetMask,
    uint32_t targetSpotIndexA, uint32_t targetSpotIndexB)
{
	const int hIdx = g_localPawnHandleIndex.load(std::memory_order_relaxed);
	const int cIdx = g_localControllerEntityIndex.load(std::memory_order_relaxed);
	int localIdx = 0;
	bool haveLocal = false;
	if (hIdx > 0 && hIdx < 8192) {
		localIdx = hIdx;
		haveLocal = true;
	} else if (cIdx > 0 && cIdx < 8192) {
		localIdx = cIdx;
		haveLocal = true;
	}

	const unsigned tA = (unsigned)(targetSpotIndexA & 0x7FFFu) % 64u;
	const unsigned tB = (unsigned)(targetSpotIndexB & 0x7FFFu) % 64u;
	const uint64_t tm = targetMask;
	const uint64_t lm = localMask;

	if (haveLocal) {
		const unsigned li = (unsigned)localIdx % 64u;
		const uint64_t localBit = (1ull << li);
		const uint64_t targetBitA = (1ull << tA);
		const uint64_t targetBitB = (tA != tB) ? (1ull << tB) : 0ull;
		const bool edge = ((tm & localBit) != 0ull)
			|| ((lm & targetBitA) != 0ull)
			|| (targetBitB && ((lm & targetBitB) != 0ull));
		if (edge)
			return true;
	}
	return ReadSpottedBool(targetPawn) || ReadSpottedBool(localPawn);
}

bool ExpectionalBvhAnyHitboxVisibleBetweenPawns(uintptr_t localPawn, uintptr_t targetPawn);

bool SpottedLikeAnanbaban(uintptr_t localPawn, uintptr_t targetPawn, uint32_t targetSpotIndexA, uint32_t targetSpotIndexB)
{
	if (ex_world_bvh::g_world_bvh.valid())
		return ExpectionalBvhAnyHitboxVisibleBetweenPawns(localPawn, targetPawn);
	const uint64_t tm = ReadSpottedMask(targetPawn);
	const uint64_t lm = ReadSpottedMask(localPawn);
	return SpottedLikeAnanbabanWithMasks(localPawn, targetPawn, lm, tm, targetSpotIndexA, targetSpotIndexB);
}

bool CrosshairEntityIsTargetPawn(uintptr_t localPawn, uintptr_t targetPawn) {
	if (!localPawn || !targetPawn || !offsets::m_iIDEntIndex)
		return false;
	const int entIndex = g_GameMem.readv<int>(localPawn + static_cast<uintptr_t>(offsets::m_iIDEntIndex));
	if (entIndex < 0 || entIndex == 0x7FFF)
		return false;
	const uintptr_t crossPawn = ex_entity::ResolvePlayerPawnFromCrosshairIndex(entIndex);
	return crossPawn != 0 && crossPawn == targetPawn;
}

bool ExpectionalBvhVisible(const UE4Structs::Vector3& local_eye_world, const UE4Structs::Vector3& target_point_world) {
	if (!ex_world_bvh::g_world_bvh.valid())
		return false;
	return ex_world_bvh::LosClearTo(local_eye_world, target_point_world);
}

bool ExpectionalBvhVisibleSmoothed(uintptr_t stable_key, const UE4Structs::Vector3& local_eye_world,
    const UE4Structs::Vector3& target_point_world) {
	const uintptr_t cache_key = stable_key ? stable_key : 1u;
	const bool raw = QueryRawBvhCached(cache_key, false, [&] {
		return ExpectionalBvhVisible(local_eye_world, target_point_world);
	});
	if (!Settings::misc::save_fps)
		return raw;
	PruneVisSmoothIfHuge();
	return ApplyVisSmooth(stable_key, raw);
}

bool ExpectionalBvhAnyHitboxVisibleBetweenPawnsSmoothed(uintptr_t localPawn, uintptr_t targetPawn) {
	const uintptr_t cache_key = targetPawn ? (targetPawn ^ (localPawn << 1)) : 0u;
	const bool raw = QueryRawBvhCached(cache_key, true, [&] {
		return ExpectionalBvhAnyHitboxVisibleBetweenPawns(localPawn, targetPawn);
	});
	if (!Settings::misc::save_fps)
		return raw;
	PruneVisSmoothIfHuge();
	return ApplyVisSmooth(targetPawn, raw);
}

bool ExpectionalBvhVisibleBetweenPawns(uintptr_t localPawn, uintptr_t targetPawn) {
	UE4Structs::Vector3 eye{};
	UE4Structs::Vector3 head{};
	if (!ReadLocalEyeWorld(localPawn, eye) || !ReadTargetHeadWorld(targetPawn, head))
		return false;
	return ExpectionalBvhVisible(eye, head);
}

bool ExpectionalBvhAnyHitboxVisibleBetweenPawns(uintptr_t localPawn, uintptr_t targetPawn) {
	if (!ex_world_bvh::g_world_bvh.valid())
		return false;
	UE4Structs::Vector3 eye{};
	if (!ReadLocalEyeWorld(localPawn, eye))
		return false;
	if (!targetPawn || !offsets::m_vecOrigin)
		return false;
	const UE4Structs::Vector3 origin =
	    g_GameMem.readv<UE4Structs::Vector3>(targetPawn + static_cast<uintptr_t>(offsets::m_vecOrigin));
	uintptr_t gs = 0;
	if (offsets::m_pGameSceneNode)
		gs = g_GameMem.readv<uintptr_t>(targetPawn + static_cast<uintptr_t>(offsets::m_pGameSceneNode));
	uint64_t ba = 0;
	if (gs && offsets::m_boneArrayFromScene)
		ba = g_GameMem.readv<uint64_t>(gs + offsets::m_boneArrayFromScene);

	auto targetBone = [&](int boneIdx, float fz) -> UE4Structs::Vector3 {
		if (ba) {
			const UE4Structs::Vector3 v =
			    g_GameMem.readv<UE4Structs::Vector3>(ba + static_cast<uintptr_t>(boneIdx * 0x20));
			if (v.x != 0.f || v.y != 0.f || v.z != 0.f)
				return v;
		}
		return UE4Structs::Vector3(origin.x, origin.y, origin.z + fz);
	};

	static const struct {
		int bone;
		float fz;
	} kTry[] = {
		{ 7, 75.f },
		{ 6, 63.f },
		{ 23, 50.f },
		{ 1, 36.f },
	};
	for (const auto& t : kTry) {
		if (ExpectionalBvhVisible(eye, targetBone(t.bone, t.fz)))
			return true;
	}
	return false;
}

bool ExpectionalLosOrSpottedToTarget(uintptr_t localPawn, uintptr_t targetPawn, uint32_t targetSpotIndex)
{
	if (!localPawn || !targetPawn)
		return false;
	return SpottedLikeAnanbaban(localPawn, targetPawn, targetSpotIndex, targetSpotIndex);
}

bool ExpectionalTriggerVisibilityOk(uintptr_t localPawn, uintptr_t targetPawn, uint32_t spotIndex) noexcept
{
	if (!localPawn || !targetPawn)
		return false;
	
	if (CrosshairEntityIsTargetPawn(localPawn, targetPawn))
		return true;
	if (ex_world_bvh::g_world_bvh.valid() &&
	    ExpectionalBvhAnyHitboxVisibleBetweenPawns(localPawn, targetPawn))
		return true;
	if (offsets::m_entitySpottedState) {
		const uint64_t tm = ReadSpottedMask(targetPawn);
		const uint64_t lm = ReadSpottedMask(localPawn);
		if (SpottedLikeAnanbabanWithMasks(localPawn, targetPawn, lm, tm, spotIndex, spotIndex))
			return true;
	}
	
	if (ExpectionalCrosshairIsSmokeEntity(localPawn) && ExpectionalPawnInsideActiveSmoke(targetPawn))
		return true;
	return false;
}

bool ExpectionalCrosshairIsSmokeEntity(uintptr_t localPawn) noexcept
{
	if (!localPawn || !offsets::m_iIDEntIndex)
		return false;
	const int entIndex = g_GameMem.readv<int>(localPawn + static_cast<uintptr_t>(offsets::m_iIDEntIndex));
	if (entIndex < 0 || entIndex == 0x7FFF)
		return false;
	const int slot = entIndex & 0x7FFF;
	const uintptr_t ent = ex_entity::ResolveEntityFromListSlot(slot);
	const uintptr_t direct = ex_entity::ResolveHandle(static_cast<uint32_t>(slot));
	if (EntityPtrIsSmoke(ent) || EntityPtrIsSmoke(direct))
		return true;
	if (ent && offsets::smoke_m_bDidSmokeEffect) {
		const bool active = g_GameMem.readv<bool>(ent + static_cast<uintptr_t>(offsets::smoke_m_bDidSmokeEffect));
		if (active)
			return true;
	}
	if (direct && direct != ent && offsets::smoke_m_bDidSmokeEffect) {
		const bool active = g_GameMem.readv<bool>(direct + static_cast<uintptr_t>(offsets::smoke_m_bDidSmokeEffect));
		if (active)
			return true;
	}
	return false;
}

bool ExpectionalCrosshairIsNonPlayerEntity(uintptr_t localPawn, uintptr_t crossPawn) noexcept
{
	if (!localPawn || !offsets::m_iIDEntIndex)
		return false;
	const int entIndex = g_GameMem.readv<int>(localPawn + static_cast<uintptr_t>(offsets::m_iIDEntIndex));
	if (entIndex < 0 || entIndex == 0x7FFF)
		return false;
	if (crossPawn) {
		const int hp = g_GameMem.readv<int>(crossPawn + offsets::m_iHealth);
		if (hp > 0 && hp <= 100)
			return false;
	}
	return true;
}

static bool ExpectionalPointInsideActiveSmoke(const UE4Structs::Vector3& pos) noexcept
{
	if (!client || !offsets::smoke_m_bDidSmokeEffect)
		return false;

	const uintptr_t entity_list = g_GameMem.readv<uintptr_t>(client + offsets::dwEntityList);
	if (!entity_list)
		return false;

	int i_max = 1152;
	if (offsets::dwGameEntitySystem_highestEntityIndex) {
		const int hi = g_GameMem.readv<int>(
		    entity_list + static_cast<uintptr_t>(offsets::dwGameEntitySystem_highestEntityIndex));
		if (hi >= 1 && hi < 16384)
			i_max = (std::min)(1536, (std::max)(hi + 64, 640));
	}

	constexpr float kInsideRadius = 340.f;
	for (int i = 0; i < i_max; ++i) {
		const uintptr_t ent = GameEntityByIndex(entity_list, i);
		if (!ent || ent < 0x10000ull)
			continue;
		if (!g_GameMem.readv<bool>(ent + static_cast<uintptr_t>(offsets::smoke_m_bDidSmokeEffect)))
			continue;
		const UE4Structs::Vector3 smokePos =
		    g_GameMem.readv<UE4Structs::Vector3>(ent + static_cast<uintptr_t>(offsets::m_vecOrigin));
		const float dx = smokePos.x - pos.x;
		const float dy = smokePos.y - pos.y;
		const float dz = smokePos.z - pos.z;
		if (dx * dx + dy * dy + dz * dz <= kInsideRadius * kInsideRadius)
			return true;
	}
	return false;
}

bool ExpectionalLocalInsideActiveSmoke(uintptr_t localPawn) noexcept
{
	if (!localPawn || !offsets::m_vecOrigin)
		return false;
	return ExpectionalPointInsideActiveSmoke(ReadPawnOrigin(localPawn));
}

bool ExpectionalPawnInsideActiveSmoke(uintptr_t targetPawn) noexcept
{
	if (!targetPawn || !offsets::m_vecOrigin)
		return false;
	return ExpectionalPointInsideActiveSmoke(ReadPawnOrigin(targetPawn));
}

static bool SmokePathEnemyAllowsShot(uintptr_t localPawn, uintptr_t pawn, uint32_t slot, float aimDot) noexcept {
	if (!localPawn || !pawn)
		return false;
	if (CrosshairEntityIsTargetPawn(localPawn, pawn))
		return true;
	if (ex_world_bvh::g_world_bvh.valid() &&
	    ExpectionalBvhAnyHitboxVisibleBetweenPawns(localPawn, pawn))
		return true;
	if (offsets::m_entitySpottedState &&
	    SpottedLikeAnanbaban(localPawn, pawn, slot, slot))
		return true;
	
	if (!ex_world_bvh::g_world_bvh.valid() &&
	    ExpectionalPawnInsideActiveSmoke(pawn) && aimDot >= 0.99f)
		return true;
	return false;
}

uintptr_t ExpectionalFindTriggerTargetBehindSmoke(uintptr_t localPawn, int localTeam, uint32_t* outEntitySlot) noexcept
{
	if (outEntitySlot)
		*outEntitySlot = 0;
	if (!localPawn || !client || !offsets::m_angEyeAngles)
		return 0;
	if (!ExpectionalCrosshairIsSmokeEntity(localPawn))
		return 0;

	UE4Structs::Vector3 eye{};
	if (!ReadLocalEyeWorld(localPawn, eye))
		return 0;
	const UE4Structs::Vector3 ang =
	    g_GameMem.readv<UE4Structs::Vector3>(localPawn + static_cast<uintptr_t>(offsets::m_angEyeAngles));
	const UE4Structs::Vector3 fwd = ViewAnglesToForward(ang);

	const uintptr_t entity_list = g_GameMem.readv<uintptr_t>(client + offsets::dwEntityList);
	if (!entity_list)
		return 0;

	constexpr uintptr_t kStride = 112u;
	
	constexpr float kMinDot = 0.99f;

	float bestDot = -1.f;
	uintptr_t bestPawn = 0;
	uint32_t bestSlot = 0;

	for (int i = 1; i < 64; ++i) {
		const uintptr_t list_entry = g_GameMem.readv<uintptr_t>(
		    entity_list + 8ull * (static_cast<uintptr_t>(i & 0x7FFF) >> 9) + 16);
		if (!list_entry)
			continue;
		const uintptr_t controller = g_GameMem.readv<uintptr_t>(list_entry + kStride * (i & 0x1FF));
		if (!controller)
			continue;
		const std::uint32_t hPawn =
		    g_GameMem.readv<std::uint32_t>(controller + static_cast<uintptr_t>(offsets::dwPlayerPawn));
		const uintptr_t list_entry2 = g_GameMem.readv<uintptr_t>(
		    entity_list + 8ull * ((hPawn & 0x7FFF) >> 9) + 16);
		if (!list_entry2)
			continue;
		const uintptr_t pawn = g_GameMem.readv<uintptr_t>(list_entry2 + kStride * (hPawn & 0x1FF));
		if (!pawn || pawn == localPawn)
			continue;

		const int hp = g_GameMem.readv<int>(pawn + offsets::m_iHealth);
		if (hp <= 0 || hp > 100)
			continue;
		const int team = g_GameMem.readv<int>(pawn + offsets::m_iTeamNum) & 0xFF;
		const int lt = localTeam & 0xFF;
		if (Settings::Visuals::enemiesOnly && team == lt)
			continue;

		const UE4Structs::Vector3 head = ReadPawnBoneWorld(pawn, kCs2HeadBone, 75.f);
		const UE4Structs::Vector3 chest = ReadPawnBoneWorld(pawn, 6, 63.f);
		const UE4Structs::Vector3 neck = ReadPawnBoneWorld(pawn, 5, 68.f);
		float dot = (std::max)(AimDirDot(eye, fwd, head), AimDirDot(eye, fwd, chest));
		dot = (std::max)(dot, AimDirDot(eye, fwd, neck));
		if (dot < kMinDot)
			continue;
		if (!SmokePathEnemyAllowsShot(localPawn, pawn, static_cast<uint32_t>(i), dot))
			continue;

		if (dot > bestDot) {
			bestDot = dot;
			bestPawn = pawn;
			bestSlot = static_cast<uint32_t>(i);
		}
	}

	if (bestPawn && outEntitySlot)
		*outEntitySlot = bestSlot;
	return bestPawn;
}

uintptr_t ExpectionalResolveTriggerTargetPawn(uintptr_t localPawn, int localTeam, int entIndex,
    uint32_t* outEntitySlot) noexcept
{
	if (outEntitySlot)
		*outEntitySlot = 0;
	if (!localPawn)
		return 0;

	uintptr_t crossPawn = 0;
	uintptr_t targetPawn = 0;
	const bool crosshairOnSmoke = ExpectionalCrosshairIsSmokeEntity(localPawn);

	if (!crosshairOnSmoke && offsets::m_iIDEntIndex && entIndex >= 0 && entIndex != 0x7FFF) {
		const uintptr_t directPawn = ex_entity::ResolvePawnFromCrosshairIndexDirect(entIndex);
		if (IsEnemyPawnQuick(directPawn, localPawn, localTeam))
			targetPawn = directPawn;

		crossPawn = ex_entity::ResolvePlayerPawnFromCrosshairIndex(entIndex);
		if (!targetPawn && IsEnemyPawnQuick(crossPawn, localPawn, localTeam))
			targetPawn = crossPawn;
	}

	if (crosshairOnSmoke) {
		targetPawn = ExpectionalFindTriggerTargetBehindSmoke(localPawn, localTeam, outEntitySlot);
		if (!IsEnemyPawnQuick(targetPawn, localPawn, localTeam))
			targetPawn = 0;
	}

	return targetPawn;
}

void ExpectionalFillVisDebug(uintptr_t localPawn, uintptr_t targetPawn, uint32_t pawn_low, int entity_i, const char* nameUtf8) {
	ExpectionalVisDbg& d = g_visDbg;
	d.valid = true;
	d.off_spotted = static_cast<int>(offsets::m_entitySpottedState);
	d.off_id_ent = static_cast<int>(offsets::m_iIDEntIndex);
	d.local_pawn_h = g_localPawnHandleIndex.load(std::memory_order_relaxed);
	d.local_ctrl_i = g_localControllerEntityIndex.load(std::memory_order_relaxed);
	if (localPawn && offsets::m_iIDEntIndex) {
		d.id_ent_read = g_GameMem.readv<int>(localPawn + static_cast<uintptr_t>(offsets::m_iIDEntIndex));
		const uint32_t h = static_cast<uint32_t>(d.id_ent_read) & 0x7FFFu;
		d.crosshair_pawn = ex_entity::ResolveHandle(h);
	} else {
		d.id_ent_read = -999;
		d.crosshair_pawn = 0;
	}
	if (targetPawn && localPawn && offsets::m_entitySpottedState) {
		d.sample_tgt_mask = ReadSpottedMask(targetPawn);
		d.sample_loc_mask = ReadSpottedMask(localPawn);
	} else {
		d.sample_tgt_mask = 0;
		d.sample_loc_mask = 0;
	}
	d.sample_pawn_low = static_cast<int>(pawn_low & 0x7FFFu);
	d.sample_entity_i = entity_i;
	d.bvh_ready = ex_world_bvh::g_world_bvh.valid();
	d.bvh_triangle_count = static_cast<int>(ex_world_bvh::g_world_bvh.count());
	d.sample_bvh_los = localPawn && targetPawn && d.bvh_ready && ExpectionalBvhAnyHitboxVisibleBetweenPawns(localPawn, targetPawn);
	std::memset(d.sample_name, 0, sizeof d.sample_name);
	if (nameUtf8 && nameUtf8[0]) {
		std::strncpy(d.sample_name, nameUtf8, sizeof d.sample_name - 1);
		d.sample_name[sizeof d.sample_name - 1] = '\0';
	} else {
		d.sample_name[0] = '?';
		d.sample_name[1] = '\0';
	}
}
