#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace lic {

/// Polimorfik tehdit puanı (her bit ayrı bir kontrolün sonucu).
uint64_t ComputeThreatScore();

/// Bilinen crack/RE süreçleri (UTF-8 exe adları).
std::vector<std::string> ListDetectedCrackProcesses();

/// Ana modülün ilk N byte'ının SHA-256'sı (binary patcher tespiti).
std::string SelfCodeSha256();

}  // namespace lic
