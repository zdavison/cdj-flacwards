/* The FLAC slot table and the vfs wrappers. See vfs_hook.h. */
#include <stddef.h>
#include "vfs_hook.h"
#include "vh_stats.h"
#include "vwav.h"

#define E_NO_SLOT (-4)

enum { OP_OPEN, OP_READ };

typedef struct {
    uint32_t fh;                /* 0 = free */
    void *opts;                 /* opts of the current call */
    volatile int busy;
    int op;                     /* the call that slot_run makes */
    void *buf;
    uint32_t n;
    uint32_t io_ms;             /* time in the stock fread (statistics) */
    uint32_t next_pos;          /* where the next sequential read starts */
    uint32_t run_bytes;         /* bytes delivered since the last jump */
    vwav v;
    unsigned char mem[VWAV_MEM] __attribute__((aligned(8)));
    unsigned char stack[VH_STACK] __attribute__((aligned(8)));
} vh_slot;

static vh_slot slots[VH_SLOTS];
static uint32_t last_open;      /* handle from the latest hook_open */
volatile uint32_t vh_flac_enabled = 1;

#ifdef CDJ_TARGET
#include "fw.h"
static const vh_stock_t fw_stock = {
    (uint32_t (*)(uint32_t, vh_track_id))FW_TRACK_OPEN,
    (int32_t (*)(uint32_t, void *))FW_FCLOSE,
    (uint32_t (*)(void *, uint32_t, uint32_t, uint32_t, void *))FW_FREAD,
    (int32_t (*)(uint32_t, int32_t, int32_t, void *))FW_FSEEK,
    (int32_t (*)(uint32_t, void *))FW_FTELL,
    (int32_t (*)(uint32_t, void *))FW_FEOF,
    (int32_t (*)(uint32_t, uint32_t *, void *))FW_FILELEN,
    (int32_t (*)(uint32_t, vh_msg *, uint32_t, int32_t))FW_MSG_SEND,
    (void (*)(int32_t))FW_SET_ERRNO,
    (int32_t (*)(void))FW_GET_ERRNO,
    (void (*)(uint32_t *))FW_GET_TIM,
    (uint32_t (*)(const uint16_t *, const char *, void *))FW_FOPEN,
    (uint32_t (*)(const void *, uint32_t, uint32_t, uint32_t, void *))FW_FWRITE,
    (int32_t (*)(int32_t))FW_DLY_TSK,
    (int32_t (*)(uint32_t))FW_FILL_POLICY,
    (volatile uint32_t *)FW_DSP_STATUS,
    (uint32_t **)FW_STREAM_PTR,
};

/* A stream object pointer from the firmware: 4-byte aligned, in SDRAM. */
static int plausible(const uint32_t *p)
{
    uint32_t a = (uint32_t)p & 0x1fffffff;
    return ((uint32_t)p & 3) == 0 && a >= 0x04000000 && a < 0x08000000;
}
const vh_stock_t *vh_stock = &fw_stock;

/* The firmware tasks have stacks of 2 KiB and 3 KiB. dr_flac needs more. */
int32_t call_with_stack(int32_t (*fn)(void *), void *arg, void *stack_top);
#define RUN(s) call_with_stack(slot_run, (s), (s)->stack + VH_STACK)
#else
const vh_stock_t *vh_stock;             /* the PC test sets it */
#define RUN(s) slot_run(s)
#define plausible(p) ((p) != NULL)
#endif

static uint32_t now_ms(void)
{
    uint32_t t[2];
    vh_stock->get_tim(t);
    return t[1];
}

static int32_t io_read(void *user, void *buf, uint32_t n)
{
    vh_slot *s = user;
    uint32_t t0 = vh_stats_enabled ? now_ms() : 0;
    uint32_t got = vh_stock->fread(buf, 1, n, s->fh, s->opts);
    if (vh_stats_enabled)
        s->io_ms += now_ms() - t0;
    if (got < n && vh_stock->get_errno() != VH_ERRNO_EOF)
        return -1;
    return (int32_t)got;
}

static int32_t io_seek(void *user, uint32_t pos)
{
    vh_slot *s = user;
    return vh_stock->fseek(s->fh, (int32_t)pos, 0, s->opts) == 0 ? 0 : -1;
}

