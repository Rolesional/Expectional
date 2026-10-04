#include "../include/LicenseGuard.hpp"
#include "../include/lic_antidebug.hpp"
#include "../include/lic_crypto.hpp"
#include "../include/lic_hwid.hpp"
#include "../include/lic_json.hpp"
#include "../include/lic_login_ui.hpp"
#include "../include/lic_wire.hpp"
#include "../include/lic_report.hpp"
#include "../include/lic_tcp.hpp"
#include "../include/lic_ws.hpp"
#include "../include/lic_xor.hpp"
#include "../include/lic_guard_vxlang.hpp"
#include "Protection/lic_auth_vm.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <intrin.h>

#pragma comment(lib, "winhttp.lib")

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <shlobj.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "bcrypt.lib")

namespace lic {

namespace {

// =============================================================================
// license.dat formatı v2:
//   bytes 0..3  : magic "LIC2"
//   bytes 4..15 : 12-byte AES-GCM IV
//   bytes 16..  : ciphertext || 16-byte tag
//
// Plaintext = USERPASS\n{username}\n{password}\n{expires_at_ms}
//          OR LICKEY\n{license_key}\n{expires_at_ms}
//          OR legacy (no prefix / no expiry field) → yeniden login gerekir.
// app_id ve ws_url ARTIK BURADA TUTULMAZ —
// onlar binary'ye LXS ile gömülü. Yani license.dat sızsa bile saldırgan
// hangi backend'e ait olduğunu öğrenemez.
//
// Şifreleme anahtarı: HKDF-SHA256(build_secret, salt="lic-dat-v2",
//                                 info="license-key", len=32)
// Bu da binary'ye LXS'li gömülü; çalışan exe olmadan dosya çözülmez.
// =============================================================================

struct StoredAuth {
  bool account = false;
  std::string username;
  std::string password;
  std::string license_key;
  /// license.dat içindeki şifreli son kullanma (Unix epoch ms). 0 = geçersiz/eski format.
  int64_t dat_expires_at_ms = 0;
};

constexpr const char kAccountDatPrefix[] = "USERPASS\n";
constexpr const char kLicenseDatPrefix[] = "LICKEY\n";
constexpr int64_t kLicenseDatTtlMs = 10LL * 24 * 3600 * 1000;

int64_t now_epoch_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

int64_t new_dat_expires_at_ms() { return now_epoch_ms() + kLicenseDatTtlMs; }

bool is_dat_plaintext_expired(const StoredAuth& auth) {
  if (auth.dat_expires_at_ms <= 0) return true;
  return now_epoch_ms() > auth.dat_expires_at_ms;
}

bool parse_expiry_field(const std::string& raw, int64_t& out_ms) {
  std::string s = raw;
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' '))
    s.pop_back();
  if (s.empty()) return false;
  try {
    out_ms = std::stoll(s);
    return out_ms > 0;
  } catch (...) {
    return false;
  }
}

bool parse_dat_plaintext(const std::string& plain, StoredAuth& out) {
  out = StoredAuth{};
  if (plain.rfind(kAccountDatPrefix, 0) == 0) {
    const std::string rest = plain.substr(sizeof(kAccountDatPrefix) - 1);
    const auto nl1 = rest.find('\n');
    if (nl1 == std::string::npos) return false;
    const auto nl2 = rest.find('\n', nl1 + 1);
    out.account = true;
    out.username = rest.substr(0, nl1);
    if (nl2 == std::string::npos) {
      out.password = rest.substr(nl1 + 1);
      out.dat_expires_at_ms = 0;
    } else {
      out.password = rest.substr(nl1 + 1, nl2 - nl1 - 1);
      if (!parse_expiry_field(rest.substr(nl2 + 1), out.dat_expires_at_ms))
        out.dat_expires_at_ms = 0;
    }
    return !out.username.empty() && out.password.size() >= 8;
  }
  if (plain.rfind(kLicenseDatPrefix, 0) == 0) {
    const std::string rest = plain.substr(sizeof(kLicenseDatPrefix) - 1);
    const auto nl = rest.find('\n');
    if (nl == std::string::npos) return false;
    out.account = false;
    out.license_key = rest.substr(0, nl);
    if (!parse_expiry_field(rest.substr(nl + 1), out.dat_expires_at_ms))
      out.dat_expires_at_ms = 0;
    return !out.license_key.empty();
  }
  if (plain.empty()) return false;
  out.account = false;
  out.license_key = plain;
  out.dat_expires_at_ms = 0;
  return true;
}

std::string encode_account_dat_plain(const LoginCredentials& cred) {
  return std::string(kAccountDatPrefix) + cred.username + "\n" + cred.password + "\n" +
         std::to_string(new_dat_expires_at_ms());
}

std::string default_license_dat_path() {
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return "license.dat";
  PathRemoveFileSpecW(buf);
  std::wstring w(buf);
  w += L"\\license.dat";
  int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string out(sz - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), sz, nullptr, nullptr);
  return out;
}

