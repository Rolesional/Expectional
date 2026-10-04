#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace lic {

/// Ham TCP (WinSock): çerçeve = 4 byte BE uzunluk + UTF-8 gövde (sunucu ile aynı).
/// İlk çerçeve: `{"lic_hello":1,"app_tag":"<BuildAppTag çıktısı>"}` (şifresiz).
class LicTcp {
 public:
  LicTcp();
  ~LicTcp();

  /// tcp_url: tcp://host[:port]
  bool Start(const std::string& tcp_url, const std::string& app_tag,
               std::function<void(const std::string&)> onText,
               std::function<void()> onClose);
  void Stop();

  bool SendFramedUtf8(const std::string& payload);
  bool Connected() const { return connected_.load(); }

 private:
  void RecvLoop();

  void* sock_ = nullptr;  // SOCKET
  std::thread recv_thread_;
  std::atomic<bool> connected_{false};
  std::atomic<bool> stopping_{false};
  std::mutex send_mutex_;

  std::function<void(const std::string&)> on_text_;
  std::function<void()> on_close_;
};

}  // namespace lic
