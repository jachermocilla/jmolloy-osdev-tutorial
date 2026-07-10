# Chapter 3 (Screen) — 64-bit Port

This is JamesM's chapter 3 kernel, ported from 32-bit x86 to x86-64 (long mode).

It prints `Hello, world!` to the screen. That's it. The interesting part isn't
what it does — it's what had to change to get there.

```
03_screen_64/src/
├── boot.s      ← rewritten (the whole port lives here)
├── link.ld     ← rewritten
├── Makefile    ← rewritten
├── common.h    ← two typedefs added
├── common.c    ← unchanged
├── main.c      ← unchanged
├── monitor.c   ← unchanged
└── monitor.h   ← unchanged
```

---

## Building and running

```bash
cd 03_screen_64/src
make
qemu-system-x86_64 -kernel kernel
```

You should see `Hello, world!` in the top-left corner.

> **`qemu-system-i386` will not work.** It emulates a 32-bit CPU, which has no
> long mode. You'll get a blank screen and no error message. This is the single
> most common way to waste an afternoon on this port.

If you're using the repo's floppy workflow, change the run command the same way:

```bash
./update.sh 03_screen_64
qemu-system-x86_64 -fda floppy.img -boot a
```

---

## The core problem

Here is the situation when GRUB jumps to your kernel:

- The CPU is in **32-bit protected mode**.
- **Paging is off.**

And here is what long mode requires:

- **Paging is on.** Not optional. There is no such thing as 64-bit mode without
  paging.

So you can't just recompile with `-m64` and hope. Before a single line of
64-bit code can run, you have to hand-build a page table, turn paging on, and
switch the CPU into a 64-bit code segment. All of that has to happen in 32-bit
assembly, because you're not in 64-bit mode yet. Chicken, meet egg.

That bootstrap is the entire content of this chapter's port.

---

## The bootstrap, step by step

All of this is in `boot.s`. Read it alongside this section.

### Step 0: Check the CPU can even do this

```nasm
    mov     eax, 0x80000001
    cpuid
    test    edx, 1 << 29            ; the "LM" (long mode) bit
    jz      .no_long_mode
```

Every x86-64 CPU made in the last twenty years passes this. But if you skip the
check and run on a machine that doesn't support long mode, the failure mode is
a silent triple fault and a reboot loop — impossible to debug. Ten lines of
assembly buys you an error message instead.

### Step 1: Build a page table

x86-64 uses **four levels** of page table. From the top:

```
PML4  →  PDPT  →  PD  →  PT
```

Each level is a 4096-byte table of 512 eight-byte entries. Walking all four
levels gets you 4 KiB pages. But there's a shortcut: if you set the **page-size
bit (bit 7)** in a PD entry, the walk stops there and that entry maps a single
**2 MiB page** directly. No PT needed.

We use that shortcut. One PML4, one PDPT, one PD with all 512 entries filled:

```nasm
    mov     ecx, 512
    mov     eax, 0x83               ; present | writable | page-size
    mov     edi, pd
.map_pd:
    mov     [edi], eax
    add     eax, 0x200000           ; next 2 MiB frame
    add     edi, 8                  ; next 64-bit entry
    loop    .map_pd
```

512 × 2 MiB = **1 GiB, identity-mapped**. "Identity-mapped" means virtual
address `X` maps to physical address `X`. The kernel at 1 MiB stays at 1 MiB,
the VGA buffer at `0xB8000` stays at `0xB8000`, and nothing you wrote in
chapter 3 notices that paging got turned on underneath it.

Note the `0x83`: bit 0 = present, bit 1 = writable, bit 7 = page-size.

> **A subtlety worth understanding.** The page tables live in `.bss`. Multiboot
> loaders are *supposed* to zero `.bss` for you. But if even one unwritten entry
> happened to contain garbage with bit 0 set, the CPU would treat it as a valid
> mapping to a random physical address and you'd triple fault the instant
> paging came on. So `boot.s` zeroes the three tables explicitly with `rep
> stosd` rather than trusting the loader. Defensive, cheap, and it removes a
> whole class of "works on QEMU, dies on real hardware" bug.

