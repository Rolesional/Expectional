#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace lic {

enum class Status {
  NotStarted = 0,
  Connecting,
  Authenticating,
  OK,
  Kicked,
  Error,
};

struct StartOptions {
  // license.dat dosyasının yolu. Boş bırakılırsa exe ile aynı klasörde aranır.
  std::string license_dat_path;

  // (Şu an devre dışı; gelecek sürümde shared_ptr state ile aktif edilecek)
  bool auto_reconnect = false;

  // Auth tamamlanması için max bekleme (ms). WaitForAuth bunu kullanır.
  int auth_timeout_ms = 12000;

  // KICK edildiğinde davranış (manuel login DENEMELERİNDE bu opsiyonlar
  // dahili olarak geçici devre dışı bırakılır — yanlış key girildiğinde
  // program kapanmasın diye).
  bool show_messagebox_on_kick = true;
  bool exit_process_on_kick = true;     // ExitProcess(1)

  // İstemci versiyon string'i (server tarafında version_check için)
  std::string client_version = "1.0.0";

  // ===========================================================================
  // MANUEL LOGIN MODU (ImGui pencere)
  // ===========================================================================
  // license.dat yoksa veya bozuksa kullanıcıya ImGui login penceresi göster.
  // Kullanıcı sadece license_key girer; app_id ve ws_url binary'ye gömülü
  // baked_* alanlarından alınır.
  bool manual_input_if_missing = false;

  // Binary'ye gömülü (LXS macro ile compile-time XOR obfuscate ediliyor):
  //   - baked_app_id : public app id  (dashboard'da görüntülediğin)
  //   - baked_ws_url : tcp://host[:port] (önerilen, lisans ham TCP) veya
  //                    ws://host[:port]/ws / wss://... (WebSocket)
  //   - baked_build_secret : server .env'deki WIRE_BUILD_SECRET ile EŞ DEĞER.
  //     Wire encryption / X-App-Tag türetimi için kullanılır.
  // Hepsi runtime'da bellek dışına çıkmadığı için disassembly olmadan
  // okunamaz; LXS string'leri data segmentinde XOR'lı durur.
  std::string baked_app_id;
  std::string baked_ws_url;
  std::string baked_build_secret;

  // Başarılı manuel login sonrası girilen key'i license.dat'a kaydet — bir
  // dahaki açılışta otomatik login olsun.
  bool save_after_manual_login = true;

  // Login penceresi başlığı / üst satırı.
  std::string product_display_name = "Expectional";
};

/// license.dat veya manuel login üzerinden auth eder.
/// Başarılı dönerse auth tamamlanmıştır; heartbeat thread arka planda çalışır.
/// (Önceki sürümde bu fonksiyon sadece WS başlatıyordu; artık AUTH_OK dönene
///  kadar bloklar — WaitForAuth() çağrısı backward-compat için hâlâ çalışır.)
bool Start(const StartOptions& opts = {});

/// Background thread'i kapatır.
void Stop();

/// Anlık durum.
Status GetStatus();

/// En son hata mesajı (start/auth fail nedeni).
std::string LastError();

/// Auth bekler. Start() artık AUTH_OK gelene kadar blokladığı için bu
/// çoğunlukla anında true döner. Backward-compat için bırakıldı.
bool WaitForAuth(int timeout_ms);

/// Server'dan AES-GCM şifreli "feature blob" çeker (FeatureBlob.blobKey; File.id değil).
/// Oturuma bağlı; çözüm içeride, çıktı plaintext byte vektörü.
bool RequestBlob(const std::string& blob_key, std::vector<uint8_t>& out_plain,
                 int timeout_ms = 6000);

/// Panel dosya kaydı `File.id` ile indirir. Wire: `BLOB_GET` payload.blob_key=file_id
/// (sunucu `BLOB_RESP` döner); `DOWNLOAD_RESP` / `FILE_DOWNLOAD_RESP` da kabul edilir.
bool RequestFileDownload(const std::string& file_id, std::vector<uint8_t>& out_plain,
                         int timeout_ms = 600000);

/// AUTH_OK payload `expiry` (Unix epoch ms). Yoksa veya parse edilemediyse 0.
long long GetLicenseExpiryEpochMs();

}  // namespace lic
