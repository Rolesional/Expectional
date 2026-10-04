#pragma once
#include <string>
#include <unordered_map>

namespace lic {

struct HwidResult {
  std::string id;                                // SHA-256 hex (server hwid_hash)
  std::unordered_map<std::string, int> scores;   // drift score için bileşen hash'leri
};

HwidResult ComputeHwid();

/// DNS / NetBIOS — okunur makine adı (GetComputerNameEx + WMI + COMPUTERNAME).
std::string GetDisplayMachineNameUtf8();

/// WMI + bellek: CPU, OS, GPU, üretici/model, RAM MB — JSON string (HANDSHAKE.pc_specs).
std::string BuildPcSpecsJsonUtf8();

}  // namespace lic
