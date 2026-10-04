#include "votekick_reveal.hpp"
#include "globals.hpp"
#include "grenade_esp.hpp"
#include "../Driver/driver.hpp"
#include "../OSImGui/shade_imgui_settings.h"
#include "../OSImGui/os_imgui_menu.hpp"

#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "../../Includes/Imgui/imgui.h"

struct ExpectionalVoteKickCached {
	bool controller_found = false;
	bool vote_live = false;
	int active_issue = -1;
	int only_team = -999;
	/** m_iOnlyTeamToVote — "Team: T|CT|All" (Keybind tarzi). */
	char vote_team_line[96]{};
	int opt[5]{};
	int potential = 0;
	bool yes_no = false;
	bool votes_dirty = false;
	bool type_dirty = false;
	std::string kick_hint;
	/** Tahmini: yes/no + issue#0 -> Kick (topluluk; yanlis olabilir). */
	bool type_is_kick_guess = false;
	/** NO (F2) ilk saniyede gelmezse Other kilidi; gelirse Kick. Oy sifirlaninca dongu acilir. */
	bool type_force_other_session = false;
	bool vote_type_lock_decided = false;
	unsigned long long vote_type_grace_end_ms = 0;
	/** Tip metni: ilk 0.5 sn "unknown", sonra Kick/Other (UI). */
	unsigned long long vote_type_ui_unlock_ms = 0;
	/** YES+NO toplami >0 iken true; sifira inince false — tip grace yalnizca yeni "oy akisi"nda baslar. */
	bool vote_type_cycle_open = false;
	/** Kompakt oy penceresi: YES/NO sayaci degisince (veya canli gorunurken zaten oy varsa) acilir. */
	bool compact_show_vote = false;
	bool vote_latch_armed = false;
	int vote_latch_y = -1;
	int vote_latch_n = -1;
	/** Tam liste taramasini her kare yapmamak icin son bilinen controller. */
	uintptr_t cached_vote_ent = 0;
	int cached_vote_idx = -1;
};

static ExpectionalVoteKickCached g_vk;
static std::mutex g_vk_mtx;

/** C_VoteController::m_iOnlyTeamToVote — Keybind tarzi "Team: T|CT|All". */
static void ExpectionalFormatVoteTeamLine(int only_team, char* buf, size_t cap)
{
	if (!buf || cap < 16) {
		if (buf && cap)
			buf[0] = '\0';
		return;
	}
	if (only_team == 2)
		std::snprintf(buf, cap, "Team: T");
	else if (only_team == 3)
		std::snprintf(buf, cap, "Team: CT");
	else
		std::snprintf(buf, cap, "Team: All");
}

static int ExpectionalVoteKickScanMax(uintptr_t entity_list, bool narrow_idle_probe)
{
	int i_max = narrow_idle_probe ? 2048 : 4096;
	if (offsets::dwGameEntitySystem_highestEntityIndex && entity_list) {
		const int hi = g_GameMem.readv<int>(entity_list + static_cast<uintptr_t>(offsets::dwGameEntitySystem_highestEntityIndex));
		if (hi >= 1 && hi < 16384) {
			const int cap = narrow_idle_probe ? 2560 : 8192;
			const int floor = narrow_idle_probe ? 1024 : 1536;
			i_max = (std::min)(cap, (std::max)(hi + (narrow_idle_probe ? 128 : 512), floor));
		}
	}
	return i_max;
}

static bool ClassNameLooksLikeVoteController(const char* cn)
{
	if (!cn || !cn[0])
		return false;
	/* CS2 runtime sema: bazen tam "vote_controller" (oneksiz). */
	if (ex_esp::SchemaClassEq(cn, "vote_controller"))
		return true;
	if (ex_esp::SchemaClassEq(cn, "C_VoteController"))
		return true;
	if (ex_esp::SchemaClassEq(cn, "CVoteController"))
		return true;
	if (ex_esp::SchemaClassEq(cn, "cs_vote_controller"))
		return true;
	if (ex_esp::SchemaClassEq(cn, "c_vote_controller"))
		return true;
	if (ex_esp::SchemaClassEq(cn, "ccs_vote_controller"))
		return true;
	if (ex_esp::StrStrIAscii(cn, "VoteController"))
		return true;
	/* cs_vote_controller: "vote" ile "controller" arada _ olsa da ayri alt dizgiler */
	if (ex_esp::StrStrIAscii(cn, "vote") && ex_esp::StrStrIAscii(cn, "controller"))
		return true;
	return false;
}

