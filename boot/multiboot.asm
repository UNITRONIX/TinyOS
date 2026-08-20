; TinyOS boot entry for GRUB Multiboot v1 (+ Multiboot2 header for UEFI GRUB).

MBALIGN  equ 1 << 0
MEMINFO  equ 1 << 1
GRAPHICS equ 1 << 2
%ifdef TINYOS_GRAPHICAL_BOOT
FLAGS    equ MBALIGN | MEMINFO | GRAPHICS
%else
FLAGS    equ MBALIGN | MEMINFO
%endif
MAGIC    equ 0x1BADB002
CHECKSUM equ -(MAGIC + FLAGS)

MB2_MAGIC    equ 0xE85250D6
MB2_ARCH     equ 0
MB2_LENGTH   equ multiboot2_end - multiboot2_start
MB2_CHECKSUM equ -(MB2_MAGIC + MB2_ARCH + MB2_LENGTH)

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM
%ifdef TINYOS_GRAPHICAL_BOOT
    dd 0
    dd 1024
    dd 768
    dd 32
%endif

align 8
multiboot2_start:
    dd MB2_MAGIC
    dd MB2_ARCH
    dd MB2_LENGTH
    dd MB2_CHECKSUM
    dw 0
    dw 0
    dd 8
multiboot2_end:

section .text
bits 32
global start
extern kernel_main

start:
    cli
    mov esp, stack_top
    push ebx
    push eax
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:
