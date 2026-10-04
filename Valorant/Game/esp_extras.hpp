#pragma once
/**
 * ananbaban-stable ESP davranislarinin Expectional cizim katmanina ozeti:
 * dolu kutu / gradient, goz hatti, ses halkasi, dunya bombasi, cephane cubugu,
 * mesafe, scoped / blind, AWP nisangah, menu icinde onizleme.
 */
#include "globals.hpp"
#include "esp_layout.hpp"
#include "offsets_runtime.hpp"
#include "structs.hpp"
/** TriggerBot/config_io gibi TUlarda render.hpp (using UE4Structs) yok; Vector3 global gorunur olmali. */
using UE4Structs::Vector3;
using UE4Structs::view_matrix_t;
#include "entity_handle.hpp"
#include "weapon_names.hpp"
#include "../Driver/driver.hpp"

#include "../../Includes/Imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

bool w2s(const Vector3& pos, Vector3& out, view_matrix_t matrix);

namespace ex_esp {

/** Yer silahi ESP: herkesin elindeki silah entity pointer'i (dropped taramada haric tutulur). */
inline std::unordered_set<uintptr_t> g_equipped_weapon_entities;
/** Canli pawn ayak konumu (m_vecOrigin) — silah entity adresi tutmazsa bile "adamın üstünde" etiketi keser. */
inline std::vector<Vector3> g_drop_esp_pawn_feet;

/** Tek pawn ayagi ile dropped-ESP silindir testi (yer silahi oyuncu ayagi ustunde/yakin gorunmesin). */
inline bool WeaponWorldOverlapsPawnFeetCylinder(const Vector3& weaponWorld, const Vector3& feet) {
	constexpr float kHorizSq = 92.f * 92.f;
	constexpr float kBelowFeet = 48.f;
	constexpr float kAboveFeet = 125.f;
	const float dx = weaponWorld.x - feet.x;
	const float dy = weaponWorld.y - feet.y;
	const float dz = weaponWorld.z - feet.z;
	if (dx * dx + dy * dy > kHorizSq)
		return false;
	return dz >= -kBelowFeet && dz <= kAboveFeet;
}

/** Aktif silah pointer'i ile entity listesi bazen tutmuyor; dünya konumu oyuncu silindiri içindeyse dropped sayma. */
inline bool DroppedWeaponOverlapsPlayerCapsule(const Vector3& weaponWorld) {
	for (const Vector3& feet : g_drop_esp_pawn_feet) {
		if (WeaponWorldOverlapsPawnFeetCylinder(weaponWorld, feet))
			return true;
	}
	return false;
}

/** cacheGame ayak listesi + canli PlayerList ( IOCTL yok ) — eldeki silah/bicak etiketi git-gel onleme. */
inline bool DroppedWeaponOverlapsLivePlayers(const Vector3& weaponWorld) {
	if (DroppedWeaponOverlapsPlayerCapsule(weaponWorld))
		return true;
	{
		std::lock_guard<std::mutex> lk(g_PlayerListMutex);
		for (const UE4Structs::CS2Entity& pl : UE4Structs::PlayerList) {
			if (!pl.has_world_origin)
				continue;
			const Vector3 feet{ pl.world_x, pl.world_y, pl.world_z };
			if (WeaponWorldOverlapsPawnFeetCylinder(weaponWorld, feet))
				return true;
		}
	}
	return false;
}

/** cacheGame ile ayni slot 1..63 controller → pawn → aktif silah cozumu ( IOCTL sayisi: frame basina ~64 kisa okuma). */
inline void RebuildEquippedWeaponIgnoreSet(uintptr_t localPawn) {
	g_equipped_weapon_entities.clear();
	g_drop_esp_pawn_feet.clear();
	if (!client || !offsets::dwEntityList || !offsets::dwPlayerPawn || !offsets::m_vecOrigin)
		return;

	const auto pushFeet = [](uintptr_t pawn) {
		if (!pawn)
			return;
		const Vector3 o = g_GameMem.readv<Vector3>(pawn + offsets::m_vecOrigin);
		if (o.length2d() > 8.f)
			g_drop_esp_pawn_feet.push_back(o);
	};

	const auto regWeapon = [](uintptr_t pawn) {
		if (!pawn || !offsets::m_pWeaponServices || !offsets::m_hActiveWeapon)
			return;
		const uintptr_t ws = g_GameMem.readv<uintptr_t>(pawn + static_cast<uintptr_t>(offsets::m_pWeaponServices));
		if (!ws)
			return;
		const uint32_t hw = g_GameMem.readv<uint32_t>(ws + static_cast<uintptr_t>(offsets::m_hActiveWeapon));
		const uintptr_t wpn = ex_entity::ResolveHandle(hw);
		if (wpn >= 0x10000)
			g_equipped_weapon_entities.insert(wpn);
	};

	/** Olunun ayagi silindirine dusen silahlari yanlislikla gizleme (yerel oluyken). */
	if (localPawn && offsets::m_iHealth) {
		const int lhp = g_GameMem.readv<int>(localPawn + offsets::m_iHealth);
		if (lhp > 0 && lhp <= 100) {
			pushFeet(localPawn);
			regWeapon(localPawn);
		}
	}

	const uintptr_t entity_list = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwEntityList));
	if (!entity_list)
		return;
	const uintptr_t kEntStride = static_cast<uintptr_t>(
		offsets::entity_controller_stride ? offsets::entity_controller_stride : 112u);
	for (int i = 1; i < 64; ++i) {
		const uintptr_t list_entry = g_GameMem.readv<uintptr_t>(
			entity_list + 8ull * (static_cast<uintptr_t>(i & 0x7FFF) >> 9) + 16);
		if (!list_entry)
			continue;
		const uintptr_t controller = g_GameMem.readv<uintptr_t>(list_entry + kEntStride * (i & 0x1FF));
		if (!controller)
			continue;
		const std::uint32_t playerpawn = g_GameMem.readv<std::uint32_t>(controller + offsets::dwPlayerPawn);
		const uintptr_t list_entry2 = g_GameMem.readv<uintptr_t>(entity_list + 0x8 * ((playerpawn & 0x7FFF) >> 9) + 16);
		if (!list_entry2)
			continue;
		const uintptr_t pawn = g_GameMem.readv<uintptr_t>(list_entry2 + kEntStride * (playerpawn & 0x1FF));
		if (!pawn || pawn == localPawn)
			continue;
		if (!offsets::m_iHealth)
			continue;
		const int hp = g_GameMem.readv<int>(pawn + offsets::m_iHealth);
		if (hp <= 0 || hp > 100)
			continue;
		pushFeet(pawn);
		regWeapon(pawn);
	}
}

