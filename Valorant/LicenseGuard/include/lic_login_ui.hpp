#pragma once
//
// LicenseGuard manual login UI — D3D9 + ImGui modal window.
//

#include <functional>
#include <string>
#include <utility>

namespace lic {

struct LoginCredentials {
  std::string username;
  std::string password;
};

/// Called when the user clicks Login.
using LoginAttemptFn =
    std::function<std::pair<bool, std::string>(const LoginCredentials& creds)>;

/// Modal login: username + password (Discord /register account).
/// Returns true if authentication succeeded.
bool ShowLoginDialog(const std::string& product_display_name,
                     LoginAttemptFn attempt);

}  // namespace lic
