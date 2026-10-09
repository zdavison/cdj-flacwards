/* PC tests for vfs_hook, with fake stock vfs functions.
     test_vfs_hook FLAC REF OTHER
   FLAC is t16_44.flac, REF is its reference WAV file, OTHER is a file that
   is not FLAC. A fake handle h is files[h / 4]. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/vfs_hook.h"
#include "../src/vh_stats.h"

#define NFILES 8

typedef struct {
    const uint8_t *data;
    uint32_t size;
    uint32_t pos;
} fake_file;

static fake_file files[NFILES + 1];
static uint32_t fake_ms;
static int32_t fake_errno;
static int fake_reads, fake_closes, fake_fail;
static struct {
    uint32_t queue, len;
    int32_t timeout;
    vh_msg *msg;
    int calls;
} sent;
static int failures;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); \
                                  failures++; } } while (0)

static fake_file *ff(uint32_t fh)
{
    return &files[fh / 4];
}

static uint32_t fk_open(uint32_t a, vh_track_id id)
{
    uint32_t h = id.w0;
    (void)a;
    if (h / 4 <= NFILES)
        ff(h)->pos = 0;
    return h;
}

static int32_t fk_fclose(uint32_t fh, void *opts)
{
    (void)fh;
    (void)opts;
    fake_closes++;
    return 0;
}

static uint32_t fk_fread(void *buf, uint32_t size, uint32_t n, uint32_t fh, void *opts)
{
    fake_file *f = ff(fh);
    (void)opts;
    fake_reads++;
    if (fake_fail) {
        fake_errno = -5;
        return 0;
    }
    uint32_t want = size * n;
    uint32_t avail = f->size - f->pos;
    fake_ms += 1;                       /* each stock read takes 1 ms */
    uint32_t got = want < avail ? want : avail;
    memcpy(buf, f->data + f->pos, got);
    f->pos += got;
    if (got < want)
        fake_errno = -14;
    return got / size;
}

static int32_t fk_fseek(uint32_t fh, int32_t off, int32_t whence, void *opts)
{
    fake_file *f = ff(fh);
    (void)opts;
    int64_t base = whence == 0 ? 0 : whence == 1 ? f->pos : f->size;
    int64_t t = base + off;
    if (t < 0 || t > f->size)
        return -1;
    f->pos = (uint32_t)t;
    return 0;
}

static int32_t fk_ftell(uint32_t fh, void *opts)
{
    (void)opts;
    return (int32_t)ff(fh)->pos;
}

static int32_t fk_feof(uint32_t fh, void *opts)
{
    (void)opts;
    return ff(fh)->pos >= ff(fh)->size;
}

static int32_t fk_filelen(uint32_t fh, uint32_t *len, void *opts)
{
    (void)opts;
    *len = ff(fh)->size;
    return 0;
}

static int32_t fk_send(uint32_t queue, vh_msg *msg, uint32_t len, int32_t timeout)
{
    sent.queue = queue;
    sent.msg = msg;
    sent.len = len;
    sent.timeout = timeout;
    sent.calls++;
    return 0;
}

static char written[200000];
static uint32_t written_len;
static char opened_name[32];
static char opened_mode[4];

static void fk_get_tim(uint32_t t[2])
{
    t[0] = 0;
    t[1] = fake_ms;
}

static uint32_t sleeps, slept_ms;
static int32_t policy_result;
static uint32_t policy_calls, policy_ctx;
static volatile uint32_t dsp_status[5];
static uint32_t stream_obj[7];
static uint32_t *stream_ptr;

static int32_t fk_fill_policy(uint32_t ctx)
{
    policy_calls++;
    policy_ctx = ctx;
    return policy_result;
}

static int32_t fk_dly_tsk(int32_t ms)
{
    sleeps++;
    slept_ms += (uint32_t)ms;
    fake_ms += (uint32_t)ms;
    return 0;
}

/* The stats file: handle 28 (files[7]) collects the bytes in written[]. */
static uint32_t fk_fopen(const uint16_t *path, const char *mode, void *opts)
{
    int i;
    (void)opts;
    for (i = 0; path[i] && i < 31; i++)
        opened_name[i] = (char)path[i];
    opened_name[i] = 0;
    strncpy(opened_mode, mode, 3);
    written_len = 0;
    return 28;
}

static uint32_t fk_fwrite(const void *buf, uint32_t size, uint32_t n, uint32_t fh, void *opts)
{
    (void)opts;
    if (fh != 28 || written_len + size * n > sizeof written)
        return 0;
    memcpy(written + written_len, buf, size * n);
    written_len += size * n;
    return n;
}

