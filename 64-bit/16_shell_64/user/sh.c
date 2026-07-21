// sh.c -- A shell: the read-fork-exec-wait loop, and nothing more.
//
// This is the program chapter 15 promised. Every piece it needs was already on
// the bench: chapter 14 reads a line, chapter 11 forks, chapter 15 execs, and
// chapter 16 added the one missing primitive, wait(). A shell is just the loop
// that puts them in order:
//
//     print a prompt
//     read a line
//     fork
//         child: exec the line as a program
//         parent: wait for the child, then loop
//
// Type "hello" and the shell forks, the child becomes the "hello" program off
// the initrd, the shell waits until it exits, and the prompt returns. That is
// the whole of how a Unix shell runs a command, minus the parts -- arguments,
// pipes, redirection, job control -- that all hang off this same skeleton.
//
// It is itself a ring 3 program on the initrd, loaded by exec() like any other.
// The kernel execs "sh" at boot (see src/user.c) and then does nothing but run
// what this loop tells it to.

#include "ulib.h"

#define USER_TEXT   __attribute__((section(".text.start")))

// The prompt, and the built-in commands the shell answers itself rather than
// forking a program for. "exit" and "help" are not files on the initrd; a shell
// has to handle a few things in-process, and these are the smallest two.
static void help(void)
{
    syscall_monitor_write(
        "builtins: help, exit\n"
        "programs on the initrd: hello, prog\n"
        "anything else is looked up as a program name.\n");
}

USER_TEXT void _start(void)
{
    syscall_monitor_write(
        "\nsh -- a shell loaded from the initrd. Type 'help'.\n");

    char line[80];

    for (;;)
    {
        syscall_monitor_write("sh> ");
        read_line(line, sizeof(line));

        if (line[0] == 0)               // Empty line: just prompt again.
            continue;

        if (streq(line, "exit"))
            syscall_exit();             // Leaving the shell ends the session.

        if (streq(line, "help"))
        {
            help();
            continue;
        }

        // Not a builtin. Run it as a program: fork, and in the child become the
        // named file. The fork is what keeps the shell alive -- the child is the
        // one that gets consumed by exec, while the parent stays a shell.
        u64int pid = syscall_fork();

        if (pid == 0)
        {
            // The child. exec replaces it with the program, and on success this
            // is the last line of the shell's code the child ever runs. If exec
            // returns, the file was not found; the child says so and exits, so
            // it never falls back into the shell loop as a second shell.
            syscall_exec(line);
            syscall_monitor_write("sh: command not found: ");
            syscall_monitor_write(line);
            syscall_monitor_write("\n");
            syscall_exit();
        }

        // The parent. It waits for the child to finish before prompting again --
        // this is the line that makes the shell a shell and not a launcher. Drop
        // it and the next prompt races the program's own output onto the screen,
        // and both read the keyboard at once. wait() is what serialises them.
        syscall_wait(pid);
    }
}
