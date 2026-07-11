# Appendix B - A Guided Tour of the `03_screen_64` Kernel

## Reading, Understanding, Building, and Modifying Your First 64-bit Kernel

The chapters of this book introduce one idea at a time. This appendix does the opposite: it takes a single, complete, working kernel and walks through it end to end, showing where each idea actually lives in real code. The kernel we study is the screen chapter of the 64-bit tutorial, found at

```text
64-bit/03_screen_64
```

It does exactly one thing — it prints `Hello, world!` in the top-left corner of the screen. That modest output hides a surprising amount of machinery, and almost every concept from the book appears somewhere inside it. By the end of this tour you should be able to open the directory, read every file with understanding, build and run the kernel, and change it deliberately rather than by guessing.

Wherever a topic deserves fuller treatment, the relevant chapter is named. Read this appendix with the repository open in one window and the referenced chapters within reach.

---

## Learning Objectives

After completing this guided tour, you should be able to

* navigate the `03_screen_64` directory and explain the purpose of every file,
* connect each file to the chapters of this book that explain it,
* trace execution from the bootloader's handoff all the way to `main()`,
* explain why a "64-bit" kernel begins life running 32-bit code,
* build the kernel and run it under an emulator,
* modify the kernel safely and predict the result before you compile,
* recognize the failure signatures of the most common mistakes.

---

## B.1 What This Kernel Is, and What It Is Not

This kernel is a *port*. It began as the 32-bit screen chapter of James Molloy's well-known tutorial and was rewritten to run in **long mode**, the 64-bit execution mode of the x86-64 processor. The README states the essential fact plainly: almost the entire port lives in one file, `boot.s`. The C code — the screen driver, the entry point, the port helpers — is nearly identical to the 32-bit original.

That single sentence tells you where the difficulty is. The interesting part of this kernel is not *printing text*; printing text is old news. The interesting part is everything that must happen **before** a single line of 64-bit C code can run. Understanding that bootstrap is the real reward of this chapter, and it exercises Chapter 7 (paging), Chapter 9 (bit manipulation), and Chapter 10 (linking C and assembly) all at once.

Keep your expectations calibrated. When you finally see `Hello, world!` appear, the achievement is not the greeting. The achievement is that the processor is now in 64-bit mode, paging is on, a stack exists, and C code is executing — and you understand how it got there.

---

## B.2 First, Map the Territory

Chapter 13 gives one instruction before all others: **start with the directory structure, not with the code**. Follow it now. List the files:

```text
03_screen_64/
├── README.md      ← read this first
├── DISCUSSION.md   ← the concepts behind the port
└── src/
    ├── boot.s      ← the entire 64-bit bootstrap
    ├── link.ld     ← where the kernel goes in memory
    ├── Makefile    ← how the pieces become one file
    ├── common.h    ← typedefs and hardware helpers (declarations)
    ├── common.c    ← hardware helpers (definitions)
    ├── main.c      ← the C entry point
    ├── monitor.c   ← the screen driver
    └── monitor.h   ← the screen driver's interface
```

Nine files, and each has a single responsibility. Chapter 13 §13.4 tells you to read the README before the implementation, and here that advice pays off immediately: the README explains that only four files were rewritten for the port (`boot.s`, `link.ld`, `Makefile`, and two typedefs in `common.h`) and that `main.c`, `monitor.c`, and `common.c` are essentially untouched. That single paragraph saves you from hunting for 64-bit magic in files that contain none.

Before opening anything, build a mental map (Chapter 13 §13.5) by answering four questions:

1. **What problem does this kernel solve?** Reaching 64-bit C and printing to the screen.
2. **What is its public surface?** The three `monitor_*` functions in `monitor.h`.
3. **What data does it define?** The VGA buffer pointer, the cursor position, and three page tables.
4. **What depends on what?** `main.c` depends on `monitor.c`, which depends on `common.c`, all of which depend on `boot.s` having already reached long mode.

Hold that map in your head. Everything below fills it in.

---

## B.3 Trace the Story Before Reading the Files

Chapter 13 §13.6 warns that *programs execute, files do not*, and that following the flow of execution is easier than reading files in isolation. Here is the whole story of this kernel as a sequence, which we will spend the rest of the appendix expanding:

```text
GRUB / QEMU hands off
        │  (32-bit protected mode, paging OFF)
        ▼
start:  in boot.s        ← 32-bit assembly
        │  check CPU, build page tables, enable long mode
        ▼
long_mode_start:         ← first 64-bit instruction
        │  set up stack, pass multiboot pointer
        ▼
main()  in main.c        ← ordinary C at last
        │
        ▼
monitor_clear(), monitor_write("Hello, world!")
        │
        ▼
hlt (idle forever)
```

Notice that `main()` is not the beginning. This surprises students who have only written hosted programs, and it is exactly the surprise Chapter 12 §12.4 prepares you for: *if there is no operating system, where does execution begin?* The answer here is the label `start` in `boot.s`, named as the entry point by the linker script. We will return to this.

---

## B.4 The Type Foundation: `common.h`

Open `common.h`. It is the smallest file with the widest reach, and it is pure Chapter 1.

```c
#ifndef COMMON_H
#define COMMON_H

// These typedefs are written for 64-bit X86 (LP64).
typedef unsigned long long u64int;
typedef          long long s64int;
typedef unsigned int   u32int;
typedef          int   s32int;
typedef unsigned short u16int;
typedef          short s16int;
typedef unsigned char  u8int;
typedef          char  s8int;

void outb(u16int port, u8int value);
u8int inb(u16int port);
u16int inw(u16int port);

#endif // COMMON_H
```

Everything in this file was explained earlier in the book:

* The fixed-width typedefs — `u8int` through `u64int` — are the subject of **Chapter 1** (see §1.3) and Appendix A §A.2. The kernel defines its own names rather than relying on ordinary `int` because, as §1.2 argued, the hardware demands exact sizes and the C standard refuses to promise them.
* The comment *"written for 64-bit X86 (LP64)"* points at **Chapter 1 §1.5**, the LP64 data model. Under LP64, `int` stays 32 bits but `long` and pointers grow to 64. That is why the port only had to *add* `u64int` and `s64int`: the narrower types were already correct.
* The three lines `void outb(...)`, `u8int inb(...)`, `u16int inw(...)` are **declarations, not definitions** — the distinction drawn in **Chapter 11 §11.3**. They promise these functions exist; `common.c` will deliver them.
* The `#ifndef COMMON_H` / `#define COMMON_H` / `#endif` wrapper is an **include guard**, explained in **Chapter 11 §11.6**. It prevents the typedefs from being defined twice when several files include this header.

This one header is a compact review of Chapters 1 and 11. If any line looks unfamiliar, that is a signal to reread the corresponding section before continuing.

---

## B.5 Talking to Hardware: `common.c`

`common.c` defines the functions that `common.h` promised. Its first three functions are the beating heart of **Chapter 4**.

```c
void outb(u16int port, u8int value)
{
    asm volatile ("outb %1, %0" : : "dN" (port), "a" (value));
}

u8int inb(u16int port)
{
    u8int ret;
    asm volatile("inb %1, %0" : "=a" (ret) : "dN" (port));
    return ret;
}
```

Read these alongside Chapter 4:

* `outb` is the exact example dissected in **Chapter 4 §4.4**. It sends one byte to an I/O port, a thing C alone cannot express, which is why §4.1 argued that "C is not enough."
* The strange punctuation `: : "dN" (port), "a" (value)` is the GCC inline-assembly constraint syntax broken down in **§4.5**. The `"a"` binds a value to the `AL`/`AX` register; `"dN"` allows the port number in `DX` or as an immediate.
* The keyword `volatile` is not decoration. **§4.6** explains that it forbids the compiler from optimizing the instruction away or reordering it, because the effect of talking to hardware is invisible to the compiler's model of the program.
* `inb` reads a byte *back* from a port — the mirror image, covered in **§4.7** — using an output constraint `"=a" (ret)`.