/** Sinif adi yok; son care: C_VoteController alanlari mantikli tek aday. */
static uintptr_t TryWeakVoteControllerFingerprint(uintptr_t entity_list, int i_max, int* out_idx)
{
	if (out_idx)
		*out_idx = -1;
	if (!offsets::vote_m_iActiveIssueIndex || !offsets::vote_m_bIsYesNoVote || !offsets::vote_m_nPotentialVotes)
		return 0;
	std::vector<int> cands;
	cands.reserve(8);
	for (int i = 1; i < i_max; ++i) {
		const uintptr_t ent = ex_esp::GameEntityByIndex(entity_list, i);
		if (!ent)
			continue;
		const uintptr_t ident = g_GameMem.readv<uintptr_t>(ent + 0x10u);
		if (!ident)
			continue;
		const int ai = g_GameMem.readv<int>(ent + static_cast<uintptr_t>(offsets::vote_m_iActiveIssueIndex));
		const unsigned char yn = g_GameMem.readv<unsigned char>(ent + static_cast<uintptr_t>(offsets::vote_m_bIsYesNoVote));
		const int pot = g_GameMem.readv<int>(ent + static_cast<uintptr_t>(offsets::vote_m_nPotentialVotes));
		if (ai < -1 || ai > 96)
			continue;
		if (yn > 1u)
			continue;
		if (pot < 0 || pot > 128)
			continue;
		/* Ek: secenek sayilari 0..64 arasi */
		bool opt_ok = true;
		if (offsets::vote_m_nVoteOptionCount) {
			const uintptr_t base = ent + static_cast<uintptr_t>(offsets::vote_m_nVoteOptionCount);
			for (int k = 0; k < 5; ++k) {
				const int v = g_GameMem.readv<int>(base + static_cast<uintptr_t>(k * 4));
				if (v < -1 || v > 96) {
					opt_ok = false;
					break;
				}
			}
		}
		if (!opt_ok)
			continue;
		cands.push_back(i);
		if (cands.size() > 6)
			break;
	}
	if (cands.size() != 1)
		return 0;
	const int pick = cands[0];
	if (out_idx)
		*out_idx = pick;
	return ex_esp::GameEntityByIndex(entity_list, pick);
}

/** Client'ta sayac bazen 0; 0x600..0x634 icinde (evet,hayir) ~ (1..32,0..32) cifti ara. */
static bool ExpectionalVoteTallyPairProbe(uintptr_t b)
{
	if (!b)
		return false;
	for (uintptr_t o = 0x600; o <= 0x634; o += 4) {
		const int a = g_GameMem.readv<int>(b + o);
		const int c = g_GameMem.readv<int>(b + o + 4);
		if (a >= 1 && a <= 32 && c >= 0 && c <= 32)
			return true;
	}
	return false;
}

/** Semadaki cnt tabanina gore ±16 bayt layout kaymasi (int32 cifti). */
static bool ExpectionalVoteTallyNearCountBase(uintptr_t b)
{
	if (!b || !offsets::vote_m_nVoteOptionCount)
		return false;
	const uintptr_t cnt0 = static_cast<uintptr_t>(offsets::vote_m_nVoteOptionCount);
	for (intptr_t delta = -16; delta <= 16; delta += 4) {
		const uintptr_t base = b + cnt0 + static_cast<uintptr_t>(delta);
		const int a = g_GameMem.readv<int>(base);
		const int c = g_GameMem.readv<int>(base + 4u);
		if (a >= 1 && a <= 32 && c >= 0 && c <= 32)
			return true;
	}
	return false;
}