inline bool IsPlausibleWeaponDefIndex(uint16_t d) {
	return d != 0 && d != 0xFFFFu && d <= 600;
}

/** m_iItemDefinitionIndex bos/kayik; VData m_szName ("weapon_usp_silencer") yedegi. */
inline uint16_t ReadWeaponDefIndexFromVData(uintptr_t wpn) {
	if (!wpn || !offsets::entity_m_nSubclassID)
		return 0;
	const uintptr_t vdata = g_GameMem.readv<uintptr_t>(
		wpn + static_cast<uintptr_t>(offsets::entity_m_nSubclassID) + 0x8);
	if (vdata < 0x10000ull)
		return 0;
	const uintptr_t sym = g_GameMem.readv<uintptr_t>(vdata + 0x720);
	if (sym < 0x10000ull)
		return 0;
	const std::string name = g_GameMem.ReadString(sym, 64);
	return DefIndexFromWeaponClassName(name.c_str());
}

inline uint16_t ReadEconItemDefIndex(uintptr_t wpn) {
	if (!wpn || !offsets::m_WeaponEcon_AttributeManager || !offsets::m_AttributeContainer_Item ||
		!offsets::m_EconItemView_ItemDefinitionIndex)
		return 0;
	const uintptr_t idxAddr = wpn + static_cast<uintptr_t>(offsets::m_WeaponEcon_AttributeManager +
		offsets::m_AttributeContainer_Item + offsets::m_EconItemView_ItemDefinitionIndex);
	return g_GameMem.readv<uint16_t>(idxAddr);
}

