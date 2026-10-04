#pragma once

#include <cstdint>
#include <string>
#include <vector>

/** Login kripto yolu — her fonksiyon tek VL_VIRTUALIZATION BEGIN/END (Obscurion + thread guvenli). */
namespace expectional_lic_vm {

bool ReadLicenseDat(const std::string& path, const std::string& build_secret,
                    std::string& out_license_key);

bool WriteLicenseDat(const std::string& path, const std::string& build_secret,
                     const std::string& license_key);

std::vector<uint8_t> DeriveAppKey(const std::string& build_secret,
                                  const std::string& app_public_id);

std::string BuildAppTag(const std::string& build_secret, const std::string& app_public_id);

/** AUTH nonce imzasi (send_auth icin). */
std::string BuildAuthNonceSig(const std::string& license_key_upper,
                              const std::string& app_id, const std::string& nonce,
                              const std::string& hwid_hash, int64_t ts_ms);

}  // namespace expectional_lic_vm
