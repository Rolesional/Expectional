#include "../include/lic_wire.hpp"
#include "../include/lic_crypto.hpp"
#include "../include/lic_json.hpp"
#include "../include/lic_guard_vxlang.hpp"

namespace lic {

namespace {

std::string ToBase64UrlNoPad(const std::string& b64) {
  std::string out = b64;
  for (auto& c : out) {
    if (c == '+') c = '-';
    else if (c == '/') c = '_';
  }
  while (!out.empty() && out.back() == '=') out.pop_back();
  return out;
}

}  // namespace

std::vector<uint8_t> DeriveAppKey(const std::string& build_secret,
                                   const std::string& app_public_id) {
  std::vector<uint8_t> ikm(build_secret.begin(), build_secret.end());
  return HkdfSha256(ikm, "lic-wire-v1", app_public_id, 32);
}

std::vector<uint8_t> DeriveLicenseEnvelopeKey(const std::string& license_key_upper,
                                               const std::string& nonce_hex) {
  std::string lic_digest_hex = Sha256Hex(license_key_upper);
  std::vector<uint8_t> ikm = HexToBytes(lic_digest_hex);
  return HkdfSha256(ikm, "lic-session-v1", nonce_hex, 32);
}

std::string BuildAppTag(const std::string& build_secret,
                        const std::string& app_public_id) {
  auto mac = HmacSha256Bytes(build_secret, std::string("lic-app-tag-v1:") + app_public_id);
  if (mac.size() < 16) return {};
  std::string b64 = Base64Encode(mac.data(), 16);
  return ToBase64UrlNoPad(b64);
}

LIC_VL_NOINLINE
std::string EncryptEnvelope(const std::vector<uint8_t>& key,
                            const std::string& payload_json) {
  std::string frame;
  LIC_VL_AUTH_VM_OPEN;
  do {
    if (key.size() != 32) break;
    auto iv = RandomBytes(12);
    if (iv.size() != 12) break;
    std::vector<uint8_t> pt(payload_json.begin(), payload_json.end());
    auto ct = AesGcmEncrypt(key, iv, pt);
    if (ct.empty()) break;

    std::string iv_b64 = Base64Encode(iv);
    std::string ct_b64 = Base64Encode(ct);

    lic_json::Builder b;
    b.add_int("v", 1).add_str("n", iv_b64).add_str("c", ct_b64);
    frame = b.done();
  } while (0);
  LIC_VL_AUTH_VM_CLOSE;
  return frame;
}

LIC_VL_NOINLINE
std::string DecryptEnvelope(const std::vector<uint8_t>& key,
                            const std::string& frame_json) {
  std::string plain;
  LIC_VL_AUTH_VM_OPEN;
  do {
    if (key.size() != 32) break;
    std::string iv_b64, ct_b64;
    long long v = 0;
    lic_json::get_int(frame_json, "v", v);
    if (v != 1) break;
    if (!lic_json::get_string(frame_json, "n", iv_b64)) break;
    if (!lic_json::get_string(frame_json, "c", ct_b64)) break;
    auto iv = Base64Decode(iv_b64);
    auto ct = Base64Decode(ct_b64);
    if (iv.size() != 12 || ct.size() < 17) break;
    auto pt = AesGcmDecrypt(key, iv, ct);
    if (pt.empty()) break;
    plain.assign(pt.begin(), pt.end());
  } while (0);
  LIC_VL_AUTH_VM_CLOSE;
  return plain;
}

}  // namespace lic
