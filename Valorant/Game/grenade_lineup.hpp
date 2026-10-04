#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UE4Structs {
	struct view_matrix_t;
	struct Vector3;
}

enum class ExpectionalGrenadeThrowType : int {
	kNormal = 0,
	kJumpThrow,
	kWalkJumpThrow,
	kCrouchThrow,
	kCrouchJumpThrow,
	kCrouchWalkJumpThrow,
	kWalkThrow,
	kRunJumpThrow,
	kCount
};

const char* ExpectionalGrenadeThrowTypeLabel(ExpectionalGrenadeThrowType t) noexcept;

void ExpectionalGrenadeLineupRender(
	const UE4Structs::view_matrix_t& vm,
	std::uintptr_t localPawn,
	const UE4Structs::Vector3& localEyeWorld);

struct ExpectionalLineupBrowserPack {
	std::string id;               
	std::string title;            
	std::string map;              
	std::size_t lineup_count = 0; 
};

std::vector<std::string> ExpectionalLineupBrowserMapList();

std::vector<ExpectionalLineupBrowserPack> ExpectionalLineupBrowserPacksForMap(const std::string& map);

void ExpectionalLineupBrowserStageToggle(const std::string& pack_id);
bool ExpectionalLineupBrowserStageContains(const std::string& pack_id);
std::size_t ExpectionalLineupBrowserStageCount();
void ExpectionalLineupBrowserStageClear();

void ExpectionalLineupBrowserApply();

std::size_t ExpectionalLineupBrowserActiveCount();
bool ExpectionalLineupBrowserActiveContains(const std::string& pack_id);
void ExpectionalLineupBrowserClearActive();

bool ExpectionalLineupBrowserActiveAdd(const std::string& pack_id);
void ExpectionalLineupBrowserActiveRemove(const std::string& pack_id);

std::size_t ExpectionalLineupBrowserActiveCountForMap(const std::string& map);

std::vector<std::string> ExpectionalLineupBrowserActiveIdsSnapshot();
void ExpectionalLineupBrowserActiveSetFromConfig(const std::vector<std::string>& ids);

void ExpectionalLineupBrowserRefreshWorkshop();
bool ExpectionalLineupsLoaded();
std::size_t ExpectionalLineupsTotalCount();
std::size_t ExpectionalLineupPacksTotalCount();
