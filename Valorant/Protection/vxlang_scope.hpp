#pragma once



/**

 * EX_VL_PROTECT_* — oyun/overlay/bellek mantığı: yalnız obfuscation (performans + sürücü/kernel riski yok).
 * Her fonksiyonda kendi BEGIN/END (render sarmalayıcı degil). Ayni cagrida ic ice BEGIN yok:
 * espLoop korunur, TriggerBot::Run icinde ikinci BEGIN yok (espLoop icinden cagrilir).

 * EX_VL_SAFE_*     — başlangıç anchor (vxlang_hooks): obfuscation-only.

 * EX_VL_AUTH_VM_* — login kripto (lic_auth_vm.cpp): license.dat, HKDF, HMAC, app tag.
 * ImGui/WS/thread bu marker icinde degil; tek BEGIN/END per noinline fonksiyon.

 * EX_VL_MAIN_VM_* — KULLANMA (driver, D3D11 overlay, thread).

 * EX_VL_PROTECT_* — espLoop (oyun). Menu ImGui: marker yok.

 */

#include <vxlib.h>

#if defined(USE_VL_MACRO)
#pragma optimize("", off)
#pragma inline_depth(0)
#endif

#if defined(USE_VL_MACRO)

#define EX_VL_PROTECT_BEGIN        VL_OBFUSCATION_BEGIN

#define EX_VL_PROTECT_END          VL_OBFUSCATION_END

#define EX_VL_SAFE_BEGIN           VL_OBFUSCATION_BEGIN

#define EX_VL_SAFE_END             VL_OBFUSCATION_END

#define EX_VL_AUTH_VM_BEGIN        VL_VIRTUALIZATION_BEGIN

#define EX_VL_AUTH_VM_END          VL_VIRTUALIZATION_END

#define EX_VL_MAIN_VM_BEGIN        VL_VIRTUALIZATION_BEGIN

#define EX_VL_MAIN_VM_END          VL_VIRTUALIZATION_END

#else

#define EX_VL_PROTECT_BEGIN

#define EX_VL_PROTECT_END

#define EX_VL_SAFE_BEGIN

#define EX_VL_SAFE_END

#define EX_VL_AUTH_VM_BEGIN

#define EX_VL_AUTH_VM_END

#define EX_VL_MAIN_VM_BEGIN

#define EX_VL_MAIN_VM_END

#endif



namespace expectional_vxlang {



struct VirtObfScope {

#ifdef USE_VL_MACRO

	__declspec(noinline) VirtObfScope()

	{

		EX_VL_PROTECT_BEGIN;

	}

	__declspec(noinline) ~VirtObfScope()

	{

		EX_VL_PROTECT_END;

	}

	VirtObfScope(const VirtObfScope&) = delete;

	VirtObfScope& operator=(const VirtObfScope&) = delete;

#else

	VirtObfScope() = default;

#endif

};



} // namespace expectional_vxlang



#define EX_VL_SCOPE_CAT(a, b) a##b

#define EX_VL_SCOPE_CAT2(a, b) EX_VL_SCOPE_CAT(a, b)

#define VL_OBF_SCOPE ::expectional_vxlang::VirtObfScope EX_VL_SCOPE_CAT2(_ex_vl_virt_obf_, __LINE__)


#if defined(EXPECTIONAL_LAUNCHER_BUILD)

/* Launcher.exe: baglanti/ws/thread + Obscurion sonrasi VxObfuscationBegin cakisabilir. */

#undef VL_OBF_SCOPE

#define VL_OBF_SCOPE ((void)0)

#endif


