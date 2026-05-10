#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/user/transition.hpp>

namespace
{
    bool g_ready = false;
    constexpr uint32_t SyscallVector = 0x80;
    constexpr uint16_t UserCodeSelector = 0x1B;
    constexpr uint16_t UserDataSelector = 0x23;
    constexpr uint32_t UserStackAlignment = 16;
}

namespace tinyos::kernel::user::transition
{
    void initialize()
    {
        g_ready = true;
        kernel::klog::write_line(kernel::klog::Level::Info, "User transition scaffold initialized.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    uint32_t syscall_vector()
    {
        return SyscallVector;
    }

    uint16_t user_code_selector()
    {
        return UserCodeSelector;
    }

    uint16_t user_data_selector()
    {
        return UserDataSelector;
    }

    uint32_t user_stack_alignment()
    {
        return UserStackAlignment;
    }
}
