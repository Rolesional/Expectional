#include "window_radar_d3d9.hpp"

#include "../OSImGui/shade_imgui_settings.h"

#include <Windows.h>
#include <d3d9.h>
#include <winhttp.h>
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "winhttp.lib")

#define STBI_ONLY_PNG
#include "stb_image.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../Includes/Imgui/imgui.h"

extern IDirect3DDevice9Ex* p_Device;
IDirect3DDevice9Ex* p_Device = nullptr;

namespace {

std::wstring Utf8ToWide(const char* s) {
	if (!s)
		return {};
	const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
	if (n <= 0)
		return {};
	std::wstring w(static_cast<size_t>(n - 1), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
	return w;
}

static bool HttpGetRawGithubPng(const char* map_id_utf8, std::vector<uint8_t>& out, std::string& err) {
	out.clear();
	err.clear();
	const bool useExpectionalDeCacheUrl = (map_id_utf8 && _stricmp(map_id_utf8, "de_cache") == 0);
	const wchar_t* host = nullptr;
	std::wstring pathW;
	if (useExpectionalDeCacheUrl) {
		host = L"github.com";
		pathW = Utf8ToWide("/Rolesional/Expectional-cs2/blob/main/de_cache_radar_psd.png?raw=true");
	} else {
		char pathUtf8[384];
		const int n = snprintf(pathUtf8, sizeof pathUtf8,
		    "/Valthrun/valthrun-cs2/master/radar/web/src/map-info/%s/map_style_cs2.png",
		    map_id_utf8 ? map_id_utf8 : "");
		if (n <= 0 || n >= static_cast<int>(sizeof pathUtf8)) {
			err = "path";
			return false;
		}
		host = L"raw.githubusercontent.com";
		pathW = Utf8ToWide(pathUtf8);
	}

	HINTERNET hSession = WinHttpOpen(L"Expectional-WindowRadar/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
	    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession) {
		err = "WinHttpOpen";
		return false;
	}
	if (useExpectionalDeCacheUrl) {
		DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
		WinHttpSetOption(hSession, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof redirectPolicy);
	}
	HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!hConnect) {
		err = "WinHttpConnect";
		WinHttpCloseHandle(hSession);
		return false;
	}
	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", pathW.c_str(), nullptr, WINHTTP_NO_REFERER,
	    WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!hRequest) {
		err = "WinHttpOpenRequest";
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}
	if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
	    !WinHttpReceiveResponse(hRequest, nullptr)) {
		err = "HTTP";
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}
	DWORD status = 0;
	DWORD sz = sizeof status;
	if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
	        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX) &&
	    status != 200) {
		char b[48];
		snprintf(b, sizeof b, "HTTP %lu", static_cast<unsigned long>(status));
		err = b;
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}
	for (;;) {
		DWORD avail = 0;
		if (!WinHttpQueryDataAvailable(hRequest, &avail))
			break;
		if (avail == 0)
			break;
		const size_t old = out.size();
		out.resize(old + avail);
		DWORD read = 0;
		if (!WinHttpReadData(hRequest, out.data() + old, avail, &read) || read == 0)
			break;
		out.resize(old + read);
	}
	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	if (out.size() < 64) {
		err = "body small";
		return false;
	}
	return true;
}

std::mutex g_state_mtx;
IDirect3DTexture9* g_map_tex = nullptr;
std::string g_loaded_tex_world;
std::string g_status_line;
std::string g_last_err;

std::vector<uint8_t> g_staging_png;
std::string g_staging_world;
std::atomic<bool> g_staging_ready{false};
std::atomic<bool> g_dl_busy{false};
std::atomic<uint64_t> g_dl_token{0};

std::string s_spawn_throttle_world;
DWORD s_spawn_throttle_ms = 0;

static void ReleaseMapTexUnlocked() {
	if (g_map_tex) {
		g_map_tex->Release();
		g_map_tex = nullptr;
	}
	g_loaded_tex_world.clear();
}

