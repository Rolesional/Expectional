#pragma once

#include "structs.hpp"

#include <cstdint>
#include <vector>

namespace rank_reveal_cache {

/** cacheGame taramasi sonrasi: yeni oyuncu ekle, var olani rank/name ile guncelle; satir sirasi korunur. */
void MergeScannedPlayers(const std::vector<UE4Structs::CS2Entity>& scanned, bool tab_or_menu_visible);

/** Cizim: stabil sirali snapshot (her kare full sort yok). */
void CopyStableSnapshot(std::vector<UE4Structs::CS2Entity>& out);

/** Faceit/Steam kuyrugu icin steam id listesi. */
void CollectSteamIds(std::vector<std::uint64_t>& out);

/** Ozellik kapatildi / map degisti. */
void Clear();

} // namespace rank_reveal_cache
