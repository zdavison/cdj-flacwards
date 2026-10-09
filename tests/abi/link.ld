/* Page-aligned layout for qemu-sh4-static (Linux user mode). */
ENTRY(_start)
SECTIONS
{
    . = 0x00400000;
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    . = ALIGN(0x1000);
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.* COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.stack) }
}
