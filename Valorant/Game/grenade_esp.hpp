#pragma once
/**
 * Catalyst benzeri: dunya nades (smoke suresi, inferno/molotov, decoy), basit grenade helper (havasim).
 * Varlik sinifi client.dll schema zinciriyle okunur; alanlar offsets_runtime + kernel read.
 */
#include "globals.hpp"
#include "offsets_runtime.hpp"
#include "structs.hpp"
#include "../Driver/driver.hpp"
#include "entity_handle.hpp"
#include "esp_extras.hpp"
#include "catalyst_world_bvh.hpp"
#include "cat_mem_scan.hpp"
#include "catalyst_player_esp.hpp"
#include "weapon_names.hpp"
#include "expectional_misc_runtime.hpp"
#include "world_entity_scan_cache.hpp"

#include "../../Includes/Imgui/imgui.h"
#include "../AnanbabanOverlay/expectional_frame_cache.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ex_esp {

/** Dunya entity taramasi yapan Other ESP sayisi (carrier envanter/cacheGame'de; tam liste taramasi degil). */
inline bool WorldGrenadeEspActive() noexcept {
	return Settings::Visuals::worldGrenades || Settings::Visuals::worldInfernoHull;
}

inline int ConcurrentHeavyWorldKernelScanFeatures() {
	int n = 0;
	if (WorldGrenadeEspActive())
		++n;
	if (Settings::Visuals::bombWorldEsp)
		++n;
	if (Settings::Visuals::droppedWeaponEsp)
		++n;
	return n;
}

inline bool AnyWorldPickupEspEnabled() noexcept {
	return Settings::Visuals::bombWorldEsp || Settings::Visuals::bombCarrierEsp ||
	       Settings::Visuals::droppedWeaponEsp;
}

inline uintptr_t GameEntityByIndex(uintptr_t entity_list, int i) {
	if (!entity_list || i < 0 || i >= 8192)
		return 0;
	const uintptr_t list_entry = g_GameMem.readv<uintptr_t>(
		entity_list + 8ull * (static_cast<uintptr_t>(i & 0x7FFF) >> 9) + 16);
	if (!list_entry)
		return 0;
	const uintptr_t stride = static_cast<uintptr_t>(
		offsets::entity_controller_stride ? offsets::entity_controller_stride : 112u);
	return g_GameMem.readv<uintptr_t>(list_entry + stride * (i & 0x1FF));
}

/** Yerdeki pickup/bomb: IOCTL/surucu yuku; mapped .sys ile BSOD riskini azaltmak icin tavan dusuk tutulur. */
inline int ClampedPickupEntityScanMax(uintptr_t entity_list) {
	int i_max = 1152;
	if (offsets::dwGameEntitySystem_highestEntityIndex && entity_list) {
		const int hiRead =
			g_GameMem.readv<int>(entity_list + static_cast<uintptr_t>(offsets::dwGameEntitySystem_highestEntityIndex));
		if (hiRead >= 1 && hiRead < 16384)
			i_max = (std::min)(1536, (std::max)(hiRead + 96, 640));
	}
	const int combo = ConcurrentHeavyWorldKernelScanFeatures();
	if (combo >= 3)
		i_max = (std::min)(i_max, 960);
	else if (combo >= 2)
		i_max = (std::min)(i_max, 1152);
	return i_max;
}

/**
 * Sadece yer silahi ESP — bomb/nade ile paylasilan ClampedPickupEntityScanMax dusuk index'te birakabiliyordu.
 * IOCTL biraz artar; kaçan dusuklari azaltir.
 */
inline int ClampedDroppedWeaponScanMax(uintptr_t entity_list) {
	int i_max = 2048;
	if (offsets::dwGameEntitySystem_highestEntityIndex && entity_list) {
		const int hiRead =
			g_GameMem.readv<int>(entity_list + static_cast<uintptr_t>(offsets::dwGameEntitySystem_highestEntityIndex));
		if (hiRead >= 1 && hiRead < 16384)
			i_max = (std::min)(3072, (std::max)(hiRead + 256, 1024));
		if (hiRead >= 10000)
			i_max = (std::min)(i_max, 2400);
		else if (hiRead >= 7500)
			i_max = (std::min)(i_max, 2688);
	}
	const int combo = ConcurrentHeavyWorldKernelScanFeatures();
	if (Settings::misc::save_fps) {
		if (combo >= 3)
			i_max = (std::min)(i_max, 1280);
		else if (combo >= 2)
			i_max = (std::min)(i_max, 1536);
		else
			i_max = (std::min)(i_max, 1792);
	} else if (combo >= 3) {
		i_max = (std::min)(i_max, 2048);
	}
	return i_max;
}

/** Dunya projectile/nade periyodik tarama. */
inline int ClampedProjectileEntityScanMax(uintptr_t entity_list) {
	int i_max = 2048;
	if (offsets::dwGameEntitySystem_highestEntityIndex && entity_list) {
		const int hiRead =
			g_GameMem.readv<int>(entity_list + static_cast<uintptr_t>(offsets::dwGameEntitySystem_highestEntityIndex));
		if (hiRead >= 1 && hiRead < 16384)
			i_max = (std::min)(3072, (std::max)(hiRead + 256, 1024));
		/** Cok yuksek entity index: tam aralik IOCTL maliyeti cok; ust ucu biraz dusur. */
		if (hiRead >= 10000)
			i_max = (std::min)(i_max, 2400);
		else if (hiRead >= 7500)
			i_max = (std::min)(i_max, 2688);
	}
	const int combo = ConcurrentHeavyWorldKernelScanFeatures();
	if (combo >= 3)
		i_max = (std::min)(i_max, 1664);
	else if (combo >= 2)
		i_max = (std::min)(i_max, 2304);
	return i_max;
}

/**
 * Catalyst systems::entities::get_schema_hash ile ayni zincir: CEntityIdentity+0x8 -> sinif bilgisi;
 * +0x8 uzerinden iki kademeli pointer, son adres C++ sema adi (ornek C_Inferno).
 * designerName (+0x20) ile ayni metin degil; nade/inferno ESP bu adlari bekliyor.
 */
/** Catalyst g::memory.read(schema_name, buf, 64) — tek IOCTL, char-by-char degil. */
inline bool ReadRemoteCStringRaw(uintptr_t addr, char* out, size_t cap) {
	if (!addr || !out || cap < 4)
		return false;
	out[0] = 0;
	const size_t maxRead = (cap < 96u) ? (cap - 1u) : 95u;
	char buf[96]{};
	if (km_device_read(addr, buf, maxRead)) {
		buf[maxRead] = 0;
		size_t n = 0;
		while (n < maxRead && buf[n])
			++n;
		if (!n)
			return false;
		std::memcpy(out, buf, n);
		out[n] = 0;
		return true;
	}
	for (size_t i = 0; i < maxRead; ++i) {
		const char c = g_GameMem.readv<char>(addr + i);
		out[i] = c;
		if (!c) {
			out[i] = 0;
			return out[0] != 0;
		}
	}
	out[maxRead] = 0;
	return out[0] != 0;
}

inline bool ReadEntityCppSchemaClassName(uintptr_t ent, char* out, size_t cap) {
	if (!ent || !out || cap < 4)
		return false;
	out[0] = 0;
	const uintptr_t identity = g_GameMem.readv<uintptr_t>(ent + 0x10);
	if (!identity)
		return false;
	const uintptr_t classInfo = g_GameMem.readv<uintptr_t>(identity + 0x8);
	if (!classInfo)
		return false;
	const uintptr_t schemaNamePtr = g_GameMem.readv<uintptr_t>(classInfo + 0x8);
	if (!schemaNamePtr)
		return false;
	const uintptr_t schemaName = g_GameMem.readv<uintptr_t>(schemaNamePtr);
	if (!schemaName)
		return false;
	return ReadRemoteCStringRaw(schemaName, out, cap);
}

inline bool ReadEntityDesignerClassName(uintptr_t ent, char* out, size_t cap) {
	if (!ent || !out || cap < 4)
		return false;
	out[0] = 0;
	const uintptr_t identity = g_GameMem.readv<uintptr_t>(ent + 0x10);
	if (!identity)
		return false;
	for (uintptr_t symOff : { 0x20ul, 0x18ul }) {
		const uintptr_t namePtr = g_GameMem.readv<uintptr_t>(identity + symOff);
		if (!namePtr)
			continue;
		if (ReadRemoteCStringRaw(namePtr, out, cap))
			return true;
	}
	return false;
}

/** Once C++ sema adi, olmazsa designerName / m_name (client.dll.hpp +0x20 / +0x18). */
inline bool ReadEntitySchemaClassName(uintptr_t ent, char* out, size_t cap) {
	if (!ent || !out || cap < 4)
		return false;
	out[0] = 0;
	if (ReadEntityCppSchemaClassName(ent, out, cap))
		return true;

	const uintptr_t identity = g_GameMem.readv<uintptr_t>(ent + 0x10);
	if (!identity)
		return false;
	for (uintptr_t symOff : { 0x20ul, 0x18ul }) {
		const uintptr_t namePtr = g_GameMem.readv<uintptr_t>(identity + symOff);
		if (!namePtr)
			continue;
		if (ReadRemoteCStringRaw(namePtr, out, cap))
			return true;
	}
	return false;
}

inline unsigned char LowerAscii(unsigned char c) {
	return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + 32u) : c;
}

/** DesignerName bazen tamamen kucuk harf gelir; strstr buyuk/kucuk harf duyarli oldugu icin ESP kaciriyordu. */
inline bool SchemaClassEq(const char* cn, const char* lit) {
	if (!cn || !lit)
		return false;
	while (*cn && *lit) {
		if (LowerAscii(static_cast<unsigned char>(*cn++)) != LowerAscii(static_cast<unsigned char>(*lit++)))
			return false;
	}
	return !*cn && !*lit;
}

inline const char* StrStrIAscii(const char* haystack, const char* needle) {
	if (!haystack || !needle || !needle[0])
		return nullptr;
	for (const char* h = haystack; *h; ++h) {
		const char* hp = h;
		const char* np = needle;
		while (*hp && *np &&
			LowerAscii(static_cast<unsigned char>(*hp)) == LowerAscii(static_cast<unsigned char>(*np))) {
			++hp;
			++np;
		}
		if (!*np)
			return h;
	}
	return nullptr;
}

enum class WorldProjectileKind : uint8_t {
	None = 0,
	He,
	Flash,
	Smoke,
	MolotovAir,
	Inferno,
	Decoy,
};

/** Catalyst fnv1a::runtime_hash — sinif adi dogrulama / classify. */
inline uint32_t Fnv1aRuntimeHash(const char* str) noexcept {
	uint32_t hash = 2166136261u;
	while (str && *str) {
		hash ^= static_cast<uint32_t>(*str++);
		hash *= 16777619u;
	}
	return hash;
}

/** Catalyst collector::classify_projectile — birebir FNV hash. */
inline WorldProjectileKind ClassifyWorldProjectileByHash(uint32_t schemaHash) {
	switch (schemaHash) {
	case 0x74db88a5u:
		return WorldProjectileKind::He;
	case 0x3e9aa8ceu:
		return WorldProjectileKind::Flash;
	case 0x4e65f325u:
		return WorldProjectileKind::Smoke;
	case 0xf6ca69ecu:
		return WorldProjectileKind::MolotovAir;
	case 0x41a8368eu:
		return WorldProjectileKind::Inferno;
	case 0x40987e9au:
		return WorldProjectileKind::Decoy;
	default:
		return WorldProjectileKind::None;
	}
}

/** Catalyst collector::classify_projectile — birebir sinif adi. */
inline WorldProjectileKind ClassifyWorldProjectileCatalyst(const char* cn) {
	if (!cn || !cn[0])
		return WorldProjectileKind::None;
	const WorldProjectileKind byHash = ClassifyWorldProjectileByHash(Fnv1aRuntimeHash(cn));
	if (byHash != WorldProjectileKind::None)
		return byHash;
	if (SchemaClassEq(cn, "C_HEGrenadeProjectile"))
		return WorldProjectileKind::He;
	if (SchemaClassEq(cn, "C_FlashbangProjectile"))
		return WorldProjectileKind::Flash;
	if (SchemaClassEq(cn, "C_SmokeGrenadeProjectile"))
		return WorldProjectileKind::Smoke;
	if (SchemaClassEq(cn, "C_MolotovProjectile"))
		return WorldProjectileKind::MolotovAir;
	if (SchemaClassEq(cn, "C_Inferno"))
		return WorldProjectileKind::Inferno;
	if (SchemaClassEq(cn, "C_DecoyProjectile"))
		return WorldProjectileKind::Decoy;
	return WorldProjectileKind::None;
}

/** C++ schema okunamazsa designerName ile yedek (Catalyst'te yok; surucu farki icin). */
inline WorldProjectileKind ClassifyWorldProjectileDesignerFallback(const char* cn) {
	if (!cn || !cn[0])
		return WorldProjectileKind::None;
	if (SchemaClassEq(cn, "C_Inferno") || StrStrIAscii(cn, "inferno"))
		return WorldProjectileKind::Inferno;
	if (StrStrIAscii(cn, "hegrenade") && StrStrIAscii(cn, "projectile"))
		return WorldProjectileKind::He;
	if (StrStrIAscii(cn, "flashbang") && StrStrIAscii(cn, "projectile"))
		return WorldProjectileKind::Flash;
	if (StrStrIAscii(cn, "smokegrenade") && StrStrIAscii(cn, "projectile"))
		return WorldProjectileKind::Smoke;
	if (StrStrIAscii(cn, "molotov") && StrStrIAscii(cn, "projectile"))
		return WorldProjectileKind::MolotovAir;
	if (StrStrIAscii(cn, "decoy") && StrStrIAscii(cn, "projectile"))
		return WorldProjectileKind::Decoy;
	if (StrStrIAscii(cn, "projectile")) {
		if (StrStrIAscii(cn, "he") || StrStrIAscii(cn, "frag"))
			return WorldProjectileKind::He;
		if (StrStrIAscii(cn, "flash"))
			return WorldProjectileKind::Flash;
		if (StrStrIAscii(cn, "smoke"))
			return WorldProjectileKind::Smoke;
		if (StrStrIAscii(cn, "molotov") || StrStrIAscii(cn, "incendiary"))
			return WorldProjectileKind::MolotovAir;
		if (StrStrIAscii(cn, "decoy"))
			return WorldProjectileKind::Decoy;
	}
	return WorldProjectileKind::None;
}

/** Tek bir inferno yangin noktasi gecerli mi (stale/garbage filtre). */
inline bool IsPlausibleInfernoFirePosition(const Vector3& p) {
	if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
		return false;
	if (std::fabs(p.x) > 65536.f || std::fabs(p.y) > 65536.f || std::fabs(p.z) > 65536.f)
		return false;
	return !(p.x == 0.f && p.y == 0.f && p.z == 0.f);
}

/** Catalyst: inferno ~7s; gecerli effect tick yoksa hayalet (timer donuk). */
struct InfernoTimerState {
	bool valid = false;
	float remaining = 0.f;
	float frac = 0.f;
	float expireTime = 0.f;
	int effectTick = 0;
};