static HRESULT CreateTextureFromPngMemory(IDirect3DDevice9* device, const uint8_t* data, size_t len,
	IDirect3DTexture9** outTex) {
	*outTex = nullptr;
	if (!device || !data || len == 0 || len > 0x7FFFFFFFu)
		return E_INVALIDARG;

	int iw = 0, ih = 0, channels = 0;
	unsigned char* rgba = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data), static_cast<int>(len),
	    &iw, &ih, &channels, 4);
	if (!rgba)
		return E_FAIL;
	if (iw <= 0 || ih <= 0 || iw > 8192 || ih > 8192) {
		stbi_image_free(rgba);
		return E_FAIL;
	}

	const UINT w = static_cast<UINT>(iw);
	const UINT h = static_cast<UINT>(ih);

	IDirect3DTexture9* texSys = nullptr;
	HRESULT hr = device->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &texSys, nullptr);
	if (FAILED(hr)) {
		stbi_image_free(rgba);
		return hr;
	}

	D3DLOCKED_RECT lr{};
	hr = texSys->LockRect(0, &lr, nullptr, 0);
	if (FAILED(hr)) {
		texSys->Release();
		stbi_image_free(rgba);
		return hr;
	}

	auto* dstBase = static_cast<BYTE*>(lr.pBits);
	for (UINT y = 0; y < h; ++y) {
		const unsigned char* src = rgba + static_cast<size_t>(y) * static_cast<size_t>(iw) * 4u;
		BYTE* dst = dstBase + static_cast<size_t>(y) * static_cast<size_t>(lr.Pitch);
		for (UINT x = 0; x < w; ++x) {
			dst[x * 4u + 0u] = src[x * 4u + 2u];
			dst[x * 4u + 1u] = src[x * 4u + 1u];
			dst[x * 4u + 2u] = src[x * 4u + 0u];
			dst[x * 4u + 3u] = src[x * 4u + 3u];
		}
	}
	texSys->UnlockRect(0);
	stbi_image_free(rgba);

	IDirect3DTexture9* texVid = nullptr;
	hr = device->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texVid, nullptr);
	if (FAILED(hr)) {
		texSys->Release();
		return hr;
	}
	hr = device->UpdateTexture(texSys, texVid);
	texSys->Release();
	if (FAILED(hr)) {
		texVid->Release();
		return hr;
	}
	*outTex = texVid;
	return S_OK;
}

static void ConsumeStagingAndCreateTexture() {
	std::vector<uint8_t> bytes;
	std::string forWorld;
	{
		std::lock_guard<std::mutex> lk(g_state_mtx);
		if (!g_staging_ready.load())
			return;
		bytes = std::move(g_staging_png);
		forWorld = std::move(g_staging_world);
		g_staging_ready.store(false);
	}
	if (!p_Device || bytes.empty())
		return;
	IDirect3DTexture9* nt = nullptr;
	const HRESULT hr = CreateTextureFromPngMemory(p_Device, bytes.data(), bytes.size(), &nt);
	std::lock_guard<std::mutex> lk(g_state_mtx);
	if (SUCCEEDED(hr) && nt) {
		ReleaseMapTexUnlocked();
		g_map_tex = nt;
		g_loaded_tex_world = forWorld;
		g_status_line = "Map PNG: " + forWorld;
		g_last_err.clear();
	} else {
		char b[96];
		snprintf(b, sizeof b, "Map Image Failed (HRESULT=0x%08lX)", static_cast<unsigned long>(hr));
		g_last_err = b;
		g_status_line = g_last_err;
	}
}

