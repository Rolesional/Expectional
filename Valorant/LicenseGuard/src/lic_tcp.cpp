#include "../include/lic_tcp.hpp"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#include "../include/lic_guard_vxlang.hpp"

namespace lic {
namespace {

struct ParsedTcp {
  std::string host;
  uint16_t port = 0;
};

bool parse_tcp_url(const std::string& url, ParsedTcp& out) {
  if (url.compare(0, 6, "tcp://") != 0) return false;
  size_t i = 6;
  size_t colon = url.find(':', i);
  std::string hostport;
  if (colon == std::string::npos) {
    hostport = url.substr(i);
    out.port = 3001;
  } else {
    hostport = url.substr(i, colon - i);
    try {
      out.port = static_cast<uint16_t>(std::stoi(url.substr(colon + 1)));
    } catch (...) {
      return false;
    }
  }
  out.host = hostport;
  return !out.host.empty() && out.port > 0;
}

std::once_flag g_wsa_once;

void ensure_wsa_once() {
  std::call_once(g_wsa_once, [] {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
  });
}

}  // namespace

LicTcp::LicTcp() {}
LicTcp::~LicTcp() { Stop(); }

bool LicTcp::Start(const std::string& tcp_url, const std::string& app_tag,
                   std::function<void(const std::string&)> onText,
                   std::function<void()> onClose) {
  on_text_ = std::move(onText);
  on_close_ = std::move(onClose);

  ParsedTcp u;
  if (!parse_tcp_url(tcp_url, u)) return false;
  ensure_wsa_once();

  addrinfo hints = {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  addrinfo* res = nullptr;
  std::string portstr = std::to_string(static_cast<int>(u.port));
  if (getaddrinfo(u.host.c_str(), portstr.c_str(), &hints, &res) != 0 || !res) {
    return false;
  }

  SOCKET s = INVALID_SOCKET;
  for (addrinfo* p = res; p; p = p->ai_next) {
    s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (s == INVALID_SOCKET) continue;
    if (connect(s, p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0) break;
    closesocket(s);
    s = INVALID_SOCKET;
  }
  freeaddrinfo(res);

  if (s == INVALID_SOCKET) {
    return false;
  }

  sock_ = reinterpret_cast<void*>(s);
  connected_.store(true);

  std::string hello = std::string("{\"lic_hello\":1,\"app_tag\":\"") + app_tag + "\"}";
  if (!SendFramedUtf8(hello)) {
    connected_.store(false);
    closesocket(s);
    sock_ = nullptr;
    return false;
  }

  recv_thread_ = std::thread([this] { RecvLoop(); });
  return true;
}

void LicTcp::RecvLoop() {
  SOCKET s = static_cast<SOCKET>(reinterpret_cast<uintptr_t>(sock_));
  std::vector<uint8_t> buf(64 * 1024);
  std::vector<uint8_t> acc;

  while (!stopping_.load() && s != INVALID_SOCKET) {
    int n = recv(s, reinterpret_cast<char*>(buf.data()),
                 static_cast<int>(buf.size()), 0);
    if (n <= 0) break;
    acc.insert(acc.end(), buf.begin(), buf.begin() + n);

    while (acc.size() >= 4) {
      uint32_t len =
          (static_cast<uint32_t>(acc[0]) << 24) | (static_cast<uint32_t>(acc[1]) << 16) |
          (static_cast<uint32_t>(acc[2]) << 8) | static_cast<uint32_t>(acc[3]);
      if (len == 0 || len > 512u * 1024u) {
        stopping_.store(true);
        break;
      }
      if (acc.size() < 4 + len) break;
      std::string msg(reinterpret_cast<char*>(acc.data() + 4),
                        reinterpret_cast<char*>(acc.data() + 4 + len));
      acc.erase(acc.begin(), acc.begin() + 4 + len);
      if (on_text_) on_text_(msg);
    }
    if (stopping_.load()) break;
  }

  connected_.store(false);
  if (on_close_) on_close_();
}

#if defined(USE_VL_MACRO)
__declspec(noinline)
#endif
bool LicTcp::SendFramedUtf8(const std::string& payload) {
  SOCKET s = static_cast<SOCKET>(reinterpret_cast<uintptr_t>(sock_));
  if (s == INVALID_SOCKET || !connected_.load()) return false;
  if (payload.size() > 512u * 1024u) return false;

  bool ok = false;
  LIC_VL_OBF_OPEN;
  do {
    std::lock_guard<std::mutex> lk(send_mutex_);
    uint32_t len = static_cast<uint32_t>(payload.size());
    unsigned char hdr[4] = {static_cast<unsigned char>((len >> 24) & 0xff),
                            static_cast<unsigned char>((len >> 16) & 0xff),
                            static_cast<unsigned char>((len >> 8) & 0xff),
                            static_cast<unsigned char>(len & 0xff)};
    if (send(s, reinterpret_cast<const char*>(hdr), 4, 0) != 4) break;
    size_t off = 0;
    while (off < payload.size()) {
      int sn = send(s, payload.data() + off,
                    static_cast<int>(payload.size() - off), 0);
      if (sn <= 0) break;
      off += static_cast<size_t>(sn);
    }
    if (off < payload.size()) break;
    ok = true;
  } while (0);
  LIC_VL_OBF_CLOSE;
  return ok;
}

void LicTcp::Stop() {
  stopping_.store(true);
  connected_.store(false);
  if (sock_) {
    SOCKET s = static_cast<SOCKET>(reinterpret_cast<uintptr_t>(sock_));
    shutdown(s, SD_BOTH);
    closesocket(s);
    sock_ = nullptr;
  }
  if (recv_thread_.joinable()) {
    if (recv_thread_.get_id() == std::this_thread::get_id())
      recv_thread_.detach();
    else
      recv_thread_.join();
  }
}

}  // namespace lic
