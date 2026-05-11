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
        __kernel_text_start = .;
        *(.text*)
        __kernel_text_end = .;
    }

    .rodata ALIGN(4096) :
    {
        __kernel_rodata_start = .;
        *(.rodata*)
        __kernel_rodata_end = .;
    }

    .data ALIGN(4096) :
    {
        __kernel_data_start = .;
        *(.data*)
        __kernel_data_end = .;
    }

    .bss ALIGN(4096) :
    {
        __kernel_bss_start = .;
        *(COMMON)
        *(.bss*)
        __kernel_bss_end = .;
    }

    __kernel_end = .;
}
