/* Addresses in the stock CDJ-900 MAIN 4.32 application (see NOTES.md). */
#pragma once
#include <stdint.h>

/* Console: print one NUL-terminated string. */
#define fw_puts ((void (*)(const char *))0x041026b4)

/* Console: parsed arguments of the current command. */
#define fw_args ((volatile uint32_t *)0x045a01a8)

/* Stock functions that the FLAC hooks wrap or call (see NOTES.md, "File and
   stream layer" and "FLAC hooks"). */
#define FW_TRACK_OPEN   0x042e8ea8      /* (a, 8-byte track ID on the stack) -> handle */
#define FW_FOPEN        0x042e55a4      /* (wchar *path, char *mode, opts) -> handle or 0 */
#define FW_FCLOSE       0x042e59de      /* (fh, opts) -> 0 or -1 */
#define FW_FREAD        0x042e5c1a      /* (buf, size, n, fh, opts) -> items */
#define FW_FSEEK        0x042e5e1e      /* (fh, off, whence, opts) -> 0 or not 0 */
#define FW_FTELL        0x042e5ea8      /* (fh, opts) -> position or -1 */
#define FW_FEOF         0x042e5f3a      /* (fh, opts) -> 1, 0 or -1 */
#define FW_FILELEN      0x042e65a4      /* (fh, uint *len, opts) -> 0 or not 0 */
#define FW_MSG_SEND     0x042f01e8      /* (queue, msg, len, timeout) */
#define FW_SET_ERRNO    0x041f26d0
#define FW_GET_ERRNO    0x041f26ca
#define FW_GET_TIM      0x042f1a40      /* NORTi get_tim(SYSTIM *): u16 high, then u32 low at +4, ms */
#define FW_FWRITE       0x042e5d4c      /* (buf, size, n, fh, opts) -> items; same checks as fread */

#define fw_fopen     ((uint32_t (*)(const uint16_t *, const char *, void *))FW_FOPEN)
#define fw_get_errno ((int32_t (*)(void))FW_GET_ERRNO)

/* Header parser branches of the dispatch FUN_041b1b40 (checked in the
   disassembly at 0x041b1b5e..0x041b1bd4). P is the header stream at ctx+0x54,
   info is ctx+0xec and T is the track info. Result: 0, -2 or -3. */
#define FW_PARSE_WAV    0x041b1c7c      /* (P, info, T) */
#define FW_PARSE_AIFF   0x041b1de8      /* (P, info, T) */
#define FW_PARSE_MP3    0x041b1f2c      /* (P, T) */

/* NORTi dly_tsk(ticks): sleep. dly_tsk(0) returns at once without a yield. */
#define FW_DLY_TSK      0x042f1a84
#define fw_dly_tsk   ((int32_t (*)(int32_t))FW_DLY_TSK)
#define fw_get_tim   ((void (*)(uint32_t *))FW_GET_TIM)

/* Read-ahead policy (see NOTES.md, "Read-ahead policy"). FUN_041ac360(Mng
   ctx) returns 0 idle, 1 fill forward (40 units), 2 fill backward, 3 other.
   The DSP status block: [1] units behind the play position, [2] units ahead.
   One unit is T+0x12c bytes (1/75 s). The PCM file context 0x04835984 holds
   the stream object pointer at +0x30; the stream object [0] is the handle. */
#define FW_FILL_POLICY  0x041ac360
#define FW_DSP_STATUS   0xac0c7cc8
#define FW_STREAM_PTR   0x048359b4