void delete_license_dat_safely(const std::string& path) {
  // utf8 → wide
  int sz = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
  if (sz <= 0) return;
  std::wstring w(sz - 1, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, w.data(), sz);
  ::DeleteFileW(w.c_str());
}

// =============================================================================
// Guard state (singleton)
// =============================================================================

struct GuardState {
  StartOptions opts;
  std::unique_ptr<WinHttpWs> ws;
  std::unique_ptr<LicTcp> tcp;

  std::string app_public_id;
  std::string license_key;
  bool use_account_auth = false;
  std::string account_username;
  std::string account_password;
  std::string ws_url;
  std::string build_secret;

  // Wire encryption
  std::vector<uint8_t> wire_app_key;       // Phase A — handshake/auth
  std::vector<uint8_t> wire_session_key;   // Phase B — post-AUTH

  std::string hwid_hash;
  std::unordered_map<std::string, int> hwid_scores;

  std::mutex m;
  std::condition_variable auth_cv;

  std::atomic<Status> status{Status::NotStarted};
  std::string last_error;

  std::string nonce;
  std::string session_token;
  std::string token_jti;
  long long token_seq = 0;
  int hb_interval_ms = 25000;
  /// AUTH_OK payload `expiry` (Unix epoch ms). Yoksa / parse yoksa 0.
  long long license_expiry_epoch_ms = 0;

  uint32_t outgoing_seq = 100;

  std::atomic<bool> stopping{false};
  std::thread heartbeat_thread;

  // Manuel login deneme modunda KICK ekrana mesaj gösterip ExitProcess
  // yapmasın — sadece bu denemeyi başarısız işaretlesin.
  std::atomic<bool> suppress_kick_termination{false};
  /** AUTH_OK sonrasi heartbeat KICK icin ExitProcess acik olabilir. */
  std::atomic<bool> session_active{false};

  // Blob bekletme — basit single-slot
  std::mutex blob_m;
  std::condition_variable blob_cv;
  std::string blob_key_pending;
  bool blob_received = false;
  std::vector<uint8_t> blob_plain;

  std::mutex download_m;
  std::condition_variable download_cv;
  bool download_received = false;
  std::string download_url;
};

std::unique_ptr<GuardState> g_state;
std::mutex g_state_mutex;

GuardState* GS() { return g_state.get(); }

void set_status(Status s) {
  if (auto* g = GS()) g->status.store(s);
}
void set_error(const std::string& e) {
  if (auto* g = GS()) {
    std::lock_guard<std::mutex> lk(g->m);
    g->last_error = e;
  }
}