static int32_t slot_run(void *arg)
{
    vh_slot *s = arg;
    if (s->op == OP_OPEN) {
        vwav_io io = { io_read, io_seek, s };
        return vwav_open(&s->v, &io, s->mem, VWAV_MEM);
    }
    return vwav_read(&s->v, s->buf, s->n);
}

static vh_slot *find(uint32_t fh)
{
    if (fh == 0)
        return NULL;
    for (int i = 0; i < VH_SLOTS; i++)
        if (slots[i].fh == fh)
            return &slots[i];
    return NULL;
}

int vh_is_flac(uint32_t fh)
{
    return find(fh) != NULL;
}

int vh_register(uint32_t fh, void *opts)
{
    vh_slot *s = NULL;
    int32_t r;

    if (fh == 0 || fh == VH_BAD_HANDLE || (fh & 3) != 0)
        return E_NO_SLOT;
    /* A fresh handle that is still in the table was closed on a path that the
       patch does not wrap. Free its slot. */
    s = find(fh);
    if (s != NULL) {
        vwav_close(&s->v);
        s->fh = 0;
        s = NULL;
    }
    for (int i = 0; i < VH_SLOTS; i++) {
        if (slots[i].fh == 0 && !slots[i].busy) {
            s = &slots[i];
            break;
        }
    }
    if (s == NULL) {
        vh_stock->fseek(fh, 0, 0, opts);
        return E_NO_SLOT;
    }
    s->busy = 1;
    s->next_pos = 0;
    s->run_bytes = 0;
    s->fh = fh;                         /* io_read and io_seek need it */
    s->opts = opts;
    s->op = OP_OPEN;
    r = RUN(s);
    if (r != 0) {
        s->fh = 0;
        vh_stock->fseek(fh, 0, 0, opts);
    }
    s->busy = 0;
    return r;
}

/* Return 1 if the file starts with "fLaC". Leave the position at 0. */
static int has_flac_magic(uint32_t fh)
{
    uint8_t magic[4];
    uint32_t got = vh_stock->fread(magic, 1, 4, fh, NULL);
    vh_stock->fseek(fh, 0, 0, NULL);
    return got == 4 && magic[0] == 'f' && magic[1] == 'L' && magic[2] == 'a' && magic[3] == 'C';
}

uint32_t hook_open(uint32_t a, vh_track_id id)
{
    uint32_t fh = vh_stock->open(a, id);
    int r;

    last_open = fh;
    if (fh == 0 || fh == VH_BAD_HANDLE || (fh & 3) != 0 || !vh_flac_enabled)
        return fh;
    r = vh_register(fh, NULL);
    /* A FLAC file that did not register must not open. The header cache of
       FUN_041b046c can hold type 11 for this track, and then the stock PCM
       path would play the raw FLAC bytes. A scope reject never gets type 11,
       so it opens normally and the parser rejects it. */
    if (r == VWAV_E_IO || (r == E_NO_SLOT && has_flac_magic(fh))) {
        vh_stock->fclose(fh, NULL);
        last_open = 0;
        return 0;
    }
    return fh;
}

int32_t hook_fclose(uint32_t fh, void *opts)
{
    vh_slot *s = find(fh);
    int32_t r;
    if (s != NULL) {
        vwav_close(&s->v);
        s->fh = 0;
    }
    r = vh_stock->fclose(fh, opts);
    if (s != NULL && vh_stats_enabled)
        vs_write();
    return r;
}

/* Sleep after a read in a long run (see VH_THROTTLE_AFTER_S). pos and got
   describe the read, decode_ms its time. Return the sleep in ms. */
static uint32_t throttle(vh_slot *s, uint32_t pos, uint32_t got, uint32_t decode_ms)
{
    uint32_t bps = s->v.bytes_per_sec;
    if (pos != s->next_pos)
        s->run_bytes = 0;               /* a jump */
    s->next_pos = pos + got;
    s->run_bytes += got;
    if (bps == 0 || got == 0 || s->run_bytes < VH_THROTTLE_AFTER_S * bps)
        return 0;
    /* The fill rate is audio_ms / (decode_ms + sleep). Keep it at
       VH_MIN_FILL_RATE or more. */
    uint32_t audio_ms = (uint32_t)((uint64_t)got * 1000 / bps);
    uint32_t budget = audio_ms / VH_MIN_FILL_RATE;
    uint32_t sleep = decode_ms;
    if (decode_ms >= budget)
        return 0;
    if (sleep > budget - decode_ms)
        sleep = budget - decode_ms;
    if (sleep > 0)
        vh_stock->dly_tsk((int32_t)sleep);
    return sleep;
}

