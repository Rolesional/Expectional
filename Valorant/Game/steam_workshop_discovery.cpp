#include "steam_workshop_discovery.hpp"
#include "expectional_winio.hpp"

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_set>

namespace steam_ws {
namespace {

static std::wstring ReadRegistryStringW(HKEY root, const wchar_t* subkey, const wchar_t* value)
{
	HKEY h{};
	if (RegOpenKeyExW(root, subkey, 0, KEY_READ | KEY_WOW64_64KEY, &h) != ERROR_SUCCESS) {
		if (RegOpenKeyExW(root, subkey, 0, KEY_READ | KEY_WOW64_32KEY, &h) != ERROR_SUCCESS)
			return {};
	}
	wchar_t buf[1024]{};
	DWORD cb = sizeof(buf) - sizeof(wchar_t);
	DWORD type = 0;
	const LSTATUS r = RegQueryValueExW(h, value, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &cb);
	RegCloseKey(h);
	if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
		return {};
	buf[(cb / sizeof(wchar_t)) % (sizeof(buf) / sizeof(wchar_t))] = 0;
	std::wstring s(buf);
	std::replace(s.begin(), s.end(), L'/', L'\\');
	while (!s.empty() && (s.back() == L'\\' || s.back() == L' '))
		s.pop_back();
	return s;
}

static void NormalizePath(std::wstring& p)
{
	std::replace(p.begin(), p.end(), L'/', L'\\');
	while (!p.empty() && (p.back() == L'\\' || p.back() == L' '))
		p.pop_back();
}

static bool EndsWithI(const std::wstring& s, const wchar_t* suffix)
{
	const size_t n = wcslen(suffix);
	if (s.size() < n)
		return false;
	return _wcsicmp(s.c_str() + s.size() - n, suffix) == 0;
}

static std::wstring Utf8ToWide(const std::string& s)
{
	if (s.empty()) return {};
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
	std::wstring w(n, 0);
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
	return w;
}

static std::vector<std::wstring> ExtractPathsFromVdf(const std::string& text)
{
	std::vector<std::wstring> out;
	size_t pos = 0;
	while (pos < text.size()) {
		const size_t k = text.find("\"path\"", pos);
		if (k == std::string::npos)
			break;
		const size_t q1 = text.find('"', k + 6);
		if (q1 == std::string::npos) break;
		const size_t q2 = text.find('"', q1 + 1);
		if (q2 == std::string::npos) break;
		std::string val = text.substr(q1 + 1, q2 - q1 - 1);
		
		std::string unesc;
		unesc.reserve(val.size());
		for (size_t i = 0; i < val.size(); ++i) {
			if (val[i] == '\\' && i + 1 < val.size() && val[i + 1] == '\\') {
				unesc.push_back('\\');
				++i;
			} else if (val[i] == '\\' && i + 1 < val.size() && val[i + 1] == 'n') {
				++i;
			} else {
				unesc.push_back(val[i]);
			}
		}
		std::wstring w = Utf8ToWide(unesc);
		NormalizePath(w);
		if (!w.empty())
			out.push_back(std::move(w));
		pos = q2 + 1;
	}
	return out;
}

static void AppendLibraryPathsFromVdf(const std::wstring& steamapps_dir, std::vector<std::wstring>& out,
                                      std::unordered_set<std::wstring>& seen)
{
	if (steamapps_dir.empty())
		return;
	const std::wstring steam_install = [&]() {
		if (EndsWithI(steamapps_dir, L"\\steamapps")) {
			return steamapps_dir.substr(0, steamapps_dir.size() - 10);
		}
		return steamapps_dir;
	}();

	for (const wchar_t* rel : { L"libraryfolders.vdf", L"config\\libraryfolders.vdf" }) {
		const std::wstring vdf = steam_install + L"\\steamapps\\" + rel;
		if (!ExpectionalWinIO::FileExistsWide(vdf))
			continue;
		std::vector<uint8_t> raw;
		if (!ExpectionalWinIO::ReadAllBytesWide(vdf, raw))
			continue;
		const std::string text(reinterpret_cast<const char*>(raw.data()), raw.size());
		for (std::wstring p : ExtractPathsFromVdf(text)) {
			NormalizePath(p);
			if (p.empty())
				continue;
			const std::wstring candidate = p + L"\\steamapps";
			if (seen.insert(candidate).second)
				out.push_back(candidate);
		}
	}
}

static void AppendSteamAppsDir(std::wstring root, std::vector<std::wstring>& out,
                               std::unordered_set<std::wstring>& seen)
{
	if (root.empty())
		return;
	NormalizePath(root);
	if (EndsWithI(root, L"\\steamapps")) {
		if (ExpectionalWinIO::DirExistsWide(root) && seen.insert(root).second)
			out.push_back(root);
		return;
	}
	const std::wstring candidate = root + L"\\steamapps";
	if (ExpectionalWinIO::DirExistsWide(candidate) && seen.insert(candidate).second)
		out.push_back(candidate);
}

static void AppendCommonSteamRootsOnAllDrives(std::vector<std::wstring>& steam_roots,
                                              std::unordered_set<std::wstring>& seen_steam)
{
	for (wchar_t dl = L'A'; dl <= L'Z'; ++dl) {
		wchar_t root_path[] = { dl, L':', L'\\', L'\0' };
		if (GetDriveTypeW(root_path) != DRIVE_FIXED)
			continue;
		const std::wstring drive(1, dl);
		const wchar_t* suffixes[] = {
			L"\\Program Files (x86)\\Steam",
			L"\\Program Files\\Steam",
			L"\\Steam",
			L"\\SteamLibrary",
			L"\\Games\\Steam",
			L"\\Games\\SteamLibrary",
		};
		for (const wchar_t* sfx : suffixes) {
			std::wstring p = drive + L":" + sfx;
			NormalizePath(p);
			if (seen_steam.insert(p).second)
				steam_roots.push_back(std::move(p));
		}
	}
}

static std::vector<std::wstring> CollectAllSteamAppsDirs()
{
	std::vector<std::wstring> steam_roots;
	std::unordered_set<std::wstring> seen_steam;
	std::vector<std::wstring> steamapps_dirs;
	std::unordered_set<std::wstring> seen_steamapps;

	const auto push_steam_root = [&](std::wstring p) {
		if (p.empty())
			return;
		NormalizePath(p);
		if (seen_steam.insert(p).second)
			steam_roots.push_back(std::move(p));
	};

	push_steam_root(ReadRegistryStringW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"));
	push_steam_root(ReadRegistryStringW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath"));
	push_steam_root(ReadRegistryStringW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath"));
	push_steam_root(L"C:\\Program Files (x86)\\Steam");
	push_steam_root(L"C:\\Program Files\\Steam");

	AppendCommonSteamRootsOnAllDrives(steam_roots, seen_steam);

	for (const std::wstring& steam : steam_roots)
		AppendSteamAppsDir(steam, steamapps_dirs, seen_steamapps);

	const size_t initial = steamapps_dirs.size();
	for (size_t i = 0; i < initial; ++i)
		AppendLibraryPathsFromVdf(steamapps_dirs[i], steamapps_dirs, seen_steamapps);
	for (size_t i = initial; i < steamapps_dirs.size(); ++i)
		AppendLibraryPathsFromVdf(steamapps_dirs[i], steamapps_dirs, seen_steamapps);

	for (wchar_t dl = L'A'; dl <= L'Z'; ++dl) {
		wchar_t root_path[] = { dl, L':', L'\\', L'\0' };
		if (GetDriveTypeW(root_path) != DRIVE_FIXED)
			continue;
		const std::wstring drive(1, dl);
		const wchar_t* direct_ws[] = {
			L"\\SteamLibrary\\steamapps",
			L"\\Program Files (x86)\\Steam\\steamapps",
			L"\\Program Files\\Steam\\steamapps",
			L"\\Steam\\steamapps",
			L"\\Games\\Steam\\steamapps",
			L"\\Games\\SteamLibrary\\steamapps",
		};
		for (const wchar_t* rel : direct_ws) {
			const std::wstring sa = drive + L":" + rel;
			if (!ExpectionalWinIO::DirExistsWide(sa))
				continue;
			if (seen_steamapps.insert(sa).second)
				steamapps_dirs.push_back(sa);
		}
	}

	return steamapps_dirs;
}

}  

std::vector<std::wstring> FindSteamInstallPaths()
{
	std::vector<std::wstring> out;
	std::unordered_set<std::wstring> seen;
	const auto push = [&](std::wstring p) {
		if (p.empty()) return;
		NormalizePath(p);
		if (seen.insert(p).second)
			out.push_back(std::move(p));
	};
	push(ReadRegistryStringW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"));
	push(ReadRegistryStringW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath"));
	push(ReadRegistryStringW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath"));
	push(L"C:\\Program Files (x86)\\Steam");
	push(L"C:\\Program Files\\Steam");
	AppendCommonSteamRootsOnAllDrives(out, seen);
	return out;
}

std::vector<std::wstring> ParseSteamLibraryFolders(const std::wstring& steam_install)
{
	std::vector<std::wstring> out;
	std::unordered_set<std::wstring> seen;
	if (steam_install.empty())
		return out;
	AppendSteamAppsDir(steam_install, out, seen);
	AppendLibraryPathsFromVdf(out.empty() ? steam_install + L"\\steamapps" : out.front(), out, seen);
	return out;
}

std::vector<std::wstring> FindAllWorkshopContent730Roots()
{
	std::vector<std::wstring> out;
	std::unordered_set<std::wstring> seen;
	for (const std::wstring& lib : CollectAllSteamAppsDirs()) {
		std::wstring ws_root = lib + L"\\workshop\\content\\730";
		if (!ExpectionalWinIO::DirExistsWide(ws_root))
			continue;
		NormalizePath(ws_root);
		if (seen.insert(ws_root).second)
			out.push_back(ws_root);
	}
	return out;
}

std::vector<WorkshopAddon> EnumerateCs2WorkshopAddons()
{
	std::vector<WorkshopAddon> out;
	std::unordered_set<std::wstring> seen_paths;

	for (const std::wstring& ws_root : FindAllWorkshopContent730Roots()) {
		ExpectionalWinIO::ForEachSubdirWide(ws_root, [&](const std::wstring& dir) {
			if (!seen_paths.insert(dir).second)
				return;
			WorkshopAddon a;
			a.path = dir;
			const size_t slash = dir.find_last_of(L"\\/");
			a.addon_id = ExpectionalWinIO::WideToUtf8(
			    slash == std::wstring::npos ? dir : dir.substr(slash + 1));
			out.push_back(std::move(a));
		});
	}
	return out;
}

}  
