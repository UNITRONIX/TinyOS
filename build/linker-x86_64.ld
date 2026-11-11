/* TinyOS x86_64 linker script.
 *
 * The kernel is linked at 1 MiB so the Multiboot2 loader can place it there
 * without relocating, and so the 32-bit entry stub can reach it through the
 * identity map it builds before switching to long mode.
 *
 * A higher-half layout (0xFFFFFFFF80000000) is the target of step A3 in
 * docs/x86_64-migration.md. Changing the base address now would require the boot
 * stub to relocate itself, which is worth doing only once there is a working
 * long-mode kernel to debug the relocation against.
 */

ENTRY(_start)

SECTIONS
{
    . = 1M;
    __kernel_start = .;

    .multiboot ALIGN(8) :
    {
        KEEP(*(.multiboot))
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

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
        *(.eh_frame*)
    }
}
