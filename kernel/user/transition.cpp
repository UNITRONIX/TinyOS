#include <tinyos/arch/gdt.hpp>
#include <tinyos/drivers/vga.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/memory/paging.hpp>
#include <tinyos/kernel/user/transition.hpp>

namespace
{
    bool g_ready = false;
    bool g_launch_supported = false;
    bool g_init_launched = false;
    bool g_init_exited = false;
    uint32_t g_init_exit_code = 0;

    constexpr uint32_t SyscallVector = 0x80;
    constexpr uint32_t UserStackAlignment = 16;
    constexpr const char* InitProcessName = "init";
    constexpr const char* InitEntryPath = "/system/init";
    constexpr uintptr_t InitUserBase = 0x00400000;
    constexpr uintptr_t InitUserStackTop = 0x00408000;
    constexpr size_t InitUserStackBytes = 16 * 1024;
    alignas(16) unsigned char g_embedded_init[] = {
        0xB8, 0x00, 0x00, 0x00, 0x00,       // mov eax, 0 (write)
        0xBB, 0x00, 0x00, 0x00, 0x00,       // mov ebx, msg (patched)
        0xB9, 0x08, 0x00, 0x00, 0x00,       // mov ecx, 8
        0xCD, 0x80,                         // int 0x80
        0xB8, 0x05, 0x00, 0x00, 0x00,       // mov eax, 5 (exit)
        0x31, 0xDB,                         // xor ebx, ebx
        0xCD, 0x80,                         // int 0x80
        0xF4,                               // hlt
        'i', 'n', 'i', 't', '-', 'o', 'k', '\n'
    };

    void patch_and_copy_init()
    {
        const uint32_t message_addr = static_cast<uint32_t>(InitUserBase + 27);
        g_embedded_init[6] = static_cast<unsigned char>(message_addr & 0xFF);
        g_embedded_init[7] = static_cast<unsigned char>((message_addr >> 8) & 0xFF);
        g_embedded_init[8] = static_cast<unsigned char>((message_addr >> 16) & 0xFF);
        g_embedded_init[9] = static_cast<unsigned char>((message_addr >> 24) & 0xFF);

        auto* destination = reinterpret_cast<unsigned char*>(InitUserBase);
        for (size_t index = 0; index < sizeof(g_embedded_init); ++index)
        {
            destination[index] = g_embedded_init[index];
        }
    }
}

namespace tinyos::kernel::user::transition
{
    void initialize()
    {
        g_ready = true;
        g_launch_supported = tinyos::arch::gdt::is_ready();
        g_init_launched = false;
        g_init_exited = false;
        g_init_exit_code = 0;
        kernel::klog::write_line(kernel::klog::Level::Info, "User transition initialized.");
        if (g_launch_supported)
        {
            kernel::klog::write_line(kernel::klog::Level::Info, "Ring-3 init launch supported.");
        }
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
        return tinyos::arch::gdt::is_ready() ? tinyos::arch::gdt::user_code_selector() : 0x1B;
    }

    uint16_t user_data_selector()
    {
        return tinyos::arch::gdt::is_ready() ? tinyos::arch::gdt::user_data_selector() : 0x23;
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
        return g_launch_supported;
    }

    bool initial_process_contract_ready()
    {
        return g_ready &&
            InitProcessName != nullptr &&
            InitEntryPath != nullptr &&
            InitUserStackBytes >= 4096 &&
            (InitUserStackTop % UserStackAlignment) == 0 &&
            g_launch_supported;
    }

    bool validation_self_test()
    {
        return g_ready &&
            SyscallVector == 0x80 &&
            (user_code_selector() & 0x3) == 0x3 &&
            (user_data_selector() & 0x3) == 0x3 &&
            UserStackAlignment == 16 &&
            initial_process_contract_ready();
    }

    void note_init_exit(uint32_t code)
    {
        g_init_exited = true;
        g_init_exit_code = code;
    }

    bool init_exited()
    {
        return g_init_exited;
    }

    uint32_t init_exit_code()
    {
        return g_init_exit_code;
    }

    bool launch_init()
    {
        if (!g_launch_supported || g_init_launched)
        {
            return false;
        }

        patch_and_copy_init();

        const uintptr_t user_region_bytes = InitUserStackTop - InitUserBase;
        const uint32_t user_flags =
            tinyos::kernel::memory::paging::PageFlagRead |
            tinyos::kernel::memory::paging::PageFlagWrite |
            tinyos::kernel::memory::paging::PageFlagUser |
            tinyos::kernel::memory::paging::PageFlagExecute;
        if (tinyos::kernel::memory::paging::update_mapping_flags_for_range(
                InitUserBase, user_region_bytes, user_flags) == 0)
        {
            tinyos::kernel::klog::write_line(
                tinyos::kernel::klog::Level::Warn,
                "Ring-3 init pages could not be marked user-accessible.");
            return false;
        }

        alignas(16) static uint8_t kernel_trap_stack[8192];
        tinyos::arch::gdt::set_kernel_stack(
            reinterpret_cast<uint32_t>(kernel_trap_stack + sizeof(kernel_trap_stack)));

        g_init_launched = true;
        tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Launching embedded ring-3 init.");
        const uint32_t code = launch_user_and_wait(
            static_cast<uint32_t>(InitUserBase),
            static_cast<uint32_t>(InitUserStackTop),
            tinyos::arch::gdt::user_data_selector(),
            tinyos::arch::gdt::user_code_selector());

        note_init_exit(code);
        tinyos::drivers::vga::write_line("Ring-3 init exited.");
        tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Ring-3 init exited cleanly.");
        return true;
    }
}
