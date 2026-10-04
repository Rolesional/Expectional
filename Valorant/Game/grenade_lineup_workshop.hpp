#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "structs.hpp"

namespace grenade_lineup_workshop {

/** Workshop KV3 satiri; grenade_lineup icinde LineupNadeKind ile ayni sayisal sira (0=smoke..4=decoy). */
struct ParsedRow {
	std::string map;
	std::string name;
	std::string desc;
	UE4Structs::Vector3 stand{};
	UE4Structs::Vector3 angles{};
	UE4Structs::Vector3 target{};
	/** Title/Desc panel anchor: main Position + TextPositionOffset (CS2 MapAnnotation). */
	UE4Structs::Vector3 label_world{};
	/** 0 center, 1 left, 2 right — TextHorizontalAlign. */
	std::uint8_t label_h_align = 0;
	int throw_idx = 0;
	int nade_kind = 0;
	/** Kaynak paket kimligi (dosya yolu UTF-8). Browser packet bazinda gruplar. */
	std::string source_pack_id;
	/** Paket goruntuleme adi: KV3 'Title'/'GroupName'/dosya adi. */
	std::string source_pack_title;
};

/** prac_mirage -> de_mirage gibi workshop map adlarini oyundaki isimle eslestirir. */
inline std::string NormalizeWorkshopMapName(std::string map)
{
	while (!map.empty() && (unsigned char)map.front() <= ' ')
		map.erase(0, 1);
	while (!map.empty() && (unsigned char)map.back() <= ' ')
		map.pop_back();
	if (map.rfind("prac_", 0) == 0 && map.size() > 5)
		return "de_" + map.substr(5);
	return map;
}

/** dir_wide altindaki .txt dosyalarini tarar; KV3 MapAnnotationNode icerenleri cozup out'a ekler. */
void AppendWorkshopKv3FromDirectory(const std::wstring& dir_wide, std::vector<ParsedRow>& out);

/** Recursive: alt dizinleri de tara (CS2 workshop addonlari ic ic dizinler kullanabilir). */
void AppendWorkshopKv3FromDirectoryRecursive(const std::wstring& dir_wide, std::vector<ParsedRow>& out);

/** Workshop addon klasorundeki *_dir.vpk icinden annotations/*.txt lineup KV3 okur. */
void AppendWorkshopKv3FromAddonVpk(const std::wstring& addon_dir_wide, std::vector<ParsedRow>& out);

/**
 * workshop\content\730 kokunu (alt klasorler dahil) tarar.
 * publish_data basligi + source_folder (or. miragelineups) + VPK icindeki MapAnnotationNode.
 */
void AppendAllFromWorkshopContent730(const std::wstring& ws730_root_wide, std::vector<ParsedRow>& out);

} // namespace grenade_lineup_workshop
