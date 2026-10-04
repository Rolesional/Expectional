#pragma once

#include "structs.hpp"
#include "globals.hpp"
#include "weapon_icon_glyphs.hpp"
#include "weapon_names.hpp"
#include "weapon_knife_icons.hpp"
#include "esp_extras.hpp"
#include "../../Includes/Imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <cstdio>

bool w2s(const UE4Structs::Vector3& pos, UE4Structs::Vector3& out, UE4Structs::view_matrix_t matrix);

namespace catalyst_esp {

struct ScreenBounds {
	float minX = 0.f, minY = 0.f, maxX = 0.f, maxY = 0.f;
	bool valid = false;
	float width() const { return maxX - minX; }
	float height() const { return maxY - minY; }
};

struct DrawOffsets {
	float left = 0.f, top = 0.f, bottom = 0.f, right = 0.f;
};

inline bool ProjectionOk(const UE4Structs::Vector3& sp) {
	return sp.z >= 0.01f;
}

template<typename ReadBoneFn>
inline ScreenBounds ComputeScreenBounds(const ReadBoneFn& readBone, const UE4Structs::view_matrix_t& vm) {
	static constexpr int kBoneIds[] = {
		1, 3, 4, 6, 7, 9, 10, 11, 13, 14, 15, 17, 18, 19, 20, 21, 22, 23
	};
	constexpr float width_offset = 7.5f;
	constexpr float height_offset = 9.0f;

	float sminX = FLT_MAX, sminY = FLT_MAX, smaxX = -FLT_MAX, smaxY = -FLT_MAX;
	bool any = false;

	for (int boneIdx : kBoneIds) {
		const UE4Structs::Vector3 bone_pos = readBone(boneIdx);
		if (bone_pos.x == 0.f && bone_pos.y == 0.f && bone_pos.z == 0.f)
			continue;

		const UE4Structs::Vector3 expanded[] = {
			bone_pos,
			{ bone_pos.x + width_offset, bone_pos.y, bone_pos.z },
			{ bone_pos.x - width_offset, bone_pos.y, bone_pos.z },
			{ bone_pos.x, bone_pos.y + width_offset, bone_pos.z },
			{ bone_pos.x, bone_pos.y - width_offset, bone_pos.z },
			{ bone_pos.x, bone_pos.y, bone_pos.z + height_offset },
			{ bone_pos.x, bone_pos.y, bone_pos.z - height_offset },
			{ bone_pos.x + width_offset, bone_pos.y + width_offset, bone_pos.z },
			{ bone_pos.x - width_offset, bone_pos.y - width_offset, bone_pos.z },
		};

		for (const UE4Structs::Vector3& ep : expanded) {
			UE4Structs::Vector3 sp{};
			if (!w2s(ep, sp, vm) || !ProjectionOk(sp))
				continue;
			any = true;
			sminX = (std::min)(sminX, sp.x);
			sminY = (std::min)(sminY, sp.y);
			smaxX = (std::max)(smaxX, sp.x);
			smaxY = (std::max)(smaxY, sp.y);
		}
	}

	ScreenBounds out{};
	if (!any)
		return out;
	out.minX = sminX;
	out.minY = sminY;
	out.maxX = smaxX;
	out.maxY = smaxY;
	out.valid = true;
	return out;
}

inline void AddRectCornered(ImDrawList* dl, float x, float y, float w, float h, ImU32 col,
	float cornerMax, float thick) {
	const float c = (std::min)(cornerMax, (std::min)(w, h) * 0.4f);
	dl->AddLine(ImVec2(x, y), ImVec2(x + c, y), col, thick);
	dl->AddLine(ImVec2(x, y), ImVec2(x, y + c), col, thick);
	dl->AddLine(ImVec2(x + w, y), ImVec2(x + w - c, y), col, thick);
	dl->AddLine(ImVec2(x + w, y), ImVec2(x + w, y + c), col, thick);
	dl->AddLine(ImVec2(x + w, y + h), ImVec2(x + w - c, y + h), col, thick);
	dl->AddLine(ImVec2(x + w, y + h), ImVec2(x + w, y + h - c), col, thick);
	dl->AddLine(ImVec2(x, y + h), ImVec2(x + c, y + h), col, thick);
	dl->AddLine(ImVec2(x, y + h), ImVec2(x, y + h - c), col, thick);
}

inline void DrawCatalystBox(ImDrawList* dl, float x, float y, float w, float h, ImU32 color,
	bool wantFill, bool outline, int boxMode) {
	const ImU32 black180 = IM_COL32(0, 0, 0, 180);
	const ImU32 black200 = IM_COL32(0, 0, 0, 200);
	const float cornerMax = 10.f;
	const bool fullStyle = (boxMode == 1 || boxMode == 3);

	if (wantFill && w > 4.f && h > 4.f) {
		const ImVec4 cf = ImGui::ColorConvertU32ToFloat4(color);
		const float r = cf.x, g = cf.y, b = cf.z;
		const float avg = (r + g + b) / 3.f;
		constexpr float desat = 0.7f;
		const float er = r * desat + avg * (1.f - desat);
		const float eg = g * desat + avg * (1.f - desat);
		const float eb = b * desat + avg * (1.f - desat);

		const ImU32 edge = IM_COL32(
			(int)(er * 255.f), (int)(eg * 255.f), (int)(eb * 255.f), (int)(255 * 0.5f));
		const ImU32 center = IM_COL32(
			(int)(er * 255.f * 0.4f), (int)(eg * 255.f * 0.4f), (int)(eb * 255.f * 0.4f), (int)(255 * 0.08f));

		if (Settings::Visuals::filledGradient) {
			const ImU32 c0 = IM_COL32(
				(int)(EspUiColors::esp_fill_col[0] * 255.f),
				(int)(EspUiColors::esp_fill_col[1] * 255.f),
				(int)(EspUiColors::esp_fill_col[2] * 255.f), 100);
			const ImU32 c1 = IM_COL32(
				(int)(EspUiColors::esp_fill2_col[0] * 255.f),
				(int)(EspUiColors::esp_fill2_col[1] * 255.f),
				(int)(EspUiColors::esp_fill2_col[2] * 255.f), 100);
			dl->AddRectFilledMultiColor(ImVec2(x + 1, y + 1), ImVec2(x + w - 1, y + h - 1), c0, c0, c1, c1);
		} else {
			const float midY = y + h * 0.5f;
			dl->AddRectFilledMultiColor(ImVec2(x + 1, y + 1), ImVec2(x + w - 1, midY - 1),
				edge, edge, center, center);
			dl->AddRectFilledMultiColor(ImVec2(x + 1, midY), ImVec2(x + w - 1, y + h - 1),
				center, center, edge, edge);
		}
	}

	if (fullStyle) {
		if (outline) {
			dl->AddRect(ImVec2(x - 1.f, y - 1.f), ImVec2(x + w + 1.f, y + h + 1.f), black180, 0.f, 0, 1.f);
			dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), black200, 0.f, 0, 2.f);
		}
		dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), color, 0.f, 0, 1.f);
	} else {
		if (outline) {
			AddRectCornered(dl, x - 1.f, y - 1.f, w + 2.f, h + 2.f, black180, cornerMax + 1.f, 1.f);
			AddRectCornered(dl, x, y, w, h, black200, cornerMax, 2.f);
		}
		AddRectCornered(dl, x, y, w, h, color, cornerMax, 1.f);
	}
}