struct ExpectionalVoteSnap {
	uintptr_t ent = 0;
	int idx = -1;
	int active = -999;
	int onlyT = -999;
	int opt[5]{};
	int pot = 0;
	bool yn = false;
	bool vd = false;
	bool td = false;
	bool tally_probe = false;
	bool tally_near_cnt = false;
	bool live = false;
	bool kick_guess = false;
	int pick_score = 0;
};

static void ExpectionalFillVoteSnap(uintptr_t b, int match_index, ExpectionalVoteSnap* s)
{
	if (!s || !b)
		return;
	s->ent = b;
	s->idx = match_index;
	s->active = g_GameMem.readv<int>(b + static_cast<uintptr_t>(offsets::vote_m_iActiveIssueIndex));
	s->onlyT = -999;
	if (offsets::vote_m_iOnlyTeamToVote)
		s->onlyT = g_GameMem.readv<int>(b + static_cast<uintptr_t>(offsets::vote_m_iOnlyTeamToVote));
	for (int k = 0; k < 5; ++k)
		s->opt[k] = 0;
	if (offsets::vote_m_nVoteOptionCount) {
		const uintptr_t base = b + static_cast<uintptr_t>(offsets::vote_m_nVoteOptionCount);
		for (int k = 0; k < 5; ++k)
			s->opt[k] = g_GameMem.readv<int>(base + static_cast<uintptr_t>(k * 4));
	}
	s->pot = 0;
	if (offsets::vote_m_nPotentialVotes)
		s->pot = g_GameMem.readv<int>(b + static_cast<uintptr_t>(offsets::vote_m_nPotentialVotes));
	s->yn = false;
	if (offsets::vote_m_bIsYesNoVote)
		s->yn = g_GameMem.readv<uint8_t>(b + static_cast<uintptr_t>(offsets::vote_m_bIsYesNoVote)) != 0;
	s->vd = s->td = false;
	if (offsets::vote_m_bVotesDirty)
		s->vd = g_GameMem.readv<uint8_t>(b + static_cast<uintptr_t>(offsets::vote_m_bVotesDirty)) != 0;
	if (offsets::vote_m_bTypeDirty)
		s->td = g_GameMem.readv<uint8_t>(b + static_cast<uintptr_t>(offsets::vote_m_bTypeDirty)) != 0;

	s->tally_probe = ExpectionalVoteTallyPairProbe(b);
	s->tally_near_cnt = ExpectionalVoteTallyNearCountBase(b);

	int sumOpt = 0;
	for (int x : s->opt)
		sumOpt += (std::max)(0, x);

	const bool team_only_hint = (s->onlyT == 2) || (s->onlyT == 3);
	/*
	 * Ana menüde m_bIsYesNoVote çoğu zaman 1 kalabiliyor; yn veya sadece only_team ile
	 * "oy var" deme — compact pencere ve Kick yanlış pozitif üretir.
	 */
	const bool structural =
	    (s->active >= 0) || (sumOpt > 0) || (s->pot > 0) || s->tally_probe || s->tally_near_cnt;
	s->live = structural || s->vd || s->td;

	const int sumHigh = (std::max)(0, s->opt[2]) + (std::max)(0, s->opt[3]) + (std::max)(0, s->opt[4]);
	const int f12 = (std::max)(0, s->opt[0]) + (std::max)(0, s->opt[1]);
	int pos_buckets = 0;
	for (int k = 0; k < 5; ++k) {
		if (s->opt[k] > 0)
			++pos_buckets;
	}
	/* Harita / coklu secenek: 3+ slotta oy veya F3-F5 tarafinda sayim -> Kick degil. */
	const bool multi_option_look = (sumHigh > 0) || (pos_buckets >= 3);
	const bool binary_f1f2 = (sumHigh == 0) && (f12 > 0 || s->pot > 0);
	const bool yesno_signal = s->yn || binary_f1f2 || s->tally_probe || s->tally_near_cnt;
	/* Client bazen m_iActiveIssueIndex replike etmez (-1); ikili oylama + yapisal sinyal yeterli. */
	const bool issue_ok = (s->active >= 0 && s->active <= 24) || s->tally_probe || s->tally_near_cnt ||
	    (s->active < 0 && (s->vd || s->td) && (sumOpt > 0 || f12 > 0 || s->pot > 0)) ||
	    (s->active < 0 && sumHigh == 0 &&
	        (s->yn || f12 > 0 || s->pot > 0 || s->vd || s->td || s->tally_probe || s->tally_near_cnt));
	/* Kick: saf ikili (F1/F2 slotlari); harita oylari genelde F3+ veya 3+ adayda sayim tasir. */
	s->kick_guess = s->live && structural && yesno_signal && issue_ok && !multi_option_look;

	s->pick_score = sumOpt * 4 + (std::min)(128, s->pot) * 2 + (s->active >= 0 ? 24 : 0) +
	    (structural && s->yn ? 20 : 0) + (s->vd ? 12 : 0) + (s->td ? 12 : 0) + (s->tally_probe ? 80 : 0) +
	    (s->tally_near_cnt ? 100 : 0) + (team_only_hint && structural ? 8 : 0) + (binary_f1f2 ? 16 : 0);
}

