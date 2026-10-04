#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "faceit_rank_query.hpp"
#include "rank_reveal_cache.hpp"
#include "expectional_reveal_workers.hpp"
#include "cs2_competitive_rank_type.hpp"
#include "expectional_misc_runtime.hpp"
#include "globals.hpp"
#include "structs.hpp"
#include "../Driver/driver.hpp"
#include "../OSImGui/shade_imgui_settings.h"
#include "../OSImGui/os_imgui_menu.hpp"

#include <Windows.h>
#include <winhttp.h>
#include <Shlwapi.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "Shlwapi.lib")

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../Includes/Imgui/imgui.h"

namespace {

static constexpr const char kExpectionalFaceitBearer[] = "7ed60fec-6e2d-4f36-b946-0f4ca3297e25";

struct FaceitRow {
	std::string nickname;
	int skill_level = -1;
	int faceit_elo = -1;
	std::string status;
	uint64_t fetched_tick_ms = 0;
};

std::mutex g_faceit_mu;
std::unordered_map<std::uint64_t, FaceitRow> g_faceit_cache;
std::deque<std::uint64_t> g_faceit_queue;
std::unordered_set<std::uint64_t> g_faceit_queued_set;

std::unordered_set<std::uint64_t> g_faceit_inflight;

struct SteamInvRow {
	int64_t value_cents = -1;
	std::string status;
	bool truncated = false;
	uint64_t fetched_tick_ms = 0;
	
	uint32_t refetch_after_ms = 0;
};

std::mutex g_steam_inv_mu;
std::unordered_map<std::uint64_t, SteamInvRow> g_steam_inv_cache;
std::deque<std::uint64_t> g_steam_inv_queue;
std::unordered_set<std::uint64_t> g_steam_inv_queued_set;

std::unordered_set<std::uint64_t> g_steam_inv_inflight;

std::mutex g_steam_price_mu;

std::unordered_map<std::string, int64_t> g_steam_price_cents;

std::atomic<bool> g_worker_stop{ false };
std::thread g_worker;
std::thread g_steam_worker;

static int JsonIntAfter(const std::string& j, size_t from)
{
	while (from < j.size() && (j[from] == ' ' || j[from] == '\t' || j[from] == '\n' || j[from] == '\r'))
		++from;
	if (from >= j.size() || j[from] == 'n')
		return -1;
	const char* p = j.c_str() + from;
	char* end = nullptr;
	const long long v = std::strtoll(p, &end, 10);
	if (end == p)
		return -1;
	if (v > 2147483647LL)
		return 2147483647;
	if (v < -2147483648LL)
		return -2147483648;
	return static_cast<int>(v);
}

static bool JsonExtractIntKey(const std::string& j, const char* key, int& out)
{
	const std::string needle = std::string("\"") + key + "\":";
	const size_t p = j.find(needle);
	if (p == std::string::npos)
		return false;
	const size_t colon = p + needle.size() - 1;
	size_t at = colon + 1;
	while (at < j.size() && (j[at] == ' ' || j[at] == '\t' || j[at] == '\n' || j[at] == '\r'))
		++at;
	if (at + 3 < j.size() && j[at] == 'n' && j.compare(at, 4, "null") == 0)
		return false;
	const int v = JsonIntAfter(j, colon + 1);
	out = v;
	return true;
}

static bool JsonExtractString(const std::string& j, const char* key, std::string& out)
{
	out.clear();
	const std::string needle = std::string("\"") + key + "\":\"";
	const size_t p = j.find(needle);
	if (p == std::string::npos)
		return false;
	size_t q = p + needle.size();
	while (q < j.size() && j[q] != '"') {
		if (j[q] == '\\' && q + 1 < j.size()) {
			out.push_back(j[q + 1]);
			q += 2;
			continue;
		}
		out.push_back(j[q]);
		++q;
	}
	return !out.empty();
}

static std::wstring Utf8ToWide(const std::string& u8)
{
	if (u8.empty())
		return {};
	int n = MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, nullptr, 0);
	if (n <= 1)
		return {};
	std::wstring w(static_cast<size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, w.data(), n);
	if (!w.empty() && w.back() == L'\0')
		w.pop_back();
	return w;
}

static bool HttpHttpsGet(const wchar_t* userAgent, const wchar_t* host, const std::wstring& pathW,
    const std::wstring& extraHeaders, std::string& bodyOut, DWORD& statusOut, std::string& errOut)
{
	bodyOut.clear();
	errOut.clear();
	statusOut = 0;
	std::wstring hdr = extraHeaders;

	HINTERNET hSession = WinHttpOpen(userAgent ? userAgent : L"Expectional/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
	    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession) {
		errOut = "WinHttpOpen";
		return false;
	}
#if defined(WINHTTP_OPTION_DECOMPRESSION) && defined(WINHTTP_DECOMPRESSION_FLAG_ALL)
	{
		DWORD decomp = WINHTTP_DECOMPRESSION_FLAG_ALL;
		(void)WinHttpSetOption(hSession, WINHTTP_OPTION_DECOMPRESSION, &decomp, sizeof decomp);
	}
#endif
	DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
	WinHttpSetOption(hSession, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof redirectPolicy);
	{
		DWORD secureProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
		secureProtocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#else
		secureProtocols |= 0x2000;
#endif
		WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &secureProtocols, sizeof secureProtocols);
	}

	HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!hConnect) {
		errOut = "WinHttpConnect";
		WinHttpCloseHandle(hSession);
		return false;
	}
	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", pathW.c_str(), nullptr, WINHTTP_NO_REFERER,
	    WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!hRequest) {
		errOut = "WinHttpOpenRequest";
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}
	const wchar_t* hdrPtr = hdr.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdr.c_str();
	const DWORD hdrLen = hdr.empty() ? 0 : static_cast<DWORD>(-1L);
	if (!WinHttpSendRequest(hRequest, hdrPtr, hdrLen, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
	    !WinHttpReceiveResponse(hRequest, nullptr)) {
		errOut = "WinHttpSendRequest/ReceiveResponse";
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}
	DWORD status = 0;
	DWORD sz = sizeof status;
	if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX))
		statusOut = status;

	for (;;) {
		DWORD avail = 0;
		if (!WinHttpQueryDataAvailable(hRequest, &avail))
			break;
		if (avail == 0)
			break;
		const size_t old = bodyOut.size();
		bodyOut.resize(old + avail);
		DWORD read = 0;
		if (!WinHttpReadData(hRequest, bodyOut.data() + old, avail, &read) || read == 0) {
			bodyOut.resize(old);
			break;
		}
		bodyOut.resize(old + read);
	}
	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	return true;
}