inline void DrawHealthBarLeft(ImDrawList* dl, float boundsMinX, float boundsMinY, float boundsH,
	int hpClamped, DrawOffsets& off, bool showNumericOnBar) {
	constexpr float barSize = 3.5f;
	constexpr float padding = 4.f;
	constexpr float outlineSz = 1.f;

	const int hp = (std::clamp)(hpClamped, 0, 100);
	const float fraction = hp / 100.f;
	const float barW = barSize;
	const float barH = std::floor(boundsH);
	const float filled = std::floor(barH * fraction);

	const float x = std::floor(boundsMinX - barSize - padding - off.left - outlineSz);
	const float y = std::floor(boundsMinY);

	off.left += barSize + padding + outlineSz * 2.f;

	const ImU32 bg = IM_COL32(20, 20, 24, 230);
	const ImU32 outlineCol = IM_COL32(0, 0, 0, 240);
	const ImU32 full = ImGui::ColorConvertFloat4ToU32(ImVec4(
		EspUiColors::health_bar_high[0], EspUiColors::health_bar_high[1], EspUiColors::health_bar_high[2], 1.f));
	const ImU32 low = ImGui::ColorConvertFloat4ToU32(ImVec4(
		EspUiColors::health_bar_low[0], EspUiColors::health_bar_low[1], EspUiColors::health_bar_low[2], 1.f));

	dl->AddRectFilled(ImVec2(x - 1.f, y - 1.f), ImVec2(x + barW + 1.f, y + barH + 1.f), outlineCol);
	dl->AddRectFilled(ImVec2(x, y), ImVec2(x + barW, y + barH), bg);
	if (filled > 0.f)
		dl->AddRectFilledMultiColor(ImVec2(x, y + barH - filled), ImVec2(x + barW, y + barH),
			full, full, low, low);

	if (showNumericOnBar && hp > 0 && hp <= 100) {
		char t[8];
		snprintf(t, sizeof t, "%d", hp);
		const ImVec2 ts = ImGui::CalcTextSize(t);
		const float tx = std::floor(x + barW * 0.5f - ts.x * 0.5f);
		const float ty = std::floor(y + barH - filled - ts.y - 2.f);
		for (int ox = -1; ox <= 1; ++ox)
			for (int oy = -1; oy <= 1; ++oy)
				if (ox || oy)
					dl->AddText(ImVec2(tx + (float)ox, ty + (float)oy), IM_COL32(0, 0, 0, 220), t);
		const ImU32 valTxt = ImGui::ColorConvertFloat4ToU32(ImVec4(
			EspUiColors::health_bar_value_text[0], EspUiColors::health_bar_value_text[1],
			EspUiColors::health_bar_value_text[2], 1.f));
		dl->AddText(ImVec2(tx, ty), valTxt, t);
	}
}

