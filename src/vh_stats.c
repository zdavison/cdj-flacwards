/* Debug statistics. See vh_stats.h. */
#include <stddef.h>
#include "putfmt.h"
#include "vfs_hook.h"
#include "vh_stats.h"

volatile uint32_t vh_stats_enabled;

static vs_rec recs[VS_MAX];
static uint32_t total;          /* records added since the last write */
uint32_t vs_capped;
static uint32_t file_no;        /* n in FLSTATn.TXT, 0 to 9 */
static char out[4096];
static uint32_t out_len;

void vs_add(const vs_rec *r)
{
    recs[total % VS_MAX] = *r;
    total++;
}

static int flush(uint32_t fh)
{
    int ok = out_len == 0 || vh_stock->fwrite(out, 1, out_len, fh, NULL) == out_len;
    out_len = 0;
    return ok;
}

static int put_line(uint32_t fh, const char *line, uint32_t n)
{
    if (out_len + n > sizeof out && !flush(fh))
        return 0;
    for (uint32_t i = 0; i < n; i++)
        out[out_len++] = line[i];
    return 1;
}

int vs_write(void)
{
    uint16_t name[] = { 'C', ':', '/', 'F', 'L', 'S', 'T', 'A', 'T', '0', '.', 'T', 'X', 'T', 0 };
    uint32_t kept = total < VS_MAX ? total : VS_MAX;
    uint32_t first = total - kept;
    char line[160];
    char *p;
    int ok;

    name[9] = (uint16_t)('0' + file_no);
    file_no = (file_no + 1) % 10;
    uint32_t fh = vh_stock->fopen(name, "w", NULL);
    if (fh == 0) {
        total = 0;
        return -1;
    }
    out_len = 0;
    p = put_str(line, "FLSTAT v1 records=");
    p = put_dec(p, kept);
    p = put_str(p, " dropped=");
    p = put_dec(p, first);
    p = put_str(p, " capped=");
    p = put_dec(p, vs_capped);
    p = put_str(p, "\nt0_ms,dt_ms,io_ms,pos,n,got,file_bytes,seeks,caller,slot,sleep,behind,ahead\n");
    ok = put_line(fh, line, (uint32_t)(p - line));
    for (uint32_t i = first; ok && i < total; i++) {
        const vs_rec *r = &recs[i % VS_MAX];
        p = put_dec(line, r->t0);
        *p++ = ',';
        p = put_dec(p, r->dt);
        *p++ = ',';
        p = put_dec(p, r->io);
        *p++ = ',';
        p = put_dec(p, r->pos);
        *p++ = ',';
        p = put_dec(p, r->n);
        *p++ = ',';
        p = put_dec(p, r->got);
        *p++ = ',';
        p = put_dec(p, r->file_bytes);
        p = put_str(p, ",seek=");
        p = put_dec(p, r->seeks);
        p = put_str(p, ",");
        p = put_hex(p, r->caller);
        *p++ = ',';
        p = put_dec(p, r->slot);
        *p++ = ',';
        p = put_dec(p, r->sleep);
        *p++ = ',';
        p = put_dec(p, r->behind);
        *p++ = ',';
        p = put_dec(p, r->ahead);
        *p++ = '\n';
        ok = put_line(fh, line, (uint32_t)(p - line));
    }
    ok = ok && flush(fh);
    vh_stock->fclose(fh, NULL);
    total = 0;
    vs_capped = 0;
    return ok ? 0 : -1;
}
