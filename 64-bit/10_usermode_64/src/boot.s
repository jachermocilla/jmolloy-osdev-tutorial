;
; boot.s -- Kernel start location. Also defines the multiboot header.
;           64-bit port of JamesM's tutorial boot.s.
;
; GRUB / QEMU hand control over in 32-bit protected mode with paging OFF.
; Long mode *requires* paging, so before we can call any 64-bit C code we must:
;   1. build an identity-mapped page table for the low 1 GiB
;   2. enable PAE (CR4.PAE)
;   3. set the long-mode-enable bit in the EFER MSR
;   4. turn paging on (CR0.PG) -- this puts us in "compatibility mode"
;   5. load a 64-bit GDT and far-jump into a 64-bit code segment
;

MBOOT_PAGE_ALIGN    equ 1<<0        ; Load kernel and modules on a page boundary
MBOOT_MEM_INFO      equ 1<<1        ; Provide your kernel with memory info
MBOOT_HEADER_MAGIC  equ 0x1BADB002  ; Multiboot Magic value
MBOOT_HEADER_FLAGS  equ MBOOT_PAGE_ALIGN | MBOOT_MEM_INFO
MBOOT_CHECKSUM      equ -(MBOOT_HEADER_MAGIC + MBOOT_HEADER_FLAGS)

[EXTERN main]                       ; This is the entry point of our C code

; ---------------------------------------------------------------------------
; The multiboot header must live in the first 8 KiB of the file, 4-byte aligned.
; ---------------------------------------------------------------------------
section .multiboot
align 4
[GLOBAL mboot]
mboot:
    dd  MBOOT_HEADER_MAGIC
    dd  MBOOT_HEADER_FLAGS
    dd  MBOOT_CHECKSUM

; ---------------------------------------------------------------------------
; 32-bit bootstrap.
; ---------------------------------------------------------------------------
section .text
[BITS 32]
[GLOBAL start]
start:
    cli
    mov     esp, stack_top          ; a stack of our own; GRUB's is not ours to keep
    mov     [mboot_ptr], ebx        ; stash the multiboot info pointer for later

    ; -- Does this CPU actually support long mode? --------------------------
    mov     eax, 0x80000000
    cpuid
    cmp     eax, 0x80000001         ; is the extended leaf we need available?
    jb      .no_long_mode
    mov     eax, 0x80000001
    cpuid
    test    edx, 1 << 29            ; LM bit
    jz      .no_long_mode

    ; -- Zero the three page tables ----------------------------------------
    ; Multiboot loaders are supposed to zero .bss for us, but a stray
    ; present-bit in an unwritten entry would be fatal, so do it ourselves.
    mov     edi, pml4
    mov     ecx, (4096 * 3) / 4
    xor     eax, eax
    rep     stosd

    ; -- Identity-map the first 1 GiB using 2 MiB pages ---------------------
    ; PML4[0] -> PDPT
    mov     eax, pdpt
    or      eax, 0x03               ; present | writable
    mov     [pml4], eax
    ; PDPT[0] -> PD
    mov     eax, pd
    or      eax, 0x03
    mov     [pdpt], eax
    ; PD[i] -> 2 MiB page at i * 2 MiB
    mov     ecx, 512
    mov     eax, 0x83               ; present | writable | page-size (2 MiB)
    mov     edi, pd
.map_pd:
    mov     [edi], eax
    add     eax, 0x200000           ; next 2 MiB frame
    add     edi, 8                  ; next 64-bit entry
    loop    .map_pd

    ; -- Enable PAE ---------------------------------------------------------
    mov     eax, cr4
    or      eax, 1 << 5             ; CR4.PAE
    mov     cr4, eax

    ; -- Point CR3 at our PML4 ---------------------------------------------
    mov     eax, pml4
    mov     cr3, eax

    ; -- Set EFER.LME (long mode enable) -----------------------------------
    mov     ecx, 0xC0000080         ; IA32_EFER
    rdmsr
    or      eax, 1 << 8             ; EFER.LME
    wrmsr

    ; -- Enable paging; we are now in compatibility mode --------------------
    mov     eax, cr0
    or      eax, 1 << 31            ; CR0.PG
    mov     cr0, eax

    ; -- Load the 64-bit GDT and jump into a 64-bit code segment ------------
    lgdt    [gdt64.pointer]
    jmp     gdt64.code:long_mode_start

.no_long_mode:
    ; Print "ERR: no long mode" straight to the VGA buffer and stop.
    mov     esi, no_lm_msg
    mov     edi, 0xB8000
.print:
    lodsb
    test    al, al
    jz      .stop
    mov     ah, 0x0F                ; white on black
    mov     [edi], ax
    add     edi, 2
    jmp     .print
.stop:
    cli
    hlt
    jmp     .stop

; ---------------------------------------------------------------------------
; 64-bit entry point.
; ---------------------------------------------------------------------------
[BITS 64]
long_mode_start:
    ; In long mode the data segment registers are largely ignored, but they
    ; must not hold a stale 32-bit selector.
    mov     ax, gdt64.data
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax

    mov     rsp, stack_top

    ; System V AMD64 ABI: first integer argument goes in RDI.
    xor     rdi, rdi
    mov     edi, [mboot_ptr]        ; struct multiboot *mboot_ptr
    call    main

.hang:
    hlt                             ; halt rather than spin, so we idle politely
    jmp     .hang

; ---------------------------------------------------------------------------
section .rodata
no_lm_msg:
    db "ERR: CPU does not support long mode", 0

align 8
gdt64:
    dq 0                                    ; null descriptor
.code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53) ; code: executable, type=code/data,
                                             ; present, 64-bit
.data: equ $ - gdt64
    dq (1<<44) | (1<<47) | (1<<41)           ; data: type=code/data, present, writable
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

; ---------------------------------------------------------------------------
section .bss
alignb 4096
pml4:
    resb 4096
pdpt:
    resb 4096
pd:
    resb 4096
mboot_ptr:
    resd 1
alignb 16
[GLOBAL stack_bottom]
stack_bottom:
    resb 16384                      ; 16 KiB kernel stack
[GLOBAL stack_top]
stack_top:
