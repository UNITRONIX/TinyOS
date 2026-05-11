; TinyOS boot entry for GRUB Multiboot v1.

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