Below these three functions you will find something important for later chapters:

```c
void memcpy(u8int *dest, const u8int *src, u32int len)
{
    // TODO: implement this yourself!
}
```

`memcpy`, `memset`, `strcmp`, `strcpy`, and `strcat` are present but **empty**. This is not an oversight; it is **Chapter 12** made visible. §12.5 asks *"Who provides `memcpy()`?"* and answers that in a freestanding environment, *you* do — there is no standard library. This kernel does not yet need them, so they are left as stubs for you to complete (see the exercises). When you build, the compiler will warn that the string functions "reach the end of a non-void function"; that warning is Chapter 12's point arriving on schedule.

---

## B.6 The Screen Driver: `monitor.c`

The screen driver is where Chapters 3, 4, and 9 meet. Open `monitor.c` and read its first two lines of data:

```c
// The VGA framebuffer starts at 0xB8000.
u16int *video_memory = (u16int *)0xB8000;
```

This single declaration is a small anthology of the book:

* Treating the constant `0xB8000` as a **pointer** is **Chapter 3 §3.6** ("hardware is memory too") and **§3.10** ("casting addresses"). The cast `(u16int *)0xB8000` tells the compiler: *interpret this integer as the address of an array of 16-bit cells.*
* That the number is written in **hexadecimal** is Chapter 1 §1.7 and Chapter 9 §9.4 — hardware addresses are almost always written in hex because their bit structure matters.
* That an **address is just data** you can assign to a variable is **Chapter 1 §1.8**.

Each screen cell is a 16-bit value: the low byte is the character, the high byte is a colour attribute. Look at how a character is placed:

```c
u8int  attributeByte = (backColour << 4) | (foreColour & 0x0F);
u16int attribute = attributeByte << 8;
...
location = video_memory + (cursor_y*80 + cursor_x);
*location = c | attribute;
```

Every operator here has a home in the book:

* `backColour << 4` and `attributeByte << 8` are **bit shifts** — **Chapter 9 §9.6**. Shifting the background colour into the high nibble, then the attribute byte into the high byte, assembles the 16-bit cell piece by piece.
* `foreColour & 0x0F` is a **mask** (**Chapter 9 §9.7**) that keeps only the low four bits.
* `c | attribute` uses **OR to combine** the character and its colour into one word (**Chapter 9 §9.5**), the same idiom §9.8 uses for setting flags.
* `video_memory + (cursor_y*80 + cursor_x)` is **pointer arithmetic** (**Chapter 3 §3.7-3.8**). Because `video_memory` is a `u16int *`, adding one advances by two bytes — one cell — automatically.
* `*location = ...` is a **dereference** (**Chapter 3 §3.4**) that writes directly into video memory. There is no `printf`; the write *is* the output.

The tab-handling line is a lovely bit-trick worth pausing on:

```c
cursor_x = (cursor_x+8) & ~(8-1);
```

`~(8-1)` is `~0x07`, a mask with the low three bits cleared (**Chapter 9 §9.5** NOT and §9.7 masks). ANDing with it rounds the cursor down to the nearest multiple of eight — a tab stop — without a single division.

Finally, `move_cursor()` reaches back to Chapter 4:

```c
outb(0x3D4, 14);
outb(0x3D5, cursorLocation >> 8);
```

Here the driver uses the `outb` from `common.c` to program the VGA hardware cursor, and `cursorLocation >> 8` (Chapter 9 §9.6) extracts the high byte to send it. Note also that `move_cursor` and `scroll` are declared `static` — **Chapter 11 §11.7** — because they are private helpers no other file should call. This is Chapter 15's "small interfaces" (§15.4) in practice: only three functions are public, and the rest are hidden.

