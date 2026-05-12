#include <stddef.h>
#include <stdint.h>

#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/provision/image.hpp>

namespace
{
    using tinyos::kernel::provision::image::Phase;
    using tinyos::kernel::provision::image::State;
    using tinyos::kernel::provision::image::Step;
    using tinyos::kernel::provision::image::TrustLevel;

    const Step g_steps[] = {
        { "project-workspace", "tinyos-image provision-init", "create isolated project folder and profile skeleton", Phase::ProjectWorkspace, State::HostToolPlanned, TrustLevel::Development, true, false, false },
        { "provision-config", "tinyos-image provision-config", "set encryption, API, remote access and UI defaults", Phase::ProvisionConfig, State::HostToolPlanned, TrustLevel::Manifested, true, false, false },
        { "device-variants", "tinyos-image provision-variant", "declare target device variants and resource budgets", Phase::DeviceVariant, State::HostToolPlanned, TrustLevel::Manifested, true, false, false },
        { "project-api", "tinyos-api manifest", "publish project API and capability surface", Phase::ProjectApi, State::KernelPlanned, TrustLevel::Manifested, false, false, false },
        { "resource-budget", "tinyos-image provision-resources", "estimate RAM, ROM and image footprint per variant", Phase::ResourceDiagnostics, State::HostToolPlanned, TrustLevel::Manifested, true, false, false },
        { "diagnostic-terminal", "provisionui", "show color TUI provisioning status and resource checks", Phase::TerminalExperience, State::KernelPlanned, TrustLevel::Development, false, false, false },
        { "terminal-colors", "terminaltheme", "color-coded terminal theme contract for panels and diagnostics", Phase::TerminalExperience, State::KernelPlanned, TrustLevel::Development, false, false, false },
        { "high-resolution-console", "videomode", "select text-grid fallback or framebuffer console variants", Phase::TerminalExperience, State::KernelPlanned, TrustLevel::Development, false, false, false },
        { "app-bundle", "tinyos-app", "package app binary, manifest and assets", Phase::ApplicationBundle, State::ReadyContract, TrustLevel::Manifested, true, false, false },
        { "system-profile", "system.profile", "declare target arch, files, apps and capabilities", Phase::SystemProfile, State::ReadyContract, TrustLevel::Manifested, true, false, false },
        { "encryption-default", "system.profile", "require encryption for provisioning image workflows", Phase::Encryption, State::ReadyContract, TrustLevel::Encrypted, true, false, false },
        { "image-manifest", "tinyos-image manifest", "record image inputs and hashes", Phase::ImageManifest, State::ReadyContract, TrustLevel::Manifested, true, false, false },
        { "image-build", "tinyos-image build", "compile TinyOS and assemble bootable image", Phase::ImageBuild, State::HostToolPlanned, TrustLevel::Manifested, true, false, false },
        { "image-sign", "tinyos-image sign", "sign image or manifest with developer key", Phase::Signing, State::HostToolPlanned, TrustLevel::Signed, true, true, false },
        { "image-encrypt", "tinyos-image encrypt", "encrypt image for a target or deployment profile", Phase::Encryption, State::HostToolPlanned, TrustLevel::Encrypted, true, true, false },
        { "remote-folder-access", "tinyos-image remote-access", "configure SSH/SFTP access to the project workspace", Phase::RemoteTransport, State::HostToolPlanned, TrustLevel::Encrypted, true, true, true },
        { "deploy-ssh", "tinyos-image deploy", "copy signed image through SSH or SFTP transport", Phase::RemoteTransport, State::HostToolPlanned, TrustLevel::Encrypted, true, true, true },
        { "target-verify", "provision-agent verify", "verify signature and image policy before activation", Phase::TargetVerification, State::KernelPlanned, TrustLevel::Verified, false, true, false },
        { "rollback-slot", "provision-agent rollback", "keep a previous bootable image slot", Phase::Rollback, State::KernelPlanned, TrustLevel::Verified, false, false, false },
        { "deploy-receipt", "tinyos-image receipt", "store a deployment receipt for audit and recovery", Phase::RemoteTransport, State::ReadyContract, TrustLevel::Verified, true, false, true }
    };

    bool g_ready = false;

    bool strings_equal(const char* left, const char* right)
    {
        if (left == nullptr || right == nullptr)
        {
            return left == right;
        }

        while (*left != '\0' && *right != '\0')
        {
            if (*left != *right)
            {
                return false;
            }

            ++left;
            ++right;
        }

        return *left == '\0' && *right == '\0';
    }
}

