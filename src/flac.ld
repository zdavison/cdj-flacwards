/* The FLAC blob: the hooks and the VW console command, in the 0xff padding
   of the loaded application image. .bss is emitted as zeros into the image,
   so no start-up code is needed. The firmware calls the hook_* functions
   through literal-pool words, so the linker must keep them. */
ENTRY(_cmd_vw)
MEMORY
{
    PAD (rwx) : ORIGIN = 0x04347400, LENGTH = 0x043bfffc - 0x04347400
}
SECTIONS
{
    .text : { KEEP(*(.text.cmd_vw)) KEEP(*(.text.cmd_vp)) KEEP(*(.text.cmd_tk)) KEEP(*(.text.hook_*)) *(.text .text.*) } > PAD
    .rodata : { *(.rodata .rodata.*) } > PAD
    .data : {
        *(.data .data.*)
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
    } > PAD
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
