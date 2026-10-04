#pragma once
/**
 * Optimized world entity enumeration + classification cache.
 *
 * Performans hedefi: dunya ESP (bomb / dropped weapon / world grenade / molotov hull)
 * her tick ayri ayri 2048 entity icin ~5 IOCTL'lik schema class name okumasi yaparak
 * IOCTL sayisini ~25,000+'a cikariyordu. Bu modul iki ana iyilestirme yapar:
 *
 *   1) BLOK BATCH IOCTL: entity list 512'lik bloklara bolunmus. Her blokun pointer
 *      tablosu (stride * 512 byte) tek bir km_device_read ile bir kerede okunur.
 *      Boylece 2048 entity icin enumerasyon 4 IOCTL'e duser (eskiden 2048 IOCTL).
 *
 *   2) CLASSIFY CACHE: ent_ptr -> (ident_ptr, kind, def_idx). Bir sonraki tick'te
 *      ident_ptr ile dogrulanir; ayni entity ise schema class name pointer chain
 *      (~5 IOCTL) ATLANir. Yeni entity'ler ya da geri donusumde tekrar siniflandirilir.
 *
 * Bu sayede steady-state'te (oyun gidiyor, entity'ler ayni) IOCTL maliyeti
 * ~30x dusurulur, RTX 4060 / dusuk-orta uc CPU'larda FPS dususu hissedilmez.
 */