static bool HttpGetBearer(const wchar_t* host, const std::wstring& pathW, const std::string& bearerToken,
    std::string& bodyOut, DWORD& statusOut, std::string& errOut)
{
	std::wstring hdr = L"Authorization: Bearer ";
	hdr += Utf8ToWide(bearerToken);
	hdr += L"\r\n";
	return HttpHttpsGet(L"Expectional-FaceIT/1.0", host, pathW, hdr, bodyOut, statusOut, errOut);
}

static std::string UrlEncodeMarketComponent(const std::string& s)
{
	static const char* hx = "0123456789ABCDEF";
	std::string o;
	o.reserve(s.size() * 3);
	for (unsigned char c : s) {
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
		    c == '.' || c == '~')
			o.push_back(static_cast<char>(c));
		else {
			o.push_back('%');
			o.push_back(hx[c >> 4]);
			o.push_back(hx[c & 15]);
		}
	}
	return o;
}

static std::wstring SteamMarketNameEscapedW(const std::string& marketHashUtf8)
{
	std::wstring in = Utf8ToWide(marketHashUtf8);
	if (in.empty())
		return L"";
	DWORD cap = static_cast<DWORD>(std::max<size_t>(in.length() * 6u + 32u, 256u));
	for (int k = 0; k < 12; ++k) {
		std::wstring buf(cap, L'\0');
		DWORD cch = cap;
		const HRESULT hr = UrlEscapeW(in.c_str(), buf.data(), &cch, URL_ESCAPE_SEGMENT_ONLY | URL_ESCAPE_PERCENT);
		if (SUCCEEDED(hr)) {
			buf.resize(cch);
			while (!buf.empty() && buf.back() == L'\0')
				buf.pop_back();
			return buf;
		}
		if (hr == E_POINTER || static_cast<unsigned long>(hr) == 0x8007007A)
			cap = (std::max)(cch + 16u, cap + cap / 2u);
		else
			break;
	}
	return Utf8ToWide(UrlEncodeMarketComponent(marketHashUtf8));
}

static std::mutex g_steam_net_mu;
static uint64_t g_steam_next_http_ok_ms = 0;

static void SteamNetThrottle()
{
	std::lock_guard<std::mutex> lk(g_steam_net_mu);
	const uint64_t now = GetTickCount64();
	constexpr uint64_t kGap = 700;
	if (now < g_steam_next_http_ok_ms) {
		const uint64_t w = g_steam_next_http_ok_ms - now;
		if (w > 0 && w < 30000)
			Sleep(static_cast<DWORD>(w));
	}
	g_steam_next_http_ok_ms = GetTickCount64() + kGap;
}

static bool SteamCommunityGet(const std::wstring& pathW, std::string& bodyOut, DWORD& statusOut, std::string& errOut)
{
	SteamNetThrottle();
	const std::wstring hdr =
	    L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36\r\n"
	    L"Accept: application/json, text/javascript, */*;q=0.01\r\n"
	    L"Referer: https://steamcommunity.com/market/search?appid=730\r\n";
	const bool ok = HttpHttpsGet(L"Expectional-SteamInv/1.0", L"steamcommunity.com", pathW, hdr, bodyOut, statusOut, errOut);
	return ok;
}

static bool SteamInventoryGetOnHost(std::uint64_t steam64, const wchar_t* host, const std::wstring& pathW,
    std::string& bodyOut, DWORD& statusOut, std::string& errOut)
{
	SteamNetThrottle();
	std::wstring hdr =
	    L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36\r\n"
	    L"Accept: */*\r\n"
	    L"Accept-Encoding: gzip, deflate\r\n"
	    L"X-Requested-With: XMLHttpRequest\r\n"
	    L"Referer: https://steamcommunity.com/profiles/";
	hdr += Utf8ToWide(std::to_string(steam64));
	hdr += L"/inventory/\r\n";
	const bool ok = HttpHttpsGet(L"Expectional-SteamInv/1.0", host, pathW, hdr, bodyOut, statusOut, errOut);
	return ok;
}

static bool SteamInventoryGetReliableOnHost(std::uint64_t steam64, const wchar_t* host, const std::wstring& pathW,
    std::string& bodyOut, DWORD& statusOut, std::string& errOut)
{
	const int kMax = 3;
	for (int attempt = 0; attempt < kMax; ++attempt) {
		if (!SteamInventoryGetOnHost(steam64, host, pathW, bodyOut, statusOut, errOut))
			return false;
		if (statusOut != 429u)
			return true;
		const uint64_t cool = 30000ull + 30000ull * static_cast<unsigned>(attempt);
		{
			std::lock_guard<std::mutex> lk(g_steam_net_mu);
			const uint64_t now = GetTickCount64();
			g_steam_next_http_ok_ms = (std::max)(g_steam_next_http_ok_ms, now + cool);
		}
		Sleep(static_cast<DWORD>(cool));
	}
	return true;
}

