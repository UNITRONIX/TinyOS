bits 32

section .text

global arch_context_switch

; void arch_context_switch(tinyos::arch::context::Context* from, Context* to)
;
; Context layout (must match include/tinyos/arch/context.hpp):
;   +0  edi
;   +4  esi
;   +8  ebx
;   +12 ebp
;   +16 esp
;   +20 eip
;   +24 argument
;   +28 eflags
;   +32 magic

arch_context_switch:
    mov eax, [esp + 4]
    mov edx, [esp + 8]

    test eax, eax
    jz .load

    mov ecx, [esp]
    mov [eax + 20], ecx
    lea ecx, [esp + 4]
    mov [eax + 16], ecx
    mov [eax + 0], edi
    mov [eax + 4], esi
    mov [eax + 8], ebx
    mov [eax + 12], ebp

.load:
    mov eax, edx

    mov edi, [eax + 0]
    mov esi, [eax + 4]
    mov ebx, [eax + 8]
    mov ebp, [eax + 12]
    mov esp, [eax + 16]

    push dword [eax + 20]
    ret
