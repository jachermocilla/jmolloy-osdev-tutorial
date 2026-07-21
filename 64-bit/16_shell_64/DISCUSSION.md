# Chapter 16: A Shell, or the Loop That Runs Everything

Every program in this book has been launched by the kernel. Chapter 15 loaded one from a file, and even that was set in motion by a line in `main.c`. The machine could run a program, but it could not yet be *told* which one. A shell is the program that closes that gap: it reads a name from a person and runs it, over and over, for as long as the machine is on.

What makes this chapter short is that the shell invents nothing. Chapter 14 taught the kernel to read a line. Chapter 11 taught it to fork. Chapter 15 taught it to exec. The shell is those three in a loop, plus one primitive chapter 15 left for here — a way for a parent to wait until its child is done. Build `wait()`, arrange the four calls in order, and the result is a shell.

---

# The Loop Is Four Calls

Strip a shell of arguments, pipes, redirection, and history, and what remains is a loop over four system calls:

```text
    for (;;) {
        write("sh> ");          // 1. prompt
        read_line(cmd);         // 2. read a command
        pid = fork();           // 3. split in two
        if (pid == 0)
            exec(cmd);          //    child: become the program
        else
            wait(pid);          // 4. parent: wait for it, then loop
    }
```

Read a command, make a copy of yourself, have the copy turn into the command, and wait for it to finish before asking for the next one. That is the whole idea, and it has run every Unix session since 1971.

The order is the meaning. Read comes first because there is nothing to run until someone says what. Fork comes before exec because exec is a one-way door — it destroys the caller — and the shell has to survive to prompt again. Wait comes last because without it the loop laps itself: the prompt returns while the program is still printing, and two processes read the keyboard at once.

---

# Why Fork, and Not Just Exec

The tempting shortcut is to skip the fork. The shell has a name; exec turns a process into the program named by a string; so why not exec the command directly?

Because exec does not start a program beside the caller. It *replaces* the caller. A process that execs `hello` does not launch `hello` and watch it — it becomes `hello`, and the code that called exec is gone. A shell that exec'd its commands directly would run exactly one, and then be that command forever. There would be no shell to return to.

Fork is what buys the shell its survival. It makes a second process, identical for one instant, and only the child walks through the exec door. The parent keeps its code, its loop, and its place at the prompt. The design spends a whole process to run one command, and that apparent waste is the point: the copy is expendable, so the original is safe.

```text
    sh (pid 5)  ── fork() ──►  sh (pid 5)   +   child (pid 7)
                                  │                  │
                                  │                  exec("hello")
                               wait(7)               │
                                  │              becomes hello,
                               (blocked)          runs, exits
                                  │                  │
                                  ◄───────────────── ┘   wait returns
                               prompt again
```

The child is the sacrifice; the parent is the shell. Chapter 15 already built both halves of the child's story. This chapter's real work is the parent's single blocked line: `wait`.

---

# wait(): The Primitive Chapter 15 Skipped

Chapter 12 gave threads `join`: a thread waits for another thread in the same process to finish, and collects its return value. That is nearly what the shell needs, but off by one level. The shell and its command are not two threads of one process — they are two processes, born of fork, each in its own address space. `join` cannot cross that boundary, and chapter 15's fork made the child *detached* precisely to say so. The parent there had no way to wait, so it idled.

`wait()` is `join` moved up one level, from thread to process. Set them side by side and the code is the same routine twice: find the target, and if it has not finished, hang yourself on its wakeup hook and block; when it exits, it wakes you; then read its exit value and free its remains. The only edits are that `wait` finds its target by process id instead of thread id, and it drops `join`'s "is this even joinable" checks, because a child is its parent's to reap by definition.

The subtlety is in the remains. When a process's last thread calls exit, it cannot free everything on its way out — it is still standing on its own kernel stack, still executing in its own address space. Chapter 12's `thread_exit` already faces this: it frees the address space (the expensive part) but leaves the thread's stack and bookkeeping for whoever waits. What is left is a **zombie** — a finished process reduced to four things: its kernel stack, its `thread_t`, its exit value, and its process id. It holds those until a parent calls `wait`, which reads the exit value and frees the rest.