### Step 2: Enable PAE

Long mode's page tables use 64-bit entries, which is the format introduced by
**PAE** (Physical Address Extension). So PAE must be on first:

```nasm
    mov     eax, cr4
    or      eax, 1 << 5             ; CR4.PAE
    mov     cr4, eax
```

### Step 3: Point CR3 at the PML4

`CR3` is the register that tells the MMU where the top-level page table lives.

```nasm
    mov     eax, pml4
    mov     cr3, eax
```

### Step 4: Set the long-mode-enable bit

This one isn't a normal register. It's a bit in a **Model-Specific Register**
(MSR), reached with the `rdmsr`/`wrmsr` instruction pair. The MSR is called
`IA32_EFER` and lives at index `0xC0000080`.

```nasm
    mov     ecx, 0xC0000080         ; IA32_EFER
    rdmsr                           ; reads into EDX:EAX
    or      eax, 1 << 8             ; EFER.LME
    wrmsr                           ; writes back from EDX:EAX
```

`rdmsr` and `wrmsr` both use `ECX` to select which MSR, and `EDX:EAX` as the
64-bit value. Setting `LME` says "I *intend* to enter long mode." It doesn't
happen yet.

### Step 5: Turn paging on

```nasm
    mov     eax, cr0
    or      eax, 1 << 31            ; CR0.PG
    mov     cr0, eax
```

The moment `PG` goes high with `LME` already set, the CPU enters **compatibility
mode** — a sort of halfway house where you're technically in long mode but
still executing 32-bit code, because your current code segment is a 32-bit one.

You can verify this happened: after boot, QEMU's monitor shows
`EFER=0x500`. Bit 8 is `LME` (you set it) and bit 10 is `LMA` (long mode
*active* — the CPU set that one for you).

### Step 6: Far-jump into a 64-bit code segment

The last step. You need a GDT with a code descriptor that has the **L bit**
(bit 53) set, marking it as a 64-bit segment:

```nasm
gdt64:
    dq 0                                     ; null descriptor
.code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53) ; executable, code/data, present, 64-bit
```

Then load it and jump:

```nasm
    lgdt    [gdt64.pointer]
    jmp     gdt64.code:long_mode_start
```

The instruction *after* that far jump is the first 64-bit instruction your
kernel executes.

### Step 7: Call `main`

```nasm
[BITS 64]
long_mode_start:
    mov     rsp, stack_top
    xor     rdi, rdi
    mov     edi, [mboot_ptr]        ; struct multiboot *mboot_ptr
    call    main
```

Note `RDI`, not a `push`. In 32-bit code, arguments go on the stack — that's
why the original `boot.s` said `push ebx`. The **System V AMD64 ABI** passes
the first six integer arguments in registers instead: `RDI, RSI, RDX, RCX, R8,
R9`. So the multiboot pointer goes in `RDI`.

You'll trip over this calling-convention difference repeatedly. Get used to it now.

---

## The build system trick

Here's a real problem. The Multiboot 1 specification — which is what GRUB
Legacy and `qemu -kernel` implement — only knows how to parse **ELF32**
headers. Hand it an ELF64 file and it refuses to load it at all.

But our kernel *is* 64-bit code. So what do we do?

The answer: link as ELF64 (so the linker resolves 64-bit relocations correctly),
then **rewrite the container format** to ELF32:

```make
link:
	ld $(LDFLAGS) -o kernel64.elf $(SOURCES)
	objcopy -I elf64-x86-64 -O elf32-i386 kernel64.elf kernel
```

`objcopy` rewrites the ELF *headers* — the metadata describing where the
sections go — while leaving the actual bytes of code and data untouched. The
loader reads the ELF32 header, copies the sections to 1 MiB, and jumps to the
entry point. It has no idea (and no need to know) that the machine code it just
loaded is a mixture of 32-bit and 64-bit instructions.