/** Adaylar arasinda: skor, canli oylama, toplam oy, dusuk index (stabil). */
static bool ExpectionalVoteSnapBetter(const ExpectionalVoteSnap& cand, const ExpectionalVoteSnap& best)
{
	if (cand.pick_score != best.pick_score)
		return cand.pick_score > best.pick_score;
	if (cand.live != best.live)
		return cand.live && !best.live;
	int sumc = 0, sumb = 0;
	for (int k = 0; k < 5; ++k) {
		sumc += (std::max)(0, cand.opt[k]);
		sumb += (std::max)(0, best.opt[k]);
	}
	if (sumc != sumb)
		return sumc > sumb;
	return cand.idx < best.idx;
}

static bool ExpectionalTryFastVoteSnap(uintptr_t entity_list, ExpectionalVoteSnap& chosen)
{
	if (!g_vk.cached_vote_ent || !entity_list)
		return false;
	const uintptr_t ent = g_vk.cached_vote_ent;
	const uintptr_t ident = g_GameMem.readv<uintptr_t>(ent + 0x10u);
	if (!ident || ident < 0x10000u) {
		g_vk.cached_vote_ent = 0;
		g_vk.cached_vote_idx = -1;
		return false;
	}
	ExpectionalFillVoteSnap(ent, g_vk.cached_vote_idx, &chosen);
	const int y = chosen.opt[0];
	const int n = chosen.opt[1];
	if (chosen.live || chosen.vd || chosen.td || chosen.yn || (y + n) > 0 || chosen.active >= 0)
		return true;
	g_vk.cached_vote_ent = 0;
	g_vk.cached_vote_idx = -1;
	return false;
}

static void CollectKickHintNames(std::string& out)
{
	out.clear();
	if (!client || !offsets::dwEntityList || !offsets::dwPlayerPawn || !offsets::controller_m_bCannotBeKicked ||
	    !offsets::dwSanitizedName)
		return;
	const uintptr_t entity_list = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwEntityList));
	if (!entity_list)
		return;
	const uintptr_t kEntStride = static_cast<uintptr_t>(
	    offsets::entity_controller_stride ? offsets::entity_controller_stride : 112u);
	std::vector<std::string> names;
	names.reserve(8);
	for (int i = 1; i < 64; ++i) {
		const uintptr_t list_entry = g_GameMem.readv<uintptr_t>(
		    entity_list + 8ull * (static_cast<uintptr_t>(i & 0x7FFF) >> 9) + 16);
		if (!list_entry)
			continue;
		const uintptr_t ctrl = g_GameMem.readv<uintptr_t>(list_entry + kEntStride * (i & 0x1FF));
		if (!ctrl)
			continue;
		const bool cannotBeKicked = g_GameMem.readv<uint8_t>(
		    ctrl + static_cast<uintptr_t>(offsets::controller_m_bCannotBeKicked)) != 0;
		if (cannotBeKicked)
			continue;
		std::string raw = g_GameMem.ReadString(ctrl + offsets::dwSanitizedName, 32);
		const size_t z = raw.find('\0');
		if (z != std::string::npos)
			raw.resize(z);
		while (!raw.empty() && (unsigned char)raw.back() <= ' ')
			raw.pop_back();
		if (!raw.empty())
			names.push_back(std::move(raw));
	}
	if (names.empty() || names.size() > 4)
		return;
	for (size_t k = 0; k < names.size(); ++k) {
		if (k)
			out += ", ";
		out += names[k];
	}
}

bool ExpectionalVoteRevealIsSessionActive() noexcept
{
	std::lock_guard<std::mutex> lk(g_vk_mtx);
	return g_vk.compact_show_vote || g_vk.vote_live;
}

