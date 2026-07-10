//
// isr.h -- Interface and structures for high level interrupt service routines.
//          Part of this code is modified from Bran's kernel development tutorials.
//          Rewritten for JamesM's kernel development tutorials.
//          Ported to x86-64.
//

#ifndef ISR_H
#define ISR_H

#include "common.h"

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
void isr_handler(registers_t *regs);

#endif // ISR_H
