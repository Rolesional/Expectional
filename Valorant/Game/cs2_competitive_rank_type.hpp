#pragma once

namespace expectional::cs2 {

constexpr int kCompetitiveRankTypeNone = 0;

constexpr int kCompetitiveRankTypeMatchmaking = 1;

constexpr int kCompetitiveRankTypeWingman = 2;

constexpr int kCompetitiveRankTypePremier = 7;

inline bool RankTypeIsPremier(int rankType) noexcept
{
	return rankType == kCompetitiveRankTypePremier;
}

inline bool RankTypeIsWingman(int rankType) noexcept
{
	return rankType == kCompetitiveRankTypeWingman;
}

inline bool MatchUsesPremierColumns(int localControllerRankType, bool anyListedPlayerPremier) noexcept
{
	if (RankTypeIsPremier(localControllerRankType))
		return true;
	return anyListedPlayerPremier;
}

} 
