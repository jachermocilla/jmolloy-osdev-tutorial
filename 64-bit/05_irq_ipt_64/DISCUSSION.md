# Chapter 5: Hardware Interrupts and the System Timer

In the previous chapter, the operating system learned how to respond to processor exceptions. Whenever the CPU encountered an exceptional condition, such as a divide-by-zero operation or an invalid instruction, it consulted the Interrupt Descriptor Table (IDT) and transferred control to the appropriate handler. Every one of those interrupts originated inside the processor itself.

An operating system must also answer to the world outside the chip. A keyboard reports that a key has been pressed, a network card signals that a packet has arrived, a disk controller announces that a read has completed, and — most important for the chapters ahead — a timer fires at regular intervals, giving the kernel a way to measure time and eventually to schedule processes.

This chapter introduces the hardware interrupt mechanism used by traditional x86 systems: how interrupt requests reach the processor, why the Programmable Interrupt Controller must be reconfigured before any of them can be trusted, and how the Programmable Interval Timer gives the kernel its first notion of time.

By the end, the kernel will no longer run from start to finish undisturbed. Hardware will interrupt it periodically, proving that the operating system can react to events that occur independently of its own execution.

---

# Exceptions Versus Hardware Interrupts

The processor responds to two classes of events that arrive through the same door for entirely different reasons.

**Exceptions** are generated internally, when the processor detects an unusual condition while executing an instruction: division by zero, an invalid opcode, a page fault, a breakpoint. **Hardware interrupts** originate outside the processor, when a peripheral device asks for attention. Both end up invoking entries in the Interrupt Descriptor Table.

```text
                   Processor

        +-------------------------+
        |                         |
        |   Interrupt Descriptor  |
        |          Table          |
        +------------+------------+
                     |
      +--------------+--------------+
      |                             |
      v                             v
 CPU Exception              Hardware Interrupt
 (Internal Event)          (External Device)
```

To the processor, both are simply interrupts, dispatched by the same machinery. To the operating system they mean opposite things: an exception says that something went wrong in the software the processor was running, while a hardware interrupt says that a device outside the processor needs service.

---

# Why Hardware Cannot Interrupt the Processor Directly

Wiring every device straight to the processor sounds reasonable until you count the devices. A modern machine holds timers, keyboards, storage controllers, network interfaces, USB controllers, and a long tail of others, all capable of demanding attention at any moment; giving each one its own pin on the CPU would take an impractical number of dedicated lines.

Traditional x86 systems place an intermediary in between, the **Programmable Interrupt Controller (PIC)**, which collects requests from the devices and presents them to the processor one at a time and in a defined order.

```text
Hardware Device
        |
        v
+----------------------+
| Programmable         |
| Interrupt Controller |
+----------------------+
        |
        v
       CPU
        |
        v
       IDT
```

The PIC is a traffic controller. Devices raise their requests whenever they please, and the PIC decides which one reaches the processor next.

Modern systems have largely retired the chip in favour of the Advanced Programmable Interrupt Controller (APIC), which is faster, understands multiple processors, and is considerably more complicated. The PIC remains the better teacher, and every x86 machine still starts up with one.

---

# Interrupt Request Lines

The PIC manages sixteen **interrupt request lines**, or **IRQs**, each traditionally assigned to one device.

```text
IRQ0   System Timer
IRQ1   Keyboard
IRQ2   Cascade
IRQ3   Serial Port
IRQ4   Serial Port
...
IRQ14  Primary Disk
IRQ15  Secondary Disk
```

Sixteen lines is really eight and eight. The original 8259A chip handled only eight, so the IBM PC used two of them, chaining the second into line 2 of the first: IRQ2 carries no device of its own, and any interrupt on lines 8 through 15 arrives at the processor by way of the second chip reporting to the first. This is why the kernel programs two controllers and why acknowledging a high-numbered interrupt takes two writes rather than one.

The assignments themselves are historical rather than architectural. Nothing in the silicon makes IRQ0 the timer; four decades of software expecting it there does.

---

# Why the PIC Must Be Remapped

Out of the box, the PIC delivers its interrupts on vectors 0 through 15, which the processor has already claimed for its exceptions.

```text
CPU Exceptions

0 ---------------- 31


Default PIC

0 ----------- 15
```

Vector 8 is then either a double fault or a keyboard press, and nothing in the interrupt tells the processor which. Both land in the same entry of the Interrupt Descriptor Table, and the handler has no way to ask.

The fix is **PIC remapping**: before enabling anything, the kernel sends the controller a short initialization sequence telling it to deliver its sixteen interrupts starting at vector 32 instead of vector 0. The first chip is given a base of 32, the second a base of 40.

```text
CPU Exceptions

0 ------------------------ 31


Hardware IRQs

32 ----------------------- 47
```

The exceptions keep the lower part of the table, the hardware interrupts get sixteen dedicated entries above them, and vector 8 becomes unambiguous again. Practically every x86 operating system uses this layout, and IRQ0 arriving as vector 32 is the reason interrupt numbers in kernel source so often appear with a mysterious offset.

---

# Extending the Interrupt Descriptor Table

Sixteen new descriptors join the thirty-two that the previous chapter installed for exceptions.

```text
Interrupt Descriptor Table

0  - 31   Processor Exceptions

32 - 47   Hardware IRQs

48 -255   Available
```

