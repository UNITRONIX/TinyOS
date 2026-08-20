bits 32

section .text

global syscall_stub
extern syscall_dispatch_entry

; int 0x80 entry. Args: eax=number, ebx=arg0, ecx=arg1, edx=arg2
; Returns value in eax.
syscall_stub:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; pusha layout at esp+16 (after 4 segment pushes):
    ; +0 edi +4 esi +8 ebp +12 esp +16 ebx +20 edx +24 ecx +28 eax
    mov eax, [esp + 44]
    mov ebx, [esp + 32]
    mov ecx, [esp + 40]
    mov edx, [esp + 36]

    push edx
    push ecx
    push ebx
    push eax
    call syscall_dispatch_entry
    add esp, 16

    mov [esp + 44], eax
    pop gs
    pop fs
    pop es
    pop ds
    popa
    iretd