inline uint16_t ReadWeaponDefIndex(uintptr_t pawn) {
	if (!pawn || !offsets::m_pWeaponServices || !offsets::m_hActiveWeapon)
		return 0;
	const uintptr_t ws = g_GameMem.readv<uintptr_t>(pawn + static_cast<uintptr_t>(offsets::m_pWeaponServices));
	if (!ws)
		return 0;
	const uint32_t hw = g_GameMem.readv<uint32_t>(ws + static_cast<uintptr_t>(offsets::m_hActiveWeapon));
	const uintptr_t wpn = ex_entity::ResolveHandle(hw);
	if (!wpn)
		return 0;
	const uint16_t econ = ReadEconItemDefIndex(wpn);
	if (IsPlausibleWeaponDefIndex(econ))
		return econ;
	return ReadWeaponDefIndexFromVData(wpn);
}

/** Aktif silah entity'si zaten cozulduyse (grenade helper); pawn zinciri basarisiz olsa da calisir. */
inline uint16_t ReadWeaponDefIndexFromWeaponEntity(uintptr_t wpn) {
	if (!wpn)
		return 0;
	const uint16_t econ = ReadEconItemDefIndex(wpn);
	if (IsPlausibleWeaponDefIndex(econ))
		return econ;
	return ReadWeaponDefIndexFromVData(wpn);
}

/**
 * m_hMyWeapons — ananbaban-stable Entity.cpp GetWeaponInventory ile ayni yerlesim:
 * sayim vector tabaninda +0, handle dizisi isaretcisi +8 (CUtlVector {size; pad/data @8}).
 */
inline bool PawnInventoryContainsWeaponDef(uintptr_t pawn, uint16_t wantDef) {
	if (!pawn || wantDef == 0)
		return false;
	if (!offsets::m_pWeaponServices || !offsets::m_WeaponEcon_AttributeManager ||
	    !offsets::m_AttributeContainer_Item || !offsets::m_EconItemView_ItemDefinitionIndex)
		return ReadWeaponDefIndex(pawn) == wantDef;
	const uintptr_t ws = g_GameMem.readv<uintptr_t>(pawn + static_cast<uintptr_t>(offsets::m_pWeaponServices));
	if (!ws)
		return ReadWeaponDefIndex(pawn) == wantDef;
	if (offsets::m_hMyWeapons) {
		const uintptr_t vec = ws + static_cast<uintptr_t>(offsets::m_hMyWeapons);
		const int sizeDb = g_GameMem.readv<int>(vec);
		const uintptr_t dataDb = g_GameMem.readv<uintptr_t>(vec + 8);
		if (dataDb >= 0x10000 && sizeDb > 0 && sizeDb <= 64) {
			for (int i = 0; i < sizeDb; ++i) {
				const uint32_t h = g_GameMem.readv<uint32_t>(
					dataDb + static_cast<uintptr_t>(static_cast<unsigned>(i) * 4u));
				const uintptr_t wpn = ex_entity::ResolveHandle(h);
				if (!wpn)
					continue;
				if (ReadWeaponDefIndexFromWeaponEntity(wpn) == wantDef)
					return true;
			}
			return false;
		}
		/** Alternatif layout (eski tahmin): ptr +0, count +0x10 */
		const uintptr_t dataAlt = g_GameMem.readv<uintptr_t>(vec);
		const int sizeAlt = g_GameMem.readv<int>(vec + 0x10);
		if (dataAlt >= 0x10000 && sizeAlt > 0 && sizeAlt <= 64) {
			for (int i = 0; i < sizeAlt; ++i) {
				const uint32_t h = g_GameMem.readv<uint32_t>(
					dataAlt + static_cast<uintptr_t>(static_cast<unsigned>(i) * 4u));
				const uintptr_t wpn = ex_entity::ResolveHandle(h);
				if (!wpn)
					continue;
				if (ReadWeaponDefIndexFromWeaponEntity(wpn) == wantDef)
					return true;
			}
			return false;
		}
	}
	return ReadWeaponDefIndex(pawn) == wantDef;
}

/** Gecerli CS2 item index araligi (0xFFFF bos). */
inline bool IsPlausibleDroppedWeaponDef(uint16_t d) {
	if (d == 0 || d == 0xFFFFu || d > 600)
		return false;
	return true;
}

