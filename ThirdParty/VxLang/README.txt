VxLang integration (official SDK layout; mirrors vxlang-page deploy + full 2.3.5.4 C SDK)
========================================================================================

Synced from full Windows C SDK (example paths):
  SDK\C\include — vxlib.h, kvxlib.h (kernel stubs), vxextsmm.h (ExtsMM: VLX_MM_*, VLX_OBFUSCATION_*),
                  axion_type.h (Axion callback / detection IDs)
  SDK\C\lib     — vxlib64.lib, vxlib64.dll, vxsyslib*.lib (and x86 vxlib32.* if needed)

GitHub deploy mirror often ships only vxlib.h (+ kvxlib.h). Full package adds VxLinkExtsBegin/End in vxlib.h
and the extra headers above; runtime vxlib64.dll/.lib sizes differ slightly vs older mirrors.

References:
  - Documentation hub: https://vxlang.github.io/
  - SDK / examples / precautions: https://github.com/vxlang/vxlang-page
  - SDK Tutorial (optimizations, SEH, switch-case): src/Example/01 on GitHub

What is in this folder:
  include/vxlib.h      — VL_* macros; declares VxLinkEvent + VxLinkExtsBegin/End when USE_VL_MACRO
  include/vxextsmm.h   — VLX_OBFUSCATION_* / VLX_MM_* (obfuscation + external extensions MM)
  include/axion_type.h — Axion detection types / event IDs (optional; include where you wire callbacks)
  include/kvxlib.h     — kernel-mode VL_* (driver projects only)
  lib/vxlib64.lib      — MSVC import library (x64 user-mode EXE)
  lib/vxlib64.dll      — runtime stub; copy next to Expectional.exe when markers are linked

Per-translation-unit markers:
  .cpp files include Protection/vxlang_per_tu.hpp for macros. Do not register static global objects that call
  TuObfAnchor before main() — Obscurion-protected EXEs often exit silently. Anchors run from
  ExpectionalVxLang_RuntimeAnchor() in vxlang_hooks.cpp after main() prints startup.

Build this project:
  1) Release | x64: Valorant.vcxproj has EnableVxLang=true — USE_VL_MACRO + vxlib64.lib; vxlib64.dll copied to OutDir.
  2) To build without SDK markers (plain exe): set EnableVxLang to false in Valorant.vcxproj (or override MSBuild property).

Post-build protection (required for real obfuscation/virtualization):
  - Use the VxLang GUI tool (Obscurion / protector from your VxLang package; demo or full per vendor).
  - Tutorial flow (Example 01): build your EXE, open it in the tool, optionally check disable-core for tests,
    then Compile / protect per product docs.
  - CLI options mentioned in README include --disable-core, --use-multi-vm, --use-signature.

Precautions (from VxLang tutorial / Example/01):
  - Aggressive MSVC optimization + LTCG/WPO can merge or deduplicate marker sequences — protector verifier
    then reports orphan VxObfuscationBegin/End. This project with EnableVxLang: WholeProgramOptimization off,
    linker COMDAT folding off + /OPT:NOICF, #pragma optimize("", off) via vxlang_scope.hpp when USE_VL_MACRO,
    and TuObfAnchor/__declspec(noinline) in vxlang_hooks.cpp.
  - VL_VIRTUALIZATION_* / EX_VL_MAIN_VM_*: one Begin + one End per function — use do{}while(0) + break so the
    verifier does not see orphan VxVirtualizationEnd sites.
  - EX_VL_MAIN_VM_* wraps main(); EX_VL_AUTH_VM_* wraps LicenseGuard try_connect + license.dat read/write +
    lic_login_ui ShowLoginDialog (nested with main — verify in Obscurion if warnings appear).
  - EX_VL_PROTECT_* = VL_OBFUSCATION_* (gameplay/overlay e.g. render). Startup: EX_VL_SAFE_* + __try/__except in vxlang_hooks.cpp.
  - Avoid problematic switch/jump tables inside VM/obfuscation regions unless you follow their guidance.
  - SEH vs C++ exceptions: follow their cxxeh sample if you use try/catch inside marked regions.

Full / paid tooling:
  - See https://vxlang.github.io/pages/purchase/ and Patreon links on vxlang-page README.
