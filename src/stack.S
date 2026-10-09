/* void call_on_stack(void (*fn)(void), void *stack_top)
 *
 * Run fn on a private stack, then return on the caller's stack. Firmware
 * tasks have small stacks (the file task has 2 KiB) and dr_flac needs about
 * 5.5 KiB, so every entry into our code switches stacks here.
 * r14 is callee-saved, so it holds the caller's stack pointer across fn.
 */
    .text
    .align 2
    .global _call_on_stack
_call_on_stack:
    mov.l   r14,@-r15
    sts.l   pr,@-r15
    mov     r15,r14
    jsr     @r4
    mov     r5,r15          /* delay slot: switch before fn runs */
    mov     r14,r15
    lds.l   @r15+,pr
    rts
    mov.l   @r15+,r14

/* int32_t call_with_stack(int32_t (*fn)(void *), void *arg, void *stack_top)
 *
 * Run fn(arg) on a private stack and return its result. It also saves MACH
 * and MACL, because the firmware's calling convention keeps them, and
 * libgcc may change them.
 */
    .align 2
    .global _call_with_stack
_call_with_stack:
    mov.l   r14,@-r15
    sts.l   pr,@-r15
    sts.l   mach,@-r15
    sts.l   macl,@-r15
    mov     r15,r14
    mov     r4,r0
    mov     r5,r4
    jsr     @r0
    mov     r6,r15          /* delay slot: switch before fn runs */
    mov     r14,r15
    lds.l   @r15+,macl
    lds.l   @r15+,mach
    lds.l   @r15+,pr
    rts
    mov.l   @r15+,r14
