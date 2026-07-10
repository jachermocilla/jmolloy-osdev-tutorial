# Chapter 4 (GDT & IDT) — 64-bit Port

This is JamesM's chapter 4 kernel, ported to x86-64. It sets up a Global
Descriptor Table and an Interrupt Descriptor Table, then fires four software
interrupts to prove the handlers work.

**Read `03_screen_64/README.md` first.** This chapter builds directly on the
long-mode bootstrap explained there.

```
04_gdt_idt_64/src/
├── boot.s                ← carried over from chapter 3, unchanged
├── link.ld               ← carried over, unchanged
├── Makefile              ← one line changed (added the new .o files)
├── monitor.c/.h          ← unchanged
├── common.c/.h           ← unchanged
├── main.c                ← unchanged (bar the greeting text)
├── descriptor_tables.h   ← IDT entry doubled in size
├── descriptor_tables.c   ← new segment flags, 64-bit addresses
├── gdt.s                 ← rewritten
├── interrupt.s           ← rewritten
├── isr.h                 ← rewritten
└── isr.c                 ← signature change
```

---

## Building and running

```bash
cd 04_gdt_idt_64/src
make
qemu-system-x86_64 -kernel kernel
```

Expected output:

```
Hello, 64-bit world!
recieved interrupt: 3
recieved interrupt: 4
recieved interrupt: 4
recieved interrupt: 4
```

(The typo in "recieved" is inherited from the tutorial. Leaving it in makes it
easier to diff against the original.)

Again: **`qemu-system-i386` will not work.**

---

## What's actually different in long mode

Chapter 3's port was confined to assembly. This one reaches into the C.

There are five substantive changes. Each one is a place where the hardware
genuinely behaves differently, not just a place where a type got wider.

---

### 1. IDT entries doubled from 8 bytes to 16

An IDT entry has to store the address of the handler function. In 32-bit mode
that's 32 bits, split awkwardly across two 16-bit fields. In 64-bit mode it's
64 bits, so you need three fields — and the entry grew a new byte along the way.

Before:

```c
struct idt_entry_struct {
    u16int base_lo;   // handler address, bits  0-15
    u16int sel;       // code segment selector
    u8int  always0;
    u8int  flags;
    u16int base_hi;   // handler address, bits 16-31
} __attribute__((packed));            // 8 bytes
```

After:

```c
struct idt_entry_struct {
    u16int base_lo;   // handler address, bits  0-15
    u16int sel;       // code segment selector
    u8int  ist;       // NEW: Interrupt Stack Table index
    u8int  flags;
    u16int base_mid;  // handler address, bits 16-31
    u32int base_hi;   // handler address, bits 32-63
    u32int always0;   // reserved
} __attribute__((packed));            // 16 bytes
```

That `always0` byte in the old struct became the **`ist`** field. It's a
3-bit index into a table of known-good stack pointers stored in the TSS. If you
put a nonzero value there, the CPU *unconditionally* switches to that stack
when this interrupt fires — even from kernel mode, even if the current `RSP` is
garbage.

We leave it at 0 for now. But this is the mechanism that lets you write a
double-fault handler that survives a stack overflow, and eventually you'll want
it. Remember it exists.

Two more places grow: `gdt_ptr.base` and `idt_ptr.base` are now 64 bits wide,
because that's what `lgdt` and `lidt` expect in long mode.

---

### 2. Segment descriptors: `0xAF`, not `0xCF`

This is a one-character change that will cost you an afternoon if you get it wrong.

```c
gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xAF); // Kernel code segment (L=1)
gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // Kernel data segment
```

The `granularity` byte's high nibble holds four flags:

| Bit | Name | Meaning |
|-----|------|---------|
| 7 | **G** | Granularity: limit counts 4 KiB pages, not bytes |
| 6 | **D/B** | Default operand size is 32-bit |
| 5 | **L** | This is a **64-bit** code segment |
| 4 | AVL | Available for software use |

A 64-bit code segment must set **L** and must **clear D/B**. So `0xA0 | 0x0F` =
`0xAF`, where the low nibble is the top of the (ignored) limit.

Setting both `L` and `D/B` is an **illegal combination**. The CPU doesn't warn
you; it faults on the first far jump into that segment, which manifests as a
reboot loop.

Notice also what *doesn't* matter any more. The `base` and `limit` fields are
ignored entirely for code and data segments in long mode — every segment is
flat and spans the whole address space. We keep the fields, and keep
`gdt_set_gate`'s signature, purely so the code still looks like the tutorial's.
Segmentation is vestigial on x86-64. Paging does all the real work.

