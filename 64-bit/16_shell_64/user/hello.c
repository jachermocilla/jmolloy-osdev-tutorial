// hello.c -- The smallest useful program on the initrd: it prints and exits.
//
// It exists to give the shell something to run that is not "prog". Type "hello"
// at the sh> prompt and watch the shell fork, the child exec this file, this
// program run to its syscall_exit(), and the shell's wait() return so the prompt
// comes back. It takes no input and holds no state -- which is the point, it is
// the control case against prog.c's line-reading.

#include "ulib.h"

#define USER_TEXT   __attribute__((section(".text.start")))

USER_TEXT void _start(void)
{
    syscall_monitor_write("  [hello] hello from a program on the initrd.\n");
    syscall_monitor_write("  [hello] my pid is ");
    syscall_monitor_write_dec(syscall_getpid());
    syscall_monitor_write(", and I am about to exit.\n");
    syscall_exit();

    for (;;) { }        // unreachable
}
