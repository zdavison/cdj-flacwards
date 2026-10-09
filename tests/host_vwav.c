/* PC harness for vwav.
     host_vwav dump IN.flac OUT.wav    write the virtual WAV file of IN
     host_vwav check IN.flac [REF]     run the vwcheck pattern, compare with REF
     host_vwav reverse IN.flac REF BLOCK
                                       read the PCM in BLOCK-byte blocks from the
                                       end to the start, as reverse play does;
                                       compare with REF and print the bytes that
                                       were read from IN
   dump prints "open=CODE". If CODE is not 0, it writes no file.
   check prints the same line as the console command "N,VW". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/vwav.h"
#include "../src/vwcheck.h"
#include "../src/crc32.h"

static unsigned char mem[VWAV_MEM];
static vwav v;
static uint64_t file_bytes;     /* bytes read from the real file */

static int32_t file_read(void *user, void *buf, uint32_t n)
{
    size_t got = fread(buf, 1, n, user);
    file_bytes += got;
    return ferror((FILE *)user) ? -1 : (int32_t)got;
}

static int32_t file_seek(void *user, uint32_t pos)
{
    return fseek(user, (long)pos, SEEK_SET) == 0 ? 0 : -1;
}

static FILE *open_flac(const char *path, int *result)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        perror(path);
        exit(2);
    }
    vwav_io io = { file_read, file_seek, f };
    *result = vwav_open(&v, &io, mem, sizeof mem);
    return f;
}

static int dump(const char *in, const char *out_path)
{
    int r;
    FILE *f = open_flac(in, &r);
    printf("open=%d\n", r);
    if (r != 0)
        return 1;
    FILE *out = fopen(out_path, "wb");
    if (out == NULL) {
        perror(out_path);
        return 2;
    }
    static unsigned char buf[1000];     /* not a multiple of the chunk size */
    int32_t got;
    while ((got = vwav_read(&v, buf, sizeof buf)) > 0)
        fwrite(buf, 1, (size_t)got, out);
    fclose(out);
    vwav_close(&v);
    fclose(f);
    return got < 0 ? 3 : 0;
}

static const uint8_t *ref_data;
static uint32_t ref_size;
static int mismatch;

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

static void seen(void *ctx, uint32_t off, const uint8_t *data, uint32_t n)
{
    (void)ctx;
    if (ref_data == NULL || mismatch)
        return;
    if (off + n > ref_size || memcmp(ref_data + off, data, n) != 0) {
        mismatch = 1;
        fprintf(stderr, "mismatch in %u bytes at offset %u\n", n, off);
    }
}

static int32_t v_read(void *user, void *buf, uint32_t n)
{
    return vwav_read(user, buf, n);
}

static int v_seek(void *user, int32_t off, int whence)
{
    return vwav_seek(user, off, whence);
}

static int check_file(const char *in, const char *ref_path)
{
    int r, e1, e2;
    FILE *f = open_flac(in, &r);
    if (r != 0) {
        printf("VW open=%d\n", r);
        return 1;
    }
    if (ref_path != NULL)
        ref_data = load(ref_path, &ref_size);
    uint32_t len = vwav_length(&v);
    vwcheck_file vf = { v_read, v_seek, len, &v, seen, NULL };
    uint32_t seq = vwcheck_seq(&vf, &e1);
    uint32_t jumps = vwcheck_jumps(&vf, 1, 300, &e2);
    printf("VW open=0 len=%u seq=%08x/%d jumps=%08x/%d\n", len, seq, e1, jumps, e2);
    vwav_close(&v);
    fclose(f);
    if (ref_data != NULL && ref_size != len) {
        fprintf(stderr, "length %u, reference %u\n", len, ref_size);
        return 1;
    }
    return e1 || e2 || mismatch;
}

