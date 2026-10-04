#include "../include/lic_hwid.hpp"
#include "../include/lic_crypto.hpp"
#include "../include/lic_json.hpp"

#define _WIN32_DCOM
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wbemidl.h>
#include <comdef.h>
#include <iphlpapi.h>

#include <cctype>
#include <cstdlib>
#include <iterator>
#include <sstream>
#include <vector>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "iphlpapi.lib")
#include "../include/lic_guard_vxlang.hpp"

namespace lic {
namespace {

struct ComInit {
  HRESULT hr;
  ComInit() : hr(CoInitializeEx(0, COINIT_MULTITHREADED)) {}
  ~ComInit() {
    if (SUCCEEDED(hr) || hr == S_FALSE || hr == RPC_E_CHANGED_MODE) CoUninitialize();
  }
};

template <class T>
struct ComPtr {
  T* p = nullptr;
  ~ComPtr() { if (p) p->Release(); }
  T** out() { return &p; }
  T* operator->() const { return p; }
  explicit operator bool() const { return p != nullptr; }
};

static std::string trim_wmi_string(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
    s.pop_back();
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
  return s.substr(i);
}

static std::string utf8_from_wide(const wchar_t* w) {
  if (!w || !*w) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1) return "";
  std::string out(static_cast<size_t>(n - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
  return out;
}

static bool str_eq_ci(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const unsigned char ca = static_cast<unsigned char>(a[i]);
    const unsigned char cb = static_cast<unsigned char>(b[i]);
    if (std::tolower(ca) != std::tolower(cb)) return false;
  }
  return true;
}

/// Aynı WMI sorgusunun tüm satırları (ör. birden fazla GPU).
std::vector<std::string> WmiAllStrings(const wchar_t* className, const wchar_t* propName) {
  ComInit ci;
  std::vector<std::string> out;
  if (FAILED(ci.hr) && ci.hr != RPC_E_CHANGED_MODE) return out;

  CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
                       RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);

  ComPtr<IWbemLocator> loc;
  if (FAILED(CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                              IID_IWbemLocator, reinterpret_cast<void**>(loc.out()))))
    return out;

  ComPtr<IWbemServices> svc;
  if (FAILED(loc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, 0, 0,
                                0, 0, svc.out())))
    return out;

  CoSetProxyBlanket(svc.p, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                    RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                    nullptr, EOAC_NONE);

  std::wstring query = L"SELECT ";
  query += propName;
  query += L" FROM ";
  query += className;

  ComPtr<IEnumWbemClassObject> e;
  if (FAILED(svc->ExecQuery(bstr_t(L"WQL"), bstr_t(query.c_str()),
                            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                            nullptr, e.out())))
    return out;

  IWbemClassObject* obj = nullptr;
  ULONG ret = 0;
  while (e.p && SUCCEEDED(e->Next(WBEM_INFINITE, 1, &obj, &ret)) && ret) {
    VARIANT v;
    VariantInit(&v);
    std::string one;
    if (SUCCEEDED(obj->Get(propName, 0, &v, nullptr, nullptr))) {
      if (v.vt == VT_BSTR && v.bstrVal) {
        char buf[1024];
        const int n = WideCharToMultiByte(CP_UTF8, 0, v.bstrVal, -1, buf,
                                          sizeof(buf), nullptr, nullptr);
        if (n > 0) one.assign(buf, static_cast<size_t>(n - 1));
      }
      VariantClear(&v);
    }
    obj->Release();
    one = trim_wmi_string(std::move(one));
    if (one.empty()) continue;
    bool dup = false;
    for (const auto& ex : out) {
      if (str_eq_ci(ex, one)) {
        dup = true;
        break;
      }
    }
    if (!dup) out.push_back(std::move(one));
  }
  return out;
}

static std::string json_array_of_strings_utf8(const std::vector<std::string>& items) {
  if (items.empty()) return "[]";
  std::ostringstream os;
  os << '[';
  for (size_t i = 0; i < items.size(); ++i) {
    if (i) os << ',';
    lic_json::escape_to(os, items[i]);
  }
  os << ']';
  return os.str();
}

