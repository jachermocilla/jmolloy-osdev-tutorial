# Chapter 5 (IRQs & the PIT) — 64-bit Port

This is JamesM's chapter 5 kernel, ported to x86-64. It remaps the PIC, wires up
the sixteen hardware IRQs, and programs the Programmable Interval Timer to fire
a periodic tick.

**Read `04_gdt_idt_64/README.md` first.** This chapter reuses its ISR machinery
almost verbatim.

```
05_irq_ipt_64/src/
├── boot.s                ← unchanged from chapter 4
├── link.ld               ← unchanged
├── gdt.s                 ← unchanged
├── monitor.c             ← unchanged
├── Makefile              ← added timer.o
├── main.c                ← calls init_timer()
├── common.h              ← added missing prototypes (see below)
├── monitor.h             ← added missing prototypes
├── descriptor_tables.h   ← irq0..irq15 externs
├── descriptor_tables.c   ← PIC remap, IDT gates 32-47
├── interrupt.s           ← IRQ macro + irq_common_stub
├── isr.h/.c              ← handler registry, irq_handler
└── timer.c/.h            ← new; PIT setup
```

---

## Building and running

```bash
cd 05_irq_ipt_64/src
make
qemu-system-x86_64 -kernel kernel
```

You should see the greeting, two software interrupts, and then a tick counter
climbing steadily:

```
Hello, 64-bit world!
recieved interrupt: 3
recieved interrupt: 4
Tick: 1
Tick: 2
Tick: 3
...
```

As always: **`qemu-system-i386` will not work.**

---

## The short version

This chapter is genuinely the easy one. The 8259A Programmable Interrupt
Controller is a chip from 1981, and the 8253/8254 PIT is from 1981 too. Neither
has the faintest idea what a 64-bit CPU is. You talk to both of them with `outb`
to 8-bit I/O ports, and that code is **byte-for-byte identical** to the 32-bit
tutorial:

```c
// Remap the irq table.
outb(0x20, 0x11);   outb(0xA0, 0x11);
outb(0x21, 0x20);   outb(0xA1, 0x28);
outb(0x21, 0x04);   outb(0xA1, 0x02);
outb(0x21, 0x01);   outb(0xA1, 0x01);
outb(0x21, 0x0);    outb(0xA1, 0x0);
```

So does the PIT programming, the EOI (end-of-interrupt) logic, and the whole
idea of a `interrupt_handlers[256]` callback table.

Everything that *did* change follows from decisions already made in chapter 4.

---

## What changed, and why

### 1. `isr_t` takes a pointer

```c
typedef void (*isr_t)(registers_t *);   // was: void (*)(registers_t)
```

This is the chapter 4 decision propagating outward. The C-level handler
receives a pointer to the live interrupt frame, so every registered callback
must too. Concretely:

```c
static void timer_callback(registers_t *regs)   // was: registers_t regs
```

It costs nothing today — `timer_callback` ignores its argument. But in chapter 9
the scheduler's timer callback will need to *write* to `regs->rip` and
`regs->rsp` to switch tasks, and that is only possible because the frame is
passed by reference. Passing the 176-byte struct by value under the System V
AMD64 ABI would hand the callback a throwaway copy.

Design decisions in kernels are like this: the cost shows up chapters later.

### 2. `irq_common_stub` mirrors `isr_common_stub`

Same `PUSH_ALL` / `mov rdi, rsp` / `cld` / `call` / `POP_ALL` / `add rsp, 16` /
`iretq` shape. The only difference is which C function it calls. The `IRQ` macro
pushes a dummy zero error code exactly as `ISR_NOERRCODE` does — hardware IRQs
never push one — so the frame layout stays uniform.

```nasm
%macro IRQ 2
  global irq%1
  irq%1:
    push qword 0        ; dummy error code
    push qword %2       ; the remapped interrupt number (32..47)
    jmp irq_common_stub
%endmacro
```