// Hatayı NON-BLOCKING gösterip belirtilen süre sonra ExitProcess yapar.
// MessageBoxA modaldir; bu yüzden ayrı bir thread'de açıyoruz, ana thread
// uyuyup ardından ExitProcess çağırıyor (bu MessageBox penceresini de kapatır).
// Kullanıcı isterse 3 sn içinde "OK"e basabilir; ama basmazsa otomatik kapanır.
void show_license_fatal_error(const std::string& title, const std::string& body,
                              bool show_box, bool exit_process, int ms = 3000) {
  std::fprintf(stderr, "[%s] %s\n", title.c_str(), body.c_str());
  std::fflush(stderr);
  if (show_box) {
    MessageBoxA(nullptr, body.c_str(), title.c_str(),
                MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
  }
  if (exit_process) {
    Sleep(ms > 0 ? ms : 0);
    ExitProcess(1);
  }
}

// Sunucu KICK reason ham metni URL/app id içerebilir; kullanıcıya yalnız güvenli özet.
std::string KickReasonForUser(const std::string& raw) {
  std::string r = raw;
  while (!r.empty() && (r.back() == ' ' || r.back() == '\t' || r.back() == '\r' || r.back() == '\n'))
    r.pop_back();
  while (!r.empty() && (r.front() == ' ' || r.front() == '\t')) r.erase(r.begin());

  std::string lower = r;
  for (char& c : lower)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  static const struct {
    const char* code;
    const char* msg;
  } k_map[] = {
      {"unknown", "Authentication failed."},
      {"license_invalid", "Invalid license key."},
      {"invalid_license", "Invalid license key."},
      {"invalid_credentials", "Invalid username or password."},
      {"account_banned", "Access denied."},
      {"hwid_mismatch", "This license is bound to another device."},
      {"expired", "Your license has expired."},
      {"revoked", "This license has been revoked."},
      {"banned", "Access denied."},
      {"version_mismatch", "Please update the application."},
      {"connection_lost", "Connection lost."},
      {"timeout", "Authentication timed out."},
  };
  for (const auto& e : k_map) {
    if (lower == e.code) return std::string(e.msg);
  }

  bool safe_token = !lower.empty() && lower.size() <= 48;
  for (unsigned char uc : lower) {
    if (!std::isalnum(static_cast<int>(uc)) && uc != '_' && uc != '-') {
      safe_token = false;
      break;
    }
  }
  if (safe_token)
    return "Authentication failed. Please try again or contact support.";
  return "Authentication failed.";
}

void apply_kick(const std::string& reason) {
  auto* g = GS();
  const std::string user_reason = KickReasonForUser(reason);
  set_status(Status::Kicked);
  set_error(user_reason);

  // auth_cv'yi her durumda uyandır — try_connect_and_auth'ta bekleyen olabilir
  if (g) g->auth_cv.notify_all();
  if (g) {
    {
      std::lock_guard<std::mutex> lk(g->blob_m);
      g->blob_cv.notify_all();
    }
    {
      std::lock_guard<std::mutex> lk(g->download_m);
      g->download_received = true;
      g->download_url.clear();
      g->download_cv.notify_all();
    }
  }

  // Manuel login denemeleri sırasında ekrana gösterme + exit etme
  if (g && g->suppress_kick_termination.load()) {
    return;
  }

  if (!g || !g->opts.exit_process_on_kick) {
    // Exit istenmiyorsa eski davranış (modal MessageBox)
    if (g && g->opts.show_messagebox_on_kick) {
      std::string title = LXS("License Error");
      MessageBoxA(nullptr, user_reason.c_str(), title.c_str(),
                  MB_OK | MB_ICONERROR | MB_TOPMOST);
    }
    return;
  }

  // Exit istenmiştir — async messagebox + 3 sn sonra çık (kullanıcı OK'e basmasa da)
  std::string title = LXS("License Error");
  std::string body = user_reason + LXS(
      "\n\nThis application will exit automatically in a few seconds.");
  const bool hard_exit =
      g->opts.exit_process_on_kick && g->session_active.load();
  show_license_fatal_error(title, body, g->opts.show_messagebox_on_kick, hard_exit, 3000);
}

// =============================================================================
// Wire encryption I/O
// =============================================================================

// Aktif WS encryption key — Phase B varsa o, yoksa Phase A (app_key).
const std::vector<uint8_t>& current_wire_key(const GuardState& g) {
  return g.wire_session_key.empty() ? g.wire_app_key : g.wire_session_key;
}

bool send_encrypted(GuardState& g, const std::string& json) {
  const auto& key = current_wire_key(g);
  std::string out = json;
  if (key.size() == 32) {
    out = EncryptEnvelope(key, json);
    if (out.empty()) return false;
  }
  if (g.tcp) return g.tcp->SendFramedUtf8(out);
  if (g.ws) return g.ws->SendText(out);
  return false;
}

// =============================================================================
// Protocol senders
// =============================================================================

uint32_t next_seq(GuardState& g) {
  std::lock_guard<std::mutex> lk(g.m);
  return ++g.outgoing_seq;
}

void send_handshake(GuardState& g) {
  lic_json::Builder components;
  for (const auto& [k, v] : g.hwid_scores) components.add_int(k, v);
  std::string compsObj = components.done();

  std::string code_hash = SelfCodeSha256();
  std::string machine_name = GetDisplayMachineNameUtf8();
  if (machine_name.empty()) machine_name = "unknown-pc";
  std::string pc_specs = BuildPcSpecsJsonUtf8();
  if (pc_specs.empty()) pc_specs = "{\"ram_total_mb\":0}";

  lic_json::Builder payload;
  payload.add_str("app_id", g.app_public_id)
         .add_str("version", g.opts.client_version)
         .add_str("hwid", g.hwid_hash)
         .add_raw("components", compsObj)
         .add_str("code_hash", code_hash)
         .add_str("machine_name", machine_name)
         .add_str("pc_specs", pc_specs);

  lic_json::Builder env;
  env.add_str("op", "HANDSHAKE")
     .add_int("seq", next_seq(g))
     .add_raw("payload", payload.done());
  send_encrypted(g, env.done());
}

void send_auth(GuardState& g) {
  LIC_VL_AUTH_VM_OPEN;
  do {
  const auto ts =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  std::string sigPayload =
      g.app_public_id + "|" + g.nonce + "|" + g.hwid_hash + "|" + std::to_string(ts);

  lic_json::Builder payload;
  if (g.use_account_auth) {
    const std::string sig = HmacSha256Hex(g.account_password, sigPayload);
    payload.add_str("username", g.account_username)
           .add_str("password", g.account_password)
           .add_str("nonce_sig", sig)
           .add_uint64("ts", static_cast<uint64_t>(ts));
  } else {
    const std::string sig = expectional_lic_vm::BuildAuthNonceSig(
        g.license_key, g.app_public_id, g.nonce, g.hwid_hash, ts);
    payload.add_str("license_key", g.license_key)
           .add_str("nonce_sig", sig)
           .add_uint64("ts", static_cast<uint64_t>(ts));
  }

  lic_json::Builder env;
  env.add_str("op", "AUTH")
     .add_int("seq", next_seq(g))
     .add_raw("payload", payload.done());
  send_encrypted(g, env.done());
  } while (0);
  LIC_VL_AUTH_VM_CLOSE;
}

bool decode_jwt_jti_seq(const std::string& jwt, std::string& out_jti, long long& out_seq) {
  size_t p1 = jwt.find('.');
  if (p1 == std::string::npos) return false;
  size_t p2 = jwt.find('.', p1 + 1);
  if (p2 == std::string::npos) return false;
  std::string b64 = jwt.substr(p1 + 1, p2 - p1 - 1);
  auto raw = Base64Decode(b64);
  std::string body(raw.begin(), raw.end());
  return lic_json::get_string(body, "jti", out_jti) &&
         lic_json::get_int(body, "seq", out_seq);
}

void send_heartbeat(GuardState& g) {
  std::string token, jti;
  long long claim_seq = 0;
  {
    std::lock_guard<std::mutex> lk(g.m);
    token = g.session_token;
  }
  if (token.empty()) return;
  if (!decode_jwt_jti_seq(token, jti, claim_seq)) return;

  std::string code_hash = SelfCodeSha256();
  uint64_t threat = ComputeThreatScore();
  unsigned long long stamped = __rdtsc() ^ threat;
  std::string rdtsc = std::to_string(stamped);

  std::string sigPayload = jti + "|" + std::to_string(claim_seq) + "|" + rdtsc + "|" + code_hash;
  std::string integrity = HmacSha256Hex(token, sigPayload);

  lic_json::Builder payload;
  payload.add_str("token", token)
         .add_str("integrity", integrity)
         .add_str("rdtsc", rdtsc)
         .add_str("code_hash", code_hash);

  lic_json::Builder env;
  env.add_str("op", "HEARTBEAT")
     .add_int("seq", next_seq(g))
     .add_raw("payload", payload.done());
  send_encrypted(g, env.done());
}

// =============================================================================
// Inbound
// =============================================================================

void handle_message(GuardState& g, const std::string& wire_raw) {
  // -- Wire decrypt: önce session_key, fallback olarak app_key dene --
  std::string raw;
  if (!g.wire_session_key.empty()) {
    raw = DecryptEnvelope(g.wire_session_key, wire_raw);
  }
  if (raw.empty() && !g.wire_app_key.empty()) {
    raw = DecryptEnvelope(g.wire_app_key, wire_raw);
  }
  if (raw.empty()) {
    // Şifresiz/bozuk frame → protokol hatası, sessizce yoksay.
    return;
  }

  std::string op;
  if (!lic_json::get_string(raw, "op", op)) return;
  std::string payload;
  if (!lic_json::get_object(raw, "payload", payload)) payload = "{}";

  if (op == "CHALLENGE") {
    std::string nonce;
    if (!lic_json::get_string(payload, "nonce", nonce)) return;
    {
      std::lock_guard<std::mutex> lk(g.m);
      g.nonce = nonce;
    }
    set_status(Status::Authenticating);
    send_handshake(g);
    send_auth(g);
    return;
  }

  if (op == "AUTH_OK") {
    std::string token;
    long long hb = 25000;
    long long expiry_ms = 0;
    lic_json::get_string(payload, "session_token", token);
    lic_json::get_int(payload, "heartbeat_interval_ms", hb);
    const bool have_expiry =
        lic_json::get_int(payload, "expiry", expiry_ms) && expiry_ms > 0;

    // -- enc_session_key → çöz, wire_session_key olarak set --
    // payload.enc_session_key = { iv, ct } (base64)
    std::string esk_obj;
    if (lic_json::get_object(payload, "enc_session_key", esk_obj)) {
      std::string iv_b64, ct_b64;
      lic_json::get_string(esk_obj, "iv", iv_b64);
      lic_json::get_string(esk_obj, "ct", ct_b64);
      auto iv = Base64Decode(iv_b64);
      auto ct = Base64Decode(ct_b64);
      const std::string envelope_material =
          g.use_account_auth ? g.account_password : ToUpperAscii(g.license_key);
      auto envKey = DeriveLicenseEnvelopeKey(envelope_material, g.nonce);
      auto sk = AesGcmDecrypt(envKey, iv, ct);
      if (sk.size() == 32) {
        g.wire_session_key = std::move(sk);
        // Bu noktadan sonra TÜM giden ve gelen frame'ler session_key ile
      }
    }

    {
      std::lock_guard<std::mutex> lk(g.m);
      g.session_token = token;
      g.hb_interval_ms = static_cast<int>(hb);
      g.license_expiry_epoch_ms = have_expiry ? expiry_ms : 0;
    }
    set_status(Status::OK);
    g.auth_cv.notify_all();
    return;
  }

  if (op == "HB_ACK") {
    std::string nt;
    if (lic_json::get_string(payload, "next_token", nt)) {
      std::lock_guard<std::mutex> lk(g.m);
      g.session_token = nt;
    }
    return;
  }

  if (op == "KICK") {
    std::string reason = "unknown";
    lic_json::get_string(payload, "reason", reason);
    apply_kick(reason);
    return;
  }

  if (op == "BLOB_RESP") {
    std::string blob_key, iv_b64, ct_b64, sk_hex;
    lic_json::get_string(payload, "blob_key", blob_key);
    lic_json::get_string(payload, "iv", iv_b64);
    lic_json::get_string(payload, "ciphertext", ct_b64);
    lic_json::get_string(payload, "session_key_hex", sk_hex);

    std::vector<uint8_t> key = HexToBytes(sk_hex);
    std::vector<uint8_t> iv = Base64Decode(iv_b64);
    std::vector<uint8_t> ct = Base64Decode(ct_b64);
    std::vector<uint8_t> plain = AesGcmDecrypt(key, iv, ct);

    {
      std::lock_guard<std::mutex> lk(g.blob_m);
      if (g.blob_key_pending == blob_key) {
        g.blob_plain = std::move(plain);
        g.blob_received = true;
        g.blob_cv.notify_all();
      }
    }
    return;
  }

  if (op == "DOWNLOAD_URL") {
    std::string url;
    lic_json::get_string(payload, "url", url);
    {
      std::lock_guard<std::mutex> lk(g.download_m);
      g.download_url = url;
      g.download_received = true;
      g.download_cv.notify_all();
    }
    return;
  }
}

// =============================================================================
// Heartbeat thread
// =============================================================================

void heartbeat_loop(GuardState& g) {
  while (!g.stopping.load()) {
    int interval = 25000;
    {
      std::lock_guard<std::mutex> lk(g.m);
      interval = g.hb_interval_ms;
    }
    for (int slept = 0; slept < interval && !g.stopping.load(); slept += 250) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    if (g.stopping.load()) break;
    if (g.status.load() == Status::OK) {
      send_heartbeat(g);
    }
  }
}

// =============================================================================
// Internal: WS bağlantı + AUTH_OK bekle (synchronous)
//
// suppress_kick → manuel login denemeleri için: KICK gelirse program çıkmasın.
// Başarılıysa heartbeat thread başlatılmış, g_state singleton dolu olarak
// döner. Başarısızsa tüm kaynaklar serbest bırakılır, g_state nullptr olur.
// =============================================================================

void teardown_global_state_locked() {
  // Mutex çağrı tarafından zaten alındı varsayılır.
  if (!g_state) return;
  g_state->stopping.store(true);
  if (g_state->ws) g_state->ws->Stop();
  if (g_state->tcp) g_state->tcp->Stop();
  if (g_state->heartbeat_thread.joinable()) {
    if (g_state->heartbeat_thread.get_id() == std::this_thread::get_id())
      g_state->heartbeat_thread.detach();
    else
      g_state->heartbeat_thread.join();
  }
  g_state.reset();
}

LIC_VL_NOINLINE
bool try_connect_and_auth(const std::string& app_id,
                          const std::string& build_secret,
                          const StoredAuth& auth,
                          const std::string& url, const StartOptions& opts,
                          bool suppress_kick) {
  LicGuardTuAnchor(0x41555448u);
  LIC_VL_AUTH_VM_OPEN;
  bool lic_vl_ok = false;
  do {
  // Mevcut bir state varsa temizle (önceki başarısız deneme)
  {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    teardown_global_state_locked();
    g_state = std::make_unique<GuardState>();
  }

  auto& g = *g_state;
  g.opts = opts;
  g.app_public_id = app_id;
  g.build_secret = build_secret;
  g.use_account_auth = auth.account;
  if (auth.account) {
    g.account_username = auth.username;
    g.account_password = auth.password;
    g.license_key = auth.username;
  } else {
    g.license_key = auth.license_key;
  }
  g.ws_url = url;
  g.suppress_kick_termination.store(suppress_kick);

  // Wire crypto: Phase A app_key = HKDF(build_secret, "lic-wire-v1", app_id)
  g.wire_app_key = expectional_lic_vm::DeriveAppKey(build_secret, app_id);
  if (g.wire_app_key.size() != 32) {
    set_status(Status::Error);
    set_error(LXS("Could not initialize a secure session."));
    std::lock_guard<std::mutex> lk(g_state_mutex);
    teardown_global_state_locked();
    break;
  }
  // X-App-Tag header (app_id wire'da çıplak gözükmesin)
  std::string app_tag = expectional_lic_vm::BuildAppTag(build_secret, app_id);

  HwidResult hw = ComputeHwid();
  g.hwid_hash = hw.id;
  g.hwid_scores = std::move(hw.scores);

  {
    std::vector<std::string> crack_tools = ListDetectedCrackProcesses();
    if (!crack_tools.empty()) {
      std::string api_base = DeriveApiBaseFromLicenseUrl(url);
      std::string mn = GetDisplayMachineNameUtf8();
      const std::string lic_prefix =
          auth.account
              ? (auth.username.size() >= 8 ? auth.username.substr(0, 8) : auth.username)
              : (auth.license_key.size() >= 8 ? auth.license_key.substr(0, 8)
                                             : auth.license_key);
      std::thread([api_base, app_id, build_secret, crack_tools, mn, lic_prefix]() {
        SendLicenseSecurityReport(api_base, app_id, build_secret, "crack_tools", mn,
                                  crack_tools, lic_prefix);
      }).detach();
    }
  }

  set_status(Status::Connecting);

  auto on_text = [&g](const std::string& s) { handle_message(g, s); };
  auto on_close = [&g] {
    if (g.stopping.load()) return;
    if (g.status.load() == Status::Kicked) return;
    if (g.status.load() == Status::OK) {
      apply_kick(LXS("connection_lost"));
    } else {
      set_status(Status::Error);
      g.auth_cv.notify_all();
    }
  };

  const bool use_tcp = (g.ws_url.compare(0, 6, "tcp://") == 0);
  bool ok = false;
  if (use_tcp) {
    g.tcp = std::make_unique<LicTcp>();
    ok = g.tcp->Start(g.ws_url, app_tag, on_text, on_close);
  } else {
    g.ws = std::make_unique<WinHttpWs>();
    std::string headers = std::string("X-App-Tag: ") + app_tag + "\r\n";
    ok = g.ws->Start(g.ws_url, headers, on_text, on_close);
  }

  if (!ok) {
    set_status(Status::Error);
    set_error(LXS("Could not reach the authentication service."));
    std::lock_guard<std::mutex> lk(g_state_mutex);
    teardown_global_state_locked();
    break;
  }

  // AUTH_OK / KICK / Error / timeout bekle
  Status final_status = Status::Error;
  {
    std::unique_lock<std::mutex> lk(g.m);
    bool signaled = g.auth_cv.wait_for(
        lk, std::chrono::milliseconds(opts.auth_timeout_ms), [&] {
          Status s = g.status.load();
          return s == Status::OK || s == Status::Kicked || s == Status::Error;
        });
    final_status = g.status.load();
    if (!signaled && final_status != Status::OK) {
      g.last_error = LXS("Authentication timed out.");
    }
  }

  if (final_status == Status::OK) {
    g.session_active.store(true);
    g.suppress_kick_termination.store(false);
    g.heartbeat_thread = std::thread([&g] { heartbeat_loop(g); });
    lic_vl_ok = true;
    break;
  }

  // Başarısız — temizle
  {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    teardown_global_state_locked();
  }
  } while (0);
  LIC_VL_AUTH_VM_CLOSE;
  return lic_vl_ok;
}

// Launcher progress UI: WS/HTTP beklerken ana-thread mesaj döngüsünü pompalayarak
// WM_TIMER ile çubuk/metin güncellenir (download_cv.wait_for mesajları bloklar).
static void PumpGuiMessagesForDownloadUi() {
  constexpr DWORD kSliceMs = 50;
  (void)MsgWaitForMultipleObjectsEx(0, nullptr, kSliceMs, QS_ALLINPUT,
                                    MWMO_INPUTAVAILABLE);
  MSG msg;
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

}  // namespace

// İmzalı indirme URL'si GET (public API'den çağrılır; unnamed namespace dışında).
// err_detail: HTTP != 200 ise JSON body'deki error (+ file_id) eklenir (proxy/HTML ayrımı).
static bool HttpGetUrlUtf8(const std::string& url_utf8, std::vector<uint8_t>& out,
                          std::string* err_detail) {
  VL_OBF_SCOPE;

  int nw = MultiByteToWideChar(CP_UTF8, 0, url_utf8.c_str(), -1, nullptr, 0);
  if (nw <= 0) return false;
  std::wstring wurl(static_cast<size_t>(nw - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, url_utf8.c_str(), -1, wurl.data(), nw);

  std::vector<wchar_t> host(1024);
  std::vector<wchar_t> url_path(8192);
  std::vector<wchar_t> extra_info(65536);

  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof(uc);
  uc.lpszHostName = host.data();
  uc.dwHostNameLength = static_cast<DWORD>(host.size());
  uc.lpszUrlPath = url_path.data();
  uc.dwUrlPathLength = static_cast<DWORD>(url_path.size());
  uc.lpszExtraInfo = extra_info.data();
  uc.dwExtraInfoLength = static_cast<DWORD>(extra_info.size());

  if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.length()), 0, &uc)) return false;

  const bool secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
  INTERNET_PORT port = uc.nPort;
  if (port == 0) port = secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;

  std::wstring object = uc.lpszUrlPath ? std::wstring(uc.lpszUrlPath) : L"/";
  if (uc.lpszExtraInfo && uc.lpszExtraInfo[0]) object += uc.lpszExtraInfo;

  HINTERNET ses = WinHttpOpen(L"LicenseGuard/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) return false;
  DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(ses, WINHTTP_OPTION_REDIRECT_POLICY,
                   static_cast<LPVOID>(&redirect_policy), sizeof(redirect_policy));

  HINTERNET con = WinHttpConnect(ses, uc.lpszHostName, port, 0);
  if (!con) {
    WinHttpCloseHandle(ses);
    return false;
  }
  const DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET req =
      WinHttpOpenRequest(con, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!req) {
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return false;
  }
  if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(req, nullptr)) {
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return false;
  }
  DWORD status = 0;
  DWORD hdr_sz = sizeof(status);
  if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &status, &hdr_sz,
                           WINHTTP_NO_HEADER_INDEX)) {
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return false;
  }

  if (status != 200) {
    std::string body;
    for (;;) {
      char buf[4096];
      DWORD read = 0;
      if (!WinHttpReadData(req, buf, sizeof(buf), &read)) break;
      if (read == 0) break;
      if (body.size() < 8192) {
        const size_t take = std::min<size_t>(read, 8192 - body.size());
        body.append(buf, take);
      }
    }
    std::string detail = "HTTP " + std::to_string(status);
    std::string ej, fid, sk;
    if (lic_json::get_string(body, "error", ej)) {
      detail += " - " + ej;
      if (lic_json::get_string(body, "file_id", fid)) detail += " file_id=" + fid;
      if (lic_json::get_string(body, "storage_key", sk)) detail += " storage_key=" + sk;
    } else if (!body.empty() && body.size() <= 256) {
      detail += " body=" + body;
    }
    if (err_detail) *err_detail = std::move(detail);
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return false;
  }

  out.clear();
  for (;;) {
    char buf[32768];
    DWORD read = 0;
    if (!WinHttpReadData(req, buf, sizeof(buf), &read)) {
      WinHttpCloseHandle(req);
      WinHttpCloseHandle(con);
      WinHttpCloseHandle(ses);
      return false;
    }
    if (read == 0) break;
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(buf),
               reinterpret_cast<const uint8_t*>(buf) + read);
    PumpGuiMessagesForDownloadUi();
  }
  WinHttpCloseHandle(req);
  WinHttpCloseHandle(con);
  WinHttpCloseHandle(ses);
  return true;
}

