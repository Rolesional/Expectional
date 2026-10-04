#pragma once
//
// LicenseGuard VxLang — SDK kurallari (vxlang_scope.hpp + ThirdParty/VxLang/README.txt):
//   - Her fonksiyonda tek BEGIN/END cifti; ic ice marker yok.
//   - VL_VIRTUALIZATION (AUTH_VM): login/kripto; do-while(0)+break ile orphan End yok.
//   - VL_OBFUSCATION (PROTECT): HWID/antidebug/HTTP send; tek END sitesi — do-while(0)+break
//     (erken return/onceden CLOSE yok; verifier orphan VxObfuscationEnd uyarisi verir).
//   - Static init ile TuObfAnchor cagirma — CRT oncesi crash riski.
//

#include "Protection/vxlang_scope.hpp"
#include "Protection/vxlang_per_tu.hpp"

namespace lic {

inline void LicGuardTuAnchor(unsigned salt) {
#if defined(USE_VL_MACRO)
  expectional_vxlang_tu::TuObfAnchor(salt);
#else
  (void)salt;
#endif
}

}  // namespace lic

#if defined(USE_VL_MACRO) && !defined(EXPECTIONAL_LAUNCHER_BUILD)
#define LIC_VL_NOINLINE __declspec(noinline)
#define LIC_VL_AUTH_VM_OPEN  EX_VL_AUTH_VM_BEGIN
#define LIC_VL_AUTH_VM_CLOSE EX_VL_AUTH_VM_END
#define LIC_VL_OBF_OPEN      EX_VL_PROTECT_BEGIN
#define LIC_VL_OBF_CLOSE     EX_VL_PROTECT_END
#elif defined(USE_VL_MACRO)
#define LIC_VL_NOINLINE __declspec(noinline)
#define LIC_VL_AUTH_VM_OPEN  ((void)0)
#define LIC_VL_AUTH_VM_CLOSE ((void)0)
#define LIC_VL_OBF_OPEN      ((void)0)
#define LIC_VL_OBF_CLOSE     ((void)0)
#else
#define LIC_VL_NOINLINE
#define LIC_VL_AUTH_VM_OPEN
#define LIC_VL_AUTH_VM_CLOSE
#define LIC_VL_OBF_OPEN
#define LIC_VL_OBF_CLOSE
#endif