---

## B.7 The C Entry Point: `main.c`

`main.c` is almost anticlimactic, and deliberately so:

```c
#include "monitor.h"

int main(struct multiboot *mboot_ptr)
{
    monitor_clear();
    monitor_write("Hello, world!");
    return 0;
}
```

Two things here connect to the book:

* This `main` is **not** the C `main` you know from hosted programs. Chapter 12 §12.4 explains that in a freestanding kernel, execution begins in the startup assembly, which then *calls* a C function. The name `main` is just a convention; `boot.s` could have called it anything.
* The parameter `struct multiboot *mboot_ptr` is a pointer the bootloader fills with information about the machine. Where does its value come from? From a register, set by the assembly bootstrap according to the calling convention — the subject of **Chapter 10**, which the next section examines. (When you build, GCC warns that `struct multiboot` is declared inside the parameter list; the type is never actually dereferenced here, so it is harmless, but it is a good thread to pull on later.)

`main` calls two functions from the driver and returns. After it returns, control goes back to `boot.s`, which halts the processor.

---

## B.8 The Bootstrap: `boot.s`

This is the file the whole port exists for. It is long, but Chapter 10 §10.12 offers the right attitude: *read assembly without fear*. Read it in the order the processor runs it, and lean on the README and DISCUSSION documents, which narrate it step by step.

### The multiboot header and the entry symbol

```nasm
[EXTERN main]
[GLOBAL start]
```

`[EXTERN main]` declares that `main` is defined elsewhere (in `main.c`), and `[GLOBAL start]` exposes `start` so the linker can use it as the entry point. This is the assembly side of **Chapter 10 §10.7** — "crossing the language boundary" — and the `extern`/`global` pairing of **Chapter 11 §11.7**. The multiboot header itself (the three `dd` values) is the handshake that lets a Multiboot loader recognize and load the kernel; it is the startup machinery Chapter 12 §12.9 describes.

### Step 0 — Does this CPU support long mode?

```nasm
    mov     eax, 0x80000001
    cpuid
    test    edx, 1 << 29            ; the LM bit
    jz      .no_long_mode
```

`test edx, 1 << 29` isolates a single bit — the long-mode flag — using the shift-and-mask thinking of **Chapter 9 §9.6-9.7**. Checking before acting is **defensive programming** (Chapter 15 §15.9): the README notes that skipping this check turns an unsupported CPU into a silent reboot loop, whereas ten lines of assembly buy you a readable error message instead.

### Step 1 — Build the page tables

This is **Chapter 7** made concrete. Long mode *requires* paging (DISCUSSION.md and Chapter 7 §7.1 both stress this), so the kernel must build a page table before it can enter 64-bit mode. First it zeroes three tables:

```nasm
    mov     edi, pml4
    mov     ecx, (4096 * 3) / 4
    xor     eax, eax
    rep     stosd
```

The three tables — `pml4`, `pdpt`, `pd` — are the top three levels of the **four-level page table** from **Chapter 7 §7.5**. Zeroing them by hand rather than trusting the loader is again defensive (Chapter 15 §15.9): a stray "present" bit in an unwritten entry would map a random page and cause an instant fault. Then it fills the page directory:

```nasm
    mov     ecx, 512
    mov     eax, 0x83               ; present | writable | page-size (2 MiB)
    mov     edi, pd
.map_pd:
    mov     [edi], eax
    add     eax, 0x200000           ; next 2 MiB frame
    add     edi, 8                  ; next 64-bit entry
    loop    .map_pd
```

