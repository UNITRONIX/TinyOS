#include <stddef.h>
#include <stdint.h>

#include <tinyos/kernel/app/launcher.hpp>
#include <tinyos/kernel/app/runtime.hpp>
#include <tinyos/kernel/klog.hpp>

namespace
{
    bool g_ready = false;
    size_t g_checks_run = 0;
    size_t g_allowed_count = 0;
    size_t g_denied_count = 0;

    bool capability_subset(uint32_t requested, uint32_t granted)
    {
        return (requested & granted) == requested;
    }

    tinyos::kernel::app::launcher::Status evaluate_profile(const tinyos::kernel::app::manifest::AppProfile& app, uint32_t requested_capabilities)
    {
        if (!tinyos::kernel::app::manifest::is_ready())
        {
            return tinyos::kernel::app::launcher::Status::ManifestNotReady;
        }

        if (app.state != tinyos::kernel::app::manifest::State::Ready)
        {
            return tinyos::kernel::app::launcher::Status::AppNotReady;
        }

        if (!capability_subset(requested_capabilities, app.requested_capabilities))
        {
            return tinyos::kernel::app::launcher::Status::CapabilityDenied;
        }

        if (!tinyos::kernel::app::manifest::capabilities_allowed(app))
        {
            return tinyos::kernel::app::launcher::Status::CapabilityDenied;
        }

        if (!tinyos::kernel::app::manifest::launchable(app))
        {
            return tinyos::kernel::app::launcher::Status::RuntimeNotReady;
        }

        return tinyos::kernel::app::launcher::Status::Allowed;
    }

    void record_status(tinyos::kernel::app::launcher::Status status)
    {
        ++g_checks_run;
        if (status == tinyos::kernel::app::launcher::Status::Allowed)
        {
            ++g_allowed_count;
        }
        else
        {
            ++g_denied_count;
        }
    }
}

namespace tinyos::kernel::app::launcher
{
    void initialize()
    {
        g_ready = true;
        g_checks_run = 0;
        g_allowed_count = 0;
        g_denied_count = 0;
        tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Application launch policy initialized.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    Status check(const char* app_name, uint32_t requested_capabilities)
    {
        if (!manifest::is_ready())
        {
            record_status(Status::ManifestNotReady);
            return Status::ManifestNotReady;
        }

        const auto* app = manifest::find_by_name(app_name);
        if (app == nullptr)
        {
            record_status(Status::AppNotFound);
            return Status::AppNotFound;
        }

        const Status status = evaluate_profile(*app, requested_capabilities);
        record_status(status);
        return status;
    }

    Status check_profile(const manifest::AppProfile& app, uint32_t requested_capabilities)
    {
        const Status status = evaluate_profile(app, requested_capabilities);
        record_status(status);
        return status;
    }

    size_t checks_run()
    {
        return g_checks_run;
    }

    size_t allowed_count()
    {
        return g_allowed_count;
    }

    size_t denied_count()
    {
        return g_denied_count;
    }

    bool validation_self_test()
    {
        const auto* shell = manifest::find_by_name("system-shell");
        const auto* web_gui = manifest::find_by_name("web-gui-host");
        return g_ready &&
            shell != nullptr &&
            web_gui != nullptr &&
            evaluate_profile(*shell, runtime::CapabilityConsole) == Status::Allowed &&
            evaluate_profile(*shell, runtime::CapabilityNetwork) == Status::CapabilityDenied &&
            evaluate_profile(*web_gui, runtime::CapabilityGui) == Status::AppNotReady;
    }

    const char* status_name(Status status)
    {
        switch (status)
        {
        case Status::Allowed:
            return "allowed";
        case Status::ManifestNotReady:
            return "manifest-not-ready";
        case Status::AppNotFound:
            return "app-not-found";
        case Status::AppNotReady:
            return "app-not-ready";
        case Status::RuntimeNotReady:
            return "runtime-not-ready";
        case Status::CapabilityDenied:
            return "capability-denied";
        }

        return "unknown";
    }
}