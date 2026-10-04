#include "window_radar_map_tex.hpp"
#include "globals.hpp"
#include "../AnanbabanOverlay/expectional_ananbaban_overlay.hpp"
#include "../OSImGui/shade_imgui_settings.h"

#include "../../Includes/Imgui/imgui.h"

#include <TlHelp32.h>
#include <Windows.h>
#include <psapi.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <d3d11.h>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <cstdio>

#ifndef TH32CS_SNAPMODULE64
#define TH32CS_SNAPMODULE64 0x00000040
#endif

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "psapi.lib")

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "ThirdParty/vpk-parser/VPK.hpp"
#include "ThirdParty/vpk-parser/VtexParser.hpp"
#include "expectional_winio.hpp"

static std::string g_last_key;

struct AbandonInfo {
	DWORD last_tick;
	int attempts;
};
static std::unordered_map<std::string, AbandonInfo> g_maptex_abandoned_keys;

static constexpr DWORD kRadarRetryMs = 0xFFFFFFFF; 
static constexpr int kRadarMaxHeavyAttempts = 1;

static void DebugRadarLog(const char* ) {
	
}

static std::vector<std::string> g_workshop_container_cache;
static bool g_workshop_container_cache_built = false;
static const std::vector<std::string>& WorkshopContainerCache() {
	if (!g_workshop_container_cache_built) {
		g_workshop_container_cache = vpk::find_workshop_map_container_vpks();
		g_workshop_container_cache_built = true;
	}
	return g_workshop_container_cache;
}

struct ContainerIndex {
	std::string path;
	std::vector<std::string> inner_map_names_lower;
};
static std::vector<ContainerIndex> g_container_index;
static bool g_container_index_built = false;
static ID3D11ShaderResourceView* g_srv = nullptr;
static char g_status[192];

static void SetStatus(const char* msg) {
	if (msg)
		strncpy_s(g_status, msg, _TRUNCATE);
	else
		g_status[0] = '\0';
}

static bool MapIdSafe(const char* s) {
	if (!s || !*s)
		return false;
	for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p; ++p) {
		if (*p < 32 || *p > 126)
			return false;
		if (*p == '/' || *p == '\\' || *p == ':' || *p == '*' || *p == '?' || *p == '"' || *p == '<' || *p == '>'
			|| *p == '|')
			return false;
	}
	return true;
}

static bool ModuleExePath(const wchar_t* module_name, std::wstring& out) {
	out.clear();
	if (!processid || !module_name)
		return false;
	const DWORD flags = TH32CS_SNAPMODULE | TH32CS_SNAPMODULE64;
	HANDLE snap = CreateToolhelp32Snapshot(flags, static_cast<DWORD>(processid));
	if (snap == INVALID_HANDLE_VALUE)
		return false;
	MODULEENTRY32W me{};
	me.dwSize = sizeof(me);
	bool ok = false;
	if (Module32FirstW(snap, &me)) {
		do {
			if (!_wcsicmp(me.szModule, module_name)) {
				out = me.szExePath;
				ok = !out.empty();
				break;
			}
		} while (Module32NextW(snap, &me));
	}
	CloseHandle(snap);
	return ok;
}

static bool ModuleExePathPsapi(const wchar_t* module_name, std::wstring& out) {
	out.clear();
	if (!processid || !module_name)
		return false;
	const DWORD pid = static_cast<DWORD>(processid);
	HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
	if (!h)
		h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
	if (!h)
		return false;
	HMODULE mods[768]{};
	DWORD cb = 0;
	if (!EnumProcessModules(h, mods, sizeof(mods), &cb) || cb < sizeof(HMODULE)) {
		CloseHandle(h);
		return false;
	}
	const unsigned n = static_cast<unsigned>(cb / sizeof(HMODULE));
	bool ok = false;
	for (unsigned i = 0; i < n; ++i) {
		wchar_t path[520]{};
		if (!GetModuleFileNameExW(h, mods[i], path, static_cast<DWORD>(sizeof(path) / sizeof(path[0]))))
			continue;
		const wchar_t* slash = wcsrchr(path, L'\\');
		const wchar_t* base = slash ? slash + 1 : path;
		if (!_wcsicmp(base, module_name)) {
			out = path;
			ok = !out.empty();
			break;
		}
	}
	CloseHandle(h);
	return ok;
}

static bool SteamPathToCsgoRoot(std::wstring& out_csgo) {
	out_csgo.clear();
	auto try_root = [&out_csgo](const std::wstring& steam_root) -> bool {
		if (steam_root.size() < 3)
			return false;
		std::wstring base = steam_root;
		if (!base.empty() && (base.back() == L'\\' || base.back() == L'/'))
			base.pop_back();
		const std::wstring cand = base + L"\\steamapps\\common\\Counter-Strike Global Offensive\\game\\csgo";
		const std::wstring vpk = cand + L"\\pak01_dir.vpk";
		if (GetFileAttributesW(vpk.c_str()) != INVALID_FILE_ATTRIBUTES) {
			out_csgo = cand;
			return true;
		}
		return false;
	};
	HKEY h = nullptr;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &h) == ERROR_SUCCESS) {
		wchar_t buf[MAX_PATH * 2]{};
		DWORD sz = sizeof(buf);
		DWORD typ = 0;
		if (RegQueryValueExW(h, L"SteamPath", nullptr, &typ, reinterpret_cast<LPBYTE>(buf), &sz) == ERROR_SUCCESS
			&& (typ == REG_SZ || typ == REG_EXPAND_SZ) && sz >= sizeof(wchar_t) * 2) {
			RegCloseKey(h);
			h = nullptr;
			if (try_root(buf))
				return true;
		} else
			RegCloseKey(h);
	}
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", 0, KEY_READ, &h) == ERROR_SUCCESS) {
		wchar_t buf[MAX_PATH * 2]{};
		DWORD sz = sizeof(buf);
		DWORD typ = 0;
		if (RegQueryValueExW(h, L"InstallPath", nullptr, &typ, reinterpret_cast<LPBYTE>(buf), &sz) == ERROR_SUCCESS
			&& (typ == REG_SZ || typ == REG_EXPAND_SZ) && sz >= sizeof(wchar_t) * 2) {
			RegCloseKey(h);
			if (try_root(buf))
				return true;
		} else
			RegCloseKey(h);
	}
	return false;
}

