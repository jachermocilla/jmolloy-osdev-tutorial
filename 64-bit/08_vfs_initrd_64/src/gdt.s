;
; gdt.s -- contains global descriptor table and interrupt descriptor table
;          setup code.
;          Based on code from Bran's kernel development tutorials.
;          Rewritten for JamesM's kernel development tutorials.
;          Ported to x86-64.

[BITS 64]

[GLOBAL gdt_flush]    ; Allows the C code to call gdt_flush().

; void gdt_flush(u64int gdt_ptr)
; The System V AMD64 ABI passes the first argument in RDI, not on the stack.
gdt_flush:
    lgdt [rdi]        ; Load the new GDT pointer

    mov ax, 0x10      ; 0x10 is the offset in the GDT to our data segment
    mov ds, ax        ; Load all data segment selectors
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; There is no far jump in 64-bit mode, so we cannot do `jmp 0x08:.flush`
    ; the way the 32-bit version did. The idiom is to fake a far *return*:
    ; pop our own return address, push the target CS and RIP, then RETFQ.
    pop rdi           ; RDI = the address gdt_flush was called from
    mov rax, 0x08     ; 0x08 is the offset to our code segment
    push rax          ; ...pushed first, so it ends up above RIP
    push rdi
    retfq             ; pops RIP then CS -- reloads CS with 0x08

[GLOBAL idt_flush]    ; Allows the C code to call idt_flush().

; void idt_flush(u64int idt_ptr)
idt_flush:
    lidt [rdi]        ; Load the IDT pointer.
    ret