inline bool ComputeInfernoTimer(uintptr_t ent, float curTime, InfernoTimerState& out) {
	out = {};
	if (!ent || !offsets::inferno_m_nFireEffectTickBegin)
		return false;
	const int effTick = g_GameMem.readv<int>(
		ent + static_cast<uintptr_t>(offsets::inferno_m_nFireEffectTickBegin));
	if (effTick <= 0)
		return false;
	out.effectTick = effTick;
	const float startTime = static_cast<float>(effTick) * (1.f / 64.f);
	constexpr float kDur = 7.f;
	out.expireTime = startTime + kDur;
	if (curTime <= 1.f) {
		out.remaining = kDur;
		out.frac = 1.f;
		out.valid = true;
		return true;
	}
	if (startTime > curTime + 0.25f)
		return false;
	out.remaining = (std::max)(0.f, out.expireTime - curTime);
	out.frac = std::clamp(out.remaining / kDur, 0.f, 1.f);
	if (out.remaining <= 0.05f)
		return false;
	/** Yangin basladiktan sonra timer hala dolu: bozuk/stale entity (effTick donuk). */
	const float elapsed = curTime - startTime;
	if (elapsed > 0.75f && out.remaining > 6.85f)
		return false;
	out.valid = true;
	return true;
}

/** Inferno suresi doldu mu (Catalyst: 7s after m_nFireEffectTickBegin). */
inline bool InfernoExpiredByTick(uintptr_t ent, float curTime) {
	InfernoTimerState ts;
	if (!ComputeInfernoTimer(ent, curTime, ts))
		return true;
	return !ts.valid;
}

/** C_Inferno sinifi okunamasa bile m_fireCount + aktif yanma + gecerli konum ile tespit. */
inline bool EntityLooksLikeInferno(uintptr_t ent, float curTime = 0.f) {
	if (!ent || !offsets::inferno_m_fireCount || !offsets::inferno_m_firePositions ||
	    !offsets::inferno_m_bFireIsBurning)
		return false;
	const int fc = g_GameMem.readv<int>(ent + static_cast<uintptr_t>(offsets::inferno_m_fireCount));
	if (fc <= 0 || fc > 64)
		return false;
	const uintptr_t firePosBase = ent + static_cast<uintptr_t>(offsets::inferno_m_firePositions);
	const uintptr_t fireBurnBase = ent + static_cast<uintptr_t>(offsets::inferno_m_bFireIsBurning);
	const int probe = (fc < 64) ? fc : 64;
	int burning = 0;
	for (int i = 0; i < probe; ++i) {
		if (!g_GameMem.readv<bool>(fireBurnBase + static_cast<uintptr_t>(i)))
			continue;
		const Vector3 p = g_GameMem.readv<Vector3>(
			firePosBase + static_cast<uintptr_t>(i) * sizeof(Vector3));
		if (!IsPlausibleInfernoFirePosition(p))
			continue;
		++burning;
	}
	if (burning <= 0)
		return false;
	InfernoTimerState ts;
	return ComputeInfernoTimer(ent, curTime, ts);
}

/** Catalyst entities::refresh — highestEntityIndex + IOCTL tavan. */
inline int CatalystWorldProjectileScanMax(uintptr_t entity_list) {
	return ClampedProjectileEntityScanMax(entity_list);
}

/** Forward decl: silah "weapon"/"knife"/"gun" filtresi — dosyada asagida tanimli. */
inline bool SchemaLooksLikeDroppedGun(const char* cn);

/** Forward decl: inferno offset imzasi hizli check — detail namespace icinde asagida. */
namespace grenade_esp_detail {
inline bool QuickInfernoSlotCountOk(uintptr_t ent);
}

/**
 * Classify cache: bir entity'i TEK seferde TAM siniflandirir (her chain'i dener).
 *
 * ONEMLI: tum classify chain'leri kosulsuz calistirilir, boylece cache sonucu hangi
 * scan'in cagirdigindan bagimsizdir. Bomb scan'i siniflandirdiginda grenade scan'i
 * de ayni dogru kind'i alir. (Eskiden want_* gate'leri Irrelevant cachelemesi
 * yaratiyordu → inferno/dropped/c4 gorunmuyordu.)
 */
inline world_scan::Kind ClassifyEntityCached(uintptr_t ent, uintptr_t ident_ptr,
                                              uint16_t& out_def, float curTime) noexcept {
	using namespace world_scan;
	{
		ClassifyEntry cached{};
		if (g_classify_cache.Lookup(ent, ident_ptr, cached)) {
			out_def = cached.def_idx;
			/** Inferno timer dustuyse expired — ama cache'i bozma (entity hala Inferno). */
			if (cached.kind == Kind::Inferno && InfernoExpiredByTick(ent, curTime))
				return Kind::Irrelevant;
			return cached.kind;
		}
	}

	/** CPP schema (catalyst hash icin) + Designer schema (eski kod weapon detection burada kullanir). */
	char cnCpp[112]{};
	char cnDes[112]{};
	const bool cppOk = ReadEntityCppSchemaClassName(ent, cnCpp, sizeof cnCpp);
	const bool desOk = ReadEntityDesignerClassName(ent, cnDes, sizeof cnDes);

	WorldProjectileKind proj_kind = WorldProjectileKind::None;
	if (cppOk)
		proj_kind = ClassifyWorldProjectileCatalyst(cnCpp);
	if (proj_kind == WorldProjectileKind::None && desOk)
		proj_kind = ClassifyWorldProjectileDesignerFallback(cnDes);

	Kind kind = Kind::Irrelevant;
	uint16_t def = 0;

	switch (proj_kind) {
	case WorldProjectileKind::He:        kind = Kind::ProjHe; break;
	case WorldProjectileKind::Flash:     kind = Kind::ProjFlash; break;
	case WorldProjectileKind::Smoke:     kind = Kind::ProjSmoke; break;
	case WorldProjectileKind::MolotovAir:kind = Kind::ProjMolotovAir; break;
	case WorldProjectileKind::Inferno:   kind = Kind::Inferno; break;
	case WorldProjectileKind::Decoy:     kind = Kind::ProjDecoy; break;
	default: break;
	}

	/** Inferno offset-imza fallback (schema "C_Inferno" gelmezse bile). Bagimsiz check. */
	if (kind == Kind::Irrelevant && grenade_esp_detail::QuickInfernoSlotCountOk(ent) &&
	    EntityLooksLikeInferno(ent, curTime)) {
		kind = Kind::Inferno;
	}

	/**
	 * Silah / C4 tespiti — CPP schema yoksa designer adi kullan (eski TickDropped 'ReadEntitySchemaClassName'
	 * combined reader idi; yerde silah olan ama cpp schema chain'i okunamayan entity'ler aksi halde kacardi).
	 */
	const char* cn = nullptr;
	if (cppOk && cnCpp[0]) cn = cnCpp;
	else if (desOk && cnDes[0]) cn = cnDes;

	if (kind == Kind::Irrelevant && cn) {
		const bool excluded =
		    StrStrIAscii(cn, "viewmodel") || StrStrIAscii(cn, "view_model") ||
		    StrStrIAscii(cn, "planted") || StrStrIAscii(cn, "observer") ||
		    StrStrIAscii(cn, "playerpawn") || StrStrIAscii(cn, "player_pawn") ||
		    StrStrIAscii(cn, "controller");
		if (!excluded) {
			const uint16_t d = ReadWeaponDefIndexDropped(ent, cn);
			if (d == 49 || StrStrIAscii(cn, "c4")) {
				kind = Kind::Bomb;
				def = d ? d : 49;
			} else if (d && !(d >= 43 && d <= 48) && SchemaLooksLikeDroppedGun(cn)) {
				kind = Kind::DroppedWeapon;
				def = d;
			}
		}
	}

	/**
	 * Cache'leme kosulu: en az bir schema okuma basariliysa cache'le.
	 * Tum okuma basarisiz olursa cache'leme — transient IOCTL failure'i bir
	 * entity'i 'Irrelevant' olarak yapistirip 14+ saniye kacirmasin.
	 */
	const bool any_schema_ok = cppOk || desOk;
	if (any_schema_ok) {
		ClassifyEntry entry{};
		entry.ident_ptr = ident_ptr;
		entry.kind = kind;
		entry.def_idx = def;
		g_classify_cache.Insert(ent, entry);
	}

	out_def = def;
	if (kind == Kind::Inferno && InfernoExpiredByTick(ent, curTime))
		return Kind::Irrelevant;
	return kind;
}

inline bool IsInfernoEntityClass(const char* cn) {
	if (!cn)
		return false;
	return SchemaClassEq(cn, "C_Inferno");
}

inline float GlobalCurTime() {
	if (!client || !offsets::dwGlobalVars)
		return 0.f;
	const uintptr_t gv = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwGlobalVars));
	if (!gv)
		return 0.f;
	return g_GameMem.readv<float>(gv + 0x30);
}

inline uint32_t LocalPlayerPawnHandle() {
	if (!client || !offsets::dwLocalPlayerController || !offsets::dwPlayerPawn)
		return 0;
	const uintptr_t ctrl = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwLocalPlayerController));
	if (!ctrl)
		return 0;
	return g_GameMem.readv<uint32_t>(ctrl + static_cast<uintptr_t>(offsets::dwPlayerPawn));
}

inline void DrawProjTimerBar(ImDrawList* dl, float cx, float y, float frac, ImU32 fillCol, ImU32 bgCol) {
	const float barW = 36.f;
	const float barH = 4.f;
	const float bx = cx - barW * 0.5f;
	dl->AddRectFilled(ImVec2(bx - 1.f, y - 1.f), ImVec2(bx + barW + 1.f, y + barH + 1.f), bgCol, 2.f);
	dl->AddRectFilled(ImVec2(bx, y), ImVec2(bx + barW * std::clamp(frac, 0.f, 1.f), y + barH), fillCol, 2.f);
}

/** Catalyst projectile::draw_timer — 30x3, renk frac ile (kalan yuksek iken hiCol). */
inline void DrawCatalystProjTimerBar(ImDrawList* dl, float cx, float yTop, float frac,
	ImU32 hiCol, ImU32 loCol, ImU32 bgCol) {
	const float barW = 30.f;
	const float barH = 3.f;
	const float bx = std::floor(cx - barW * 0.5f);
	const float by = std::floor(yTop);
	const float f = std::clamp(frac, 0.f, 1.f);
	const ImVec4 a = ImGui::ColorConvertU32ToFloat4(hiCol);
	const ImVec4 b = ImGui::ColorConvertU32ToFloat4(loCol);
	const float u = f;
	ImVec4 m{};
	m.x = a.x * u + b.x * (1.f - u);
	m.y = a.y * u + b.y * (1.f - u);
	m.z = a.z * u + b.z * (1.f - u);
	m.w = a.w * u + b.w * (1.f - u);
	const ImU32 fill = ImGui::ColorConvertFloat4ToU32(m);
	dl->AddRectFilled(ImVec2(bx - 1.f, by - 1.f), ImVec2(bx + barW + 1.f, by + barH + 1.f), bgCol);
	dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + barW * f, by + barH), fill);
}

/** Catalyst projectile ESP: glyph ekran (cx,cy) merkezinde; cy = merkez Y. */
inline void DrawWeaponGlyphCenterOutlined(ImDrawList* dl, float cx, float cy, const char* glyph, ImU32 col) {
	if (!g_WeaponsIconFont || !glyph || !glyph[0])
		return;
	ImGui::PushFont(g_WeaponsIconFont);
	const ImVec2 ts = ImGui::CalcTextSize(glyph);
	const float x = std::floor(cx - ts.x * 0.5f);
	const float y = std::floor(cy - ts.y * 0.5f);
	for (int ox = -1; ox <= 1; ++ox)
		for (int oy = -1; oy <= 1; ++oy)
			if (ox || oy)
				dl->AddText(ImVec2(x + (float)ox, y + (float)oy), IM_COL32(0, 0, 0, 220), glyph);
	dl->AddText(ImVec2(x, y), col, glyph);
	ImGui::PopFont();
}

/** Catalyst projectile.cpp — weapons font tek karakter. */
inline void DrawWeaponGlyphOutlined(ImDrawList* dl, float cx, float cyTop, const char* glyph, ImU32 col,
	float* outBottomY) {
	if (!g_WeaponsIconFont || !glyph || !glyph[0]) {
		if (outBottomY)
			*outBottomY = cyTop;
		return;
	}
	ImGui::PushFont(g_WeaponsIconFont);
	const ImVec2 ts = ImGui::CalcTextSize(glyph);
	const float x = std::floor(cx - ts.x * 0.5f);
	const float y = std::floor(cyTop);
	for (int ox = -1; ox <= 1; ++ox)
		for (int oy = -1; oy <= 1; ++oy)
			if (ox || oy)
				dl->AddText(ImVec2(x + (float)ox, y + (float)oy), IM_COL32(0, 0, 0, 220), glyph);
	dl->AddText(ImVec2(x, y), col, glyph);
	ImGui::PopFont();
	if (outBottomY)
		*outBottomY = y + ts.y;
}

/** Yangin sayisina gore halka segmenti (cok inferno = daha az nokta). */
inline int InfernoHullRingCount(int nFires) noexcept {
	if (nFires > 40)
		return 4;
	if (nFires > 24)
		return 6;
	if (nFires > 12)
		return 8;
	return 10;
}

/** Ates konumlari degisince bir kez: dunya uzayinda halka noktalari (oyun okumasi yok). */
inline void BuildInfernoWorldRingPoints(const Vector3* firePos, int nFires, float fireRadiusWorld,
	std::vector<Vector3>& outWorld) {
	outWorld.clear();
	if (nFires <= 0)
		return;
	const int kRing = InfernoHullRingCount(nFires);
	const float twoPi = 6.2831853f;
	outWorld.reserve(static_cast<size_t>(nFires) * static_cast<size_t>(kRing));
	for (int fi = 0; fi < nFires; ++fi) {
		const Vector3& p = firePos[fi];
		for (int i = 0; i < kRing; ++i) {
			const float a = (static_cast<float>(i) / static_cast<float>(kRing)) * twoPi;
			Vector3 w{};
			w.x = p.x + std::cosf(a) * fireRadiusWorld;
			w.y = p.y + std::sinf(a) * fireRadiusWorld;
			w.z = p.z;
			outWorld.push_back(w);
		}
	}
}

inline bool ConvexHull2DMonotoneChain(std::vector<ImVec2>& pts, std::vector<ImVec2>& outHull) {
	thread_local std::vector<ImVec2> tls_lower;
	thread_local std::vector<ImVec2> tls_upper;
	auto& lower = tls_lower;
	auto& upper = tls_upper;
	lower.clear();
	upper.clear();
	if (pts.size() < 3u)
		return false;
	std::sort(pts.begin(), pts.end(), [](const ImVec2& a, const ImVec2& b) {
		return a.x < b.x || (a.x == b.x && a.y < b.y);
	});
	for (const ImVec2& p : pts) {
		while (lower.size() >= 2) {
			const ImVec2& p1 = lower[lower.size() - 2];
			const ImVec2& p2 = lower[lower.size() - 1];
			if ((p2.x - p1.x) * (p.y - p1.y) - (p2.y - p1.y) * (p.x - p1.x) > 0.f)
				break;
			lower.pop_back();
		}
		lower.push_back(p);
	}
	for (auto it = pts.rbegin(); it != pts.rend(); ++it) {
		const ImVec2& p = *it;
		while (upper.size() >= 2) {
			const ImVec2& p1 = upper[upper.size() - 2];
			const ImVec2& p2 = upper[upper.size() - 1];
			if ((p2.x - p1.x) * (p.y - p1.y) - (p2.y - p1.y) * (p.x - p1.x) > 0.f)
				break;
			upper.pop_back();
		}
		upper.push_back(p);
	}
	if (lower.size() < 2 || upper.size() < 2)
		return false;
	lower.pop_back();
	upper.pop_back();
	lower.insert(lower.end(), upper.begin(), upper.end());
	if (lower.size() < 3u)
		return false;
	outHull.swap(lower);
	return true;
}

