#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace steam_ws {

struct WorkshopAddon {
	std::string addon_id;  
	std::wstring path;     
};

std::vector<std::wstring> FindSteamInstallPaths();

std::vector<std::wstring> ParseSteamLibraryFolders(const std::wstring& steam_install);

std::vector<WorkshopAddon> EnumerateCs2WorkshopAddons();

std::vector<std::wstring> FindAllWorkshopContent730Roots();

}  
