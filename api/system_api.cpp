#include <tinyos/api/system_api.hpp>
#include <tinyos/drivers/keyboard.hpp>
#include <tinyos/drivers/vga.hpp>
#include <tinyos/shell/shell.hpp>

namespace tinyos::api
{
    void print(const char* text)
    {
        drivers::vga::write(text);
    }

    void clear_screen()
    {
        drivers::vga::clear();
    }

    void get_input(char* buffer, size_t max_length)
    {
        drivers::keyboard::read_line(buffer, max_length);
    }

    void execute_command(const char* input)
    {
        shell::execute(input);
    }
}
