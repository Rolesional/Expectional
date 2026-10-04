#include "rank_reveal_cache.hpp"

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace rank_reveal_cache {

namespace {

struct Slot {
	UE4Structs::CS2Entity ent{};
	int miss_scans = 0;
};

std::mutex g_mtx;
std::unordered_map<std::uint64_t, Slot> g_slots;
std::vector<std::uint64_t> g_row_order;

static int TeamSortOrder(int rawTeam) noexcept
{
	const int t = rawTeam & 0xFF;
	if (t == 2)
		return 0;
	if (t == 3)
		return 1;
	return 2;
}

static std::uint64_t PlayerStableKey(const UE4Structs::CS2Entity& e) noexcept
{
	if (e.steam_id64 > 17ull)
		return e.steam_id64;
	if (e.Controller >= 0x10000ull)
		return 0xC000000000000000ull | (e.Controller & 0x0000FFFFFFFFFFFFull);
	return 0xA000000000000000ull | static_cast<std::uint64_t>(e.entity_index & 0xFFFF);
}

static bool EntityLess(const UE4Structs::CS2Entity& a, const UE4Structs::CS2Entity& b) noexcept
{
	const int oa = TeamSortOrder(a.team_num);
	const int ob = TeamSortOrder(b.team_num);
	if (oa != ob)
		return oa < ob;
	return a.name < b.name;
}

static void PatchRankFieldsOnly(UE4Structs::CS2Entity& dst, const UE4Structs::CS2Entity& fresh)
{
	dst.health = fresh.health;
	dst.armor = fresh.armor;
	dst.team_num = fresh.team_num;
	dst.entity_index = fresh.entity_index;
	dst.pawn_handle_low = fresh.pawn_handle_low;
	dst.Actor = fresh.Actor;
	dst.Controller = fresh.Controller;
	if (!fresh.name.empty())
		dst.name = fresh.name;
	if (fresh.steam_id64 > 17ull)
		dst.steam_id64 = fresh.steam_id64;
	dst.competitive_ranking = fresh.competitive_ranking;
	dst.competitive_wins = fresh.competitive_wins;
	dst.competitive_rank_type = fresh.competitive_rank_type;
	dst.rank_pred_win = fresh.rank_pred_win;
	dst.rank_pred_loss = fresh.rank_pred_loss;
	dst.rank_pred_tie = fresh.rank_pred_tie;
}

static void InsertKeySorted(std::uint64_t key)
{
	const UE4Structs::CS2Entity& ref = g_slots[key].ent;
	auto it = g_row_order.begin();
	for (; it != g_row_order.end(); ++it) {
		if (EntityLess(ref, g_slots[*it].ent))
			break;
	}
	g_row_order.insert(it, key);
}

static void PruneMissing(const std::unordered_map<std::uint64_t, bool>& seen_this_scan, bool tab_visible)
{
	const int max_miss = tab_visible ? 4 : 1;
	for (auto it = g_row_order.begin(); it != g_row_order.end();) {
		const std::uint64_t key = *it;
		auto sit = g_slots.find(key);
		if (sit == g_slots.end()) {
			it = g_row_order.erase(it);
			continue;
		}
		if (seen_this_scan.count(key)) {
			sit->second.miss_scans = 0;
			++it;
			continue;
		}
		if (++sit->second.miss_scans >= max_miss) {
			g_slots.erase(sit);
			it = g_row_order.erase(it);
		} else {
			++it;
		}
	}
}

} // namespace

void Clear()
{
	std::lock_guard<std::mutex> lk(g_mtx);
	g_slots.clear();
	g_row_order.clear();
}

void MergeScannedPlayers(const std::vector<UE4Structs::CS2Entity>& scanned, bool tab_or_menu_visible)
{
	std::lock_guard<std::mutex> lk(g_mtx);
	std::unordered_map<std::uint64_t, bool> seen;
	seen.reserve(scanned.size());

	for (const auto& fresh : scanned) {
		const std::uint64_t key = PlayerStableKey(fresh);
		seen[key] = true;
		auto it = g_slots.find(key);
		if (it == g_slots.end()) {
			Slot s{};
			s.ent = fresh;
			s.miss_scans = 0;
			g_slots.emplace(key, std::move(s));
			InsertKeySorted(key);
		} else {
			PatchRankFieldsOnly(it->second.ent, fresh);
			it->second.miss_scans = 0;
		}
	}

	if (!scanned.empty())
		PruneMissing(seen, tab_or_menu_visible);
}

void CopyStableSnapshot(std::vector<UE4Structs::CS2Entity>& out)
{
	std::lock_guard<std::mutex> lk(g_mtx);
	out.clear();
	out.reserve(g_row_order.size());
	for (std::uint64_t key : g_row_order) {
		const auto it = g_slots.find(key);
		if (it != g_slots.end())
			out.push_back(it->second.ent);
	}
}

void CollectSteamIds(std::vector<std::uint64_t>& out)
{
	std::lock_guard<std::mutex> lk(g_mtx);
	for (std::uint64_t key : g_row_order) {
		const auto it = g_slots.find(key);
		if (it == g_slots.end())
			continue;
		const std::uint64_t sid = it->second.ent.steam_id64;
		if (sid > 17ull)
			out.push_back(sid);
	}
}

} // namespace rank_reveal_cache