inline void DrawAmmoBarBottom(ImDrawList* dl, float boundsMinX, float boundsMaxY, float boundsW,
	float boundsH, int clip, int maxAmmo, DrawOffsets& off, bool drawBar, bool drawText, ImU32 ammoTextCol) {
	if (clip < 0 || maxAmmo <= 0)
		return;
	if (!drawBar && !drawText)
		return;

	const int clamped = (std::clamp)(clip, 0, maxAmmo);
	char t[24];
	snprintf(t, sizeof t, "%d/%d", clamped, maxAmmo);
	const ImVec2 ts = ImGui::CalcTextSize(t);

	if (drawBar) {
		constexpr float barSize = 3.5f;
		constexpr float padding = 4.f;
		constexpr float outlineSz = 1.f;

		const float fraction = static_cast<float>(clamped) / static_cast<float>(maxAmmo);
		const float barW = std::floor(boundsW);
		const float barH = barSize;
		const float filled = std::floor(barW * fraction);

		const float x = std::floor(boundsMinX);
		const float y = std::floor(boundsMaxY + padding + off.bottom + outlineSz);

		off.bottom += barSize + padding + outlineSz * 2.f;

		const ImU32 bg = IM_COL32(22, 22, 28, 230);
		const ImU32 outlineCol = IM_COL32(0, 0, 0, 240);
		const ImU32 ammoGradLeft = ImGui::ColorConvertFloat4ToU32(ImVec4(
			EspUiColors::ammo_bar_hi[0], EspUiColors::ammo_bar_hi[1], EspUiColors::ammo_bar_hi[2], 1.f));
		const ImU32 ammoGradRight = ImGui::ColorConvertFloat4ToU32(ImVec4(
			EspUiColors::ammo_bar_lo[0], EspUiColors::ammo_bar_lo[1], EspUiColors::ammo_bar_lo[2], 1.f));

		dl->AddRectFilled(ImVec2(x - 1.f, y - 1.f), ImVec2(x + barW + 1.f, y + barH + 1.f), outlineCol);
		dl->AddRectFilled(ImVec2(x, y), ImVec2(x + barW, y + barH), bg);
		if (filled > 0.f)
			dl->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + filled, y + barH), ammoGradLeft, ammoGradRight,
				ammoGradRight, ammoGradLeft);

		if (drawText) {
			const float tx = std::floor(x + barW * 0.5f - ts.x * 0.5f);
			const float ty = std::floor(y + barH + 2.f);
			for (int ox = -1; ox <= 1; ++ox)
				for (int oy = -1; oy <= 1; ++oy)
					if (ox || oy)
						dl->AddText(ImVec2(tx + (float)ox, ty + (float)oy), IM_COL32(0, 0, 0, 220), t);
			dl->AddText(ImVec2(tx, ty), ammoTextCol, t);
			off.bottom += ts.y + 2.f;
		}
		return;
	}

	if (drawText) {
		const float midX = boundsMinX + boundsW * 0.5f;
		const float tx = std::floor(midX - ts.x * 0.5f);
		const float ty = std::floor(boundsMaxY + 4.f + off.bottom);
		for (int ox = -1; ox <= 1; ++ox)
			for (int oy = -1; oy <= 1; ++oy)
				if (ox || oy)
					dl->AddText(ImVec2(tx + (float)ox, ty + (float)oy), IM_COL32(0, 0, 0, 220), t);
		dl->AddText(ImVec2(tx, ty), ammoTextCol, t);
		off.bottom += ts.y + 4.f;
	}
}