That last item, the process id, is why this chapter touches `thread_t` at all. A zombie has already lost its `process` struct — `thread_exit` freed it — so it can no longer answer "which process were you?" by looking there. `wait` still needs that answer to match a zombie to the pid its parent is asking about. So the pid is copied onto the thread when the thread is born and rides along after the process is gone. One field, stamped in three places, and it exists entirely to survive the death of the struct that used to hold it.

```text
    child running ──► exit() ──► ZOMBIE ──► parent's wait() ──► gone
                        │           │              │
              frees address     keeps: stack,   reads exit value,
              space (big)       thread_t, exit  frees stack +
                                value, pid       thread_t
```

The zombie is a real Unix idea, not a shortcut. A finished child lingers as a near-empty record so its parent can still learn how it did. Reap it and it disappears; forget to, and it lingers as a leak — which is why a shell that forks always waits.

---

# init Forks; It Does Not exec

The kernel cannot simply exec the shell. That lesson cost a page fault to learn, and it is worth keeping.

The kernel's first ring 3 code runs as a thread inside the kernel's *own* process, sharing the address space the machine booted in. Have that thread call `exec("sh")` and exec does what it always does — it tears down the current address space and builds the shell's in its place. But the current address space is the kernel's, and other threads are still living in it, including the boot thread idling in `main`. Pulling it out from under them faults the moment one of them touches a page the shell no longer maps.

The fix is the shape every Unix uses to reach its first program. The kernel's stub does not exec the shell; it *forks*, and the child — a fresh, disposable process — is the one that execs. The kernel process is never disturbed. The parent stays behind and waits on the shell, which makes it the shell's parent and the reaper of whatever the shell leaves behind. Fork a child to be the first real program, then wait forever: that paragraph is `init`, and it is the same fork-then-exec that the shell itself runs one level down.

---

# What a Shell Handles Itself

Two commands in this shell are not programs. Type `exit` and there is no file to load — the shell has to end its own loop. Type `help` and the message has to come from the shell, since no other program knows what the shell can do. These are **builtins**, and every shell has a handful for the same reason: some actions change the shell itself, and a forked child cannot reach back to change its parent. A child that `cd`'d into a directory would change its own doomed copy and exit, leaving the shell exactly where it started. Anything that must outlast the command runs in the shell, before the fork.

Everything else is looked up as a program name. The dispatch is a short ladder: compare against each builtin, and on no match, fork and exec. Real shells search a list of directories for the name; this one hands the bare string to exec, which looks only on the initrd. The search path is policy layered on top — the skeleton underneath is the same.

---

# What This Shell Does Not Do

It takes no arguments. `exec` here receives a single word, so `hello` works and `hello world` does not — the whole line goes to exec as one name, and there is no `argv` for a program to read. Adding arguments means splitting the line on spaces and passing a vector through exec onto the new program's stack, which is real work and a natural next step.

It has no pipes, no redirection, and no background jobs. Each of those is a rearrangement of the child between fork and exec — the gap where the child is a normal process that can close a file, open another, or wire its output to a pipe before becoming the command. The shell sets the scene in that gap and the program is none the wiser. `&` is the smallest of them: it is the shell choosing *not* to wait, letting the child run while the prompt returns. Every one of these features hangs off the four-call skeleton without changing it.

The honest debts are the ones the whole book carries. `wait` finds a child by walking the thread registry and matching a pid, which is fine when a forked process has one thread and wrong the moment it has two — the walk would return whichever thread it met first. The pointer that exec, fork, and wait each trust from ring 3 still goes unchecked. These are correct for the machine this book runs on: one processor, one thread per process, one user who is not an adversary. A second processor makes several of those sentences false at once, and that is where the book ends.

---

# Looking Ahead

The machine now does the thing a computer is for: it sits at a prompt and runs what it is told. A person types a name, a program loads from storage and runs, and the prompt returns for the next one. Every layer under that prompt was built by hand in this book — the screen it prints to, the interrupts that deliver the keystrokes, the heap the shell's line buffer lives in, the paging that gives each program its own memory, the scheduler that runs them in turn, the traps that carry the four system calls across the ring boundary, and the filesystem the programs load from.

What is left is breadth, not depth. Arguments, a real filesystem that can be written as well as read, a path to search, more programs to run — each is an addition to a structure that is now complete, not a missing piece of it. The kernel has become a small operating system: a thing that runs other things and gets out of their way.