namespace tinyos::kernel::provision::image
{
    void initialize()
    {
        g_ready = true;
        tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Secure image provisioning manifest initialized.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    size_t step_count()
    {
        return sizeof(g_steps) / sizeof(g_steps[0]);
    }

    size_t ready_contract_count()
    {
        size_t ready = 0;
        for (size_t index = 0; index < step_count(); ++index)
        {
            if (g_steps[index].state == State::ReadyContract)
            {
                ++ready;
            }
        }

        return ready;
    }

    size_t host_tool_planned_count()
    {
        size_t planned = 0;
        for (size_t index = 0; index < step_count(); ++index)
        {
            if (g_steps[index].state == State::HostToolPlanned)
            {
                ++planned;
            }
        }

        return planned;
    }

    size_t kernel_planned_count()
    {
        size_t planned = 0;
        for (size_t index = 0; index < step_count(); ++index)
        {
            if (g_steps[index].state == State::KernelPlanned)
            {
                ++planned;
            }
        }

        return planned;
    }

    size_t key_step_count()
    {
        size_t keys = 0;
        for (size_t index = 0; index < step_count(); ++index)
        {
            if (g_steps[index].requires_key)
            {
                ++keys;
            }
        }

        return keys;
    }

    size_t remote_step_count()
    {
        size_t remote = 0;
        for (size_t index = 0; index < step_count(); ++index)
        {
            if (g_steps[index].remote_operation)
            {
                ++remote;
            }
        }

        return remote;
    }

    const Step* at(size_t index)
    {
        return index < step_count() ? &g_steps[index] : nullptr;
    }

    const Step* find_step(const char* name)
    {
        for (size_t index = 0; index < step_count(); ++index)
        {
            if (strings_equal(g_steps[index].name, name))
            {
                return &g_steps[index];
            }
        }

        return nullptr;
    }

    bool validation_self_test()
    {
        return g_ready &&
            step_count() >= 19 &&
            ready_contract_count() >= 5 &&
            host_tool_planned_count() >= 8 &&
            kernel_planned_count() >= 5 &&
            key_step_count() >= 5 &&
            remote_step_count() >= 3 &&
            find_step("project-workspace") != nullptr &&
            find_step("provision-config") != nullptr &&
            find_step("device-variants") != nullptr &&
            find_step("project-api") != nullptr &&
            find_step("resource-budget") != nullptr &&
            find_step("diagnostic-terminal") != nullptr &&
            find_step("system-profile") != nullptr &&
            find_step("encryption-default") != nullptr &&
            find_step("image-sign") != nullptr &&
            find_step("image-encrypt") != nullptr &&
            find_step("remote-folder-access") != nullptr &&
            find_step("deploy-ssh") != nullptr &&
            find_step("target-verify") != nullptr;
    }

    const char* phase_name(Phase phase)
    {
        switch (phase)
        {
        case Phase::ProjectWorkspace:
            return "project-workspace";
        case Phase::ProvisionConfig:
            return "provision-config";
        case Phase::DeviceVariant:
            return "device-variant";
        case Phase::ProjectApi:
            return "project-api";
        case Phase::ResourceDiagnostics:
            return "resource-diagnostics";
        case Phase::TerminalExperience:
            return "terminal-experience";
        case Phase::ApplicationBundle:
            return "application-bundle";
        case Phase::SystemProfile:
            return "system-profile";
        case Phase::ImageManifest:
            return "image-manifest";
        case Phase::ImageBuild:
            return "image-build";
        case Phase::Signing:
            return "signing";
        case Phase::Encryption:
            return "encryption";
        case Phase::RemoteTransport:
            return "remote-transport";
        case Phase::TargetVerification:
            return "target-verification";
        case Phase::Rollback:
            return "rollback";
        }

        return "unknown";
    }

    const char* state_name(State state)
    {
        switch (state)
        {
        case State::ReadyContract:
            return "ready-contract";
        case State::HostToolPlanned:
            return "host-tool-planned";
        case State::KernelPlanned:
            return "kernel-planned";
        }

        return "unknown";
    }

    const char* trust_level_name(TrustLevel trust_level)
    {
        switch (trust_level)
        {
        case TrustLevel::Development:
            return "development";
        case TrustLevel::Manifested:
            return "manifested";
        case TrustLevel::Signed:
            return "signed";
        case TrustLevel::Encrypted:
            return "encrypted";
        case TrustLevel::Verified:
            return "verified";
        }

        return "unknown";
    }
}