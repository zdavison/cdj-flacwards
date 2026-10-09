/* Console command "N,VW": open C:/T16.FLA (N = 1), C:/T24.FLA (N = 2) or
   C:/TNS.FLA (N = 3, a file without a SEEKTABLE) on the USB stick, register it as a FLAC handle, and read it through the hook
   wrappers. It prints the same line as "host_vwav check". It runs only when
   someone types it, never at boot. */
#include <stdint.h>
#include "fw.h"
#include "putfmt.h"
#include "vfs_hook.h"
#include "vwcheck.h"

static const uint16_t path1[] = { 'C', ':', '/', 'T', '1', '6', '.', 'F', 'L', 'A', 0 };
static const uint16_t path2[] = { 'C', ':', '/', 'T', '2', '4', '.', 'F', 'L', 'A', 0 };
static const uint16_t path3[] = { 'C', ':', '/', 'T', 'N', 'S', '.', 'F', 'L', 'A', 0 };

static int32_t vw_read(void *user, void *buf, uint32_t n)
{
    uint32_t got = hook_fread(buf, 1, n, (uint32_t)user, 0);
    if (got < n && fw_get_errno() != VH_ERRNO_EOF)
        return -1;
    return (int32_t)got;
}

static int vw_seek(void *user, int32_t off, int whence)
{
    return hook_fseek((uint32_t)user, off, whence, 0) == 0 ? 0 : -1;
}

void cmd_vw(void)
{
    const uint16_t *path = fw_args[0] == 3 ? path3 : fw_args[0] == 2 ? path2 : path1;
    char line[120];
    char *p;
    uint32_t fh = fw_fopen(path, "r", 0);

    if (fh == 0) {
        fw_puts("VW fopen failed\r\n");
        return;
    }
    int r = vh_register(fh, 0);
    p = put_str(line, "VW open=");
    p = put_int(p, r);
    if (r == 0) {
        uint32_t len = 0;
        int e1, e2;
        hook_filelen(fh, &len, 0);
        vwcheck_file f = { vw_read, vw_seek, len, (void *)fh, 0, 0 };
        uint32_t seq = vwcheck_seq(&f, &e1);
        uint32_t jumps = vwcheck_jumps(&f, 1, 300, &e2);
        p = put_str(p, " len=");
        p = put_dec(p, len);
        p = put_str(p, " seq=");
        p = put_hex(p, seq);
        p = put_str(p, "/");
        p = put_int(p, e1);
        p = put_str(p, " jumps=");
        p = put_hex(p, jumps);
        p = put_str(p, "/");
        p = put_int(p, e2);
    }
    p = put_str(p, "\r\n");
    *p = 0;
    fw_puts(line);
    hook_fclose(fh, 0);
}

/* Console command "N,TK": call dly_tsk(N) 100 times and print the time it
   took, in ms (system time). It shows the RTOS tick length. */
void cmd_tk(void)
{
    uint32_t t0[2], t1[2];
    char line[64];
    char *p;
    int32_t n = (int32_t)fw_args[0];

    fw_get_tim(t0);
    for (int i = 0; i < 100; i++)
        fw_dly_tsk(n);
    fw_get_tim(t1);
    p = put_str(line, "TK dly_tsk(");
    p = put_int(p, n);
    p = put_str(p, ") x100 = ");
    p = put_dec(p, t1[1] - t0[1]);
    p = put_str(p, " ms\r\n");
    *p = 0;
    fw_puts(line);
}
