#pragma once

#include <stddef.h>
#include <stdint.h>

#include <tinyos/kernel/app/package.hpp>

namespace tinyos::kernel::app::package_verifier
{
    enum class Verdict : uint32_t
    {
        TrustedBuiltin,
        InstallReady,
        ManifestVerified,
        SignatureRequired,
        SignatureUnverified,
        PayloadRequired,
        PayloadUnverified,
        Planned,
        TrustAnchorMissing,
        RuntimeUnavailable,
        ProfileMismatch,
        RegistryNotReady,
        PackageNotFound,
        Rejected
    };

    struct Report
    {
        const package::Package* package;
        Verdict verdict;
        bool extension_ok;
        bool runtime_ready;
        bool profile_ok;
        bool trust_store_ready;
        bool trusted_algorithm;
        bool signature_present;
        bool signature_ready;
        bool payload_hash_present;
        bool payload_ready;
        bool installable;
        bool launchable;
    };

    void initialize();
    bool is_ready();
    Report verify(const package::Package* package);
    Report verify_name(const char* package_or_app_name);
    size_t checks_run();
    size_t install_ready_count();
    size_t blocked_count();
    bool validation_self_test();
    const char* verdict_name(Verdict verdict);
}