#include "../Driver/driver.hpp"
#include "offsets_runtime.hpp"

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace ex_esp {
namespace world_scan {

enum class Kind : std::uint8_t {
	Unknown = 0,
	Irrelevant,     /**< Schema okundu, ilgisiz (skip et). Yeniden okumayalim. */
	Bomb,           /**< C4 (def_idx == 49). */
	DroppedWeapon,  /**< Normal silah, def_idx >= 1. */
	ProjHe,
	ProjFlash,
	ProjSmoke,
	ProjMolotovAir,
	Inferno,
	ProjDecoy,
};

struct ClassifyEntry {
	std::uintptr_t ident_ptr = 0;
	Kind kind = Kind::Unknown;
	std::uint16_t def_idx = 0;
	std::uint32_t verify_tick = 0;     /**< Siniflandirmanin yapildigi tick (yenilenmez). */
	std::uint32_t last_seen_tick = 0;  /**< Her PeekAndTouch / Lookup'ta guncellenir. */
};

class ClassifyCache {
public:
	/** Irrelevant entries: re-verify periyodu (~3-4 saniye scan tick'ine bagli). */
	static constexpr std::uint32_t kIrrelevantReverifyTicks = 90u;
	/** Interesting entries: re-verify periyodu (cok daha uzun — class degismez). */
	static constexpr std::uint32_t kInterestingReverifyTicks = 600u;

	bool Lookup(std::uintptr_t ent, std::uintptr_t ident_ptr, ClassifyEntry& out) {
		auto it = m_map.find(ent);
		if (it == m_map.end())
			return false;
		if (it->second.ident_ptr != ident_ptr) {
			/** Slot recycle — eski entry'yi sil, yeniden siniflandirilsin. */
			m_map.erase(it);
			return false;
		}
		const std::uint32_t age = m_tick - it->second.verify_tick;
		const std::uint32_t reverify = (it->second.kind == Kind::Irrelevant)
		                                   ? kIrrelevantReverifyTicks
		                                   : kInterestingReverifyTicks;
		if (age > reverify) {
			/** TTL doldu — re-classify icin sil (transient classify failure'larini fixler). */
			m_map.erase(it);
			return false;
		}
		it->second.last_seen_tick = m_tick;
		out = it->second;
		return true;
	}

	/**
	 * FAST PATH: cache'de "Irrelevant" olarak isaretliyse identity dogrulama IOCTL'i atla.
	 * Bu, taranan ~2000 entity'nin %95'i icin identProbe okumayi siler — en buyuk FPS kazanci.
	 * TTL doldugunda re-classify icin entry silinir (slot recycle + spawn transient cozumu).
	 */
	bool PeekAndTouchIrrelevant(std::uintptr_t ent) {
		auto it = m_map.find(ent);
		if (it == m_map.end() || it->second.kind != Kind::Irrelevant)
			return false;
		if (m_tick - it->second.verify_tick > kIrrelevantReverifyTicks) {
			m_map.erase(it);
			return false;
		}
		it->second.last_seen_tick = m_tick;
		return true;
	}

	void Insert(std::uintptr_t ent, const ClassifyEntry& entry) {
		ClassifyEntry e = entry;
		e.verify_tick = m_tick;
		e.last_seen_tick = m_tick;
		m_map[ent] = e;
	}

	/** Eski (kacmis / ortadan kalkmis) girisleri at. */
	void Prune(std::uint32_t max_age_ticks) {
		if (m_map.empty())
			return;
		const std::uint32_t cutoff = (m_tick > max_age_ticks) ? (m_tick - max_age_ticks) : 0u;
		for (auto it = m_map.begin(); it != m_map.end();) {
			if (it->second.last_seen_tick < cutoff)
				it = m_map.erase(it);
			else
				++it;
		}
	}

	void Reset() {
		m_map.clear();
		m_tick = 0;
	}

	std::uint32_t Tick() const noexcept { return m_tick; }
	void NextTick() noexcept { ++m_tick; }

private:
	std::unordered_map<std::uintptr_t, ClassifyEntry> m_map;
	std::uint32_t m_tick = 0;
};

inline ClassifyCache g_classify_cache;
inline std::mutex g_classify_mtx;

/**
 * Tek 512-slot blok pointer tablosunu BATCH okur.
 * Stride * 512 byte tek IOCTL — 64KB limitine sigar (stride genelde ~120).
 * Basarisiz olursa per-entity fallback.
 */
inline int BatchReadEntityBlock(std::uintptr_t block_base, std::uint32_t stride,
                                 std::uintptr_t out_ptrs[512]) noexcept {
	if (!block_base || stride == 0)
		return 0;
	const std::size_t total = static_cast<std::size_t>(stride) * 512u;
	if (total > 0xF000u) {
		/** Stride cok buyuk; fallback. */
		for (int s = 0; s < 512; ++s)
			out_ptrs[s] = g_GameMem.readv<std::uintptr_t>(
			    block_base + static_cast<std::uintptr_t>(stride) * s);
		return 512;
	}
	thread_local std::vector<std::uint8_t> tls_buf;
	if (tls_buf.size() < total)
		tls_buf.resize(total);
	if (!km_device_read(block_base, tls_buf.data(), total)) {
		for (int s = 0; s < 512; ++s)
			out_ptrs[s] = g_GameMem.readv<std::uintptr_t>(
			    block_base + static_cast<std::uintptr_t>(stride) * s);
		return 512;
	}
	const std::uint8_t* base = tls_buf.data();
	for (int s = 0; s < 512; ++s) {
		std::uintptr_t p = 0;
		std::memcpy(&p, base + static_cast<std::size_t>(stride) * s, sizeof(p));
		out_ptrs[s] = p;
	}
	return 512;
}

/** Entity list'in blok-pointer tablosunu (16 blok x 8 byte) tek IOCTL'de okur. */
inline int BatchReadBlockBases(std::uintptr_t entity_list, int blocks_needed,
                                std::uintptr_t out_bases[16]) noexcept {
	if (!entity_list || blocks_needed <= 0)
		return 0;
	const int n = (blocks_needed < 16) ? blocks_needed : 16;
	const std::size_t total = sizeof(std::uintptr_t) * static_cast<std::size_t>(n);
	if (!km_device_read(entity_list + 16u, out_bases, total)) {
		for (int b = 0; b < n; ++b)
			out_bases[b] = g_GameMem.readv<std::uintptr_t>(
			    entity_list + 8ull * static_cast<std::uintptr_t>(b) + 16);
	}
	return n;
}

/**
 * Tum entity'leri (non-null, slot dolu) enumerator callback ile yurutur.
 * cb(idx, ent_ptr, user) — sadece gecerli pointer'lar icin cagrilir.
 * BATCH IOCTL ile ~2048 entity icin sadece ~4 syscall harcar.
 */
template <typename Fn>
inline void EnumerateLiveEntities(std::uintptr_t entity_list, int i_max, Fn&& cb) {
	if (!entity_list || i_max <= 0)
		return;
	/** Stride: orijinal GameEntityByIndex ile ayni fallback (112) — yanlis stride = tum garbage. */
	const std::uint32_t stride = offsets::entity_controller_stride
	                                 ? offsets::entity_controller_stride
	                                 : 112u;

	const int blocks_needed = (i_max + 511) / 512;
	std::uintptr_t block_bases[16]{};
	BatchReadBlockBases(entity_list, blocks_needed, block_bases);

	std::uintptr_t block_ents[512]{};
	for (int b = 0; b < blocks_needed && b < 16; ++b) {
		if (!block_bases[b])
			continue;
		BatchReadEntityBlock(block_bases[b], stride, block_ents);
		const int slot_start = b * 512;
		const int slot_count = (slot_start + 512 <= i_max) ? 512 : (i_max - slot_start);
		for (int s = 0; s < slot_count; ++s) {
			const std::uintptr_t ent = block_ents[s];
			if (!ent || ent < 0x10000ull)
				continue;
			cb(slot_start + s, ent);
		}
	}
}

}  // namespace world_scan
}  // namespace ex_esp
