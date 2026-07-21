# Chapter 16 (shell) — new for the 64-bit series

This chapter is not in JamesM's original ten, and it is the last. It gives the kernel a **shell**: a program that reads a command, runs it, and prompts for the next — the read-fork-exec-wait loop that has been every Unix session's outer layer since 1971. It adds the one primitive chapter 15 left for here, `wait()`, and then the shell falls out of pieces already on the bench.

**Read `11_fork_64`, `14`, and `15` first.** The shell is `fork` (11) + `exec` (15) + a line read from the keyboard (14), wired into a loop by `wait()`, which is `thread_join` from chapter 12 moved up a level to processes.

```
16_shell_64/
├── make_initrd.c, test*.txt
├── mkinitrd.sh        ← now builds three programs in a loop and packs each
├── user/              ← the ring 3 programs, built on their own
│   ├── ulib.h         ←   NEW: shared user lib (streq, read_line) — DRY
│   ├── sh.c           ←   NEW: the shell (read-fork-exec-wait + builtins)
│   ├── hello.c        ←   NEW: a trivial program for the shell to run
│   ├── prog.c         ←   chapter 15's, refactored onto ulib.h
│   └── user.ld        ←   the generic linker script (was prog.ld)
└── src/
    ├── thread.h/.c    ← + proc_wait(); thread_t carries its pid now
    ├── proc.c         ← fork's child is joinable, not detached
    ├── syscall.c/.h   ← + SYS_WAIT (handled beside fork/exec/exit)
    ├── user.c         ← the boot stub: fork, child execs "sh", parent waits
    ├── main.c         ← launches the shell instead of the fork+exec demo
    └── everything else ← unchanged since chapter 15
```

The scheduler, the heap, paging, the VFS, `exec`, and the context switch are all untouched. `wait()` is not new machinery so much as a second caller of the zombie-and-joiner mechanism `thread_join` has used since chapter 12.

---

## What changed, and why

**`src/thread.c` + `thread.h` — `proc_wait()`, and a pid on every thread.** `proc_wait(pid, &status)` is `thread_join` line for line, with two edits: it finds its target by process id, and it drops join's joinability guards because a child is its parent's to reap. It reuses the same `joiner`/`block()`/`unblock()` path — the parent hangs on the child's wakeup hook and blocks; the child's `thread_exit` wakes it. The one piece of new state is a `pid` field on `thread_t`, stamped at each of the three places a thread is born. A zombie outlives its `process_t` (`thread_exit` frees that when the last thread leaves), so the pid has to ride on the thread for `wait` to still identify the corpse. `DISCUSSION.md` develops this.

**`src/proc.c` — the forked child is joinable.** Chapter 15 set `child->detached = 1` with the note "no join across processes," which is exactly the gap this chapter closes. The child is now `detached = 0` and carries its pid, so its parent can `wait` on it and reap it.

**`src/syscall.c` + `syscall.h` — `SYS_WAIT`.** Handled directly in `syscall_handler`, beside `fork`, `exit`, and `exec`. It does not need the interrupt frame the way those do, but it sits with them rather than in the `syscalls[]` table because `exec` already stepped outside that dense array and the two process-lifecycle calls read better together.

**`user/sh.c` — the shell.** The four-call loop, plus two builtins (`help`, `exit`) it must handle itself because they change the shell rather than run a program. Anything else is forked and exec'd; if `exec` returns, the child reports "command not found" and exits. It is a flat binary on the initrd like any other program.

**`user/ulib.h` — the user-space standard library.** A ring 3 program links against nothing, so the line reader and string compare live here as inline functions and every program includes them. `prog.c` grew its own `read_line` in chapter 15; now there are three programs, so the loop moved here and `prog.c` was refactored onto it.

**`src/user.c` + `main.c` — the boot stub.** The kernel drops a thread into ring 3 that **forks**; the child execs `sh`, and the parent waits on it. The fork matters: this stub runs inside the kernel's own process, and calling `exec` directly would tear the kernel's address space out from under itself. Forking first is what every Unix `init` does, and for the same reason.

---

## Building and running

```bash
cd 16_shell_64
bash mkinitrd.sh        # builds the kernel AND sh/hello/prog, packs the initrd
qemu-system-x86_64 -kernel src/kernel -initrd initrd.img
```

At the `sh>` prompt, type `help`, or a program name (`hello`, `prog`), or `exit`. Captured from a real boot — running `hello`, then `prog` (typing `world`), an unknown command, `help`, and `exit`:

```
Found file sh
Found file hello
Found file prog

tasking up. pid = 1, tid = 2

.user_text 0x108000 .. 0x109000
.user_data 0x109000 .. 0x10a000
starting shell from the initrd...


sh -- a shell loaded from the initrd. Type 'help'.
sh> hello  [hello] hello from a program on the initrd.
  [hello] my pid is 7, and I am about to exit.
sh> prog  [prog] hello -- I am a flat binary from the initrd.
  [prog] pid 9 -- type a line and I will read it: world
  [prog] you said: world
  [prog] exiting.
sh> nope
sh: command not found: nope
sh> help
builtins: help, exit
programs on the initrd: hello, prog
anything else is looked up as a program name.
sh> exit
```

The whole chapter is in the fact that the prompt keeps coming back. Each command runs as its own process — `hello` is pid 7, `prog` is pid 9 — and the shell survives all of them, because it forked instead of exec'ing and waited instead of racing. Run `hello` twice and it gets a new pid each time: a fresh process, not a rerun of the old one.

### A note on verification

The shell exercises `fork`, `exec`, and the new `wait` in a tight loop, moving address spaces around under running threads — the kind of thing that faults if one mapping is wrong. Booted under `qemu -d int`, the full session above — several fork/exec/wait cycles, a failed `exec`, and the shell's own `exit` — completed with no page fault, no general-protection fault, and no double fault. `wait` also does its visible job: the prompt never returns before a command finishes, so no two processes ever write the screen or read the keyboard at once.

One bug is worth recording because the fix is a lesson. An earlier draft had the boot stub `exec` the shell directly instead of forking first. It faulted immediately — `exec` tore down the kernel process's own address space mid-call. Forking first, so a disposable child does the `exec`, is not a workaround; it is why `init` forks, and the fault was the proof.

---

## What this chapter does not do

No arguments: `exec` takes one word, so `hello` runs but `hello world` does not, and there is no `argv`. No pipes, no redirection, no background jobs — each is a rearrangement of the child between fork and exec, and `&` is just the shell choosing not to `wait`. No path search: a bare name is handed to `exec`, which looks only on the initrd. And `wait` finds a child by matching a pid in the thread registry, which is unambiguous only because a forked process has exactly one thread here. These are the debts the whole book carries — correct on one processor, with one thread per process, for one user who is not an adversary. `DISCUSSION.md` names each and where it breaks.

This is the last chapter. The kernel now runs programs it is told to run and gets out of their way, which is what an operating system is.
