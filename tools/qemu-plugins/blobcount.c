/* SPDX-License-Identifier: GPL-2.0-or-later (it uses the QEMU plugin API). */
/* QEMU TCG plugin: count the instructions that run in one address range.
 *   -plugin blobcount.so,lo=0x04347400,hi=0x043bfffc,mark=0xADDR,mark=0xADDR...
 * Each time an instruction at a mark address runs, the plugin prints
 * "blobcount MARK COUNT" to stderr. COUNT is the running total in the range.
 * Use it to measure the cost of our code between two markers.
 * prof=FILE: also count the instructions per 16-byte block of the range and
 * write "address count" lines to FILE at exit (a profile).
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

#define MAX_MARKS 16

static uint64_t lo, hi;
static uint64_t marks[MAX_MARKS];
static int n_marks;
static struct qemu_plugin_scoreboard *board;
static qemu_plugin_u64 count;
static uint64_t *prof;          /* per 16-byte block, or NULL */
static char prof_path[256];

static void on_tb_exec(unsigned int vcpu, void *udata)
{
    uint64_t v = (uint64_t)(uintptr_t)udata;    /* block index << 8 | insns */
    prof[v >> 8] += v & 0xff;
}

static void on_mark(unsigned int vcpu, void *udata)
{
    fprintf(stderr, "blobcount %#" PRIx64 " %" PRIu64 "\n",
            (uint64_t)(uintptr_t)udata, qemu_plugin_u64_sum(count));
}

static void on_tb(struct qemu_plugin_tb *tb, void *userdata)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    if (prof != NULL && n > 0) {
        uint64_t pc0 = qemu_plugin_insn_vaddr(qemu_plugin_tb_get_insn(tb, 0)) & 0x1fffffff;
        if (pc0 >= lo && pc0 < hi && n < 256)
            qemu_plugin_register_vcpu_tb_exec_cb(tb, on_tb_exec, QEMU_PLUGIN_CB_NO_REGS,
                                                 (void *)(uintptr_t)((((pc0 - lo) >> 4) << 8) | n));
    }
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t pc = qemu_plugin_insn_vaddr(insn) & 0x1fffffff;
        if (pc < lo || pc >= hi)
            continue;
        qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(insn, QEMU_PLUGIN_INLINE_ADD_U64,
                                                            count, 1);
        for (int m = 0; m < n_marks; m++)
            if (pc == marks[m])
                qemu_plugin_register_vcpu_insn_exec_cb(insn, on_mark, QEMU_PLUGIN_CB_NO_REGS,
                                                       (void *)(uintptr_t)pc);
    }
}

static void at_exit(void *userdata)
{
    if (prof != NULL) {
        FILE *f = fopen(prof_path, "w");
        for (uint64_t i = 0; f != NULL && i < (hi - lo) / 16; i++)
            if (prof[i])
                fprintf(f, "%#" PRIx64 " %" PRIu64 "\n", lo + i * 16, prof[i]);
        if (f != NULL)
            fclose(f);
    }
    fprintf(stderr, "blobcount total %" PRIu64 "\n", qemu_plugin_u64_sum(count));
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                                           int argc, char **argv)
{
    for (int i = 0; i < argc; i++) {
        char *v = strchr(argv[i], '=');
        if (v == NULL)
            return -1;
        uint64_t x = strncmp(argv[i], "prof=", 5) ? strtoull(v + 1, NULL, 0) & 0x1fffffff : 0;
        if (strncmp(argv[i], "lo=", 3) == 0)
            lo = x;
        else if (strncmp(argv[i], "hi=", 3) == 0)
            hi = x;
        else if (strncmp(argv[i], "prof=", 5) == 0)
            snprintf(prof_path, sizeof prof_path, "%s", argv[i] + 5);
        else if (strncmp(argv[i], "mark=", 5) == 0 && n_marks < MAX_MARKS)
            marks[n_marks++] = x;
        else
            return -1;
    }
    if (prof_path[0])
        prof = calloc((hi - lo) / 16 + 1, sizeof *prof);
    board = qemu_plugin_scoreboard_new(sizeof(uint64_t));
    count = qemu_plugin_scoreboard_u64(board);
    qemu_plugin_register_vcpu_tb_trans_cb(id, on_tb, NULL);
    qemu_plugin_register_atexit_cb(id, at_exit, NULL);
    return 0;
}
