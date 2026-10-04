/**
 * VxLang SDK anchor TU (ThirdParty/VxLang). USE_VL_MACRO off → macros no-op, no vxlib link.
 * Startup uses EX_VL_SAFE_* (obfuscation only, no VM) + SEH so vxlib stub issues cannot kill before license/UI.
 */

#include <cstdio>
#include <Windows.h>

#include "Protection/vxlang_per_tu.hpp"

#if defined(USE_VL_MACRO)
__declspec(noinline)
#endif
void expectional_vxlang_tu::TuObfAnchor(unsigned salt)
{
	EX_VL_SAFE_BEGIN;
	volatile unsigned long long x = 1469598103934665603ull ^ static_cast<unsigned long long>(salt);
	x = (x ^ (x >> 33)) * 0xff51afd7ed558ccdull;
	volatile unsigned long long y = 0xC0FFEEull;
	for (int i = 0; i < 5; ++i)
		y = (y * 1315423911ull) ^ (y >> 13);
	(void)x;
	(void)y;
	EX_VL_SAFE_END;
}

#if defined(USE_VL_MACRO)
__declspec(noinline)
#endif
void ExpectionalVxLang_RuntimeAnchor()
{
#if defined(USE_VL_MACRO)
	__try {
#endif
		expectional_vxlang_tu::TuObfAnchor(2654435761u);
#if defined(USE_VL_MACRO)
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		printf("> vxlang: runtime anchor skipped (access violation in vxlib stub).\n");
		fflush(stdout);
	}
#endif
}
