/* Wrappers for the vfs calls on the playback path (see NOTES.md, "FLAC
   hooks"). The patch points literal-pool words at these functions. A handle
   that hook_open registered is a FLAC file and goes to vwav. Every other
   handle goes to the stock function without change. */
#pragma once
#include <stdint.h>

#define VH_SLOTS        4
#define VH_STACK        (16 * 1024)
#define VH_ERRNO_EOF    (-14)
#define VH_ERRNO_BUSY   (-1)            /* any code but EOF: the caller sees an error */
#define VH_BAD_HANDLE   0x05762a98u     /* FUN_042e8ea8 can return this; it is not a handle */

/* Read-ahead throttle. After a jump, the firmware refills a large read-ahead
   buffer at full speed. For FLAC that is CPU work in a priority-6 task, and
   the other priority-6 tasks (library lists, audio to the DSP) wait. When a
   run of reads has delivered VH_THROTTLE_AFTER_S seconds of audio since the
   last jump, each read is followed by a sleep as long as its decode, but the
   fill never drops below VH_MIN_FILL_RATE times real time. Audio comes first:
   the maximum tempo (+100 %) needs 2 times real time. */
#define VH_THROTTLE_AFTER_S 8
#define VH_MIN_FILL_RATE    3

/* Read-ahead cap for FLAC streams, in units (1/75 s): 1500 = 20 s. The stock
   policy fills up to 25-30 s ahead, and in one branch further (the deck read
   114 s ahead). For WAV that costs USB time only; for FLAC each unit is CPU
   work. Below the cap, every stock rule stays (urgent fill below 1.8 s,
   backward fills, reverse-play aborts). */
#define VH_AHEAD_CAP        1500

/* A task message. On the target, arg is 32 bits, so the message is 8 bytes. */
typedef struct {
    uint32_t code;
    uintptr_t arg;
} vh_msg;

/* The 8-byte track ID of the playback open. The firmware passes it by value
   on the stack (Renesas convention, -mrenesas), not in registers. */
typedef struct {
    uint32_t w0, w1;
} vh_track_id;

/* The stock functions. The PC test supplies fakes. */
typedef struct {
    uint32_t (*open)(uint32_t a, vh_track_id id);                      /* FUN_042e8ea8 */
    int32_t (*fclose)(uint32_t fh, void *opts);
    uint32_t (*fread)(void *buf, uint32_t size, uint32_t n, uint32_t fh, void *opts);
    int32_t (*fseek)(uint32_t fh, int32_t off, int32_t whence, void *opts);
    int32_t (*ftell)(uint32_t fh, void *opts);
    int32_t (*feof)(uint32_t fh, void *opts);
    int32_t (*filelen)(uint32_t fh, uint32_t *len, void *opts);
    int32_t (*send)(uint32_t queue, vh_msg *msg, uint32_t len, int32_t timeout);
    void (*set_errno)(int32_t code);
    int32_t (*get_errno)(void);
    void (*get_tim)(uint32_t t[2]);     /* system time; t[1] = low 32 bits, ms */
    uint32_t (*fopen)(const uint16_t *path, const char *mode, void *opts);
    uint32_t (*fwrite)(const void *buf, uint32_t size, uint32_t n, uint32_t fh, void *opts);
    int32_t (*dly_tsk)(int32_t ms);    /* RTOS sleep; the tick is 1 ms */
    int32_t (*fill_policy)(uint32_t ctx);       /* FUN_041ac360 */
    volatile uint32_t *dsp_status;              /* [1] behind, [2] ahead, in units */
    uint32_t **stream_ptr;                      /* the current PCM stream object */
} vh_stock_t;

extern const vh_stock_t *vh_stock;

/* 1: register FLAC files. 0: every file goes to the stock path (stage-1 deck
   build). tools/build_patch.py writes this word; it is in .data. */
extern volatile uint32_t vh_flac_enabled;

uint32_t hook_open(uint32_t a, vh_track_id id);
int32_t hook_fclose(uint32_t fh, void *opts);
uint32_t hook_fread(void *buf, uint32_t size, uint32_t n, uint32_t fh, void *opts);
int32_t hook_fseek(uint32_t fh, int32_t off, int32_t whence, void *opts);
int32_t hook_ftell(uint32_t fh, void *opts);
int32_t hook_feof(uint32_t fh, void *opts);
int32_t hook_filelen(uint32_t fh, uint32_t *len, void *opts);
int32_t hook_send(uint32_t queue, vh_msg *msg, uint32_t len, int32_t timeout);
int32_t hook_fill_policy(uint32_t ctx);

/* If fh is a FLAC file in scope and a slot is free, register fh and return 0.
   If not, seek fh back to 0 and return the vwav_open result (or -4: no slot). */
int vh_register(uint32_t fh, void *opts);
int vh_is_flac(uint32_t fh);