The constant `0x83` is a **page-table entry** with three flag bits set, precisely the construction of **Chapter 9 §9.10**: bit 0 (present), bit 1 (writable), and bit 7 (page-size). Setting bit 7 uses the 2 MiB "large page" shortcut described in DISCUSSION.md, so one entry maps 2 MiB directly and no fourth-level table is needed. Five hundred and twelve entries × 2 MiB = 1 GiB, **identity-mapped** — virtual address `X` maps to physical address `X` (Chapter 3 §3.13 and Chapter 7 §7.2). This is why `monitor.c` did not have to change: `0xB8000` still means the VGA buffer after paging turns on.

### Steps 2-5 — Flip the switches

```nasm
    mov     eax, cr4
    or      eax, 1 << 5             ; CR4.PAE
    mov     cr4, eax
    mov     eax, pml4
    mov     cr3, eax
    mov     ecx, 0xC0000080         ; IA32_EFER
    rdmsr
    or      eax, 1 << 8             ; EFER.LME
    wrmsr
    mov     eax, cr0
    or      eax, 1 << 31            ; CR0.PG
    mov     cr0, eax
```

Every line here is *read a register, set one bit with OR, write it back* — the read-modify-write flag pattern of **Chapter 9 §9.8**, applied to the **control registers** of **Chapter 10 §10.9**. `1 << 5`, `1 << 8`, and `1 << 31` are named positions expressed as shifts (Chapter 9 §9.6). The `CR3` load points the MMU (Chapter 7 §7.10) at the page table just built. The `rdmsr`/`wrmsr` pair reads and writes a *model-specific register*, `IA32_EFER`, whose `LME` bit announces the intent to enter long mode.

### Step 6 — The far jump into 64-bit code

```nasm
gdt64:
    dq 0                                     ; null descriptor
.code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53) ; executable, code/data, present, 64-bit
...
    lgdt    [gdt64.pointer]
    jmp     gdt64.code:long_mode_start
```

The GDT is a small table of 64-bit **descriptors**, and building one with OR-ed bit positions is Chapter 2's structures-as-hardware-layout idea (§2.6, where the IDT was built the same way) combined with Chapter 9's bit flags. The far jump is the one operation Chapter 10 §10.8 singled out as *impossible to write in C*: it reloads the code segment and, because the new descriptor has bit 53 (the "L" bit) set, the very next instruction runs as true 64-bit code.

### Step 7 — Call `main` the 64-bit way

```nasm
[BITS 64]
long_mode_start:
    ...
    mov     rsp, stack_top
    xor     rdi, rdi
    mov     edi, [mboot_ptr]        ; struct multiboot *mboot_ptr
    call    main
```

This is **Chapter 10 §10.4** — the **System V AMD64 calling convention** — in its purest form. Setting up `rsp` establishes the stack (Chapter 10 §10.5). Then the multiboot pointer is placed in **`RDI`**, because the ABI passes the first integer argument in that register, *not* on the stack the way 32-bit code did. This is exactly the register that becomes `mboot_ptr` back in `main.c` §B.7. When `main` returns, the code falls into a `hlt`/`jmp` loop that idles the processor forever.

The page tables and stack themselves live in `.bss`:

```nasm
section .bss
align 4096
pml4:  resb 4096
pdpt:  resb 4096
pd:    resb 4096
...
stack_bottom:
    resb 16384                      ; 16 KiB kernel stack
stack_top:
```

`.bss` is the zero-initialized region introduced in Chapter 6 §6.2 ("three places where memory lives") and placed by the linker script in the next section. The 4096-byte alignment matters because page tables must sit on page boundaries.

---

## B.9 Assembling the Pieces: the `Makefile`

The Makefile is **Chapter 11 §11.8-11.10** brought to life, with a twist from Chapter 10 and Chapter 12.

```make
SOURCES=boot.o main.o monitor.o common.o

CFLAGS=-m64 -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
       -fno-pie -fno-pic -ffreestanding -mno-red-zone \
       -mno-mmx -mno-sse -mno-sse2 -Wall
LDFLAGS=-T link.ld -m elf_x86_64 -z max-page-size=0x1000 -z noexecstack
ASFLAGS=-felf64

link:
	ld $(LDFLAGS) -o kernel64.elf $(SOURCES)
	objcopy -I elf64-x86-64 -O elf32-i386 kernel64.elf kernel
```

