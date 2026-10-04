// Single-translation-unit build of the zstd decompressor.
// All zstd source files are included here so only this .cpp needs to be compiled.
// Do NOT define ZSTD_STATIC_LINKING_ONLY or include zstd.h elsewhere with implementation.

#if defined(_MSC_VER)
#  pragma warning(push, 0)
#endif

// Put zstd headers on a private include path relative to this file.
// zstd sources use #include "zstd_internal.h" etc. (local includes) so they
// find their siblings automatically when the file is compiled in its own dir.

#include "Debug.c"
#include "ErrorPrivate.c"
#include "EntropyCommon.c"
#include "FseDecompress.c"
#include "ZstdCommon.c"
#include "Xxhash.c"
#include "HufDecompress.c"
#include "ZstdDdict.c"
#include "ZstdDecompressBlock.c"
#include "ZstdDecompress.c"

#if defined(_MSC_VER)
#  pragma warning(pop)
#endif