/** Kamera her kare: yalnizca w2s + convex hull (ates okumasi yok). */
inline bool ProjectInfernoScreenHull(const view_matrix_t& vm, const std::vector<Vector3>& worldPts,
	std::vector<ImVec2>& outHull) {
	thread_local std::vector<ImVec2> tls_pts;
	tls_pts.clear();
	tls_pts.reserve(worldPts.size());
	for (const Vector3& w : worldPts) {
		Vector3 sp{};
		if (!w2s(w, sp, vm) || sp.z < 0.01f)
			continue;
		tls_pts.push_back(ImVec2(sp.x, sp.y));
	}
	return ConvexHull2DMonotoneChain(tls_pts, outHull);
}

/** Tek seferlik yol (fallback / eski cagri). */
inline bool BuildInfernoScreenHull(const view_matrix_t& vm, const Vector3* firePos, int nFires,
	float fireRadiusWorld, std::vector<ImVec2>& outHull) {
	thread_local std::vector<Vector3> tls_world;
	BuildInfernoWorldRingPoints(firePos, nFires, fireRadiusWorld, tls_world);
	return ProjectInfernoScreenHull(vm, tls_world, outHull);
}

inline void DrawInfernoHullOverlay(ImDrawList* dl, const std::vector<ImVec2>& hull, ImU32 fillCol, ImU32 lineCol) {
	if (hull.size() < 3u)
		return;
	dl->AddConvexPolyFilled(hull.data(), static_cast<int>(hull.size()), fillCol);
	dl->AddPolyline(hull.data(), static_cast<int>(hull.size()), lineCol, ImDrawFlags_Closed, 2.f);
}

/** Catalyst collector::collect_projectiles molotov_fire — yalnizca yanmakta olan slotlar. */
inline bool CollectInfernoFiresCatalyst(uintptr_t ent, std::vector<Vector3>& fires, float& outExpireTime,
	float curTime = 0.f, InfernoTimerState* timerOut = nullptr) {
	fires.clear();
	outExpireTime = 0.f;
	if (!ent || !offsets::inferno_m_fireCount || !offsets::inferno_m_firePositions ||
	    !offsets::inferno_m_bFireIsBurning)
		return false;
	const int fc = g_GameMem.readv<int>(ent + static_cast<uintptr_t>(offsets::inferno_m_fireCount));
	if (fc <= 0)
		return false;
	const uintptr_t firePosBase = ent + static_cast<uintptr_t>(offsets::inferno_m_firePositions);
	const uintptr_t fireBurnBase = ent + static_cast<uintptr_t>(offsets::inferno_m_bFireIsBurning);
	const int n = (fc < 64) ? fc : 64;
	fires.reserve(static_cast<size_t>(n));
	for (int i = 0; i < n; ++i) {
		if (!g_GameMem.readv<bool>(fireBurnBase + static_cast<uintptr_t>(i)))
			continue;
		const Vector3 p = g_GameMem.readv<Vector3>(
			firePosBase + static_cast<uintptr_t>(i) * sizeof(Vector3));
		if (!IsPlausibleInfernoFirePosition(p))
			continue;
		fires.push_back(p);
	}
	if (fires.empty())
		return false;
	InfernoTimerState ts;
	if (!ComputeInfernoTimer(ent, curTime, ts))
		return false;
	outExpireTime = ts.expireTime;
	if (timerOut)
		*timerOut = ts;
	return true;
}

/** Convex hull; basarisiz olursa catalyst gibi cizilmez (fallback halka yok). */
inline void DrawInfernoHullOrFallback(ImDrawList* dl, const view_matrix_t& vm, const std::vector<Vector3>& fires,
	ImU32 hullFill, ImU32 hullLine, ImU32 /*fallbackRing*/) {
	if (fires.empty() || !Settings::Visuals::worldInfernoHull)
		return;
	thread_local std::vector<ImVec2> tls_hull;
	tls_hull.clear();
	constexpr float kFireRadiusWorld = 60.f;
	if (!BuildInfernoScreenHull(vm, fires.data(), static_cast<int>(fires.size()), kFireRadiusWorld, tls_hull))
		return;
	DrawInfernoHullOverlay(dl, tls_hull, hullFill, hullLine);
}

namespace grenade_esp_detail {
inline bool GrenadeEspCullSkip(const Vector3& worldPos);
}

inline void DrawInfernoFireEsp(ImDrawList* dl, const view_matrix_t& vm, uintptr_t ent,
	const std::vector<Vector3>& fires, float curTime, float /*expireTime*/) {
	if (fires.empty())
		return;

	InfernoTimerState ts;
	if (!ComputeInfernoTimer(ent, curTime, ts))
		return;

	const bool drawHull = Settings::Visuals::worldInfernoHull;
	const bool drawLabels = Settings::Visuals::worldGrenades;

	if (drawHull)
		DrawInfernoHullOrFallback(dl, vm, fires,
			IM_COL32(230, 120, 60, 50), IM_COL32(230, 120, 60, 150), IM_COL32(230, 100, 50, 220));

	if (!drawLabels)
		return;

	Vector3 center{};
	for (const Vector3& f : fires) {
		center.x += f.x;
		center.y += f.y;
		center.z += f.z;
	}
	const float inv = 1.f / static_cast<float>(fires.size());
	center.x *= inv;
	center.y *= inv;
	center.z *= inv;
	/** Merkez yerine: yayilmada bir yangin noktasi menzildeyse inferno etiketi cizilir. */
	{
		bool anyInRange = false;
		for (const Vector3& f : fires) {
			if (!grenade_esp_detail::GrenadeEspCullSkip(f)) {
				anyInRange = true;
				break;
			}
		}
		if (!anyInRange)
			return;
	}

	Vector3 isp{};
	if (!w2s(center, isp, vm) || isp.z < 0.01f)
		return;

	const float remaining = ts.remaining;
	const float frac = ts.frac;

	const ImU32 mcol = IM_COL32(230, 140, 90, 255);
	const ImU32 hiTimer = IM_COL32(230, 156, 110, 255);
	const ImU32 loTimer = IM_COL32(222, 59, 59, 255);
	const ImU32 barBg = IM_COL32(20, 22, 28, 230);

	float yOff = 0.f;
	if (Settings::Visuals::worldGrenadeIcons && g_WeaponsIconFont) {
		DrawWeaponGlyphCenterOutlined(dl, isp.x, isp.y, "l", mcol);
		ImGui::PushFont(g_WeaponsIconFont);
		yOff += ImGui::CalcTextSize("l").y - 5.5f;
		ImGui::PopFont();
	}

	StrokeTextBg(dl, "fire", isp.x, isp.y + yOff, mcol);
	{
		const ImVec2 fs = ImGui::CalcTextSize("fire");
		yOff += fs.y - 5.5f;
	}
	DrawCatalystProjTimerBar(dl, isp.x, isp.y + yOff + 6.f, frac, hiTimer, loTimer, barBg);
}

