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

; In isr.c
extern isr_handler

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

    POP_ALL
    add rsp, 16                 ; Pop the pushed error code and ISR number.
    iretq                       ; Pops RIP, CS, RFLAGS, RSP and SS.