static bool CsgoRootFromClientDll(const std::wstring& client_dll, std::wstring& out_csgo) {
	out_csgo.clear();
	if (client_dll.size() < 16)
		return false;
	std::wstring p = client_dll;
	const size_t slash = p.find_last_of(L"\\/");
	if (slash == std::wstring::npos || slash < 2)
		return false;
	p.resize(slash);
	for (int i = 0; i < 2; ++i) {
		const size_t s2 = p.find_last_of(L"\\/");
		if (s2 == std::wstring::npos || s2 < 2)
			return false;
		p.resize(s2);
	}
	out_csgo = std::move(p);
	return !out_csgo.empty();
}

static bool CsgoRootFromCs2Exe(const std::wstring& cs2_exe, std::wstring& out_csgo) {
	out_csgo.clear();
	if (cs2_exe.size() < 12)
		return false;
	std::wstring p = cs2_exe;
	const size_t slash = p.find_last_of(L"\\/");
	if (slash == std::wstring::npos || slash < 2)
		return false;
	p.resize(slash);
	for (int i = 0; i < 2; ++i) {
		const size_t s2 = p.find_last_of(L"\\/");
		if (s2 == std::wstring::npos || s2 < 2)
			return false;
		p.resize(s2);
	}
	out_csgo = p + L"\\csgo";
	return true;
}

static bool ResolveCsgoRoot(std::wstring& out_csgo) {
	std::wstring client;
	if (ModuleExePath(L"client.dll", client) && CsgoRootFromClientDll(client, out_csgo))
		return true;
	if (ModuleExePathPsapi(L"client.dll", client) && CsgoRootFromClientDll(client, out_csgo))
		return true;
	std::wstring cs2;
	if (ModuleExePath(L"cs2.exe", cs2) && CsgoRootFromCs2Exe(cs2, out_csgo))
		return true;
	if (ModuleExePathPsapi(L"cs2.exe", cs2) && CsgoRootFromCs2Exe(cs2, out_csgo))
		return true;
	if (SteamPathToCsgoRoot(out_csgo))
		return true;
	return false;
}

static std::string WideToUtf8(const std::wstring& w) {
	if (w.empty())
		return {};
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	if (n <= 0)
		return {};
	std::string u(static_cast<size_t>(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), u.data(), n, nullptr, nullptr);
	return u;
}

static void StrToLowerAscii(std::string& s) {
	for (char& c : s) {
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}
}

static void AppendRadarKeyVariantsLower(const std::string& key_lower, std::vector<std::string>& out) {
	out.clear();
	if (key_lower.empty())
		return;
	out.push_back(key_lower);
	std::string cur = key_lower;
	for (;;) {
		const size_t u = cur.rfind('_');
		if (u == std::string::npos || u < 3u)
			break;
		const std::string tail = cur.substr(u + 1);
		bool all_digit = !tail.empty();
		for (unsigned char ch : tail) {
			if (!std::isdigit(ch)) {
				all_digit = false;
				break;
			}
		}
		if (!all_digit)
			break;
		cur.resize(u);
		bool dup = false;
		for (const std::string& e : out)
			if (e == cur) {
				dup = true;
				break;
			}
		if (!dup)
			out.push_back(cur);
	}
	const auto push_if_new = [&out](const std::string& s) {
		if (s.empty())
			return;
		for (const std::string& e : out)
			if (e == s)
				return;
		out.push_back(s);
	};
	if (key_lower.size() > 3u) {
		if (key_lower.compare(0, 3, "mg_") == 0)
			push_if_new(key_lower.substr(3));
		if (key_lower.compare(0, 3, "gd_") == 0)
			push_if_new(key_lower.substr(3));
	}
}

static bool TryOpenPakDir(vpk::VPKDir& dir, const std::wstring& csgo) {
	for (unsigned pi = 1; pi <= 4u; ++pi) {
		wchar_t name[40]{};
		swprintf_s(name, L"pak%02u_dir.vpk", pi);
		const std::wstring path = csgo + L"\\" + name;
		if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
			continue;
		const std::string u8 = WideToUtf8(path);
		if (!u8.empty() && dir.open(u8))
			return true;
	}
	return false;
}

static void VpkPathToNameExtLower(const std::string& path, std::string& name_lower, std::string& ext_lower) {
	const auto slash = path.rfind('/');
	const std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
	const auto dot = base.rfind('.');
	if (dot == std::string::npos || dot + 1 >= base.size()) {
		name_lower = base;
		StrToLowerAscii(name_lower);
		ext_lower.clear();
		return;
	}
	name_lower = base.substr(0, dot);
	ext_lower = base.substr(dot + 1);
	StrToLowerAscii(name_lower);
	StrToLowerAscii(ext_lower);
}