namespace grenade_esp_detail {

/** Yerel oyuncudan daha uzaktaki nade ESP cizilmez (Source birimleri ~ inch). */
constexpr float kWorldGrenadeEspMaxDist = 26000.f;

/** Entity listesi taramasi: seyrek; canli veri cache'ten her kare yenilenir. */
constexpr unsigned kScanEveryNFrames = 4u;
inline unsigned WorldGrenadeScanPeriodFrames() {
	if (Settings::Visuals::worldGrenades || Settings::Visuals::worldInfernoHull)
		return 2u;
	const int userPeriod = Settings::render_opt::world_grenade_scan_period_frames;
	if (userPeriod >= 1 && userPeriod <= 32)
		return static_cast<unsigned>(userPeriod);
	if (!Settings::misc::save_fps)
		return 2u;
	const int combo = ConcurrentHeavyWorldKernelScanFeatures();
	if (combo >= 3)
		return 6u;
	if (combo >= 2)
		return 4u;
	return kScanEveryNFrames;
}
inline unsigned g_scanCounter = 0;
inline std::mutex g_worldGrenadeCacheMutex;
inline bool g_forceWorldGrenadeRescan = true;
inline bool g_prevWorldGrenadeKernelActive = false;

inline Vector3 g_spatialCullOrigin{};
inline float g_spatialCullDistSq = 0.f;

inline void SetupGrenadeEspSpatialCull(uintptr_t localPawn) {
	g_spatialCullDistSq = 0.f;
	if (!localPawn)
		return;
	g_spatialCullOrigin = ReadWorldPositionFromEntity(localPawn);
	const float d = kWorldGrenadeEspMaxDist;
	g_spatialCullDistSq = d * d;
}

/** true = bu dunya noktasini cizmeyi atla (mesafe buyuk). */
inline bool GrenadeEspCullSkip(const Vector3& worldPos) {
	if (g_spatialCullDistSq <= 0.f)
		return false;
	const float dx = worldPos.x - g_spatialCullOrigin.x;
	const float dy = worldPos.y - g_spatialCullOrigin.y;
	const float dz = worldPos.z - g_spatialCullOrigin.z;
	return dx * dx + dy * dy + dz * dz > g_spatialCullDistSq;
}

inline uint32_t HashViewMatrixCoarse(const view_matrix_t& vm) {
	auto q = [](float f) -> uint32_t {
		return static_cast<uint32_t>(std::lround(f * 256.f)) & 0xFFFFu;
	};
	uint32_t h = 2166136261u;
	for (int r = 0; r < 4; ++r) {
		for (int c = 0; c < 4; ++c) {
			h ^= q(vm.matrix[r][c]);
			h *= 16777619u;
		}
	}
	return h;
}

inline uint32_t HashInfernoFires(const std::vector<Vector3>& fires) {
	uint32_t h = static_cast<uint32_t>(fires.size());
	for (const Vector3& p : fires) {
		h = h * 31u + static_cast<uint32_t>(std::lround(p.x * 0.25f));
		h = h * 31u + static_cast<uint32_t>(std::lround(p.y * 0.25f));
		h = h * 31u + static_cast<uint32_t>(std::lround(p.z * 0.25f));
	}
	return h | 1u;
}

/** Periyodik entity taramasi ciktisi (pointer listesi). */
inline std::vector<uintptr_t> g_discovered_infernos;
inline std::vector<uintptr_t> g_cachedSmokes;
inline std::vector<uintptr_t> g_cachedMollyAirs;
inline std::vector<uintptr_t> g_cachedDecoys;
inline std::vector<uintptr_t> g_cachedHe;
inline std::vector<uintptr_t> g_cachedFlash;

/** Overlay cizim cache — yalnizca cache'lenmis entity'ler uzerinde IOCTL. */
struct InfernoDrawEntry {
	uintptr_t ent = 0;
	std::vector<Vector3> fires;
	InfernoTimerState timer{};
	std::vector<Vector3> hull_world_pts;
	std::vector<ImVec2> hull;
	Vector3 center{};
	Vector3 label_sp{};
	bool label_on_screen = false;
	uint32_t hull_fires_hash = 0;
	uint32_t hull_world_fires_hash = 0;
	uint32_t label_vm_hash = 0;
	double keepUntil = 0.0;
};

struct ProjectileDrawEntry {
	uintptr_t ent = 0;
	WorldProjectileKind kind = WorldProjectileKind::None;
	Vector3 world{};
	Vector3 sp{};
	bool on_screen = false;
	float timer_frac = 1.f;
	bool show_timer = false;
	float decoy_remaining = 0.f;
	uint32_t sp_vm_hash = 0;
	double keepUntil = 0.0;
};

inline std::vector<InfernoDrawEntry> g_inferno_draw;
inline std::vector<ProjectileDrawEntry> g_projectile_draw;

inline void ClearInfernoCacheOnly() {
	g_discovered_infernos.clear();
}

inline void ClearProjectileListsForRescan() {
	g_cachedSmokes.clear();
	g_cachedMollyAirs.clear();
	g_cachedDecoys.clear();
	g_cachedHe.clear();
	g_cachedFlash.clear();
}

inline void ClearWorldGrenadeCache() {
	g_discovered_infernos.clear();
	g_cachedSmokes.clear();
	g_cachedMollyAirs.clear();
	g_cachedDecoys.clear();
	g_cachedHe.clear();
	g_cachedFlash.clear();
	g_inferno_draw.clear();
	g_projectile_draw.clear();
}

inline bool QuickInfernoSlotCountOk(uintptr_t ent) {
	if (!ent || !offsets::inferno_m_fireCount)
		return false;
	const int fc = g_GameMem.readv<int>(ent + static_cast<uintptr_t>(offsets::inferno_m_fireCount));
	return fc > 0 && fc <= 64;
}

inline bool TryPushInfernoWorldCache(uintptr_t ent, float curTime) {
	if (!EntityLooksLikeInferno(ent, curTime))
		return false;
	g_discovered_infernos.push_back(ent);
	return true;
}

inline void ReconcileInfernoDrawCache() {
	std::vector<InfernoDrawEntry> next;
	next.reserve(g_discovered_infernos.size());
	for (uintptr_t ent : g_discovered_infernos) {
		if (!ent)
			continue;
		InfernoDrawEntry entry{};
		entry.ent = ent;
		for (const InfernoDrawEntry& old : g_inferno_draw) {
			if (old.ent != ent)
				continue;
			entry.fires = old.fires;
			entry.timer = old.timer;
			entry.center = old.center;
			entry.hull = old.hull;
			entry.hull_world_pts = old.hull_world_pts;
			entry.hull_fires_hash = old.hull_fires_hash;
			entry.hull_world_fires_hash = old.hull_world_fires_hash;
			entry.label_sp = old.label_sp;
			entry.label_on_screen = old.label_on_screen;
			entry.label_vm_hash = old.label_vm_hash;
			entry.keepUntil = old.keepUntil;
			break;
		}
		next.push_back(std::move(entry));
	}
	g_inferno_draw = std::move(next);
}

inline void ReconcileProjectileDrawCache() {
	std::vector<ProjectileDrawEntry> next;
	next.reserve(g_cachedSmokes.size() + g_cachedMollyAirs.size() + g_cachedDecoys.size() +
	             g_cachedHe.size() + g_cachedFlash.size());
	auto pushUnique = [&](uintptr_t ent, WorldProjectileKind kind) {
		if (!ent)
			return;
		for (const ProjectileDrawEntry& e : next) {
			if (e.ent == ent)
				return;
		}
		ProjectileDrawEntry e{};
		e.ent = ent;
		e.kind = kind;
		for (const ProjectileDrawEntry& old : g_projectile_draw) {
			if (old.ent != ent)
				continue;
			e.world = old.world;
			e.sp = old.sp;
			e.on_screen = old.on_screen;
			e.timer_frac = old.timer_frac;
			e.show_timer = old.show_timer;
			e.decoy_remaining = old.decoy_remaining;
			e.sp_vm_hash = old.sp_vm_hash;
			e.keepUntil = old.keepUntil;
			break;
		}
		next.push_back(std::move(e));
	};
	for (uintptr_t e : g_cachedSmokes)
		pushUnique(e, WorldProjectileKind::Smoke);
	for (uintptr_t e : g_cachedMollyAirs)
		pushUnique(e, WorldProjectileKind::MolotovAir);
	for (uintptr_t e : g_cachedDecoys)
		pushUnique(e, WorldProjectileKind::Decoy);
	for (uintptr_t e : g_cachedHe)
		pushUnique(e, WorldProjectileKind::He);
	for (uintptr_t e : g_cachedFlash)
		pushUnique(e, WorldProjectileKind::Flash);
	g_projectile_draw = std::move(next);
}

inline bool RefreshInfernoDrawEntry(InfernoDrawEntry& e, float curTime) {
	const double now = ImGui::GetTime();
	float expireDummy = 0.f;
	if (!CollectInfernoFiresCatalyst(e.ent, e.fires, expireDummy, curTime, &e.timer)) {
		if (!e.fires.empty() && e.keepUntil >= now)
			return true;
		return false;
	}
	e.keepUntil = now + 0.5;
	e.center = Vector3{};
	for (const Vector3& f : e.fires) {
		e.center.x += f.x;
		e.center.y += f.y;
		e.center.z += f.z;
	}
	const float inv = 1.f / static_cast<float>(e.fires.size());
	e.center.x *= inv;
	e.center.y *= inv;
	e.center.z *= inv;
	const uint32_t fh = HashInfernoFires(e.fires);
	if (fh != e.hull_fires_hash) {
		e.hull_fires_hash = fh;
		e.hull_world_fires_hash = 0;
		e.hull_world_pts.clear();
		e.hull.clear();
	}
	return true;
}

inline void EnsureInfernoScreenCache(InfernoDrawEntry& e, const view_matrix_t& vm, uint32_t vm_hash, bool drawHull) {
	if (drawHull && !e.fires.empty() && !GrenadeEspCullSkip(e.center)) {
		if (e.hull_world_fires_hash != e.hull_fires_hash) {
			constexpr float kFireRadiusWorld = 60.f;
			BuildInfernoWorldRingPoints(e.fires.data(), static_cast<int>(e.fires.size()), kFireRadiusWorld, e.hull_world_pts);
			e.hull_world_fires_hash = e.hull_fires_hash;
		}
		if (!e.hull_world_pts.empty())
			(void)ProjectInfernoScreenHull(vm, e.hull_world_pts, e.hull);
	}
	if (e.label_vm_hash != vm_hash) {
		e.label_on_screen = w2s(e.center, e.label_sp, vm) && e.label_sp.z >= 0.01f;
		e.label_vm_hash = vm_hash;
	}
}

inline bool RefreshProjectileDrawEntry(ProjectileDrawEntry& e, float curTime, uint32_t localPawnH, bool localOnly) {
	const double now = ImGui::GetTime();
	if (!e.ent) {
		if (e.keepUntil >= now && e.on_screen)
			return true;
		return false;
	}
	if (e.kind == WorldProjectileKind::Smoke && localOnly && localPawnH && offsets::grenade_m_hThrower) {
		const uint32_t th = g_GameMem.readv<uint32_t>(e.ent + static_cast<uintptr_t>(offsets::grenade_m_hThrower));
		if (th != localPawnH)
			return false;
	}
	if (e.kind == WorldProjectileKind::He || e.kind == WorldProjectileKind::Flash) {
		if (offsets::proj_m_nExplodeEffectTickBegin) {
			const int detTick =
			    g_GameMem.readv<int>(e.ent + static_cast<uintptr_t>(offsets::proj_m_nExplodeEffectTickBegin));
			if (detTick > 0)
				return false;
		}
	}
	e.world = ReadWorldPositionFromEntity(e.ent);
	if (e.world.x * e.world.x + e.world.y * e.world.y + e.world.z * e.world.z < 1.f) {
		if (e.keepUntil >= now && e.on_screen)
			return true;
		return false;
	}
	e.keepUntil = now + 0.45;

	e.show_timer = false;
	e.timer_frac = 1.f;
	e.decoy_remaining = 0.f;

	switch (e.kind) {
	case WorldProjectileKind::Smoke:
		if (offsets::smoke_m_bDidSmokeEffect && offsets::smoke_m_nSmokeEffectTickBegin && curTime > 1.f) {
			const bool active =
			    g_GameMem.readv<bool>(e.ent + static_cast<uintptr_t>(offsets::smoke_m_bDidSmokeEffect));
			if (active) {
				const int tickBegin =
				    g_GameMem.readv<int>(e.ent + static_cast<uintptr_t>(offsets::smoke_m_nSmokeEffectTickBegin));
				constexpr float kSmokeDur = 18.f;
				const float smoke_start = static_cast<float>(tickBegin) * (1.f / 64.f);
				const float remaining = (std::max)(0.f, kSmokeDur - (curTime - smoke_start));
				e.timer_frac = std::clamp(remaining / kSmokeDur, 0.f, 1.f);
				e.show_timer = true;
			}
		}
		break;
	case WorldProjectileKind::Decoy:
		if (offsets::decoy_m_nDecoyShotTick && curTime > 1.f) {
			const int shotTick = g_GameMem.readv<int>(e.ent + static_cast<uintptr_t>(offsets::decoy_m_nDecoyShotTick));
			if (shotTick > 0) {
				const float t0 = static_cast<float>(shotTick) * (1.f / 64.f);
				constexpr float fuse = 15.f;
				e.decoy_remaining = (std::max)(0.f, fuse - (curTime - t0));
				e.timer_frac = std::clamp(e.decoy_remaining / fuse, 0.f, 1.f);
				e.show_timer = true;
			}
		}
		break;
	default:
		break;
	}
	return true;
}

inline void EnsureProjectileScreenCache(ProjectileDrawEntry& e, const view_matrix_t& vm, uint32_t vm_hash) {
	if (e.sp_vm_hash != vm_hash) {
		e.on_screen = w2s(e.world, e.sp, vm) && e.sp.z >= 0.01f;
		e.sp_vm_hash = vm_hash;
	}
}

inline void DrawInfernoFromCache(ImDrawList* dl, const InfernoDrawEntry& e, bool drawHull, bool drawLabels) {
	if (drawHull && e.hull.size() >= 3u)
		DrawInfernoHullOverlay(dl, e.hull, IM_COL32(230, 120, 60, 50), IM_COL32(230, 120, 60, 150));

	if (!drawLabels)
		return;

	bool anyInRange = false;
	for (const Vector3& f : e.fires) {
		if (!GrenadeEspCullSkip(f)) {
			anyInRange = true;
			break;
		}
	}
	if (!anyInRange || !e.label_on_screen)
		return;

	const float sx = e.label_sp.x;
	const float sy = e.label_sp.y;
	const float remaining = e.timer.remaining;
	const float frac = e.timer.frac;
	const ImU32 mcol = IM_COL32(230, 140, 90, 255);
	const ImU32 hiTimer = IM_COL32(230, 156, 110, 255);
	const ImU32 loTimer = IM_COL32(222, 59, 59, 255);
	const ImU32 barBg = IM_COL32(20, 22, 28, 230);

	float yOff = 0.f;
	if (Settings::Visuals::worldGrenadeIcons && g_WeaponsIconFont) {
		DrawWeaponGlyphCenterOutlined(dl, sx, sy, "l", mcol);
		ImGui::PushFont(g_WeaponsIconFont);
		yOff += ImGui::CalcTextSize("l").y - 5.5f;
		ImGui::PopFont();
	}
	StrokeTextBg(dl, "fire", sx, sy + yOff, mcol);
	{
		const ImVec2 fs = ImGui::CalcTextSize("fire");
		yOff += fs.y - 5.5f;
	}
	(void)remaining;
	DrawCatalystProjTimerBar(dl, sx, sy + yOff + 6.f, frac, hiTimer, loTimer, barBg);
}

inline void DrawProjectileFromCache(ImDrawList* dl, const ProjectileDrawEntry& e, float /*curTime*/) {
	const float sx = e.sp.x;
	const float sy = e.sp.y;

	switch (e.kind) {
	case WorldProjectileKind::Smoke: {
		const ImU32 col = IM_COL32(160, 210, 190, 255);
		const ImU32 hiTimer = IM_COL32(98, 217, 109, 255);
		const ImU32 loTimer = IM_COL32(222, 59, 59, 255);
		const ImU32 barBg = IM_COL32(20, 22, 28, 230);
		float yOff = 0.f;
		if (Settings::Visuals::worldGrenadeIcons && g_WeaponsIconFont) {
			DrawWeaponGlyphCenterOutlined(dl, sx, sy, "k", col);
			ImGui::PushFont(g_WeaponsIconFont);
			yOff += ImGui::CalcTextSize("k").y - 5.5f;
			ImGui::PopFont();
		}
		StrokeTextBg(dl, "smoke", sx, sy + yOff, col);
		{
			const ImVec2 ns = ImGui::CalcTextSize("smoke");
			yOff += ns.y - 5.5f;
		}
		if (e.show_timer)
			DrawCatalystProjTimerBar(dl, sx, sy + yOff + 6.f, e.timer_frac, hiTimer, loTimer, barBg);
		break;
	}
	case WorldProjectileKind::MolotovAir: {
		const ImU32 mc = IM_COL32(230, 150, 80, 255);
		float y0 = sy;
		if (Settings::Visuals::worldGrenadeIcons && g_WeaponsIconFont) {
			float gbot = y0;
			DrawWeaponGlyphOutlined(dl, sx, y0, "l", mc, &gbot);
			y0 = gbot + 2.f;
		}
		StrokeTextBg(dl, "molotov (air)", sx, y0, mc);
		break;
	}
	case WorldProjectileKind::Decoy: {
		const ImU32 dc = IM_COL32(180, 185, 210, 255);
		if (e.show_timer) {
			char buf[48]{};
			std::snprintf(buf, sizeof buf, "decoy  %.1fs", e.decoy_remaining);
			float y0 = sy;
			if (Settings::Visuals::worldGrenadeIcons && g_WeaponsIconFont) {
				float gbot = y0;
				DrawWeaponGlyphOutlined(dl, sx, y0, "m", dc, &gbot);
				y0 = gbot + 2.f;
			}
			StrokeTextBg(dl, buf, sx, y0, dc);
			const ImVec2 bsz = ImGui::CalcTextSize(buf);
			DrawProjTimerBar(dl, sx, y0 + bsz.y + 4.f, e.timer_frac, IM_COL32(160, 170, 220, 255),
			                 IM_COL32(20, 22, 28, 200));
		} else {
			float y0 = sy;
			if (Settings::Visuals::worldGrenadeIcons && g_WeaponsIconFont) {
				float gbot = y0;
				DrawWeaponGlyphOutlined(dl, sx, y0, "m", dc, &gbot);
				y0 = gbot + 2.f;
			}
			StrokeTextBg(dl, "decoy", sx, y0, dc);
		}
		break;
	}
	case WorldProjectileKind::He:
	case WorldProjectileKind::Flash: {
		const bool isHe = e.kind == WorldProjectileKind::He;
		const char* lab = isHe ? "HE" : "flash";
		const char* glyph = isHe ? "j" : "i";
		const ImU32 gc = IM_COL32(240, 200, 120, 255);
		float y0 = sy;
		if (Settings::Visuals::worldGrenadeIcons && g_WeaponsIconFont) {
			float gbot = y0;
			DrawWeaponGlyphOutlined(dl, sx, y0, glyph, gc, &gbot);
			y0 = gbot + 2.f;
		}
		StrokeTextBg(dl, lab, sx, y0, gc);
		break;
	}
	default:
		break;
	}
}

/** Molotov hull tek basina: sinif adi okumadan sadece inferno offset imzasi. */
inline void ScanInfernosOnlyFast(uintptr_t entity_list, float curTime) {
	ClearInfernoCacheOnly();
	if (!entity_list || !offsets::inferno_m_fireCount)
		return;
	const int i_max = CatalystWorldProjectileScanMax(entity_list);
	/** BATCH IOCTL: blok pointer tablolari tek IOCTL ile (eskiden per-entity). */
	world_scan::EnumerateLiveEntities(entity_list, i_max, [&](int /*idx*/, uintptr_t ent) {
		if (!QuickInfernoSlotCountOk(ent))
			return;
		TryPushInfernoWorldCache(ent, curTime);
	});
}

/** Catalyst collector::collect_projectiles — entity list taramasi (batch IOCTL + cache). */
inline void ScanWorldProjectilesCatalyst(uintptr_t entity_list, bool wantInferno, bool wantProjectiles,
	uint32_t localPawnH, bool localOnly, float curTime) {
	if (wantInferno && !wantProjectiles) {
		ScanInfernosOnlyFast(entity_list, curTime);
		return;
	}
	if (wantProjectiles)
		ClearProjectileListsForRescan();
	if (wantInferno)
		ClearInfernoCacheOnly();
	if (!wantProjectiles && !wantInferno)
		return;
	if (!entity_list)
		return;
	const int i_max = CatalystWorldProjectileScanMax(entity_list);

	{
		std::lock_guard<std::mutex> ck(world_scan::g_classify_mtx);
		world_scan::g_classify_cache.NextTick();
	}

	/** BATCH IOCTL: 512-blok pointer tablolari tek pass. */
	world_scan::EnumerateLiveEntities(entity_list, i_max, [&](int /*idx*/, uintptr_t ent) {
		/** FAST PATH: cache "ilgisiz" → identity IOCTL atla. */
		{
			std::lock_guard<std::mutex> ck(world_scan::g_classify_mtx);
			if (world_scan::g_classify_cache.PeekAndTouchIrrelevant(ent))
				return;
		}

		const uintptr_t identProbe = g_GameMem.readv<uintptr_t>(ent + 0x10);
		if (!identProbe || identProbe < 0x10000u)
			return;

		uint16_t def = 0;
		world_scan::Kind kind;
		{
			std::lock_guard<std::mutex> ck(world_scan::g_classify_mtx);
			kind = ClassifyEntityCached(ent, identProbe, def, curTime);
		}

		switch (kind) {
		case world_scan::Kind::Inferno:
			if (wantInferno)
				TryPushInfernoWorldCache(ent, curTime);
			break;
		case world_scan::Kind::ProjSmoke:
			if (!wantProjectiles)
				break;
			if (localOnly && localPawnH && offsets::grenade_m_hThrower) {
				const uint32_t th = g_GameMem.readv<uint32_t>(ent + static_cast<uintptr_t>(offsets::grenade_m_hThrower));
				if (th != localPawnH)
					break;
			}
			g_cachedSmokes.push_back(ent);
			break;
		case world_scan::Kind::ProjMolotovAir:
			if (wantProjectiles) g_cachedMollyAirs.push_back(ent);
			break;
		case world_scan::Kind::ProjDecoy:
			if (wantProjectiles) g_cachedDecoys.push_back(ent);
			break;
		case world_scan::Kind::ProjHe:
			if (wantProjectiles) g_cachedHe.push_back(ent);
			break;
		case world_scan::Kind::ProjFlash:
			if (wantProjectiles) g_cachedFlash.push_back(ent);
			break;
		default:
			break;
		}
	});
}

/** Kurulu / yerdeki C4 + bombaci + dusmus silah: IOCTL tarama (seyrek), cizim her kare cache. */
struct CachedPlantedBombWorld {
	bool show = false;
	Vector3 world{};
	double keepUntil = 0.0;
};
struct CachedGroundC4World {
	Vector3 world{};
	double keepUntil = 0.0;
};
inline CachedPlantedBombWorld g_cachedPlantedBomb{};
inline std::vector<CachedGroundC4World> g_cachedGroundC4;
inline std::unordered_set<uintptr_t> g_cachedC4CarrierEntities;
inline unsigned g_pickupScanCounter = 0;
inline std::mutex g_pickupCacheMutex;

inline unsigned WorldPickupEspScanPeriodFrames() noexcept {
	if (!Settings::misc::save_fps)
		return 2u;
	const int combo = ConcurrentHeavyWorldKernelScanFeatures();
	if (combo >= 2)
		return 6u;
	return 3u;
}

inline void MergeGroundC4CacheEntry(const Vector3& wp, double now) {
	constexpr float kMergeDistSq = 40.f * 40.f;
	constexpr double kKeepSec = 0.55;
	for (auto& e : g_cachedGroundC4) {
		const float dx = e.world.x - wp.x;
		const float dy = e.world.y - wp.y;
		const float dz = e.world.z - wp.z;
		if (dx * dx + dy * dy + dz * dz <= kMergeDistSq) {
			e.world = wp;
			e.keepUntil = now + kKeepSec;
			return;
		}
	}
	CachedGroundC4World entry{};
	entry.world = wp;
	entry.keepUntil = now + kKeepSec;
	g_cachedGroundC4.push_back(entry);
}

inline void PruneGroundC4Cache(double now) {
	g_cachedGroundC4.erase(
	    std::remove_if(g_cachedGroundC4.begin(), g_cachedGroundC4.end(),
	        [now](const CachedGroundC4World& e) { return e.keepUntil < now; }),
	    g_cachedGroundC4.end());
}

inline void RefreshPlantedBombWorldCache() {
	const double now = ImGui::GetTime();
	if (!client || !offsets::dwPlantedC4) {
		if (g_cachedPlantedBomb.keepUntil < now)
			g_cachedPlantedBomb = {};
		return;
	}
	const uintptr_t plantedAddr = client + static_cast<uintptr_t>(offsets::dwPlantedC4);
	const bool plantedFlag = g_GameMem.readv<bool>(plantedAddr - 8);
	if (!plantedFlag) {
		if (g_cachedPlantedBomb.keepUntil < now)
			g_cachedPlantedBomb.show = false;
		return;
	}
	uintptr_t p = g_GameMem.readv<uintptr_t>(plantedAddr);
	uintptr_t bomb = 0;
	if (p) {
		const uintptr_t q = g_GameMem.readv<uintptr_t>(p);
		bomb = q ? q : p;
	}
	if (!bomb)
		return;
	if (offsets::c4_m_bBombDefused &&
	    g_GameMem.readv<bool>(bomb + static_cast<uintptr_t>(offsets::c4_m_bBombDefused)))
		return;
	g_cachedPlantedBomb.show = true;
	g_cachedPlantedBomb.world = ReadWorldPositionFromEntity(bomb);
	g_cachedPlantedBomb.keepUntil = now + 0.45;
}

inline int UnifiedPickupEntityScanMax(uintptr_t entity_list, bool need_dropped_scan) {
	if (need_dropped_scan)
		return ClampedDroppedWeaponScanMax(entity_list);
	return ClampedPickupEntityScanMax(entity_list);
}

} // namespace grenade_esp_detail