static void SpawnPngDownload(const std::string& worldCopy, const char* mapId) {
	if (!mapId || !mapId[0])
		return;
	g_dl_busy.store(true);
	const uint64_t token = ++g_dl_token;
	g_status_line = std::string("Downloading: ") + mapId + " ...";
	std::thread([worldCopy, mapId, token]() {
		std::string err;
		std::vector<uint8_t> buf;
		const bool ok = HttpGetRawGithubPng(mapId, buf, err);
		if (g_dl_token.load() != token) {
			g_dl_busy.store(false);
			return;
		}
		if (!ok) {
			std::lock_guard<std::mutex> lk(g_state_mtx);
			g_last_err = err;
			g_status_line = std::string("Download: ") + err;
			g_staging_ready.store(false);
			g_staging_png.clear();
			g_staging_world.clear();
			g_dl_busy.store(false);
			return;
		}
		{
			std::lock_guard<std::mutex> lk(g_state_mtx);
			g_staging_png = std::move(buf);
			g_staging_world = worldCopy;
			g_staging_ready.store(true);
		}
		g_dl_busy.store(false);
	}).detach();
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

} // namespace

void WindowRadarD3d9_Tick(const char* map_id_utf8, const char* world_name_utf8) {
	if (!map_id_utf8 || !map_id_utf8[0] || !world_name_utf8 || !world_name_utf8[0])
		return;

	ConsumeStagingAndCreateTexture();

	const std::string world = world_name_utf8;
	const bool needTex = [&]() {
		std::lock_guard<std::mutex> lk(g_state_mtx);
		return !g_map_tex || _stricmp(g_loaded_tex_world.c_str(), world.c_str()) != 0;
	}();
	const DWORD now = GetTickCount();
	const bool allow_spawn =
	    (world != s_spawn_throttle_world) || (now - s_spawn_throttle_ms > 3500u);
	if (needTex && !g_dl_busy.load() && !g_staging_ready.load() && allow_spawn) {
		s_spawn_throttle_world = world;
		s_spawn_throttle_ms = now;
		SpawnPngDownload(world, map_id_utf8);
	}
}

void WindowRadarD3d9_DrawUnderBlips(ImDrawList* dl, const ImVec2& rmin, const ImVec2& rmax, const ImVec2& uv0,
	const ImVec2& uv1, bool rotate_with_view, float view_yaw_deg, float map_w, float map_h) {
	if (!dl)
		return;

	IDirect3DTexture9* tex = nullptr;
	{
		std::lock_guard<std::mutex> lk(g_state_mtx);
		if (g_map_tex) {
			tex = g_map_tex;
			tex->AddRef();
		}
	}

	if (tex) {
		const ImVec2 ctr((rmin.x + rmax.x) * 0.5f, (rmin.y + rmax.y) * 0.5f);
		if (!rotate_with_view || fabsf(view_yaw_deg) < 0.02f) {
			dl->AddImage(reinterpret_cast<ImTextureID>(tex), rmin, rmax, uv0, uv1, IM_COL32_WHITE);
		} else {
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
			dl->AddImageQuad(reinterpret_cast<ImTextureID>(tex), p1, p2, p3, p4, u1, u2, u3, u4, IM_COL32_WHITE);
		}
		tex->Release();
		return;
	}

	dl->AddRectFilled(rmin, rmax, ImGui::GetColorU32(ImVec4(
	    c::elements::background.x, c::elements::background.y, c::elements::background.z, c::elements::background.w)));
}

void WindowRadarD3d9_Shutdown() {
	++g_dl_token;
	g_dl_busy.store(false);
	std::lock_guard<std::mutex> lk(g_state_mtx);
	ReleaseMapTexUnlocked();
	g_staging_png.clear();
	g_staging_world.clear();
	g_staging_ready.store(false);
	g_status_line.clear();
	g_last_err.clear();
}

bool WindowRadarD3d9_IsReady() {
	std::lock_guard<std::mutex> lk(g_state_mtx);
	return g_map_tex != nullptr;
}

const char* WindowRadarD3d9_LastStatus() {
	std::lock_guard<std::mutex> lk(g_state_mtx);
	return g_status_line.c_str();
}
