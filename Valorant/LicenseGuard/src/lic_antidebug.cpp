#include "../include/lic_antidebug.hpp"
#include "../include/lic_crypto.hpp"

#include <algorithm>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <winternl.h>

#pragma comment(lib, "psapi.lib")
#include "../include/lic_guard_vxlang.hpp"

namespace lic {
namespace {

bool IsBeingDebugged_PEB() {
#if defined(_M_X64) || defined(__x86_64__)
  PPEB peb = reinterpret_cast<PPEB>(__readgsqword(0x60));
#else
  PPEB peb = reinterpret_cast<PPEB>(__readfsdword(0x30));
#endif
  return peb && peb->BeingDebugged;
}

bool NtGlobalFlag_HeapCheck() {
#if defined(_M_X64) || defined(__x86_64__)
  PPEB peb = reinterpret_cast<PPEB>(__readgsqword(0x60));
  if (!peb) return false;
  const ULONG flags = *reinterpret_cast<PULONG>(reinterpret_cast<PUCHAR>(peb) + 0xBC);
  return (flags & 0x70) != 0;
#else
  PPEB peb = reinterpret_cast<PPEB>(__readfsdword(0x30));
  if (!peb) return false;
  const ULONG flags = *reinterpret_cast<PULONG>(reinterpret_cast<PUCHAR>(peb) + 0x68);
  return (flags & 0x70) != 0;
#endif
}

bool TimingDetour() {
  LARGE_INTEGER f, a, b;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&a);
  for (volatile int i = 0; i < 1000; ++i) _mm_pause();
  QueryPerformanceCounter(&b);
  const double dtMs = (b.QuadPart - a.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart);
  return dtMs > 100.0;
}

bool ProcessAttached() {
  BOOL d = FALSE;
  if (CheckRemoteDebuggerPresent(GetCurrentProcess(), &d) && d) return true;
  return IsDebuggerPresent();
}

bool VmHints_CPUID() {
  int cpuInfo[4];
  __cpuid(cpuInfo, 1);
  return (cpuInfo[2] & (1 << 31)) != 0;  // hypervisor present
}

static std::string exe_w_to_utf8(const wchar_t* w) {
  if (!w || !*w) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1) return "";
  std::string s(static_cast<size_t>(n - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}

bool match_crack_exe(const wchar_t* exe) {
  static const wchar_t* kBad[] = {
      L"dnspy.exe",
      L"dnspy-x86.exe",
      L"dnspy.console.exe",
      L"x64dbg.exe",
      L"x32dbg.exe",
      L"ollydbg.exe",
      L"ida64.exe",
      L"ida.exe",
      L"idaq.exe",
      L"idaq64.exe",
      L"windbg.exe",
      L"cheatengine-x86_64.exe",
      L"cheatengine-i386.exe",
      L"processhacker.exe",
      L"httpdebuggerui.exe",
      L"fiddler.exe",
      L"wireshark.exe",
      L"scylla.exe",
      L"x96dbg.exe",
      L"ilspy.exe",
      L"de4dot.exe",
  };
  for (const wchar_t* n : kBad) {
    if (_wcsicmp(exe, n) == 0) return true;
  }
  return false;
}

}  // namespace

std::vector<std::string> ListDetectedCrackProcesses() {
  std::vector<std::string> out;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return out;
  PROCESSENTRY32W pe;
  pe.dwSize = sizeof(pe);
  if (Process32FirstW(snap, &pe)) {
    do {
      if (match_crack_exe(pe.szExeFile)) {
        std::string u8 = exe_w_to_utf8(pe.szExeFile);
        if (!u8.empty()) out.push_back(std::move(u8));
      }
    } while (Process32NextW(snap, &pe));
  }
  CloseHandle(snap);
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

uint64_t ComputeThreatScore() {
  LIC_VL_OBF_OPEN;
  uint64_t s = 0;
  if (IsBeingDebugged_PEB())   s |= 1ULL << 0;
  if (NtGlobalFlag_HeapCheck()) s |= 1ULL << 1;
#if !defined(EXPECTIONAL_LAUNCHER_BUILD)
  if (TimingDetour())           s |= 1ULL << 2;
  if (VmHints_CPUID())          s |= 1ULL << 4;
#endif
  if (ProcessAttached())        s |= 1ULL << 3;
  if (!ListDetectedCrackProcesses().empty())        s |= 1ULL << 5;
  LIC_VL_OBF_CLOSE;
  return s;
}

LIC_VL_NOINLINE
std::string SelfCodeSha256() {
  std::string digest;
  LIC_VL_OBF_OPEN;
  do {
    HMODULE h = GetModuleHandleW(nullptr);
    if (!h) break;
    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), h, &mi, sizeof(mi))) break;
    const auto* base = reinterpret_cast<const uint8_t*>(mi.lpBaseOfDll);
    const size_t toRead = (mi.SizeOfImage < 0x100000) ? mi.SizeOfImage : 0x100000;
    digest = Sha256Hex(base, toRead);
  } while (0);
  LIC_VL_OBF_CLOSE;
  return digest;
}

}  // namespace lic