inline void DrawSmokeProjectileWorldEsp(ImDrawList* dl, const view_matrix_t& vm, uintptr_t ent,
	float curTime, uint32_t localPawnH, bool localOnly) {
	if (localOnly && localPawnH && offsets::grenade_m_hThrower) {
		const uint32_t th = g_GameMem.readv<uint32_t>(ent + static_cast<uintptr_t>(offsets::grenade_m_hThrower));
		if (th != localPawnH)
			return;
	}
	const Vector3 worldPos = ReadWorldPositionFromEntity(ent);
	if (worldPos.x * worldPos.x + worldPos.y * worldPos.y + worldPos.z * worldPos.z < 1.f)
		return;
	if (grenade_esp_detail::GrenadeEspCullSkip(worldPos))
		return;
	Vector3 sp{};
	if (!w2s(worldPos, sp, vm) || sp.z < 0.01f)
		return;
	const float sx = sp.x;
	const float sy = sp.y;
	const ImU32 col = IM_COL32(160, 210, 190, 255);
	const ImU32 hiTimer = IM_COL32(98, 217, 109, 255);
	const ImU32 loTimer = IM_COL32(222, 59, 59, 255);
	const ImU32 barBg = IM_COL32(20, 22, 28, 230);

	float frac = 1.f;
	bool smokeActive = false;
	if (offsets::smoke_m_bDidSmokeEffect && offsets::smoke_m_nSmokeEffectTickBegin && curTime > 1.f) {
		const bool active = g_GameMem.readv<bool>(ent + static_cast<uintptr_t>(offsets::smoke_m_bDidSmokeEffect));
		if (active) {
			const int tickBegin = g_GameMem.readv<int>(ent + static_cast<uintptr_t>(offsets::smoke_m_nSmokeEffectTickBegin));
			constexpr float kSmokeDur = 18.f;
			const float smoke_start = static_cast<float>(tickBegin) * (1.f / 64.f);
			const float remaining = (std::max)(0.f, kSmokeDur - (curTime - smoke_start));
			frac = std::clamp(remaining / kSmokeDur, 0.f, 1.f);
			smokeActive = true;
		}
	}

	float yOff = 0.f;
	if (Settings::Visuals::worldGrenadeIcons && g_WeaponsIconFont) {
		DrawWeaponGlyphCenterOutlined(dl, sx, sy, "k", col);
		ImGui::PushFont(g_WeaponsIconFont);
		yOff += ImGui::CalcTextSize("k").y - 5.5f;
		ImGui::PopFont();
	}

	StrokeTextBg(dl, "smoke", sx, sy + yOff, col);
	{
		const ImVec2 ns = ImGui::CalcTextSize("smoke");
		yOff += ns.y - 5.5f;
	}

	if (smokeActive)
		DrawCatalystProjTimerBar(dl, sx, sy + yOff + 6.f, frac, hiTimer, loTimer, barBg);
}

inline void DrawMollyAirWorldEsp(ImDrawList* dl, const view_matrix_t& vm, uintptr_t ent) {
	const Vector3 worldPos = ReadWorldPositionFromEntity(ent);
	if (worldPos.x * worldPos.x + worldPos.y * worldPos.y + worldPos.z * worldPos.z < 1.f)
		return;
	if (grenade_esp_detail::GrenadeEspCullSkip(worldPos))
		return;
	Vector3 sp{};
	if (!w2s(worldPos, sp, vm) || sp.z < 0.01f)
		return;
	const float sx = sp.x;
	const float sy = sp.y;
	const ImU32 mc = IM_COL32(230, 150, 80, 255);
	float y0 = sy;
	if (Settings::Visuals::worldGrenadeIcons && g_WeaponsIconFont) {
		float gbot = y0;
		DrawWeaponGlyphOutlined(dl, sx, y0, "l", mc, &gbot);
		y0 = gbot + 2.f;
	}
	StrokeTextBg(dl, "molotov (air)", sx, y0, mc);
}

inline void DrawDecoyProjectileWorldEsp(ImDrawList* dl, const view_matrix_t& vm, uintptr_t ent, float curTime) {
	const Vector3 worldPos = ReadWorldPositionFromEntity(ent);
	if (grenade_esp_detail::GrenadeEspCullSkip(worldPos))
		return;
	Vector3 sp{};
	if (!w2s(worldPos, sp, vm) || sp.z < 0.01f)
		return;
	const float sx = sp.x;
	const float sy = sp.y;
	const ImU32 dc = IM_COL32(180, 185, 210, 255);
	if (offsets::decoy_m_nDecoyShotTick && curTime > 1.f) {
		const int shotTick = g_GameMem.readv<int>(ent + static_cast<uintptr_t>(offsets::decoy_m_nDecoyShotTick));
		if (shotTick > 0) {
			const float t0 = static_cast<float>(shotTick) * (1.f / 64.f);
			constexpr float fuse = 15.f;
			const float remaining = (std::max)(0.f, fuse - (curTime - t0));
			const float frac = std::clamp(remaining / fuse, 0.f, 1.f);
			char buf[48]{};
			std::snprintf(buf, sizeof buf, "decoy  %.1fs", remaining);
			float y0 = sy;
			if (Settings::Visuals::worldGrenadeIcons && g_WeaponsIconFont) {
				float gbot = y0;
				DrawWeaponGlyphOutlined(dl, sx, y0, "m", dc, &gbot);
				y0 = gbot + 2.f;
			}
			StrokeTextBg(dl, buf, sx, y0, dc);
			const ImVec2 bsz = ImGui::CalcTextSize(buf);
			DrawProjTimerBar(dl, sx, y0 + bsz.y + 4.f, frac, IM_COL32(160, 170, 220, 255), IM_COL32(20, 22, 28, 200));
			return;
		}
	}
	float y0 = sy;
	if (Settings::Visuals::worldGrenadeIcons && g_WeaponsIconFont) {
		float gbot = y0;
		DrawWeaponGlyphOutlined(dl, sx, y0, "m", dc, &gbot);
		y0 = gbot + 2.f;
	}
	StrokeTextBg(dl, "decoy", sx, y0, dc);
}

inline void DrawHeFlashProjectileWorldEsp(ImDrawList* dl, const view_matrix_t& vm, uintptr_t ent, bool isHe) {
	if (offsets::proj_m_nExplodeEffectTickBegin) {
		const int detTick = g_GameMem.readv<int>(ent + static_cast<uintptr_t>(offsets::proj_m_nExplodeEffectTickBegin));
		if (detTick > 0)
			return;
	}
	const Vector3 worldPos = ReadWorldPositionFromEntity(ent);
	if (worldPos.x * worldPos.x + worldPos.y * worldPos.y + worldPos.z * worldPos.z < 1.f)
		return;
	if (grenade_esp_detail::GrenadeEspCullSkip(worldPos))
		return;
	Vector3 sp{};
	if (!w2s(worldPos, sp, vm) || sp.z < 0.01f)
		return;
	const float sx = sp.x;
	const float sy = sp.y;
	const char* lab = isHe ? "HE" : "flash";
	const char* glyph = isHe ? "j" : "i";
	const ImU32 gc = IM_COL32(240, 200, 120, 255);
	float y0 = sy;
	if (Settings::Visuals::worldGrenadeIcons && g_WeaponsIconFont) {
		float gbot = y0;
		DrawWeaponGlyphOutlined(dl, sx, y0, glyph, gc, &gbot);
		y0 = gbot + 2.f;
	}
	StrokeTextBg(dl, lab, sx, y0, gc);
}

/** IOCTL + entity taramasi — yalnizca cacheGame (render thread mutex bloklamaz). */
inline void TickWorldGrenadeEspDataCache() {
	const bool wg = Settings::Visuals::worldGrenades;
	const bool hull = Settings::Visuals::worldInfernoHull;
	const bool wantInferno = wg || hull;
	const bool active = wantInferno;
	if (active != grenade_esp_detail::g_prevWorldGrenadeKernelActive) {
		grenade_esp_detail::g_prevWorldGrenadeKernelActive = active;
		grenade_esp_detail::g_forceWorldGrenadeRescan = true;
	}
	if (!active || !client || !offsets::dwEntityList)
		return;

	std::lock_guard<std::mutex> lk(grenade_esp_detail::g_worldGrenadeCacheMutex);

	const unsigned scan_period = grenade_esp_detail::WorldGrenadeScanPeriodFrames();
	const bool doScan = grenade_esp_detail::g_forceWorldGrenadeRescan ||
	    ((grenade_esp_detail::g_scanCounter++) % scan_period) == 0;

	const float curTime = GlobalCurTime();
	const uint32_t localPawnH = LocalPlayerPawnHandle();
	const bool localOnly = Settings::Visuals::grenadeEspLocalOnly;

	if (doScan) {
		const uintptr_t entity_list =
		    g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwEntityList));
		if (entity_list) {
			grenade_esp_detail::ScanWorldProjectilesCatalyst(entity_list, wantInferno, wg, localPawnH, localOnly, curTime);
			if (wantInferno)
				grenade_esp_detail::ReconcileInfernoDrawCache();
			if (wg)
				grenade_esp_detail::ReconcileProjectileDrawCache();
			grenade_esp_detail::g_forceWorldGrenadeRescan = false;
		}
	}

	/** Render thread'in IOCTL'siz cull yapabilmesi icin cull origin'i burada guncelle. */
	grenade_esp_detail::SetupGrenadeEspSpatialCull(global_pawn);

	if (wantInferno) {
		for (auto it = grenade_esp_detail::g_inferno_draw.begin(); it != grenade_esp_detail::g_inferno_draw.end();) {
			if (!grenade_esp_detail::RefreshInfernoDrawEntry(*it, curTime))
				it = grenade_esp_detail::g_inferno_draw.erase(it);
			else
				++it;
		}
	}
	if (wg) {
		for (auto it = grenade_esp_detail::g_projectile_draw.begin(); it != grenade_esp_detail::g_projectile_draw.end();) {
			if (!grenade_esp_detail::RefreshProjectileDrawEntry(*it, curTime, localPawnH, localOnly))
				it = grenade_esp_detail::g_projectile_draw.erase(it);
			else
				++it;
		}
	}
}

/** Overlay: yalnizca w2s + cizim (surucu okuma yok). */
inline void DrawWorldGrenadeEsp(const view_matrix_t& vm) {
	const bool wg = Settings::Visuals::worldGrenades;
	const bool hull = Settings::Visuals::worldInfernoHull;
	const bool wantInferno = wg || hull;
	if (!wantInferno || !client)
		return;

	std::lock_guard<std::mutex> lk(grenade_esp_detail::g_worldGrenadeCacheMutex);

	/** Cull origin cacheGame'de set ediliyor (IOCTL render thread'de degil). */

	const float curTime = GlobalCurTime();
	const uint32_t vm_hash = grenade_esp_detail::HashViewMatrixCoarse(vm);
	ImDrawList* dl = ExBgDrawList();

	if (wantInferno) {
		for (const auto& ent : grenade_esp_detail::g_inferno_draw) {
			if (ent.fires.empty())
				continue;
			if (grenade_esp_detail::GrenadeEspCullSkip(ent.center))
				continue;
			grenade_esp_detail::InfernoDrawEntry copy = ent;
			grenade_esp_detail::EnsureInfernoScreenCache(copy, vm, vm_hash, hull);
			grenade_esp_detail::DrawInfernoFromCache(dl, copy, hull, wg);
		}
	}

	if (wg) {
		for (const auto& ent : grenade_esp_detail::g_projectile_draw) {
			if (grenade_esp_detail::GrenadeEspCullSkip(ent.world))
				continue;
			grenade_esp_detail::ProjectileDrawEntry copy = ent;
			grenade_esp_detail::EnsureProjectileScreenCache(copy, vm, vm_hash);
			if (!copy.on_screen)
				continue;
			grenade_esp_detail::DrawProjectileFromCache(dl, copy, curTime);
		}
	}
}

