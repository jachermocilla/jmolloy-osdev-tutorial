/* link.ld -- 64-bit linker script for the kernel.                          */
/*            Based on the original from Bran's Kernel Development          */
/*            tutorials: http://www.osdever.net/bkerndev/index.php.         */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(start)

SECTIONS
{
    . = 0x100000;

    /* The multiboot header must come first, and within the first 8 KiB. */
    .multiboot :
    {
        *(.multiboot)
    }

    .text ALIGN(4096) :
    {
        code = .; _code = .; __code = .;
        *(.text)
    }

    /* Code and constants that ring 3 is allowed to touch. Page-aligned and
       page-granular, so initialise_paging() can flip the user bit on exactly
       these pages and nothing else. */
    .user_text ALIGN(4096) :
    {
        user_text_start = .;
        *(.user_text)
        *(.user_data)
        . = ALIGN(4096);
        user_text_end = .;
    }

    .rodata ALIGN(4096) :
    {
        *(.rodata*)
    }

    .data ALIGN(4096) :
    {
        data = .; _data = .; __data = .;
        *(.data)
    }

    .bss ALIGN(4096) :
    {
        bss = .; _bss = .; __bss = .;
        *(COMMON)
        *(.bss)
    }

    end = .; _end = .; __end = .;

    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note*) }
}
