# Chapter 14 (The Keyboard) — new for the 64-bit series

This chapter is not in JamesM's original ten. It is the first time the kernel hears from outside itself: a key is pressed, an interrupt fires, and a thread wakes up holding the character. Almost none of the machinery is new — the buffer, the blocking, and the waking are chapter 13's semaphore doing chapter 13's job. What is new is that the producer is an interrupt handler, and an interrupt handler may post but may never wait.

**Read `13_condvar_sem_64/README.md` first.** The driver is that chapter's bounded buffer with one end wired to hardware.

```
14_keyboard_64/
├── make_initrd.c, test*.txt, mkinitrd.sh
└── src/
    ├── keyboard.c/.h  ← NEW: IRQ1 handler, scancode tables, keyboard_getchar()
    ├── semaphore.c/.h ← unchanged, now load-bearing: the driver's "chars ready"
    ├── syscall.c/.h   ← + SYS_GETCHAR
    ├── main.c         ← the ring 0 line editor
    ├── user.c         ← the ring 3 echo program
    └── everything else ← unchanged since chapter 13
```

The scheduler, paging, and the IRQ machinery are all untouched. The driver registers a handler on vector 33 (IRQ1) exactly as the timer registers one on vector 32, and it does not go near the PIC: `descriptor_tables.c` already unmasks every line (`outb(0x21, 0x0)`), so IRQ1 is live from boot. `irq_handler()` in `isr.c` sends the EOI before calling any handler, so `keyboard_handler` owes the hardware nothing but a single read of the data port.

---

## What changed, and why

**`keyboard.c/.h` — the driver.** A 128-byte ring, a "characters available" semaphore, an IRQ1 handler that reads port 0x60, and a blocking `keyboard_getchar`. The handler is the producer and the reader is the consumer, and the semaphore between them is `sem_init(0)` — the chapter-13 primitive, unchanged. The handler calls `sem_post` and never `sem_wait`, because it runs in interrupt context and interrupt context cannot block; a full ring makes it drop the key rather than wait for room. The reader guards its two lines on the ring with `irq_save`/`irq_restore`, which on one processor excludes the only other party — the interrupt — completely. See `DISCUSSION.md` for why masking is the right tool here when chapter 12 warned it usually is not.

**`semaphore.c/.h` — promoted from demo to infrastructure.** Chapter 13 built the counting semaphore to move numbers between two threads. Here it moves keystrokes from an interrupt to a thread, which is the same operation with a different producer. Nothing in the file changed; its role did.

**`syscall.c/.h` + `SYS_GETCHAR` — the reader can live in ring 3.** One new system call, `syscall_getchar`, blocks in the kernel and returns the next key. Unlike chapter 13's ring-3 primitives it needs no shared page and no trusted pointer, because a keystroke is a value the kernel returns rather than a structure ring 3 owns. Input flows one way, which makes it the cheaper direction to cross a privilege boundary.

**`main.c` and `user.c` — the demo, in both rings.** The boot thread reads one line straight from the driver and prints it back. Then it hands the keyboard to a ring-3 program that reads keys through the syscall and echoes them. Echo lives in the readers, not the driver: the driver delivers bytes and holds no opinion about whether they appear on screen.

---

## Building and running

```bash
cd 14_keyboard_64
bash mkinitrd.sh
qemu-system-x86_64 -kernel src/kernel -initrd initrd.img
```

Type a line at the `>` prompt and press Enter; the kernel echoes each key and prints the line back, then a ring-3 program takes over the keyboard at the `$` prompt and does the same. Captured from a real boot, after typing `hello`:

```
Found file test.txt
Found file test2.txt

tasking up. pid = 1, tid = 2

.user_text 0x... .. 0x...
.user_data 0x... .. 0x...
keyboard: type a line and press Enter.
> hello
kernel read: hello

keyboard now belongs to ring 3 (tid 4). Type away.

  [ring3] over to you -- type, Enter echoes the line.
  $
```

The part worth watching is invisible on screen: between keystrokes the machine is asleep. `keyboard_getchar` blocks the reader, the scheduler has nothing else runnable, and the idle thread halts the processor until the next interrupt. A key press wakes the reader, one letter appears, and the processor halts again. No polling, no spin — the driver spends zero CPU waiting for input, which was the whole reason chapter 13's block-and-wake had to exist before this chapter could.

### A note on verification

The interrupt path was checked directly, not just built. Booted under `qemu -d int`, six key presses (`hello` plus Enter) produced twelve IRQ1 deliveries — a make code and a break code each — with no triple fault and no reset, and the screen gained exactly the echoed text. The ring-3 syscall path is exercised too: the user program prints its prompt and then blocks in `syscall_getchar`, waiting for the next key with the processor idle.

---

## What this chapter does not do

No cursor keys, no keypad, no layouts past US, and no line discipline beyond backspace. Those are more scancode table, not more idea. And it does not run what you type — reading a line is the input half of a shell, and the half that runs a command is `exec()`, which is chapter 15. This chapter stops at "the kernel can read a line," which is exactly the piece a shell is missing and the keyboard can now supply.