namespace grenade_cat_detail {

constexpr float kDegToRad = 3.14159265f / 180.f;
constexpr float kTickInterval = 1.f / 64.f;
constexpr float kGravityScale = 0.4f;
constexpr float kElasticity = 0.45f;
/** Simülasyon üst sınırı (tick); uzun yuvarlanma / düşük sürtünme için yüksek tutulur. */
constexpr int kMaxTicks = 16384;
constexpr int kTicksPerPoint = 4;
/** sv_gravity okuyucu yok; Catalyst ile ayni varsayilan. */
constexpr float kDefaultSvGravity = 800.f;

inline float vec_dot(const Vector3& a, const Vector3& b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline float vec_len_sqr(const Vector3& v) {
	return vec_dot(v, v);
}
inline float vec_len(const Vector3& v) {
	return std::sqrt(vec_len_sqr(v));
}
inline Vector3 vec_scale(const Vector3& v, float s) {
	return Vector3(v.x * s, v.y * s, v.z * s);
}
inline Vector3 vec_sub(const Vector3& a, const Vector3& b) {
	return Vector3(a.x - b.x, a.y - b.y, a.z - b.z);
}
inline Vector3 vec_add(const Vector3& a, const Vector3& b) {
	return Vector3(a.x + b.x, a.y + b.y, a.z + b.z);
}
inline Vector3 vec_normalized(const Vector3& v) {
	const float L = vec_len(v);
	if (L < 1e-8f)
		return Vector3(1.f, 0.f, 0.f);
	return vec_scale(v, 1.f / L);
}

/** Catalyst math::vector3::to_directions — pitch=x yaw=y roll=z */
inline void angles_to_directions(const Vector3& angles, Vector3* forward, Vector3* right, Vector3* up) {
	const float sp = std::sinf(angles.x * kDegToRad);
	const float cp = std::cosf(angles.x * kDegToRad);
	const float sy = std::sinf(angles.y * kDegToRad);
	const float cy = std::cosf(angles.y * kDegToRad);
	const float sr = std::sinf(angles.z * kDegToRad);
	const float cr = std::cosf(angles.z * kDegToRad);
	if (forward) {
		forward->x = cp * cy;
		forward->y = cp * sy;
		forward->z = -sp;
	}
	if (right) {
		right->x = -1.f * sr * sp * cy + -1.f * cr * -sy;
		right->y = -1.f * sr * sp * sy + -1.f * cr * cy;
		right->z = -1.f * sr * cp;
	}
	if (up) {
		up->x = cr * sp * cy + -sr * -sy;
		up->y = cr * sp * sy + -sr * cy;
		up->z = cr * cp;
	}
}

/** Catalyst systems::view::update — CViewRender + 0x10, origin+0 angles+0xC */
inline bool read_cview_render_origin_angles(Vector3& origin, Vector3& angles) {
	static uintptr_t view_render_inst = 0;
	if (!client)
		return false;
	if (!view_render_inst)
		view_render_inst = cat_mem::find_vtable_instance(static_cast<uintptr_t>(client), "CViewRender");
	if (!view_render_inst)
		return false;
	const uintptr_t view = view_render_inst + 0x10;
	origin = g_GameMem.readv<Vector3>(view);
	angles = g_GameMem.readv<Vector3>(view + 0xc);
	if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
	    !std::isfinite(angles.x) || !std::isfinite(angles.y) || !std::isfinite(angles.z)) {
		view_render_inst = 0;
		return false;
	}
	return true;
}

inline void apply_grenade_pitch_adjust(Vector3& angles) {
	if (angles.x > 90.f)
		angles.x -= 360.f;
	else if (angles.x < -90.f)
		angles.x += 360.f;
	angles.x -= (90.f - std::fabsf(angles.x)) * 10.f / 90.f;
}

inline void resolve_collision_cat(const ex_world_bvh::bvh::trace_result& trace, Vector3& pos, Vector3& vel) {
	const float total_elasticity = std::clamp(kElasticity, 0.f, 0.9f);
	const float backoff = vec_dot(vel, trace.normal) * 2.f;
	Vector3 new_vel = vec_scale(vec_sub(vel, vec_scale(trace.normal, backoff)), total_elasticity);

	if (trace.normal.z > 0.7f) {
		const float speed_sqr = vec_len_sqr(new_vel);
		if (speed_sqr > 96000.f) {
			const float l = vec_dot(vec_normalized(new_vel), trace.normal);
			if (l > 0.5f)
				new_vel = vec_scale(new_vel, 1.5f - l);
		}
		if (speed_sqr < 400.f) {
			vel = Vector3{};
			return;
		}
	}

	vel = new_vel;
	const float remaining = 1.f - trace.fraction;
	if (remaining > 0.f) {
		const Vector3 slide_end = vec_add(pos, vec_scale(new_vel, remaining * kTickInterval));
		const auto post_trace = ex_world_bvh::g_world_bvh.trace_ray(pos, slide_end);
		pos = post_trace.end_pos;
	}
}

/** Overlay trajectory: tek trace/tick — slide trace yok (2x BVH maliyeti kaldirildi). */
inline void resolve_collision_overlay(const ex_world_bvh::bvh::trace_result& trace, Vector3& vel) {
	const float total_elasticity = std::clamp(kElasticity, 0.f, 0.9f);
	const float backoff = vec_dot(vel, trace.normal) * 2.f;
	Vector3 new_vel = vec_scale(vec_sub(vel, vec_scale(trace.normal, backoff)), total_elasticity);

	if (trace.normal.z > 0.7f) {
		const float speed_sqr = vec_len_sqr(new_vel);
		if (speed_sqr > 96000.f) {
			const float l = vec_dot(vec_normalized(new_vel), trace.normal);
			if (l > 0.5f)
				new_vel = vec_scale(new_vel, 1.5f - l);
		}
		if (speed_sqr < 400.f) {
			vel = Vector3{};
			return;
		}
	}
	vel = new_vel;
}

inline void step_simulation_overlay(Vector3& pos, Vector3& vel, float sv_gravity,
                                    ex_world_bvh::bvh::trace_result& trace) {
	const float gravity = sv_gravity * kGravityScale;
	const float new_vel_z = vel.z - gravity * kTickInterval;
	const Vector3 move(
		vel.x * kTickInterval,
		vel.y * kTickInterval,
		(vel.z + new_vel_z) * 0.5f * kTickInterval);
	vel.z = new_vel_z;

	trace = ex_world_bvh::g_world_bvh.trace_ray(pos, vec_add(pos, move));
	pos = trace.end_pos;
	if (trace.hit)
		resolve_collision_overlay(trace, vel);
}

inline void step_simulation_cat(Vector3& pos, Vector3& vel, float sv_gravity, ex_world_bvh::bvh::trace_result& trace) {
	const float gravity = sv_gravity * kGravityScale;
	const float new_vel_z = vel.z - gravity * kTickInterval;
	const Vector3 move(
		vel.x * kTickInterval,
		vel.y * kTickInterval,
		(vel.z + new_vel_z) * 0.5f * kTickInterval);
	vel.z = new_vel_z;

	trace = ex_world_bvh::g_world_bvh.trace_ray(pos, vec_add(pos, move));
	pos = trace.end_pos;
	if (trace.hit)
		resolve_collision_cat(trace, pos, vel);
}

inline bool should_detonate_cat(std::uint16_t defIdx, const Vector3& vel, int tick, float detonate_time, float velocity_threshold) {
	switch (defIdx) {
	case 45: /* smoke */
	case 47: /* decoy */
	{
		const float speed_2d = std::sqrtf(vel.x * vel.x + vel.y * vel.y);
		const int check_ticks = static_cast<int>(0.2f / kTickInterval);
		return speed_2d < velocity_threshold && check_ticks > 0 && (tick % check_ticks) == 0;
	}
	case 46: /* molotov */
	case 48: /* inc */
		return static_cast<float>(tick) * kTickInterval > detonate_time;
	case 43: /* flash */
	case 44: /* he */
		return static_cast<float>(tick - 8) * kTickInterval > detonate_time;
	default:
		return false;
	}
}

inline void weapon_timing_cat(std::uint16_t defIdx, float& detonate_time, float& velocity_threshold) {
	switch (defIdx) {
	case 46:
	case 48:
		detonate_time = 2.f;
		velocity_threshold = 0.f;
		break;
	case 47:
		detonate_time = 2.f;
		velocity_threshold = 0.2f;
		break;
	default:
		detonate_time = 1.5f;
		velocity_threshold = 0.1f;
		break;
	}
}

/** Render thread artik kullanmiyor; worker tek seferde tamamlar. */
constexpr int kTrajTracesPerFrame = 12;
constexpr int kTrajWorkerMaxTicks = 320;
constexpr int kTrajSimCapHardMax = 1024;
constexpr int kTrajFastPreviewSteps = 36;

/** Worker tam BVH (CatalystSimulateFull) — render_opt ust siniri; save_fps worker'i kirpmaz. */
inline int EffectiveGrenadeHelperMaxTicks() noexcept {
	int t = std::clamp(Settings::render_opt::grenade_helper_max_ticks, 128, kTrajSimCapHardMax);
	if (Settings::misc::save_fps && t > 320)
		t = 320;
	return t;
}

struct TrajectorySimState {
	uint32_t input_hash = 0;
	bool complete = false;
	bool use_bvh = false;
	std::uint16_t def_idx = 0;
	int tick = 0;
	int tick_timer = 0;
	int bounce_count = 0;
	int sim_cap = kTrajSimCapHardMax;
	Vector3 pos{};
	Vector3 vel{};
	Vector3 end_pos{};
	Vector3 spawn{};
	float detonate_time = 1.5f;
	float velocity_threshold = 0.1f;
	float molotov_max_slope_z = 0.707f;
	std::vector<Vector3> points;

