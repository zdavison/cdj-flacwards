/* The release FLAC blob: the hooks only, in the 0xff padding of the loaded
   application image. .bss is emitted as zeros into the image,
   so no start-up code is needed. The firmware calls the hook_* functions
   through literal-pool words, so the linker must keep them. */
ENTRY(_hook_open)
MEMORY
{
    PAD (rwx) : ORIGIN = 0x04347400, LENGTH = 0x043bfffc - 0x04347400
}
SECTIONS
{
    .text : { KEEP(*(.text.hook_*)) *(.text .text.*) } > PAD
    .rodata : { *(.rodata .rodata.*) } > PAD
    .data : {
        *(.data .data.*)
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
    } > PAD
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