std::string WmiString(const wchar_t* className, const wchar_t* propName) {
  ComInit ci;
  if (FAILED(ci.hr) && ci.hr != RPC_E_CHANGED_MODE) return "";

  // CoInitializeSecurity may already have been called; ignore RPC_E_TOO_LATE.
  CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
                       RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);

  ComPtr<IWbemLocator> loc;
  if (FAILED(CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                              IID_IWbemLocator, reinterpret_cast<void**>(loc.out()))))
    return "";

  ComPtr<IWbemServices> svc;
  if (FAILED(loc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, 0, 0,
                                0, 0, svc.out())))
    return "";

  CoSetProxyBlanket(svc.p, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                    RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                    nullptr, EOAC_NONE);

  std::wstring query = L"SELECT ";
  query += propName;
  query += L" FROM ";
  query += className;

  ComPtr<IEnumWbemClassObject> e;
  if (FAILED(svc->ExecQuery(bstr_t(L"WQL"), bstr_t(query.c_str()),
                            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                            nullptr, e.out())))
    return "";

  IWbemClassObject* obj = nullptr;
  ULONG ret = 0;
  std::string result;
  while (e.p && SUCCEEDED(e->Next(WBEM_INFINITE, 1, &obj, &ret)) && ret) {
    VARIANT v;
    VariantInit(&v);
    if (SUCCEEDED(obj->Get(propName, 0, &v, nullptr, nullptr))) {
      if (v.vt == VT_BSTR && v.bstrVal) {
        char buf[1024];
        const int n = WideCharToMultiByte(CP_UTF8, 0, v.bstrVal, -1, buf,
                                          sizeof(buf), nullptr, nullptr);
        if (n > 0) result.assign(buf, static_cast<size_t>(n - 1));
      }
      VariantClear(&v);
    }
    obj->Release();
    if (!result.empty()) break;
  }
  return result;
}

std::string MacString() {
  IP_ADAPTER_INFO info[16];
  DWORD sz = sizeof(info);
  if (GetAdaptersInfo(info, &sz) != ERROR_SUCCESS) return "";
  for (PIP_ADAPTER_INFO p = info; p; p = p->Next) {
    if (p->Type != MIB_IF_TYPE_ETHERNET && p->Type != IF_TYPE_IEEE80211) continue;
    if (p->AddressLength != 6) continue;
    char buf[24];
    snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X",
             p->Address[0], p->Address[1], p->Address[2],
             p->Address[3], p->Address[4], p->Address[5]);
    return buf;
  }
  return "";
}

}  // namespace

LIC_VL_NOINLINE
HwidResult ComputeHwid() {
  LIC_VL_OBF_OPEN;
  std::string cpu = WmiString(L"Win32_Processor", L"ProcessorId");
  std::string disk = WmiString(L"Win32_DiskDrive", L"SerialNumber");
  std::string mb = WmiString(L"Win32_BaseBoard", L"SerialNumber");
  std::string guid = WmiString(L"Win32_ComputerSystemProduct", L"UUID");
  if (guid.empty()) guid = MacString();

  std::ostringstream blob;
  blob << "v1|" << cpu << "|" << disk << "|" << mb << "|" << guid;

  HwidResult r{};
  r.id = Sha256Hex(blob.str());

  auto shortHash = [](const std::string& s) -> int {
    if (s.empty()) return 0;
    const std::string h = Sha256Hex(s);
    try { return static_cast<int>(std::stoul(h.substr(0, 7), nullptr, 16)); }
    catch (...) { return 0; }
  };
  r.scores["cpu"] = shortHash(cpu);
  r.scores["disk"] = shortHash(disk);
  r.scores["mb"] = shortHash(mb);
  r.scores["guid"] = shortHash(guid);
  LIC_VL_OBF_CLOSE;
  return r;
}