	void reset() {
		complete = false;
		tick = 0;
		tick_timer = 0;
		bounce_count = 0;
		points.clear();
	}
};

/** Worker/cache bucket: ince aci degisiminde BVH yolu kaybolmasin. */
inline uint32_t HashTrajectoryLookup(std::uint16_t defIdx, float strength, const Vector3& angles,
                                      const Vector3& origin, const Vector3& pawnVel) {
	const float ang_q = Settings::misc::save_fps ? 3.f : 2.f;
	auto qang = [ang_q](float f) -> uint32_t {
		return static_cast<uint32_t>(std::lround(f * ang_q)) & 0xFFFFu;
	};
	auto qpos = [](float f) -> uint32_t {
		return static_cast<uint32_t>(std::lround(f * 8.f)) & 0xFFFFu;
	};
	uint32_t h = static_cast<uint32_t>(defIdx);
	h = h * 31u + qang(angles.x) + (qang(angles.y) << 16);
	h = h * 31u + qpos(origin.x) + (qpos(origin.y) << 16);
	h = h * 31u + qpos(origin.z) + (static_cast<uint32_t>(std::lround(strength * 16.f)) << 16);
	h = h * 31u + qpos(pawnVel.x) + (qpos(pawnVel.y) << 16);
	return h;
}

inline uint32_t HashTrajectoryInputs(std::uint16_t defIdx, float strength, const Vector3& angles,
                                     const Vector3& origin, const Vector3& pawnVel) {
	const float ang_q = Settings::misc::save_fps ? 4.f : 8.f;
	auto qang = [ang_q](float f) -> uint32_t {
		return static_cast<uint32_t>(std::lround(f * ang_q)) & 0xFFFFu;
	};
	auto qpos = [](float f) -> uint32_t {
		return static_cast<uint32_t>(std::lround(f * 16.f)) & 0xFFFFu;
	};
	uint32_t h = static_cast<uint32_t>(defIdx);
	h = h * 31u + qang(angles.x) + (qang(angles.y) << 16);
	h = h * 31u + qpos(origin.x) + (qpos(origin.y) << 16);
	h = h * 31u + qpos(origin.z) + (static_cast<uint32_t>(std::lround(strength * 32.f)) << 16);
	h = h * 31u + qpos(pawnVel.x) + (qpos(pawnVel.y) << 16);
	return h;
}

/** @deprecated HashTrajectoryInputs kullan — bu cok kaba quantize ediyordu. */
inline std::uint32_t HashTrajectoryCoarse(std::uint16_t defIdx, float strength, const Vector3& angles,
                                          const Vector3& origin) {
	return HashTrajectoryInputs(defIdx, strength, angles, origin, Vector3{});
}

inline void TrajectoryFinalizePoints(TrajectorySimState& st) {
	if (st.points.size() < 2) {
		if (st.points.empty())
			st.points.push_back(st.spawn);
		st.points.push_back(st.end_pos);
	} else if (vec_len_sqr(vec_sub(st.points.back(), st.end_pos)) > 1.f)
		st.points.push_back(st.end_pos);
	st.complete = true;
}

inline void TrajectoryRunFastPreview(TrajectorySimState& st) {
	st.points.clear();
	st.points.push_back(st.spawn);
	const float g = kDefaultSvGravity * kGravityScale;
	const float horizon = 2.8f;
	for (int i = 1; i <= kTrajFastPreviewSteps; ++i) {
		const float t = horizon * (static_cast<float>(i) / static_cast<float>(kTrajFastPreviewSteps));
		const Vector3 p = vec_add(vec_add(st.spawn, vec_scale(st.vel, t)), Vector3(0.f, 0.f, -0.5f * g * t * t));
		st.points.push_back(p);
	}
	st.end_pos = st.points.back();
	TrajectoryFinalizePoints(st);
}

/** Catalyst setup_throw: gozden ileri BVH trace ile spawn (duvara yakin atis). */
inline Vector3 ComputeGrenadeThrowSpawn(const Vector3& eye_pos, const Vector3& forward) {
	if (!ex_world_bvh::g_world_bvh.valid())
		return vec_add(eye_pos, vec_scale(forward, 16.f));
	const auto trace = ex_world_bvh::g_world_bvh.trace_ray(eye_pos, vec_add(eye_pos, vec_scale(forward, 22.f)));
	return trace.hit ? vec_sub(trace.end_pos, vec_scale(forward, 6.f)) : vec_add(eye_pos, vec_scale(forward, 16.f));
}

inline void TrajectoryBeginSim(TrajectorySimState& st, std::uint16_t defIdx, const Vector3& spawn, const Vector3& vel) {
	st.reset();
	st.def_idx = defIdx;
	st.spawn = spawn;
	st.pos = spawn;
	st.vel = vel;
	st.points.push_back(spawn);
	weapon_timing_cat(defIdx, st.detonate_time, st.velocity_threshold);
	st.molotov_max_slope_z = std::cosf(45.f * kDegToRad);
	st.sim_cap = std::clamp(EffectiveGrenadeHelperMaxTicks(), 192, kTrajSimCapHardMax);
	st.use_bvh = ex_world_bvh::g_world_bvh.valid();
	if (!st.use_bvh) {
		TrajectoryRunFastPreview(st);
		return;
	}
}

inline void TrajectoryAdvanceBvh(TrajectorySimState& st, int max_traces) {
	if (st.complete || !st.use_bvh || max_traces <= 0)
		return;

	const float sv_gravity = kDefaultSvGravity;
	int traces = 0;
	while (st.tick < st.sim_cap && traces < max_traces && !st.complete) {
		if (st.tick_timer == 0)
			st.points.push_back(st.pos);

		ex_world_bvh::bvh::trace_result trace{};
		step_simulation_cat(st.pos, st.vel, sv_gravity, trace);
		++traces;

		if (trace.hit) {
			++st.bounce_count;
			const bool is_molotov = st.def_idx == 46 || st.def_idx == 48;
			if (is_molotov && trace.normal.z >= st.molotov_max_slope_z) {
				st.end_pos = st.pos;
				TrajectoryFinalizePoints(st);
				return;
			}
		}

		const bool velocity_stopped = std::fabsf(st.vel.x) < 20.f && std::fabsf(st.vel.y) < 20.f &&
		                              vec_len_sqr(st.vel) < 400.f;
		if (should_detonate_cat(st.def_idx, st.vel, st.tick, st.detonate_time, st.velocity_threshold) ||
		    st.bounce_count > 20 || velocity_stopped) {
			st.end_pos = st.pos;
			TrajectoryFinalizePoints(st);
			return;
		}

		if (trace.hit || ++st.tick_timer >= kTicksPerPoint)
			st.tick_timer = 0;
		++st.tick;
	}

	if (st.tick >= st.sim_cap) {
		st.end_pos = st.pos;
		TrajectoryFinalizePoints(st);
	}
}

/** VM + yol hash degismeden w2s tekrarlanmaz (overlay 500fps hedefi). */
/** Uzun BVH segmentlerine hafif ara nokta — cok seyrek degil, FPS icin sinirli. */
inline void DensifyTrajectoryPath(const std::vector<Vector3>& sparse, const Vector3& end_pos,
                                  std::vector<Vector3>& dense) {
	dense.clear();
	if (sparse.empty())
		return;
	constexpr float kFillMinDist = 52.f;
	constexpr float kFillStep = 38.f;
	constexpr int kMaxFillPerSeg = 2;
	constexpr std::size_t kMaxDensePts = 48;

	dense.push_back(sparse.front());
	for (std::size_t i = 1; i < sparse.size() && dense.size() < kMaxDensePts; ++i) {
		const Vector3& a = sparse[i - 1];
		const Vector3& b = sparse[i];
		const float dx = b.x - a.x;
		const float dy = b.y - a.y;
		const float dz = b.z - a.z;
		const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
		if (dist > kFillMinDist) {
			int steps = static_cast<int>(dist / kFillStep);
			if (steps > kMaxFillPerSeg)
				steps = kMaxFillPerSeg;
			if (steps < 1)
				steps = 1;
			for (int s = 1; s <= steps && dense.size() < kMaxDensePts; ++s) {
				const float t = static_cast<float>(s) / static_cast<float>(steps + 1);
				dense.push_back(Vector3{a.x + dx * t, a.y + dy * t, a.z + dz * t});
			}
		}
		dense.push_back(b);
	}
	if (dense.size() < kMaxDensePts && vec_len_sqr(vec_sub(dense.back(), end_pos)) > 64.f)
		dense.push_back(end_pos);
}

inline void DrawTrajectoryPolylineCached(const view_matrix_t& vm, const std::vector<Vector3>& points,
                                         const Vector3& end_pos, std::uint32_t traj_hash) {
	if (points.size() < 2)
		return;

	std::vector<Vector3> draw_pts;
	DensifyTrajectoryPath(points, end_pos, draw_pts);
	if (draw_pts.size() < 2)
		return;

	struct TrajScreenCache {
		std::uint32_t traj_hash = 0;
		std::uint32_t vm_hash = 0;
		std::vector<ImVec2> poly;
		ImVec2 end_scr{};
		bool end_valid = false;
	};
	constexpr int kTrajScrSlots = 4;
	thread_local TrajScreenCache s_scr_slots[kTrajScrSlots]{};
	thread_local int s_scr_lru = 0;

	const std::uint32_t vm_hash = grenade_esp_detail::HashViewMatrixCoarse(vm);
	TrajScreenCache* slot = nullptr;
	for (TrajScreenCache& s : s_scr_slots) {
		if (s.traj_hash == traj_hash) {
			slot = &s;
			break;
		}
	}
	if (!slot) {
		slot = &s_scr_slots[s_scr_lru % kTrajScrSlots];
		s_scr_lru = (s_scr_lru + 1) % kTrajScrSlots;
	}

	if (slot->traj_hash != traj_hash || slot->vm_hash != vm_hash) {
		slot->poly.clear();
		slot->poly.reserve(draw_pts.size());
		for (const Vector3& p : draw_pts) {
			Vector3 sp{};
			if (!w2s(p, sp, vm) || sp.z < 0.01f)
				continue;
			slot->poly.push_back(ImVec2(sp.x, sp.y));
		}
		Vector3 end_sp{};
		slot->end_valid = w2s(end_pos, end_sp, vm) && end_sp.z > 0.01f;
		if (slot->end_valid)
			slot->end_scr = ImVec2(end_sp.x, end_sp.y);
		slot->traj_hash = traj_hash;
		slot->vm_hash = vm_hash;
	}

	if (slot->poly.size() < 2)
		return;

	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	const ImU32 lineCol = ImGui::ColorConvertFloat4ToU32(ImVec4(
		EspUiColors::grenade_traj_line[0], EspUiColors::grenade_traj_line[1], EspUiColors::grenade_traj_line[2], 1.f));
	const ImU32 detCol = ImGui::ColorConvertFloat4ToU32(ImVec4(
		EspUiColors::grenade_traj_marker[0], EspUiColors::grenade_traj_marker[1], EspUiColors::grenade_traj_marker[2], 1.f));

	if (slot->poly.size() >= 2)
		dl->AddPolyline(slot->poly.data(), static_cast<int>(slot->poly.size()), lineCol, 0, 1.8f);
	if (slot->end_valid)
		dl->AddCircleFilled(slot->end_scr, 5.f, detCol, 14);
}

inline void DrawTrajectoryPolyline(const view_matrix_t& vm, const std::vector<Vector3>& points,
                                   const Vector3& end_pos) {
	DrawTrajectoryPolylineCached(vm, points, end_pos, 0u);
}

/** Catalyst systems::view::update — once CViewRender (origin+angles), sonra yedek. */
inline bool GatherGrenadeThrowView(uintptr_t localPawn, const Vector3& eyeWorldFallback, Vector3& out_origin,
                                   Vector3& out_angles) {
	out_angles = Vector3{};
	out_origin = eyeWorldFallback;

	Vector3 cvo{}, cva{};
	if (read_cview_render_origin_angles(cvo, cva)) {
		out_origin = cvo;
		out_angles = cva;
		return true;
	}

	bool haveAngles = false;
	if (offsets::dwViewAngles && client) {
		const Vector3 va = g_GameMem.readv<Vector3>(static_cast<uintptr_t>(client) + static_cast<uintptr_t>(offsets::dwViewAngles));
		if (std::isfinite(va.x) && std::isfinite(va.y) && std::fabsf(va.x) <= 89.99f) {
			out_angles = va;
			haveAngles = true;
		}
	}
	if (!haveAngles && localPawn && offsets::m_angEyeAngles) {
		const Vector3 ea = g_GameMem.readv<Vector3>(localPawn + static_cast<uintptr_t>(offsets::m_angEyeAngles));
		if (std::isfinite(ea.x) && std::isfinite(ea.y)) {
			out_angles = ea;
			haveAngles = true;
		}
	}
	if (!haveAngles)
		return false;
	if (out_origin.IsZero() && !eyeWorldFallback.IsZero())
		out_origin = eyeWorldFallback;
	else if (out_origin.IsZero() && localPawn) {
		out_origin = ReadWorldPositionFromEntity(localPawn);
		out_origin.z += 64.f;
	}
	return true;
}

/** Catalyst misc::grenades::simulate — birebir (max_ticks=1024, ticks_per_point=4, slide trace). */
struct CatalystTrajectory {
	std::vector<Vector3> points;
	Vector3 end_pos{};
	bool valid = false;
};

inline void CatalystSimulateFull(std::uint16_t defIdx, const Vector3& start, const Vector3& velocity,
                                 CatalystTrajectory& out, int max_ticks = 1024) {
	constexpr int kCatMaxTicks = 1024;
	constexpr int kCatTicksPerPoint = 4;
	max_ticks = std::clamp(max_ticks, 64, kCatMaxTicks);

	if (!ex_world_bvh::g_world_bvh.valid()) {
		out.points.clear();
		out.valid = false;
		return;
	}

	float detonate_time = 1.5f;
	float velocity_threshold = 0.1f;
	weapon_timing_cat(defIdx, detonate_time, velocity_threshold);
	const float molotov_max_slope_z = std::cosf(45.f * kDegToRad);
	const float sv_gravity = kDefaultSvGravity;

	out.points.clear();
	out.points.reserve(static_cast<std::size_t>(kCatMaxTicks / kCatTicksPerPoint));
	out.valid = false;

	Vector3 pos = start;
	Vector3 vel = velocity;
	int bounce_count = 0;
	int tick_timer = 0;
	int end_tick = -1;

	for (int tick = 0; tick < max_ticks; ++tick) {
		if (tick_timer == 0)
			out.points.push_back(pos);

		ex_world_bvh::bvh::trace_result trace{};
		step_simulation_cat(pos, vel, sv_gravity, trace);

		if (trace.hit) {
			++bounce_count;
			const bool is_molotov = defIdx == 46 || defIdx == 48;
			if (is_molotov && trace.normal.z >= molotov_max_slope_z) {
				end_tick = tick;
				out.end_pos = pos;
				break;
			}
		}

		const bool velocity_stopped = std::fabsf(vel.x) < 20.f && std::fabsf(vel.y) < 20.f && vec_len_sqr(vel) < 400.f;
		if (should_detonate_cat(defIdx, vel, tick, detonate_time, velocity_threshold) || bounce_count > 20 ||
		    velocity_stopped) {
			end_tick = tick;
			out.end_pos = pos;
			break;
		}

		if (trace.hit || ++tick_timer >= kCatTicksPerPoint)
			tick_timer = 0;
	}

	if (end_tick < 0 && !out.points.empty()) {
		end_tick = max_ticks - 1;
		out.end_pos = pos;
	}
	if (!out.points.empty() && end_tick >= 0) {
		if (vec_len_sqr(vec_sub(out.points.back(), out.end_pos)) > 1.f)
			out.points.push_back(out.end_pos);
		out.valid = true;
	}
}

/** Worker: slide trace yok — ~2x daha hizli, duvar carpismasi korunur. */
inline void CatalystSimulateOverlay(std::uint16_t defIdx, const Vector3& start, const Vector3& velocity,
                                    CatalystTrajectory& out, int max_ticks = kTrajWorkerMaxTicks) {
	constexpr int kCatTicksPerPoint = 4;
	if (!ex_world_bvh::g_world_bvh.valid()) {
		out.points.clear();
		out.valid = false;
		return;
	}

	float detonate_time = 1.5f;
	float velocity_threshold = 0.1f;
	weapon_timing_cat(defIdx, detonate_time, velocity_threshold);
	const float molotov_max_slope_z = std::cosf(45.f * kDegToRad);
	const float sv_gravity = kDefaultSvGravity;
	max_ticks = std::clamp(max_ticks, 64, kTrajSimCapHardMax);

	out.points.clear();
	out.points.reserve(static_cast<std::size_t>(max_ticks / kCatTicksPerPoint));
	out.valid = false;

	Vector3 pos = start;
	Vector3 vel = velocity;
	int bounce_count = 0;
	int tick_timer = 0;
	int end_tick = -1;

	for (int tick = 0; tick < max_ticks; ++tick) {
		if (tick_timer == 0)
			out.points.push_back(pos);

		ex_world_bvh::bvh::trace_result trace{};
		step_simulation_overlay(pos, vel, sv_gravity, trace);

		if (trace.hit) {
			++bounce_count;
			const bool is_molotov = defIdx == 46 || defIdx == 48;
			if (is_molotov && trace.normal.z >= molotov_max_slope_z) {
				end_tick = tick;
				out.end_pos = pos;
				break;
			}
		}

		const bool velocity_stopped = std::fabsf(vel.x) < 20.f && std::fabsf(vel.y) < 20.f && vec_len_sqr(vel) < 400.f;
		if (should_detonate_cat(defIdx, vel, tick, detonate_time, velocity_threshold) || bounce_count > 20 ||
		    velocity_stopped) {
			end_tick = tick;
			out.end_pos = pos;
			break;
		}

		if (trace.hit || ++tick_timer >= kCatTicksPerPoint)
			tick_timer = 0;
	}

	if (end_tick < 0 && !out.points.empty()) {
		end_tick = max_ticks - 1;
		out.end_pos = pos;
	}
	if (!out.points.empty() && end_tick >= 0) {
		if (vec_len_sqr(vec_sub(out.points.back(), out.end_pos)) > 1.f)
			out.points.push_back(out.end_pos);
		out.valid = true;
	}
}

/** Catalyst misc::grenades::can_predict + pin edge (throw cooldown). */
inline std::chrono::steady_clock::time_point g_catalyst_last_throw_time{};
inline bool g_catalyst_was_holding_pin = false;

inline void CatalystUpdatePinHoldEdge(std::uintptr_t weapon) {
	bool pin_now = false;
	if (offsets::nade_m_bPinPulled && weapon)
		pin_now = g_GameMem.readv<bool>(weapon + static_cast<uintptr_t>(offsets::nade_m_bPinPulled));
	if (g_catalyst_was_holding_pin && !pin_now)
		g_catalyst_last_throw_time = std::chrono::steady_clock::now();
	g_catalyst_was_holding_pin = pin_now;
}

inline bool CatalystCanPredict(std::uintptr_t weapon, std::uint16_t defIdx) {
	if (!weapon || defIdx < 43 || defIdx > 48)
		return false;

	const bool pin_pulled = offsets::nade_m_bPinPulled &&
	                          g_GameMem.readv<bool>(weapon + static_cast<uintptr_t>(offsets::nade_m_bPinPulled));
	if (!pin_pulled) {
		const float since = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_catalyst_last_throw_time)
		                        .count();
		if (since < 1.0f)
			return false;
	}
	if (offsets::nade_m_fThrowTime) {
		const float throw_time = g_GameMem.readv<float>(weapon + static_cast<uintptr_t>(offsets::nade_m_fThrowTime));
		if (throw_time > 0.f)
			return false;
	}
	return true;
}

/** Catalyst misc::grenades::setup_throw */
inline bool CatalystSetupThrow(std::uintptr_t weapon, std::uintptr_t localPawn, float throw_velocity,
                               Vector3 view_origin, Vector3 view_angles, Vector3& out_origin, Vector3& out_velocity) {
	float strength = 1.f;
	if (offsets::nade_m_flThrowStrength && offsets::nade_m_bPinPulled && weapon) {
		const bool pin_pulled = g_GameMem.readv<bool>(weapon + static_cast<uintptr_t>(offsets::nade_m_bPinPulled));
		if (pin_pulled) {
			strength = std::clamp(
			    g_GameMem.readv<float>(weapon + static_cast<uintptr_t>(offsets::nade_m_flThrowStrength)), 0.f, 1.f);
			if (std::fabsf(strength - 0.5f) <= 0.1f)
				strength = 0.5f;
		}
	}

	Vector3 angles = view_angles;
	if (angles.x > 90.f)
		angles.x -= 360.f;
	else if (angles.x < -90.f)
		angles.x += 360.f;
	angles.x -= (90.f - std::fabsf(angles.x)) * 10.f / 90.f;

	Vector3 pawn_vel{};
	if (offsets::m_vecAbsVelocity && localPawn)
		pawn_vel = g_GameMem.readv<Vector3>(localPawn + static_cast<uintptr_t>(offsets::m_vecAbsVelocity));

	Vector3 eye_pos = view_origin;
	eye_pos.z += strength * 12.f - 12.f;

	Vector3 forward{}, right{}, up{};
	angles_to_directions(angles, &forward, &right, &up);

	if (!ex_world_bvh::g_world_bvh.valid())
		return false;

	const auto trace = ex_world_bvh::g_world_bvh.trace_ray(eye_pos, vec_add(eye_pos, vec_scale(forward, 22.f)));
	out_origin = trace.hit ? vec_sub(trace.end_pos, vec_scale(forward, 6.f)) : vec_add(eye_pos, vec_scale(forward, 16.f));

	const float throw_vel = std::clamp(throw_velocity * 0.9f, 15.f, 750.f);
	const float throw_speed = (strength * 0.7f + 0.3f) * throw_vel;
	out_velocity = vec_add(vec_scale(forward, throw_speed), vec_scale(pawn_vel, 1.25f));
	return true;
}

} // namespace grenade_cat_detail

