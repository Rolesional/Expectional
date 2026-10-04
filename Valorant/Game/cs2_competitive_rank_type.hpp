#pragma once

/**
 * CS2: `CCSPlayerController::m_iCompetitiveRankType` (int8) — şema / offset kaynağı:
 *   https://cs2-sdk.com/  (cs2-universal-offsets → client_dll / CCSPlayerController)
 *   https://github.com/scros22/cs2-universal-offsets
 * Oyun güncellemesiyle enum değerleri kayabilir; yeni build’de client_dll.hpp içindeki
 * ilgili enum’u veya cs2-sdk çıktısını karşılaştırıp bu sabitleri güncelleyin.
 */
namespace expectional::cs2 {

/** Şema: “sıra türü yok”. */
constexpr int kCompetitiveRankTypeNone = 0;

/** Sık görülen: klasik MM skill rank (itzarty `matchmaking/` ikonları). */
constexpr int kCompetitiveRankTypeMatchmaking = 1;

/** Wingman skill rank (itzarty `wingman/` ikonları). Şema drift ederse client_dll enum ile doğrulayın. */
constexpr int kCompetitiveRankTypeWingman = 2;

/**
 * Premier modu (yaygın harici doğrulama + int8 alanı; build drift riski var).
 * Şüphede kaldığınızda cs2-sdk üretilmiş `client_dll.hpp` içinde CompetitiveRankType
 * benzeri enum satırlarına bakın.
 */
constexpr int kCompetitiveRankTypePremier = 7;

inline bool RankTypeIsPremier(int rankType) noexcept
{
	return rankType == kCompetitiveRankTypePremier;
}

inline bool RankTypeIsWingman(int rankType) noexcept
{
	return rankType == kCompetitiveRankTypeWingman;
}

/** Yerel oyuncu Premier ise veya lobide herhangi bir oyuncu Premier tipi bildiriyorsa. */
inline bool MatchUsesPremierColumns(int localControllerRankType, bool anyListedPlayerPremier) noexcept
{
	if (RankTypeIsPremier(localControllerRankType))
		return true;
	return anyListedPlayerPremier;
}

} // namespace expectional::cs2