/** StrStr benzeri; haystack icinde needle (ASCII buyuk/kucuk harf duyumsuz). */
inline bool AsciiHaystackContainsInsensitive(const char* hay, const char* needle) {
	if (!hay || !needle || !needle[0])
		return false;
	for (; *hay; ++hay) {
		const char* hp = hay;
		const char* np = needle;
		while (*hp && *np) {
			unsigned char a = static_cast<unsigned char>(*hp++);
			unsigned char b = static_cast<unsigned char>(*np++);
			if (a >= 'A' && a <= 'Z')
				a = static_cast<unsigned char>(a + 32u);
			if (b >= 'A' && b <= 'Z')
				b = static_cast<unsigned char>(b + 32u);
			if (a != b)
				break;
		}
		if (!*np)
			return true;
	}
	return false;
}

/**
 * ent + m_Econ_AttributeManager... okumasi yanlis sinifta anlamdisiz VA ve IOCTL spam demek;
 * yalnizca sema adinda silah ipucu varsa yapilir.
 */
inline bool SchemaAllowsDroppedEconReadOnEntity(const char* schemaCn) {
	if (!schemaCn || !schemaCn[0])
		return false;
	return AsciiHaystackContainsInsensitive(schemaCn, "weapon") ||
		AsciiHaystackContainsInsensitive(schemaCn, "csweapon") ||
		AsciiHaystackContainsInsensitive(schemaCn, "cs2weapon") ||
		AsciiHaystackContainsInsensitive(schemaCn, "gun") ||
		AsciiHaystackContainsInsensitive(schemaCn, "knife") ||
		AsciiHaystackContainsInsensitive(schemaCn, "melee") ||
		AsciiHaystackContainsInsensitive(schemaCn, "c4");
}

/**
 * Yer silahi: once C_BasePlayerWeapon yolu (m_WeaponEcon...), olmazsa (sinirli) C_EconEntity yolu.
 * schemaCn: ReadEntitySchemaClassName ciktisi; yoksa econ fallback kullanilmaz (surucuye gereksiz okuma gitmez).
 */
inline uint16_t ReadWeaponDefIndexDropped(uintptr_t ent, const char* schemaCn) {
	if (!ent)
		return 0;
	uint16_t d = ReadWeaponDefIndexFromWeaponEntity(ent);
	if (IsPlausibleDroppedWeaponDef(d))
		return d;
	if (!SchemaAllowsDroppedEconReadOnEntity(schemaCn))
		return 0;
	std::ptrdiff_t econAm = offsets::m_Econ_AttributeManager;
	if (!econAm)
		econAm = offsets::m_WeaponEcon_AttributeManager;
	if (!econAm || !offsets::m_AttributeContainer_Item || !offsets::m_EconItemView_ItemDefinitionIndex)
		return 0;
	const uintptr_t idxAddr = ent + static_cast<uintptr_t>(econAm +
		offsets::m_AttributeContainer_Item + offsets::m_EconItemView_ItemDefinitionIndex);
	d = g_GameMem.readv<uint16_t>(idxAddr);
	if (!IsPlausibleDroppedWeaponDef(d))
		return 0;
	return d;
}

inline int ReadClipFromActiveWeapon(uintptr_t pawn) {
	if (!offsets::m_iClip1)
		return -1;
	const uintptr_t ws = g_GameMem.readv<uintptr_t>(pawn + static_cast<uintptr_t>(offsets::m_pWeaponServices));
	if (!ws)
		return -1;
	const uint32_t hw = g_GameMem.readv<uint32_t>(ws + static_cast<uintptr_t>(offsets::m_hActiveWeapon));
	const uintptr_t wpn = ex_entity::ResolveHandle(hw);
	if (!wpn)
		return -1;
	return g_GameMem.readv<int>(wpn + static_cast<uintptr_t>(offsets::m_iClip1));
}

inline bool IsSniperDef(uint16_t d) {
	return d == 9 || d == 40 || d == 38 || d == 11;
}

