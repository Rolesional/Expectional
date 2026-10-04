#pragma once
/**
 * Steam install + workshop addon kesfi.
 *
 * Otomatik:
 *   - Steam kurulum dizini (registry + tum sabit disklerde yaygin yollar)
 *   - libraryfolders.vdf icindeki tum kutuphaneler (multi-disk Steam)
 *   - SteamLibrary / ozel kurulum yollarinda dogrudan steamapps kesfi
 *   - Her kutuphane icin steamapps\workshop\content\730\<addon_id>\ taranir
 *
 * Kullanici manuel dosya tasimaz; CS2 workshop addonlari oldugu yerden okunur.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace steam_ws {

struct WorkshopAddon {
	std::string addon_id;  /**< Workshop addon ID (klasor adi). */
	std::wstring path;     /**< Tam yol. */
};

/** Steam install path'lerini bul (genelde 1; ana kurulum). */
std::vector<std::wstring> FindSteamInstallPaths();

/** Bir Steam install icindeki tum library folders (libraryfolders.vdf'den). */
std::vector<std::wstring> ParseSteamLibraryFolders(const std::wstring& steam_install);

/** CS2 (app 730) icin tum library'lerdeki workshop addonlarini listele. */
std::vector<WorkshopAddon> EnumerateCs2WorkshopAddons();

/** Tum Steam kutuphanelerindeki ...\workshop\content\730 kok dizinleri. */
std::vector<std::wstring> FindAllWorkshopContent730Roots();

}  // namespace steam_ws