static bool SteamCommunityGetReliable(const std::wstring& pathW, std::string& bodyOut, DWORD& statusOut, std::string& errOut)
{
	const int kMax = 3;
	for (int attempt = 0; attempt < kMax; ++attempt) {
		if (!SteamCommunityGet(pathW, bodyOut, statusOut, errOut))
			return false;
		if (statusOut != 429u)
			return true;
		const uint64_t cool = 30000ull + 30000ull * static_cast<unsigned>(attempt);
		{
			std::lock_guard<std::mutex> lk(g_steam_net_mu);
			const uint64_t now = GetTickCount64();
			g_steam_next_http_ok_ms = (std::max)(g_steam_next_http_ok_ms, now + cool);
		}
		Sleep(static_cast<DWORD>(cool));
	}
	return true;
}

static bool SteamInventoryGetReliable(std::uint64_t steam64, const std::wstring& pathW, std::string& bodyOut,
    DWORD& statusOut, std::string& errOut)
{
	return SteamInventoryGetReliableOnHost(steam64, L"steamcommunity.com", pathW, bodyOut, statusOut, errOut);
}

static void JsonUnescapeUInPlace(std::string& s)
{
	for (size_t i = 0; i + 5 < s.size(); ++i) {
		if (s[i] != '\\' || s[i + 1] != 'u')
			continue;
		unsigned code = 0;
		bool ok = true;
		for (int k = 0; k < 4; ++k) {
			const char c = s[i + 2u + static_cast<unsigned>(k)];
			code <<= 4u;
			if (c >= '0' && c <= '9')
				code |= static_cast<unsigned>(c - '0');
			else if (c >= 'a' && c <= 'f')
				code |= 10u + static_cast<unsigned>(c - 'a');
			else if (c >= 'A' && c <= 'F')
				code |= 10u + static_cast<unsigned>(c - 'A');
			else {
				ok = false;
				break;
			}
		}
		if (!ok || code == 0 || code > 0x10FFFF)
			continue;
		std::string rep;
		if (code < 0x80)
			rep.push_back(static_cast<char>(code));
		else if (code < 0x800) {
			rep.push_back(static_cast<char>(0xC0 | static_cast<int>(code >> 6)));
			rep.push_back(static_cast<char>(0x80 | static_cast<int>(code & 63)));
		} else {
			rep.push_back(static_cast<char>(0xE0 | static_cast<int>(code >> 12)));
			rep.push_back(static_cast<char>(0x80 | static_cast<int>((code >> 6) & 63)));
			rep.push_back(static_cast<char>(0x80 | static_cast<int>(code & 63)));
		}
		s.replace(i, 6u, rep);
	}
}

static int64_t ParseSteamUsdPriceToCents(const std::string& body)
{
	size_t p = body.find("\"median_price\":\"");
	if (p == std::string::npos)
		p = body.find("\"lowest_price\":\"");
	if (p == std::string::npos)
		return -1;
	p = body.find(':', p);
	if (p == std::string::npos)
		return -1;
	p = body.find('"', p);
	if (p == std::string::npos)
		return -1;
	++p;
	size_t e = p;
	while (e < body.size() && body[e] != '"') {
		if (body[e] == '\\' && e + 1 < body.size()) {
			e += 2;
			continue;
		}
		++e;
	}
	std::string frag = body.substr(p, e - p);
	std::string digits;
	for (char c : frag) {
		if ((c >= '0' && c <= '9') || c == '.')
			digits.push_back(c);
	}
	if (digits.empty())
		return -1;
	const double v = std::strtod(digits.c_str(), nullptr);
	if (v <= 0.0 || v > 1e9)
		return -1;
	return static_cast<int64_t>(std::llround(v * 100.0));
}

static bool ExtractJsonArrayInner(const std::string& body, const char* key, std::string& inner)
{
	const std::string needle = std::string("\"") + key + "\":";
	const size_t p = body.find(needle);
	if (p == std::string::npos)
		return false;
	const size_t lb = body.find('[', p);
	if (lb == std::string::npos)
		return false;
	int depth = 0;
	for (size_t i = lb; i < body.size(); ++i) {
		if (body[i] == '[')
			++depth;
		else if (body[i] == ']') {
			--depth;
			if (depth == 0) {
				inner = body.substr(lb + 1, i - lb - 1);
				return true;
			}
		}
	}
	return false;
}

static void ForEachTopLevelJsonObject(const std::string& inner, const std::function<void(const std::string&)>& onObj)
{
	size_t i = 0;
	while (i < inner.size()) {
		while (i < inner.size() && inner[i] != '{')
			++i;
		if (i >= inner.size())
			break;
		const size_t start = i;
		int dep = 0;
		for (; i < inner.size(); ++i) {
			if (inner[i] == '{')
				++dep;
			else if (inner[i] == '}') {
				--dep;
				if (dep == 0) {
					onObj(inner.substr(start, i - start + 1));
					++i;
					break;
				}
			}
		}
	}
}

static int64_t CsgoBackpackInvValueCents(std::uint64_t steam64, DWORD& httpOut, std::string& errOut)
{
	httpOut = 0;
	errOut.clear();
	const std::wstring pathW =
	    Utf8ToWide(std::string("/api/GetInventoryValue/?id=") + std::to_string(steam64) + "&currency=USD");
	const std::wstring hdr =
	    L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36\r\n"
	    L"Accept: application/json, */*;q=0.1\r\n";
	std::string body;
	const bool netOk = HttpHttpsGet(L"Expectional-Inv/1.0", L"csgobackpack.net", pathW, hdr, body, httpOut, errOut);
	if (!netOk)
		return -1;
	if (httpOut != 200u)
		return -1;
	if (body.find("\"success\":true") == std::string::npos &&
	    body.find("\"success\": true") == std::string::npos) {
		if (body.find("\"success\":false") != std::string::npos ||
		    body.find("\"success\": false") != std::string::npos) {
			return -2;
		}
		return -1;
	}
	size_t p = body.find("\"value\":");
	if (p == std::string::npos)
		p = body.find("\"value\" :");
	if (p == std::string::npos)
		return -1;
	p = body.find(':', p);
	if (p == std::string::npos)
		return -1;
	++p;
	while (p < body.size() && (body[p] == ' ' || body[p] == '\t'))
		++p;
	const bool quoted = (p < body.size() && body[p] == '"');
	if (quoted)
		++p;
	std::string digits;
	while (p < body.size() && digits.size() < 32u) {
		const char c = body[p];
		if ((c >= '0' && c <= '9') || c == '.')
			digits.push_back(c);
		else if (c == ',')
			;
		else
			break;
		++p;
	}
	if (digits.empty()) {
		return -1;
	}
	const double v = std::strtod(digits.c_str(), nullptr);
	if (v < 0.0 || v > 1e9) {
		return -1;
	}
	const int64_t cents = static_cast<int64_t>(std::llround(v * 100.0));
	return cents;
}

