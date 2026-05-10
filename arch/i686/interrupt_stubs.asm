bits 32

section .text

global idt_load
extern interrupt_dispatch
extern irq_dispatch

idt_load:
    mov eax, [esp + 4]
    lidt [eax]
    ret

%macro ISR_NOERR 1
global isr_stub_%1
isr_stub_%1:
    push dword 0
    push dword %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr_stub_%1
isr_stub_%1:
    push dword %1
    jmp isr_common
%endmacro

isr_common:
    pusha
    mov eax, [esp + 32]
    mov edx, [esp + 36]
    push edx
    push eax
    call interrupt_dispatch
    add esp, 8
    popa
    add esp, 8
    iretd

%macro IRQ 1
global irq_stub_%1
irq_stub_%1:
    push dword %1
    jmp irq_common
%endmacro

irq_common:
    pusha
    mov eax, [esp + 32]
    push eax
    call irq_dispatch
    add esp, 4
    popa
    add esp, 4
    iretd

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR 8
ISR_NOERR 9
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR 29
ISR_ERR 30
ISR_NOERR 31

IRQ 0
IRQ 1
IRQ 2
IRQ 3
IRQ 4
IRQ 5
IRQ 6
IRQ 7
IRQ 8
IRQ 9
IRQ 10
IRQ 11
IRQ 12
IRQ 13
IRQ 14
IRQ 15

global isr_stub_table
isr_stub_table:
    dd isr_stub_0
    dd isr_stub_1
    dd isr_stub_2
    dd isr_stub_3
    dd isr_stub_4
    dd isr_stub_5
    dd isr_stub_6
    dd isr_stub_7
    dd isr_stub_8
    dd isr_stub_9
    dd isr_stub_10
    dd isr_stub_11
    dd isr_stub_12
    dd isr_stub_13
    dd isr_stub_14
    dd isr_stub_15
    dd isr_stub_16
    dd isr_stub_17
    dd isr_stub_18
    dd isr_stub_19
    dd isr_stub_20
    dd isr_stub_21
    dd isr_stub_22
    dd isr_stub_23
    dd isr_stub_24
    dd isr_stub_25
    dd isr_stub_26
    dd isr_stub_27
    dd isr_stub_28
    dd isr_stub_29
    dd isr_stub_30
    dd isr_stub_31

global irq_stub_table
irq_stub_table:
    dd irq_stub_0
    dd irq_stub_1
    dd irq_stub_2
    dd irq_stub_3
    dd irq_stub_4
    dd irq_stub_5
    dd irq_stub_6
    dd irq_stub_7
    dd irq_stub_8
    dd irq_stub_9
    dd irq_stub_10
    dd irq_stub_11
    dd irq_stub_12
    dd irq_stub_13
    dd irq_stub_14
    dd irq_stub_15