// =============================================================================
// Public API
// =============================================================================

static bool ShouldPurgeLicenseDatAfterAutoLoginFail(const std::string& user_err) {
  if (user_err.empty()) return false;
  static const char* kPurge[] = {
      "Invalid username or password.",
      "Invalid license key.",
      "This license has expired.",
      "This license has been revoked.",
      "This license is bound to another device.",
      "Access denied.",
  };
  for (const char* p : kPurge) {
    if (user_err == p) return true;
  }
  return false;
}

#if defined(USE_VL_MACRO)
__declspec(noinline)
#endif
bool Start(const StartOptions& opts) {
	// Eğer halihazırda Start edilmişse hata
	{
    std::lock_guard<std::mutex> lk(g_state_mutex);
    if (g_state) return false;
  }

  // Tüm bağlantı bilgileri ARTIK opts'tan; license.dat SADECE license_key tutar.
  if (opts.baked_app_id.empty() || opts.baked_ws_url.empty() ||
      opts.baked_build_secret.empty()) {
    show_license_fatal_error(
        LXS("Configuration Error"),
        LXS("This application is not configured correctly."),
        opts.show_messagebox_on_kick, false);
    return false;
  }
  const std::string m_app_id = opts.baked_app_id;
  const std::string m_url = opts.baked_ws_url;
  const std::string m_secret = opts.baked_build_secret;

  std::string lic_path = opts.license_dat_path.empty()
                             ? default_license_dat_path()
                             : opts.license_dat_path;

  std::string saved_plain;
  bool have_dat = expectional_lic_vm::ReadLicenseDat(lic_path, m_secret, saved_plain);
  StoredAuth saved_auth;
  if (have_dat) have_dat = parse_dat_plaintext(saved_plain, saved_auth);
  if (have_dat && is_dat_plaintext_expired(saved_auth)) {
    delete_license_dat_safely(lic_path);
    have_dat = false;
  }

  // ------ 1) license.dat varsa otomatik login ------
  // Obscurion/code_hash degisince sunucu KICK edebilir; suppress_kick ile ExitProcess yerine manuel login'e dus.
  if (have_dat) {
    if (try_connect_and_auth(m_app_id, m_secret, saved_auth, m_url, opts,
                              /*suppress_kick=*/true)) {
      if (auto* g = GS()) g->suppress_kick_termination.store(false);
      return true;
    }
    if (ShouldPurgeLicenseDatAfterAutoLoginFail(LastError()))
      delete_license_dat_safely(lic_path);
  }

  // ------ 2) Manuel login modu kapalıysa: hata ------
  if (!opts.manual_input_if_missing) {
    const std::string body =
        have_dat ? std::string(LXS("Your license could not be verified."))
                 : std::string(LXS("Saved license data was not found."));
    show_license_fatal_error(LXS("License Error"), body, opts.show_messagebox_on_kick,
                             false);
    return false;
  }

  // ------ 3) Manuel login: ImGui (username + password) ------
  LoginCredentials success_creds;

  bool dialog_ok = ShowLoginDialog(
      opts.product_display_name,
      [&](const LoginCredentials& entered) -> std::pair<bool, std::string> {
        StoredAuth attempt;
        attempt.account = true;
        attempt.username = entered.username;
        attempt.password = entered.password;
        if (try_connect_and_auth(m_app_id, m_secret, attempt, m_url, opts,
                                 /*suppress_kick=*/true)) {
          success_creds = entered;
          if (auto* g = GS()) g->suppress_kick_termination.store(false);
          return {true, ""};
        }
        std::string err = LastError();
        if (err.empty()) err = LXS("Unable to verify license.");
        return {false, err};
      });

  if (!dialog_ok) {
    return false;
  }

  // ------ 4) Başarılı login → license.dat'a kaydet ------
  if (opts.save_after_manual_login) {
    expectional_lic_vm::WriteLicenseDat(lic_path, m_secret,
                                        encode_account_dat_plain(success_creds));
  }

  return true;
}