static int reverse(const char *in, const char *ref_path, uint32_t block)
{
    static uint8_t buf[0x10000];
    int r, bad = 0;
    FILE *f = open_flac(in, &r);
    if (r != 0 || block > sizeof buf) {
        printf("open=%d\n", r);
        return 1;
    }
    ref_data = load(ref_path, &ref_size);
    uint32_t len = vwav_length(&v);
    uint32_t blocks = (len - VWAV_HEADER) / block;
    file_bytes = 0;
    for (uint32_t i = blocks; i-- > 0;) {
        uint32_t off = VWAV_HEADER + i * block;
        if (vwav_seek(&v, (int32_t)off, 0) != 0 || vwav_read(&v, buf, block) != (int32_t)block
            || memcmp(buf, ref_data + off, block) != 0) {
            bad = 1;
            fprintf(stderr, "block %u wrong\n", i);
            break;
        }
    }
    fseek(f, 0, SEEK_END);
    printf("reverse blocks=%u file=%ld read=%llu\n", blocks, ftell(f),
           (unsigned long long)file_bytes);
    vwav_close(&v);
    fclose(f);
    return bad;
}

/* For each POS: open IN again, seek to POS, read N bytes, and print the
   bytes read from IN. This replays the first read of a deck session. */
static int probe(int argc, char **argv)
{
    static uint8_t buf[0x10000];
    for (int i = 3; i + 1 < argc; i += 2) {
        int r;
        FILE *f = open_flac(argv[2], &r);
        uint32_t pos = (uint32_t)strtoul(argv[i], NULL, 0), n = (uint32_t)strtoul(argv[i + 1], NULL, 0);
        if (r != 0 || n > sizeof buf) {
            printf("open=%d\n", r);
            fclose(f);
            return 1;
        }
        file_bytes = 0;
        vwav_seek(&v, (int32_t)pos, 0);
        int32_t got = vwav_read(&v, buf, n);
        uint32_t crc = crc32_end(crc32_update(crc32_begin(), buf, got > 0 ? (uint32_t)got : 0));
        printf("%u %d %llu %08x\n", pos, got, (unsigned long long)file_bytes, crc);
        vwav_close(&v);
        fclose(f);
    }
    return 0;
}

/* Open IN once, then do the reads of a deck stats file (FLSTATn.TXT) in
   order. Print the FLAC bytes and the seeks for each read. */
static int replay(const char *in, const char *stats)
{
    static uint8_t buf[0x10000];
    char line[256];
    int r;
    FILE *f = open_flac(in, &r), *st = fopen(stats, "r");
    if (r != 0 || st == NULL) {
        printf("open=%d\n", r);
        return 1;
    }
    while (fgets(line, sizeof line, st)) {
        unsigned t0, dt, io, pos, n, got, fb, seeks;
        if (sscanf(line, "%u,%u,%u,%u,%u,%u,%u,seek=%u", &t0, &dt, &io, &pos, &n, &got, &fb, &seeks) != 8)
            continue;
        file_bytes = 0;
        uint32_t s0 = v.seeks;
        vwav_seek(&v, (int32_t)pos, 0);
        int32_t g = vwav_read(&v, buf, n < sizeof buf ? n : sizeof buf);
        printf("pos=%u got=%d file_bytes=%llu seeks=%u deck_file_bytes=%u deck_dt=%u\n", pos, g,
               (unsigned long long)file_bytes, v.seeks - s0, fb, dt);
    }
    vwav_close(&v);
    fclose(f);
    fclose(st);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "replay") == 0)
        return replay(argv[2], argv[3]);
    if (argc >= 5 && strcmp(argv[1], "probe") == 0)
        return probe(argc, argv);
    if (argc == 5 && strcmp(argv[1], "reverse") == 0)
        return reverse(argv[2], argv[3], (uint32_t)strtoul(argv[4], NULL, 0));
    if (argc == 4 && strcmp(argv[1], "dump") == 0)
        return dump(argv[2], argv[3]);
    if ((argc == 3 || argc == 4) && strcmp(argv[1], "check") == 0)
        return check_file(argv[2], argc == 4 ? argv[3] : NULL);
    fprintf(stderr, "usage: %s dump IN.flac OUT.wav | check IN.flac [REF.wav]\n", argv[0]);
    return 2;
}