---

### 3. There is no far jump in 64-bit mode

The 32-bit `gdt_flush` ends by reloading `CS` with a far jump:

```nasm
    jmp 0x08:.flush     ; 32-bit only
.flush:
    ret
```

You can't do that in long mode — the far `jmp` with an immediate segment
doesn't exist. The standard idiom is to fake a far **return** instead. `retf`
pops `RIP` and then `CS` off the stack, so you build a fake frame and return
into it:

```nasm
gdt_flush:
    lgdt [rdi]        ; note: RDI, not [esp+4]
    mov ax, 0x10
    mov ds, ax
    ... (es, fs, gs, ss)

    pop rdi           ; RDI = our own return address
    mov rax, 0x08     ; the new CS
    push rax          ; pushed first, so it lands above RIP
    push rdi          ; the RIP to return to
    retfq             ; pops RIP then CS
```

Two things to notice:

- **`lgdt [rdi]`, not `lgdt [esp+4]`.** The System V AMD64 ABI passes the first
  argument in `RDI`. Every function called from C changes this way.
- **`retfq`**, not `retf`. The `q` forces a 64-bit operand size, so it pops two
  8-byte values rather than two 4-byte ones.

---

### 4. `PUSHA` and `POPA` no longer exist

Long mode deleted them. There are now sixteen general-purpose registers instead
of eight, and the instruction encoding for `PUSHA` was reused for something
else. So the ISR stub pushes all fifteen (all sixteen minus `RSP`, which the
CPU already saved) by hand:

```nasm
%macro PUSH_ALL 0
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    ... through r15
%endmacro
```

and pops them in exactly reverse order. `registers_t` in `isr.h` must mirror
this layout **read bottom-up**, because the stack grows downward: the *last*
register pushed sits at the *lowest* address, which is the *first* struct field.

Some smaller consequences in the same file:

- **`push byte 0` becomes `push qword 0`.** Every push is 8 bytes wide now;
  there's no such thing as pushing a byte.

- **The `cli` at the top of each stub is gone.** It was always dead code, even
  in 32-bit: an *interrupt* gate (type `0xE`, the `0x8E` in `idt_set_gate`)
  clears the interrupt flag on entry automatically. A *trap* gate (`0xF`)
  doesn't. Since we install interrupt gates, the `cli` never did anything.

- **The DS/ES/FS/GS reload in `isr_common_stub` is gone.** In 64-bit mode those
  segment registers are ignored for data accesses and their base is forced to
  zero. There's nothing to fix up, so there's nothing to save and restore.

#### Stack alignment: the trap nobody warns you about

The AMD64 ABI requires `RSP` to be 16-byte aligned immediately before a `CALL`.
Violate it and you get bizarre crashes deep inside C code, usually the first
time GCC decides to spill something to an aligned stack slot.

Do the arithmetic once, and write it down:

| What | Qwords pushed | Running total |
|------|---------------|---------------|
| CPU aligns `RSP` to 16 on interrupt | — | aligned |
| CPU pushes `SS, RSP, RFLAGS, CS, RIP` | 5 | 40 bytes |
| Stub pushes error code + interrupt number | 2 | 56 bytes |
| `PUSH_ALL` | 15 | **176 bytes** |

176 is a multiple of 16, so we are correctly aligned at the `call` with no
adjustment needed. That is a coincidence of the numbers, not a law of nature.
**If you ever add or remove a push, this silently breaks.**

Also note the `cld` before `call isr_handler` — the ABI requires the direction
flag to be clear when C code runs, and nothing else guarantees it.

---

### 5. The handler takes a pointer, not a struct

This is the change that's easiest to overlook and most important to get right.

```c
void isr_handler(registers_t *regs);   // was: registers_t regs
```

In 32-bit, `isr_handler(registers_t regs)` worked by accident: the struct's
fields happened to be laid out exactly as the stub had pushed them, so the
"argument on the stack" *was* the register frame.

On x86-64 the ABI says a 176-byte struct passed by value gets **copied by the
caller into a fresh stack slot**. So the handler would receive a snapshot. It
could read the registers, but any modification would be thrown away when
`iretq` restored from the *original* frame.

That's fatal, because modifying the frame is the whole point of an interrupt
handler:

- The page-fault handler (chapter 6) needs to map a page and then *resume the
  faulting instruction*.
- The scheduler (chapter 9) changes `RIP` and `RSP` to switch tasks.
- A system-call handler writes the return value into `regs->rax`.