/** Silah def index icin kucuk ESP ikon kutusu rengi (texture yokken). */
inline ImU32 WeaponCategoryTintU32(uint16_t id) {
	switch (id) {
	case 49: return IM_COL32(255, 95, 75, 255);
	case 31: return IM_COL32(110, 210, 255, 255);
	case 42:
	case 59: return IM_COL32(170, 175, 190, 255);
	case 9:
	case 11:
	case 38:
	case 40: return IM_COL32(255, 185, 90, 255);
	case 14:
	case 28: return IM_COL32(210, 145, 75, 255);
	case 25:
	case 27:
	case 29:
	case 35: return IM_COL32(130, 200, 120, 255);
	case 17:
	case 19:
	case 23:
	case 24:
	case 33:
	case 34: return IM_COL32(200, 130, 255, 255);
	case 1:
	case 2:
	case 3:
	case 4:
	case 30:
	case 32:
	case 36:
	case 61:
	case 63:
	case 64: return IM_COL32(140, 190, 255, 255);
	case 7:
	case 8:
	case 10:
	case 13:
	case 16:
	case 39:
	case 60: return IM_COL32(120, 230, 160, 255);
	case 43:
	case 44:
	case 45:
	case 46:
	case 47:
	case 48: return IM_COL32(255, 220, 100, 255);
	default: return IM_COL32(185, 185, 210, 255);
	}
}

inline void StrokeTextBg(ImDrawList* dl, const char* txt, float x, float y, ImU32 col) {
	const ImVec2 ts = ImGui::CalcTextSize(txt);
	const float tx = x - ts.x * 0.5f;
	for (int ox = -1; ox <= 1; ++ox)
		for (int oy = -1; oy <= 1; ++oy)
			if (ox || oy)
				dl->AddText(ImVec2(tx + (float)ox, y + (float)oy), IM_COL32(0, 0, 0, 220), txt);
	dl->AddText(ImVec2(tx, y), col, txt);
}

/** StrokeTextBg ile ayni golge; x sol kenar (ESP sag sutun hizasi). */
inline void StrokeTextBgLeft(ImDrawList* dl, const char* txt, float leftX, float y, ImU32 col) {
	const ImVec2 ts = ImGui::CalcTextSize(txt);
	for (int ox = -1; ox <= 1; ++ox)
		for (int oy = -1; oy <= 1; ++oy)
			if (ox || oy)
				dl->AddText(ImVec2(leftX + (float)ox, y + (float)oy), IM_COL32(0, 0, 0, 220), txt);
	dl->AddText(ImVec2(leftX, y), col, txt);
}

inline void DrawSniperCrosshairCenter(ImU32 col, float gap, float arm, float thick) {
	const ImGuiIO& io = ImGui::GetIO();
	const float cx = io.DisplaySize.x * 0.5f;
	const float cy = io.DisplaySize.y * 0.5f;
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	dl->AddRectFilled(ImVec2(cx - gap - arm, cy - thick), ImVec2(cx - gap, cy + thick), col);
	dl->AddRectFilled(ImVec2(cx + gap, cy - thick), ImVec2(cx + gap + arm, cy + thick), col);
	dl->AddRectFilled(ImVec2(cx - thick, cy - gap - arm), ImVec2(cx + thick, cy - gap), col);
	dl->AddRectFilled(ImVec2(cx - thick, cy + gap), ImVec2(cx + thick, cy + gap + arm), col);
}

inline void DrawEyeRay(const view_matrix_t& vm, const Vector3& headWorld, const Vector3& angDeg,
	float lengthWorld, ImU32 col, float thick) {
	const float d2r = static_cast<float>(M_PI) / 180.f;
	const float px = angDeg.y * d2r;
	const float py = angDeg.x * d2r;
	const float lineLen = std::cos(py) * lengthWorld;
	Vector3 end{};
	end.x = headWorld.x + std::cos(px) * lineLen;
	end.y = headWorld.y + std::sin(px) * lineLen;
	end.z = headWorld.z - std::sin(py) * lengthWorld;
	Vector3 s0, s1;
	if (!w2s(headWorld, s0, vm) || !w2s(end, s1, vm))
		return;
	if (s0.z < 0.01f || s1.z < 0.01f)
		return;
	const ImVec2 es = EspLayout::Get(EspLayout::Eye);
	ImGui::GetBackgroundDrawList()->AddLine(ImVec2(s0.x + es.x, s0.y + es.y), ImVec2(s1.x + es.x, s1.y + es.y), col, thick);
}

