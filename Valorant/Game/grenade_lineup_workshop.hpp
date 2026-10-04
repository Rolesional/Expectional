#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "structs.hpp"

namespace grenade_lineup_workshop {

struct ParsedRow {
	std::string map;
	std::string name;
	std::string desc;
	UE4Structs::Vector3 stand{};
	UE4Structs::Vector3 angles{};
	UE4Structs::Vector3 target{};
	
	UE4Structs::Vector3 label_world{};
	
	std::uint8_t label_h_align = 0;
	int throw_idx = 0;
	int nade_kind = 0;
	
	std::string source_pack_id;
	
	std::string source_pack_title;
};

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

void AppendWorkshopKv3FromDirectory(const std::wstring& dir_wide, std::vector<ParsedRow>& out);

void AppendWorkshopKv3FromDirectoryRecursive(const std::wstring& dir_wide, std::vector<ParsedRow>& out);

void AppendWorkshopKv3FromAddonVpk(const std::wstring& addon_dir_wide, std::vector<ParsedRow>& out);

void AppendAllFromWorkshopContent730(const std::wstring& ws730_root_wide, std::vector<ParsedRow>& out);

} 