inline float WeaponIconPixelHeight(float boxW) {
	float ih = 15.f;
	if (boxW > 1.f && boxW < 52.f)
		ih = 7.f + (boxW / 52.f) * 8.f;
	if (ih < 7.f)
		ih = 7.f;
	if (ih > 15.f)
		ih = 15.f;
	return ih;
}

inline float DroppedWeaponIconPixelHeight(float depth) {
	float ih = 16.f;
	if (depth > 500.f)
		ih = 16.f - (depth - 500.f) * (3.f / 2000.f);
	if (ih < 13.f)
		ih = 13.f;
	if (ih > 16.f)
		ih = 16.f;
	return ih;
}

inline void DrawWeaponGlyphAndOrText(ImDrawList* dl, float boundsMinX, float boundsMaxY, float boundsW,
	const char* weaponName, ImU32 col, DrawOffsets& off, bool wantIcon, bool wantText) {
	if ((!wantIcon && !wantText) || !weaponName || !weaponName[0])
		return;
	const char* shown = WeaponDisplayNameFromKey(weaponName);
	if (!shown || !shown[0])
		shown = weaponName;
	const float midX = boundsMinX + boundsW * 0.5f;
	float y = boundsMaxY + 2.f + off.bottom;
	float added = 0.f;

	void* knifeTex = nullptr;
	int knifeW = 0, knifeH = 0;
	if (wantIcon && ExpectionalKnifeIcon(weaponName, &knifeTex, &knifeW, &knifeH) && knifeTex && knifeW > 0 && knifeH > 0) {
		const float ih = WeaponIconPixelHeight(boundsW);
		const float iw = ih * static_cast<float>(knifeW) / static_cast<float>(knifeH);
		const float gx = std::floor(midX - iw * 0.5f);
		const float gy = std::floor(y);
		const ImVec2 a(gx, gy);
		const ImVec2 b(gx + iw, gy + ih);
		const ImTextureID id = reinterpret_cast<ImTextureID>(knifeTex);
		const ImVec2 nudge[4] = { ImVec2(-1.f, 0.f), ImVec2(1.f, 0.f), ImVec2(0.f, -1.f), ImVec2(0.f, 1.f) };
		for (const ImVec2& n : nudge)
			dl->AddImage(id, ImVec2(a.x + n.x, a.y + n.y), ImVec2(b.x + n.x, b.y + n.y),
				ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), IM_COL32(0, 0, 0, 220));
		dl->AddImage(id, a, b, ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), col);
		y += ih + 2.f;
		added += ih + 2.f;
	} else if (wantIcon && g_WeaponsIconFont) {
		const char* glyph = WeaponIconGlyphForInternalName(weaponName);
		const float fs = WeaponIconPixelHeight(boundsW);
		const ImVec2 gs = g_WeaponsIconFont->CalcTextSizeA(fs, FLT_MAX, 0.f, glyph);
		const float gx = std::floor(midX - gs.x * 0.5f);
		const float gy = std::floor(y);
		for (int ox = -1; ox <= 1; ++ox)
			for (int oy = -1; oy <= 1; ++oy)
				if (ox || oy)
					dl->AddText(g_WeaponsIconFont, fs, ImVec2(gx + (float)ox, gy + (float)oy), IM_COL32(0, 0, 0, 220), glyph);
		dl->AddText(g_WeaponsIconFont, fs, ImVec2(gx, gy), col, glyph);
		y += gs.y + 2.f;
		added += gs.y + 2.f;
	}

	if (wantText) {
		const ImVec2 wsz = ImGui::CalcTextSize(shown);
		const float cx = std::floor(midX - wsz.x * 0.5f);
		const float textY = std::floor(y);
		for (int ox = -1; ox <= 1; ++ox)
			for (int oy = -1; oy <= 1; ++oy)
				if (ox || oy)
					dl->AddText(ImVec2(cx + (float)ox, textY + (float)oy), IM_COL32(0, 0, 0, 220), shown);
		dl->AddText(ImVec2(cx, textY), col, shown);
		off.bottom += added + wsz.y + 2.f;
	} else {
		off.bottom += added;
	}
}

