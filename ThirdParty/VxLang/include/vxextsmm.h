#pragma once

#ifdef USE_VL_MACRO
#include "vxlib.h"

#define VLX_OBFUSCATION_BEGIN               VxObfuscationBegin();\
                                            VxLinkExtsBegin()
#define VLX_OBFUSCATION_END                 VxLinkExtsEnd();\
                                            VxObfuscationEnd()
											
#define VLX_MM_BEGIN                        VxLinkExtsBegin()
#define VLX_MM_END                          VxLinkExtsEnd()											
#else
#define VLX_OBFUSCATION_BEGIN
#define VLX_OBFUSCATION_END

#define VLX_MM_BEGIN
#define VLX_MM_END
#endif


