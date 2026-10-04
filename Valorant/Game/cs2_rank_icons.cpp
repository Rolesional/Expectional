#include "cs2_rank_icons.hpp"

#include <d3d11.h>
#include <Windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4244 4267 4996)
#define NANOSVG_IMPLEMENTATION
#include "../../ThirdParty/nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "../../ThirdParty/nanosvg/nanosvgrast.h"
#pragma warning(pop)

namespace {

constexpr int kRaster = 64;
constexpr int kSubCount = 21;
constexpr int kSlots = 42;

std::mutex g_mu;
std::array<std::vector<uint8_t>, kSlots> g_pending_rgba;
std::array<ID3D11ShaderResourceView*, kSlots> g_srv {};
std::atomic<bool> g_worker_started{ false };
std::atomic<bool> g_stop_worker{ false };
std::atomic<bool> g_worker_finished{ false };
std::thread g_worker;

static std::wstring Utf8PathToWide(const std::string& u8)
{
	if (u8.empty())
		return L"";
	const int n = MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, nullptr, 0);
	if (n <= 1)
		return L"";
	std::wstring w(static_cast<size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, w.data(), n);
	if (!w.empty() && w.back() == L'\0')
		w.pop_back();
	return w;
}

static bool HttpGetGithubSvg(const std::string& pathUtf8, std::vector<uint8_t>& out, std::string& err)
{
	out.clear();
	err.clear();
	if (pathUtf8.empty() || pathUtf8[0] != '/')
		return false;
	const std::wstring pathW = Utf8PathToWide(pathUtf8);
	if (pathW.empty()) {
		err = "path";
		return false;
	}

	HINTERNET hSession = WinHttpOpen(L"Expectional-RankIcons/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
	    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession) {
		err = "WinHttpOpen";
		return false;
	}
	HINTERNET hConnect = WinHttpConnect(hSession, L"raw.githubusercontent.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
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
		err = "send";
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}
	DWORD status = 0;
	DWORD sz = sizeof status;
	if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX) && status != 200u) {
		err = "HTTP";
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
	if (out.size() < 32u) {
		err = "small";
		return false;
	}
	return true;
}

static HRESULT CreateSrvFromRgba(ID3D11Device* device, const uint8_t* rgba, UINT w, UINT h,
    ID3D11ShaderResourceView** outSrv)
{
	*outSrv = nullptr;
	if (!device || !rgba || w == 0 || h == 0 || w > 4096 || h > 4096)
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
	*outSrv = srv;
	return S_OK;
}

static std::string BuildSvgPath(bool wingman, int sub)
{
	std::string p = "/itzarty/csgo-rank-icons/main/";
	p += wingman ? "wingman/" : "matchmaking/";
	if (sub == 19)
		p += "none.svg";
	else if (sub == 20)
		p += "expired.svg";
	else
		p += std::to_string(sub) + ".svg";
	return p;
}

static void RankIconsWorker()
{
	NSVGrasterizer* rast = nsvgCreateRasterizer();
	if (!rast) {
		g_worker_finished.store(true, std::memory_order_release);
		return;
	}
	for (int wing = 0; wing <= 1 && !g_stop_worker.load(std::memory_order_relaxed); ++wing) {
		for (int sub = 0; sub < kSubCount && !g_stop_worker.load(std::memory_order_relaxed); ++sub) {
			std::vector<uint8_t> svg;
			std::string err;
			const std::string path = BuildSvgPath(wing != 0, sub);
			if (!HttpGetGithubSvg(path, svg, err)) {
				Sleep(150);
				continue;
			}
			std::string mut(reinterpret_cast<const char*>(svg.data()), svg.size());
			mut.push_back('\0');
			NSVGimage* image = nsvgParse(mut.data(), "px", 96.f);
			if (!image) {
				Sleep(50);
				continue;
			}
			std::vector<uint8_t> rgba(static_cast<size_t>(kRaster * kRaster * 4), 0);
			const float iw = image->width > 1.f ? image->width : static_cast<float>(kRaster);
			const float ih = image->height > 1.f ? image->height : static_cast<float>(kRaster);
			const float scale = (std::min)(static_cast<float>(kRaster) / iw, static_cast<float>(kRaster) / ih);
			const float sw = iw * scale;
			const float sh = ih * scale;
			const float tx = (static_cast<float>(kRaster) - sw) * 0.5f;
			const float ty = (static_cast<float>(kRaster) - sh) * 0.5f;
			nsvgRasterize(rast, image, tx, ty, scale, rgba.data(), kRaster, kRaster, kRaster * 4);
			nsvgDelete(image);
			{
				std::lock_guard<std::mutex> lk(g_mu);
				const int idx = (wing ? kSubCount : 0) + sub;
				if (!g_stop_worker.load(std::memory_order_relaxed))
					g_pending_rgba[static_cast<size_t>(idx)] = std::move(rgba);
			}
			Sleep(100);
		}
	}
	nsvgDeleteRasterizer(rast);
	g_worker_finished.store(true, std::memory_order_release);
}

static void ReleaseAllSrvUnlocked()
{
	for (ID3D11ShaderResourceView*& s : g_srv) {
		if (s) {
			s->Release();
			s = nullptr;
		}
	}
}

} // namespace

void ExpectionalRankIconsShutdown()
{
	g_stop_worker.store(true, std::memory_order_relaxed);
	if (g_worker.joinable())
		g_worker.join();
	g_worker_finished.store(false, std::memory_order_relaxed);
	g_worker_started.store(false, std::memory_order_relaxed);
	g_stop_worker.store(false, std::memory_order_relaxed);
	std::lock_guard<std::mutex> lk(g_mu);
	ReleaseAllSrvUnlocked();
	for (auto& v : g_pending_rgba)
		v.clear();
}

void ExpectionalRankIconsFrame(ID3D11Device* device)
{
	if (!device)
		return;
	if (!g_worker_started.exchange(true)) {
		g_stop_worker.store(false, std::memory_order_relaxed);
		g_worker_finished.store(false, std::memory_order_relaxed);
		if (g_worker.joinable())
			g_worker.join();
		g_worker = std::thread(RankIconsWorker);
	}

	int uploads = 0;
	std::lock_guard<std::mutex> lk(g_mu);
	for (int i = 0; i < kSlots && uploads < 8; ++i) {
		if (g_srv[static_cast<size_t>(i)])
			continue;
		std::vector<uint8_t>& pending = g_pending_rgba[static_cast<size_t>(i)];
		if (pending.size() != static_cast<size_t>(kRaster * kRaster * 4))
			continue;
		ID3D11ShaderResourceView* srv = nullptr;
		if (SUCCEEDED(CreateSrvFromRgba(device, pending.data(), kRaster, kRaster, &srv)) && srv) {
			g_srv[static_cast<size_t>(i)] = srv;
			pending.clear();
			++uploads;
		}
	}
}

void* ExpectionalRankIconTexture(bool wingman, int sub)
{
	if (sub < 0 || sub > 20)
		return nullptr;
	const int idx = (wingman ? kSubCount : 0) + sub;
	std::lock_guard<std::mutex> lk(g_mu);
	return g_srv[static_cast<size_t>(idx)];
}