static const std::vector<ContainerIndex>& BuildContainerIndex() {
	if (g_container_index_built)
		return g_container_index;
	g_container_index_built = true;
	for (const std::string& cp : WorkshopContainerCache()) {
		vpk::VPKDir cont;
		if (!cont.open(cp))
			continue;
		ContainerIndex ci;
		ci.path = cp;
		for (const std::string& mf : cont.list_files("maps/", ".vpk")) {
			std::string nm, ext;
			VpkPathToNameExtLower(mf, nm, ext);
			if (ext == "vpk" && !nm.empty())
				ci.inner_map_names_lower.push_back(nm);
		}
		if (!ci.inner_map_names_lower.empty())
			g_container_index.push_back(std::move(ci));
	}
	return g_container_index;
}
static bool ContainerInnerMatch(const ContainerIndex& ci, const std::vector<std::string>& try_keys) {
	for (const std::string& nm : ci.inner_map_names_lower)
		for (const std::string& vk : try_keys)
			if (nm == vk)
				return true;
	return false;
}

static int RadarAssetTier(const std::string& name_lower, const std::string& ext_lower) {
	if (ext_lower == "vtex_c") {
		if (name_lower.find("_radar") == std::string::npos)
			
			return 35;
		if (name_lower.find("_radar_psd") != std::string::npos)
			return 0;
		if (name_lower.find("_radar_tga") != std::string::npos)
			return 1;
		return 2;
	}
	if (ext_lower == "png")
		return 10;
	return 99;
}

static bool FindBestOverheadRadarInVpk(vpk::VPKDir& dir, const std::string& map_key_lower, std::string& out_path,
	std::string& out_ext_lower) {
	out_path.clear();
	out_ext_lower.clear();
	int best_tier = 999;
	std::string best_path;
	std::string best_ext;
	const char* prefixes[] = {
		"panorama/images/overheadmaps/",
		"panorama/images/overheads/maps/",
	};
	const char* suffixes[] = {".vtex_c", ".png", ".jpg"};
	for (const char* pre : prefixes) {
		for (const char* suf : suffixes) {
			const std::vector<std::string> files = dir.list_files(pre, suf);
			for (const std::string& full : files) {
				std::string flo = full;
				StrToLowerAscii(flo);
				if (flo.find(map_key_lower) == std::string::npos)
					continue;
				std::string nm, ext;
				VpkPathToNameExtLower(full, nm, ext);
				if (ext != "vtex_c" && ext != "png" && ext != "jpg")
					continue;
				const int tier = RadarAssetTier(nm, ext == "vtex_c" ? std::string("vtex_c") : ext);
				if (tier >= 99)
					continue;
				const bool replace = best_path.empty() || tier < best_tier
					|| (tier == best_tier && full.size() < best_path.size());
				if (replace) {
					best_tier = tier;
					best_path = full;
					best_ext = ext;
				}
			}
		}
	}
	if (best_path.empty())
		return false;
	out_path = std::move(best_path);
	out_ext_lower = std::move(best_ext);
	return true;
}

static bool FindUnkeyedOverheadRadarInVpk(vpk::VPKDir& dir, std::string& out_path, std::string& out_ext_lower) {
	out_path.clear();
	out_ext_lower.clear();
	int best_tier = 999;
	std::string best_path;
	std::string best_ext;
	const char* prefixes[] = {
		"panorama/images/overheadmaps/",
		"panorama/images/overheads/maps/",
	};
	const char* suffixes[] = {".vtex_c", ".png", ".jpg"};
	for (const char* pre : prefixes) {
		for (const char* suf : suffixes) {
			const std::vector<std::string> files = dir.list_files(pre, suf);
			for (const std::string& full : files) {
				std::string nm, ext;
				VpkPathToNameExtLower(full, nm, ext);
				if (ext != "vtex_c" && ext != "png" && ext != "jpg")
					continue;
				const int tier = RadarAssetTier(nm, ext == "vtex_c" ? std::string("vtex_c") : ext);
				if (tier >= 99)
					continue;
				const bool replace = best_path.empty() || tier < best_tier
					|| (tier == best_tier && full.size() < best_path.size());
				if (replace) {
					best_tier = tier;
					best_path = full;
					best_ext = ext;
				}
			}
		}
	}
	if (best_path.empty())
		return false;
	out_path = std::move(best_path);
	out_ext_lower = std::move(best_ext);
	return true;
}

static bool TryOpenGameMapVpk(vpk::VPKDir& dir, const std::wstring& csgo, const std::string& map_key_lower) {
	std::vector<std::string> keys;
	AppendRadarKeyVariantsLower(map_key_lower, keys);
	for (const std::string& k : keys) {
		const std::wstring w = csgo + L"\\maps\\" + std::wstring(k.begin(), k.end()) + L".vpk";
		if (::GetFileAttributesW(w.c_str()) == INVALID_FILE_ATTRIBUTES)
			continue;
		const std::string u8 = WideToUtf8(w);
		if (!u8.empty() && dir.open(u8))
			return true;
	}
	return false;
}

static bool TryWorkshopRadarVpkOpen(vpk::VPKDir& dir, const std::string& map_key_lower) {
	std::vector<std::string> keys;
	AppendRadarKeyVariantsLower(map_key_lower, keys);
	for (const std::string& vk : keys) {
		for (const auto& p : vpk::find_workshop_map_vpks(vk)) {
			if (dir.open(p))
				return true;
		}
	}
	for (const auto& cp : vpk::find_workshop_map_container_vpks()) {
		vpk::VPKDir cont;
		if (!cont.open(cp))
			continue;
		for (const auto& mf : cont.list_files("maps/", ".vpk")) {
			std::string flo = mf;
			StrToLowerAscii(flo);
			bool hit = false;
			for (const std::string& vk : keys) {
				if (flo.find(vk) != std::string::npos) {
					hit = true;
					break;
				}
			}
			if (!hit)
				continue;
			const auto nested = cont.read_file(mf);
			if (!nested || nested->empty())
				continue;
			if (dir.open_from_bytes(*nested))
				return true;
		}
	}
	return false;
}