Read the flags with Chapter 11 §11.10 ("why compiler flags matter") and Chapter 12 open:

* `-ffreestanding`, `-nostdlib`, and `-nostdinc` declare the **freestanding environment** of **Chapter 12 §12.2-12.3**: no standard library, no standard headers, no assumed `main`. This is why `common.c` had to provide its own `memcpy`.
* `-fno-builtin` and `-fno-stack-protector` remove hidden helpers the compiler would otherwise insert — Chapter 12 §12.11's "avoiding hidden dependencies."
* `-mno-red-zone` and `-mno-sse` are the two kernel-specific flags the README dwells on. They have no 32-bit counterpart and, if omitted, cause bugs that do not appear until interrupts fire. They belong to the same family of subtle, environment-driven decisions Chapter 12 §12.13 warns about.

The two-line `link` rule is the port's cleverest trick. It **links as ELF64** so relocations resolve correctly, then rewrites the container to **ELF32** with `objcopy`, because Multiboot 1 loaders only parse ELF32 headers. The machine code is untouched; only the metadata changes. This is a linking subtlety beyond Chapter 11 §11.8, and the README explains why it works: every address in the kernel is below 4 GiB, so nothing is lost in translation.

The `.s.o` rule at the bottom runs `nasm $(ASFLAGS)`, with `-felf64` producing 64-bit object files (Chapter 10). The C files compile through Make's built-in rule using `CFLAGS`.

---

## B.10 Placing the Kernel in Memory: `link.ld`

The linker script is the subject of **Chapter 11 §11.11**, and this one is short enough to read whole.

```ld
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(start)

SECTIONS
{
    . = 0x100000;

    .multiboot : { *(.multiboot) }
    .text   ALIGN(4096) : { *(.text) }
    .rodata ALIGN(4096) : { *(.rodata*) }
    .data   ALIGN(4096) : { *(.data) }
    .bss    ALIGN(4096) : { *(COMMON) *(.bss) }
    end = .;
}
```

Three lines are worth connecting to the book:

* `ENTRY(start)` names the label `start` from `boot.s` as the entry point — the answer to Chapter 12 §12.4's question about where execution begins, and the reason `main` is *not* first.
* `. = 0x100000;` sets the load address to 1 MiB. That the kernel lives at a fixed **physical** address, and that identity mapping makes it also its **virtual** address, is Chapter 3 §3.13 and Chapter 7 §7.2. (One megabyte is `0x100000`; the multiboot header must come first, which is why it is listed before `.text`.)
* The four sections `.text`, `.rodata`, `.data`, `.bss` are the memory regions of Chapter 6 §6.2, and placing them is the linker's job (Chapter 11 §11.11). `.bss` is where the page tables and stack from `boot.s` end up.

If you build and inspect the result, the entry symbol `start` sits at `0x101000` and `main` a little past it, near `0x101000` — a fact worth verifying yourself as an exercise in reading tools (Chapter 13 §13.12).

---

## B.11 Building and Running

Now stop reading and *do*, as Chapter 16 §16.2 insists: build, run, observe. From the `src` directory:

```bash
cd 03_screen_64/src
make
qemu-system-x86_64 -kernel kernel
```

You should see `Hello, world!` in the top-left corner. Two cautions from the README, both worth internalizing:

* **Use `qemu-system-x86_64`, never `qemu-system-i386`.** The 32-bit emulator has no long mode; you will get a blank screen and no error. The README calls this "the single most common way to waste an afternoon."
* **Expect warnings during the build.** The empty string functions in `common.c` produce "control reaches end of non-void function" — that is Chapter 12's freestanding library, not a real error. Chapter 14 §14.2 reminds you that reading warnings carefully is part of debugging *before* the bug.

