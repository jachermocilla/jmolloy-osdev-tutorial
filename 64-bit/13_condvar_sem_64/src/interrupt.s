;
; interrupt.s -- Contains interrupt service routine wrappers.
;                Based on Bran's kernel development tutorials.
;                Rewritten for JamesM's kernel development tutorials.
;                Ported to x86-64.

[BITS 64]

; This macro creates a stub for an ISR which does NOT push its own error code
; (so we add a dummy zero to keep the stack frame uniform).
;
; Note: `push byte 0` from the 32-bit version does not exist here. In long mode
; a push is always 8 bytes wide; `push qword` makes that explicit.
;
; Note also that the 32-bit version began each stub with `cli`. That was always
; redundant -- an *interrupt* gate (type 0xE) clears IF on entry, unlike a trap
; gate (0xF). We rely on that instead.
%macro ISR_NOERRCODE 1
  global isr%1
  isr%1:
    push qword 0                ; Push a dummy error code.
    push qword %1               ; Push the interrupt number.
    jmp isr_common_stub         ; Go to our common handler code.
%endmacro

; This macro creates a stub for an ISR which passes its own error code.
; The CPU has already pushed the error code, so we only push the number.
%macro ISR_ERRCODE 1
  global isr%1
  isr%1:
    push qword %1               ; Push the interrupt number
    jmp isr_common_stub
%endmacro

; This macro creates a stub for an IRQ. The first parameter is the IRQ number,
; the second is the ISR number it has been remapped to by the PIC.
;
; The IRQ pushes no error code, so like ISR_NOERRCODE we push a dummy zero to
; keep the frame layout uniform.
%macro IRQ 2
  global irq%1
  irq%1:
    push qword 0
    push qword %2
    jmp irq_common_stub
%endmacro

ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17               ; #AC really does push an error code.
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_ERRCODE   21               ; #CP really does push an error code.
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30               ; #SX really does push an error code.
ISR_NOERRCODE 31

; The syscall vector. Reachable from ring 3.
ISR_NOERRCODE 128

; A software interrupt vector the kernel raises on itself, to yield the CPU.
ISR_NOERRCODE 129

IRQ   0,    32
IRQ   1,    33
IRQ   2,    34
IRQ   3,    35
IRQ   4,    36
IRQ   5,    37
IRQ   6,    38
IRQ   7,    39
IRQ   8,    40
IRQ   9,    41
IRQ  10,    42
IRQ  11,    43
IRQ  12,    44
IRQ  13,    45
IRQ  14,    46
IRQ  15,    47

; In isr.c
extern isr_handler
extern irq_handler

; Long mode deleted PUSHA and POPA, so we save and restore the fifteen
; general-purpose registers by hand. The order here must exactly mirror
; registers_t in isr.h, read bottom-up.
%macro PUSH_ALL 0
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro POP_ALL 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

; This is our common ISR stub. It saves the processor state, calls the C-level
; fault handler, and finally restores the stack frame.
;
; We no longer reload DS/ES/FS/GS: in 64-bit mode those selectors are ignored
; for data accesses, and their base is forced to zero. There is nothing to fix.
isr_common_stub:
    PUSH_ALL

    ; Hand the C handler a *pointer* to the frame we just built. RDI is the
    ; first argument register in the System V AMD64 ABI.
    mov rdi, rsp

    ; The ABI requires RSP to be 16-byte aligned immediately before a CALL.
    ; On interrupt the CPU aligns RSP to 16, then pushes 5 qwords; the stub
    ; pushes 2 more, and PUSH_ALL pushes 15. 22 qwords = 176 bytes, a multiple
    ; of 16, so we are already correctly aligned. Nothing to do -- but if you
    ; ever add or remove a push, this is what will silently break.
    cld                         ; The ABI requires DF clear on entry to C code.
    call isr_handler

    mov rsp, rax                ; See irq_common_stub. Lets int $0x81 yield.

    POP_ALL
    add rsp, 16                 ; Pop the pushed error code and ISR number.
    iretq                       ; Pops RIP, CS, RFLAGS, RSP and SS.

; This is our common IRQ stub. Structurally identical to isr_common_stub --
; the only difference is which C function it calls.
;
; Note that the 32-bit original ended with `sti` before `iret`. That was a bug
; waiting to happen: iretq restores RFLAGS from the stack, which already has IF
; set (it was set when the interrupt fired), so the sti was redundant. Worse,
; it re-enabled interrupts one instruction *early*, opening a window in which a
; second IRQ could arrive while we were still on the old frame.
irq_common_stub:
    PUSH_ALL

    mov rdi, rsp                ; First argument: pointer to registers_t.
    cld
    call irq_handler

    ; irq_handler returns, in RAX, the frame that iretq should restore.
    ; Normally that is the frame we passed in and this is a no-op. When the
    ; scheduler ran, it is a *different task's* frame, sitting on a different
    ; kernel stack -- and this one instruction is the context switch.
    mov rsp, rax

    POP_ALL
    add rsp, 16                 ; Pop the pushed error code and IRQ number.
    iretq