static void SteamInvFetchOne(std::uint64_t steam64)
{
	SteamInvRow row;
	row.fetched_tick_ms = GetTickCount64();

	const std::wstring path75 =
	    Utf8ToWide(std::string("/inventory/") + std::to_string(steam64) + "/730/2?l=english&count=75");
	const std::wstring path2000 =
	    Utf8ToWide(std::string("/inventory/") + std::to_string(steam64) + "/730/2?l=english&count=2000");
	std::string body;
	DWORD http = 0;
	std::string err;
	const auto bodyLooksInventory = [](const std::string& b) {
		return b.size() >= 8u && b[0] == '{' &&
		    (b.find("\"descriptions\"") != std::string::npos ||
		     b.find("\"rgInventory\"") != std::string::npos ||
		     b.find("\"assets\"") != std::string::npos);
	};
	const auto tryInv = [&](const wchar_t* host, const std::wstring& pathW, bool markTruncated) -> bool {
		std::string tmp;
		DWORD st = 0;
		std::string er;
		if (!SteamInventoryGetReliableOnHost(steam64, host, pathW, tmp, st, er) || st != 200u ||
		    !bodyLooksInventory(tmp))
			return false;
		body = std::move(tmp);
		http = 200u;
		err = std::move(er);
		if (markTruncated)
			row.truncated = true;
		return true;
	};
	bool inventoryReady = false;
	
	if (tryInv(L"steamcommunity.com", path75, true))
		inventoryReady = true;
	else if (tryInv(L"steamcommunity.com", path2000, false))
		inventoryReady = true;
	else if (tryInv(L"www.steamcommunity.com", path75, true))
		inventoryReady = true;
	else if (tryInv(L"www.steamcommunity.com", path2000, false))
		inventoryReady = true;
	if (!inventoryReady) {
		const std::wstring legacyPath =
		    Utf8ToWide(std::string("/profiles/") + std::to_string(steam64) + "/inventory/json/730/2");
		std::string lbody;
		DWORD lhttp = 0;
		std::string lerr;
		if (SteamInventoryGetReliableOnHost(steam64, L"steamcommunity.com", legacyPath, lbody, lhttp, lerr) &&
		    lhttp == 200u && bodyLooksInventory(lbody)) {
			body = std::move(lbody);
			http = 200u;
			row.truncated = true;
			inventoryReady = true;
		} else if (SteamInventoryGetReliableOnHost(steam64, L"www.steamcommunity.com", legacyPath, lbody, lhttp,
			     lerr) &&
		    lhttp == 200u && bodyLooksInventory(lbody)) {
			body = std::move(lbody);
			http = 200u;
			row.truncated = true;
			inventoryReady = true;
		} else if (lhttp != 0u) {
			http = lhttp;
		}
	}

	if (!inventoryReady) {
		DWORD httpCb = 0;
		std::string errCb;
		const int64_t cbCents = CsgoBackpackInvValueCents(steam64, httpCb, errCb);
		if (cbCents >= 0) {
			row.value_cents = cbCents;
			row.status = "OK";
			std::lock_guard<std::mutex> lk(g_steam_inv_mu);
			g_steam_inv_cache[steam64] = std::move(row);
			return;
		}
		if (cbCents == -2) {
			row.status = "Gizli/bos";
			row.refetch_after_ms = 600000;
			std::lock_guard<std::mutex> lk(g_steam_inv_mu);
			g_steam_inv_cache[steam64] = std::move(row);
			return;
		}
	}

	if (!inventoryReady) {
		if (http == 429u) {
			row.status = "Steam limiti";
			row.refetch_after_ms = 75000;
		} else if (http == 400u || http == 0u) {
			row.status = "Steam istek";
			row.refetch_after_ms = 300000;
		} else if (http == 403u) {
			row.status = "Gizli/bos";
			row.refetch_after_ms = 600000;
		} else if (body.find("\"success\":false") != std::string::npos) {
			row.status = "Gizli/bos";
			row.refetch_after_ms = 600000;
		} else if (http == 200u) {
			row.status = "Gizli/bos";
			row.refetch_after_ms = 600000;
		} else {
			row.status = "Steam ag";
			row.refetch_after_ms = 180000;
		}
		std::lock_guard<std::mutex> lk(g_steam_inv_mu);
		g_steam_inv_cache[steam64] = std::move(row);
		return;
	}

	std::unordered_map<std::string, std::string> classid_to_hash;
	std::string descInner;
	if (ExtractJsonArrayInner(body, "descriptions", descInner)) {
		ForEachTopLevelJsonObject(descInner, [&](const std::string& ob) {
			const bool canSell = ob.find("\"marketable\":1") != std::string::npos ||
			    ob.find("\"marketable\":true") != std::string::npos;
			if (!canSell)
				return;
			std::string cid;
			std::string hash;
			if (!JsonExtractString(ob, "classid", cid) || cid.empty())
				return;
			if (!JsonExtractString(ob, "market_hash_name", hash) || hash.empty())
				return;
			JsonUnescapeUInPlace(hash);
			classid_to_hash[cid] = std::move(hash);
		});
	}

	std::unordered_map<std::string, int64_t> classid_qty;
	std::string assetsInner;
	if (ExtractJsonArrayInner(body, "assets", assetsInner)) {
		ForEachTopLevelJsonObject(assetsInner, [&](const std::string& ob) {
			std::string cid;
			if (!JsonExtractString(ob, "classid", cid) || cid.empty())
				return;
			std::string amt = "1";
			(void)JsonExtractString(ob, "amount", amt);
			int64_t q = 1;
			char* e = nullptr;
			const long long v = std::strtoll(amt.c_str(), &e, 10);
			if (e != amt.c_str() && v > 0 && v < 100000)
				q = v;
			classid_qty[cid] += q;
		});
	}

	std::unordered_map<std::string, int64_t> hash_qty;
	for (const auto& cq : classid_qty) {
		const auto it = classid_to_hash.find(cq.first);
		if (it == classid_to_hash.end())
			continue;
		hash_qty[it->second] += cq.second;
	}

	std::vector<std::pair<std::string, int64_t>> order(hash_qty.begin(), hash_qty.end());
	std::sort(order.begin(), order.end(),
	    [](const std::pair<std::string, int64_t>& a, const std::pair<std::string, int64_t>& b) {
		    return a.second > b.second;
	    });

	constexpr int kMaxPricedNames = 14;
	int64_t totalCents = 0;
	bool steamLimited = false;
	for (size_t idx = 0; idx < order.size() && static_cast<int>(idx) < kMaxPricedNames; ++idx) {
		const std::string& h = order[idx].first;
		const int64_t q = order[idx].second;
		int64_t pc = -3;
		{
			std::lock_guard<std::mutex> lk(g_steam_price_mu);
			const auto pit = g_steam_price_cents.find(h);
			if (pit != g_steam_price_cents.end())
				pc = pit->second;
		}
		if (pc >= 0) {
			totalCents += pc * q;
			continue;
		}
		if (pc == -2)
			continue;

		const std::wstring pathPriceW =
		    std::wstring(L"/market/priceoverview/?appid=730&currency=1&market_hash_name=") + SteamMarketNameEscapedW(h);
		std::string pj;
		DWORD st2 = 0;
		std::string er2;
		int64_t parsed = -2;
		if (SteamCommunityGetReliable(pathPriceW, pj, st2, er2) && st2 == 200) {
			const int64_t v = ParseSteamUsdPriceToCents(pj);
			parsed = (v >= 0) ? v : static_cast<int64_t>(-2);
		} else if (st2 == 429) {
			steamLimited = true;
			row.truncated = true;
			row.refetch_after_ms = 90000;
			break;
		} else if (st2 == 400) {
			parsed = -2;
		}
		{
			std::lock_guard<std::mutex> lk(g_steam_price_mu);
			g_steam_price_cents[h] = parsed;
		}
		if (parsed >= 0)
			totalCents += parsed * q;
	}

	row.truncated = row.truncated || (order.size() > static_cast<size_t>(kMaxPricedNames));
	row.value_cents = totalCents;
	if (steamLimited)
		row.status = (totalCents > 0) ? "Kismi (~limit)" : "Steam limiti";
	else
		row.status = "OK";
	{
		std::lock_guard<std::mutex> lk(g_steam_inv_mu);
		g_steam_inv_cache[steam64] = std::move(row);
	}
}