/** Radar/world ile ayni: CS2'de pozisyon genelde GameSceneNode->m_vecAbsOrigin; degilse m_vecOrigin. */
inline Vector3 ReadWorldPositionFromEntity(uintptr_t entity) {
	if (!entity)
		return {};
	if (offsets::m_pGameSceneNode && offsets::m_vecAbsOrigin) {
		const uintptr_t sn = g_GameMem.readv<uintptr_t>(entity + static_cast<uintptr_t>(offsets::m_pGameSceneNode));
		if (sn)
			return g_GameMem.readv<Vector3>(sn + static_cast<uintptr_t>(offsets::m_vecAbsOrigin));
	}
	if (offsets::m_vecOrigin)
		return g_GameMem.readv<Vector3>(entity + static_cast<uintptr_t>(offsets::m_vecOrigin));
	if (offsets::m_vecAbsOrigin)
		return g_GameMem.readv<Vector3>(entity + static_cast<uintptr_t>(offsets::m_vecAbsOrigin));
	return {};
}

/** C_BaseEntity::m_hOwnerEntity — silah/C4 entity tasiyan pawn (cozulmus adres). */
inline uintptr_t ResolveOwnerEntityPtr(uintptr_t ent) {
	if (!ent || !offsets::m_hOwnerEntity)
		return 0;
	const uint32_t h = g_GameMem.readv<uint32_t>(ent + static_cast<uintptr_t>(offsets::m_hOwnerEntity));
	return ex_entity::ResolveHandle(h);
}

inline void DrawAmmoBarH(uintptr_t key, int clip, int reserveGuess, float x, float y, float w, float h) {
	if (clip < 0)
		return;
	const int mx = (std::max)(reserveGuess, clip);
	if (mx <= 0)
		return;
	const float p = std::clamp(clip / (float)mx, 0.f, 1.f);
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	const ImVec2 a(x, y);
	const ImVec2 b(x + w, y + h);
	dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 200), 2.f);
	dl->AddRectFilled(a, ImVec2(x + w * p, y + h), IM_COL32(240, 220, 60, 255), 2.f);
	dl->AddRect(a, b, IM_COL32(60, 60, 60, 255), 2.f, 0, 1.f);
}

inline void DrawFilledBehindBox(float x, float y, float w, float h, float rounding, bool gradient, bool visFill, bool visibleOk) {
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	const ImVec2 mn(x, y);
	const ImVec2 mx(x + w, y + h);
	if (Settings::Visuals::filledVisBox && visibleOk) {
		const ImU32 vc = IM_COL32(
			(int)(EspUiColors::vis_espcol[0] * 255.f),
			(int)(EspUiColors::vis_espcol[1] * 255.f),
			(int)(EspUiColors::vis_espcol[2] * 255.f), 90);
		dl->AddRectFilled(mn, mx, vc, rounding);
		return;
	}
	if (gradient) {
		const ImU32 c0 = IM_COL32(
			(int)(EspUiColors::esp_fill_col[0] * 255.f),
			(int)(EspUiColors::esp_fill_col[1] * 255.f),
			(int)(EspUiColors::esp_fill_col[2] * 255.f), 110);
		const ImU32 c1 = IM_COL32(
			(int)(EspUiColors::esp_fill2_col[0] * 255.f),
			(int)(EspUiColors::esp_fill2_col[1] * 255.f),
			(int)(EspUiColors::esp_fill2_col[2] * 255.f), 110);
		dl->AddRectFilledMultiColor(mn, mx, c0, c0, c1, c1);
	} else {
		const ImU32 c = IM_COL32(
			(int)(EspUiColors::esp_fill_col[0] * 255.f),
			(int)(EspUiColors::esp_fill_col[1] * 255.f),
			(int)(EspUiColors::esp_fill_col[2] * 255.f), 100);
		dl->AddRectFilled(mn, mx, c, rounding);
	}
}

inline void DrawSnapLine(float cxBox, float yTop, float yBot, ImU32 col, float thick) {
	const ImGuiIO& io = ImGui::GetIO();
	ImVec2 start;
	ImVec2 end;
	const ImVec2 ss = EspLayout::Get(EspLayout::Snap);
	switch (Settings::Visuals::snaplineMode) {
	case 1:
		start = ImVec2(cxBox + ss.x, yBot + ss.y);
		end = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
		break;
	case 2:
		start = ImVec2(cxBox + ss.x, yTop + ss.y);
		end = ImVec2(io.DisplaySize.x * 0.5f, 0.f);
		break;
	default:
		start = ImVec2(cxBox + ss.x, yBot + ss.y);
		end = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y);
		break;
	}
	ImGui::GetBackgroundDrawList()->AddLine(start, end, col, thick);
}

} // namespace ex_esp
