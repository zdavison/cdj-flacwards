/* SH-4 user-mode test of the hook_open calling convention (run with
 * qemu-sh4-static). fw_call_open copies the firmware call at 0x041b038c:
 * r4 holds one value, and the 8-byte track ID goes on the stack.
 * fake_open reads its arguments as the stock FUN_042e8ea8 does.
 */
    .text
    .align 2

/* uint32_t fw_call_open(uint32_t a, const uint32_t id[2], void *fn) */
    .global _fw_call_open
_fw_call_open:
    sts.l   pr,@-r15
    add     #-8,r15
    mov.l   @r5,r7
    mov.l   @(4,r5),r2
    mov.l   r7,@r15
    jsr     @r6
    mov.l   r2,@(4,r15)
    add     #8,r15
    lds.l   @r15+,pr
    rts
    nop

/* Record r4 and the two stack words in fake_rec[0..2]. Return 0 (no file). */
    .global _fake_open
_fake_open:
    mov.l   .Lrec,r1
    mov.l   r4,@r1
    mov.l   @r15,r2
    mov.l   r2,@(4,r1)
    mov.l   @(4,r15),r2
    mov.l   r2,@(8,r1)
    rts
    mov     #0,r0

/* Linux entry: exit(main()). */
    .global _start
_start:
    mov.l   .Lmain,r0
    jsr     @r0
    nop
    mov     r0,r4
    mov     #1,r3
    trapa   #0x11
    .align 2
.Lrec:  .long _fake_rec
.Lmain: .long _main