static void FaceitFetchOne(std::uint64_t steam64, const char* apiKey)
{
	const std::string pathUtf8 =
	    std::string("/data/v4/players?game=cs2&game_player_id=") + std::to_string(steam64);
	const std::wstring pathW = Utf8ToWide(pathUtf8);
	if (pathW.empty()) {
		std::lock_guard<std::mutex> lk(g_faceit_mu);
		FaceitRow& r = g_faceit_cache[steam64];
		r.status = "path";
		r.fetched_tick_ms = GetTickCount64();
		return;
	}

	std::string body;
	DWORD httpStatus = 0;
	std::string err;
	if (!HttpGetBearer(L"open.faceit.com", pathW, apiKey ? apiKey : "", body, httpStatus, err)) {
		std::lock_guard<std::mutex> lk(g_faceit_mu);
		FaceitRow& r = g_faceit_cache[steam64];
		r.status = err.empty() ? "HTTP" : err;
		r.fetched_tick_ms = GetTickCount64();
		return;
	}

	FaceitRow row;
	row.fetched_tick_ms = GetTickCount64();
	if (httpStatus == 404) {
		row.status = "none";
	} else if (httpStatus == 401 || httpStatus == 403) {
		row.status = "API key";
	} else if (httpStatus != 200) {
		row.status = "HTTP " + std::to_string(httpStatus);
	} else {
		row.status = "OK";
		JsonExtractString(body, "nickname", row.nickname);
		const size_t games = body.find("\"games\"");
		const size_t cs2 = games != std::string::npos ? body.find("\"cs2\"", games) : std::string::npos;
		if (cs2 != std::string::npos) {
			const size_t sliceEnd = std::min(cs2 + static_cast<size_t>(8000), body.size());
			const std::string slice = body.substr(cs2, sliceEnd - cs2);
			int skTmp = -1;
			if (JsonExtractIntKey(slice, "skill_level", skTmp))
				row.skill_level = skTmp;
			int eloTmp = -1;
			if (JsonExtractIntKey(slice, "faceit_elo", eloTmp))
				row.faceit_elo = eloTmp;
			else if (JsonExtractIntKey(slice, "elo", eloTmp))
				row.faceit_elo = eloTmp;
		}
		if (row.faceit_elo < 0) {
			size_t scan = 0;
			int bestElo = -1;
			for (;;) {
				const size_t k = body.find("\"faceit_elo\"", scan);
				if (k == std::string::npos)
					break;
				const size_t colon = body.find(':', k);
				if (colon != std::string::npos) {
					size_t at = colon + 1;
					while (at < body.size() && (body[at] == ' ' || body[at] == '\t' || body[at] == '\n' || body[at] == '\r'))
						++at;
					if (!(at + 3 < body.size() && body[at] == 'n' && body.compare(at, 4, "null") == 0)) {
						const int v = JsonIntAfter(body, colon + 1);
						if (v > 0 && v < 60000)
							bestElo = v;
					}
				}
				scan = k + 12;
			}
			if (bestElo >= 0)
				row.faceit_elo = bestElo;
		}
		if (row.nickname.empty() && httpStatus == 200)
			row.status = "JSON?";
	}

	std::lock_guard<std::mutex> lk(g_faceit_mu);
	g_faceit_cache[steam64] = std::move(row);
}

