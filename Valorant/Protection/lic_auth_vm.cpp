#include "lic_auth_vm.hpp"

#include "../LicenseGuard/include/lic_crypto.hpp"
#include "../LicenseGuard/include/lic_wire.hpp"

#include "vxlang_per_tu.hpp"

#include <fstream>
#include <iterator>

namespace expectional_lic_vm {
namespace {

std::vector<uint8_t> derive_dat_key_local(const std::string& build_secret) {
  std::vector<uint8_t> ikm(build_secret.begin(), build_secret.end());
  return lic::HkdfSha256(ikm, "lic-dat-v2", "license-key", 32);
}

std::string to_upper_ascii_local(std::string s) {
  for (char& c : s) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
  }
  return s;
}

}  // namespace

#if defined(USE_VL_MACRO)
__declspec(noinline)
#endif
bool ReadLicenseDat(const std::string& path, const std::string& build_secret,
                    std::string& out_license_key) {
  bool ok = false;
#if defined(USE_VL_MACRO)
  EX_VL_AUTH_VM_BEGIN;
#endif
  do {
    out_license_key.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) break;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
    if (bytes.size() < 4 + 12 + 17) break;
    if (bytes[0] != 'L' || bytes[1] != 'I' || bytes[2] != 'C' || bytes[3] != '2') break;
    std::vector<uint8_t> iv(bytes.begin() + 4, bytes.begin() + 16);
    std::vector<uint8_t> ct(bytes.begin() + 16, bytes.end());
    auto key = derive_dat_key_local(build_secret);
    if (key.size() != 32) break;
    auto plain = lic::AesGcmDecrypt(key, iv, ct);
    if (plain.empty()) break;
    out_license_key.assign(plain.begin(), plain.end());
    ok = !out_license_key.empty();
  } while (0);
#if defined(USE_VL_MACRO)
  EX_VL_AUTH_VM_END;
#endif
  return ok;
}

#if defined(USE_VL_MACRO)
__declspec(noinline)
#endif
bool WriteLicenseDat(const std::string& path, const std::string& build_secret,
                     const std::string& license_key) {
  bool ok = false;
#if defined(USE_VL_MACRO)
  EX_VL_AUTH_VM_BEGIN;
#endif
  do {
    auto key = derive_dat_key_local(build_secret);
    if (key.size() != 32) break;
    auto iv = lic::RandomBytes(12);
    if (iv.size() != 12) break;
    std::vector<uint8_t> pt(license_key.begin(), license_key.end());
    auto ct = lic::AesGcmEncrypt(key, iv, pt);
    if (ct.empty()) break;

    std::vector<uint8_t> out;
    out.reserve(4 + 12 + ct.size());
    out.push_back('L');
    out.push_back('I');
    out.push_back('C');
    out.push_back('2');
    out.insert(out.end(), iv.begin(), iv.end());
    out.insert(out.end(), ct.begin(), ct.end());

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) break;
    f.write(reinterpret_cast<const char*>(out.data()),
            static_cast<std::streamsize>(out.size()));
    ok = f.good();
  } while (0);
#if defined(USE_VL_MACRO)
  EX_VL_AUTH_VM_END;
#endif
  return ok;
}

#if defined(USE_VL_MACRO)
__declspec(noinline)
#endif
std::vector<uint8_t> DeriveAppKey(const std::string& build_secret,
                                  const std::string& app_public_id) {
  std::vector<uint8_t> out;
#if defined(USE_VL_MACRO)
  EX_VL_AUTH_VM_BEGIN;
#endif
  do {
    out = lic::DeriveAppKey(build_secret, app_public_id);
  } while (0);
#if defined(USE_VL_MACRO)
  EX_VL_AUTH_VM_END;
#endif
  return out;
}

#if defined(USE_VL_MACRO)
__declspec(noinline)
#endif
std::string BuildAppTag(const std::string& build_secret, const std::string& app_public_id) {
  std::string out;
#if defined(USE_VL_MACRO)
  EX_VL_AUTH_VM_BEGIN;
#endif
  do {
    out = lic::BuildAppTag(build_secret, app_public_id);
  } while (0);
#if defined(USE_VL_MACRO)
  EX_VL_AUTH_VM_END;
#endif
  return out;
}

#if defined(USE_VL_MACRO)
__declspec(noinline)
#endif
std::string BuildAuthNonceSig(const std::string& license_key_upper,
                              const std::string& app_id, const std::string& nonce,
                              const std::string& hwid_hash, int64_t ts_ms) {
  std::string sig;
#if defined(USE_VL_MACRO)
  EX_VL_AUTH_VM_BEGIN;
#endif
  do {
    const std::string key_upper = to_upper_ascii_local(license_key_upper);
    const std::string sig_payload =
        app_id + "|" + nonce + "|" + hwid_hash + "|" + std::to_string(ts_ms);
    sig = lic::HmacSha256Hex(key_upper, sig_payload);
  } while (0);
#if defined(USE_VL_MACRO)
  EX_VL_AUTH_VM_END;
#endif
  return sig;
}

}  // namespace expectional_lic_vm
