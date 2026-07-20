# Chapter 15: exec(), or Becoming Someone Else

Every program this book has run was already inside the kernel. The ring 3 demos were functions compiled into the kernel image and published to user space out of a section called `.user_text`. They ran in ring 3, they made system calls, they were user code in every way that matters — except that they shipped as part of the kernel and could not have existed without it.

This chapter cuts that cord. A program is read from a file on the initrd, loaded into memory, and run, having never been part of the kernel at all. And the process that runs it is not a new process: it is an existing one that stops being what it was and becomes the program instead.

That second half is what `exec` means, and it is stranger than loading a file. `fork` from chapter 11 made a copy of a running program. Nothing since has been able to make a running program into a *different* one. `exec` is that missing operation, and the two together — fork to make a process, exec to change what it runs — are how every Unix has started every program for fifty years.

---

# The Shape Is Older Than Any of This

The idiom fits in five lines and has not changed since the 1970s.

```text
    pid = fork();
    if (pid == 0)
        exec("prog");       // child: become a new program
    else
        ...                 // parent: carry on as yourself
```

`fork` returns twice, once in each of two now-identical processes. The child takes the branch that calls `exec` and ceases to be a copy of its parent; the parent takes the other branch and remains itself. What began as one program splits into two that were briefly the same and then diverge for good — the parent still the parent, the child now some program off the disk that shares none of the parent's code.

Splitting the job in two looks wasteful — why copy the whole parent in `fork` only to throw it away in `exec`? — and it is the design's quiet strength. Between the fork and the exec, the child is a normal process that can rearrange itself: close files, change identity, redirect output. Everything a shell does to set up a command happens in that gap. The chapter builds the two calls; the gap between them is where chapter 16's shell will live.

---

# What exec Has To Do

Loading a program is the easy half and the obvious half: find the file, put its bytes in memory, jump to them. The hard half is that a process is already running when it asks to become something else, and it is running *in the very memory it wants to replace*.

`exec` resolves this by building the new world beside the old one and stepping across, rather than remodelling the house it is standing in.

```text
    1. read the file from the initrd            (before anything is destroyed)
    2. build a fresh address space              (kernel shared, no user pages)
    3. switch into it
    4. load the program's bytes                 (as user pages, at its address)
    5. give it a clean user stack
    6. rewrite the return frame to fall into it (exec does not "call" the program)
    7. free the old address space               (the caller's image is nobody's now)
```

Step 1 comes first for a reason worth stating: it is the only step that can fail. A missing file must leave the caller exactly as it was, so every fallible thing happens before any destructive thing. Once the first page is unmapped there is no caller to return an error to.

---

# A Fresh Space That Shares the Kernel

The new address space needs the kernel in it — the program will trap for every `write` and every keystroke, and the kernel's code and data must be mapped when it does — but it must carry none of the old program's user pages. Chapter 12's `clone_address_space` almost does this: it shares the kernel by reference and copies user pages. `exec` wants the first behaviour without the second, so it uses a near-twin that shares supervisor frames and *skips* user pages entirely, leaving an empty user region to load the new program into.

The kernel half is shared, not copied, and that sharing is what makes the next step survivable. `exec` runs inside a system call, on the calling thread's kernel stack, and that stack — like the kernel's code, the direct map, and the heap — lives in memory mapped identically into every address space. So the thread can load a new page table into `CR3` in the middle of running, and its next instruction, its stack, and the kernel around it are all still there. The lower half changed under its feet; the half it was standing on did not move. Chapter 12's `thread_exit` leaned on exactly this to free an address space while executing in it, and `exec` leans on it to switch address spaces mid-call.

---

# exec Does Not Call the Program. It Returns Into It.

The final move is the one worth slowing down for, because it looks like it should be a jump and is not.

`exec` was entered through `int $0x80`, and every system call ends the same way: the handler returns an interrupt frame, and `iretq` restores it — popping a return address, a stack pointer, and the flags, and dropping back to whoever made the call. That frame is a small record on the kernel stack saying *where to resume*. `exec` does not jump anywhere. It overwrites that record.

```text
    a normal syscall frame            exec's rewritten frame

    rip -> after the int $0x80        rip -> the program's entry
    cs  -> caller's code segment      cs  -> ring 3
    rsp -> caller's user stack        rsp -> the fresh user stack
```

Then it returns like any other handler, and the same `iretq` that would have resumed the caller instead drops into the loaded program, in ring 3, on the new stack. `exec` never returns to its caller — on success there is no caller left to return to. It loaded an image and pointed the return at it, and the machine did the rest on its way back to user space. This is why `exec`, alone among the system calls that touch the frame, keeps company with `fork` and `exit`: all three need the frame itself, because all three are less about computing a value than about changing where the thread wakes up.

---

# A Flat Binary Is the Smallest Honest Program

The program on the disk is a *flat binary*: the raw bytes of the code and its constants, with no header, loaded whole at a fixed address and entered at its first byte. The kernel copies the file to that address and jumps to the top of it. Nothing is parsed, because there is nothing to parse.

That simplicity costs one thing, and naming the cost is the point. A flat binary has no way to say "here is a region that should exist but is not in the file" — no header, no section table, nowhere to record it. So it cannot have a `.bss`: the zero-initialised globals a normal C program takes for granted. `objcopy` drops that section on the way to a flat image, and anything that lived there would arrive as whatever was in the frame. The program in this chapter uses only locals and string constants, and avoids the problem by not having the thing that triggers it.

The format that fixes this is ELF, which carries a table of segments, each saying where it loads, how many bytes come from the file, and how many more should be zeroed past them. Needing `.bss` is one of the reasons ELF exists, and the reason it is the format every real `exec` reaches for. This chapter takes the flat binary because it is the smallest loader that is honestly correct, and because seeing what it cannot do is the clearest possible motivation for the header that can.

Two conventions hold the flat binary together, and both live in its linker script. It is linked at the exact address the kernel loads it — non-relocatable, so its constants resolve to where they actually land — and its `_start` is forced to be the first byte of the image, because with no header the only thing that can name the entry point is the convention *entry equals load address*.

---

# The Process Is the Same Process

The child in the demo forks with one pid and, after `exec`, prints that same pid. That is not a detail to gloss: `exec` replaces a process's *program*, not the process. The identity — the pid, the place in the process table, the relationship to its parent — outlives the image entirely. A shell that runs a hundred commands does not spawn a hundredfold-growing family of identities; it forks and execs, and the child wears each command in turn under whatever pid it was born with. Isolation came from `fork`, a new program came from `exec`, and the thread of identity ran straight through the second unbroken.

---

# Looking Ahead

The kernel can now do the two things a shell is made of. Chapter 14 taught it to read a line. This chapter taught it to run a program named by a string. A shell is a loop that does the first and then the second: read a command, fork, exec it in the child, wait, repeat. Every piece is now on the bench.

That loop is chapter 16, and building it needs one more primitive the demo here quietly did without — a parent's ability to wait for a child in another address space to finish. `thread_join` from chapter 12 works within a process; across the fork boundary the child is detached and the parent cannot wait on it, which is why the parent here simply idles. `wait()` closes that gap, and with it the read-fork-exec-wait loop is a shell.

The debts are the ones every chapter has carried. The mutex, the semaphore, and the keyboard ring are all correct because one processor makes masking into exclusion; `exec` frees an address space it is standing in for the same reason the kernel half is shared. Each of those sentences is about the machine and not the code, and the book ends where a second processor makes several of them false at once.