static void WorkerLoop()
{
	for (; !g_worker_stop.load(std::memory_order_relaxed);) {
		std::uint64_t next = 0;
		{
			std::lock_guard<std::mutex> lk(g_faceit_mu);
			if (!g_faceit_queue.empty()) {
				next = g_faceit_queue.front();
				g_faceit_queue.pop_front();
				g_faceit_queued_set.erase(next);
				g_faceit_inflight.insert(next);
			}
		}
		if (!next) {
			Sleep(200);
			continue;
		}
		FaceitFetchOne(next, kExpectionalFaceitBearer);
		{
			std::lock_guard<std::mutex> lk(g_faceit_mu);
			g_faceit_inflight.erase(next);
		}
		Sleep(350);
	}
}

static void SteamWorkerLoop()
{
	for (; !g_worker_stop.load(std::memory_order_relaxed);) {
		std::uint64_t next = 0;
		{
			std::lock_guard<std::mutex> lk(g_steam_inv_mu);
			if (!g_steam_inv_queue.empty()) {
				next = g_steam_inv_queue.front();
				g_steam_inv_queue.pop_front();
				g_steam_inv_queued_set.erase(next);
				g_steam_inv_inflight.insert(next);
			}
		}
		if (!next) {
			Sleep(240);
			continue;
		}
		if (!Settings::misc::rank_reveal_inventory_enabled) {
			std::lock_guard<std::mutex> lk(g_steam_inv_mu);
			g_steam_inv_inflight.erase(next);
			continue;
		}
		SteamInvFetchOne(next);
		{
			std::lock_guard<std::mutex> lk(g_steam_inv_mu);
			g_steam_inv_inflight.erase(next);
		}
	}
}

static bool RankRevealUsesPremierRating(int rankType, int ranking) noexcept
{
	if (expectional::cs2::RankTypeIsPremier(rankType))
		return true;
	if (ranking > 18)
		return true;
	return false;
}

static bool RankingIsLegacyIndex(int ranking) noexcept
{
	return ranking >= 1 && ranking <= 18;
}

static void RankRevealDrawPremierPredDelta(const ImVec4& col, int curRanking, int predAbs) noexcept
{
	if (!predAbs) {
		ImGui::TextUnformatted("");
		return;
	}
	if (RankingIsLegacyIndex(curRanking)) {
		ImGui::TextUnformatted("");
		return;
	}
	const int baseline = curRanking < 0 ? 0 : curRanking;
	const int d = predAbs - baseline;
	ImGui::TextColored(col, "%+d", d);
}

static ImVec4 RankRevealPremierRatingColor(int rating) noexcept
{
	if (rating < 5000)
		return ImVec4(1.f, 1.f, 1.f, 1.f);
	if (rating < 10000)
		return ImVec4(0.58f, 0.82f, 1.f, 1.f);
	if (rating < 15000)
		return ImVec4(0.26f, 0.52f, 1.f, 1.f);
	if (rating < 20000)
		return ImVec4(0.74f, 0.60f, 1.f, 1.f);
	if (rating < 25000)
		return ImVec4(1.f, 0.48f, 0.80f, 1.f);
	if (rating < 30000)
		return ImVec4(0.98f, 0.22f, 0.26f, 1.f);
	return ImVec4(1.f, 0.92f, 0.22f, 1.f);
}

static const char* Cs2RankLabelShort(int ranking, int rankType)
{
	(void)rankType;
	if (ranking <= 0)
		return "-";
	if (ranking < 19) {
		static const char* kLegacy[] = {
			"-",
			"SILVER I", "SILVER II", "SILVER III", "SILVER IV", "SILVER ELITE", "SEM",
			"G NOVA I", "G NOVA II", "G NOVA III", "G NOVA MASTER",
			"MG I", "MG II", "MGE", "DMG", "LE", "LEM", "SMFC", "GLOBAL",
		};
		if (ranking >= 0 && ranking < (int)(sizeof kLegacy / sizeof kLegacy[0]))
			return kLegacy[ranking];
	}
	return "-";
}

} 

void ExpectionalFaceitWorkerEnsureStarted()
{
	static std::once_flag once;
	std::call_once(once, [] {
		g_worker_stop.store(false, std::memory_order_relaxed);
		g_worker = std::thread(WorkerLoop);
		g_steam_worker = std::thread(SteamWorkerLoop);
	});
}

