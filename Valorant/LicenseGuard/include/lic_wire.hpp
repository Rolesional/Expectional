#pragma once
//
// Wire encryption — server'daki server/src/lib/wire-crypto.ts ile birebir.
//
// Her WS frame:
//   { "v":1, "n":"<b64 12B IV>", "c":"<b64 ct||tag>" }
//
// Phase A key (HANDSHAKE/CHALLENGE/AUTH/AUTH_OK):
//   key = HKDF-SHA256(BUILD_SECRET, salt="lic-wire-v1", info=app_id, len=32)
//
// Phase B key (HEARTBEAT/HB_ACK/KICK/BLOB_*/DOWNLOAD_*/CFG_*):
//   key = session_key  (server'ın AUTH_OK ile gönderdiği rastgele 32 byte)
//
// AUTH_OK içinde enc_session_key = AES-GCM(session_key, key=licEnvKey),
//   licEnvKey = HKDF(SHA-256(UPPER(licenseKey)), salt="lic-session-v1",
//                    info=challenge_nonce_hex, len=32)
//
// X-App-Tag header = base64url-trunc16( HMAC(BUILD_SECRET, "lic-app-tag-v1:" + app_id) )
//

#include <cstdint>
#include <string>
#include <vector>

namespace lic {

std::vector<uint8_t> DeriveAppKey(const std::string& build_secret,
                                   const std::string& app_public_id);

std::vector<uint8_t> DeriveLicenseEnvelopeKey(const std::string& license_key_upper,
                                               const std::string& nonce_hex);

/// X-App-Tag header değeri (base64url, padding'siz, 22 karakter civarı).
std::string BuildAppTag(const std::string& build_secret,
                        const std::string& app_public_id);

/// payload_json düz mesajı şifreleyip { v, n, c } JSON döner.
std::string EncryptEnvelope(const std::vector<uint8_t>& key,
                            const std::string& payload_json);

/// Gelen frame_json içerisinden { n, c } okuyup AES-GCM decrypt eder.
/// Hata → boş string.
std::string DecryptEnvelope(const std::vector<uint8_t>& key,
                            const std::string& frame_json);

}  // namespace lic