`push byte` → `push qword`, and the `cli` at the top is gone, for the reasons
covered in chapter 4.

### 3. The `sti` before `iret` is gone — and it was a bug

The 32-bit original ends `irq_common_stub` like this:

```nasm
    add esp, 8
    sti          ; <-- remove this
    iret
```

Think about what `iret` (or `iretq`) does: it pops `RIP`, `CS`, and **`RFLAGS`**
off the stack. The `RFLAGS` value sitting there is the one the CPU saved when
the interrupt fired — and if the interrupt fired, `IF` was set. So `iretq`
re-enables interrupts *by itself*, atomically, as part of returning.

The explicit `sti` therefore does two things: nothing useful, and one genuinely
bad thing. It opens a window of one instruction during which interrupts are
enabled but we are still standing on the old interrupt frame with `RSP` pointing
into it. A second IRQ arriving in that window nests on top of a frame we're
halfway through dismantling.

In practice you will rarely catch it. That's what makes it worth deleting.

---

## A real bug in the tutorial: the PIT divisor overflows

This one has nothing to do with 64-bit. It's been there all along, and the
tutorial's own comment tells you about it before ignoring it:

```c
// ... Important to note is that the divisor must be small enough to fit
// into 16-bits.
u32int divisor = 1193180 / frequency;
```

`main.c` then calls `init_timer(10)`.

Do the arithmetic. `1193180 / 10 = 119318`, which is `0x1D216` — **seventeen
bits**. The code then splits it into two bytes:

```c
u8int l = (u8int)(divisor & 0xFF);          // 0x16
u8int h = (u8int)((divisor >> 8) & 0xFF);   // 0xD2
```

The `0x1` at the top is silently dropped. The PIT receives `0xD216` = 53782, and
so ticks at `1193180 / 53782` = **22.2 Hz**, not the 10 Hz you asked for.

I measured this before fixing it, by sampling the tick counter five seconds
apart:

```
ticks: [64, 175]  ->  measured 22.2 Hz (requested 10)
```

22.2 Hz, to one decimal place, exactly as predicted. The kernel was lying about
its own clock by a factor of 2.2, and nothing complained.

The fix is a clamp:

```c
u32int divisor = 1193180 / frequency;
if (divisor > 0xFFFF) divisor = 0xFFFF;   // ~18.2 Hz, the slowest possible
if (divisor == 0)     divisor = 1;
```

And `main.c` now asks for 50 Hz, which yields a divisor of 23863 — comfortably
inside 16 bits, and the rate every later chapter assumes. Re-measured:

```
ticks: [144, 394]  ->  measured 50.0 Hz (requested 50)
```

The general lesson: **the PIT cannot tick slower than 18.2 Hz**
(`1193180 / 65535`). If you ever want a slower tick, you count PIT interrupts in
software and only act on every *n*th one. There is no way to ask the hardware.

---

## Missing prototypes: sloppy in 32-bit, dangerous in 64-bit

`memset`, `memcpy`, `strcpy`, `strcat`, `monitor_write_hex`, and
`monitor_write_dec` are defined in `common.c` and `monitor.c` but were never
declared in the corresponding headers. In 32-bit this produced warnings that
everybody ignored.

Under an implicit declaration, C assumes a function returns `int`. On 32-bit
x86, `int` and `char *` are both 32 bits and both come back in `EAX`, so the
sloppiness was invisible.

On x86-64, `int` is 32 bits and a pointer is 64. A call to an
implicitly-declared `strcpy` takes the 64-bit pointer in `RAX` and **truncates
it to 32 bits**. If your kernel ever lives above 4 GiB — which it will, the
moment you move to the higher half — every such call silently corrupts a
pointer.

So `common.h` and `monitor.h` now declare everything. Compile with `-Wall` and
keep the output clean.

