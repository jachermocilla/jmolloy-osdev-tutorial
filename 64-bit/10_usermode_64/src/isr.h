//
// isr.h -- Interface and structures for high level interrupt service routines.
//          Part of this code is modified from Bran's kernel development tutorials.
//          Rewritten for JamesM's kernel development tutorials.
//          Ported to x86-64.
//

#ifndef ISR_H
#define ISR_H

#include "common.h"

// A few defines to make life a little easier
#define IRQ0 32
#define IRQ1 33
#define IRQ2 34
#define IRQ3 35
#define IRQ4 36
#define IRQ5 37
#define IRQ6 38
#define IRQ7 39
#define IRQ8 40
#define IRQ9 41
#define IRQ10 42
#define IRQ11 43
#define IRQ12 44
#define IRQ13 45
#define IRQ14 46
#define IRQ15 47

// A software interrupt the kernel raises on itself to yield the CPU.
#define INT_YIELD 0x81

// The layout of the stack at the moment isr_handler() is entered.
//
// Long mode has no PUSHA/POPA, so interrupt.s pushes all fifteen
// general-purpose registers by hand. The fields below appear in ascending
// address order, which is the *reverse* of the order they are pushed in.
//
// Note also that in 64-bit mode the CPU *always* pushes SS:RSP, even when the
// interrupt does not involve a privilege-level change. In 32-bit mode it only
// did so on a ring transition, which is why the old struct's `useresp` and
// `ss` fields were meaningless for kernel-mode faults.
typedef struct registers
{
    // Pushed by us, in interrupt.s.
    u64int r15, r14, r13, r12, r11, r10, r9, r8;
    u64int rbp, rdi, rsi, rdx, rcx, rbx, rax;

    // Pushed by the ISR stub: the interrupt number, and either the CPU's
    // error code or a dummy zero.
    u64int int_no, err_code;

    // Pushed by the processor automatically.
    u64int rip, cs, rflags, userrsp, ss;
} registers_t;

// The handler takes a *pointer*. Passing a 176-byte struct by value under the
// System V AMD64 ABI would make the caller copy it into a fresh stack slot, so
// the handler could never modify the frame that iretq restores.
// Handlers return a pointer to the frame that iretq should restore.
//
// Almost always that is the frame they were given. The scheduler is the
// exception: it returns *another task's* frame, and the stub simply switches
// RSP to it before unwinding. That single change is the entire context switch.
// There is no read_eip(), no magic 0x12345, no inline asm that clobbers
// whichever register GCC happened to choose.
registers_t *isr_handler(registers_t *regs);
registers_t *irq_handler(registers_t *regs);

// Enables registration of callbacks for interrupts or IRQs.
// For IRQs, to ease confusion, use the #defines above as the first parameter.
//
// Note the pointer: a handler that wants to influence what iretq restores
// (a scheduler, a page-fault handler) must be able to write to the live frame.
typedef registers_t *(*isr_t)(registers_t *);
void register_interrupt_handler(u8int n, isr_t handler);

#endif // ISR_H
