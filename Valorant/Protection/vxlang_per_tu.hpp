#pragma once

/**
 * SDK macros + vxextsmm. TuObfAnchor lives in vxlang_hooks.cpp.
 *
 * Do not use static global registrators that call Vx* during CRT init — Obscurion-protected builds
 * often exit silently when dozens of translation units run markers before main().
 */
#include "vxlang_scope.hpp"
#include "vxextsmm.h"

namespace expectional_vxlang_tu {

void TuObfAnchor(unsigned salt);

} // namespace expectional_vxlang_tu
