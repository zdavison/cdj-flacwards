/* Debug statistics for the deck: one record for each firmware read on a
   FLAC handle. hook_fclose writes them to C:/FLSTATn.TXT on the stick.
   Only the debug build turns this on (vh_stats_enabled). */
#pragma once
#include <stdint.h>

#define VS_MAX 1280             /* records kept; older records are dropped */

typedef struct {
    uint32_t t0;                /* start, ms (system time) */
    uint32_t dt;                /* total time of the read, ms */
    uint32_t io;                /* time in the stock fread (USB), ms */
    uint32_t pos;               /* position in the virtual file */
    uint32_t n;                 /* bytes requested */
    uint32_t got;               /* bytes returned */
    uint32_t file_bytes;        /* bytes read from the FLAC file */
    uint16_t seeks;             /* dr_flac seeks */
    uint8_t slot;
    uint8_t sleep;              /* throttle sleep after the read, ms */
    uint32_t caller;            /* return address in the firmware */
    uint16_t behind;            /* DSP buffer behind the play position, units */
    uint16_t ahead;             /* DSP buffer ahead of the play position, units */
} vs_rec;

#ifdef FLAC_STATS
extern volatile uint32_t vh_stats_enabled;
extern uint32_t vs_capped;      /* forward fills that hook_fill_policy stopped */
#define vs_count_capped() (vs_capped++)

void vs_add(const vs_rec *r);
/* Write the records since the last write to C:/FLSTATn.TXT, then clear them.
   Return 0, or -1 if the file could not be written. */
int vs_write(void);
#else
/* Release build: no statistics. The compiler removes the code that uses them. */
#define vh_stats_enabled 0
#define vs_count_capped() ((void)0)
static inline void vs_add(const vs_rec *r) { (void)r; }
static inline int vs_write(void) { return 0; }
#endif
