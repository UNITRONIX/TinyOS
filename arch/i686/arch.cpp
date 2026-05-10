#include <tinyos/arch/hal.hpp>
#include <tinyos/arch/io.hpp>

namespace tinyos::arch
{
    void initialize()
    {
        // Architecture-specific initialization placeholder.
    }

    [[noreturn]] void halt()
    {
        for (;;)
        {
            asm volatile ("hlt");
        }
    }

    [[noreturn]] void reboot()
    {
        while ((io::inb(0x64) & 0x02) != 0)
        {
        }

        io::outb(0x64, 0xFE);
        halt();
    }
}
