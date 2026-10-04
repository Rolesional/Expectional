#include "../include/lic_crypto.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <cstdio>
#include <cstring>

namespace lic {

std::string ToLowerHex(const uint8_t* data, size_t len) {
  static const char* hex = "0123456789abcdef";
  std::string out(len * 2, '\0');
  for (size_t i = 0; i < len; ++i) {
    out[i * 2] = hex[data[i] >> 4];
    out[i * 2 + 1] = hex[data[i] & 0x0F];
  }
  return out;
}

std::string ToUpperAscii(std::string s) {
  for (auto& c : s) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
  }
  return s;
}

std::string Sha256Hex(const uint8_t* data, size_t len) {
  BCRYPT_ALG_HANDLE hAlg = nullptr;
  if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return "";
  uint8_t hash[32];
  NTSTATUS ok = BCryptHash(hAlg, nullptr, 0, const_cast<PUCHAR>(data),
                           static_cast<ULONG>(len), hash, sizeof(hash));
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return ok == 0 ? ToLowerHex(hash, sizeof(hash)) : std::string{};
}

std::string Sha256Hex(const std::string& data) {
  return Sha256Hex(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

std::vector<uint8_t> HmacSha256Bytes(const std::string& key, const std::string& payload) {
  std::vector<uint8_t> out;
  BCRYPT_ALG_HANDLE hAlg = nullptr;
  if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr,
                                  BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0)
    return out;
  BCRYPT_HASH_HANDLE hHash = nullptr;
  NTSTATUS s = BCryptCreateHash(
      hAlg, &hHash, nullptr, 0,
      reinterpret_cast<PUCHAR>(const_cast<char*>(key.data())),
      static_cast<ULONG>(key.size()), 0);
  if (s == 0) {
    s = BCryptHashData(hHash,
                       reinterpret_cast<PUCHAR>(const_cast<char*>(payload.data())),
                       static_cast<ULONG>(payload.size()), 0);
    if (s == 0) {
      out.resize(32);
      s = BCryptFinishHash(hHash, out.data(), static_cast<ULONG>(out.size()), 0);
      if (s != 0) out.clear();
    }
    BCryptDestroyHash(hHash);
  }
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return out;
}

std::string HmacSha256Hex(const std::string& key, const std::string& payload) {
  auto b = HmacSha256Bytes(key, payload);
  return b.empty() ? std::string{} : ToLowerHex(b.data(), b.size());
}

std::vector<uint8_t> AesGcmDecrypt(const std::vector<uint8_t>& key,
                                   const std::vector<uint8_t>& iv,
                                   const std::vector<uint8_t>& ctWithTag) {
  if (key.size() != 32 || iv.size() != 12 || ctWithTag.size() < 16) return {};
  BCRYPT_ALG_HANDLE hAlg = nullptr;
  if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) return {};
  BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                    reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                    sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
  BCRYPT_KEY_HANDLE hKey = nullptr;
  NTSTATUS s = BCryptGenerateSymmetricKey(
      hAlg, &hKey, nullptr, 0,
      reinterpret_cast<PUCHAR>(const_cast<uint8_t*>(key.data())),
      static_cast<ULONG>(key.size()), 0);
  if (s != 0) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return {};
  }
  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO ai;
  BCRYPT_INIT_AUTH_MODE_INFO(ai);
  ai.pbNonce = const_cast<PUCHAR>(iv.data());
  ai.cbNonce = static_cast<ULONG>(iv.size());
  const size_t tagOff = ctWithTag.size() - 16;
  ai.pbTag = const_cast<PUCHAR>(ctWithTag.data() + tagOff);
  ai.cbTag = 16;
  std::vector<uint8_t> out(tagOff);
  ULONG resultLen = 0;
  s = BCryptDecrypt(
      hKey, reinterpret_cast<PUCHAR>(const_cast<uint8_t*>(ctWithTag.data())),
      static_cast<ULONG>(tagOff), &ai, nullptr, 0, out.data(),
      static_cast<ULONG>(out.size()), &resultLen, 0);
  BCryptDestroyKey(hKey);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  if (s != 0) return {};
  out.resize(resultLen);
  return out;
}

std::vector<uint8_t> AesGcmEncrypt(const std::vector<uint8_t>& key,
                                   const std::vector<uint8_t>& iv,
                                   const std::vector<uint8_t>& plaintext) {
  if (key.size() != 32 || iv.size() != 12) return {};
  BCRYPT_ALG_HANDLE hAlg = nullptr;
  if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) return {};
  BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                    reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                    sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
  BCRYPT_KEY_HANDLE hKey = nullptr;
  NTSTATUS s = BCryptGenerateSymmetricKey(
      hAlg, &hKey, nullptr, 0,
      reinterpret_cast<PUCHAR>(const_cast<uint8_t*>(key.data())),
      static_cast<ULONG>(key.size()), 0);
  if (s != 0) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return {};
  }
  uint8_t tag[16] = {0};
  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO ai;
  BCRYPT_INIT_AUTH_MODE_INFO(ai);
  ai.pbNonce = const_cast<PUCHAR>(iv.data());
  ai.cbNonce = static_cast<ULONG>(iv.size());
  ai.pbTag = tag;
  ai.cbTag = sizeof(tag);
  std::vector<uint8_t> ct(plaintext.size());
  ULONG resultLen = 0;
  s = BCryptEncrypt(
      hKey,
      plaintext.empty()
          ? nullptr
          : reinterpret_cast<PUCHAR>(const_cast<uint8_t*>(plaintext.data())),
      static_cast<ULONG>(plaintext.size()), &ai, nullptr, 0,
      ct.empty() ? nullptr : ct.data(), static_cast<ULONG>(ct.size()),
      &resultLen, 0);
  BCryptDestroyKey(hKey);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  if (s != 0) return {};
  ct.resize(resultLen);
  ct.insert(ct.end(), tag, tag + sizeof(tag));
  return ct;
}

