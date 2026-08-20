bits 32

section .text

global gdt_flush
global tss_flush
global enter_user_mode
global launch_user_and_wait
global return_from_user_exit

section .bss
align 4
g_saved_ebx: resd 1
g_saved_esi: resd 1
g_saved_edi: resd 1
g_saved_ebp: resd 1
g_saved_esp: resd 1
g_saved_eip: resd 1

section .text

; void gdt_flush(const GdtDescriptor* descriptor)
gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.reload_cs
.reload_cs:
    ret

; void tss_flush(uint16_t selector)
tss_flush:
    mov ax, [esp + 4]
    ltr ax
    ret

; uint32_t launch_user_and_wait(uint32_t entry, uint32_t user_stack_top,
;                               uint16_t user_data_sel, uint16_t user_code_sel)
; Saves kernel return frame, enters ring 3, returns exit code from return_from_user_exit.
launch_user_and_wait:
    mov eax, [esp]
    mov [g_saved_eip], eax
    mov [g_saved_ebx], ebx
    mov [g_saved_esi], esi
    mov [g_saved_edi], edi
    mov [g_saved_ebp], ebp
    lea eax, [esp + 4]
    mov [g_saved_esp], eax

    mov eax, [esp + 4]
    mov ebx, [esp + 8]
    movzx ecx, word [esp + 12]
    movzx edx, word [esp + 16]
    jmp user_mode_enter

; void enter_user_mode(uint32_t entry, uint32_t user_stack_top,
;                      uint16_t user_data_sel, uint16_t user_code_sel)
enter_user_mode:
    mov eax, [esp + 4]
    mov ebx, [esp + 8]
    movzx ecx, word [esp + 12]
    movzx edx, word [esp + 16]

user_mode_enter:
    cli
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx

    push ecx
    push ebx
    pushf
    pop esi
    or esi, 0x200
    push esi
    push edx
    push eax
    iretd

; void return_from_user_exit(uint32_t code) — noreturn into launch_user_and_wait caller
return_from_user_exit:
    mov eax, [esp + 4]
    mov bx, 0x10
    mov ds, bx
    mov es, bx
    mov fs, bx
    mov gs, bx
    mov ss, bx
    mov ebx, [g_saved_ebx]
    mov esi, [g_saved_esi]
    mov edi, [g_saved_edi]
    mov ebp, [g_saved_ebp]
    mov esp, [g_saved_esp]
    push dword [g_saved_eip]
    ret
