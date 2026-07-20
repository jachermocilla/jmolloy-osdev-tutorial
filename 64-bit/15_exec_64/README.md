# Chapter 15 (exec) — new for the 64-bit series

This chapter is not in JamesM's original ten. It gives the kernel `exec()`: a running process can stop being what it is and become a program loaded from a file. Paired with chapter 11's `fork()`, it is how Unix has always started programs — fork to make a process, exec to change what it runs — and it is the first time in this book that a program lives on the disk rather than inside the kernel image.

**Read `11_fork_64/README.md` and `13`–`14` first.** `exec` is `fork`'s missing partner, and the demo builds on the semaphore-backed keyboard from chapter 14.

```
15_exec_64/
├── make_initrd.c, test*.txt
├── mkinitrd.sh        ← now also builds user/prog.bin and packs it as "prog"
├── user/              ← NEW: the program exec loads, built on its own
│   ├── prog.c         ←   a freestanding ring 3 program (reads a line)
│   └── prog.ld        ←   links it flat at USER_LOAD_BASE, _start first
└── src/
    ├── exec.c/.h      ← NEW: the loader (fresh space, load, rewrite the frame)
    ├── paging.c/.h    ← + new_kernel_address_space(): share kernel, no user pages
    ├── syscall.c/.h   ← + SYS_EXEC (handled like fork/exit: it needs the frame)
    ├── main.c         ← launches the ring 3 fork+exec program
    ├── user.c         ← the fork+exec driver
    └── everything else ← unchanged since chapter 14
```

The scheduler, the heap, the VFS, and the context switch are all untouched. `exec` is assembled from parts already present: `clone_address_space`'s share-the-kernel logic (in a variant that drops user pages), the VFS's `finddir_fs`/`read_fs`, `get_page`/`alloc_frame` to map the image, and the ring-3 interrupt frame `create_user_task` already knew how to build.

---

## What changed, and why

**`user/prog.c` + `prog.ld` — the first program that is not the kernel.** A small freestanding ring 3 program, compiled and linked on its own, flattened to a raw binary with `objcopy -O binary`, and packed into the initrd as `prog`. It is linked at `USER_LOAD_BASE` (non-relocatable, so its constants resolve) with `_start` forced to the first byte (a flat binary has no header, so entry can only be "the start of the image"). It has no `.bss` on purpose — a flat binary cannot carry one — which `DISCUSSION.md` uses to explain why ELF exists.

**`exec.c/.h` — the loader.** `do_exec` reads the file, builds a fresh address space that shares the kernel but has no user pages, switches into it, maps and copies the program, maps a clean user stack, and rewrites the syscall's interrupt frame so the returning `iretq` falls into the new program in ring 3. On success it does not return; on a missing file it returns −1 with the caller untouched, which is why the file is read before anything is destroyed.

**`paging.c/.h` — `new_kernel_address_space`.** A near-twin of `clone_address_space`: it shares the whole kernel half by reference and shares lower-half supervisor frames, but skips user leaves instead of copying them. The result is a valid kernel view with an empty user region for `exec` to fill.

**`syscall.c/.h` — `SYS_EXEC`.** Handled directly in `syscall_handler`, next to `fork` and `exit`, because like them it needs the interrupt frame rather than just an argument. The path pointer is trusted unchecked, the same shortcut in place since chapter 12.

**`main.c` + `user.c` — the demo.** A ring 3 program forks; the child execs `prog` and becomes it; the parent prints the child's pid and idles. The loaded program reads the keyboard through chapter 14's syscall, so it is a first-class ring 3 process that happens to have arrived as a file.

---

## Building and running

```bash
cd 15_exec_64
bash mkinitrd.sh        # builds the kernel AND user/prog.bin, packs the initrd
qemu-system-x86_64 -kernel src/kernel -initrd initrd.img
```

Type a line when `prog` asks for one. Captured from a real boot, after typing `world`:

```
Found file test.txt
Found file test2.txt
Found file prog

tasking up. pid = 1, tid = 2

fork + exec: a ring 3 program forks; the child becomes a file.
launched tid 4

  [parent] forking...
  [parent] child has pid 5
  [child] now exec("prog")
  [prog] hello -- I am a flat binary from the initrd.
  [prog] pid 5 -- type a line and I will read it: world
  [prog] you said: world
  [prog] exiting.
```

Two details in that output are the whole chapter. `Found file prog` is a program sitting on the filesystem, which no earlier chapter could run. And the loaded program reports `pid 5` — the same pid `fork` gave the child — because `exec` replaced the child's *program*, not the child. Identity ran straight through the transformation.

### A note on verification

`exec` moves an address space out from under a running thread, and that is the kind of thing that faults if a single mapping is wrong. Booted under `qemu -d int`, the full fork → exec → run → read-a-line → exit sequence completed with no page fault, no general-protection fault, and no double fault — the `CR3` switch mid-syscall, the frame rewrite, and the teardown of the old space all held. The keyboard path from chapter 14 is exercised inside the loaded program, proving it is a complete ring 3 process and not a special case.

---

## What this chapter does not do

No `argv`/`argc` — the program takes no arguments. No ELF, so no `.bss` and no separate segments (`DISCUSSION.md` explains the trade). And no `wait()`: `fork` makes the child detached, so the parent cannot wait on it and simply idles. That last gap is the one thing between this chapter and a shell. Chapter 16 closes it — `wait()`, and then the read-fork-exec-wait loop that is a shell — using every piece chapters 8 through 15 have put on the bench.
