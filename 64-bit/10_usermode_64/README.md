# Chapter 10 (User Mode) — 64-bit Port

This is JamesM's chapter 10, ported to x86-64. It builds a Task State Segment, a
ring-3 code segment, a `int $0x80` syscall interface, and drops a task into user
mode where it can do nothing but call the kernel.

**Read `09_multitasking_64/README.md` first.** Chapter 9 ended by listing exactly
what this chapter needs, and most of it you already built.

```
10_usermode_64/
├── make_initrd.c, test*.txt, mkinitrd.sh
└── src/
    ├── descriptor_tables.*  ← 64-bit TSS, its 16-byte descriptor, a DPL-3 gate
    ├── gdt.s               ← ltr
    ├── process.s           ← enter_user_mode: iretq into ring 3
    ├── syscall.c/.h        ← the dispatcher, in plain C
    ├── task.c/.h           ← create_user_task, set_kernel_stack on every switch
    ├── paging.c/.h         ← make_page_user, map_user_page
    ├── user.c              ← the ring-3 program
    ├── link.ld             ← a page-aligned .user_text section
    ├── main.c              ← publishes user pages, launches a user task
    └── everything else     ← unchanged since chapter 9 (19 files)
```

---

## Building and running

```bash
cd 10_usermode_64
bash mkinitrd.sh
qemu-system-x86_64 -kernel src/kernel -initrd initrd.img
```

```
[A pid=1 tick=127]
  [B pid=2]
  [ring 3] hello from pid 3, rsp=0x700000033f98
[A pid=1 tick=130]
  [B pid=2]
  [ring 3] hello from pid 3, rsp=0x700000033f98
```

`[A]` and `[B]` are kernel threads. `[ring 3]` is a userspace program, running on
its own user stack at `0x7000_0003_3f98`, that reaches the screen **only** by
asking the kernel through `int $0x80`. All three are preemptively scheduled.

---

## What you already had

Chapter 9's closing note turned out to be accurate. Here is the promised list,
and where each piece came from:

| Needed for ring 3 | Where it came from |
|---|---|
| ring-3 code and data segments | chapter 4 built `0x1B`/`0x23`, never used them |
| an `iretq` frame to restore | chapter 9's `create_task` already builds one |
| a per-task kernel stack | chapter 9's `task_t.kstack` |
| a place to put the CR3 reload | chapter 9 left a one-line hole in `schedule()` |
| pages the MMU lets ring 3 read | chapter 6's `user` bit, chapter 7's frame allocator |

`create_user_task` is `create_task` with three constants changed:

```c
frame->cs      = 0x1B;          // was 0x08   -- user code, RPL 3
frame->ss      = 0x23;          // was 0x10   -- user data, RPL 3
frame->rflags  = 0x202;         // IF set, IOPL 0
```

That is the whole of "start a process in ring 3." The scheduler does not know or
care which ring a frame targets; `iretq` reads `CS.RPL` and drops privilege
automatically.

---

## The four things that are genuinely new

### 1. The TSS is a different structure

The 32-bit TSS is 104 bytes of saved registers, because the 386 could switch
tasks in hardware by swapping TSSs. **Long mode deleted hardware task switching**,
so the 64-bit TSS keeps none of those registers. What remains is a table of stack
pointers:

```c
struct tss_entry_struct {
    u32int reserved0;
    u64int rsp0, rsp1, rsp2;      // stack to load on entry to ring 0/1/2
    u64int reserved1;
    u64int ist1, ist2, ..., ist7; // the Interrupt Stack Table
    u64int reserved2;
    u16int reserved3;
    u16int iomap_base;
} __attribute__((packed));        // exactly 104 bytes, for different reasons
```

Only `rsp0` matters to us: it is the kernel stack the CPU loads when ring 3 takes
an interrupt. The `ist` entries are the mechanism chapter 4 mentioned — a gate
with a nonzero `ist` switches stacks unconditionally, which is how you survive a
`#DF` after a stack overflow.

`iomap_base = sizeof(tss_entry)` sets the I/O bitmap limit past the end of the
segment, meaning "no bitmap," meaning every port is denied to ring 3.

### 2. Its GDT descriptor is 16 bytes

A code or data descriptor is 8 bytes. A **system** descriptor — which a TSS is —
is 16, because it holds a full 64-bit base. So it occupies two GDT slots:

```c
gdt_entry_t gdt_entries[7];   // 5 usable segments + a 2-slot TSS descriptor
```

`write_tss` casts `&gdt_entries[5]` to a 16-byte `tss_descriptor_t` and fills in
`base` across four fields, `access = 0x89` (present, type 9 = available 64-bit
TSS). Then `gdt.s` loads it:

```nasm
tss_flush:
    mov ax, 0x28      ; index 5 << 3, RPL 0
    ltr ax
    ret
```

The 32-bit tutorial loads `0x2B` (RPL 3). That is wrong for `ltr`, which requires
RPL 0; the RPL of a TR selector is not meaningful.

### 3. Entering ring 3 is a fake `iretq`

There is no "switch to user mode" instruction and no far jump. The only way down
a privilege level is to return from an interrupt you never took. Build the frame
`iretq` wants, with a code selector whose RPL is 3:

```nasm
enter_user_mode:            ; (rip in rdi, rsp in rsi)
    cli
    mov ax, 0x23
    mov ds, ax              ; (es, fs, gs too -- but NOT ss)
    push qword 0x23         ; ss
    push rsi                ; rsp
    pushfq
    pop rax
    or  eax, 0x200          ; IF: keep the timer alive in ring 3
    and eax, 0xFFFFCFFF     ; IOPL = 0: no port access
    push rax                ; rflags
    push qword 0x1B         ; cs  (RPL 3)
    push rdi                ; rip
    iretq
```

This is chapter 4's `retfq` trick, one ring lower. `iretq` pops `RIP`, `CS`,
`RFLAGS`, `RSP`, `SS` and — seeing `CS.RPL = 3` — drops to ring 3 atomically.

Note we deliberately do **not** load `SS` by hand. `iretq` loads it from the
frame, and it must arrive with RPL 3 or the return faults.

### 4. The syscall dispatcher is plain C

Here is the entire thing:

```c
regs->rax = fn(regs->rdi, regs->rsi, regs->rdx, regs->rcx, regs->r8, regs->r9);
```

Compare the 32-bit tutorial, which hand-writes five `push`es, a `call`, and five
`pop`s in inline assembly, because 32-bit cdecl passes arguments on the stack —
and the stack in question belongs to ring 3, which the kernel must not trust.

On x86-64 the System V ABI passes the first six arguments in registers, which the
interrupt frame already captured. So dispatch is a function-pointer call. A
handler that wants fewer arguments simply ignores the extra registers.

This is also, not coincidentally, why real x86-64 kernels find `syscall` cheap.

---

## The security bug this fixes

The 32-bit tutorial enables user mode by ORing `0x60` into the flags of **every**
IDT gate:

```c
idt_entries[num].flags = flags | 0x60;   // DPL 3 on all 49 gates
```

`0x60` sets the gate's DPL to 3, permitting ring 3 to invoke it with `int`. Doing
it to every gate means a user program can execute:

- `int $0x0e` — forge a page fault
- `int $0x08` — forge a double fault
- `int $0x20` — forge a timer tick, and **drive the scheduler by hand**

Only the syscall vector should be reachable from ring 3:

```c
idt_set_gate(128, (u64int)isr128, 0x08, 0xEE);   // 0x8E | 0x60: this gate only
// every other gate stays 0x8E -- DPL 0
```

I tested that the fix holds. A ring-3 task executing `int $0x0e`:

```
v=0e e=0000 i=1 cpl=3    <- QEMU logs the attempted software interrupt
v=0d e=0072 i=0 cpl=3    <- the CPU's actual response: #GP
```

Error code `0x72` is selector `(14<<3)|2` — "you tried to reach vector 14 without
the privilege to." The forgery is denied at the hardware level. On the tutorial's
kernel, that same instruction runs the page-fault handler.

---

## What "user mode" actually protects — tested three ways

A `[ring 3]` line printing on screen proves the transition works. It does not
prove the *isolation* works. So I pointed a hostile task at three things it must
not be able to do:

**Write kernel memory.** `*(u64int *)0x101000 = 0` from ring 3:

```
v=0e e=0007 cpl=3 CR2=0000000000101000
```

`#PF`, error `0x7` = present + write + user. The kernel's `.text` is mapped
supervisor-only, so the write is refused. `0x101000` is never modified.

**Touch an I/O port.** `outb` from ring 3:

```
v=0d cpl=3
```

`#GP`, because IOPL is 0 and the TSS I/O bitmap is empty. Ring 3 cannot talk to
hardware directly — it has to ask the kernel.

**Forge an interrupt.** Shown above: `#GP`, not the forged handler.

All three denials come from the CPU, not from any check the kernel wrote. That is
the point of user mode: the hardware enforces the boundary, and the kernel only
has to set the bits correctly.

---

## A subtle build trap: `static inline` is not `always_inline`

This one cost me a triple fault and is worth the warning.

The syscall stubs in `syscall.h` are `static inline`. The ring-3 program calls
them. Everything the ring-3 program touches must live in a user-accessible page —
that is why `user.c` puts its code in `.user_text`.

But `static inline` is only a *hint*. At `-O0`, or when GCC decides a function is
too big, it emits the stub as a real function **in `.text`** — a supervisor page.
The ring-3 program then `call`s into kernel memory and faults on the instruction
fetch:

```
v=0e e=0005 cpl=3 IP=...105432   <- inside syscall_monitor_write, in .text
```

The fault is *correct*: ring 3 tried to execute a supervisor page. The bug is
that the syscall stub was there at all. The fix is to force it:

```c
static inline __attribute__((always_inline)) u64int syscall_##fn(...)
```

Now the stub is copied into `user_task`'s own page, and there is no cross-ring
call. The general lesson: **when code must live in a particular section, `inline`
is a wish and `always_inline` is a requirement.** The same applies to string
literals — a bare `"hello"` lands in `.rodata` (supervisor), so `user.c` puts its
strings in `.user_data`.

---

## Things to try

1. **Watch the ring transition in the monitor.** Boot with `-d int`, find the
   first `v=80`, and read `cpl` on the line before and after. You will see
   `cpl=3` calling in and `cpl=0` servicing. That single pair of numbers is the
   entire security model.

2. **Attack your own kernel.** Point `create_user_task` at a task that writes to
   `0x101000`, reads the kernel heap, or runs `cli`. Confirm each one faults. This
   is the most reassuring thing you will do in the whole tutorial.

3. **Add a real syscall.** Wire `read_fs` into the dispatcher so ring 3 can read
   a file from the initrd. Now you have to face pointer validation: the user hands
   you a buffer address, and you must check it is a mapped user page before you
   write to it. `syscall.c` names this gap; fill it.

4. **Give the syscall stub its own page properly.** Rather than inlining, put the
   stubs in `.user_text` explicitly and take the `always_inline` off. Confirm they
   land in a user page and the cross-ring call works. Two ways to solve the same
   problem; understand both.

5. **Return to the kernel.** The user task here loops forever. Add a
   `sys_exit` that removes the current task from the ready queue and yields.
   Watch the scheduler carry on with one fewer task.

---

## Where this leaves the tutorial

You now have a 64-bit kernel that boots via GRUB or `qemu -kernel`, sets up long
mode from scratch, handles interrupts and faults, manages physical and virtual
memory with a four-level page table and a real heap, reads a filesystem from an
initrd, preemptively schedules multiple tasks, and runs an isolated userspace
program that can only reach the kernel through a system call.

That is a complete, if minimal, operating system, and it is the end of JamesM's
ten chapters.

The honest gaps, in rough order of how much they will teach you:

- **`fork()` and per-process address spaces.** The single biggest missing piece.
  It needs a `clone_pml4` that copies the kernel half by reference and the user
  half by copy-on-write, and a CR3 reload in the one-line hole `schedule()` still
  leaves. Chapter 9's note on `copy_page_physical` applies: the direct map means
  you copy a frame with a plain `memcpy`, never disabling paging.
- **Pointer validation at the syscall boundary.** Noted in `syscall.c`. Until you
  do this, the kernel is a confused deputy.
- **`syscall`/`sysret`** instead of `int $0x80`. Faster, and what real x86-64
  kernels use. It needs the `STAR`, `LSTAR` and `SFMASK` MSRs and a slightly
  different register discipline (it clobbers `RCX` and `R11`).
- **A real memory map** from the multiboot info, replacing the hardcoded 16 MiB.
- **The IST**, for a double-fault handler that survives a stack overflow. The
  field is already in your TSS, waiting.

Each of those is a weekend, and each is more interesting than the last.