static int TryVpkRadarPayload(vpk::VPKDir& dir, const std::string& key, bool allow_unkeyed_fallback,
	std::string& maptex_fail_detail, stbi_uc*& pixels, int& iw, int& ih, int& comp, int* out_vtex_w, int* out_vtex_h) {
	std::string vpk_path, ext_l;
	if (!FindBestOverheadRadarInVpk(dir, key, vpk_path, ext_l)) {
		if (!allow_unkeyed_fallback || !FindUnkeyedOverheadRadarInVpk(dir, vpk_path, ext_l))
			return 0;
	}
	const auto bytes = dir.read_file(vpk_path);
	if (!bytes || bytes->empty())
		return 0;
	if (ext_l == "png" || ext_l == "jpg") {
		pixels = stbi_load_from_memory(bytes->data(), static_cast<int>(bytes->size()), &iw, &ih, &comp, 4);
		return (pixels && iw > 0 && ih > 0) ? 1 : 0;
	}
	if (ext_l != "vtex_c")
		return 0;
	const vtex::LoadResult lr = vtex::load(g_ExpectionalMainDX11Device, *bytes);
	if (lr.srv && lr.width > 0 && lr.height > 0) {
		if (g_srv)
			g_srv->Release();
		g_srv = lr.srv;
		if (out_vtex_w)
			*out_vtex_w = lr.width;
		if (out_vtex_h)
			*out_vtex_h = lr.height;
		return 2;
	}
	if (lr.srv)
		lr.srv->Release();
	const std::string& err = vtex::last_error();
	if (!err.empty())
		maptex_fail_detail = err;
	return 0;
}

static HRESULT CreateSrvFromRgba(ID3D11Device* device, const uint8_t* rgba, UINT w, UINT h,
	ID3D11ShaderResourceView** out_srv) {
	*out_srv = nullptr;
	if (!device || !rgba || w == 0 || h == 0 || w > 8192 || h > 8192)
		return E_INVALIDARG;

	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = w;
	desc.Height = h;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA sub{};
	sub.pSysMem = rgba;
	sub.SysMemPitch = w * 4u;

	ID3D11Texture2D* tex = nullptr;
	HRESULT hr = device->CreateTexture2D(&desc, &sub, &tex);
	if (FAILED(hr) || !tex)
		return FAILED(hr) ? hr : E_FAIL;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
	srvd.Format = desc.Format;
	srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvd.Texture2D.MipLevels = 1;
	srvd.Texture2D.MostDetailedMip = 0;

	ID3D11ShaderResourceView* srv = nullptr;
	hr = device->CreateShaderResourceView(tex, &srvd, &srv);
	tex->Release();
	if (FAILED(hr) || !srv)
		return FAILED(hr) ? hr : E_FAIL;
	*out_srv = srv;
	return S_OK;
}

static void ReleaseSrv() {
	if (g_srv) {
		g_srv->Release();
		g_srv = nullptr;
	}
}

