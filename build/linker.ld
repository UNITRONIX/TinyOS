ENTRY(start)

SECTIONS
{
    . = 1M;
    __kernel_start = .;

    .multiboot ALIGN(4) :
    {
        *(.multiboot)
    }

    .text ALIGN(4096) :
    {
        *(.text*)
    }

    .rodata ALIGN(4096) :
    {
        *(.rodata*)
    }

    .data ALIGN(4096) :
    {
        *(.data*)
    }

    .bss ALIGN(4096) :
    {
        *(COMMON)
        *(.bss*)
    }

    __kernel_end = .;
}
