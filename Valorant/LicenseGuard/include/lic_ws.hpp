#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace lic {

/// Native WinHTTP WebSocket istemcisi. ws:// veya wss:// destekler.
/// Ekstra dış bağımlılık yok — Windows 8+ tüm sürümlerde mevcut.
class WinHttpWs {
 public:
  WinHttpWs();
  ~WinHttpWs();

  /// url: ws://host[:port]/path veya wss://host[:port]/path
  /// extraHeaders: "Header-Name: value\r\n" şeklinde, çoklu satır olabilir.
  /// onText: text mesaj geldiğinde çağrılır (UTF-8).
  /// onClose: bağlantı kapandığında çağrılır.
  bool Start(const std::string& url, const std::string& extraHeaders,
             std::function<void(const std::string&)> onText,
             std::function<void()> onClose);
  void Stop();

  bool SendText(const std::string& payload);
  bool Connected() const { return connected_.load(); }

 private:
  void RecvLoop();

  void* hSession_ = nullptr;     // HINTERNET
  void* hConnect_ = nullptr;
  void* hRequest_ = nullptr;
  void* hWebSocket_ = nullptr;

  std::thread recv_thread_;
  std::atomic<bool> connected_{false};
  std::atomic<bool> stopping_{false};
  std::mutex send_mutex_;

  std::function<void(const std::string&)> on_text_;
  std::function<void()> on_close_;
};

}  // namespace lic
