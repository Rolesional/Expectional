#pragma once
#include <cstdint>

/** Overlay karesi: TAB/menu kenari, istek bayraklari (IOCTL yok). */
void ExpectionalRevealWorkersNotifyOverlayFrame() noexcept;

/** Rank listesi cacheGame'de yenilensin mi (TAB acikken ~450ms). */
bool ExpectionalRankRevealWantsCacheRefresh() noexcept;

/** Faceit/Steam kuyrugu yalnizca scoreboard/menu acikken. */
bool ExpectionalRankRevealWantsNetworkPoll() noexcept;

/** TAB veya menu acik (rank tablosu gorunur). */
bool ExpectionalRankRevealUiVisible() noexcept;

/** Oy aktifken veya menu onizlemede hizli tarama istegi. */
void ExpectionalVoteRevealRequestScan() noexcept;

void ExpectionalRevealWorkersEnsureStarted() noexcept;
