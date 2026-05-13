#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/user/transition.hpp>

namespace
{
    bool g_ready = false;
    constexpr uint32_t SyscallVector = 0x80;
    constexpr uint16_t UserCodeSelector = 0x1B;
    constexpr uint16_t UserDataSelector = 0x23;
    constexpr uint32_t UserStackAlignment = 16;
    constexpr const char* InitProcessName = "init";
    constexpr const char* InitEntryPath = "/system/init";
    constexpr uintptr_t InitUserStackTop = 0x00800000;
    constexpr size_t InitUserStackBytes = 16 * 1024;
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

    const char* init_process_name()
    {
        return InitProcessName;
    }

    const char* init_entry_path()
    {
        return InitEntryPath;
    }

    uintptr_t init_user_stack_top()
    {
        return InitUserStackTop;
    }

    size_t init_user_stack_bytes()
    {
        return InitUserStackBytes;
    }

    bool init_launch_supported()
    {
        return false;
    }

    bool initial_process_contract_ready()
    {
        return g_ready &&
            InitProcessName != nullptr &&
            InitEntryPath != nullptr &&
            InitUserStackBytes >= 4096 &&
            (InitUserStackTop % UserStackAlignment) == 0 &&
            !init_launch_supported();
    }

    bool validation_self_test()
    {
        return g_ready &&
            SyscallVector == 0x80 &&
            (UserCodeSelector & 0x3) == 0x3 &&
            (UserDataSelector & 0x3) == 0x3 &&
            UserStackAlignment == 16 &&
            initial_process_contract_ready();
    }
}