void Stop() {
  std::lock_guard<std::mutex> lk(g_state_mutex);
  teardown_global_state_locked();
}

Status GetStatus() {
  std::lock_guard<std::mutex> lk(g_state_mutex);
  return g_state ? g_state->status.load() : Status::NotStarted;
}

std::string LastError() {
  std::lock_guard<std::mutex> lk(g_state_mutex);
  if (!g_state) return "";
  std::lock_guard<std::mutex> lk2(g_state->m);
  return g_state->last_error;
}

long long GetLicenseExpiryEpochMs() {
  std::lock_guard<std::mutex> lk(g_state_mutex);
  if (!g_state) return 0;
  std::lock_guard<std::mutex> lk2(g_state->m);
  return g_state->license_expiry_epoch_ms;
}

bool WaitForAuth(int timeout_ms) {
  // Start() artık AUTH_OK gelene kadar blokluyor; bu fonksiyon backward-compat.
  GuardState* g = nullptr;
  {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    g = g_state.get();
  }
  if (!g) return false;
  Status s = g->status.load();
  if (s == Status::OK) return true;
  if (s == Status::Kicked || s == Status::Error) return false;

  std::unique_lock<std::mutex> lk(g->m);
  return g->auth_cv.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                             [&] {
                               Status x = g->status.load();
                               return x == Status::OK || x == Status::Kicked ||
                                      x == Status::Error;
                             }) &&
         g->status.load() == Status::OK;
}

