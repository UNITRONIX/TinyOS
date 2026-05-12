#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::provision::image
{
    enum class Phase : uint32_t
    {
        ProjectWorkspace,
        ProvisionConfig,
        DeviceVariant,
        ProjectApi,
        ResourceDiagnostics,
        TerminalExperience,
        ApplicationBundle,
        SystemProfile,
        ImageManifest,
        ImageBuild,
        Signing,
        Encryption,
        RemoteTransport,
        TargetVerification,
        Rollback
    };

    enum class State : uint32_t
    {
        ReadyContract,
        HostToolPlanned,
        KernelPlanned
    };

    enum class TrustLevel : uint32_t
    {
        Development,
        Manifested,
        Signed,
        Encrypted,
        Verified
    };

    struct Step
    {
        const char* name;
        const char* tool;
        const char* purpose;
        Phase phase;
        State state;
        TrustLevel trust_level;
        bool host_side;
        bool requires_key;
        bool remote_operation;
    };

    void initialize();
    bool is_ready();
    size_t step_count();
    size_t ready_contract_count();
    size_t host_tool_planned_count();
    size_t kernel_planned_count();
    size_t key_step_count();
    size_t remote_step_count();
    const Step* at(size_t index);
    const Step* find_step(const char* name);
    bool validation_self_test();
    const char* phase_name(Phase phase);
    const char* state_name(State state);
    const char* trust_level_name(TrustLevel trust_level);
}