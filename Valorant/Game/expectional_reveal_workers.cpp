#include "expectional_reveal_workers.hpp"
#include "globals.hpp"
#include "rank_reveal_cache.hpp"
#include "votekick_reveal.hpp"

#include <Windows.h>
#include <atomic>
#include <mutex>
#include <thread>

namespace {

std::atomic<bool> g_workers_started{ false };
std::atomic<bool> g_rank_ui_visible{ false };
std::atomic<bool> g_rank_refresh_requested{ false };
std::atomic<bool> g_vote_scan_requested{ false };

void VoteRevealWorkerLoop() noexcept
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
	DWORD last_scan_ms = 0;
	for (;;) {
		if (!Settings::misc::votekick_reveal_window) {
			last_scan_ms = 0;
			Sleep(500);
			continue;
		}

		const bool menu = Settings::bMenu;
		const bool forced = g_vote_scan_requested.exchange(false, std::memory_order_acq_rel);
		const bool vote_active = ExpectionalVoteRevealIsSessionActive();

		DWORD interval_ms = 1800u;
		if (menu)
			interval_ms = 400u;
		else if (vote_active)
			interval_ms = 120u;
		else if (forced)
			interval_ms = 0u;
		else
			interval_ms = 700u;

		const DWORD now = GetTickCount();
		if (interval_ms > 0u && last_scan_ms != 0u && (now - last_scan_ms) < interval_ms) {
			Sleep(40);
			continue;
		}
		last_scan_ms = now;
		ExpectionalVoteRevealPollFromWorker();
		Sleep(vote_active ? 25 : 50);
	}
}

} 

void ExpectionalRevealWorkersEnsureStarted() noexcept
{
	if (g_workers_started.exchange(true))
		return;
	std::thread(VoteRevealWorkerLoop).detach();
}

void ExpectionalRevealWorkersNotifyOverlayFrame() noexcept
{
	const bool rankOn = Settings::misc::rank_reveal_window;
	const bool voteOn = Settings::misc::votekick_reveal_window;
	if (!rankOn && !voteOn)
		return;

	ExpectionalRevealWorkersEnsureStarted();

	static bool s_prev_tab = false;
	static bool s_rank_feature_was_on = false;
	const bool tabHeld = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
	const bool menu = Settings::bMenu;
	const bool rankUi = rankOn && (tabHeld || menu);
	if (s_rank_feature_was_on && !rankOn)
		rank_reveal_cache::Clear();
	s_rank_feature_was_on = rankOn;

	if (tabHeld && !s_prev_tab)
		g_rank_refresh_requested.store(true, std::memory_order_release);
	s_prev_tab = tabHeld;

	g_rank_ui_visible.store(rankUi, std::memory_order_release);

	if (Settings::misc::votekick_reveal_window && (menu || ExpectionalVoteRevealIsSessionActive()))
		g_vote_scan_requested.store(true, std::memory_order_release);
}

bool ExpectionalRankRevealWantsCacheRefresh() noexcept
{
	if (!Settings::misc::rank_reveal_window)
		return false;
	if (g_rank_refresh_requested.exchange(false, std::memory_order_acq_rel))
		return true;
	if (!g_rank_ui_visible.load(std::memory_order_acquire))
		return false;
	static DWORD s_last_ms = 0;
	const DWORD now = GetTickCount();
	const DWORD iv = Settings::misc::save_fps ? 500u : 350u;
	if (s_last_ms != 0u && (now - s_last_ms) < iv)
		return false;
	s_last_ms = now;
	return true;
}

bool ExpectionalRankRevealWantsNetworkPoll() noexcept
{
	return Settings::misc::rank_reveal_window &&
	       g_rank_ui_visible.load(std::memory_order_acquire);
}

bool ExpectionalRankRevealUiVisible() noexcept
{
	return g_rank_ui_visible.load(std::memory_order_acquire);
}

void ExpectionalVoteRevealRequestScan() noexcept
{
	g_vote_scan_requested.store(true, std::memory_order_release);
	ExpectionalRevealWorkersEnsureStarted();
}