bool WindowRadarMapTex_Tick(const char* panorama_map_id_utf8) {
	if (!g_ExpectionalMainDX11Device) {
		ReleaseSrv();
		g_last_key.clear();
		g_maptex_abandoned_keys.clear();
		SetStatus("maptex: no D3D11 device");
		return false;
	}

	std::string key = panorama_map_id_utf8 ? panorama_map_id_utf8 : "";
	if (key == "<empty>")
		key.clear();
	while (!key.empty() && (unsigned char)key.back() <= ' ')
		key.pop_back();
	while (!key.empty() && (unsigned char)key.front() <= ' ')
		key.erase(0, 1);
	if (key.empty() || key == "(planar)" || !MapIdSafe(key.c_str())) {
		if (g_last_key.empty() && !g_srv)
			return false;
		ReleaseSrv();
		g_last_key.clear();
		return false;
	}

	for (char& c : key) {
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}

	if (key == g_last_key && g_srv)
		return true;

	{
		const DWORD now = GetTickCount();
		const auto it = g_maptex_abandoned_keys.find(key);
		if (it != g_maptex_abandoned_keys.end()) {
			
			it->second.last_tick = now;
			return false;
		}
	}
	
	std::wstring csgo;
	if (!ResolveCsgoRoot(csgo)) {
		return false;
	}

	std::vector<std::string> disk_try_keys{key};

	std::string maptex_fail_detail;
	stbi_uc* pixels = nullptr;
	int iw = 0, ih = 0, comp = 0;

	std::wstring csgo_parent = csgo;
	{
		const size_t s = csgo_parent.find_last_of(L"\\/");
		if (s != std::wstring::npos && s >= 1)
			csgo_parent.resize(s);
	}

	const std::wstring search_bases[] = {
		csgo + L"\\panorama\\images\\overheadmaps\\",
		csgo + L"\\panorama\\images\\overheads\\maps\\",
		csgo + L"\\resource\\overviews\\",
		csgo_parent + L"\\content\\csgo\\panorama\\images\\overheadmaps\\",
		csgo_parent + L"\\content\\csgo\\panorama\\images\\overheads\\maps\\",
	};
	const wchar_t* exts[] = {L".png", L".jpg", L".jpeg"};

	for (const std::string& try_stem : disk_try_keys) {
		const std::wstring base_name(try_stem.begin(), try_stem.end());
		for (const std::wstring& base_dir : search_bases) {
			for (const wchar_t* ext : exts) {
				const std::wstring full = base_dir + base_name + ext;
				std::vector<uint8_t> pngBytes;
				if (!ExpectionalWinIO::ReadAllBytesWide(full, pngBytes))
					continue;
				pixels = stbi_load_from_memory(pngBytes.data(), static_cast<int>(pngBytes.size()),
				    &iw, &ih, &comp, 4);
				if (pixels && iw > 0 && ih > 0)
					break;
				if (pixels) {
					stbi_image_free(pixels);
					pixels = nullptr;
				}
			}
			if (pixels)
				break;
		}
		if (pixels)
			break;
	}

	if (!pixels) {
		const std::wstring oh = csgo + L"\\panorama\\images\\overheadmaps\\";
		const wchar_t* vtex_suffixes[] = {L"_radar_psd.vtex_c", L"_radar_tga.vtex_c", L"_radar.vtex_c"};
		for (const std::string& try_stem : disk_try_keys) {
			const std::wstring base_name(try_stem.begin(), try_stem.end());
			for (const wchar_t* suf : vtex_suffixes) {
				const std::wstring full = oh + base_name + suf;
				std::vector<uint8_t> vtex_buf;
				if (!ExpectionalWinIO::ReadAllBytesWide(full, vtex_buf))
					continue;
				if (vtex_buf.size() < 32 || vtex_buf.size() > 80 * 1024 * 1024)
					continue;
				const vtex::LoadResult lr = vtex::load(g_ExpectionalMainDX11Device, vtex_buf);
				if (lr.srv && lr.width > 0 && lr.height > 0) {
					if (g_srv)
						g_srv->Release();
					g_srv = lr.srv;
					g_last_key = key;
					g_maptex_abandoned_keys.erase(key);
					char ok[192];
					_snprintf_s(ok, _TRUNCATE, "maptex: ok %dx%d", lr.width, lr.height);
					SetStatus(ok);
					return true;
				}
				if (lr.srv)
					lr.srv->Release();
				const std::string& e = vtex::last_error();
				if (!e.empty())
					maptex_fail_detail = e;
			}
		}
	}

	if (!pixels) {
		std::vector<std::string> vpk_try_keys;
		AppendRadarKeyVariantsLower(key, vpk_try_keys);
		
		const std::vector<std::string> stock_try_keys{key};
		auto attempt_vpk = [&](vpk::VPKDir& dir, bool opened, bool allow_unkeyed,
			const std::vector<std::string>& try_keys) -> bool {
			if (!opened)
				return false;
			for (const std::string& try_key : try_keys) {
				int vtw = 0, vth = 0;
				const int r = TryVpkRadarPayload(dir, try_key, allow_unkeyed, maptex_fail_detail, pixels, iw, ih, comp,
					&vtw, &vth);
				if (r == 2) {
					g_last_key = key;
					g_maptex_abandoned_keys.erase(key);
					char ok[192];
					_snprintf_s(ok, _TRUNCATE, "maptex: ok %dx%d", vtw, vth);
					SetStatus(ok);
					return true;
				}
				if (r == 1 && pixels && iw > 0 && ih > 0) {
					g_maptex_abandoned_keys.erase(key);
					return false;
				}
			}
			return false;
		};

		vpk::VPKDir pak;
		if (!pixels && attempt_vpk(pak, TryOpenPakDir(pak, csgo), false, stock_try_keys))
			return true;
		vpk::VPKDir mapvpk;
		if (!pixels && attempt_vpk(mapvpk, TryOpenGameMapVpk(mapvpk, csgo, key), true, vpk_try_keys))
			return true;
		
		int prev_attempts = 0;
		{
			const auto it = g_maptex_abandoned_keys.find(key);
			if (it != g_maptex_abandoned_keys.end())
				prev_attempts = it->second.attempts;
		}
		const bool allow_heavy_workshop = prev_attempts < kRadarMaxHeavyAttempts;
		vpk::VPKDir ws;
		if (allow_heavy_workshop && !pixels
			&& attempt_vpk(ws, TryWorkshopRadarVpkOpen(ws, key), true, vpk_try_keys))
			return true;
		if (allow_heavy_workshop && !pixels) {
			
			bool workshop_nested_png = false;
			for (const ContainerIndex& ci : BuildContainerIndex()) {
				if (workshop_nested_png || pixels)
					break;
				if (!ContainerInnerMatch(ci, vpk_try_keys))
					continue;
				vpk::VPKDir cont;
				if (!cont.open(ci.path))
					continue;
				const std::vector<std::string> entries = cont.list_files("maps/", ".vpk");
				for (const std::string& mf : entries) {
					std::string nm, ext;
					VpkPathToNameExtLower(mf, nm, ext);
					if (ext != "vpk")
						continue;
					bool name_match = false;
					for (const std::string& vk : vpk_try_keys) {
						if (nm == vk) {
							name_match = true;
							break;
						}
					}
					if (!name_match)
						continue;
					const auto nested = cont.read_file(mf);
					if (!nested || nested->empty())
						continue;
					if (!ws.open_from_bytes(*nested))
						continue;
					if (attempt_vpk(ws, true, true, vpk_try_keys))
						return true;
					if (pixels && iw > 0 && ih > 0) {
						g_maptex_abandoned_keys.erase(key);
						workshop_nested_png = true;
						break;
					}
					
					for (const auto& deeper : ws.list_files("", ".vpk")) {
						std::string dnm, dext;
						VpkPathToNameExtLower(deeper, dnm, dext);
						bool dmatch = false;
						for (const std::string& vk : vpk_try_keys) {
							if (dnm == vk) {
								dmatch = true;
								break;
							}
						}
						if (!dmatch)
							continue;
						const auto deeper_buf = ws.read_file(deeper);
						if (!deeper_buf || deeper_buf->empty())
							continue;
						vpk::VPKDir ws2;
						if (!ws2.open_from_bytes(*deeper_buf))
							continue;
						if (attempt_vpk(ws2, true, true, vpk_try_keys))
							return true;
						if (pixels && iw > 0 && ih > 0) {
							g_maptex_abandoned_keys.erase(key);
							workshop_nested_png = true;
							break;
						}
					}
					if (workshop_nested_png)
						break;
				}
			}
		}
	}

	auto MarkAbandoned = [&]() {
		auto& info = g_maptex_abandoned_keys[key];
		info.last_tick = GetTickCount();
		++info.attempts;
	};

	if (!pixels || iw <= 0 || ih <= 0) {
		MarkAbandoned();
		char log[256];
		if (!maptex_fail_detail.empty()) {
			_snprintf_s(log, _TRUNCATE, "failed to parse: key='%s' detail='%s'", key.c_str(),
				maptex_fail_detail.c_str());
		} else {
			_snprintf_s(log, _TRUNCATE, "failed to parse: key='%s' (no overhead / decode failed)", key.c_str());
		}
		DebugRadarLog(log);
		return false;
	}

	ID3D11ShaderResourceView* srv = nullptr;
	const HRESULT hr = CreateSrvFromRgba(g_ExpectionalMainDX11Device, pixels, static_cast<UINT>(iw),
		static_cast<UINT>(ih), &srv);
	stbi_image_free(pixels);
	if (FAILED(hr) || !srv) {
		MarkAbandoned();
		char log[256];
		_snprintf_s(log, _TRUNCATE, "failed to parse: key='%s' (D3D11 SRV creation failed hr=0x%08lx)", key.c_str(),
			static_cast<long>(hr));
		DebugRadarLog(log);
		return false;
	}

	if (g_srv)
		g_srv->Release();
	g_srv = srv;
	g_last_key = key;
	g_maptex_abandoned_keys.erase(key);
	char ok[192];
	_snprintf_s(ok, _TRUNCATE, "maptex: ok %dx%d", iw, ih);
	SetStatus(ok);
	return true;
}