This works because **every address in our kernel is below 4 GiB**. Load address
`0x100000` fits comfortably in a 32-bit ELF field. Nothing is lost in the
translation. If you later relocate the kernel to the top of the address space
(`0xFFFFFFFF80000000`, the conventional "higher half" for x86-64), this trick
stops working and you need a different approach.

---

## Compiler flags, and why each one matters

```make
CFLAGS=-m64 -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
       -fno-pie -fno-pic -ffreestanding -mno-red-zone \
       -mno-mmx -mno-sse -mno-sse2 -Wall
```

The ones that are new or that changed meaning:

**`-mno-red-zone`** — The AMD64 ABI reserves the 128 bytes *below* `RSP` as
scratch space that a leaf function may use without adjusting the stack pointer.
This is a nice optimization in userspace. In a kernel it is a disaster: when an
interrupt fires, the CPU pushes the interrupt frame at `RSP` and downward,
straight through the red zone, silently corrupting whatever the interrupted
function had stashed there. There is no red zone in 32-bit x86, so this flag
has no 32-bit counterpart. **You will not notice the bug until chapter 4**,
when interrupts start firing, and then it will present as impossible random
corruption.

**`-mno-sse -mno-sse2 -mno-mmx`** — On x86-64, GCC *assumes* SSE is available
(it's part of the base architecture) and will happily emit SSE instructions for
things as mundane as copying a struct. But the FPU/SSE unit hasn't been
initialized yet, so the first such instruction faults. Again: no 32-bit
counterpart, because 32-bit GCC doesn't assume SSE.

**`-fno-pie -fno-pic`** — Modern GCC builds position-independent code by
default, which means globals are reached through a register holding the GOT
base. A kernel loaded at a fixed address doesn't want or need that indirection.
(This same flag, or its absence, is the cause of a genuinely nasty bug in the
32-bit chapter 9 — see that chapter's notes.)

**`-mcmodel`** — deliberately *not* set. The default (`small`) assumes all
symbols live in the low 2 GiB, which is true for us at 1 MiB. Do **not** reach
for `-mcmodel=kernel`; that model assumes the kernel is in the *top* 2 GiB.

---

## What didn't change

`main.c`, `monitor.c`, and `common.c` are **byte-for-byte identical** to the
32-bit originals.

This is worth pausing on. Writing to the VGA text buffer at `0xB8000` and
talking to the VGA controller with `outb` don't care what mode the CPU is in.
`u16int *video_memory = (u16int *)0xB8000;` is a perfectly good 64-bit pointer,
because we identity-mapped that address.

The only C change anywhere is two added typedefs in `common.h`:

```c
typedef unsigned long long u64int;
typedef          long long s64int;
```

`u32int` is still `unsigned int` — that's still 32 bits under the LP64 model
Linux and GCC use. What changed size is `long` and, more importantly, **pointers**.

---

## Things to try

1. **Break it deliberately.** Comment out the `mov cr0, eax` line that enables
   paging and watch what happens. Then comment out the `lgdt` instead. Get a
   feel for what each failure looks like, because you'll see them again.

2. **Prove you're really in long mode.** Run with QEMU's monitor
   (`qemu-system-x86_64 -kernel kernel -monitor stdio`), type `info registers`,
   and look at `EFER` and `CR4`. You want `EFER=0x500` and `CR4` bit 5 set.

3. **Print a 64-bit value.** `monitor_write_hex` takes a `u32int`. Write a
   `monitor_write_hex64` and use it to print the address of `main`. Confirm
   it's around `0x101000`.

4. **Understand the 2 MiB limit.** The kernel identity-maps exactly 1 GiB. What
   happens if you dereference a pointer to `0x40000000`? Try it. (Then read
   chapter 4, where you'll actually be able to *see* the resulting page fault
   instead of just rebooting.)

---

## Next

Chapter 3 is the easy one — the port is confined almost entirely to `boot.s`.
Chapter 4 is where the C code starts to change: 16-byte IDT entries, no
`PUSHA`, no far jumps, and a calling convention that forces a redesign of how
interrupt handlers receive their arguments.