void ExpectionalFaceitWorkerNotifyFrame()
{
	if (!ExpectionalRankRevealWantsNetworkPoll())
		return;
	const DWORD faceitPoll = Settings::misc::save_fps ? 800u : 400u;
	if (!ex_misc::PollMs(faceitPoll, ex_misc::PollSlot::FaceitNotify))
		return;
	ExpectionalFaceitWorkerEnsureStarted();

	std::vector<std::uint64_t> steamIds;
	rank_reveal_cache::CollectSteamIds(steamIds);

	const uint64_t now = GetTickCount64();
	{
		std::lock_guard<std::mutex> lk(g_faceit_mu);
		for (std::uint64_t sid : steamIds) {
			const auto it = g_faceit_cache.find(sid);
			const bool stale = it == g_faceit_cache.end() || (now - it->second.fetched_tick_ms > 120000ull);
			if (!stale)
				continue;
			if (g_faceit_queued_set.count(sid))
				continue;
			if (g_faceit_inflight.count(sid))
				continue;
			g_faceit_queued_set.insert(sid);
			g_faceit_queue.push_back(sid);
			if (g_faceit_queue.size() > 64u) {
				const std::uint64_t drop = g_faceit_queue.front();
				g_faceit_queue.pop_front();
				g_faceit_queued_set.erase(drop);
			}
		}
	}
	if (Settings::misc::rank_reveal_inventory_enabled) {
		std::lock_guard<std::mutex> lk(g_steam_inv_mu);
		for (std::uint64_t sid : steamIds) {
			const auto it = g_steam_inv_cache.find(sid);
			const uint64_t ttlMs =
			    (it != g_steam_inv_cache.end() && it->second.refetch_after_ms != 0u)
			        ? static_cast<uint64_t>(it->second.refetch_after_ms)
			        : 1200000ull;
			const bool stale =
			    it == g_steam_inv_cache.end() || (now - it->second.fetched_tick_ms > ttlMs);
			if (!stale)
				continue;
			if (g_steam_inv_queued_set.count(sid))
				continue;
			if (g_steam_inv_inflight.count(sid))
				continue;
			g_steam_inv_queued_set.insert(sid);
			g_steam_inv_queue.push_back(sid);
			if (g_steam_inv_queue.size() > 48u) {
				const std::uint64_t drop = g_steam_inv_queue.front();
				g_steam_inv_queue.pop_front();
				g_steam_inv_queued_set.erase(drop);
			}
		}
	}
}

static ImVec4 RankRevealFaceitLevelColor(int skillLevel) noexcept
{
	if (skillLevel >= 1 && skillLevel <= 2)
		return ImVec4(1.f, 1.f, 1.f, 1.f);
	if (skillLevel >= 3 && skillLevel <= 5)
		return ImVec4(0.32f, 0.92f, 0.44f, 1.f);
	if (skillLevel >= 6 && skillLevel <= 7)
		return ImVec4(1.f, 0.58f, 0.15f, 1.f);
	if (skillLevel >= 8 && skillLevel <= 10)
		return ImVec4(0.98f, 0.30f, 0.34f, 1.f);
	return c::elements::text;
}

