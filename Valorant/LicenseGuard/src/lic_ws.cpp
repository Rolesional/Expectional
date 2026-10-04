#include "../include/lic_ws.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <cstring>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#include "../include/lic_guard_vxlang.hpp"

namespace lic {

namespace {

struct ParsedUrl {
  bool secure = false;
  std::wstring host;
  uint16_t port = 80;
  std::wstring path = L"/";
};

bool parse_ws_url(const std::string& url, ParsedUrl& out) {
  size_t i = 0;
  if (url.compare(0, 6, "wss://") == 0) {
    out.secure = true;
    out.port = 443;
    i = 6;
  } else if (url.compare(0, 5, "ws://") == 0) {
    out.secure = false;
    out.port = 80;
    i = 5;
  } else {
    return false;
  }
  size_t slash = url.find('/', i);
  std::string hostport = url.substr(i, (slash == std::string::npos) ? std::string::npos : slash - i);
  std::string path = (slash == std::string::npos) ? "/" : url.substr(slash);
  size_t colon = hostport.find(':');
  std::string host;
  if (colon != std::string::npos) {
    host = hostport.substr(0, colon);
    out.port = static_cast<uint16_t>(std::stoi(hostport.substr(colon + 1)));
  } else {
    host = hostport;
  }
  // ASCII → wide
  out.host.assign(host.begin(), host.end());
  out.path.assign(path.begin(), path.end());
  return !out.host.empty();
}

}  // namespace

WinHttpWs::WinHttpWs() {}
WinHttpWs::~WinHttpWs() { Stop(); }

bool WinHttpWs::Start(const std::string& url, const std::string& extraHeaders,
                      std::function<void(const std::string&)> onText,
                      std::function<void()> onClose) {
  on_text_ = std::move(onText);
  on_close_ = std::move(onClose);

  ParsedUrl u;
  if (!parse_ws_url(url, u)) return false;

  hSession_ = WinHttpOpen(L"LicenseGuard/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession_) return false;

  hConnect_ = WinHttpConnect(static_cast<HINTERNET>(hSession_), u.host.c_str(),
                             u.port, 0);
  if (!hConnect_) { Stop(); return false; }

  DWORD reqFlags = u.secure ? WINHTTP_FLAG_SECURE : 0;
  hRequest_ = WinHttpOpenRequest(static_cast<HINTERNET>(hConnect_), L"GET",
                                 u.path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, reqFlags);
  if (!hRequest_) { Stop(); return false; }

  // Upgrade'i WS yap
  if (!WinHttpSetOption(static_cast<HINTERNET>(hRequest_),
                        WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
    Stop();
    return false;
  }

  // Extra headers
  std::wstring whdrs;
  whdrs.assign(extraHeaders.begin(), extraHeaders.end());

  if (!WinHttpSendRequest(static_cast<HINTERNET>(hRequest_),
                          whdrs.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS
                                        : whdrs.c_str(),
                          whdrs.empty() ? 0 : static_cast<DWORD>(-1L),
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
    Stop();
    return false;
  }
  if (!WinHttpReceiveResponse(static_cast<HINTERNET>(hRequest_), nullptr)) {
    Stop();
    return false;
  }

  DWORD status = 0;
  DWORD szStatus = sizeof(status);
  WinHttpQueryHeaders(static_cast<HINTERNET>(hRequest_),
                      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &szStatus,
                      WINHTTP_NO_HEADER_INDEX);
  if (status != HTTP_STATUS_SWITCH_PROTOCOLS) {
    Stop();
    return false;
  }

  hWebSocket_ = WinHttpWebSocketCompleteUpgrade(static_cast<HINTERNET>(hRequest_), 0);
  if (!hWebSocket_) { Stop(); return false; }

  // request handle artık WS handle'a kapatıldı; kapatabiliriz
  WinHttpCloseHandle(static_cast<HINTERNET>(hRequest_));
  hRequest_ = nullptr;

  connected_.store(true);
  recv_thread_ = std::thread([this] { RecvLoop(); });
  return true;
}

void WinHttpWs::RecvLoop() {
  std::vector<uint8_t> buf(8 * 1024);
  std::string accumulator;

  while (!stopping_.load() && hWebSocket_) {
    DWORD bytesRead = 0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE bt;
    DWORD rc = WinHttpWebSocketReceive(static_cast<HINTERNET>(hWebSocket_),
                                       buf.data(),
                                       static_cast<DWORD>(buf.size()),
                                       &bytesRead, &bt);
    if (rc != NO_ERROR) break;

    if (bt == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
    if (bt == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE ||
        bt == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE) {
      // protokolümüz text only — binary frame'leri yoksayalım
      continue;
    }
    accumulator.append(reinterpret_cast<char*>(buf.data()), bytesRead);
    if (bt == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
      if (on_text_) on_text_(accumulator);
      accumulator.clear();
    }
    // FRAGMENT ise akümüle etmeye devam et
  }

  connected_.store(false);
  if (on_close_) on_close_();
}

bool WinHttpWs::SendText(const std::string& payload) {
  if (!hWebSocket_ || !connected_.load()) return false;
  LIC_VL_OBF_OPEN;
  std::lock_guard<std::mutex> lk(send_mutex_);
  DWORD rc = WinHttpWebSocketSend(static_cast<HINTERNET>(hWebSocket_),
                                  WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                  const_cast<char*>(payload.data()),
                                  static_cast<DWORD>(payload.size()));
  const bool ok = (rc == NO_ERROR);
  LIC_VL_OBF_CLOSE;
  return ok;
}

void WinHttpWs::Stop() {
  stopping_.store(true);
  connected_.store(false);
  if (hWebSocket_) {
    WinHttpWebSocketClose(static_cast<HINTERNET>(hWebSocket_),
                          WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,
                          nullptr, 0);
    WinHttpCloseHandle(static_cast<HINTERNET>(hWebSocket_));
    hWebSocket_ = nullptr;
  }
  if (hRequest_) {
    WinHttpCloseHandle(static_cast<HINTERNET>(hRequest_));
    hRequest_ = nullptr;
  }
  if (hConnect_) {
    WinHttpCloseHandle(static_cast<HINTERNET>(hConnect_));
    hConnect_ = nullptr;
  }
  if (hSession_) {
    WinHttpCloseHandle(static_cast<HINTERNET>(hSession_));
    hSession_ = nullptr;
  }
  if (recv_thread_.joinable()) {
    if (recv_thread_.get_id() == std::this_thread::get_id())
      recv_thread_.detach();
    else
      recv_thread_.join();
  }
}

}  // namespace lic