void ExpectionalVoteRevealPollFromWorker()
{
	if (!Settings::misc::votekick_reveal_window) {
		std::lock_guard<std::mutex> lk(g_vk_mtx);
		g_vk = {};
		return;
	}
	if (!client || !offsets::dwEntityList || !offsets::vote_m_iActiveIssueIndex) {
		std::lock_guard<std::mutex> lk(g_vk_mtx);
		g_vk = {};
		return;
	}
	const uintptr_t entity_list = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwEntityList));
	if (!entity_list) {
		std::lock_guard<std::mutex> lk(g_vk_mtx);
		g_vk = {};
		return;
	}

	std::lock_guard<std::mutex> lk(g_vk_mtx);

	ExpectionalVoteSnap chosen{};
	int weak_idx = -1;
	uintptr_t vote_ent = 0;
	std::vector<std::pair<uintptr_t, int>> vote_cands;
	vote_cands.reserve(4);

	const bool session_active = g_vk.compact_show_vote || g_vk.vote_live;
	const bool use_fast = g_vk.cached_vote_ent != 0 && (session_active || Settings::bMenu);
	if (use_fast && ExpectionalTryFastVoteSnap(entity_list, chosen))
		vote_ent = chosen.ent;

	if (!vote_ent) {
		const int i_max = ExpectionalVoteKickScanMax(entity_list, !session_active && !Settings::bMenu);
		for (int i = 1; i < i_max; ++i) {
			const uintptr_t ent = ex_esp::GameEntityByIndex(entity_list, i);
			if (!ent)
				continue;
			char cn[128]{};
			if (!ex_esp::ReadEntitySchemaClassName(ent, cn, sizeof cn))
				continue;
			if (!ClassNameLooksLikeVoteController(cn))
				continue;
			vote_cands.push_back({ ent, i });
		}

		if (!vote_cands.empty()) {
			bool have = false;
			for (const auto& pr : vote_cands) {
				ExpectionalVoteSnap s{};
				ExpectionalFillVoteSnap(pr.first, pr.second, &s);
				if (!have || ExpectionalVoteSnapBetter(s, chosen)) {
					chosen = s;
					have = true;
				}
			}
			vote_ent = chosen.ent;
		} else {
			vote_ent = TryWeakVoteControllerFingerprint(entity_list, i_max, &weak_idx);
			if (vote_ent)
				ExpectionalFillVoteSnap(vote_ent, weak_idx, &chosen);
		}
	}

	if (!vote_ent) {
		g_vk.cached_vote_ent = 0;
		g_vk.cached_vote_idx = -1;
		g_vk = {};
		g_vk.controller_found = false;
		return;
	}

	const bool prev_vote_live = g_vk.vote_live;

	g_vk.cached_vote_ent = vote_ent;
	g_vk.cached_vote_idx = chosen.idx;
	g_vk.controller_found = true;
	g_vk.vote_live = chosen.live;
	g_vk.active_issue = chosen.active;
	g_vk.only_team = chosen.onlyT;
	for (int k = 0; k < 5; ++k)
		g_vk.opt[k] = chosen.opt[k];
	g_vk.potential = chosen.pot;
	g_vk.yes_no = chosen.yn;
	g_vk.votes_dirty = chosen.vd;
	g_vk.type_dirty = chosen.td;
	g_vk.type_is_kick_guess = chosen.kick_guess;
	ExpectionalFormatVoteTeamLine(g_vk.only_team, g_vk.vote_team_line, sizeof g_vk.vote_team_line);
	g_vk.kick_hint.clear();

	/* Kompakt pencere: YES/NO toplami >0 ile basladiysa veya sayilar degistiysa acik kalir. */
	if (!chosen.live) {
		g_vk.compact_show_vote = false;
		g_vk.vote_latch_armed = false;
		g_vk.vote_latch_y = -1;
		g_vk.vote_latch_n = -1;
		g_vk.type_force_other_session = false;
		g_vk.vote_type_lock_decided = false;
		g_vk.vote_type_grace_end_ms = 0;
		g_vk.vote_type_cycle_open = false;
		g_vk.vote_type_ui_unlock_ms = 0;
	} else {
		const int y = chosen.opt[0];
		const int n = chosen.opt[1];
		const unsigned long long now_ms = static_cast<unsigned long long>(GetTickCount64());
		/*
		 * Oy bitince sayaclar 0'a iner; controller hala structural "live" kalabilir (yn/vd/td).
		 * Kompakt pencere + latch sifirlanmazsa son oyunun type/takimi yapistir.
		 */
		if (y == 0 && n == 0) {
			if (g_vk.vote_type_cycle_open) {
				g_vk.vote_type_cycle_open = false;
				g_vk.vote_type_lock_decided = false;
				g_vk.vote_type_grace_end_ms = 0;
				g_vk.type_force_other_session = false;
				g_vk.vote_type_ui_unlock_ms = 0;
			}
			if (g_vk.compact_show_vote) {
				g_vk.compact_show_vote = false;
				g_vk.vote_latch_armed = false;
				g_vk.vote_latch_y = -1;
				g_vk.vote_latch_n = -1;
			}
		} else if (!prev_vote_live) {
			g_vk.vote_latch_armed = false;
			g_vk.vote_latch_y = -1;
			g_vk.vote_latch_n = -1;
			g_vk.compact_show_vote = false;
		}
		/* Tip: F2/NO ilk ~1 sn icinde yoksa Other kilidi; gelirse Kick. Dongu = ilk kez oy sayaci >0. */
		if ((y + n) > 0 && !g_vk.vote_type_cycle_open) {
			g_vk.vote_type_cycle_open = true;
			g_vk.vote_type_lock_decided = false;
			g_vk.type_force_other_session = false;
			g_vk.vote_type_grace_end_ms = now_ms + 1000ull;
			g_vk.vote_type_ui_unlock_ms = now_ms + 500ull;
		}
		if (g_vk.vote_type_cycle_open && !g_vk.vote_type_lock_decided && g_vk.vote_type_grace_end_ms != 0) {
			if (n >= 1) {
				g_vk.type_force_other_session = false;
				g_vk.vote_type_lock_decided = true;
			} else if (now_ms >= g_vk.vote_type_grace_end_ms) {
				g_vk.type_force_other_session = (n == 0);
				g_vk.vote_type_lock_decided = true;
			}
		}
		if (!g_vk.vote_latch_armed) {
			g_vk.vote_latch_armed = true;
			g_vk.vote_latch_y = y;
			g_vk.vote_latch_n = n;
			if (y + n > 0)
				g_vk.compact_show_vote = true;
		} else if (y != g_vk.vote_latch_y || n != g_vk.vote_latch_n) {
			g_vk.compact_show_vote = true;
			g_vk.vote_latch_y = y;
			g_vk.vote_latch_n = n;
		}
	}

	if (chosen.live && g_vk.compact_show_vote) {
		static int s_last_hint_y = -1;
		static int s_last_hint_n = -1;
		const int hy = g_vk.opt[0];
		const int hn = g_vk.opt[1];
		if (hy != s_last_hint_y || hn != s_last_hint_n || g_vk.kick_hint.empty()) {
			s_last_hint_y = hy;
			s_last_hint_n = hn;
			CollectKickHintNames(g_vk.kick_hint);
		}
	}
}