static ImVec2 MapTexRotScreenPt(const ImVec2& p, const ImVec2& ctr, float yawDeg, float mapW, float mapH) {
	if (fabsf(yawDeg) < 0.02f)
		return p;
	const float rad = yawDeg * (3.14159265f / 180.f);
	const float dx = p.x - ctr.x, dy = p.y - ctr.y;
	const float denom = 0.5f * (std::min)(mapW, mapH);
	if (denom < 1e-3f)
		return p;
	const float nx = dx / denom;
	const float ny = dy / denom;
	const float c = cosf(rad), s = sinf(rad);
	const float rx = nx * c - ny * s;
	const float ry = nx * s + ny * c;
	return ImVec2(ctr.x + rx * (mapW * 0.5f), ctr.y + ry * (mapH * 0.5f));
}

void WindowRadarMapTex_DrawUnderBlips(ImDrawList* dl, const ImVec2& rmin, const ImVec2& rmax, const ImVec2& uv0,
	const ImVec2& uv1, bool rotate_with_view, float view_yaw_deg, float map_w, float map_h) {
	if (!dl)
		return;
	if (g_srv) {
		const ImVec2 ctr((rmin.x + rmax.x) * 0.5f, (rmin.y + rmax.y) * 0.5f);
		if (!rotate_with_view || fabsf(view_yaw_deg) < 0.02f) {
			dl->AddImage(reinterpret_cast<ImTextureID>(g_srv), rmin, rmax, uv0, uv1, IM_COL32_WHITE);
			return;
		}
		
		const float map_rot_yaw = view_yaw_deg - 90.f;
		
		const float kCoverage = 1.4143f;
		const float half_dx = (rmax.x - rmin.x) * 0.5f * kCoverage;
		const float half_dy = (rmax.y - rmin.y) * 0.5f * kCoverage;
		const ImVec2 q_min(ctr.x - half_dx, ctr.y - half_dy);
		const ImVec2 q_max(ctr.x + half_dx, ctr.y + half_dy);
		const float uv_cx = (uv0.x + uv1.x) * 0.5f;
		const float uv_cy = (uv0.y + uv1.y) * 0.5f;
		const float uv_hx = (uv1.x - uv0.x) * 0.5f * kCoverage;
		const float uv_hy = (uv1.y - uv0.y) * 0.5f * kCoverage;
		
		ImVec2 eUv0(uv_cx - uv_hx, uv_cy - uv_hy);
		ImVec2 eUv1(uv_cx + uv_hx, uv_cy + uv_hy);
		ImVec2 p1(q_min.x, q_min.y), p2(q_max.x, q_min.y), p3(q_max.x, q_max.y), p4(q_min.x, q_max.y);
		const float new_w = q_max.x - q_min.x;
		const float new_h = q_max.y - q_min.y;
		p1 = MapTexRotScreenPt(p1, ctr, map_rot_yaw, new_w, new_h);
		p2 = MapTexRotScreenPt(p2, ctr, map_rot_yaw, new_w, new_h);
		p3 = MapTexRotScreenPt(p3, ctr, map_rot_yaw, new_w, new_h);
		p4 = MapTexRotScreenPt(p4, ctr, map_rot_yaw, new_w, new_h);
		const ImVec2 u1(eUv0.x, eUv0.y), u2(eUv1.x, eUv0.y), u3(eUv1.x, eUv1.y), u4(eUv0.x, eUv1.y);
		dl->AddImageQuad(reinterpret_cast<ImTextureID>(g_srv), p1, p2, p3, p4, u1, u2, u3, u4, IM_COL32_WHITE);
		return;
	}
	dl->AddRectFilled(rmin, rmax, ImGui::GetColorU32(ImVec4(
		c::elements::background.x, c::elements::background.y, c::elements::background.z, c::elements::background.w)));
}