std::string GetDisplayMachineNameUtf8() {
  wchar_t buf[512];
  DWORD sz = static_cast<DWORD>(std::size(buf));
  if (GetComputerNameExW(ComputerNamePhysicalDnsFullyQualified, buf, &sz)) {
    std::string u8 = utf8_from_wide(buf);
    u8 = trim_wmi_string(std::move(u8));
    if (!u8.empty()) return u8.substr(0, 256);
  }
  sz = static_cast<DWORD>(std::size(buf));
  if (GetComputerNameExW(ComputerNamePhysicalDnsHostname, buf, &sz)) {
    std::string u8 = utf8_from_wide(buf);
    u8 = trim_wmi_string(std::move(u8));
    if (!u8.empty()) return u8.substr(0, 256);
  }
  wchar_t nb[256];
  DWORD n = static_cast<DWORD>(std::size(nb));
  if (GetComputerNameW(nb, &n) && n > 0) {
    std::string u8 = utf8_from_wide(nb);
    u8 = trim_wmi_string(std::move(u8));
    if (!u8.empty()) return u8.substr(0, 256);
  }
  char envn[256];
  if (GetEnvironmentVariableA("COMPUTERNAME", envn, sizeof(envn)) > 0 && envn[0] != '\0')
    return std::string(envn);
  std::string wmiDns = trim_wmi_string(WmiString(L"Win32_ComputerSystem", L"DNSHostName"));
  if (!wmiDns.empty()) return wmiDns.substr(0, 256);
  std::string wmiName = trim_wmi_string(WmiString(L"Win32_ComputerSystem", L"Name"));
  if (!wmiName.empty()) return wmiName.substr(0, 256);
  return "unknown-pc";
}

std::string BuildPcSpecsJsonUtf8() {
  MEMORYSTATUSEX ms{};
  ms.dwLength = sizeof(ms);
  uint64_t ram_mb = 0;
  if (GlobalMemoryStatusEx(&ms)) ram_mb = ms.ullTotalPhys / (1024ULL * 1024ULL);

  lic_json::Builder b;
  b.add_uint64("ram_total_mb", static_cast<uint64_t>(ram_mb));

  auto trunc = [](std::string s, size_t max) {
    if (s.size() > max) s.resize(max);
    return s;
  };

  try {
    std::string dns = trim_wmi_string(WmiString(L"Win32_ComputerSystem", L"DNSHostName"));
    std::string cs_name = trim_wmi_string(WmiString(L"Win32_ComputerSystem", L"Name"));
    std::string manufacturer = trim_wmi_string(WmiString(L"Win32_ComputerSystem", L"Manufacturer"));
    std::string model = trim_wmi_string(WmiString(L"Win32_ComputerSystem", L"Model"));
    std::string cpu = trim_wmi_string(WmiString(L"Win32_Processor", L"Name"));
    std::string os = trim_wmi_string(WmiString(L"Win32_OperatingSystem", L"Caption"));
    std::string arch = trim_wmi_string(WmiString(L"Win32_OperatingSystem", L"OSArchitecture"));
    std::vector<std::string> gpus = WmiAllStrings(L"Win32_VideoController", L"Name");

    if (!dns.empty()) b.add_str("dns_hostname", trunc(std::move(dns), 240));
    if (!cs_name.empty()) b.add_str("computer_system_name", trunc(std::move(cs_name), 120));
    if (!manufacturer.empty()) b.add_str("manufacturer", trunc(std::move(manufacturer), 120));
    if (!model.empty()) b.add_str("model", trunc(std::move(model), 120));
    if (!cpu.empty()) b.add_str("cpu", trunc(std::move(cpu), 240));
    if (!os.empty()) b.add_str("os", trunc(std::move(os), 240));
    if (!arch.empty()) b.add_str("os_arch", trunc(std::move(arch), 64));
    if (!gpus.empty()) {
      b.add_raw("gpus", json_array_of_strings_utf8(gpus));
      std::ostringstream gpu_join;
      for (size_t i = 0; i < gpus.size(); ++i) {
        if (i) gpu_join << " | ";
        gpu_join << gpus[i];
      }
      std::string gpu_s = gpu_join.str();
      b.add_str("gpu", trunc(std::move(gpu_s), 520));
    }
  } catch (...) {
    // WMI/COM — RAM alanı yine de gider
  }

  return b.done();
}

}  // namespace lic