So the stub hands over a pointer to the real frame:

```nasm
    PUSH_ALL
    mov rdi, rsp        ; RDI = pointer to registers_t on the stack
    cld
    call isr_handler
```

Anything the handler writes through `regs` is what `iretq` will restore.

---

## A bug fixed along the way

The original tutorial declares interrupts 17 and 21 as `ISR_NOERRCODE`. This is
wrong. `#AC` (17, Alignment Check) and `#CP` (21, Control Protection) both push
an error code, and so does `#SX` (30, Security Exception).

Why it matters: the `NOERRCODE` macro pushes a *dummy* zero to keep the frame
uniform. If the CPU already pushed a real error code and you push a dummy on
top, every field in `registers_t` is shifted by 8 bytes. `int_no` reads as the
error code, `rip` reads as `cs`, and `iretq` returns to garbage.

This port classifies 8, 10–14, 17, 21, and 30 as `ISR_ERRCODE`. That's the
correct set for x86-64.

---

## Another thing worth noticing: `ss` is now real

In 32-bit mode, the CPU only pushed `SS:ESP` onto the interrupt frame if the
interrupt caused a **privilege-level change** (ring 3 → ring 0). For a fault
that happened in the kernel, the `useresp` and `ss` fields of `registers_t`
contained whatever junk was on the stack.

In 64-bit mode the CPU pushes `SS:RSP` **always**. Those fields are now
meaningful for every interrupt, which is a small but real improvement.

---

## How this was verified

The `int $3` / `int $4` demo in `main.c` only exercises the *no-error-code*
path. That proves almost nothing — the error-code path has a different stack
layout and could be silently wrong.

To check it properly, temporarily make the handler dump the whole frame and
trigger a real fault by writing one byte past the 1 GiB identity map:

```c
asm volatile("movq $1, 0x40000000");   // unmapped -> page fault (vector 14)
```

You should see:

```
int=3  err=0x0  rip=0x101124 cs=0x8 ss=0x10 rax=0xcafe r15=0xbeef
int=14 err=0x2  rip=0x101124 cs=0x8 ss=0x10 rax=0xcafe r15=0xbeef
```

Read that carefully:

- **`int=14`** — the vector number landed in the right slot.
- **`err=0x2`** — bit 0 clear (page not present), bit 1 set (it was a write).
  Exactly right. If `ISR_ERRCODE` were misclassified you'd see the *interrupt
  number* here.
- **`ss=0x10`** — a real value, not garbage. (See above.)
- **`r15=0xbeef`** — the value we planted before the interrupt, surviving both
  faults. This is the check that `PUSH_ALL` and `POP_ALL` are exact mirrors of
  each other. Get one register out of order and this is the only symptom.

Then, to prove the pointer-passing design actually works, have the handler skip
the faulting instruction:

```c
if (regs->int_no == 14) regs->rip += 12;   // length of the movq
```

If `iretq` resumes at the *next* instruction rather than looping forever on the
fault, the handler successfully modified the live frame. If `registers_t` were
still passed by value, this would spin forever.

This kind of adversarial self-testing is worth doing every time you touch an
interrupt stub. The failure modes are all silent.

---

## Things to try

1. **Break the alignment.** Add one extra `push rax` before `mov rdi, rsp` (and
   a matching `pop`). The kernel will still mostly work — until it doesn't.
   This is what a real alignment bug feels like.

2. **Misclassify an ISR.** Change `ISR_ERRCODE 14` to `ISR_NOERRCODE 14`, then
   trigger a page fault and print the frame. Watch every field shift by one slot.

3. **Reintroduce the red zone.** Drop `-mno-red-zone` from `CFLAGS`, then write
   a small leaf function in `main.c` that keeps a local array and calls
   `int $3` in the middle of using it. Observe corruption.

4. **Print `regs->ss` and `regs->cs`.** Confirm they're `0x10` and `0x08` —
   the selectors your own `init_gdt` installed, not the ones from `boot.s`'s
   bootstrap GDT.

---

## Next

Chapter 5 (IRQs and the PIT) is mostly mechanical from here. Remap the PIC
exactly as the 32-bit version does — the 8259 doesn't know or care what mode
the CPU is in — and change `irq_handler` to take a pointer, the same way
`isr_handler` did.

Chapter 6 (paging) is the next real one. Four page-table levels instead of two,
and the tutorial's `page_directory_t`, with its parallel arrays of virtual and
physical pointers, does not survive the translation. You'll want to design that
one yourself rather than port it.
