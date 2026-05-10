#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/initrd/modules.hpp>
#include <tinyos/kernel/memory/frame_allocator.hpp>
#include <tinyos/kernel/memory/heap.hpp>
#include <tinyos/kernel/security/integrity.hpp>

namespace
{
    bool g_ready = false;
    size_t g_checks_run = 0;
}

namespace tinyos::kernel::security::integrity
{
    void initialize()
    {
        g_checks_run = 0;
        g_ready = true;
        kernel::klog::write_line(kernel::klog::Level::Info, "Integrity checks scaffold initialized.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    bool allocator_state_valid()
    {
        ++g_checks_run;
        return tinyos::kernel::memory::frames::reserved_frames() + tinyos::kernel::memory::frames::free_frames() == tinyos::kernel::memory::frames::total_frames()
            && tinyos::kernel::memory::frames::accounting_valid()
            && tinyos::kernel::memory::heap::used_bytes() + tinyos::kernel::memory::heap::free_bytes() == tinyos::kernel::memory::heap::total_bytes()
            && tinyos::kernel::memory::heap::state_valid();
    }

    bool boot_modules_valid()
    {
        ++g_checks_run;
        return tinyos::kernel::initrd::modules::is_ready()
            && tinyos::kernel::initrd::modules::validation_passed();
    }

    size_t checks_run()
    {
        return g_checks_run;
    }
}
