/* prog.ld -- Link the ring 3 program as a flat binary at USER_LOAD_BASE.       */
/*            The image has no header, so _start must be its first byte: the    */
/*            .text.start section (which holds _start) is placed first, and the */
/*            load address matches exec.c's USER_LOAD_BASE exactly.             */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x40000000;                 /* == USER_LOAD_BASE in exec.h */

    .text : {
        *(.text.start)              /* _start, first byte of the image */
        *(.text .text.*)
    }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }
    .bss    : { *(.bss .bss.*) *(COMMON) }

    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note*) }
}