inline bool SchemaLooksLikeDroppedGun(const char* cn) {
	if (!cn || !cn[0])
		return false;
	if (StrStrIAscii(cn, "projectile"))
		return false;
	if (StrStrIAscii(cn, "viewmodel") || StrStrIAscii(cn, "view_model"))
		return false;
	if (StrStrIAscii(cn, "inferno"))
		return false;
	if (StrStrIAscii(cn, "planted"))
		return false;
	if (StrStrIAscii(cn, "observer"))
		return false;
	if (StrStrIAscii(cn, "c4"))
		return false;
	if (StrStrIAscii(cn, "playerpawn") || StrStrIAscii(cn, "player_pawn"))
		return false;
	if (StrStrIAscii(cn, "controller") && !StrStrIAscii(cn, "weapon"))
		return false;
	if (StrStrIAscii(cn, "weapon"))
		return true;
	if (StrStrIAscii(cn, "baseplayerweapon"))
		return true;
	if (StrStrIAscii(cn, "csweapon") || StrStrIAscii(cn, "cs2weapon"))
		return true;
	if (StrStrIAscii(cn, "gun") && !StrStrIAscii(cn, "grenade") && !StrStrIAscii(cn, "flash"))
		return true;
	if (StrStrIAscii(cn, "knife") || StrStrIAscii(cn, "melee"))
		return true;
	return false;
}

struct DroppedWorldEspCacheEntry {
	Vector3 world{};
	char label[24]{};
	double validUntil = 0.0;
	bool hideCorpse = false;
};
inline std::unordered_map<uintptr_t, DroppedWorldEspCacheEntry> g_droppedWorldEspCache;

inline bool DroppedWeaponShouldHideWhileBoundToCorpse(uintptr_t weaponEnt) {
	if (!weaponEnt || !offsets::m_hOwnerEntity || !offsets::m_iHealth)
		return false;
	const uintptr_t owner = ResolveOwnerEntityPtr(weaponEnt);
	if (!owner || owner < 0x10000)
		return false;
	char ocn[112]{};
	if (!ReadEntitySchemaClassName(owner, ocn, sizeof ocn))
		return false;
	if (!StrStrIAscii(ocn, "pawn") && !StrStrIAscii(ocn, "playerpawn") && !StrStrIAscii(ocn, "player_pawn"))
		return false;
	const int hp = g_GameMem.readv<int>(owner + static_cast<uintptr_t>(offsets::m_iHealth));
	return hp <= 0 || hp > 100;
}

/** Bomb world color + bombaci: hafif tarama (dusmus silah listesi degil). */
inline void TickBombWorldEspCache(uintptr_t localPawn) {
	using namespace grenade_esp_detail;
	const bool need_bomb_world = Settings::Visuals::bombWorldEsp;
	const bool need_carrier = Settings::Visuals::bombCarrierEsp;
	if (!need_bomb_world && !need_carrier)
		return;

	std::lock_guard<std::mutex> lk(g_pickupCacheMutex);
	const double scan_now = ImGui::GetTime();
	RefreshPlantedBombWorldCache();
	if (need_carrier)
		g_cachedC4CarrierEntities.clear();

	if (!need_bomb_world && !need_carrier)
		return;
	if (!client || !offsets::dwEntityList)
		return;

	const uintptr_t entity_list = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwEntityList));
	if (!entity_list)
		return;

	Vector3 loc{};
	if (localPawn)
		loc = ReadWorldPositionFromEntity(localPawn);
	constexpr float kBombMaxDSq = 14000.f * 14000.f;
	const int i_max = ClampedPickupEntityScanMax(entity_list);
	const float curTime = GlobalCurTime();

	{
		std::lock_guard<std::mutex> ck(world_scan::g_classify_mtx);
		world_scan::g_classify_cache.NextTick();
	}

	/** BATCH IOCTL: tek pass'te 512-blok pointer tablolari okunur (eskiden per-entity 1 IOCTL). */
	world_scan::EnumerateLiveEntities(entity_list, i_max, [&](int /*idx*/, uintptr_t ent) {
		if (ent == global_pawn)
			return;

		/** FAST PATH: cache'te "ilgisiz" olarak isaretliyse identity okumayi atla. */
		{
			std::lock_guard<std::mutex> ck(world_scan::g_classify_mtx);
			if (world_scan::g_classify_cache.PeekAndTouchIrrelevant(ent))
				return;
		}

		const uintptr_t identProbe = g_GameMem.readv<uintptr_t>(ent + 0x10);
		if (!identProbe || identProbe < 0x10000u)
			return;

		uint16_t def = 0;
		world_scan::Kind kind;
		{
			std::lock_guard<std::mutex> ck(world_scan::g_classify_mtx);
			kind = ClassifyEntityCached(ent, identProbe, def, curTime);
		}

		if (kind != world_scan::Kind::Bomb)
			return;

		if (need_carrier) {
			const uintptr_t owner = ResolveOwnerEntityPtr(ent);
			if (owner)
				g_cachedC4CarrierEntities.insert(owner);
		}

		if (!need_bomb_world)
			return;
		if (g_equipped_weapon_entities.count(ent))
			return;
		const Vector3 wp = ReadWorldPositionFromEntity(ent);
		if (DroppedWeaponOverlapsLivePlayers(wp))
			return;
		bool inRange = true;
		if (loc.length2d() > 1.f) {
			const float dx = wp.x - loc.x, dy = wp.y - loc.y, dz = wp.z - loc.z;
			inRange = dx * dx + dy * dy + dz * dz <= kBombMaxDSq;
		}
		if (inRange)
			MergeGroundC4CacheEntry(wp, scan_now);
	});

	if (need_bomb_world)
		PruneGroundC4Cache(scan_now);

	/** Periyodik cache temizligi (gitmis entity'leri at). */
	{
		std::lock_guard<std::mutex> ck(world_scan::g_classify_mtx);
		if ((world_scan::g_classify_cache.Tick() & 0x3Fu) == 0u)
			world_scan::g_classify_cache.Prune(64u);
	}
}

/** Dusmus silah: agir entity taramasi — cacheGame'de seyrek; overlay IOCTL yok. */
inline void TickDroppedWeaponEspCache(uintptr_t localPawn, bool rebuild_equipped_ignore) {
	using namespace grenade_esp_detail;
	if (!Settings::Visuals::droppedWeaponEsp)
		return;

	std::lock_guard<std::mutex> lk(g_pickupCacheMutex);
	(void)rebuild_equipped_ignore;
	RebuildEquippedWeaponIgnoreSet(localPawn);

	if (!client || !offsets::dwEntityList)
		return;

	const uintptr_t entity_list = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwEntityList));
	if (!entity_list)
		return;

	Vector3 loc{};
	if (localPawn)
		loc = ReadWorldPositionFromEntity(localPawn);
	constexpr float kDropMaxDSq = 12000.f * 12000.f;
	const double now = ImGui::GetTime();
	const float curTime = GlobalCurTime();
	const int i_max = ClampedDroppedWeaponScanMax(entity_list);

	{
		std::lock_guard<std::mutex> ck(world_scan::g_classify_mtx);
		world_scan::g_classify_cache.NextTick();
	}

	/** BATCH IOCTL: 512'lik bloklar tek pass'te okunur (eskiden 2048 IOCTL → ~4 IOCTL). */
	world_scan::EnumerateLiveEntities(entity_list, i_max, [&](int /*idx*/, uintptr_t ent) {
		if (ent == global_pawn)
			return;

		/** FAST PATH: ilgisiz cache hit → identity IOCTL atla. */
		{
			std::lock_guard<std::mutex> ck(world_scan::g_classify_mtx);
			if (world_scan::g_classify_cache.PeekAndTouchIrrelevant(ent))
				return;
		}

		const uintptr_t identProbe = g_GameMem.readv<uintptr_t>(ent + 0x10);
		if (!identProbe || identProbe < 0x10000u)
			return;

		if (g_equipped_weapon_entities.count(ent)) {
			g_droppedWorldEspCache.erase(ent);
			return;
		}

		uint16_t def = 0;
		world_scan::Kind kind;
		{
			std::lock_guard<std::mutex> ck(world_scan::g_classify_mtx);
			kind = ClassifyEntityCached(ent, identProbe, def, curTime);
		}
		if (kind != world_scan::Kind::DroppedWeapon)
			return;
		if (!def || (def >= 43 && def <= 48) || def == 49)
			return;

		const bool hideCorpse = DroppedWeaponShouldHideWhileBoundToCorpse(ent);
		if (hideCorpse) {
			g_droppedWorldEspCache.erase(ent);
			return;
		}

		char nameBuf[24]{};
		const char* wname = WeaponNameFromDefIndex(def);
		if (!wname) {
			std::snprintf(nameBuf, sizeof nameBuf, "#%u", static_cast<unsigned>(def));
			wname = nameBuf;
		}

		const Vector3 wp = ReadWorldPositionFromEntity(ent);
		if (DroppedWeaponOverlapsLivePlayers(wp))
			return;
		if (loc.length2d() > 1.f) {
			const float dx = wp.x - loc.x, dy = wp.y - loc.y, dz = wp.z - loc.z;
			if (dx * dx + dy * dy + dz * dz > kDropMaxDSq)
				return;
		}
		DroppedWorldEspCacheEntry ce{};
		ce.world = wp;
		if (wname)
			std::snprintf(ce.label, sizeof ce.label, "%s", wname);
		else
			ce.label[0] = 0;
		ce.validUntil = now + 2.0;
		ce.hideCorpse = false;
		g_droppedWorldEspCache[ent] = ce;
	});
}

/** Geriye uyumluluk — ayri bomb/dropped zamanlayicilari tercih edilir. */
inline void TickWorldPickupEspScan(uintptr_t localPawn, bool rebuild_equipped_ignore) {
	TickBombWorldEspCache(localPawn);
	TickDroppedWeaponEspCache(localPawn, rebuild_equipped_ignore);
}

inline void ApplyCachedC4CarrierFlags(std::vector<UE4Structs::CS2Entity>& players) {
	if (!Settings::Visuals::bombCarrierEsp || players.empty())
		return;
	for (auto& pl : players) {
		if (grenade_esp_detail::g_cachedC4CarrierEntities.count(pl.Actor) ||
		    grenade_esp_detail::g_cachedC4CarrierEntities.count(pl.Controller))
			pl.has_c4 = true;
	}
}

/** MergeWorldCarriedC4CarrierFlags: cacheGame icin — tam liste taramasi yapmaz. */
inline void MergeWorldCarriedC4CarrierFlags(std::vector<UE4Structs::CS2Entity>& players) {
	ApplyCachedC4CarrierFlags(players);
}

inline unsigned WorldPickupEspScanPeriodFrames() noexcept {
	return grenade_esp_detail::WorldPickupEspScanPeriodFrames();
}

inline bool ShouldRunWorldPickupEspScanThisFrame() noexcept {
	if (!AnyWorldPickupEspEnabled())
		return false;
	return (grenade_esp_detail::g_pickupScanCounter++ % WorldPickupEspScanPeriodFrames()) == 0u;
}

/** Yer + kurulu bomba — yalnizca cache + w2s (IOCTL yok). */
inline void DrawBombWorldEsp(const view_matrix_t& vm) {
	if (!Settings::Visuals::bombWorldEsp || !client)
		return;
	std::lock_guard<std::mutex> lk(grenade_esp_detail::g_pickupCacheMutex);
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	const ImU32 colDot = ImGui::ColorConvertFloat4ToU32(ImVec4(
		EspUiColors::bomb_esp_col[0], EspUiColors::bomb_esp_col[1], EspUiColors::bomb_esp_col[2], 1.f));
	const ImU32 txtWhite = IM_COL32(255, 255, 255, 255);

	const double draw_now = ImGui::GetTime();
	const bool show_planted = grenade_esp_detail::g_cachedPlantedBomb.show &&
	    grenade_esp_detail::g_cachedPlantedBomb.keepUntil >= draw_now;
	if (show_planted) {
		Vector3 sp{};
		if (w2s(grenade_esp_detail::g_cachedPlantedBomb.world, sp, vm) && sp.z >= 0.01f) {
			dl->AddCircleFilled(ImVec2(sp.x, sp.y), 6.f, colDot, 16);
			StrokeTextBg(dl, "Planted", sp.x, sp.y - 18.f, txtWhite);
		}
	}

	for (const auto& c4 : grenade_esp_detail::g_cachedGroundC4) {
		if (c4.keepUntil < draw_now)
			continue;
		Vector3 sp{};
		if (!w2s(c4.world, sp, vm) || sp.z < 0.01f)
			continue;
		dl->AddCircleFilled(ImVec2(sp.x, sp.y), 5.f, colDot, 14);
		StrokeTextBg(dl, "C4", sp.x, sp.y - 16.f, txtWhite);
	}
}

/** Dusmus silah — yalnizca cache + w2s (surucu okuma yok). */
inline void DrawDroppedWeaponsWorldEsp(const view_matrix_t& vm) {
	if (!Settings::Visuals::droppedWeaponEsp)
		return;

	std::lock_guard<std::mutex> lk(grenade_esp_detail::g_pickupCacheMutex);
	const double now = ImGui::GetTime();
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	const ImU32 dwCol = ImGui::ColorConvertFloat4ToU32(ImVec4(
		EspUiColors::dropped_weapon_esp[0], EspUiColors::dropped_weapon_esp[1], EspUiColors::dropped_weapon_esp[2], 1.f));

	for (auto it = g_droppedWorldEspCache.begin(); it != g_droppedWorldEspCache.end(); ) {
		if (it->second.validUntil < now || it->second.hideCorpse) {
			it = g_droppedWorldEspCache.erase(it);
			continue;
		}
		if (g_equipped_weapon_entities.count(it->first)) {
			it = g_droppedWorldEspCache.erase(it);
			continue;
		}
		if (DroppedWeaponOverlapsLivePlayers(it->second.world)) {
			it = g_droppedWorldEspCache.erase(it);
			continue;
		}
		Vector3 sp{};
		if (!w2s(it->second.world, sp, vm) || sp.z < 0.01f) {
			++it;
			continue;
		}
		const char* lt = it->second.label[0] ? it->second.label : "?";
		catalyst_esp::DrawDroppedWeaponWorldLabel(dl, sp.x, sp.y - 6.f, lt, dwCol, sp.z);
		++it;
	}
}

} // namespace ex_esp
