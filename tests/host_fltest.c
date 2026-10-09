/* Host reference: run the same decode core on a FLAC file and print the same line. */
#include <stdio.h>
#include <stdlib.h>
#include "../src/fltest_core.h"

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s FILE.flac\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *data = malloc(size);
    if (fread(data, 1, size, f) != (size_t)size) {
        perror("read");
        return 1;
    }
    fclose(f);
    static unsigned char pool[192 * 1024];
    fltest_result r;
    fltest_run(data, size, pool, sizeof pool, &r);
    printf("FL status=%d rate=%u ch=%u bits=%u frames=%u/%u crc=%08x pool=%u\n",
           r.status, r.sample_rate, r.channels, r.bits, r.frames_read, r.frames_total,
           r.crc, r.pool_peak);
    return r.status;
}
