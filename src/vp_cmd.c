/* Console command "N,VP": run a stock header parser on a file of the USB
   stick, with the header stream set up as FUN_041b046c sets it, and print
   the track info T and a CRC-32 of the data region.
     N = 1: C:/T16.FLA  2: C:/T24.FLA   FLAC through the hooks, parsed as WAV
     N = 3: C:/T16.WAV  4: C:/T24.WAV   stock WAV
     N = 5: C:/T16.AIF                  stock AIFF
     N = 6: C:/T16.MP3                  stock MP3
   A FLAC file and a WAV file with the same audio must give the same line.
   It runs only when someone types it, never at boot. */
#include <stdint.h>
#include <string.h>
#include "crc32.h"
#include "fw.h"
#include "putfmt.h"
#include "vfs_hook.h"

#define CTX_SIZE    0x600
#define T_SIZE      0x200
/* The header buffer of the real parser context 0x0483543c: pointer at
   ctx+0x538, size at ctx+0x53c. FUN_041b046c copies both into P[7] and P[8].
   VP uses the same buffer, so run VP only when no track is loading. */
#define FW_HDR_BUF_PTR  ((volatile uint32_t *)0x04835974)

typedef int32_t (*parse3_fn)(uint32_t *p, void *info, uint32_t *t);
typedef int32_t (*parse2_fn)(uint32_t *p, uint32_t *t);

static const uint16_t names[6][11] = {
    { 'C', ':', '/', 'T', '1', '6', '.', 'F', 'L', 'A', 0 },
    { 'C', ':', '/', 'T', '2', '4', '.', 'F', 'L', 'A', 0 },
    { 'C', ':', '/', 'T', '1', '6', '.', 'W', 'A', 'V', 0 },
    { 'C', ':', '/', 'T', '2', '4', '.', 'W', 'A', 'V', 0 },
    { 'C', ':', '/', 'T', '1', '6', '.', 'A', 'I', 'F', 0 },
    { 'C', ':', '/', 'T', '1', '6', '.', 'M', 'P', '3', 0 },
};
static const uint32_t types[6] = { 11, 11, 11, 11, 12, 1 };

static uint32_t ctx[CTX_SIZE / 4];
static uint32_t track[T_SIZE / 4];
static uint8_t data_buf[4096];

static uint32_t data_crc(uint32_t fh, uint32_t start, uint32_t len, int *err)
{
    uint32_t crc = crc32_begin();
    *err = 0;
    if (hook_fseek(fh, (int32_t)start, 0, 0) != 0) {
        *err = 1;
        return 0;
    }
    while (len > 0) {
        uint32_t n = len < sizeof data_buf ? len : sizeof data_buf;
        uint32_t got = hook_fread(data_buf, 1, n, fh, 0);
        crc = crc32_update(crc, data_buf, got);
        if (got != n) {
            *err = 2;
            break;
        }
        len -= n;
    }
    return crc32_end(crc);
}

void cmd_vp(void)
{
    uint32_t n = fw_args[0];
    char line[200];
    char *p;
    uint8_t *t8 = (uint8_t *)track;

    if (n < 1 || n > 6) {
        fw_puts("VP N must be 1 to 6\r\n");
        return;
    }
    uint32_t fh = fw_fopen(names[n - 1], "r", 0);
    if (fh == 0) {
        fw_puts("VP fopen failed\r\n");
        return;
    }
    p = put_str(line, "VP");
    if (n <= 2) {
        p = put_str(p, " reg=");
        p = put_int(p, vh_register(fh, 0));
    }
    memset(ctx, 0, sizeof ctx);
    memset(track, 0, sizeof track);
    uint32_t *hs = ctx + 0x54 / 4;      /* header stream P */
    hs[1] = fh;
    hs[2] = 0x110003;                   /* playback opts, as at 0xa409fd14 */
    hs[3] = 0x1b58;
    hs[7] = FW_HDR_BUF_PTR[0];
    hs[8] = FW_HDR_BUF_PTR[1];
    track[2] = types[n - 1];
    int32_t r;
    if (types[n - 1] == 1)
        r = ((parse2_fn)FW_PARSE_MP3)(hs, track);
    else if (types[n - 1] == 12)
        r = ((parse3_fn)FW_PARSE_AIFF)(hs, ctx + 0xec / 4, track);
    else
        r = ((parse3_fn)FW_PARSE_WAV)(hs, ctx + 0xec / 4, track);
    p = put_str(p, " r=");
    p = put_int(p, r);
    p = put_str(p, " mode=");
    p = put_dec(p, track[0x0c / 4]);
    p = put_str(p, " dur=");
    p = put_dec(p, track[0x10 / 4]);
    p = put_str(p, " start=");
    p = put_dec(p, track[0x14 / 4]);
    p = put_str(p, " len=");
    p = put_dec(p, track[0x18 / 4]);
    p = put_str(p, " tail=");
    p = put_dec(p, track[0x1c / 4]);
    p = put_str(p, " unit=");
    p = put_dec(p, track[0x12c / 4]);
    p = put_str(p, " fmt=");
    p = put_hex(p, (uint32_t)t8[0x130] << 24 | (uint32_t)t8[0x131] << 16
                   | (uint32_t)t8[0x132] << 8 | t8[0x133]);
    if (r == 0) {
        int e;
        uint32_t crc = data_crc(fh, track[0x14 / 4], track[0x18 / 4], &e);
        p = put_str(p, " data=");
        p = put_hex(p, crc);
        p = put_str(p, "/");
        p = put_int(p, e);
    }
    p = put_str(p, "\r\n");
    *p = 0;
    fw_puts(line);
    hook_fclose(fh, 0);
}