void ExpectionalDrawRankRevealWindow()
{
	if (!Settings::misc::rank_reveal_window)
		return;
	const bool menuOpen = Settings::bMenu;
	const bool tabHeld = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
	if (!menuOpen && !tabHeld)
		return;

	ImGui::SetNextWindowPos(ImVec2(48.f, 300.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(536, 244), ImGuiCond_FirstUseEver);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 0.f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, c::background::rounding);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
	ImGui::PushStyleColor(ImGuiCol_Border, c::background::stroke);
	ImGui::PushStyleColor(ImGuiCol_Text, c::elements::text_active);

	ExpectionalOsMenu_UiFontScope uiFont;

	const ImGuiWindowFlags kRevWin = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
	    ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoTitleBar;

	if (!ImGui::Begin("Rank Revealer##ExpectionalRank", nullptr, kRevWin)) {
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(3);
		ImGui::End();
		return;
	}

	const ImVec2 wp = ImGui::GetWindowPos();
	const ImVec2 ws = ImGui::GetWindowSize();
	ImDrawList* wdl = ImGui::GetWindowDrawList();
	wdl->AddRectFilled(wp, wp + ws, ImGui::GetColorU32(c::background::filling), c::background::rounding);
	wdl->AddRect(wp, wp + ws, ImGui::GetColorU32(c::background::stroke), c::background::rounding, 0, 1.5f);

	ImGui::SetCursorPos(ImVec2(0.f, 0.f));
	ImGui::InvisibleButton("##rankrev_drag", ImVec2(ws.x, 26.f));
	{
		static ImVec2 s_drag_origin{};
		static bool s_dragging = false;
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
			if (!s_dragging) {
				s_drag_origin = ImGui::GetWindowPos();
				s_dragging = true;
			}
			ImGui::SetWindowPos(s_drag_origin + ImGui::GetMouseDragDelta(ImGuiMouseButton_Left));
		} else {
			s_dragging = false;
		}
	}
	{
		const ImU32 titCol = IM_COL32(255, 255, 255, 255);
		const ImVec2 tp(wp.x + 10.f, wp.y + 4.f);
		if (font::lexend_regular)
			wdl->AddText(font::lexend_regular, font::lexend_regular->FontSize, tp, titCol, "Rank Revealer");
		else
			wdl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), tp, titCol, "Rank Revealer");
	}

	static std::vector<UE4Structs::CS2Entity> drawSnap;
	rank_reveal_cache::CopyStableSnapshot(drawSnap);

	int localTeam = 0;
	bool viewerSpectator = true;
	if (global_pawn && offsets::m_iTeamNum) {
		localTeam = g_GameMem.readv<int>(global_pawn + static_cast<uintptr_t>(offsets::m_iTeamNum)) & 0xFF;
		viewerSpectator = (localTeam != 2 && localTeam != 3);
	}

	const int localRt = g_cs2LocalCompetitiveRankType.load(std::memory_order_relaxed);
	const bool anyPlayerPremierSignal = std::any_of(drawSnap.begin(), drawSnap.end(), [](const UE4Structs::CS2Entity& p) {
		return RankRevealUsesPremierRating(p.competitive_rank_type, p.competitive_ranking);
	});
	const bool premierCols = expectional::cs2::MatchUsesPremierColumns(localRt, anyPlayerPremierSignal);

	const bool showInv = Settings::misc::rank_reveal_inventory_enabled;
	const int ncols = (premierCols ? 7 : 4) + (showInv ? 1 : 0);

	std::unordered_map<std::uint64_t, FaceitRow> cacheCopy;
	{
		std::lock_guard<std::mutex> lk(g_faceit_mu);
		cacheCopy = g_faceit_cache;
	}
	std::unordered_map<std::uint64_t, SteamInvRow> steamCopy;
	if (showInv) {
		std::lock_guard<std::mutex> lk(g_steam_inv_mu);
		steamCopy = g_steam_inv_cache;
	}

	static const ImVec4 kAllyGreen(0.32f, 0.92f, 0.44f, 1.f);
	static const ImVec4 kEnemyRed(0.98f, 0.30f, 0.34f, 1.f);
	static const ImVec4 kPredPlusGreen(0.25f, 0.92f, 0.42f, 1.f);
	static const ImVec4 kPredTieOrange(1.0f, 0.58f, 0.18f, 1.f);
	static const ImVec4 kPredMinusRed(0.96f, 0.28f, 0.30f, 1.f);
	static const ImVec4 kRankRevealPlainWhite(1.f, 1.f, 1.f, 1.f);
	const ImVec4 kSpecWhite = c::elements::text_active;

	ImGui::PushStyleColor(ImGuiCol_TableRowBg, c::elements::background);
	ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, c::elements::background_rect);
	ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, c::elements::background_widget);
	ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, c::background::stroke);
	ImGui::PushStyleColor(ImGuiCol_TableBorderLight, c::background::stroke);
	if (ImGui::BeginTable("##rankrev", ncols,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
		ImVec2(0, -2.f))) {
		ImGui::TableSetupColumn("Name");
		ImGui::TableSetupColumn("Rank");
		if (premierCols) {
			ImGui::TableSetupColumn("+");
			ImGui::TableSetupColumn("Tie");
			ImGui::TableSetupColumn("-");
		}
		ImGui::TableSetupColumn("Wins");
		ImGui::TableSetupColumn("Faceit");
		if (showInv)
			ImGui::TableSetupColumn("Steam $");
		ImGui::TableHeadersRow();

		for (const auto& p : drawSnap) {
			ImGui::TableNextRow();
			int c = 0;
			ImVec4 accentCol = kSpecWhite;
			if (!viewerSpectator) {
				const int t = p.team_num & 0xFF;
				if (t == 2 || t == 3)
					accentCol = (t == localTeam) ? kAllyGreen : kEnemyRed;
			}

			ImGui::TableSetColumnIndex(c++);
			ImGui::TextColored(accentCol, "%s", p.name.empty() ? "—" : p.name.c_str());

			ImGui::TableSetColumnIndex(c++);
			{
				if (RankingIsLegacyIndex(p.competitive_ranking)) {
					const char* lab = Cs2RankLabelShort(p.competitive_ranking, p.competitive_rank_type);
					if (lab && lab[0] && lab[0] != '-')
						ImGui::TextColored(kRankRevealPlainWhite, "%s", lab);
					else
						ImGui::TextColored(kRankRevealPlainWhite, "%d", p.competitive_ranking);
				} else if (premierCols) {
					const int pr = p.competitive_ranking < 0 ? 0 : p.competitive_ranking;
					ImGui::TextColored(RankRevealPremierRatingColor(pr), "%d", pr);
				} else {
					ImGui::Text("");
				}
			}
			if (premierCols) {
				ImGui::TableSetColumnIndex(c++);
				RankRevealDrawPremierPredDelta(kPredPlusGreen, p.competitive_ranking, p.rank_pred_win);
				ImGui::TableSetColumnIndex(c++);
				RankRevealDrawPremierPredDelta(kPredTieOrange, p.competitive_ranking, p.rank_pred_tie);
				ImGui::TableSetColumnIndex(c++);
				RankRevealDrawPremierPredDelta(kPredMinusRed, p.competitive_ranking, p.rank_pred_loss);
			}
			ImGui::TableSetColumnIndex(c++);
			if (p.competitive_wins > 0)
				ImGui::TextColored(kRankRevealPlainWhite, "%d", p.competitive_wins);
			else
				ImGui::Text("");

			ImGui::TableSetColumnIndex(c++);
			if (p.steam_id64) {
				const auto it = cacheCopy.find(p.steam_id64);
				if (it != cacheCopy.end()) {
					const int sk = it->second.skill_level;
					const int el = it->second.faceit_elo;
					const bool hasSk = sk >= 1 && sk <= 10;
					const bool showElo = el >= 0 && (sk >= 8 || el >= 1800);
					const ImVec4 faceitLvlCol = hasSk ? RankRevealFaceitLevelColor(sk) : ImVec4(0.98f, 0.30f, 0.34f, 1.f);
					if (hasSk && showElo)
						ImGui::TextColored(faceitLvlCol, "LVL %d - %d ELO", sk, el);
					else if (hasSk)
						ImGui::TextColored(faceitLvlCol, "LVL %d", sk);
					else if (el >= 0 && el >= 1800)
						ImGui::TextColored(faceitLvlCol, "%d ELO", el);
					else if (!it->second.status.empty() && it->second.status != "OK")
						ImGui::TextColored(c::elements::text, "%s", it->second.status.c_str());
					else
						ImGui::Text("");
				} else
					ImGui::Text("");
			} else
				ImGui::Text("");

			if (showInv) {
				ImGui::TableSetColumnIndex(c++);
				if (p.steam_id64) {
					const auto sit = steamCopy.find(p.steam_id64);
					if (sit != steamCopy.end() && sit->second.value_cents >= 0) {
						const double usd = static_cast<double>(sit->second.value_cents) / 100.0;
						if (sit->second.truncated)
							ImGui::TextColored(accentCol, "~$%.0f+", usd);
						else
							ImGui::TextColored(accentCol, "$%.0f", usd);
						if (sit->second.status.find("Kismi") != std::string::npos) {
							ImGui::SameLine(0.f, 4.f);
							ImGui::TextColored(c::elements::text, "*");
						}
					} else if (sit != steamCopy.end() && !sit->second.status.empty() && sit->second.status != "OK") {
						ImGui::TextColored(c::elements::text, "%s", sit->second.status.c_str());
					} else
						ImGui::TextColored(c::elements::text, "...");
				} else
					ImGui::TextColored(c::elements::text, "—");
			}
		}
		ImGui::EndTable();
	}
	ImGui::PopStyleColor(5);
	ImGui::End();
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(3);
}