static void fk_set_errno(int32_t code)
{
    fake_errno = code;
}

static int32_t fk_get_errno(void)
{
    return fake_errno;
}

static const vh_stock_t fake = {
    fk_open, fk_fclose, fk_fread, fk_fseek, fk_ftell, fk_feof, fk_filelen,
    fk_send, fk_set_errno, fk_get_errno, fk_get_tim, fk_fopen, fk_fwrite, fk_dly_tsk,
    fk_fill_policy, dsp_status, &stream_ptr,
};

static uint8_t *flac, *ref, *other;
static uint32_t flac_size, ref_size, other_size;

static uint8_t *load(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        perror(path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    *size = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(*size);
    if (fread(data, 1, *size, f) != *size) {
        perror(path);
        exit(2);
    }
    fclose(f);
    return data;
}

static uint32_t open_file(uint32_t h, const uint8_t *data, uint32_t size)
{
    vh_track_id id = { h, 0 };
    ff(h)->data = data;
    ff(h)->size = size;
    return hook_open(0, id);
}

static void test_flac_read(void)
{
    static uint8_t buf[2048];
    uint32_t len = 0, done = 0, got;
    uint32_t h = open_file(4, flac, flac_size);

    CHECK(h == 4);
    CHECK(vh_is_flac(4));
    CHECK(hook_filelen(4, &len, NULL) == 0 && len == ref_size);
    fake_errno = 0;
    while ((got = hook_fread(buf, 1, sizeof buf, 4, NULL)) > 0) {
        CHECK(memcmp(buf, ref + done, got) == 0);
        done += got;
        if (got < sizeof buf)
            break;
    }
    CHECK(done == ref_size);
    CHECK(fake_errno == -14);
    CHECK(hook_feof(4, NULL) == 1);
    CHECK(hook_fseek(4, -100, 2, NULL) == 0);
    CHECK(hook_ftell(4, NULL) == (int32_t)(ref_size - 100));
    CHECK(hook_feof(4, NULL) == 0);
    CHECK(hook_fseek(4, 1, 2, NULL) != 0);
    CHECK(hook_fseek(4, -1, 0, NULL) != 0);
    CHECK(hook_fread(buf, 2, 10, 4, NULL) == 10);
    CHECK(memcmp(buf, ref + ref_size - 100, 20) == 0);
    fake_closes = 0;
    CHECK(hook_fclose(4, NULL) == 0);
    CHECK(fake_closes == 1);
    CHECK(!vh_is_flac(4));
}

static void test_passthrough(void)
{
    uint8_t buf[4];
    uint32_t len = 0;
    uint32_t h = open_file(8, other, other_size);

    CHECK(h == 8);
    CHECK(!vh_is_flac(8));
    CHECK(ff(8)->pos == 0);
    CHECK(hook_fread(buf, 1, 4, 8, NULL) == 4 && memcmp(buf, other, 4) == 0);
    CHECK(hook_filelen(8, &len, NULL) == 0 && len == other_size);
    CHECK(hook_fclose(8, NULL) == 0);
}

static void test_table_full(void)
{
    for (uint32_t h = 4; h <= 16; h += 4) {
        CHECK(open_file(h, flac, flac_size) == h);
        CHECK(vh_is_flac(h));
    }
    /* A FLAC file with no free slot must fail to open. If it opened, a
       header-cache hit with type 11 would play the raw FLAC bytes. */
    fake_closes = 0;
    CHECK(open_file(20, flac, flac_size) == 0);
    CHECK(!vh_is_flac(20));
    CHECK(fake_closes == 1);
    /* A file that is not FLAC still opens when all slots are in use. */
    CHECK(open_file(24, other, other_size) == 24);
    CHECK(ff(24)->pos == 0);
    CHECK(hook_fclose(24, NULL) == 0);
    CHECK(hook_fclose(8, NULL) == 0);
    CHECK(open_file(20, flac, flac_size) == 20);
    CHECK(vh_is_flac(20));
    for (uint32_t h = 4; h <= 20; h += 4)
        if (h != 8)
            hook_fclose(h, NULL);
}

static void test_bad_handles(void)
{
    uint32_t bad[] = { 0, VH_BAD_HANDLE };
    for (int i = 0; i < 2; i++) {
        vh_track_id id = { bad[i], 0 };
        fake_reads = 0;
        CHECK(hook_open(0, id) == bad[i]);
        CHECK(fake_reads == 0);
        CHECK(!vh_is_flac(bad[i]));
    }
}

static void test_read_error(void)
{
    static uint8_t buf[4096];
    uint32_t h = open_file(4, flac, flac_size);

    CHECK(vh_is_flac(h));
    CHECK(hook_fseek(h, 1000000, 0, NULL) == 0);
    fake_fail = 1;
    fake_errno = 0;
    CHECK(hook_fread(buf, 1, sizeof buf, h, NULL) < sizeof buf);
    CHECK(fake_errno != -14);
    fake_fail = 0;
    hook_fclose(h, NULL);
}

static void test_open_io_error(void)
{
    /* A read error while the file opens: the open fails, so that a cached
       type 11 cannot play the raw bytes. */
    fake_closes = 0;
    fake_fail = 1;
    CHECK(open_file(4, flac, flac_size) == 0);
    fake_fail = 0;
    CHECK(!vh_is_flac(4));
    CHECK(fake_closes == 1);
}

static void test_stale_handle(void)
{
    /* The firmware closed handle 4 on a path that the patch does not wrap.
       The next open gets the same handle for a file that is not FLAC. */
    uint8_t buf[4];
    CHECK(open_file(4, flac, flac_size) == 4);
    CHECK(vh_is_flac(4));
    CHECK(open_file(4, other, other_size) == 4);
    CHECK(!vh_is_flac(4));
    CHECK(hook_fread(buf, 1, 4, 4, NULL) == 4 && memcmp(buf, other, 4) == 0);
    CHECK(hook_fclose(4, NULL) == 0);
}

static void test_disabled(void)
{
    /* Stage-1 deck build: the hooks are in place, but FLAC registration is
       off. A FLAC file then opens on the stock path, as on 4.32. */
    uint8_t buf[4];
    vh_flac_enabled = 0;
    CHECK(open_file(4, flac, flac_size) == 4);
    CHECK(!vh_is_flac(4));
    CHECK(hook_fread(buf, 1, 4, 4, NULL) == 4 && memcmp(buf, "fLaC", 4) == 0);
    CHECK(hook_fclose(4, NULL) == 0);
    vh_flac_enabled = 1;
}

static int count_lines(const char *text, uint32_t len)
{
    int n = 0;
    for (uint32_t i = 0; i < len; i++)
        n += text[i] == '\n';
    return n;
}

static void test_stats(void)
{
    /* Debug build: each firmware read on a FLAC handle gives one record.
       The close writes them to C:/FLSTATn.TXT. */
    static uint8_t buf[2048];
    vh_stats_enabled = 1;
    fake_closes = 0;
    uint32_t h = open_file(4, flac, flac_size);
    CHECK(hook_fread(buf, 1, sizeof buf, h, NULL) == sizeof buf);
    CHECK(hook_fread(buf, 1, sizeof buf, h, NULL) == sizeof buf);
    CHECK(hook_fseek(h, 2000000, 0, NULL) == 0);
    CHECK(hook_fread(buf, 1, sizeof buf, h, NULL) == sizeof buf);
    CHECK(hook_fseek(h, 1000000, 0, NULL) == 0);        /* backward */
    CHECK(hook_fread(buf, 1, sizeof buf, h, NULL) == sizeof buf);
    CHECK(hook_fclose(h, NULL) == 0);
    vh_stats_enabled = 0;
    written[written_len < sizeof written ? written_len : sizeof written - 1] = 0;
    CHECK(strcmp(opened_name, "C:/FLSTAT0.TXT") == 0);
    CHECK(strcmp(opened_mode, "w") == 0);
    CHECK(fake_closes == 2);                            /* the track and the stats file */
    CHECK(strncmp(written, "FLSTAT ", 7) == 0);
    CHECK(count_lines(written, written_len) == 2 + 4);  /* header, column names, 4 reads */
    CHECK(strstr(written, ",1000000,2048,2048,") != NULL);
    /* The backward read needed one dr_flac seek; it is the last record. */
    const char *last = written + written_len - 2;
    while (last > written && last[-1] != '\n')
        last--;
    CHECK(strstr(last, ",1000000,") != NULL);
    CHECK(strstr(last, ",seek=1,") != NULL);
}

static void test_throttle(void)
{
    /* t16_44: 176400 bytes per second. The firmware reads 9408 bytes (53 ms
       of audio) at a time, as on the deck. */
    static uint8_t buf[9408];
    const uint32_t bps = 176400;
    uint32_t h = open_file(4, flac, flac_size), pos = 44, i;

    sleeps = 0;
    CHECK(hook_fseek(h, (int32_t)pos, 0, NULL) == 0);
    /* Up to 8 s of audio after a jump: no sleep. */
    for (i = 0; (i + 1) * sizeof buf < VH_THROTTLE_AFTER_S * bps; i++)
        CHECK(hook_fread(buf, 1, sizeof buf, h, NULL) == sizeof buf);
    CHECK(sleeps == 0);
    /* After that: sleeps, and the fill stays at 3x real time or faster. */
    uint32_t t0 = fake_ms, audio = 0;
    for (i = 0; i < 40; i++) {
        CHECK(hook_fread(buf, 1, sizeof buf, h, NULL) == sizeof buf);
        audio += sizeof buf;
    }
    uint32_t elapsed = fake_ms - t0, audio_ms = (uint32_t)((uint64_t)audio * 1000 / bps);
    CHECK(sleeps > 0);
    CHECK(audio_ms >= VH_MIN_FILL_RATE * elapsed);
    /* A jump starts a new run: no sleep again. */
    sleeps = 0;
    CHECK(hook_fseek(h, 1000000, 0, NULL) == 0);
    for (i = 0; i < 20; i++)
        CHECK(hook_fread(buf, 1, sizeof buf, h, NULL) == sizeof buf);
    CHECK(sleeps == 0);
    hook_fclose(h, NULL);
}

static void test_fill_cap(void)
{
    /* The stock policy says "fill forward" (1). For a FLAC stream with
       VH_AHEAD_CAP units or more ahead in the DSP, the hook says idle (0). */
    uint32_t h = open_file(4, flac, flac_size);
    stream_obj[0] = h;
    stream_ptr = stream_obj;
    policy_result = 1;
    dsp_status[2] = VH_AHEAD_CAP;
    policy_calls = 0;
    CHECK(hook_fill_policy(0x483543c) == 0);
    CHECK(policy_calls == 1 && policy_ctx == 0x483543c);
    dsp_status[2] = VH_AHEAD_CAP - 1;            /* below the cap: stock result */
    CHECK(hook_fill_policy(0x483543c) == 1);
    dsp_status[2] = 3000;
    policy_result = 2;                           /* backward fill: not changed */
    CHECK(hook_fill_policy(0x483543c) == 2);
    policy_result = 0;
    CHECK(hook_fill_policy(0x483543c) == 0);
    policy_result = 1;
    stream_obj[0] = 8;                           /* not a FLAC handle */
    CHECK(hook_fill_policy(0x483543c) == 1);
    stream_ptr = NULL;                           /* no stream */
    CHECK(hook_fill_policy(0x483543c) == 1);
    hook_fclose(h, NULL);
    stream_obj[0] = h;
    stream_ptr = stream_obj;                     /* closed: not FLAC any more */
    CHECK(hook_fill_policy(0x483543c) == 1);
}

static void test_send(void)
{
    uint32_t t[3] = { 0, 0, 5 };
    vh_msg m = { 1, (uintptr_t)t };
    uint32_t h = open_file(4, flac, flac_size);

    sent.calls = 0;
    CHECK(hook_send(7, &m, 8, -1) == 0);
    CHECK(t[2] == 11);
    CHECK(sent.calls == 1 && sent.queue == 7 && sent.msg == &m && sent.len == 8
          && sent.timeout == -1);
    t[2] = 5;
    m.code = 2;
    hook_send(7, &m, 8, -1);
    CHECK(t[2] == 5);
    m.code = 1;
    t[2] = 1;                           /* MP3 */
    hook_send(7, &m, 8, -1);
    CHECK(t[2] == 1);
    t[2] = 5;
    hook_send(7, &m, 4, -1);
    CHECK(t[2] == 5);
    /* The latest open is a file that is not FLAC: no type change. */
    CHECK(open_file(8, other, other_size) == 8);
    hook_send(7, &m, 8, -1);
    CHECK(t[2] == 5);
    CHECK(sent.calls == 5);
    hook_fclose(8, NULL);
    hook_fclose(h, NULL);
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s FLAC REF OTHER\n", argv[0]);
        return 2;
    }
    flac = load(argv[1], &flac_size);
    ref = load(argv[2], &ref_size);
    other = load(argv[3], &other_size);
    vh_stock = &fake;
    test_flac_read();
    test_passthrough();
    test_table_full();
    test_bad_handles();
    test_read_error();
    test_open_io_error();
    test_stale_handle();
    test_disabled();
    test_stats();
    test_throttle();
    test_fill_cap();
    test_send();
    printf("%d failures\n", failures);
    return failures != 0;
}
