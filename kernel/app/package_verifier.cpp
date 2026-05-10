#include <stddef.h>
#include <stdint.h>

#include <tinyos/kernel/app/manifest.hpp>
#include <tinyos/kernel/app/package.hpp>
#include <tinyos/kernel/app/package_verifier.hpp>
#include <tinyos/kernel/app/runtime.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/security/trust.hpp>

namespace
{
    using tinyos::kernel::app::package::Package;
    using tinyos::kernel::app::package_verifier::Report;
    using tinyos::kernel::app::package_verifier::Verdict;

    bool g_ready = false;
    size_t g_checks_run = 0;
    size_t g_install_ready = 0;
    size_t g_blocked = 0;

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

    size_t string_length(const char* text)
    {
        if (text == nullptr)
        {
            return 0;
        }

        size_t length = 0;
        while (text[length] != '\0')
        {
            ++length;
        }

        return length;
    }

    bool has_suffix(const char* text, const char* suffix)
    {
        const size_t text_length = string_length(text);
        const size_t suffix_length = string_length(suffix);
        if (text_length < suffix_length)
        {
            return false;
        }

        return strings_equal(text + text_length - suffix_length, suffix);
    }

    const tinyos::kernel::app::runtime::Profile* find_runtime(const char* runtime_name)
    {
        for (size_t index = 0; index < tinyos::kernel::app::runtime::count(); ++index)
        {
            const auto* profile = tinyos::kernel::app::runtime::at(index);
            if (profile != nullptr && strings_equal(profile->name, runtime_name))
            {
                return profile;
            }
        }

        return nullptr;
    }

    Report make_report(const Package* package)
    {
        Report report;
        report.package = package;
        report.verdict = Verdict::PackageNotFound;
        report.extension_ok = false;
        report.runtime_ready = false;
        report.profile_ok = false;
        report.trust_store_ready = false;
        report.trusted_algorithm = false;
        report.signature_present = false;
        report.signature_ready = false;
        report.payload_hash_present = false;
        report.payload_ready = false;
        report.installable = false;
        report.launchable = false;
        return report;
    }

    void record_report(const Report& report)
    {
        if (report.installable)
        {
            ++g_install_ready;
        }
        else
        {
            ++g_blocked;
        }
    }
}

namespace tinyos::kernel::app::package_verifier
{
    void initialize()
    {
        g_ready = true;
        tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "TAPP package verifier initialized.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    Report verify(const Package* package)
    {
        ++g_checks_run;
        Report report = make_report(package);

        if (!g_ready || !package::is_ready())
        {
            report.verdict = Verdict::RegistryNotReady;
            record_report(report);
            return report;
        }

        if (package == nullptr)
        {
            report.verdict = Verdict::PackageNotFound;
            record_report(report);
            return report;
        }

        const auto* runtime_profile = find_runtime(package->runtime_name);
        report.extension_ok = has_suffix(package->package_name, package::extension());
        report.runtime_ready = runtime_profile != nullptr && runtime_profile->state == runtime::State::Ready;
        report.profile_ok = package::profile_matches(*package);
        report.trust_store_ready = tinyos::kernel::security::trust::is_ready();
        report.trusted_algorithm = tinyos::kernel::security::trust::algorithm_allowed_for_apps("rsa-sha256");
        report.signature_present = package->signature_state == package::SignatureState::Present ||
            package->signature_state == package::SignatureState::Verified ||
            package->signature_state == package::SignatureState::BuiltinTrusted;
        report.signature_ready = !package->signed_required ||
            package->signature_state == package::SignatureState::Verified ||
            package->signature_state == package::SignatureState::BuiltinTrusted;
        report.payload_hash_present = package->payload_state == package::PayloadState::HashListed ||
            package->payload_state == package::PayloadState::HashVerified ||
            package->payload_state == package::PayloadState::Builtin;
        report.payload_ready = package->payload_state == package::PayloadState::HashVerified ||
            package->payload_state == package::PayloadState::Builtin;
        report.launchable = package::launchable(*package);

        if (!report.extension_ok || package->state == package::State::Rejected)
        {
            report.verdict = Verdict::Rejected;
        }
        else if (!report.runtime_ready)
        {
            report.verdict = Verdict::RuntimeUnavailable;
        }
        else if (!report.profile_ok)
        {
            report.verdict = Verdict::ProfileMismatch;
        }
        else if (package->state == package::State::Planned)
        {
            report.verdict = Verdict::Planned;
        }
        else if (!report.trust_store_ready || !report.trusted_algorithm)
        {
            report.verdict = Verdict::TrustAnchorMissing;
        }
        else if (!report.signature_ready)
        {
            report.verdict = report.signature_present ? Verdict::SignatureUnverified : Verdict::SignatureRequired;
        }
        else if (!report.payload_ready)
        {
            report.verdict = report.payload_hash_present ? Verdict::PayloadUnverified : Verdict::PayloadRequired;
        }
        else if (package->trusted_system_package && report.launchable)
        {
            report.verdict = Verdict::TrustedBuiltin;
            report.installable = true;
        }
        else if (package->state == package::State::ValidManifest)
        {
            report.verdict = Verdict::ManifestVerified;
        }
        else if (report.launchable)
        {
            report.verdict = Verdict::InstallReady;
            report.installable = true;
        }
        else
        {
            report.verdict = Verdict::PayloadRequired;
        }

        record_report(report);
        return report;
    }

    Report verify_name(const char* package_or_app_name)
    {
        const auto* package = package::find_package(package_or_app_name);
        if (package == nullptr)
        {
            package = package::find_app(package_or_app_name);
        }

        return verify(package);
    }

    size_t checks_run()
    {
        return g_checks_run;
    }

    size_t install_ready_count()
    {
        return g_install_ready;
    }

    size_t blocked_count()
    {
        return g_blocked;
    }

    bool validation_self_test()
    {
        const Report shell = verify_name("system-shell.tapp");
        const Report example = verify_name("example-system-tool");
        const Report desktop = verify_name("desktop-shell.tapp");
        const Report missing = verify_name("missing.tapp");

        return g_ready &&
            package::is_ready() &&
            tinyos::kernel::security::trust::is_ready() &&
            tinyos::kernel::security::trust::has_app_package_anchor() &&
            shell.verdict == Verdict::TrustedBuiltin &&
            shell.installable &&
            example.verdict == Verdict::SignatureRequired &&
            example.profile_ok &&
            !example.installable &&
            desktop.verdict == Verdict::Planned &&
            !desktop.installable &&
            missing.verdict == Verdict::PackageNotFound &&
            blocked_count() >= 3;
    }

    const char* verdict_name(Verdict verdict)
    {
        switch (verdict)
        {
        case Verdict::TrustedBuiltin:
            return "trusted-builtin";
        case Verdict::InstallReady:
            return "install-ready";
        case Verdict::ManifestVerified:
            return "manifest-verified";
        case Verdict::SignatureRequired:
            return "signature-required";
        case Verdict::SignatureUnverified:
            return "signature-unverified";
        case Verdict::PayloadRequired:
            return "payload-required";
        case Verdict::PayloadUnverified:
            return "payload-unverified";
        case Verdict::Planned:
            return "planned";
        case Verdict::TrustAnchorMissing:
            return "trust-anchor-missing";
        case Verdict::RuntimeUnavailable:
            return "runtime-unavailable";
        case Verdict::ProfileMismatch:
            return "profile-mismatch";
        case Verdict::RegistryNotReady:
            return "registry-not-ready";
        case Verdict::PackageNotFound:
            return "package-not-found";
        case Verdict::Rejected:
            return "rejected";
        }

        return "unknown";
    }
}