#pragma once

struct ImDrawList;
struct ImVec2;

/** CS2 minimap (CCSGO_HudRadar) ile ayni dunya->radar projeksiyonu; bellek okuma + sabit varsayilan HUD convar'lari. */
namespace external_hud_radar {

/** Gercek minimap durumunu oku; false ise eski harita/planar yola dus. */
bool TickFromMemory();

/**
 * HUD modunda tum blip'leri ciz. true: tamamlandi (normal radar dallarini atla).
 * rmin/rmax: ImGui radar dikdortgeni; mapW/mapH ile ayni clip.
 */
bool TryDrawFrame(ImDrawList* dl, const ImVec2& rmin, const ImVec2& rmax, float mapW, float mapH, bool showDebugHud);

} // namespace external_hud_radar