> Two warnings deliberately remain, in `strcpy` and `strcat`. Both are real
> bugs inherited from the tutorial: `strcpy` never returns `dest`, and `strcat`
> contains `*dest = *dest++`, which is undefined behaviour, and returns the
> wrong pointer besides. Neither function is called in this chapter. Fixing
> them is left as an exercise — chapter 8 will call `strcpy`, and you'll want
> them correct by then.

---

## How the pieces fit together

Worth tracing once, end to end, because it's the first time hardware drives the
CPU rather than the other way round:

1. The PIT counts down from 23863 at 1.19 MHz. When it hits zero it raises a
   line on the PIC's IRQ 0 input and reloads.
2. The PIC, remapped by `init_idt`, asserts `INTR` and puts vector **32** on the
   bus (rather than vector 8, which would collide with the CPU's own
   double-fault exception — that collision is exactly why the remap exists).
3. The CPU aligns `RSP` to 16, pushes `SS, RSP, RFLAGS, CS, RIP`, clears `IF`
   (because gate type `0x8E` is an *interrupt* gate), and jumps to `irq0`.
4. `irq0` pushes a dummy error code and the number 32, then jumps to
   `irq_common_stub`.
5. `irq_common_stub` pushes all fifteen GPRs, points `RDI` at the frame, and
   calls `irq_handler`.
6. `irq_handler` sends the EOI byte `0x20` to port `0x20`, telling the PIC it
   may deliver further interrupts, then looks up `interrupt_handlers[32]` and
   calls `timer_callback`.
7. Unwind: `POP_ALL`, drop the two pushed qwords, `iretq`. `RFLAGS` comes back
   off the stack with `IF` set, so interrupts resume atomically with the return.

Note the EOI in step 6 happens *before* the callback runs, and interrupts are
still disabled at that point (`IF` is clear). If a callback ever calls `sti`,
IRQs will nest. Chapter 9's scheduler cares about this.

---

## Things to try

1. **Measure your own clock.** Don't trust `init_timer`. Add a second counter
   incremented by the CPU's `rdtsc` and cross-check. Kernels lie; instruments
   don't.

2. **Delete the EOI.** Comment out the `outb(0x20, 0x20)` in `irq_handler`.
   You'll get exactly one tick, then silence — the PIC is waiting for an
   acknowledgement that never comes. This is the single most common IRQ bug.

3. **Ask for 5 Hz.** With the clamp in place, `init_timer(5)` gives you 18.2 Hz,
   not 5. Now write a `tick % 4` filter in `timer_callback` to get a real 4.5 Hz
   heartbeat. This is how every real kernel derives slow timers.

4. **Reinstate the `sti`.** Put it back before the `iretq`, then set the timer to
   something aggressive (say 5000 Hz) and see whether you can provoke a nested
   IRQ. You probably can't, easily — which is the point. Rare bugs are the
   dangerous ones.

5. **Add a keyboard handler.** `register_interrupt_handler(IRQ1, &kbd_callback)`
   and read the scancode from port `0x60`. Nothing about this is 64-bit-specific;
   it's the same code as the 32-bit tutorial. That's the whole message of this
   chapter.

---

## Next

Chapter 6 (paging) is where the port stops being a translation exercise.

The 32-bit tutorial's `page_directory_t` holds two parallel arrays — one of
virtual pointers to page tables, one of their physical addresses — because a
32-bit page directory has exactly 1024 entries and the kernel needs both views.
On x86-64 there are **four** levels rather than two (PML4 → PDPT → PD → PT), and
that structure does not generalise: you'd need parallel arrays at every level.

Note also that `boot.s` has *already* built you a working four-level page table.
Chapter 6 is not about turning paging on — it's on. It's about taking ownership
of the tables `boot.s` built, adding a physical frame allocator, and handling
faults. Read the `boot.s` you wrote in chapter 3 again before you start; you
understand more of it than you think.

You'll want to design that chapter rather than port it. Recursive mapping (map
the PML4 into itself at entry 510) is the idiom worth learning.