void ExpectionalDrawVoteKickRevealWindow()
{
	if (!Settings::misc::votekick_reveal_window)
		return;

	ExpectionalVoteKickCached vk_copy{};
	{
		std::lock_guard<std::mutex> lk(g_vk_mtx);
		vk_copy = g_vk;
	}

	const bool uiOverlay = Settings::bMenu;
	/* Menü açıkken önizleme (konum); menü kapalıyken yalnızca oy varken. */
	if (!uiOverlay && !vk_copy.compact_show_vote)
		return;

	const bool previewIdle = uiOverlay && !vk_copy.compact_show_vote;

	/* Keybind listesi ile ayni palet / padding / rounding */
	const ImVec4 titleFill = c::elements::background_widget;
	ImGui::PushStyleColor(ImGuiCol_WindowBg, c::background::filling);
	ImGui::PushStyleColor(ImGuiCol_Border, c::background::stroke);
	ImGui::PushStyleColor(ImGuiCol_Text, c::elements::text_active);
	ImGui::PushStyleColor(ImGuiCol_TitleBg, titleFill);
	ImGui::PushStyleColor(ImGuiCol_TitleBgActive, titleFill);
	ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, titleFill);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.f, 10.f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);

	ExpectionalOsMenu_UiFontScope uiFont;

	ImGui::SetNextWindowBgAlpha(0.97f);
	ImGui::SetNextWindowPos(ImVec2(48.f, 120.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(220.f, 0.f), ImVec2(420.f, 400.f));

	const ImGuiWindowFlags wf = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
	    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus;

	if (!ImGui::Begin("Vote revealer##ExpectionalVk", nullptr, wf)) {
		ImGui::End();
		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor(6);
		return;
	}

	if (previewIdle) {
		const char* ynPh = "YES: —    NO: —";
		ImGui::TextUnformatted("Type: —");
		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(ynPh).x);
		ImGui::TextUnformatted(ynPh);
		ImGui::TextUnformatted("Team: All");
	} else {
		const bool show_as_kick = !vk_copy.type_force_other_session && vk_copy.type_is_kick_guess;
		const unsigned long long now_ui = static_cast<unsigned long long>(GetTickCount64());
		const bool type_ui_ready =
		    (vk_copy.vote_type_ui_unlock_ms == 0ull) || (now_ui >= vk_copy.vote_type_ui_unlock_ms);
		const char* typeSuffix = !type_ui_ready ? "unknown" : (show_as_kick ? "Kick" : "Other");

		char typeLine[72]{};
		char ynLine[72]{};
		std::snprintf(typeLine, sizeof typeLine, "Type: %s", typeSuffix);
		std::snprintf(ynLine, sizeof ynLine, "YES: %d    NO: %d", vk_copy.opt[0], vk_copy.opt[1]);
		ImGui::TextUnformatted(typeLine);
		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(ynLine).x);
		ImGui::TextUnformatted(ynLine);

		ImGui::TextUnformatted(vk_copy.vote_team_line[0] ? vk_copy.vote_team_line : "Team: All");

		if (!vk_copy.kick_hint.empty()) {
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Separator, c::background::stroke);
			ImGui::Separator();
			ImGui::PopStyleColor();
			ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
			ImGui::TextUnformatted(vk_copy.kick_hint.c_str());
			ImGui::PopTextWrapPos();
		}
	}

	ImGui::End();
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(6);
}
