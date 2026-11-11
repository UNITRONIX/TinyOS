; TinyOS x86_64 entry path.
;
; GRUB hands control to us in 32-bit protected mode with a Multiboot2
; information block. This file builds a minimal set of page tables, switches the
; CPU into long mode and calls into the 64-bit C++ entry point.
;
; Memory model at the end of this file: the first 1 GiB of physical memory is
; identity mapped using 2 MiB pages, and the kernel keeps running from that
; identity map. A higher-half kernel plus a full direct physical map is the next
; migration step (see docs/x86_64-migration.md, step A3); doing it here would
; require splitting the low boot stub into a separately linked section before
; there is anything running to debug it with.

MB2_MAGIC    equ 0xE85250D6
MB2_ARCH     equ 0
MB2_LENGTH   equ mb2_end - mb2_start
MB2_CHECKSUM equ -(MB2_MAGIC + MB2_ARCH + MB2_LENGTH)

PAGE_PRESENT equ 1 << 0
PAGE_WRITE   equ 1 << 1
PAGE_HUGE    equ 1 << 7

section .multiboot
align 8
mb2_start:
    dd MB2_MAGIC
    dd MB2_ARCH
    dd MB2_LENGTH
    dd MB2_CHECKSUM
    ; End tag: type 0, flags 0, size 8.
    dw 0
    dw 0
    dd 8
mb2_end:

section .bss
align 4096
pml4:       resq 512
pdpt:       resq 512
pd:         resq 512

align 16
stack_bottom:
    resb 32768
stack_top:

section .text
bits 32

global _start
extern kernel64_main

_start:
    cli

    ; A Multiboot compliant loader passes a magic value in eax and the
    ; information block address in ebx. Keep ebx for the 64-bit entry point.
    mov esp, stack_top
    mov ebp, esp

    ; Zero the page tables. The .bss is zeroed by the loader in practice, but
    ; relying on that silently produces bogus entries if a loader ever does not.
    mov edi, pml4
    mov ecx, (512 * 3 * 2)
    xor eax, eax
    rep stosd

    ; PML4[0] -> PDPT, present + writable.
    mov eax, pdpt
    or eax, PAGE_PRESENT | PAGE_WRITE
    mov [pml4], eax
    mov dword [pml4 + 4], 0

    ; PDPT[0] -> PD, present + writable.
    mov eax, pd
    or eax, PAGE_PRESENT | PAGE_WRITE
    mov [pdpt], eax
    mov dword [pdpt + 4], 0

    ; Fill the page directory with 512 huge (2 MiB) pages: 1 GiB total.
    xor ecx, ecx
.map_2m:
    mov eax, ecx
    shl eax, 21                     ; index * 2 MiB
    or eax, PAGE_PRESENT | PAGE_WRITE | PAGE_HUGE
    mov [pd + ecx * 8], eax
    mov dword [pd + ecx * 8 + 4], 0
    inc ecx
    cmp ecx, 512
    jne .map_2m

    mov eax, pml4
    mov cr3, eax

    ; Enable PAE. Long mode requires it regardless of the page-table granularity
    ; we actually use.
    mov eax, cr4
    or eax, 1 << 5                  ; CR4.PAE
    mov cr4, eax

    ; Enable long mode in EFER. Paging is still off at this point.
    mov ecx, 0xC0000080             ; IA32_EFER
    rdmsr
    or eax, 1 << 8                  ; EFER.LME
    wrmsr

    ; Enable paging and protection together: entering long mode requires both.
    mov eax, cr0
    or eax, 0x80000001              ; CR0.PG | CR0.PE
    mov cr0, eax

    lgdt [gdt64_pointer]
    jmp 0x08:long_mode_entry

bits 64

long_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rsp, stack_top
    xor rbp, rbp

    ; `ebx` still holds the Multiboot2 information address from the 32-bit path.
    mov edi, ebx
    call kernel64_main

.hang:
    cli
    hlt
    jmp .hang

section .data
align 8
global gdt64
global gdt64_pointer
gdt64:
    dq 0x0000000000000000           ; null
    dq 0x00AF9A000000FFFF           ; 0x08 kernel code, 64-bit
    dq 0x00CF92000000FFFF           ; 0x10 kernel data
    dq 0x00AFFA000000FFFF           ; 0x18 user code, 64-bit
    dq 0x00CFF2000000FFFF           ; 0x20 user data
gdt64_end:

gdt64_pointer:
    dw gdt64_end - gdt64 - 1
    dq gdt64
