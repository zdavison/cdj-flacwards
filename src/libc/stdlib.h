/* Minimal stdlib.h for freestanding firmware code. dr_flac uses its own
   DRFLAC_MALLOC/REALLOC/FREE macros, so only size_t is needed here. */
#pragma once
#include <stddef.h>
