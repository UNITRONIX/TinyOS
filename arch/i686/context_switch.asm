bits 32

section .text

global arch_context_switch
global tinyos_context_entry_trampoline

; void arch_context_switch(Context* from, Context* to)
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
;   +32 trampoline
;   +36 magic
;
; ABI: the caller must invoke this with interrupts disabled and restore the
; target's interrupt state only after this routine returns. switch_context() in
; context.cpp owns that critical section; the entry trampoline below enables
; interrupts for freshly created tasks.

arch_context_switch:
    mov eax, [esp + 4]
    mov edx, [esp + 8]

    test eax, eax
    jz .load

    ; Store the resume address and the stack pointer just past it, so the
    ; resumed task returns from this call as if it had never left.
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

; Entry point for freshly prepared kernel contexts.
;
; The trampoline runs on the new task's own stack with interrupts still disabled
; by switch_context(). It enables them exactly once and then builds a regular
; cdecl call frame before entering the task function.
;
; Stack layout prepared by prepare_kernel_context (esp = S, 8-byte aligned):
;   [S + 0] task entry function
;   [S + 4] argument passed to the entry function
tinyos_context_entry_trampoline:
    mov edx, esp
    sti
    mov eax, [edx]
    mov ecx, [edx + 4]
    push dword .hang
    push ecx
    call eax

.hang:
    cli
    hlt
    jmp .hang
