#include "../include/lic_report.hpp"
#include "../include/lic_crypto.hpp"
#include "../include/lic_json.hpp"
#include "../include/lic_wire.hpp"
#include "../include/lic_guard_vxlang.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace lic {
namespace {

std::wstring to_wide(const std::string& s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  if (n <= 0) return std::wstring(s.begin(), s.end());
  std::wstring w(static_cast<size_t>(n - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
  return w;
}

bool parse_http_base(const std::string& base, std::wstring& host, uint16_t& port, bool& secure) {
  size_t i = 0;
  if (base.compare(0, 8, "https://") == 0) {
    secure = true;
    port = 443;
    i = 8;
  } else if (base.compare(0, 7, "http://") == 0) {
    secure = false;
    port = 80;
    i = 7;
  } else {
    return false;
  }
  size_t slash = base.find('/', i);
  std::string hostport =
      (slash == std::string::npos) ? base.substr(i) : base.substr(i, slash - i);
  size_t colon = hostport.find(':');
  std::string h;
  if (colon != std::string::npos) {
    h = hostport.substr(0, colon);
    try {
      port = static_cast<uint16_t>(std::stoi(hostport.substr(colon + 1)));
    } catch (...) {
      return false;
    }
  } else {
    h = hostport;
  }
  if (h.empty()) return false;
  host.assign(h.begin(), h.end());
  return true;
}

}  // namespace

std::string DeriveApiBaseFromLicenseUrl(const std::string& url) {
  if (url.compare(0, 6, "tcp://") == 0) {
    size_t i = 6;
    size_t colon = url.find(':', i);
    std::string host = (colon == std::string::npos) ? url.substr(i) : url.substr(i, colon - i);
    if (host.empty()) return "";
    return "http://" + host + ":3000";
  }
  if (url.compare(0, 6, "wss://") == 0) {
    size_t i = 6;
    size_t slash = url.find('/', i);
    std::string hostport =
        (slash == std::string::npos) ? url.substr(i) : url.substr(i, slash - i);
    size_t colon = hostport.find(':');
    std::string h =
        (colon == std::string::npos) ? hostport : hostport.substr(0, colon);
    if (h.empty()) return "";
    return "https://" + h + ":443";
  }
  if (url.compare(0, 5, "ws://") == 0) {
    size_t i = 5;
    size_t slash = url.find('/', i);
    std::string hostport =
        (slash == std::string::npos) ? url.substr(i) : url.substr(i, slash - i);
    size_t colon = hostport.find(':');
    std::string h = hostport.substr(0, colon == std::string::npos ? hostport.size() : colon);
    uint16_t p = 80;
    if (colon != std::string::npos) {
      try {
        p = static_cast<uint16_t>(std::stoi(hostport.substr(colon + 1)));
      } catch (...) {
        return "";
      }
    }
    if (h.empty()) return "";
    return "http://" + h + ":" + std::to_string(static_cast<int>(p));
  }
  return "";
}

void SendLicenseSecurityReport(const std::string& api_base_url,
                               const std::string& app_public_id,
                               const std::string& build_secret,
                               const std::string& event,
                               const std::string& machine_name,
                               const std::vector<std::string>& tools,
                               const std::string& license_key_prefix) {
  if (api_base_url.empty() || app_public_id.empty() || build_secret.empty()) return;

  VL_OBF_SCOPE;

  std::vector<std::string> sorted = tools;
  std::sort(sorted.begin(), sorted.end());

  const auto ts = static_cast<int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());

  std::ostringstream pt;
  pt << "{\"app_public_id\":";
  lic_json::escape_to(pt, app_public_id);
  pt << ",\"ts\":" << ts << ",\"event\":";
  lic_json::escape_to(pt, event);
  pt << ",\"machine_name\":";
  lic_json::escape_to(pt, machine_name);
  pt << ",\"tools\":[";
  for (size_t i = 0; i < sorted.size(); ++i) {
    if (i) pt << ',';
    lic_json::escape_to(pt, sorted[i]);
  }
  pt << "],\"license_key_prefix\":";
  lic_json::escape_to(pt, license_key_prefix);
  pt << '}';
  const std::string plaintext = pt.str();

  const std::vector<uint8_t> ikm(build_secret.begin(), build_secret.end());
  const std::vector<uint8_t> report_key =
      HkdfSha256(ikm, "lic-report-v1", "aes256gcm", 32);
  if (report_key.size() != 32) return;

  const std::vector<uint8_t> iv = RandomBytes(12);
  if (iv.size() != 12) return;

  const std::vector<uint8_t> pt_bytes(plaintext.begin(), plaintext.end());
  const std::vector<uint8_t> ct = AesGcmEncrypt(report_key, iv, pt_bytes);
  if (ct.empty()) return;

  const std::string n_b64 = Base64Encode(iv);
  const std::string c_b64 = Base64Encode(ct);
  const std::string sig = HmacSha256Hex(build_secret, n_b64 + "." + c_b64);

  std::ostringstream body;
  body << "{\"v\":1,\"n\":";
  lic_json::escape_to(body, n_b64);
  body << ",\"c\":";
  lic_json::escape_to(body, c_b64);
  body << '}';
  const std::string json = body.str();

  std::wstring host;
  uint16_t port = 80;
  bool secure = false;
  if (!parse_http_base(api_base_url, host, port, secure)) return;

  HINTERNET ses =
      WinHttpOpen(L"LicenseGuard/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) return;
  HINTERNET con = WinHttpConnect(ses, host.c_str(), port, 0);
  if (!con) {
    WinHttpCloseHandle(ses);
    return;
  }
  DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET req =
      WinHttpOpenRequest(con, L"POST", L"/api/public/license-report", nullptr,
                         WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!req) {
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return;
  }

  std::wstring sigw = to_wide(sig);
  const std::string app_tag = BuildAppTag(build_secret, app_public_id);
  std::wstring hdr = L"Content-Type: application/json\r\nX-License-Report-Sig: " + sigw +
                     L"\r\nX-App-Tag: " + to_wide(app_tag) + L"\r\n";

  const void* payload = json.data();
  DWORD len = static_cast<DWORD>(json.size());
  if (!WinHttpSendRequest(req, hdr.c_str(), static_cast<DWORD>(-1),
                          const_cast<void*>(payload), len, len, 0)) {
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return;
  }
  WinHttpReceiveResponse(req, nullptr);
  WinHttpCloseHandle(req);
  WinHttpCloseHandle(con);
  WinHttpCloseHandle(ses);
}

}  // namespace lic
