// prog.c -- A ring 3 program that lives on the initrd, not in the kernel image.
//           exec() loads it from a file and runs it. New in chapter 15.
//
// This is the first program in the book that is not compiled into the kernel.
// It is built on its own (see user/prog.ld and mkinitrd.sh), turned into a flat
// binary, and packed into the initrd under the name "prog". The kernel loads
// the whole file at USER_LOAD_BASE and jumps to its first byte -- which is why
// _start has to *be* the first byte. It talks to the kernel only through the
// same int $0x80 syscalls every ring 3 program in this book has used.
//
// It uses no zero-initialised globals, and that is on purpose. A flat binary
// carries no .bss -- objcopy drops it -- so anything that would land there
// would arrive as garbage. Everything here is a local or a string literal. The
// format that fixes this is ELF, and needing .bss is one of the reasons it
// exists.

#include "syscall.h"

#define TEXT_START __attribute__((section(".text.start")))

TEXT_START void _start(void)
{
    syscall_monitor_write("  [prog] hello -- I am a flat binary from the initrd.\n");
    syscall_monitor_write("  [prog] pid ");
    syscall_monitor_write_dec(syscall_getpid());
    syscall_monitor_write(" -- type a line and I will read it: ");

    char line[80];
    int  n = 0;

    for (;;)
    {
        char c = (char)syscall_getchar();
        if (c == '\n')
            break;
        if (c == '\b')
        {
            if (n > 0) { n--; syscall_monitor_write("\b \b"); }
            continue;
        }
        if (n < 79)
        {
            char echo[2];
            echo[0] = c;
            echo[1] = 0;
            syscall_monitor_write(echo);
            line[n++] = c;
        }
    }
    line[n] = 0;

    syscall_monitor_write("\n  [prog] you said: ");
    syscall_monitor_write(line);
    syscall_monitor_write("\n  [prog] exiting.\n");
    syscall_exit();

    for (;;) { }        // unreachable: syscall_exit never returns
}