std::vector<uint8_t> RandomBytes(size_t n) {
  std::vector<uint8_t> out(n);
  if (n == 0) return out;
  if (BCryptGenRandom(nullptr, out.data(), static_cast<ULONG>(n),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
    return {};
  }
  return out;
}

// HKDF-SHA256: extract + expand (RFC 5869). Çıktı boyutu out_len byte.
std::vector<uint8_t> HkdfSha256(const std::vector<uint8_t>& ikm,
                                 const std::string& salt,
                                 const std::string& info, size_t out_len) {
  if (out_len == 0 || out_len > 32 * 255) return {};
  // Extract: PRK = HMAC(salt, IKM)
  std::string saltStr = salt;
  if (saltStr.empty()) saltStr = std::string(32, '\0');
  std::string ikmStr(reinterpret_cast<const char*>(ikm.data()), ikm.size());
  auto prk = HmacSha256Bytes(saltStr, ikmStr);
  if (prk.size() != 32) return {};

  // Expand: T(i) = HMAC(PRK, T(i-1) || info || i)
  std::string prkStr(reinterpret_cast<const char*>(prk.data()), prk.size());
  std::vector<uint8_t> out;
  out.reserve(out_len);
  std::vector<uint8_t> t_prev;
  for (uint8_t ctr = 1; out.size() < out_len; ++ctr) {
    std::string in;
    in.append(reinterpret_cast<const char*>(t_prev.data()), t_prev.size());
    in.append(info);
    in.push_back(static_cast<char>(ctr));
    auto t = HmacSha256Bytes(prkStr, in);
    if (t.size() != 32) return {};
    size_t take = (out.size() + 32 <= out_len) ? 32 : (out_len - out.size());
    out.insert(out.end(), t.begin(), t.begin() + take);
    t_prev = std::move(t);
  }
  return out;
}

std::vector<uint8_t> Base64Decode(const std::string& s) {
  static int8_t lut[256];
  static bool inited = false;
  if (!inited) {
    for (int i = 0; i < 256; ++i) lut[i] = -1;
    const char* alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; ++i) lut[(uint8_t)alpha[i]] = static_cast<int8_t>(i);
    lut[(uint8_t)'-'] = 62;
    lut[(uint8_t)'_'] = 63;
    inited = true;
  }
  std::vector<uint8_t> out;
  out.reserve((s.size() / 4) * 3);
  int val = 0, bits = -8;
  for (unsigned char c : s) {
    if (c == '=' || c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
    const int v = lut[c];
    if (v < 0) continue;
    val = (val << 6) | v;
    bits += 6;
    if (bits >= 0) {
      out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return out;
}

std::string Base64Encode(const uint8_t* data, size_t len) {
  static const char* alpha =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  size_t i = 0;
  while (i + 3 <= len) {
    uint32_t v = (uint32_t)data[i] << 16 | (uint32_t)data[i + 1] << 8 | (uint32_t)data[i + 2];
    out.push_back(alpha[(v >> 18) & 0x3F]);
    out.push_back(alpha[(v >> 12) & 0x3F]);
    out.push_back(alpha[(v >> 6) & 0x3F]);
    out.push_back(alpha[v & 0x3F]);
    i += 3;
  }
  if (i < len) {
    uint32_t v = (uint32_t)data[i] << 16;
    if (i + 1 < len) v |= (uint32_t)data[i + 1] << 8;
    out.push_back(alpha[(v >> 18) & 0x3F]);
    out.push_back(alpha[(v >> 12) & 0x3F]);
    if (i + 1 < len) {
      out.push_back(alpha[(v >> 6) & 0x3F]);
      out.push_back('=');
    } else {
      out.push_back('=');
      out.push_back('=');
    }
  }
  return out;
}

std::string Base64Encode(const std::vector<uint8_t>& bytes) {
  return Base64Encode(bytes.data(), bytes.size());
}

std::vector<uint8_t> HexToBytes(const std::string& hex) {
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
  };
  std::vector<uint8_t> out;
  out.reserve(hex.size() / 2);
  for (size_t i = 0; i + 1 < hex.size(); i += 2) {
    int hi = nib(hex[i]);
    int lo = nib(hex[i + 1]);
    if (hi < 0 || lo < 0) return {};
    out.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return out;
}

}  // namespace lic
