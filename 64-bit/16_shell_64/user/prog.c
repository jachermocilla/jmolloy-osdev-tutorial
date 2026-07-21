// prog.c -- A ring 3 program that reads a line back. From chapter 15, now on
//           the shared ulib.h.
//
// Chapter 15 introduced this as the first program that was not the kernel. It
// carried its own copy of the read-a-line loop, because it was the only program
// that needed one. Chapter 16 adds a shell and a second program, so that loop
// moved into user/ulib.h and prog.c calls it -- the same code, in one place. The
// behaviour is unchanged: it greets, reads one line, echoes it back, and exits.
//
// It still uses no zero-initialised globals on purpose. A flat binary carries no
// .bss (objcopy drops it), so anything landing there would arrive as garbage.
// The format that fixes this is ELF, and needing .bss is one reason it exists.

#include "ulib.h"

#define TEXT_START __attribute__((section(".text.start")))

TEXT_START void _start(void)
{
    syscall_monitor_write("  [prog] hello -- I am a flat binary from the initrd.\n");
    syscall_monitor_write("  [prog] pid ");
    syscall_monitor_write_dec(syscall_getpid());
    syscall_monitor_write(" -- type a line and I will read it: ");

    char line[80];
    read_line(line, sizeof(line));

    syscall_monitor_write("\n  [prog] you said: ");
    syscall_monitor_write(line);
    syscall_monitor_write("\n  [prog] exiting.\n");
    syscall_exit();

    for (;;) { }        // unreachable: syscall_exit never returns
}
