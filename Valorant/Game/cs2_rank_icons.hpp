#pragma once

struct ID3D11Device;
struct ID3D11ShaderResourceView;

/** GitHub SVG indirme + rasterize bittiğinde SRV serbest bırakma (opsiyonel). */
void ExpectionalRankIconsShutdown();

/**
 * Her kare (Rank Revealer çizilirken) çağırın. İlk açılışta arka planda
 * itzarty/csgo-rank-icons matchmaking + wingman SVG'leri indirilir.
 */
void ExpectionalRankIconsFrame(ID3D11Device* device);

/**
 * @param wingman true → `wingman/` klasörü, false → `matchmaking/`
 * @param sub 0..18 → N.svg; 19 → none.svg; 20 → expired.svg
 * @return ImGui::Image için ImTextureID; yoksa nullptr (yükleniyor veya hata)
 */
void* ExpectionalRankIconTexture(bool wingman, int sub);