void WindowRadarMapTex_Shutdown() {
	ReleaseSrv();
	g_last_key.clear();
	g_maptex_abandoned_keys.clear();
	g_workshop_container_cache.clear();
	g_workshop_container_cache_built = false;
	g_container_index.clear();
	g_container_index_built = false;
	SetStatus("");
}

bool WindowRadarMapTex_IsReady() {
	return g_srv != nullptr;
}

const char* WindowRadarMapTex_LastStatus() {
	return g_status;
}

bool WindowRadarMapTex_IsParsingMap() {
	return strncmp(g_status, "Parsing the map...", 18) == 0;
}

struct OverviewParsed {
	bool valid;
	bool tried;
	DWORD last_try_tick;
	double pos_x;
	double pos_y;
	double scale;
};
static std::unordered_map<std::string, OverviewParsed> g_overview_cache;

static bool ParseOverviewKV(const std::string& text, double& px, double& py, double& sc) {
	bool gx = false, gy = false, gs = false;
	const char* keys[3] = {"pos_x", "pos_y", "scale"};
	double* dst[3] = {&px, &py, &sc};
	bool* dstOk[3] = {&gx, &gy, &gs};
	auto is_word_boundary = [](char c) -> bool {
		return c == '\0' || c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' || c == '\'' || c == '=' ||
			c == ':' || c == ',';
	};
	for (int i = 0; i < 3; ++i) {
		size_t p = 0;
		while (p < text.size()) {
			p = text.find(keys[i], p);
			if (p == std::string::npos)
				break;
			const size_t kend = p + std::strlen(keys[i]);
			const char prev_c = (p == 0) ? '\0' : text[p - 1];
			const char next_c = (kend < text.size()) ? text[kend] : '\0';
			if (!is_word_boundary(prev_c) || !is_word_boundary(next_c)) {
				p = kend;
				continue;
			}
			
			size_t q = kend;
			while (q < text.size() && (text[q] == ' ' || text[q] == '\t' || text[q] == '=' || text[q] == ':'
										|| text[q] == '\r' || text[q] == '\n' || text[q] == '"' || text[q] == '\''))
				++q;
			
			size_t s = q;
			while (s < text.size()) {
				const char c = text[s];
				const bool num_char = (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E';
				if (!num_char)
					break;
				++s;
			}
			if (s > q) {
				try {
					*dst[i] = std::stod(text.substr(q, s - q));
					*dstOk[i] = true;
				} catch (...) {
				}
			}
			break;
		}
	}
	return gx && gy && gs;
}

static bool ReadOverviewTxtFromVpk(vpk::VPKDir& dir, const std::vector<std::string>& try_keys, std::string& out_text) {
	
	const char* prefixes[] = {"resource/overviews/", "maps/"};
	for (const char* pre : prefixes) {
		for (const std::string& k : try_keys) {
			const std::string p = std::string(pre) + k + ".txt";
			const auto bytes = dir.read_file(p);
			if (bytes && !bytes->empty()) {
				out_text.assign(reinterpret_cast<const char*>(bytes->data()), bytes->size());
				return true;
			}
		}
		const std::vector<std::string> files = dir.list_files(pre, ".txt");
		for (const std::string& full : files) {
			std::string nm, ext;
			VpkPathToNameExtLower(full, nm, ext);
			if (ext != "txt")
				continue;
			for (const std::string& k : try_keys) {
				if (nm == k) {
					const auto bytes = dir.read_file(full);
					if (bytes && !bytes->empty()) {
						out_text.assign(reinterpret_cast<const char*>(bytes->data()), bytes->size());
						return true;
					}
				}
			}
		}
	}
	return false;
}

static bool ReadOverviewTxtFromDisk(const std::wstring& csgo, const std::vector<std::string>& try_keys,
	std::string& out_text) {
	const std::wstring bases[] = {
		csgo + L"\\resource\\overviews\\",
		csgo + L"\\maps\\",
	};
	for (const std::wstring& base : bases) {
		for (const std::string& k : try_keys) {
			const std::wstring w = base + std::wstring(k.begin(), k.end()) + L".txt";
			std::vector<uint8_t> buf;
			if (!ExpectionalWinIO::ReadAllBytesWide(w, buf))
				continue;
			if (buf.empty() || buf.size() > 1024 * 1024)
				continue;
			out_text.assign(reinterpret_cast<const char*>(buf.data()), buf.size());
			return true;
		}
	}
	return false;
}

bool WindowRadarMapTex_QueryOverview(const char* panorama_map_id_utf8, double& out_pos_x, double& out_pos_y,
	double& out_scale) {
	std::string key = panorama_map_id_utf8 ? panorama_map_id_utf8 : "";
	if (key.empty() || key == "<empty>" || !MapIdSafe(key.c_str()))
		return false;
	StrToLowerAscii(key);

	auto it = g_overview_cache.find(key);
	const DWORD now = GetTickCount();
	if (it != g_overview_cache.end()) {
		if (it->second.valid) {
			out_pos_x = it->second.pos_x;
			out_pos_y = it->second.pos_y;
			out_scale = it->second.scale;
			return true;
		}
		
		if (it->second.tried)
			return false;
	}

	OverviewParsed& entry = g_overview_cache[key];
	entry.tried = true;
	entry.last_try_tick = now;

	std::wstring csgo;
	if (!ResolveCsgoRoot(csgo))
		return false;

	std::vector<std::string> try_keys;
	AppendRadarKeyVariantsLower(key, try_keys);
	
	const std::vector<std::string> stock_keys{key};

	std::string txt;
	std::string parse_src; 
	auto try_parse = [&](const std::string& s, const char* src) -> bool {
		double px = 0.0, py = 0.0, sc = 0.0;
		if (!ParseOverviewKV(s, px, py, sc)) {
			char log[256];
			_snprintf_s(log, _TRUNCATE, "parse-fail src=%s key='%s' bytes=%zu (pos_x/pos_y/scale eslesmedi)",
				src, key.c_str(), s.size());
			DebugRadarLog(log);
			
			std::string preview;
			preview.reserve((std::min)(s.size(), size_t{384}));
			for (size_t i = 0; i < s.size() && preview.size() < 384; ++i) {
				const unsigned char c = static_cast<unsigned char>(s[i]);
				if (c == '\r' || c == '\n' || c == '\t')
					preview.push_back(' ');
				else if (c >= 32 && c <= 126)
					preview.push_back(static_cast<char>(c));
				else
					preview.push_back('.');
			}
			char log2[640];
			_snprintf_s(log2, _TRUNCATE, "  raw='%s'", preview.c_str());
			DebugRadarLog(log2);
			return false;
		}
		if (sc <= 0.01 || sc > 64.0) {
			char log[256];
			_snprintf_s(log, _TRUNCATE, "parse-fail src=%s key='%s' scale=%f range-disi", src, key.c_str(), sc);
			DebugRadarLog(log);
			return false;
		}
		entry.valid = true;
		entry.pos_x = px;
		entry.pos_y = py;
		entry.scale = sc;
		out_pos_x = px;
		out_pos_y = py;
		out_scale = sc;
		char log[256];
		_snprintf_s(log, _TRUNCATE, "overview-ok src=%s key='%s' pos=(%.1f,%.1f) scale=%.3f",
			src, key.c_str(), px, py, sc);
		DebugRadarLog(log);
		return true;
	};

	{
		char log[256];
		_snprintf_s(log, _TRUNCATE, "query-start key='%s' (workshop+stock tarama)", key.c_str());
		DebugRadarLog(log);
	}

	if (ReadOverviewTxtFromDisk(csgo, stock_keys, txt) && try_parse(txt, "disk"))
		return true;

	{
		vpk::VPKDir pak;
		if (TryOpenPakDir(pak, csgo) && ReadOverviewTxtFromVpk(pak, stock_keys, txt) && try_parse(txt, "pak01"))
			return true;
	}
	{
		vpk::VPKDir mapvpk;
		if (TryOpenGameMapVpk(mapvpk, csgo, key) && ReadOverviewTxtFromVpk(mapvpk, try_keys, txt)
			&& try_parse(txt, "game-map-vpk"))
			return true;
	}
	{
		vpk::VPKDir ws;
		if (TryWorkshopRadarVpkOpen(ws, key) && ReadOverviewTxtFromVpk(ws, try_keys, txt)
			&& try_parse(txt, "workshop-direct"))
			return true;
	}
	
	int containers_scanned = 0;
	for (const ContainerIndex& ci : BuildContainerIndex()) {
		if (!ContainerInnerMatch(ci, try_keys))
			continue;
		++containers_scanned;
		{
			char log[384];
			_snprintf_s(log, _TRUNCATE, "ws-container-match key='%s' path='%s'", key.c_str(), ci.path.c_str());
			DebugRadarLog(log);
		}
		vpk::VPKDir cont;
		if (!cont.open(ci.path))
			continue;
		
		if (ReadOverviewTxtFromVpk(cont, try_keys, txt) && try_parse(txt, "ws-container-outer"))
			return true;
		for (const auto& mf : cont.list_files("maps/", ".vpk")) {
			std::string nm, ext;
			VpkPathToNameExtLower(mf, nm, ext);
			if (ext != "vpk")
				continue;
			bool name_match = false;
			for (const std::string& vk : try_keys) {
				if (nm == vk) {
					name_match = true;
					break;
				}
			}
			if (!name_match)
				continue;
			const auto nested = cont.read_file(mf);
			if (!nested || nested->empty())
				continue;
			vpk::VPKDir ws;
			if (!ws.open_from_bytes(*nested))
				continue;
			if (ReadOverviewTxtFromVpk(ws, try_keys, txt) && try_parse(txt, "ws-nested-vpk"))
				return true;
			for (const auto& deeper : ws.list_files("", ".vpk")) {
				std::string dnm, dext;
				VpkPathToNameExtLower(deeper, dnm, dext);
				if (dext != "vpk")
					continue;
				bool dmatch = false;
				for (const std::string& vk : try_keys) {
					if (dnm == vk) {
						dmatch = true;
						break;
					}
				}
				if (!dmatch)
					continue;
				const auto db = ws.read_file(deeper);
				if (!db || db->empty())
					continue;
				vpk::VPKDir ws2;
				if (!ws2.open_from_bytes(*db))
					continue;
				if (ReadOverviewTxtFromVpk(ws2, try_keys, txt) && try_parse(txt, "ws-deep-vpk"))
					return true;
			}
		}
	}
	{
		char log[320];
		_snprintf_s(log, _TRUNCATE,
			"failed to parse: overview not found key='%s' (disk + pak01 + game-map-vpk + workshop tarandi, eslesen "
			"konteyner=%d)",
			key.c_str(), containers_scanned);
		DebugRadarLog(log);
	}
	return false;
}
