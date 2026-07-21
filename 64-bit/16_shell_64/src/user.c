// user.c -- The kernel's one embedded ring 3 stub: it launches the shell.
//
// Chapter 15 put a whole fork+exec demo here, compiled into the kernel image, to
// prove exec() worked. That job is done. All this file does now is bootstrap the
// shell -- but it does it with fork+exec, not exec alone, and the reason is worth
// stating because it is the same reason every Unix has an init process.
//
// This stub runs as a thread *inside the kernel's own process*. If it called
// exec() directly, exec would replace the kernel process's address space out
// from under the kernel -- the .user_text this code lives in would vanish mid-
// call. So instead it forks: the child, a brand-new process, is the one that
// execs "sh", and the kernel process is never disturbed. The parent stays behind
// as the shell's parent and waits on it, which also makes it the thing that
// reaps the shell if it ever exits. That is init in one paragraph: fork a child
// to be the first real program, and wait forever so nothing it leaves behind
// becomes an orphan.

#include "common.h"
#include "syscall.h"

#define USER_TEXT   __attribute__((section(".user_text")))
#define USER_DATA   __attribute__((section(".user_data")))

USER_DATA static char shname[] = "sh";
USER_DATA static char m_fail[] = "  [boot] could not exec sh -- is it on the initrd?\n";

USER_TEXT void start_shell(void)
{
    u64int pid = syscall_fork();

    if (pid == 0)
    {
        // The child: a fresh process, safe to exec. It becomes the shell.
        syscall_exec(shname);

        // Reached only if "sh" is not on the initrd.
        syscall_monitor_write(m_fail);
        syscall_exit();
    }

    // The parent stays in the kernel process and waits on the shell. wait()
    // returns only if the shell exits; then there is nothing left to run, so we
    // wait again and idle in the kernel forever.
    for (;;)
        syscall_wait(pid);
}
