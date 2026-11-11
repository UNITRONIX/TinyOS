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
    constexpr uint32_t SyscallWrite = 0;
    constexpr uint32_t SyscallExit = 5;

    // Embedded ring-3 program. It has two jobs:
    //   1. prove the syscall path works (a successful write);
    //   2. prove the kernel rejects a hostile pointer (a write whose buffer
    //      points into kernel memory must come back as an error, not as a
    //      kernel memory read).
    //
    // Offsets of the patched immediates are named below so the patch code cannot
    // drift out of sync with the byte pattern.
    alignas(16) unsigned char g_embedded_init[] = {
        0xB8, 0x00, 0x00, 0x00, 0x00,       // mov eax, SyscallWrite
        0xBB, 0x00, 0x00, 0x00, 0x00,       // mov ebx, message (patched)
        0xB9, 0x08, 0x00, 0x00, 0x00,       // mov ecx, 8
        0xCD, 0x80,                         // int 0x80   -> expect ok
        0xB8, 0x00, 0x00, 0x00, 0x00,       // mov eax, SyscallWrite
        0xBB, 0x00, 0x00, 0x00, 0x00,       // mov ebx, kernel pointer (patched)
        0xB9, 0x08, 0x00, 0x00, 0x00,       // mov ecx, 8
        0xCD, 0x80,                         // int 0x80   -> expect rejection
        0x89, 0xC3,                         // mov ebx, eax (exit code = status)
        0xB8, 0x05, 0x00, 0x00, 0x00,       // mov eax, SyscallExit
        0xCD, 0x80,                         // int 0x80
        0xF4,                               // hlt
        'i', 'n', 'i', 't', '-', 'o', 'k', '\n'
    };

    // Byte offsets inside g_embedded_init. Each `mov reg, imm32` is 5 bytes, so
    // the immediates sit at opcode_offset + 1:
    //   0   B8 + imm  ->  message pointer immediate at 1
    //   5   BB + imm  ->  message pointer immediate at 6
    //   10  B9 + imm  ->  length 8
    //   15  CD 80
    //   17  B8 + imm  ->  syscall number
    //   22  BB + imm  ->  kernel pointer immediate at 23
    //   27  B9 + imm
    //   32  CD 80
    //   34  89 C3
    //   36  B8 + imm
    //   41  CD 80
    //   43  F4
    //   44  message bytes
    constexpr size_t OffsetMessageImmediate = 6;
    constexpr size_t OffsetKernelPointerImmediate = 23;
    constexpr size_t OffsetMessageBytes = 44;
    static_assert(OffsetMessageBytes < sizeof(g_embedded_init), "Embedded init message offset is outside the program.");

    void write_u32(unsigned char* destination, uint32_t value)
    {
        destination[0] = static_cast<unsigned char>(value & 0xFF);
        destination[1] = static_cast<unsigned char>((value >> 8) & 0xFF);
        destination[2] = static_cast<unsigned char>((value >> 16) & 0xFF);
        destination[3] = static_cast<unsigned char>((value >> 24) & 0xFF);
    }

    void patch_and_copy_init(uint32_t kernel_pointer)
    {
        const uint32_t message_addr = static_cast<uint32_t>(InitUserBase + OffsetMessageBytes);
        write_u32(&g_embedded_init[OffsetMessageImmediate], message_addr);
        write_u32(&g_embedded_init[OffsetKernelPointerImmediate], kernel_pointer);

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

        // Point the guest's second syscall at kernel memory. It must be rejected
        // by the user-pointer validator; if it were accepted, the kernel would
        // read its own memory into the VGA console on behalf of ring 3.
        patch_and_copy_init(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_init_launched)));

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

        // The guest exits with the status of its attempted kernel-pointer write.
        // A rejected buffer returns a negative syscall status; a zero would mean
        // the validator let a ring-3 process read kernel memory.
        if (code == 0)
        {
            tinyos::kernel::klog::write_line(
                tinyos::kernel::klog::Level::Error,
                "Ring-3 init: kernel pointer was accepted as a user buffer.");
            return false;
        }

        tinyos::kernel::klog::write_line(
            tinyos::kernel::klog::Level::Info,
            "Ring-3 init rejected kernel pointer as expected (user buffer validation active).");
        tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Ring-3 init exited cleanly.");
        return true;
    }
}