uint32_t hook_fread(void *buf, uint32_t size, uint32_t n, uint32_t fh, void *opts)
{
    vh_slot *s = find(fh);
    int32_t got;

    if (s == NULL)
        return vh_stock->fread(buf, size, n, fh, opts);
    if (size == 0 || n == 0)
        return 0;
    if (s->busy) {
        vh_stock->set_errno(VH_ERRNO_BUSY);
        return 0;
    }
    vs_rec rec;
    uint32_t pos = vwav_tell(&s->v);
    uint32_t t0 = now_ms();
    if (vh_stats_enabled) {
        rec.t0 = t0;
        rec.pos = pos;
        rec.n = size * n;
        rec.file_bytes = s->v.file_bytes;
        rec.seeks = (uint16_t)s->v.seeks;
        rec.slot = (uint8_t)(s - slots);
        rec.behind = (uint16_t)vh_stock->dsp_status[1];
        rec.ahead = (uint16_t)vh_stock->dsp_status[2];
        rec.caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
        s->io_ms = 0;
    }
    s->busy = 1;
    s->opts = opts;
    s->op = OP_READ;
    s->buf = buf;
    s->n = size * n;
    got = RUN(s);
    s->busy = 0;
    uint32_t sleep = throttle(s, pos, got < 0 ? 0 : (uint32_t)got, now_ms() - t0);
    if (vh_stats_enabled) {
        rec.dt = now_ms() - rec.t0;
        rec.sleep = (uint8_t)(sleep < 255 ? sleep : 255);
        rec.io = s->io_ms;
        rec.got = got < 0 ? 0 : (uint32_t)got;
        rec.file_bytes = s->v.file_bytes - rec.file_bytes;
        rec.seeks = (uint16_t)(s->v.seeks - rec.seeks);
        vs_add(&rec);
    }
    if (got < 0)
        got = 0;
    if ((uint32_t)got < size * n && vwav_eof(&s->v))
        vh_stock->set_errno(VH_ERRNO_EOF);
    return (uint32_t)got / size;
}

int32_t hook_fseek(uint32_t fh, int32_t off, int32_t whence, void *opts)
{
    vh_slot *s = find(fh);
    if (s == NULL)
        return vh_stock->fseek(fh, off, whence, opts);
    return vwav_seek(&s->v, off, whence) == 0 ? 0 : -1;
}

int32_t hook_ftell(uint32_t fh, void *opts)
{
    vh_slot *s = find(fh);
    if (s == NULL)
        return vh_stock->ftell(fh, opts);
    return (int32_t)vwav_tell(&s->v);
}

int32_t hook_feof(uint32_t fh, void *opts)
{
    vh_slot *s = find(fh);
    if (s == NULL)
        return vh_stock->feof(fh, opts);
    return vwav_eof(&s->v);
}

int32_t hook_filelen(uint32_t fh, uint32_t *len, void *opts)
{
    vh_slot *s = find(fh);
    if (s == NULL)
        return vh_stock->filelen(fh, len, opts);
    *len = vwav_length(&s->v);
    return 0;
}

int32_t hook_fill_policy(uint32_t ctx)
{
    int32_t r = vh_stock->fill_policy(ctx);
    if (r == 1) {
        uint32_t *stream = *vh_stock->stream_ptr;
        if (plausible(stream) && vh_is_flac(stream[0])
            && vh_stock->dsp_status[2] >= VH_AHEAD_CAP) {
            vs_count_capped();
            r = 0;
        }
    }
    return r;
}

/* FUN_041b046c and FUN_041b0648 open the track with hook_open, then send
   {1, T} to the parser task from the same task. If T is a FLAC track (type 5)
   and the handle from that open is registered, give T the WAV type (11), so
   that the stock WAV parser reads the virtual header. The two functions get
   different contexts, so the check uses the latest open, not a fixed address. */
int32_t hook_send(uint32_t queue, vh_msg *msg, uint32_t len, int32_t timeout)
{
    if (len == 8 && msg != NULL && msg->code == 1) {
        uint32_t *t = (uint32_t *)msg->arg;
        if (t != NULL && t[2] == 5 && vh_is_flac(last_open))
            t[2] = 11;
    }
    return vh_stock->send(queue, msg, len, timeout);
}