inline void DrawDroppedWeaponWorldLabel(ImDrawList* dl, float sx, float sy, const char* weaponName, ImU32 col, float depth = 0.f) {
	if (!weaponName || !weaponName[0])
		return;
	const char* shown = WeaponDisplayNameFromKey(weaponName);
	if (!shown || !shown[0])
		shown = weaponName;
	const bool text = Settings::Visuals::droppedWeaponText;
	void* knifeTex = nullptr;
	int knifeW = 0, knifeH = 0;
	const bool knifeIcon = ExpectionalKnifeIcon(weaponName, &knifeTex, &knifeW, &knifeH) && knifeTex && knifeW > 0 && knifeH > 0;
	const bool icon = Settings::Visuals::droppedWeaponIcons && (knifeIcon || g_WeaponsIconFont);
	if (!text && !icon) {
		dl->AddCircleFilled(ImVec2(sx, sy), 5.f, col, 14);
		return;
	}

	float y = sy;
	if (knifeIcon) {
		const float ih = DroppedWeaponIconPixelHeight(depth);
		const float iw = ih * static_cast<float>(knifeW) / static_cast<float>(knifeH);
		const float gx = std::floor(sx - iw * 0.5f);
		const float gy = std::floor(y);
		const ImTextureID id = reinterpret_cast<ImTextureID>(knifeTex);
		const ImVec2 a(gx, gy);
		const ImVec2 b(gx + iw, gy + ih);
		const ImVec2 nudge[4] = { ImVec2(-1.f, 0.f), ImVec2(1.f, 0.f), ImVec2(0.f, -1.f), ImVec2(0.f, 1.f) };
		for (const ImVec2& n : nudge)
			dl->AddImage(id, ImVec2(a.x + n.x, a.y + n.y), ImVec2(b.x + n.x, b.y + n.y),
				ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), IM_COL32(0, 0, 0, 220));
		dl->AddImage(id, a, b, ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), col);
		y += ih + 2.f;
	} else if (icon) {
		const char* glyph = WeaponIconGlyphForInternalName(weaponName);
		const float fs = DroppedWeaponIconPixelHeight(depth);
		const ImVec2 gs = g_WeaponsIconFont->CalcTextSizeA(fs, FLT_MAX, 0.f, glyph);
		const float gx = std::floor(sx - gs.x * 0.5f);
		const float gy = std::floor(y);
		for (int ox = -1; ox <= 1; ++ox)
			for (int oy = -1; oy <= 1; ++oy)
				if (ox || oy)
					dl->AddText(g_WeaponsIconFont, fs, ImVec2(gx + (float)ox, gy + (float)oy), IM_COL32(0, 0, 0, 220), glyph);
		dl->AddText(g_WeaponsIconFont, fs, ImVec2(gx, gy), col, glyph);
		y += gs.y + 2.f;
	}
	if (text)
		ex_esp::StrokeTextBg(dl, shown, sx, y, col);
}

} 
