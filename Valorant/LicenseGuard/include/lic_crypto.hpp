#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace lic {

std::string ToLowerHex(const uint8_t* data, size_t len);
std::string ToUpperAscii(std::string s);

std::string Sha256Hex(const std::string& data);
std::string Sha256Hex(const uint8_t* data, size_t len);
std::vector<uint8_t> HmacSha256Bytes(const std::string& key, const std::string& payload);
std::string HmacSha256Hex(const std::string& key, const std::string& payload);

/// AES-256-GCM decrypt. ciphertextWithTag = ct || tag(16). iv = 12 byte. key = 32 byte.
/// Hata olursa boş vektör döner.
std::vector<uint8_t> AesGcmDecrypt(const std::vector<uint8_t>& key,
                                   const std::vector<uint8_t>& iv,
                                   const std::vector<uint8_t>& ctWithTag);

/// AES-256-GCM encrypt. iv = 12 random byte. Çıktı: ct || 16-byte tag.
/// key = 32 byte. Hata → boş vektör.
std::vector<uint8_t> AesGcmEncrypt(const std::vector<uint8_t>& key,
                                   const std::vector<uint8_t>& iv,
                                   const std::vector<uint8_t>& plaintext);

/// 12-byte random IV. CryptGenRandom üzerinden BCrypt RNG.
std::vector<uint8_t> RandomBytes(size_t n);

/// HKDF-SHA256 (extract+expand). RFC 5869.
std::vector<uint8_t> HkdfSha256(const std::vector<uint8_t>& ikm,
                                 const std::string& salt,
                                 const std::string& info, size_t out_len);

std::vector<uint8_t> Base64Decode(const std::string& s);
std::string Base64Encode(const uint8_t* data, size_t len);
std::string Base64Encode(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> HexToBytes(const std::string& hex);

}  // namespace lic
