#include <tinyos/arch/hal.hpp>
#include <tinyos/drivers/serial.hpp>
#include <tinyos/drivers/vga.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/panic.hpp>

namespace tinyos::kernel
{
    [[noreturn]] void panic(const char* message)
    {
        kernel::klog::write_line(kernel::klog::Level::Panic, message);
        drivers::vga::set_color(drivers::vga::Color::White, drivers::vga::Color::Red);
        drivers::vga::write_line("KERNEL PANIC");
        drivers::vga::write_line(message);
        drivers::serial::write_line("KERNEL PANIC");
        drivers::serial::write_line(message);
        arch::halt();
    }
}
