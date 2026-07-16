;
; process.s -- The ring 0 -> ring 3 transition.
;              Written for JamesM's kernel development tutorials.
;              Redesigned for x86-64.
;

[BITS 64]

[GLOBAL enter_user_mode]

; void enter_user_mode(u64int rip, u64int rsp)
;
; There is no far jump in long mode and no "switch to ring 3" instruction. The
; only way down is to return from an interrupt you never took: build the frame
; iretq expects, with a code selector whose RPL is 3, and execute iretq.
;
; This is the same trick as gdt_flush's retfq in chapter 4, one privilege level
; lower.
enter_user_mode:
    cli

    mov ax, 0x23          ; user data selector (index 4, RPL 3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ; Note we do NOT load SS here. iretq loads it from the frame below, and it
    ; must arrive there with RPL 3 or the CPU faults on the return.

    push qword 0x23       ; ss
    push rsi              ; rsp   (second argument)

    pushfq                ; take the current RFLAGS...
    pop rax
    or  eax, 0x200        ; ...set IF, so the timer can still preempt us...
    and eax, 0xFFFFCFFF   ; ...and force IOPL = 0, so ring 3 cannot do outb.
    push rax              ; rflags

    push qword 0x1B       ; cs    (user code, index 3, RPL 3)
    push rdi              ; rip   (first argument)

    iretq