bool RequestBlob(const std::string& blob_key, std::vector<uint8_t>& out_plain,
                 int timeout_ms) {
  GuardState* g = nullptr;
  {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    g = g_state.get();
  }
  if (!g || g->status.load() != Status::OK) return false;

  std::string token;
  {
    std::lock_guard<std::mutex> lk(g->m);
    token = g->session_token;
  }
  if (token.empty()) return false;

  {
    std::lock_guard<std::mutex> lk(g->blob_m);
    g->blob_key_pending = blob_key;
    g->blob_received = false;
    g->blob_plain.clear();
  }

  lic_json::Builder payload;
  payload.add_str("blob_key", blob_key).add_str("token", token);
  lic_json::Builder env;
  env.add_str("op", "BLOB_GET")
     .add_int("seq", next_seq(*g))
     .add_raw("payload", payload.done());
  if (!send_encrypted(*g, env.done())) return false;

  std::unique_lock<std::mutex> lk(g->blob_m);
  if (!g->blob_cv.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                           [&] { return g->blob_received; })) {
    return false;
  }
  out_plain = g->blob_plain;
  return !out_plain.empty();
}

bool RequestFileDownload(const std::string& file_id, std::vector<uint8_t>& out_bytes,
                         int timeout_ms) {
  GuardState* g = nullptr;
  {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    g = g_state.get();
  }
  if (!g || g->status.load() != Status::OK) return false;

  std::string token;
  {
    std::lock_guard<std::mutex> lk(g->m);
    token = g->session_token;
  }
  if (token.empty()) return false;

  VL_OBF_SCOPE;

  {
    std::lock_guard<std::mutex> lk(g->download_m);
    g->download_received = false;
    g->download_url.clear();
  }

  lic_json::Builder payload;
  payload.add_str("file_id", file_id).add_str("token", token);
  lic_json::Builder env;
  env.add_str("op", "DOWNLOAD_REQ")
     .add_int("seq", next_seq(*g))
     .add_raw("payload", payload.done());
  if (!send_encrypted(*g, env.done())) return false;

  std::unique_lock<std::mutex> lk(g->download_m);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (!g->download_received) {
    if (std::chrono::steady_clock::now() >= deadline) {
      set_error(LXS("Download timed out."));
      return false;
    }
    lk.unlock();
    PumpGuiMessagesForDownloadUi();
    lk.lock();
  }
  if (g->status.load() != Status::OK) {
    return false;
  }
  const std::string url = g->download_url;
  lk.unlock();

  if (url.empty()) {
    set_error(LXS("Download was denied."));
    return false;
  }
  std::string http_err;
  if (!HttpGetUrlUtf8(url, out_bytes, &http_err) || out_bytes.empty()) {
    (void)http_err;
    set_error(LXS("Download failed."));
    return false;
  }
  return true;
}

}  // namespace lic
