// user.c -- The ring 3 program that forks and execs.
//
// This is the pair chapter 11 has been missing. fork() there made a second copy
// of one program; there was no way to make the copy become a *different*
// program. exec() is that way. The shape below is the oldest idiom in Unix:
// fork, and in the child, exec.
//
//     pid = fork();
//     if (pid == 0) exec("prog");   // child: become a new program
//     else          ...             // parent: carry on as yourself
//
// The parent keeps its own code and data and runs past the branch. The child is
// a copy right up until exec, and then it is not a copy of anything -- it is the
// program "prog", loaded fresh from the initrd, with a new address space and a
// clean stack. Two processes that were briefly identical walk away as different
// programs.

#include "common.h"
#include "syscall.h"

#define USER_TEXT   __attribute__((section(".user_text")))
#define USER_DATA   __attribute__((section(".user_data")))

USER_DATA static char m_fork[]  = "  [parent] forking...\n";
USER_DATA static char m_child[] = "  [child] now exec(\"prog\")\n";
USER_DATA static char m_ppid[]  = "  [parent] child has pid ";
USER_DATA static char m_fail[]  = "  [child] exec failed -- no such file\n";
USER_DATA static char m_nl[]    = "\n";
USER_DATA static char progname[] = "prog";

USER_TEXT static void spin(u64int n) { for (volatile u64int i = 0; i < n; i++); }

USER_TEXT void thread_task(void)
{
    syscall_monitor_write(m_fork);

    u64int pid = syscall_fork();

    if (pid == 0)
    {
        // The child, still an exact copy of the parent. The next call ends that.
        syscall_monitor_write(m_child);
        syscall_exec(progname);

        // Only reached if exec could not find the file. On success there is no
        // "after exec" -- the program that would run these lines no longer
        // exists.
        syscall_monitor_write(m_fail);
        syscall_exit();
    }

    // The parent. Its image is untouched; it prints the child's pid and idles,
    // leaving the keyboard to the program the child became.
    syscall_monitor_write(m_ppid);
    syscall_monitor_write_dec(pid);
    syscall_monitor_write(m_nl);

    for (;;)
        spin(9000000);
}