When it works, do not move on immediately. Chapter 16 §16.4 asks you to *predict before you run*: before changing anything, write down what you expect. Then change one thing (§16.5) and see whether the machine agrees with you.

---

## B.12 Modifying the Kernel: Guided Experiments

Reading is not enough (Chapter 16 §16.1). The following experiments are ordered from safe to daring. For each, follow the Chapter 16 discipline: predict the outcome, change exactly one thing, rebuild, observe, and keep notes (§16.6). Where an experiment can crash the machine, that is the point — Chapter 16 §16.7 wants you to learn to break things and Chapter 14 stands ready when you do.

**1. Change the message.** Edit the string in `main.c` and rebuild. This touches only C and cannot break the bootstrap — a gentle confirmation that your build-and-run loop works.

**2. Change the colour.** In `monitor.c`, alter `foreColour` or `backColour` in `monitor_put` and `monitor_clear`. Predict the new attribute byte using Chapter 9 §9.8 before you compile, then check whether the screen matches your prediction.

**3. Implement `monitor_write_hex`.** The function is a stub (Chapter 12 §12.6). Fill it in using bit shifts and masks (Chapter 9 §9.6-9.7) to print a `u32int` in hexadecimal, then use it to print the address of `main` and confirm it is near `0x101000`.

**4. Implement the string functions.** Complete `memcpy`, `memset`, and `strcmp` in `common.c` using pointers and loops (Chapters 3 and 12). Nothing calls them yet, but writing them is the freestanding library of Chapter 12 §12.6.

**5. Break the bootstrap on purpose.** Comment out the line `mov cr0, eax` that enables paging, rebuild, and watch the machine fail. Then restore it and comment out `lgdt` instead. Each failure has a distinct signature; learning to recognize them now (Chapter 14 §14.12) will save you hours later. Change only one line at a time (Chapter 14 §14.16).

**6. Probe the mapping's edge.** The kernel identity-maps exactly 1 GiB. From C, dereference a pointer to `0x40000000` (just past the mapped region) and see what happens. This is a page fault (Chapter 7 §7.11) waiting to occur; the next tutorial chapter gives you the tools to *see* it rather than merely reboot.

---

## B.13 When It Breaks: Debugging This Kernel

Because this kernel has no debugger of its own, Chapter 14's techniques are your instruments.

* **QEMU is your laboratory** (§14.4). Run with a monitor to inspect the machine:

  ```bash
  qemu-system-x86_64 -kernel kernel -monitor stdio
  ```

  Then type `info registers` and read `EFER` and `CR4` (Chapter 14 §14.15). You want `EFER` to show the long-mode bits set and `CR4` bit 5 (PAE) high — direct confirmation that steps 2-5 of the bootstrap succeeded.

* **Printing is the simplest debugger** (§14.5). Once `main` runs, `monitor_write` and your new `monitor_write_hex` become print-debugging tools. Before `main`, you have only the raw VGA writes that `boot.s` uses for its "no long mode" message — a reminder of how much the C environment gives you.

* **Read a crash by its symptoms** (§14.8, §14.12). A blank screen with no message usually means you booted with the 32-bit emulator or the far jump failed. A reboot loop means a triple fault, most often a broken page table. Match the symptom to the step.

* **Change one thing at a time** (§14.16) and **keep a journal** (§14.17). In bootstrap code, where a single wrong bit can be fatal and invisible, this discipline is not optional.

---

## B.14 How the Whole Book Appears in One Kernel

Chapter 15 asks you to step back and see the design, not just the code. This tiny kernel is layered exactly as §15.3 recommends: the assembly bootstrap is the lowest layer, the hardware helpers (`common.c`) sit above it, the screen driver builds on those, and `main` sits on top, ignorant of everything below. Each layer exposes a small interface (§15.4) and hides its mechanism (§15.8) — the driver never knows paging is on, and `main` never knows a driver programs a VGA cursor.