Nothing about the table itself changes; entries are simply filled in. The x86 interrupt architecture makes no structural distinction between a fault raised by the processor and a signal raised by a disk controller, which means the dispatch mechanism written in the previous chapter carries over untouched, and the remaining two hundred entries stay free for system calls and software interrupts in later chapters.

---

# The Journey of a Hardware Interrupt

With the table extended, devices can begin interrupting the processor, and the path they take will look familiar.

```text
Timer
   |
   v
PIC
   |
   v
CPU
   |
   v
IDT Entry
   |
   v
Assembly Wrapper
   |
   v
C Interrupt Handler
```

Only the first two steps are new. Once the interrupt reaches the processor, the sequence is the exception flow from the previous chapter: look up the vector, run the assembly wrapper, save the registers, call into C. The difference is entirely one of origin — an external device begins the sequence instead of a fault in the instruction stream.

---

# Acknowledging the Interrupt

One detail separates hardware interrupts from exceptions, and it is the detail that breaks first. When the handler has finished, the operating system must tell the PIC so, by sending it a byte known as the **End of Interrupt (EOI)**. Until that byte arrives, the controller assumes the interrupt is still being serviced and holds back every further request from that line.

```text
Device
   |
Interrupt
   |
   v
Operating System
   |
Process Request
   |
Send EOI
   |
   v
PIC Ready
```

The two-chip arrangement shows up here. An interrupt from lines 8 through 15 passed through both controllers on its way in and must be acknowledged at both on the way out; forget the second one and the entire upper half of the interrupt space falls silent while the lower half keeps working.

The symptom of a missing End of Interrupt is unmistakable once you have seen it: the first interrupt arrives perfectly, and no interrupt ever arrives again. A kernel that ticks exactly once and then runs on in silence is not a kernel with a broken timer.

---

# Giving the Kernel a Sense of Time

No device matters more in early kernel development than the **Programmable Interval Timer (PIT)**. Where a keyboard or a disk interrupts only when something happens to it, the timer's entire purpose is to interrupt on a schedule, whether or not anything has happened at all.

```text
Tick
 |
Tick
 |
Tick
 |
Tick
 |
Tick
```

Each tick raises IRQ0, and the operating system counts the ticks to measure the passing of time. For now, counting is all it does: the handler increments a variable and prints it. The same interrupt will later drive scheduling, sleeping threads, timeouts, and performance measurement — every kernel service that needs to know that time has moved. This is the operating system's heartbeat, and the chapter has it doing nothing but proving it beats.

---

# Programming the Timer

The PIT runs from a fixed crystal at roughly 1.19 MHz, an artifact of 1981 television timing that no software can change. The kernel cannot ask it for a frequency; it can only supply a **divisor**, and the chip divides its base clock by that value to decide how often to fire.

```text
Base Clock
      |
      |
 Divide by N
      |
      v
Interrupt Frequency
```

A smaller divisor produces more frequent interrupts, and a larger one produces fewer. The arithmetic is a single division, and the one place it bites is the boundary: the divisor is a 16-bit value, so it cannot exceed 65535, which puts the slowest tick the hardware can produce at about 18.2 times per second. Ask for anything slower and the value overflows, taking the low sixteen bits and running the timer at some unrelated speed rather than reporting an error.

Choosing a frequency is a matter of the operating system's goals rather than of hardware. Frequent ticks give finer timekeeping and more responsive scheduling at the cost of spending more of the processor inside the handler. Between 50 and 100 ticks per second is comfortable for an educational kernel.

---

# The First Periodic Interrupt

Once the timer is configured, interrupts begin arriving on their own. Nothing in the kernel asks for them, and unlike the software interrupts of the previous chapter, no instruction triggers them.

```text
Kernel Starts
      |
      v
Initialize Timer
      |
      v
Continue Executing
      |
      v
Timer Interrupt
      |
      v
Interrupt Handler
      |
      v
Resume Execution
```

Execution has stopped being sequential. The processor now pauses whatever it was doing, services a hardware event, and resumes exactly where it left off — and the code that was interrupted cannot tell that any of it happened. Every multitasking operating system is built on that one trick. Later chapters will change what happens during the pause: instead of returning to the interrupted code, the kernel will return to somewhere else entirely, and call it a context switch.

---

# Hardware Interrupts in Long Mode

The surprise of this chapter is how little of it is about 64 bits. Interrupts still arrive through the Interrupt Descriptor Table, the PIC is programmed with the same bytes sent to the same ports, and the PIT behaves exactly as it does on a 386. The controller code is byte-for-byte what a 32-bit kernel would contain, because the chip it talks to predates long mode by twenty years and has never heard of it.

What differs comes from the execution environment rather than the devices: larger interrupt frames, wider addresses to store in the descriptors, a different calling convention for the handlers, and a new return instruction. The architecture around them is unchanged.

There is a design principle in that. The x86 hardware interface has stayed remarkably stable across generations, and once the processor has reached long mode, talking to legacy hardware looks almost exactly as it did in 1985.

---

# Looking Ahead

The kernel has become event-driven. Rather than running to completion undisturbed, it can be interrupted at any instant by hardware and can respond to a world that does not wait for it.

The timer will outgrow its counter quickly. In the chapters ahead it becomes the foundation for timekeeping, scheduling, sleeping, and preemption — for every kernel service whose behaviour depends on the clock.

With interrupts working, the next task is memory. The identity-mapped page tables built during the bootstrap will give way to a full virtual memory subsystem, one that can allocate, protect, and reclaim memory while the system runs.
