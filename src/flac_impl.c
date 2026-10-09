/* dr_flac, built once for both the CDJ-900 target and the host test. */
#ifdef CDJ_TARGET
#include <stddef.h>
#include <string.h>
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_WCHAR
#define DRFLAC_ASSERT(x)            ((void)0)
#define DRFLAC_MALLOC(n)            ((void *)0) /* every call passes callbacks */
#define DRFLAC_REALLOC(p, n)        ((void *)0)
#define DRFLAC_FREE(p)              ((void)0)
#define DRFLAC_COPY_MEMORY(d, s, n) memcpy((d), (s), (n))
#define DRFLAC_ZERO_MEMORY(p, n)    memset((p), 0, (n))
#endif
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_SIMD
/* Keep the CRC code: dr_flac's binary search seek needs it. Without it, a
   backward seek restarts at the SEEKTABLE point before the target (often 10 s
   earlier) and reads forward, and reverse play on the deck stutters. */
#include "../third_party/dr_flac.h"
