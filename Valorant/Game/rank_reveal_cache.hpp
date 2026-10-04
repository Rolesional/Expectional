#pragma once

#include "structs.hpp"

#include <cstdint>
#include <vector>

namespace rank_reveal_cache {

void MergeScannedPlayers(const std::vector<UE4Structs::CS2Entity>& scanned, bool tab_or_menu_visible);

void CopyStableSnapshot(std::vector<UE4Structs::CS2Entity>& out);

void CollectSteamIds(std::vector<std::uint64_t>& out);

void Clear();

} 