For quick reference, here is where each chapter surfaces in `03_screen_64`:

| Chapter | Topic | Where it appears |
| --- | --- | --- |
| 1 | Types and portability | `common.h` typedefs; every `u16int`, `0xB8000` |
| 2 | Structures and hardware layout | the GDT descriptors in `boot.s` |
| 3 | Pointers and memory | `video_memory`, casting `0xB8000`, dereferencing |
| 4 | Inline assembly | `outb`/`inb`/`inw` in `common.c`; cursor programming |
| 6 | Where memory lives | `.data`/`.bss` sections; page tables and stack |
| 7 | Paging and virtual memory | the identity-mapped page tables in `boot.s` |
| 9 | Bit manipulation and flags | `0x83`, attribute bytes, `1 << 29`, masks |
| 10 | Linking C and assembly | `extern main`, the far jump, `RDI`, the calling convention |
| 11 | Reusable code and the build | headers, include guards, Makefile, linker script |
| 12 | Freestanding C | compiler flags, stub library functions, no real `main` |
| 13 | Reading kernel code | the reading strategy of §B.2-B.3 |
| 14 | Debugging | QEMU monitor, deliberate breakage, reading registers |
| 15 | Thinking like a kernel programmer | the layered design, small interfaces |
| 16 | Learning by experiment | the modifications of §B.12 |

Only Chapters 5 and 8 — interrupts and the heap — are absent, because this kernel does neither yet. The next tutorial chapters add them, and when you reach that code you will already know how to read it.

---

## Practice Exercises

**Exercise 1**

Without looking at §B.3, write the execution path of this kernel from GRUB's handoff to the final `hlt`, naming every file and function control passes through. Then verify it against the trace and against `boot.s`.

---

**Exercise 2**

Take the line `u16int *video_memory = (u16int *)0xB8000;` and explain each of its three ideas — the hexadecimal address, the cast, and the pointer type — citing the exact section of Chapters 1 and 3 that covers it.

---

**Exercise 3**

The bootstrap builds a page-table entry with the constant `0x83`. Write out its eight low bits, label the three that are set, and explain what each flag tells the processor. Then change the identity map to cover only 512 MiB instead of 1 GiB and predict, before building, what will break.

---

**Exercise 4**

Explain, using Chapter 10, why the line `mov edi, [mboot_ptr]` in `boot.s` corresponds to the parameter `mboot_ptr` in `main.c`. What would happen if the bootstrap placed the pointer in `RSI` instead of `RDI`?

---

**Exercise 5**

Remove `-mno-sse` from the Makefile's `CFLAGS`, rebuild, and describe what happens and why, connecting your explanation to the freestanding environment of Chapter 12. Then restore the flag.

---

**Exercise 6**

Implement `monitor_write_hex` and `monitor_write_dec` in `monitor.c`. Use them to print the address of `main` and the value of `cursor_x` after writing a message. Confirm the address is near `0x101000` and explain, using the linker script, why.

---

## Appendix Summary

`03_screen_64` looks like a program that prints a greeting, but it is really a demonstration of everything that must exist before a greeting is possible. Its nine files divide cleanly by responsibility, and each responsibility maps onto chapters you have already read: types and portability in `common.h`, inline assembly in `common.c`, pointers and bits in `monitor.c`, the freestanding entry point in `main.c`, the paging-and-calling-convention bootstrap in `boot.s`, and the build machinery in the Makefile and linker script.

The habits this appendix asked of you — map the directory before reading files, trace execution rather than lines, connect every construct to the concept behind it, predict before running, and change one thing at a time — are the habits Chapters 13 through 16 describe. Practiced on a kernel this small, they become second nature. Practiced on a kernel this small, they also become transferable: the next tutorial chapter is larger, but you will read it the same way.

You now have a kernel you understand completely. That is a rare and valuable thing. Break it, repair it, extend it, and measure your progress by what you can explain rather than by what compiles.
