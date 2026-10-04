#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <string>

// Compile-time XOR string obfuscation.
// Plaintext binary'de görünmez; "strings" çıkarımına direnir.
// Anahtar __TIME__ + __DATE__ + __LINE__ tabanlı, her satırda farklı.

namespace lic_xor {

constexpr uint64_t fnv1a64(const char* s, std::size_t i, uint64_t h = 1469598103934665603ULL) {
  return s[i] == '\0' ? h
                      : fnv1a64(s, i + 1, (h ^ static_cast<unsigned char>(s[i])) * 1099511628211ULL);
}
constexpr uint64_t base_seed() {
  return fnv1a64(__TIME__, 0) ^ fnv1a64(__DATE__, 0);
}
constexpr uint8_t kbyte(uint64_t seed, std::size_t i) {
  uint64_t v = seed + i * 2654435761ULL;
  v ^= v >> 33;
  v *= 0xff51afd7ed558ccdULL;
  v ^= v >> 33;
  return static_cast<uint8_t>(v & 0xFF);
}

template <std::size_t N>
class enc {
 public:
  constexpr enc(const char (&s)[N], uint64_t seed) : seed_(seed) {
    for (std::size_t i = 0; i < N; ++i)
      data_[i] = static_cast<char>(static_cast<uint8_t>(s[i]) ^ kbyte(seed, i));
  }
  std::string get() const {
    std::string out(N - 1, '\0');
    for (std::size_t i = 0; i < N - 1; ++i)
      out[i] = static_cast<char>(static_cast<uint8_t>(data_[i]) ^ kbyte(seed_, i));
    return out;
  }
 private:
  uint64_t seed_;
  std::array<char, N> data_{};
};

}  // namespace lic_xor

#define LXS(literal) \
  (::lic_xor::enc<sizeof(literal)>((literal), ::lic_xor::base_seed() ^ (__LINE__ * 1469598103934665603ULL)).get())
