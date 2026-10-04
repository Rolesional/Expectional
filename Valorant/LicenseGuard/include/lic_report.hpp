#pragma once
#include <string>
#include <vector>

namespace lic {

/// tcp:// veya ws(s):// URL'den HTTP API tabanı (tcp üretimde https://host, yerelde http://host:3000).
std::string DeriveApiBaseFromLicenseUrl(const std::string& connection_url);

/// Sunucuya güvenlik raporu: HKDF+AES-GCM gövde ({v,n,c}) + X-License-Report-Sig + X-App-Tag (wire ile aynı BuildAppTag).
void SendLicenseSecurityReport(const std::string& api_base_url,
                               const std::string& app_public_id,
                               const std::string& build_secret,
                               const std::string& event,
                               const std::string& machine_name,
                               const std::vector<std::string>& tools,
                               const std::string& license_key_prefix);

}  // namespace lic
