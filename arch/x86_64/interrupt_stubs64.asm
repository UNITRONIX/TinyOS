bits 64

section .text

global idt_load
global enable_interrupts
global disable_interrupts
global halt_forever
global interrupt_stub_table
extern interrupt_dispatch64

; void idt_load(const IdtDescriptor* descriptor)
idt_load:
    lidt [rdi]
    ret

; void enable_interrupts()
enable_interrupts:
    sti
    ret

; void disable_interrupts()
disable_interrupts:
    cli
    ret

; [[noreturn]] void halt_forever()
halt_forever:
    cli
.loop:
    hlt
    jmp .loop

; Frame layout produced by the CPU on an interrupt from 64-bit mode:
;   [rsp + 0]  rip
;   [rsp + 8]  cs
;   [rsp + 16] rflags
;   [rsp + 24] rsp
;   [rsp + 32] ss
;
; Vectors 8, 10, 11, 12, 13, 14, 17, 21, 29 and 30 push an error code before
; the frame, so those stubs must not synthesise one.
;
; Every stub pushes its own vector number, then falls into a shared body that
; saves the general registers and forwards a pointer to the CPU frame. A fault
; report without rip/cs/rflags is untriageable from serial output, which is the
; whole reason this path exists.

%macro ISR_NOERR 1
global isr_stub_%1
isr_stub_%1:
    push qword 0
    push qword %1
    jmp interrupt_common
%endmacro

%macro ISR_ERR 1
global isr_stub_%1
isr_stub_%1:
    push qword %1
    jmp interrupt_common
%endmacro

interrupt_common:
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Two slots (vector, error) sit above the 14 saved registers.
    mov rdi, [rsp + 14 * 8]         ; vector
    mov rsi, [rsp + 15 * 8]         ; error code
    lea rdx, [rsp + 16 * 8]         ; pointer to rip

    cld
    call interrupt_dispatch64

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    add rsp, 16                     ; vector and error code slots
    iretq

; 32 CPU exceptions. Only the listed ones push an error code.
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

; Hardware vectors 32..255 have no error code.
%assign vector 32
%rep 224
ISR_NOERR %[vector]
%assign vector vector+1
%endrep

section .data
align 8
; Resolved by the IDT initialiser. Entries point at their own stub so the
; handler knows which vector fired without consulting the APIC.
interrupt_stub_table:
%assign vector 0
%rep 256
    dq isr_stub_ %+ vector
%assign vector vector+1
%endrep
