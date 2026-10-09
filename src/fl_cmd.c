/* Console command "ADDR,LEN,FL": decode the FLAC file at ADDR (LEN bytes)
   and print its format and a CRC-32 of the decoded samples. It runs only
   when someone types it, never at boot. */
#include <stdint.h>
#include "fw.h"
#include "fltest_core.h"
#include "putfmt.h"

#define POOL_SIZE (192 * 1024)
static unsigned char pool_buf[POOL_SIZE] __attribute__((aligned(8)));

/* The console task's stack is about 4 KiB; dr_flac needs more. */
#define STACK_SIZE (16 * 1024)
static unsigned char fl_stack[STACK_SIZE] __attribute__((aligned(8)));
static volatile int fl_busy;

void call_on_stack(void (*fn)(void), void *stack_top);

static void fl_body(void)
{
    const void *data = (const void *)fw_args[0];
    uint32_t size = fw_args[1];
    fltest_result r;
    char line[160];
    char *p;

    fltest_run(data, size, pool_buf, POOL_SIZE, &r);

    p = put_str(line, "FL status=");
    p = put_dec(p, (uint32_t)r.status);
    p = put_str(p, " rate=");
    p = put_dec(p, r.sample_rate);
    p = put_str(p, " ch=");
    p = put_dec(p, r.channels);
    p = put_str(p, " bits=");
    p = put_dec(p, r.bits);
    p = put_str(p, " frames=");
    p = put_dec(p, r.frames_read);
    p = put_str(p, "/");
    p = put_dec(p, r.frames_total);
    p = put_str(p, " crc=");
    p = put_hex(p, r.crc);
    p = put_str(p, " pool=");
    p = put_dec(p, r.pool_peak);
    p = put_str(p, "\r\n");
    *p = 0;
    fw_puts(line);
}

void cmd_fl(void)
{
    if (fl_busy) {
        fw_puts("FL busy\r\n");
        return;
    }
    fl_busy = 1;
    call_on_stack(fl_body, fl_stack + STACK_SIZE);
    fl_busy = 0;
}
