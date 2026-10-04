#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UE4Structs {
	struct view_matrix_t;
	struct Vector3;
}

/** Oyun HUD'u ile uyumlu kisa etiketler (Jump, Crouch+W+Jump+Throw vb.). */
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

/** Gomulu lineup listesi; Settings::Visuals::grenadeLineups acikken cizer. */
void ExpectionalGrenadeLineupRender(
	const UE4Structs::view_matrix_t& vm,
	std::uintptr_t localPawn,
	const UE4Structs::Vector3& localEyeWorld);

/* ============================================================================
 * Lineup Browser API — PAKET (her .txt = 1 paket) bazinda. CS2'deki "Map Guide"
 * mantigi: kullanici paket secer (Practice 101, Official, vb.), o paketin
 * lineup'larinin tamami aktif olur. En fazla 2 paket ayni anda aktif.
 * ============================================================================ */

struct ExpectionalLineupBrowserPack {
	std::string id;               /**< Tam dosya yolu (unique). */
	std::string title;            /**< KV3'ten cikartilmis paket adi (yoksa dosya adi). */
	std::string map;              /**< Paketin bagli oldugu map (genelde tek). */
	std::size_t lineup_count = 0; /**< Paketteki toplam lineup sayisi. */
};

/** Mevcut map listesi (paket olan haritalar). */
std::vector<std::string> ExpectionalLineupBrowserMapList();

/** Belirli bir map icin tum paketler. */
std::vector<ExpectionalLineupBrowserPack> ExpectionalLineupBrowserPacksForMap(const std::string& map);

/** Staging (henuz apply edilmemis) paket setine ekle/cikar. */
void ExpectionalLineupBrowserStageToggle(const std::string& pack_id);
bool ExpectionalLineupBrowserStageContains(const std::string& pack_id);
std::size_t ExpectionalLineupBrowserStageCount();
void ExpectionalLineupBrowserStageClear();

/** Staged -> active (en fazla 2 paket). */
void ExpectionalLineupBrowserApply();

std::size_t ExpectionalLineupBrowserActiveCount();
bool ExpectionalLineupBrowserActiveContains(const std::string& pack_id);
void ExpectionalLineupBrowserClearActive();

/** Direkt aktif sete ekle (her zaman basarili). */
bool ExpectionalLineupBrowserActiveAdd(const std::string& pack_id);
void ExpectionalLineupBrowserActiveRemove(const std::string& pack_id);
/** Belirli bir map'te aktif olan paket sayisi (uyari mantigi icin). */
std::size_t ExpectionalLineupBrowserActiveCountForMap(const std::string& map);

/** Config kaydet/yukle icin: aktif pack_id listesi. */
std::vector<std::string> ExpectionalLineupBrowserActiveIdsSnapshot();
void ExpectionalLineupBrowserActiveSetFromConfig(const std::vector<std::string>& ids);

/** Workshop refresh — kullanici Steam'e yeni addon yukledikten sonra. */
void ExpectionalLineupBrowserRefreshWorkshop();
bool ExpectionalLineupsLoaded();
std::size_t ExpectionalLineupsTotalCount();
std::size_t ExpectionalLineupPacksTotalCount();
